"""instance (CL steps 16 and 17; validated tier, issue 3506): the release on a claimed Goldberg test instance and the
live proxy route in verify mode.

1. Claim an instance (tools/harness/pool.py claim; C5_RELEASE_INSTANCE names one or a comma list tried in order, else
   the pool's choice, which since ticket 0180 (D133) is a C: or F: instance and never a D: one), waiting up to
   C5_RELEASE_INSTANCE_WAIT minutes (90) for one to come free; never the owner's install, D26). Its mods must be the
   descriptor's (each `<folder> (v <version>).modinfo` in its MODS folder): an
   instance with another release's mods is a stop (CL 17: provision one with the tag's mods).
   **Ticket 0640 (D155 part 4):** the claim names the take's ticket and queue address (`pool.py claim --ticket T
   --address A`), so the pool shows who answers for the instance and a box on it goes to that address, not to the MC's
   desk. T is C5_RELEASE_TICKET, else C5_RELEASE_HI_TICKET; A is C5_RELEASE_ADDRESS, else C5_RELEASE_HI_ADDRESS. Without
   both the stage claims nothing and stops at once, naming the settings; a pool refusal of the ticket or the address
   (unknown or finished ticket, dead address) stops at once too, without the claim wait. Neither is part of the inputs.
2. Lay the release onto it, each step a provision.py command (the instance's own manifest records it):
   first (issue 0310) each of the descriptor's mod folders on the instance gets the VFS imports its .civ5proj has and
   its .modinfo lacks (stages/_modinfo.py; the project in the folder, an install from the fork's folders, else the
   testdata stage's copy of the tag's folder): upstream master's committed CP modinfo lacks Gamecore/GamecoreInit.lua
   and three more, which leaves Game, Map, Players nil in a game. A changed modinfo is backed up first and so is the
   instance's cache/Civ5ModsDatabase.db, which is then removed (the game caches the mods' file lists; 3515); a
   ModBuddy-built install (5.4.8) changes nothing.
   set-dll to the build stage's instance-aware 32-bit DLL (skipped when the instance already has those bytes),
   set-proxy with the x64 files and the proxy DLL; with a pack stage, a test pack (tools/release/pack.py with the
   tag's DLL and the instance's DLL as a test-only entry, README section 5) whose manifest.json and purity.tsv are
   copied into VP64\\ before the game starts and removed after it has stopped (the MC's conditions, thread #263), and
   the arms run with pack=1. Without a pack stage output the live check runs without a pack and the report says so.
3. Two arms (proxy/tools/live/arm.py, the harness's launch, guard and start-up watch, gated): 10 AI turns on a Tiny
   save and on the refsave stage's huge save, vpproxy.ini mode=on local_answers=1 local_verify=1; copies of both saves
   go into the instance's ModdedSaves under the names REL-<tag>-TINY / REL-<tag>-HUGE and are removed afterwards.
   Pass per arm = arm.py exit 0, the proxy's report line present with verified > 0, MISMATCHES 0, LOCK ERRORS 0, no
   VERIFY MISMATCH line, no boot FALLBACK line. No picture is taken (2026-11-14, D114: arm.py's end shot came after
   its checklist with popups still open; what this stage validates is the counts, not a picture); C5_VISUAL_OFF=1
   keeps the harness from recording any shot the arm's checklist takes.
4. Restore, always (also after a stop or a failure): the pack files, the saves, set-proxy --original, set-dll back to
   the kept DLL (by hash), the modinfo files and the mods cache (by hash), the front end (frontend.py restore), then provision.py verify --quick (under quietlock.py
   run; D110: the install by size, the user tree with every file this stage changed by hash; a full verify at most
   once a day per instance), and the claim is released. D110 and D133: the pool's short-run order takes the C:
   instances first, then F:. A restore that fails is a failure of the stage, said loudly.

A failed arm is a stop (the route differs: a bug before the release is validated); no free instance after the wait is a stop.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import time
from pathlib import Path

from take_release import REFUSAL_MARKERS, StageResult, sha256_file
from stages import _common, _modinfo, report
from stages import validated as V

DEPENDS = ['build', 'refsave']
USES = ['pack', 'purity']
TURNS = 10
# The Tiny save: a copy of 9029's live Tiny save, turn 8 (220,563 bytes), only read (copied onto the instance).
# C5_RELEASE_TINY_SAVE names it; else the first existing of TINY_SAVE_CANDIDATES: a work copy on C: or F:, then the
# kept copy on D: (a storage read, which D133 allows; ticket 0193, 2026-11-15). The hash check in _run guards any copy.
TINY_SAVE_NAME = 'REL548-TINY_0008.Civ5Save'
TINY_SAVE_CANDIDATES = tuple(Path(r) / '3500' / 'inputs' / TINY_SAVE_NAME
                             for r in ('C:/c5-64bit-work', 'F:/c5-64bit-work', 'D:/c5-64bit-work'))


def tiny_save_path(env=None, candidates=TINY_SAVE_CANDIDATES) -> Path:
    named = (os.environ if env is None else env).get('C5_RELEASE_TINY_SAVE')
    if named:
        return Path(named)
    return next((p for p in candidates if p.is_file()), candidates[-1])


TINY_SAVE = tiny_save_path()
TINY_SHA256 = 'b2ceb6632d9766bd817819261d70b168556cc6219ce709326deaa101dcc29163'

# Ticket 0766 (D259): no 5.4.8 save loads on a 5.5 build (upstream 0848d52cc adds an unguarded saved CvPlayer field), so
# a take whose release is Vox Populi 5.5 plays its own Tiny save: REL55-TINY_0008, the same set-up (Tiny Continents,
# Prince, Polynesia, capital founded on turn 0, turn 8) made on VP 5.5 with the -ia DLL of Release-5.5
# (research/06-tooling-and-workflows/headless-and-parallel-testing.md 6.5). C5_RELEASE_TINY_SAVE still names a copy;
# every other release keeps TINY_SAVE / TINY_SHA256 above.
TINY_SAVES_BY_VP = {
    '5.5': {'name': 'REL55-TINY_0008.Civ5Save',
            'sha256': '6d1f3a1016624dc97bdd48f8f2a54e180ebb2237599e3119b653b19e968c0f57',
            'roots': ('C:/c5-64bit-work/keep/vp55-saves', 'F:/c5-64bit-work/keep/vp55-saves')},
}
VP_OF_TAG_RE = re.compile(r'^Release-(\d+\.\d+)(?:\.0)?(?![\d.])')


def vp_of_tag(tag) -> str | None:
    """'5.5' for 'Release-5.5', 'Release-5.5.0' and 'Release-5.5-beta5'; None for 5.4.x tags and other names (their
    own Tiny save is TINY_SAVE)."""
    m = VP_OF_TAG_RE.match(str(tag or ''))
    return m.group(1) if m and m.group(1) in TINY_SAVES_BY_VP else None


def tiny_save(ctx, env=None) -> tuple[Path, str]:
    """(path, SHA-256) of the Tiny save this take plays: the release's own (TINY_SAVES_BY_VP) or TINY_SAVE /
    TINY_SHA256. C5_RELEASE_TINY_SAVE names the path in either case; the hash is the release's."""
    vp = vp_of_tag(getattr(ctx, 'tag', None))
    if vp is None:
        return TINY_SAVE, TINY_SHA256
    spec = TINY_SAVES_BY_VP[vp]
    cands = tuple(Path(r) / spec['name'] for r in spec['roots'])
    return tiny_save_path(env, cands), spec['sha256']
CLAIM_HOURS = 3
CLAIM_WAIT_MIN = 90
CLAIM_RETRY_S = 120
HOLD_NAME = 'HOLD-INSTANCE'
HOLD_POLL_S = 10
DLL = 'CvGameCore_Expansion2.dll'
REPORT_RE = re.compile(r'local: canonical handles fetched .*LOCK ERRORS (\d+);.*verified (\d+), MISMATCHES (\d+)')
LUA_RE = re.compile(r'verified (\d+), LUA MISMATCHES (\d+)')


def inputs(ctx):
    return {'instance': V.env_settings()['instance'], 'turns': TURNS,
            'tiny': _common.file_sig(tiny_save(ctx)[0])}


def new_dir(ctx, name) -> Path:
    """A path under <work>/instance/ that does not exist yet (set-proxy --original --runs-to wants a new folder)."""
    base = ctx.dir('instance')
    p, n = base / name, 1
    while p.exists():
        n += 1
        p = base / f'{name}-{n}'
    return p


def owner(ctx):
    return f'{ctx.who}-release'


#: Ticket 0640 (D155 part 4, ticket 0388): the claim's ticket and the queue address that answers for a box on the
#: instance. Each setting is read first under the instance stage's own name, then under the hi_journey stage's (a
#: take's operator sets those already); a claim without both is refused before the pool is asked.
TICKET_SETTINGS = ('C5_RELEASE_TICKET', 'C5_RELEASE_HI_TICKET')
ADDRESS_SETTINGS = ('C5_RELEASE_ADDRESS', 'C5_RELEASE_HI_ADDRESS')
NO_TICKET = 'the instance stage claims only with the take\'s ticket and address'
#: pool.py claim refusals that no wait cures (its resolve(): the ticket or the address is wrong), so the claim wait ends
#: at once instead of retrying them as "no free instance" for C5_RELEASE_INSTANCE_WAIT minutes
FINAL_REFUSALS = ('unknown ticket', 'a claim works for an open ticket', 'has no owner to answer', 'is not a live address',
                  '--address must be', 'is a broadcast or the tool', '--ticket must be')


def claim_identity() -> tuple[dict | None, str | None]:
    """({'ticket', 'address', 'ticket_from', 'address_from'}, None), or (None, why) naming the settings that are
    missing (ticket 0640)."""
    got, miss = {}, []
    for key, names in (('ticket', TICKET_SETTINGS), ('address', ADDRESS_SETTINGS)):
        n = next((n for n in names if V.setting(n, None)), None)
        if n is None:
            miss.append(' or '.join(names))
            continue
        got[key], got[f'{key}_from'] = V.setting(n, None), n
    if miss:
        return None, (f'{NO_TICKET} (D155 part 4: a box on the instance goes to that address, not to the MC\'s desk); '
                      f'not set: {"; ".join(miss)}')
    return got, None


def claim_one(ctx, name=None, who=None):
    """(name, root) of the claimed instance `name` (None: the pool's choice), or (None, why). `who` is
    claim_identity()'s dict: the claim names its ticket and address (0640); None claims without them (only a direct
    call of this helper, never claim())."""
    args = ['claim', '--owner', owner(ctx), '--hours', CLAIM_HOURS, '--note', f'take_release {ctx.tag} instance']
    if who:
        args += ['--ticket', who['ticket'], '--address', who['address']]
    if name:
        args += ['--name', name]
    r = ctx.run(V.tool(ctx, 'pool') + args, name='claim', heavy=False)
    lines = [ln.strip() for ln in r.text.splitlines() if ln.strip() and not ln.startswith('===')]
    if r.rc != 0 or len(lines) < 2:
        return None, f'pool.py claim {name or "(pool choice)"}: exit {r.rc}: {" ".join(lines[-2:])}'
    i = next((k for k, ln in enumerate(lines) if ln.startswith('claimed by')), None)
    if i is None or i < 2:
        return None, f'pool.py claim {name or "(pool choice)"}: unexpected output {lines[-3:]}'
    return (lines[i - 2], Path(lines[i - 1])), None


def claim(ctx):
    """(name, root) of a claimed instance, or (None, why). The candidates are C5_RELEASE_INSTANCE (one name or a comma
    list), else the pool's choice (since 0180 never a D: instance). When none is free the stage waits, CLAIM_RETRY_S
    between rounds and the stop file checked, up to C5_RELEASE_INSTANCE_WAIT minutes (default CLAIM_WAIT_MIN), so an
    unattended run is not stopped by another ticket's short use of an instance.
    A hold file <work>/HOLD-INSTANCE (its text says why) makes the stage wait before any claim until it is removed, the
    stop file checked: a coordinator's window on the instances (0175, 2026-11-14: 0158's capacity levels) holds the
    run without ending the driver's invocation.
    Ticket 0640: the claim names the take's ticket and address (claim_identity()); without them nothing is claimed and
    (None, why) comes back at once, why naming the settings; a pool refusal of the ticket or the address (FINAL_REFUSALS)
    ends the wait at once too."""
    who, why = claim_identity()
    if who is None:
        return None, why
    hold = ctx.work / HOLD_NAME
    if hold.exists():
        ctx.say(f'{HOLD_NAME} present ({hold.read_text(encoding="utf-8", errors="replace").strip()[:200]}); '
                'waiting for its removal before the claim')
        while hold.exists():
            ctx.check_stop()
            time.sleep(HOLD_POLL_S)
        ctx.say(f'{HOLD_NAME} removed; claiming')
    names = [n.strip() for n in (V.env_settings()['instance'] or '').split(',') if n.strip()] or [None]
    wait_s = 60 * float(V.setting('C5_RELEASE_INSTANCE_WAIT', str(CLAIM_WAIT_MIN)))
    t0 = time.monotonic()
    while True:
        whys = []
        for n in names:
            got, why = claim_one(ctx, n, who)
            if got:
                return got, None
            whys.append(why)
        if all(any(f in w for f in FINAL_REFUSALS) for w in whys):
            return None, (f'{"; ".join(whys)} (the pool refused the ticket or the address, ticket {who["ticket"]} from '
                          f'{who["ticket_from"]}, address {who["address"]} from {who["address_from"]}: no wait cures '
                          'that)')
        left = wait_s - (time.monotonic() - t0)
        if left <= 0:
            return None, '; '.join(whys) + (f' (waited {wait_s / 60:.0f} min)' if wait_s > 0 else '')
        tried = ','.join(n or 'the pool' for n in names)
        ctx.say(f'no free instance ({tried}); next try in {CLAIM_RETRY_S} s')
        end = time.monotonic() + min(CLAIM_RETRY_S, left)
        while time.monotonic() < end:
            ctx.check_stop()
            time.sleep(min(10, max(0.0, end - time.monotonic())))


def claim_unblock(why: str) -> str:
    """What a person does after claim() came back without an instance."""
    if (why or '').startswith(NO_TICKET):
        return (f'set {TICKET_SETTINGS[0]} (a ticket the pool knows: on main or on the queue) and {ADDRESS_SETTINGS[0]} '
                '(a live queue address that answers for a box on the instance), or the hi_journey stage\'s '
                f'{TICKET_SETTINGS[1]} and {ADDRESS_SETTINGS[1]}, then run again')
    if any(f in (why or '') for f in FINAL_REFUSALS):
        return (f'name a ticket the pool knows and a live address ({TICKET_SETTINGS[0]}, {ADDRESS_SETTINGS[0]}; '
                'ticketq.py addresses), then run again')
    return 'wait for a free instance (pool.py list) or set C5_RELEASE_INSTANCE, then run again'


def manifest(inst_dir: Path) -> dict:
    return json.loads((inst_dir / 'instance.json').read_text(encoding='utf-8'))


def mods_check(ctx, user: Path) -> list:
    """The descriptor's mods missing from the instance's MODS folder (as '<folder> (v N)')."""
    missing = []
    for m in getattr(ctx.desc, 'mods', None) or []:
        if not (user / 'MODS' / m.folder / f'{m.folder} (v {m.version}).modinfo').is_file():
            missing.append(f'{m.folder} (v {m.version})')
    return missing


def count_log(path: Path) -> dict:
    txt = path.read_text(encoding='utf-8', errors='replace') if path.is_file() else ''
    m = REPORT_RE.findall(txt)
    lua = LUA_RE.findall(txt)
    return {'log': str(path), 'present': path.is_file(), 'report_line': bool(m),
            'lock_errors': int(m[-1][0]) if m else None, 'verified': int(m[-1][1]) if m else None,
            'mismatches': int(m[-1][2]) if m else None,
            'lua_verified': int(lua[-1][0]) if lua else None, 'lua_mismatches': int(lua[-1][1]) if lua else None,
            'mismatch_lines': txt.count('VERIFY MISMATCH'), 'fallback_lines': txt.count('boot: FALLBACK'),
            'purity': next((ln.strip() for ln in txt.splitlines() if 'boot: purity:' in ln), None)}


def arm_verdict(arm_rc, c) -> tuple[bool, str]:
    bad = []
    if arm_rc != 0:
        bad.append(f'arm.py exit {arm_rc}')
    if not c['report_line']:
        bad.append('no proxy report line in vpproxy.log')
    else:
        if not c['verified']:
            bad.append('verified 0 answers')
        if c['mismatches']:
            bad.append(f"MISMATCHES {c['mismatches']}")
        if c['lock_errors']:
            bad.append(f"LOCK ERRORS {c['lock_errors']}")
    if c['lua_mismatches']:
        bad.append(f"LUA MISMATCHES {c['lua_mismatches']}")
    if c['mismatch_lines']:
        bad.append(f"{c['mismatch_lines']} VERIFY MISMATCH lines")
    if c['fallback_lines']:
        bad.append('the proxy fell back to the 32-bit DLL (boot: FALLBACK)')
    if bad:
        return False, '; '.join(bad)
    return True, (f"{c['verified']} answers verified, 0 mismatches, 0 lock errors"
                  + (f", {c['lua_verified']} UI-Lua answers verified" if c['lua_verified'] else ''))


def make_test_pack(ctx, b):
    """A test pack for the instance (pack.py, 3504), or (None, why not)."""
    if ctx.outputs_if_ok('pack') is None:
        return None, 'no pack: the pack stage is not ok'
    if 'pack' in (ctx._d.tools or {}) and ctx.tool_missing('pack'):
        return None, 'no pack: tools/release/pack.py missing'
    player = ctx.work / 'inputs' / 'player-x86' / DLL      # the pack stage's copy of the tag's DLL
    if player.is_file():
        x86 = ['--x86-dll', player]
    elif ctx.config.get('fork'):
        x86 = ['--fork', ctx.config['fork'], '--tag', getattr(ctx, 'ref', None) or ctx.tag]   # 3509: --base-commit
    else:
        return None, f'no pack: {player} missing and no --fork'
    major = int(ctx.config.get('major_civs') or 22)
    x86 += ['--major-civs', major] if major != 22 else []   # issue 4252: the (3b) DLL and the manifest's count
    pu = ctx.outputs_if_ok('purity')
    purity = ['--purity', pu['purity_tsv']] if pu and pu.get('purity_tsv') else []   # else pack.py's empty one
    out = _common.fresh_dir(ctx.work / 'instance' / 'pack')
    r = ctx.run(V.tool(ctx, 'pack') + ['--layout', b['x64_layout']] + purity + x86 + ['--test-x86-dll', b['i0ia_dll'],
                                       '--test-x86-origin', f'take_release {ctx.tag} instance-aware build, test only',
                                       '--release', ctx.tag, '--out', out], name='pack', heavy=False)
    pk = out / 'vp64pack'
    if r.rc != 0 or not (pk / 'manifest.json').is_file():
        return None, f'pack.py exit {r.rc}; {r.log}'
    return pk, None


def place_saves(ctx, user: Path, huge: Path):
    d = user / 'ModdedSaves' / 'single'
    d.mkdir(parents=True, exist_ok=True)
    names = {'tiny': d / f'REL-{ctx.tag}-TINY.Civ5Save', 'huge': d / f'REL-{ctx.tag}-HUGE.Civ5Save'}
    shutil.copy2(tiny_save(ctx)[0], names['tiny'])
    shutil.copy2(huge, names['huge'])
    return names


def run(ctx):
    return report.finish(ctx, _run(ctx))


def _run(ctx):
    b, rs = ctx.outputs('build'), ctx.outputs('refsave')
    tsave, tsha = tiny_save(ctx)
    if not (tsave.is_file() and sha256_file(tsave) == tsha):
        return StageResult.stop(f'the Tiny live save {tsave} is missing or changed',
                                f'restore it (SHA-256 {tsha[:16]}...) or name a copy with C5_RELEASE_TINY_SAVE')
    got, why = claim(ctx)
    if not got:
        return StageResult.stop(f'no test instance could be claimed ({why})', claim_unblock(why))
    name, root = got
    inst = root / name
    ctx.partial(instance=name, root=str(root))
    who = claim_identity()[0] or {}
    out = {'instance': name, 'root': str(root), 'ticket': who.get('ticket'), 'address': who.get('address')}
    undo = []          # (label, callable) run in reverse order
    try:
        res = _lay_and_play(ctx, b, rs, name, inst, out, undo)
    except Exception as e:  # noqa: BLE001 - the restore below runs whatever happened
        res = StageResult.failed(f'{type(e).__name__}: {e}')
    if out.get('left_running'):
        res.outputs.update(out)
        return res   # a dialog is open: nothing is touched while the game runs, the claim is kept (D29)
    restore = _restore(ctx, name, undo)
    out['restore'] = restore
    if not restore['ok']:
        # the claim is kept: nobody else may start an instance the restore did not put back (2026-11-14 00:35)
        out['claim_released'] = False
        res.outputs.update(out)
        return StageResult.failed(f'RESTORE OF {name} FAILED: {restore["errors"]}; the instance is left as it is and '
                                  f'its claim ({owner(ctx)}) is KEPT; repair it with provision.py, then release the '
                                  f'claim (pool.py release {name} --owner {owner(ctx)})', **res.outputs)
    rel = ctx.run(V.tool(ctx, 'pool') + ['release', name, '--owner', owner(ctx)], name='release', heavy=False)
    out['claim_released'] = rel.rc == 0
    res.outputs.update(out)
    return res


def _restore(ctx, name, undo) -> dict:
    errors, done = [], []
    for label, fn in reversed(undo):
        try:
            ok, why = fn()
        except Exception as e:  # noqa: BLE001
            ok, why = False, f'{type(e).__name__}: {e}'
        (done if ok else errors).append(f'{label}: {why}')
    # --quick (D110, 2026-11-13: a full verify at most once a day per instance until 0161): the install's files by size,
    # the user tree (MODS with the DLL, the proxy layout, the saves) by hash: every file this stage changed.
    v = ctx.run(V.tool(ctx, 'provision') + ['verify', name, '--quick'], name='verify',
                what=f'{ctx.who} take_release {ctx.tag} instance verify --quick')
    if v.rc != 0:
        errors.append(f'provision.py verify {name}: exit {v.rc} ({v.log})')
    return {'ok': not errors, 'errors': errors, 'done': done, 'verify_rc': v.rc}


def _cmd(ctx, args, name):
    def f():
        r = ctx.run(V.tool(ctx, 'provision') + args, name=name, heavy=False)
        return r.rc == 0, f'exit {r.rc}'
    return f


def _rm(path: Path):
    def f():
        if path.exists():
            path.unlink()
        return not path.exists(), 'removed'
    return f


def _put_back(path: Path, kept: Path | None, want: str | None):
    """An undo: `path` back to the kept copy (checked by SHA-256), or removed when it did not exist before."""
    def f():
        if kept is None:
            if path.exists():
                path.unlink()
            return not path.exists(), 'removed (it did not exist before)'
        shutil.copy2(kept, path)
        got = sha256_file(path)
        return got == want, f'restored, SHA-256 {got[:16]}' + ('' if got == want else f' (want {want[:16]})')
    return f


def fix_modinfo(ctx, user: Path, out: dict, undo: list):
    """Issue 0310: the descriptor's mod folders on the instance get the VFS imports of their .civ5proj."""
    td = ctx.outputs_if_ok('testdata') or {}
    tag_mods = Path(td['mods_dir']) if td.get('mods_dir') else None
    kept = ctx.dir('instance', 'kept-modinfo')
    rec, changed = {}, False
    for m in getattr(ctx.desc, 'mods', None) or []:
        folder = user / 'MODS' / m.folder
        proj = None
        if not list(folder.glob('*.civ5proj')) and tag_mods and (tag_mods / m.folder).is_dir():
            proj = _modinfo.one(tag_mods / m.folder, '*.civ5proj')
        p = _modinfo.plan(folder, proj)
        if p['project'] is None:
            continue
        rec[m.folder] = {'project': p['project'], 'added': [r for r, _ in p['add']], 'import_set': p['flip'],
                         'project_files_missing': p['missing'], 'changed': _modinfo.changes(p)}
        if _modinfo.changes(p):
            info = Path(p['modinfo'])
            bak = kept / info.name
            shutil.copy2(info, bak)
            undo.append((f'{m.folder} modinfo back', _put_back(info, bak, sha256_file(info))))
            _modinfo.apply(p)
            changed = True
    if changed:
        cache = user / 'cache' / 'Civ5ModsDatabase.db'
        if cache.is_file():
            bak = kept / cache.name
            shutil.copy2(cache, bak)
            undo.append(('Civ5ModsDatabase.db back', _put_back(cache, bak, sha256_file(cache))))
            cache.unlink()
        else:
            undo.append(('Civ5ModsDatabase.db removed', _put_back(cache, None, None)))
        ctx.say('modinfo regenerated from the .civ5proj:', ', '.join(f for f, r in rec.items() if r['changed']))
    out['modinfo'] = rec


def _lay_and_play(ctx, b, rs, name, inst, out, undo):
    m = manifest(inst)
    user = inst / m.get('user_dir', "Documents\\My Games\\Sid Meier's Civilization 5").replace('\\', '/')
    missing = mods_check(ctx, user)
    if missing:
        return StageResult.stop(f'instance {name} lacks the release\'s mods {missing}',
                                'provision an instance with the tag\'s mods (CL 17; tools/instances/provision.py) and '
                                'name it with C5_RELEASE_INSTANCE')
    if (m.get('proxy') or {}).get('state') == 'installed':
        r = ctx.run(V.tool(ctx, 'provision') + ['set-proxy', name, '--original', '--runs-to',
                                                new_dir(ctx, 'vp64-runs-before')], name='unproxy-first',
                    heavy=False)
        out['was_proxied'] = True
        if r.rc != 0:
            return StageResult.stop(f'instance {name} carries a proxy layout that set-proxy --original refuses to '
                                    f'remove ({r.log})', 'repair or free the instance, then run again')
        m = manifest(inst)
    fix_modinfo(ctx, user, out, undo)
    dll_rel = (m.get('dll') or {}).get('path') or f"Documents\\My Games\\Sid Meier's Civilization 5\\MODS\\(1) " \
                                                  f"Community Patch\\{DLL}"
    cur = inst / dll_rel.replace('\\', '/')
    cur_sha = sha256_file(cur)
    want_sha = sha256_file(b['i0ia_dll'])
    if cur_sha != want_sha:
        kept = ctx.dir('instance', 'kept') / DLL
        shutil.copy2(cur, kept)
        out['kept_dll'] = {'path': str(kept), 'sha256': cur_sha}
        r = ctx.run(V.tool(ctx, 'provision') + ['set-dll', name, '--dll', b['i0ia_dll']], name='set-dll', heavy=False)
        if r.rc != 0:
            return StageResult.failed(f'provision.py set-dll {name}: exit {r.rc}; {r.log}\n{r.tail(6)}')
        undo.append(('set-dll back', _cmd(ctx, ['set-dll', name, '--dll', kept], 'set-dll-back')))
    out['dll_sha256'] = want_sha
    pack, why_no_pack = make_test_pack(ctx, b)
    out['pack'] = str(pack) if pack else why_no_pack
    bin_dir = pack if pack else Path(b['x64_bin'])
    r = ctx.run(V.tool(ctx, 'provision') + ['set-proxy', name, '--bin', bin_dir, '--proxy', b['proxy_dll']],
                name='set-proxy', heavy=False)
    if r.rc != 0:
        return StageResult.failed(f'provision.py set-proxy {name}: exit {r.rc}; {r.log}\n{r.tail(6)}')
    # --runs-to: the proxy's run folders under VP64/run (vpproxy.log, server logs) move out first, else --original
    # refuses (2026-11-14 00:34 live run)
    undo.append(('set-proxy --original', _cmd(ctx, ['set-proxy', name, '--original', '--runs-to',
                                                     new_dir(ctx, 'vp64-runs')], 'set-proxy-original')))
    vp64 = user / 'MODS' / '(1) Community Patch' / 'VP64'
    ini = []
    if pack:
        for f in ('manifest.json', 'purity.tsv'):
            if (pack / f).is_file():
                shutil.copy2(pack / f, vp64 / f)
                undo.append((f'remove VP64/{f}', _rm(vp64 / f)))
        ini = ['--ini', 'pack=1']
    saves = place_saves(ctx, user, Path(rs['save']))
    for k, p in saves.items():
        undo.append((f'remove {p.name}', _rm(p)))
    undo.append(('frontend restore', lambda: (lambda r: (r.rc == 0, f'exit {r.rc}'))(
        ctx.run(V.tool(ctx, 'frontend') + ['-i', name, 'restore'], name='frontend-restore', heavy=False))))
    ev = _common.fresh_dir(ctx.work / 'instance' / 'ev')
    ev.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, C5_VISUAL_OFF='1', C5_POOL_OWNER=owner(ctx))
    arms, bad = {}, []
    for key in ('tiny', 'huge'):
        arm = f'rel-{key}'
        r = ctx.run(V.tool(ctx, 'arm') + [name, arm, '--ev', ev, '--load', saves[key].stem, '--mode', 'on',
                                          '--local', '1', '--verify', '1', '--turns', TURNS] + ini,
                    name=f'arm-{key}', gated=True, env=env)
        aj = ev / arm / 'arm.json'
        arm_rc = json.loads(aj.read_text(encoding='utf-8')).get('rc') if aj.is_file() else r.rc
        alog = ev / arm / 'arm.log'
        if r.rc == 3 and alog.is_file() and any(mk in alog.read_text(encoding='utf-8', errors='replace')
                                                for mk in REFUSAL_MARKERS):
            out['arms'] = arms
            return StageResult.stop(f'a launch gate refused the game start of arm {arm} on {name} (see {alog})',
                                    'run the stage again when the quiet-machine lock is free (quietlock.py check)',
                                    **out)
        c = count_log(ev / arm / 'vpproxy.log')
        ok, why = arm_verdict(r.rc if r.rc else arm_rc, c)
        arms[key] = dict(c, arm_rc=arm_rc, rc=r.rc, ok=ok, why=why, save=saves[key].name, ev=str(ev / arm))
        ctx.partial(arms=arms)
        if not ok:
            bad.append(f'{key}: {why}')
        if arm_rc == 8:   # a dialog is open: the game is left running for a coordinator (D29); nothing else starts
            out['arms'] = arms
            out['left_running'] = True
            return StageResult.failed(f'arm {arm}: a dialog is open in the game on {name} (left running, D29); '
                                      'a coordinator closes it and stops the game; the instance keeps the release '
                                      f'layout and the claim until it is restored by hand (undo list: '
                                      f'{[u[0] for u in undo]})')
    out['arms'] = arms
    if bad:
        return StageResult.stop(f'the live route on {name} failed: {"; ".join(bad)}',
                                'read the arms\' vpproxy.log and arm.log (outputs.arms), file a bug, fix it and run the '
                                'tier again', **out)
    return StageResult.ok(**out)
