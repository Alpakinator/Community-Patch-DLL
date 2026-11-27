"""Marshalling classification, per-call policy, cost projection; assembles proxy/model/model.json (epic 2100).

Inputs : proxy/model/interfaces.json (extract_model.py), proxy/model/lua.json (extract_lua.py),
         proxy/model/slot-proof.json (verify_slots.py), research/sources/boundary-benchmark/results.json,
         the DLL fork's sources (which engine methods the DLL calls).
         proxy/model/consumers.tsv (issue 3303: who consumes a DLL -> engine call; a bad row stops the run, exit 2).
Outputs: proxy/model/model.json (the single machine-readable model), proxy/model/policy-table.tsv (one row per
         crossing method, for reading; last column `consumer`: exe / renderer / both for dll_to_engine rows, '-' for
         engine_to_dll rows), and a summary on stdout.
Usage  : python -I proxy/gen/classify.py --src <DLL fork source at the deps/dll.lock commit> [--out-dir DIR]
         python -I proxy/gen/classify.py --src <...> --check [--against-dir DIR]
  --src      a git checkout at the deps/dll.lock commit without tracked changes, or a plain copy of it (git archive) whose
             source fingerprint equals the lock's; checked by tools/x64-dll/build.py resolve_src, refused otherwise
             (exit 2). Without --src that function's default (the fork clone toolchain.FORK) is checked the same way.
             --unlocked accepts any git checkout (fork development; the output then does not match the committed files).
  --out-dir  where model.json and policy-table.tsv are written (default proxy/model).
  --inputs-dir  issue 3503: where interfaces.json, lua.json and slot-proof.json are read (default proxy/model; the
             model stage, proxy/gen/regen_model.py, passes the folder it regenerated them into). --lock / --commit:
             check --src against another lock file, or label a plain copy taken with --unlocked
             (toolchain.resolve_source).
  --check    issue 0125: write nothing in the repository; generate into a temporary folder and compare with the committed
             files in --against-dir (default proxy/model): policy-table.tsv byte for byte (the committed file's CRLF
             folded to LF, for core.autocrlf checkouts), model.json with its `generated_at` value masked. Each differing
             row is printed with its key. Exit 0 equal, 1 different, 2 refused input.
The fork source is read for one thing that reaches policy-table.tsv (which engine methods the DLL calls, scan_dll_usage)
and one that reaches only model.json (guid_counts). interfaces.json, lua.json and slot-proof.json are committed inputs.
Every rule below that is not a compiler fact is marked [I] in the output ('rule' fields).
"""
import argparse
import collections
import importlib.util
import json
import os
import re
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolchain as tc  # noqa: E402
from marshal_types import classify_method  # noqa: E402

MODEL_DIR = os.path.join(tc.PROXY, 'model')
BENCH = os.path.join(tc.REPO, r'research\sources\boundary-benchmark\results.json')
X64_BUILD = os.path.join(tc.REPO, 'tools', 'x64-dll', 'build.py')   # resolve_src: the deps/dll.lock check

# ---------------------------------------------------------------------------------------------- costs
# Epic 1200, research ipc-design-and-measurements.md 7.4 (2026-11-10, x86 client / x64 server, unpinned, loaded).
COST_US = {
    'sync': 0.44,          # empty call median 0.42-0.44; 3 x i32 in / 1 out 0.46
    'nested': 0.40,        # a nested callback adds 0.30-0.38 to its caller (0.74-0.82 vs 0.44); counted per call
    'post': 0.06,          # 0.058-0.069 per post in batches of 100-1000
    'wake': 15.0,          # first call after an idle gap > idleSpinUs pays one event wake: 8-10 us at 1 ms gaps,
                           # 14-20 us at 10 ms gaps; counted once per frame when the frame makes any call
}

# ---------------------------------------------------------------------------------------------- policy data
LIFETIME = {'QueryInterface', 'Destroy'}
# Engine->DLL getters that return a handle the proxy can compute without asking (no lookup, cannot fail) [I].
STATIC_HANDLE = re.compile(r'^(GetGame|GetPreGame|GetMap|GetGameAsynch|GetGameDeals|GetNetworkSyncronizer|'
                           r'GetNetMessageHandler|GetWorldBuilderMapLoader|GetScriptSystemUtility|'
                           r'GetDatabaseLoadUtility|GetPlayer|GetTeam|Get\w+Info|GetPlot|GetPlotByIndex|'
                           r'GetDiplomacyAI|GetDealAI)$')
# Values the handle itself carries: (kind|owner, id) for units and cities, plot index for plots [I].
HANDLE_INTRINSIC = {('ICvUnit1', 'GetID'), ('ICvUnit1', 'GetOwner'), ('ICvCity1', 'GetID'), ('ICvCity1', 'GetOwner'),
                    ('ICvCity1', 'GetIDInfo'), ('ICvUnit1', 'GetIDInfo'), ('ICvPlot1', 'GetPlotIndex'),
                    ('ICvPlayer1', 'GetID'), ('ICvPlot1', 'GetPosition')}
# Mirrors on the proxy side: name -> (interfaces, method names or '*getters', invalidation rule)
MIRRORS = {
    'static_info': (r'^ICv\w+Info1$', '*getters',
                    'Immutable after the database is cached (CvDllDatabaseUtility::CacheGameDatabaseData). '
                    'Snapshot once after boot and after every database reload; never invalidated in between.'),
    'context_static': (r'^ICvGameContext1$', re.compile(r'^(GetNum\w+|Get[A-Z_0-9]+|GetInfoTypeFor\w+|GetDefineSTRING|'
                                                          r'GetCivilizationInfoIndex|GetPlotDirection[XY]|GetCityPlot[XY]|'
                                                          r'GetDLLGUID|GetDLLVersion)$'),
                       'Database constants and counts: snapshot with static_info.'),
    'pregame': (r'^ICvPreGame[12]$', '*getters',
                'Write-through: every pregame setter passes through the proxy, which updates the mirror before it '
                'forwards the call. The server pushes a pregame snapshot after load/new game and whenever a '
                'server-side top-level call changed pregame (diff of the pregame record at the end of each call).'),
    'players': (r'^ICvPlayer[123]$', {'IsHuman', 'IsBarbarian', 'IsMinorCiv', 'IsAlive', 'IsEverAlive', 'IsTurnActive',
                                      'GetTeam', 'GetHandicapType', 'GetCivilizationType', 'GetLeaderType',
                                      'GetPlayerColor', 'GetArtStyleType', 'GetMinorCivType', 'GetCurrentEra',
                                      'IsSimultaneousTurns', 'HasTurnTimerExpired', 'GetName', 'GetNickName',
                                      'GetLeaderTypeKey'},
                'Per-player record (63 slots). The server diffs it at the end of every top-level call and posts the '
                'changed records; the proxy applies posts before returning to the EXE. HasTurnTimerExpired is a '
                'mirror only while GAMEOPTION_END_TURN_TIMER_ENABLED is off (it mutates state when on: sync then).'),
    'game': (r'^ICvGame[12]$', {'GetActivePlayer', 'GetActiveTeam', 'GetGameTurn', 'GetElapsedGameTurns', 'GetGameState',
                                'IsPaused', 'GetPausePlayer', 'IsFinalInitialized', 'IsNetworkMultiPlayer',
                                'IsGameMultiPlayer', 'IsHotSeat', 'IsPbem', 'IsTeamGame', 'IsOption', 'IsMPOption',
                                'GetGameSpeedType', 'GetHandicapType', 'GetMaxTurns', 'IsDebugMode', 'GetFOW',
                                'GetWinner', 'GetVictory', 'GetWinningTurn', 'GetNumGameTurnActive',
                                'CountHumanPlayersAlive', 'GetBarbarianCampImprovementType'},
             'Game record, same diff-and-post rule as players.'),
    'map_dims': (r'^ICvMap1$', {'NumPlots', 'PlotNum', 'GetGridWidth', 'GetGridHeight', 'IsWrapX', 'IsWrapY'},
                 'Fixed for a loaded map: snapshot at map init/load.'),
    'plots': (r'^ICvPlot1$', {'GetActiveFogOfWarMode', 'IsRiver', 'IsNEOfRiver', 'IsWOfRiver', 'IsNWOfRiver',
                              'GetRiverEFlowDirection', 'GetRiverSEFlowDirection', 'GetRiverSWFlowDirection', 'GetOwner',
                              'GetPlotType', 'IsWater', 'IsHills', 'IsOpenGround', 'IsMountain', 'GetTerrainType',
                              'GetFeatureType', 'GetResourceType', 'GetNumResource', 'GetImprovementType',
                              'IsImprovementPillaged', 'GetRouteType', 'IsRoutePillaged', 'IsRevealed',
                              'GetRevealedImprovementType', 'GetContinentType', 'GetTeam', 'IsCity', 'GetWorldAnchor',
                              'GetWorldAnchorData'},
              'Per-plot render record (10,240 plots on Huge). Pushed whole once after load; afterwards the server '
              'attaches the changed plot records to the notifications the DLL already sends when a plot changes '
              'visually (GameplayPlotStateChange, GameplayFOWChanged, setDirty bits). [I]: whether those '
              'notifications cover every field is unverified; until then the proxy also re-fetches a plot record '
              'on IsRevealed/FOW changes.'),
}
# DLL->engine
PLUMBING = {'IsGameCoreThread', 'HasGameCoreLock', 'GetGameCoreLock', 'ReleaseGameCoreLock', 'TryGameCoreLock',
            'IsGameCoreExecuting', 'GetScriptSystem', 'GetCacheFolderPath', 'GetLogFileMgr', 'getInterfaceIFace'}
SERVER_LOCAL_IFACES = {'ICvEngineScriptSystem1', 'ICvEngineScriptSystemArgs1', 'FIFileSystem', 'FILogFileMgr',
                       'FILogFile'}
FLAG_ONLY = {'setDirty', 'SetSpecificCityInfoDirty'}  # research 7.2: batchable [I]
# Where a used plumbing method is called, appended to its rule (one-off 9029 wrote this text into the table by hand at
# 76992d235; issue 0125 moved it here so that a regeneration keeps it). Appended only while the source scan finds the
# method used. [C: CvTradeClasses.cpp `if (!gDLL->TryGameCoreLock())` in UpdateTradeStats at 04b1211b8]
RULE_NOTES = {
    'TryGameCoreLock': 'called by the DLL since upstream fix 6cc672569 (pin bbfacca1a/76992d235, CvTradeClasses.cpp '
                       'UpdateTradeStats, UI thread only), issue 9029',
}
# Handlers known to call back into the DLL (headless traces, engine-contract.md 6.7; research section 7) [C/L].
REENTRANT_OUT = {
    'GameplayUnitTeleported': 'UpdateDeferredFog', 'GameplayUnitVisibility': 'UpdateDeferredFog',
    'GameplayUnitCreated': 'UpdateDeferredFog', 'GameplayUnitEmbark': 'UpdateDeferredFog',
    'GameplayUnitDestroyed': 'UpdateDeferredFog', 'GameplayCityCreated': 'UpdateDeferredFog',
    'GameplayEraChanged': 'CvDllUnit::SetupGraphical', 'AutoSave': 'the whole Write sequence',
    'isCitySelected': 'CvDllCity::GetIDInfo', 'IsUnitSelected': 'CvDllUnit::GetID/GetOwner',
}
SELECTION = {'GetHeadSelectedUnit', 'isCitySelection', 'IsUnitSelected', 'isCitySelected', 'getSelectionPlot',
             'shiftKey', 'altKey', 'ctrlKey', 'GetLengthSelectionList', 'IsSPAutoEndTurnEnabled'}
NET_SEND = re.compile(r'^(send\w+|netMessageDebugLog)$')
NOTIFY = re.compile(r'^(Gameplay\w+|Publish\w+|TradeVisuals_\w+|Set\w*Dirty|setDirty|AddMessage|AddPopup|'
                    r'AddPlayerMessage|AddCityMessage|AddUnitMessage|PlayUnitAnim\w*|DoTurn|ToggleAdvisor\w*)$')


def log(*a):
    print(*a, flush=True)


INPUTS_DIR = MODEL_DIR   # issue 3503: --inputs-dir (interfaces.json, lua.json, slot-proof.json of a regeneration)


def load(name):
    with open(os.path.join(INPUTS_DIR, name), encoding='utf-8') as f:
        return json.load(f)


# ---------------------------------------------------------------------------------------------- usage scan
def scan_dll_usage(engine_methods, fork):
    """Which engine-side methods the DLL source calls. Receivers per interface family [C: source text]:
    gDLL / GC.getDLLIFace() -> ICvEngineUtility*, DLLUI / GC.GetEngineUserInterface() -> ICvUserInterface*,
    any '->Name(' for the script system, args, file system and log names [I: by name].
    fork: the DLL fork source root (CvGameCoreDLL_Expansion2 under it is scanned; files in sorted order)."""
    texts = []
    for root, dirs, files in os.walk(os.path.join(fork, 'CvGameCoreDLL_Expansion2')):
        dirs.sort()
        for f in sorted(files):
            if f.endswith(('.cpp', '.h')) and not f.startswith('Boundary'):
                with open(os.path.join(root, f), encoding='latin-1') as fh:
                    texts.append(fh.read())
    text = '\n'.join(texts)
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    eng = set(re.findall(r'(?:gDLL|GC\.getDLLIFace\(\))\s*->\s*(\w+)\s*\(', text))
    ui = set(re.findall(r'(?:DLLUI|GC\.GetEngineUserInterface\(\))\s*->\s*(\w+)\s*\(', text))
    # Local pointers `ICvUserInterface2* pkIFace = ...` / `ICvEngineUtility1* pDLL = ...` (server-host.md 11.3: e.g.
    # IsModalStackEmpty at CvDiplomacyRequests.cpp:620, GameplayUnitCreated at CvUnit.cpp:1777 are called only
    # through them) [C: source text]. A local name counts from its declaration to the end of the enclosing function
    # (the next '}' at column 0).
    for fam, dst in (('ICvUserInterface', ui), ('ICvEngineUtility', eng)):
        for mdecl in re.finditer(r'%s\d\s*\*\s*(\w+)\s*=' % fam, text):
            end = text.find('\n}', mdecl.end())
            body = text[mdecl.end():end if end >= 0 else len(text)]
            dst |= set(re.findall(r'\b%s\s*->\s*(\w+)\s*\(' % re.escape(mdecl.group(1)), body))
    # The rest of the 11 methods server-host.md 11.3 lists: at the pinned commit their only calls are in commented-out
    # code (CvPlayer.cpp:34130 HasReceivedTurnAllComplete, :34283-34285 isPopupQueued / setForcePopup) [C: source
    # text]. Counted as used on the team-2300 coordinator's instruction (2026-11-10): every engine method is generated
    # in epic 2300 anyway, and a re-enabled call must not meet a model that says "unused".
    eng |= {'HasReceivedTurnAllComplete'}
    ui |= {'isPopupQueued', 'setForcePopup'}
    anyarrow = collections.Counter(re.findall(r'(?:->|\.)\s*(\w+)\s*\(', text))
    used = {}
    for iface, name in engine_methods:
        if iface.startswith('ICvEngineUtility'):
            used[(iface, name)] = name in eng
        elif iface.startswith('ICvUserInterface'):
            used[(iface, name)] = name in ui
        elif iface == 'CvDLLInterfaceIFaceBase':
            # reached only through gDLL->getInterfaceIFace(); DLLUI is an ICvUserInterface2*
            used[(iface, name)] = 'getInterfaceIFace' in eng and name in set(
                re.findall(r'getInterfaceIFace\(\)\s*->\s*(\w+)\s*\(', text))
        else:
            used[(iface, name)] = anyarrow.get(name, 0) > 0
    return used, len(eng), len(ui)


def guid_counts(fork):
    inc = os.path.join(fork, r'CvGameCoreDLLUtil\include')
    out = {}
    for f in sorted(os.listdir(inc)):
        if f.startswith(('CvDllInterfaces', 'ICvDLL')):
            with open(os.path.join(inc, f), encoding='latin-1') as fh:
                out[f] = len(re.findall(r'\bGUID\s+guid\w+\s*=', fh.read()))
    return out


# ---------------------------------------------------------------------------------------------- policy
def mirror_of(iface_chain, name, m):
    for mname, (iface_re, names, _) in MIRRORS.items():
        if not any(re.match(iface_re, c) for c in iface_chain):
            continue
        if names == '*getters':
            if m['ret'] != 'void' and not name.startswith(('Set', 'set', 'Create', 'Destroy', 'Reset', 'Init',
                                                             'Read', 'Write', 'Uninit')) \
                    and not any(p['type'].endswith('&') and not p['type'].startswith('const') for p in m['params']):
                return mname
        elif hasattr(names, 'match'):
            if names.match(name):
                return mname
        elif name in names:
            return mname
    return None


def policy_engine_to_dll(iface, m, verdict):
    name = m['name']
    chain_dc = m['declared_in']
    if name in LIFETIME:
        return 'local', 'local', 'lifetime: the proxy owns its wrapper objects'
    if (chain_dc, name) in HANDLE_INTRINSIC or (chain_dc.rstrip('23') + '1', name) in HANDLE_INTRINSIC:
        return 'local', 'local', 'carried by the handle'
    if verdict['ret_codec'] == 'handle' and STATIC_HANDLE.match(name) and \
            all(p['codec'] == 'i32' for p in verdict['params']):
        return 'local', 'local', 'static handle computed by the proxy [I]'
    mir = mirror_of(iface['chain'], name, m)
    if mir:
        if name == 'HasTurnTimerExpired':
            return 'mirror', 'mirror', 'mirror:%s (sync while the end-turn timer option is on)' % mir
        return 'mirror', 'mirror', 'mirror:%s' % mir
    if name == 'Update' and chain_dc.startswith('ICvGame'):
        return 'sync', 'sync', 'top-level core-channel call; the DLL runs the AI inside it'
    returns = verdict['ret_codec'] != 'void' or any(p['codec'].startswith('out') for p in verdict['params'])
    if returns:
        return 'sync', 'sync', 'returns a value'
    return 'sync', 'post_candidate', 'void command: post only after measurement 2106 shows its DLL code calls ' \
                                     'no client-side getter (PROTOCOL.md 4 rule 3)'


def policy_dll_to_engine(iface, m, verdict, used):
    name = m['name']
    if not used:
        return 'unused', 'unused', 'the DLL never calls it: server answers a default (as server/ does today)'
    if name in PLUMBING or iface['name'] in SERVER_LOCAL_IFACES or m['declared_in'] in SERVER_LOCAL_IFACES:
        why = 'plumbing: lock/thread role emulated by the server (PROTOCOL.md 2.3)' if name in PLUMBING else \
            'server-local service (script system with mod gameplay Lua in the server, files, logs) [decision MC-2]'
        if name in RULE_NOTES:
            why += '; ' + RULE_NOTES[name]
        return 'server_local', 'server_local', why
    if name in FLAG_ONLY:
        return 'post', 'post', 'flag-only notification (research 7.2)'
    if name in REENTRANT_OUT:
        return 'sync', 'sync', 'engine handler calls back into the DLL (%s)' % REENTRANT_OUT[name]
    if name in SELECTION:
        return 'sync', 'mirror', 'UI selection/input read; target: server-side snapshot pushed by the proxy at ' \
                                 'every selection change and at the start of each top-level call [I]'
    if NET_SEND.match(name):
        return 'post', 'post', 'net send: the engine answers later through Response* (engine-contract 1.4, 6.4)'
    returns = verdict['ret_codec'] != 'void' or any(p['codec'].startswith('out') for p in verdict['params'])
    if returns:
        return 'sync', 'sync', 'the DLL uses the returned value'
    if NOTIFY.match(name):
        return 'sync', 'post_candidate', 'notification: post once 2106 shows the handler does not re-enter'
    return 'sync', 'post_candidate', 'void call: post once 2106 shows the handler does not re-enter [I]'


# ---------------------------------------------------------------------------------------------- traffic
def traffic_index(bench, model):
    """method key 'Iface::name' -> {scenario: {thread: calls}} from the top-25 lists (only top 25 recorded)."""
    wrapper_ifaces = collections.defaultdict(set)
    for w, info in model['wrappers'].items():
        for b in info['bases']:
            wrapper_ifaces[w].add(b)
    by = {e['name']: e for e in model['interfaces']}

    def resolve(raw):
        cls, name = raw.split('::', 1)
        cands = []
        for b in (wrapper_ifaces.get(cls) or [cls]):
            e = by.get(b)
            if e:
                cands += [(m['declared_in'], name) for m in e['methods'] if m['name'] == name]
        return cands[0] if cands else (cls, name)

    idx = collections.defaultdict(lambda: collections.defaultdict(dict))
    for scn, data in bench.items():
        if not isinstance(data, dict) or 'in_main' not in data:
            continue
        for key in ('in_main', 'in_core', 'out_main', 'out_core'):
            for row in data[key]['top']:
                dc, name = resolve(row[0])
                idx['%s::%s' % (dc, name)][scn][key] = idx['%s::%s' % (dc, name)][scn].get(key, 0) + row[1]
    return idx


def project(bench, pol_by_key, lua_policy):
    """Calls and microseconds per frame (a_idle, b_panzoom) and per AI turn (f1, f2) under the policy."""
    out = {}
    for scn in ('a_idle', 'b_panzoom', 'c_cityview', 'f1_aiturn_350', 'f2_aiturn_351'):
        d = bench[scn]
        fps = d.get('fps') or d.get('fps_est')
        frames = d['seconds'] * fps if not scn.startswith('f') else None
        res = {}
        for which in ('now', 'target'):
            cnt = collections.Counter()
            for key, cls_default in (('in_main', 'in'), ('in_core', 'in'), ('out_main', 'out'), ('out_core', 'out')):
                thread = 'main' if key.endswith('main') else 'core'
                top = {}
                for row in d[key]['top']:
                    top[row[0]] = row[1]
                covered = 0
                for raw, n in top.items():
                    p = pol_by_key.get(raw)
                    if p is None:
                        continue
                    covered += n
                    cnt[(thread, p[which])] += n
                rest = d[key]['total'] - covered
                # remainder by class: each class's share of the remainder, default policy per class [I]
                classes = d[key]['classes']
                ctot = sum(classes.values()) or 1
                default = {'lifetime': 'local', 'static_handle': 'local', 'getter': 'sync', 'handle_lookup': 'sync',
                           'command': 'sync' if which == 'now' else 'post_candidate', 'plumbing': 'server_local',
                           'script': 'server_local', 'notify': 'sync' if which == 'now' else 'post_candidate'}
                # subtract the covered calls from their classes proportionally is not possible (top rows carry
                # a class in the source; use it)
                cov_cls = collections.Counter()
                for row in d[key]['top']:
                    if row[0] in pol_by_key:
                        cov_cls[row[3]] += row[1]
                for c, n in classes.items():
                    r = max(0, n - cov_cls.get(c, 0))
                    pol = default.get(c, 'sync')
                    if key.startswith('in_core') and pol == 'sync':
                        pol = 'nested'
                    if key.startswith('out_main') and pol == 'sync':
                        pol = 'nested'
                    cnt[(thread, pol)] += r
                _ = rest, ctot
            lua = d['lua_main']['total']
            cnt[('main', 'lua_sync')] += lua * lua_policy['sync_share']
            us = 0.0
            for (thread, pol), n in cnt.items():
                if pol in ('sync', 'lua_sync'):
                    us += n * COST_US['sync']
                elif pol == 'nested':
                    us += n * COST_US['nested']
                elif pol in ('post', 'post_candidate'):
                    us += n * COST_US['post']
            row = {'calls': {'%s/%s' % k: round(v) for k, v in sorted(cnt.items())}}
            if frames:
                pf = {k: v / frames for k, v in row['calls'].items()}
                sync_pf = sum(v for k, v in pf.items() if k.split('/')[1] in ('sync', 'lua_sync', 'nested'))
                wake = COST_US['wake'] if sync_pf >= 1 else COST_US['wake'] * sync_pf
                row['per_frame_calls'] = {k: round(v, 2) for k, v in pf.items()}
                row['per_frame_us'] = round(us / frames + wake, 1)
                row['frame_ms_at_fps'] = round(1000.0 / fps, 2)
            else:
                row['per_turn_s'] = round(us / 1e6, 3)
                row['core_busy_s'] = d.get('core_busy_s')
                row['wall_s'] = d['seconds']
            res[which] = row
        out[scn] = res
    return out


# ---------------------------------------------------------------------------------------------- main
CONSUMERS = os.path.join(MODEL_DIR, 'consumers.tsv')
CONSUMER_VALUES = ('exe', 'renderer', 'both')


class ConsumerError(Exception):
    pass


def load_consumers(path, d2e_methods):
    """Issue 3303: proxy/model/consumers.tsv -> {(interface, method): (consumer, reason)}.

    d2e_methods: the set of (declaring interface, method name) of every dll_to_engine method. Raises ConsumerError on a
    missing file, a bad header, a row without four fields, a consumer outside CONSUMER_VALUES, an empty reason, a method
    that is not a dll_to_engine method, or a duplicate row (every error of the file, one per line)."""
    if not os.path.isfile(path):
        raise ConsumerError('%s: missing' % path)
    out, errs, header = {}, [], None
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\r\n')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            cols = line.split('\t')
            if header is None:
                header = cols
                if cols != ['interface', 'method', 'consumer', 'reason']:
                    errs.append('line %d: header must be interface, method, consumer, reason' % n)
                continue
            if len(cols) != 4:
                errs.append('line %d: %d fields, 4 expected' % (n, len(cols)))
                continue
            iface, meth, cons, why = (c.strip() for c in cols)
            if cons not in CONSUMER_VALUES:
                errs.append('line %d: consumer %r not one of %s' % (n, cons, ', '.join(CONSUMER_VALUES)))
            if not why:
                errs.append('line %d: empty reason' % n)
            if (iface, meth) not in d2e_methods:
                errs.append('line %d: %s::%s is not a dll_to_engine method of the model' % (n, iface, meth))
            if (iface, meth) in out:
                errs.append('line %d: duplicate row for %s::%s' % (n, iface, meth))
            out[(iface, meth)] = (cons, why)
    if header is None:
        errs.append('no header line')
    if errs:
        raise ConsumerError('%s:\n  %s' % (path, '\n  '.join(errs)))
    return out


def consumer_of(consumers, direction, iface, name):
    """The consumer column: dll_to_engine rows from consumers.tsv, default 'exe'; engine_to_dll rows '-'."""
    if direction != 'dll_to_engine':
        return '-'
    return consumers.get((iface, name), ('exe', ''))[0]


def resolve_fork(src, unlocked, lock=None, commit=None):
    """--src checked against the lock. Without --lock and --commit: tools/x64-dll/build.py resolve_src (the
    deps/dll.lock check of issue 0125; without --src its default clone, checked). With --lock or --commit (issue 3503):
    toolchain.resolve_source (another lock file; --unlocked then also accepts a plain copy, labelled by --commit).
    Returns (fork root, info dict); raises ValueError with the refusal's one-line reason."""
    if lock or commit:
        try:
            info = tc.resolve_source(src, lock, unlocked, commit)
        except tc.SourceError as ex:
            raise ValueError(str(ex))
        return str(info['src']), info
    spec = importlib.util.spec_from_file_location('x64build', X64_BUILD)
    x64build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(x64build)
    try:
        info = x64build.resolve_src(src, unlocked)
    except x64build.SrcError as ex:
        raise ValueError(str(ex))
    return str(info['src']), info


def generate(fork, srcinfo, out_dir):
    """Write model.json and policy-table.tsv into out_dir from the committed inputs and the fork source."""
    t0 = time.time()
    model = load('interfaces.json')
    lua = load('lua.json')
    proof = load('slot-proof.json')
    with open(BENCH, encoding='utf-8') as f:
        bench = json.load(f)

    engine_methods = [(e['name'], m['name']) for e in model['interfaces'] if e['direction'] == 'dll_to_engine'
                      for m in e['methods']]
    used, n_gdll, n_ui = scan_dll_usage(engine_methods, fork)
    # issue 3303: the consumer column (who consumes a DLL -> engine call: exe, renderer, both)
    d2e_declared = {(m['declared_in'], m['name']) for e in model['interfaces'] if e['direction'] == 'dll_to_engine'
                    for m in e['methods']}
    try:
        consumers = load_consumers(CONSUMERS, d2e_declared)
    except ConsumerError as ex:
        log('classify: consumers.tsv refused: %s' % ex)
        return 2

    rows = []
    pol_by_raw = {}
    unmarsh = []
    seen = set()
    for e in model['interfaces']:
        for m in e['methods']:
            v = classify_method(m)
            if e['direction'] == 'engine_to_dll':
                now, target, why = policy_engine_to_dll(e, m, v)
            else:
                now, target, why = policy_dll_to_engine(e, m, v, used.get((e['name'], m['name']), False))
            m['marshal'] = {'params': [p['codec'] for p in v['params']], 'ret': v['ret_codec'],
                            'mechanical': v['mechanical'], 'reasons': v['reasons']}
            m['policy'] = {'now': now, 'target': target, 'rule': why}
            if e['direction'] == 'dll_to_engine':
                m['used_by_dll'] = used.get((e['name'], m['name']), False)
                c = consumers.get((m['declared_in'], m['name']))
                m['consumer'] = {'value': c[0] if c else 'exe',
                                 'reason': c[1] if c else 'default: not listed in proxy/model/consumers.tsv'}
            key = (m['declared_in'], m['name'], tuple(p['type'] for p in m['params']))
            if key in seen:
                continue
            seen.add(key)
            rows.append((e['direction'], m['declared_in'], m['name'], m, v))
            crossing = now not in ('local', 'server_local', 'unused')
            if not v['mechanical'] and (crossing or now == 'local' and m['name'] not in LIFETIME):
                unmarsh.append({'iface': m['declared_in'], 'method': m['name'], 'direction': e['direction'],
                                'signature': '%s %s(%s)%s' % (m['ret'], m['name'],
                                                              ', '.join(p['type'] for p in m['params']),
                                                              ' const' if m['const'] else ''),
                                'policy': now, 'reasons': v['reasons']})
    # raw benchmark names -> policy
    wrapper_ifaces = {w: info['bases'] for w, info in model['wrappers'].items()}
    by = {e['name']: e for e in model['interfaces']}
    for scn, d in bench.items():
        if not isinstance(d, dict) or 'in_main' not in d:
            continue
        for key in ('in_main', 'in_core', 'out_main', 'out_core'):
            for row in d[key]['top']:
                raw = row[0]
                cls, name = raw.split('::', 1)
                for b in wrapper_ifaces.get(cls, [cls]):
                    e = by.get(b)
                    ms = [m for m in e['methods'] if m['name'] == name] if e else []
                    if ms:
                        p = ms[0]['policy']
                        pol = (p['now'], p['target'])
                        if key in ('in_core', 'out_main'):
                            pol = tuple('nested' if x == 'sync' else x for x in pol)
                        pol_by_raw[raw] = {'now': pol[0], 'target': pol[1]}
                        break

    # unmarshallable categories
    cat = collections.Counter()
    for u in unmarsh:
        kinds = set()
        for r in u['reasons']:
            if 'stream' in r:
                kinds.add('stream')
            elif 'Lua' in r:
                kinds.add('lua')
            elif 'variadic' in r:
                kinds.add('variadic')
            elif 'STL' in r:
                kinds.add('stl')
            elif 'plain struct' in r:
                kinds.add('pod')
            elif 'object of the process' in r:
                kinds.add('engine_object')
            elif 'pointer into' in r:
                kinds.add('raw_pointer')
            else:
                kinds.add('other')
        u['categories'] = sorted(kinds)
        for k in kinds:
            cat[k] += 1

    # counts
    pol_counts = collections.Counter()
    pol_counts_target = collections.Counter()
    for direction, dc, name, m, v in rows:
        pol_counts[(direction, m['policy']['now'])] += 1
        pol_counts_target[(direction, m['policy']['target'])] += 1
    e2d = [e for e in model['interfaces'] if e['direction'] == 'engine_to_dll']
    d2e = [e for e in model['interfaces'] if e['direction'] == 'dll_to_engine']
    distinct = collections.Counter(direction for direction, *_ in rows)
    wrappers_methods = 0
    for w, bases in wrapper_ifaces.items():
        ms = set()
        for b in bases:
            if b in by:
                ms |= {(m['name'], tuple(p['type'] for p in m['params'])) for m in by[b]['methods']}
        wrappers_methods += len(ms)
    cvdllinterfaces_virtual = sum(e['own_virtuals'] for e in model['interfaces']
                                  if e['file'].endswith('CvDllInterfaces.h'))
    counts = {
        'interfaces_total': len(model['interfaces']),
        'interfaces_engine_to_dll': len(e2d),
        'interfaces_dll_to_engine': len(d2e),
        'guids_by_header': guid_counts(fork),
        'distinct_virtuals_total': len(rows),
        'distinct_virtuals_engine_to_dll': distinct['engine_to_dll'],
        'distinct_virtuals_dll_to_engine': distinct['dll_to_engine'],
        'virtuals_declared_in_CvDllInterfaces_h': cvdllinterfaces_virtual,
        'wrapper_classes': len(wrapper_ifaces),
        'wrapper_methods_incl_inherited': wrappers_methods,
        'dll_to_engine_methods_used': sum(1 for k, x in used.items() if x and (k[0], k[1]) in
                                          {(r[1], r[2]) for r in rows if r[0] == 'dll_to_engine'}),
        'gDLL_distinct_names': n_gdll,
        'DLLUI_distinct_names': n_ui,
        'host_exports': 22,
        'lua_methods_compiled': lua['counts']['compiled_registrations_x86'],
        'lua_methods_distinct_incl_fractal': lua['counts']['methods_total_in_json'],
    }
    used_by_iface = collections.Counter()
    for direction, dc, name, m, v in rows:
        if direction == 'dll_to_engine' and m.get('used_by_dll'):
            used_by_iface[dc] += 1
    counts['dll_to_engine_used_by_iface'] = dict(sorted(used_by_iface.items()))

    # Lua policy (decision: hybrid; see research proxy-design.md section 6)
    lua_sum = lua['summary']
    lua_policies = {}
    for scen in ('f1_aiturn_350', 'f2_aiturn_351', 'b_panzoom'):
        t = lua_sum['traffic'].get(scen, {})
        lua_policies[scen] = {'top10_share': t.get('top10_share'), 'cacheable_share_recorded': t.get('cacheable_share')}
    costs = {}
    for name, sync_share in (('lua_all_proxied', 1.0), ('lua_hybrid_top10_mirrored', None)):
        if sync_share is None:
            # the top-10 hot read methods are answered from a proxy mirror; the rest are proxied calls
            sh = 1.0 - (lua_sum['traffic']['f1_aiturn_350']['top10_share'] +
                        lua_sum['traffic']['f2_aiturn_351']['top10_share']) / 2
        else:
            sh = sync_share
        costs[name] = project(bench, pol_by_raw, {'sync_share': sh})
        costs[name]['lua_sync_share'] = round(sh, 3)

    exports = ['FJobSetAddJobImpl', 'FJobSetInitImpl', 'FJobSetUninitImpl', 'FJobSetWaitImpl', 'FSBAAllocate',
               'FSBADeallocate', 'FSBADisable', 'FSBAGetMaxAllocatorSize', 'FSBAHasAllocatorForSize', 'FSBAIsDisabled',
               'FSBAIsSmallBlock', 'FSBAPreventCreation', 'FStringA_GetStringInitData', 'FStringW_GetStringInitData',
               'GetFileSystemMgr', 'GetLogFileMgr', 'GetMemTrackerForDLL', 'GetSmallObjectAllocator',
               'GetSmallObjectAllocatorInitParams', 'GetTempHeap', 'PeekLogFileMgr', '??4_Init_locks@std@@QAEAAV01@ABV01@@Z']
    out = {
        'generated_by': 'proxy/gen/classify.py (+ extract_model.py, verify_slots.py, extract_lua.py)',
        'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
        'source': model['source'],
        # issue 0125: the fork commit whose source scan_dll_usage and guid_counts read (`source` above is
        # interfaces.json's, from extract_model.py). Path, describe and source form (git or copy) are left out so the
        # output does not depend on how the source was made.
        'usage_scan_source': {'commit': srcinfo.get('head') or ''},
        'toolchains': model['toolchains'],
        'counts': counts,
        'slot_proof': {'clang_dump': model['slot_method'], 'msvc_check': proof['checked'],
                       'smoke_host_x64_runtime': proof['smoke_x64'], 'mismatches': proof['mismatches']},
        'cost_model_us': COST_US,
        'policy_counts_now': {'%s/%s' % k: v for k, v in sorted(pol_counts.items())},
        'policy_counts_target': {'%s/%s' % k: v for k, v in sorted(pol_counts_target.items())},
        'mirrors': {k: {'interfaces': v[0], 'invalidation': v[2]} for k, v in MIRRORS.items()},
        'unmarshallable': {'count': len(unmarsh), 'by_category': dict(cat), 'items': unmarsh},
        'projection': costs,
        'benchmark_policy_map': pol_by_raw,
        'exports': [{'name': x, 'policy': 'server_local',
                     'rule': 'resolved by the x64 DLL from the server executable (srv_exports.cpp); the proxy in the '
                             'EXE never needs them'} for x in exports],
        'interfaces': model['interfaces'],
        'wrappers': model['wrappers'],
        'records': model['records'],
        'lua': lua,
    }
    with open(os.path.join(out_dir, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(out, f, indent=1, ensure_ascii=True)
    with open(os.path.join(out_dir, 'policy-table.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('direction\tinterface\tmethod\tsignature\tslot_x86\tslot_x64\tmarshal\tmechanical\tpolicy_now\t'
                'policy_target\trule\tconsumer\n')
        for direction, dc, name, m, v in sorted(rows, key=lambda r: (r[0], r[1], r[3]['slot']['x86'])):
            sig = '%s(%s)%s' % (m['ret'], ', '.join(p['type'] for p in m['params']), ' const' if m['const'] else '')
            f.write('\t'.join(str(x) for x in (direction, dc, name, sig, m['slot']['x86'], m['slot']['x64'],
                                                ','.join([v['ret_codec']] + [p['codec'] for p in v['params']]),
                                                v['mechanical'], m['policy']['now'], m['policy']['target'],
                                                m['policy']['rule'], consumer_of(consumers, direction, dc, name)))
                    + '\n')
    log(json.dumps(counts, indent=1))
    log('policy now   ', out['policy_counts_now'])
    log('policy target', out['policy_counts_target'])
    log('unmarshallable', len(unmarsh), dict(cat))
    for name, c in costs.items():
        for scn, r in c.items():
            if isinstance(r, dict):
                log(name, scn, {k: r[k].get('per_frame_us', r[k].get('per_turn_s')) for k in r})
    log('wrote %s and policy-table.tsv in %.1f s' % (os.path.join(out_dir, 'model.json'), time.time() - t0))
    return 0


def _read_lf(path):
    with open(path, 'rb') as f:
        return f.read().replace(b'\r\n', b'\n')


def _mask_generated_at(text):
    return re.sub(rb'^( "generated_at": )"[^"]*"', rb'\1"<masked>"', text, count=1, flags=re.M)


def compare(new_dir, ref_dir):
    """issue 0125: the generated files in new_dir against the committed ones in ref_dir. Returns a list of difference
    lines (empty: equal). policy-table.tsv byte for byte after folding the reference's CRLF to LF; each differing row
    by its key (direction, interface, method, signature). model.json with the generated_at value masked."""
    diffs = []
    tsv_new = _read_lf(os.path.join(new_dir, 'policy-table.tsv'))
    tsv_ref = _read_lf(os.path.join(ref_dir, 'policy-table.tsv'))
    if tsv_new != tsv_ref:
        def rows(b):
            lines = b.decode('utf-8', 'replace').split('\n')
            out = collections.OrderedDict()
            for n, line in enumerate(lines, 1):
                if not line:
                    continue
                c = line.split('\t')
                key = '\t'.join(c[:4]) if n > 1 else '<header>'
                out.setdefault(key, (n, line))
            return out
        rn, rr = rows(tsv_new), rows(tsv_ref)
        for key in list(rr) + [k for k in rn if k not in rr]:
            a, b = rr.get(key), rn.get(key)
            if a and b and a[1] == b[1]:
                continue
            label = key.replace('\t', ' ')
            if a is None:
                diffs.append('policy-table.tsv: row only in the generated table (line %d): %s' % (b[0], label))
            elif b is None:
                diffs.append('policy-table.tsv: row only in the committed table (line %d): %s' % (a[0], label))
            else:
                ca, cb = a[1].split('\t'), b[1].split('\t')
                cols = [i for i in range(max(len(ca), len(cb))) if (ca[i:i + 1] or [None]) != (cb[i:i + 1] or [None])]
                diffs.append('policy-table.tsv: row differs (committed line %d, generated line %d, column(s) %s): %s'
                             % (a[0], b[0], ','.join(str(i + 1) for i in cols), label))
        if not diffs:   # same rows, other order or trailing bytes
            diffs.append('policy-table.tsv: same rows, other bytes (row order or line ends)')
    js_new = _mask_generated_at(_read_lf(os.path.join(new_dir, 'model.json')))
    js_ref = _mask_generated_at(_read_lf(os.path.join(ref_dir, 'model.json')))
    if js_new != js_ref:
        ln, lr = js_new.split(b'\n'), js_ref.split(b'\n')
        first = next((i for i in range(min(len(ln), len(lr))) if ln[i] != lr[i]), min(len(ln), len(lr)))
        diffs.append('model.json: differs (generated_at masked); first differing line %d: committed %r, generated %r'
                     % (first + 1, lr[first][:120] if first < len(lr) else b'<end>',
                        ln[first][:120] if first < len(ln) else b'<end>'))
    return diffs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', help='DLL fork source at the deps/dll.lock commit (git checkout or git archive copy)')
    ap.add_argument('--unlocked', action='store_true', help='accept a git checkout at another commit (fork development)')
    ap.add_argument('--out-dir', default=MODEL_DIR, help='where model.json and policy-table.tsv are written')
    ap.add_argument('--check', action='store_true', help='generate into a temporary folder and compare; write nothing')
    ap.add_argument('--against-dir', default=MODEL_DIR, help='--check: the folder with the files to compare against')
    ap.add_argument('--inputs-dir', default=MODEL_DIR,
                    help='issue 3503: where interfaces.json, lua.json and slot-proof.json are read (default proxy/model)')
    ap.add_argument('--lock', help='issue 3503: lock file to check --src against (default deps/dll.lock)')
    ap.add_argument('--commit', help='issue 3503: with --unlocked, the label of a plain copy (recorded as its commit)')
    a = ap.parse_args(argv)
    global INPUTS_DIR
    INPUTS_DIR = os.path.abspath(a.inputs_dir)
    try:
        fork, info = resolve_fork(a.src, a.unlocked, a.lock, a.commit)
    except ValueError as ex:
        log('classify: --src refused: %s' % ex)
        return 2
    log('classify: fork source %s (%s)' % (fork, info.get('note', info.get('mode', ''))))
    if not a.check:
        os.makedirs(a.out_dir, exist_ok=True)
        return generate(fork, info, a.out_dir)
    for name in ('policy-table.tsv', 'model.json'):
        if not os.path.isfile(os.path.join(a.against_dir, name)):
            log('classify: --check: %s missing' % os.path.join(a.against_dir, name))
            return 2
    tmp = tempfile.mkdtemp(prefix='classify-check-')
    try:
        rc = generate(fork, info, tmp)
        if rc:
            return rc
        diffs = compare(tmp, a.against_dir)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for d in diffs:
        log('classify: --check: ' + d)
    log('classify: --check: %s' % ('EQUAL: policy-table.tsv byte for byte, model.json (generated_at masked)'
                                    if not diffs else 'DIFFERENT: %d difference(s)' % len(diffs)))
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
