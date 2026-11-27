"""hi_journey (validated tier, issue 4506, epic 0392 W4): the human-input release journeys replayed on the release
candidate and on stock, and their report in the release folder.

The journeys are committed traces of tools/hi (tools/hi/journeys/<name>.jsonl, contract tools/hi/CONTRACT.md 8.1),
listed with their start saves in tools/hi/journeys/journeys.json. Each is replayed by `tools/hi/hi.py replay` on one
claimed Goldberg instance, first on the product route (the release laid onto the instance as the `instance` stage lays
it: the build's instance-aware 32-bit DLL, the proxy and the x64 files, a test pack with pack=1; vpproxy.ini mode=on),
then on the stock route (the same instance with vpproxy.ini mode=off: the proxy loads the 32-bit DLL, no server). The
two replays of a journey are compared with tools/hi/stockctl.py (contract 8.3).

**D162 (the owner, 2026-11-16 15:44; ticket 0436, 2026-11-16): it gates a take for the owner.** [reworded for privacy] No build is handed to the
owner to play until both HI release journeys have passed on that exact build, product and stock. A take with
`--for-owner` (the driver's marker of a build meant for the owner, recorded in the state) stops the tier
(`take_release: STOP hi_journey ... D162: ...`, exit 10) unless owner_check() passes: the stage `ok` (not `skipped`: "not played" is not "passed"), verdict
`passed`, both journeys of D162 (release-basic, release-large; on a 43-civ build release-basic-m43,
release-large-m43, ticket 0580) played on both routes and passed (a pass on a known flake's one rerun counts), from
the committed index (never a planted copy), on the build the state holds now (its
commits and product SHA-256 equal). take_release.py also checks it at the tier's end, runs the stage again when the
recorded result of an earlier take would not pass (`again()`), prints the D162 line in its summary, and `--owner-check`
answers it for a hand-out. Other takes keep the behaviour below (report only).

**Without --for-owner it reports, it does not gate** (the decision was open until D162): a journey that
fails is in the report and the outputs (`verdict`), and the stage is still `ok`; when the journeys cannot be played at
all (no free instance, a start save missing) the stage is `skipped` with the reason, which the tier also counts as
reached. Only a failure of the stage itself (a restore that did not put the instance back, a box left open, a game
that would not stop) is `failed`, because the instance is then not fit for the next user.

Steps:
1. Claim an instance (tools/harness/pool.py claim --length short --ticket T [--address A]; C5_RELEASE_HI_INSTANCE, else
   C5_RELEASE_INSTANCE, names one or a comma list; waits up to C5_RELEASE_HI_WAIT minutes, default 90).
2. Lay the release (stages/instance.py's helpers: the modinfo fix of 0310, set-dll, a test pack, set-proxy), the pack
   files into VP64\\, vpproxy.ini pack=1; the instance's settings for the journeys: GraphicsSettingsDX11.ini
   WindowResX/Y from the journey index (1920 x 1080, D158), UserSettings.ini SinglePlayerAutoEndTurnEnabled = 0
   (research human-input-testing.md 7.6) and AutoUnitCycle = 1 (the recordings' value; 0530,
   release-large-instance-difference.md); every changed file backed up by SHA-256 and restored by hash.
3. Per journey and route: vpproxy.ini mode; frontend.py prepare --mods (activates the mods, loads nothing); the
   start save copied under its index name (checked by SHA-256) and made the newest file of ModdedSaves/single (the
   journeys pick the top entry of Load Game), the saves a journey makes removed first; the harness's launch, guard check
   and the wait for mod activation; `hi.py replay`; the journey's own end (`ends: exit`: the game's EXIT and Yes are its
   last step, so the process must be gone within 60 s), `cli.py stop` for anything still running (after 0411 it works
   at the main menu too), then `cli.py leftovers`. A replay that stops in game is ended
   with the harness's stop (human-turn hook); a box is never pressed (D29, D155): the game is left running, the claim
   kept, and the stage fails naming it. **A crash without a box (ticket 0533)**: when the game of a replay ends with a
   crash and no box (the dialog watcher's marker of kind crash, 0419/0531, carrying no box or popup; or, without a
   marker, the proxy's exe-crash.txt for that pid, not older than the launch) and no game runs, the replay is recorded
   as crashed (`crash`: signature, code, address, the record, the evidence), crash.collect has run once (the watcher's;
   else `crash.py collect` here), the journey is failed (a product finding, not a harness stop), the next replays are
   played, and step 4 restores and releases as after a normal end. LeftRunning stays for a box, a popup, a game still
   running or processes left (D29, D155).
4. Restore, always: the made saves and the start save copies removed, vpproxy.ini and the settings back by hash, the
   pack files removed, set-proxy --original, set-dll back, the modinfo files, frontend.py restore (the three front-end
   files checked), provision.py verify --quick, the claim released. **Ticket 0634:** when the replays were cut short
   (the take's STOP file, a gate's refusal, an exception or a failed result after a launch) a game this stage launched
   may still run, and every tool of the restore refuses while it does; so first the game is ended with the harness's
   stop (cli.py stop with the human-turn hook: the saves settle first, no kill; AGENTS.md 4) and the stage waits until
   no game runs (GAME_END_WAIT_S after the stop returned). A box, a refused stop or a game that does not end is a loud
   failure ("GAME ON <instance> NOT ENDED, NOTHING RESTORED"): no restore, the claim kept, the undo list named as what
   is left changed. A restore that fails ("RESTORE OF <instance> FAILED") names what is left changed (`left_changed`)
   and keeps the claim.

**Ticket 0650: finishing the restore after a box.** The undo list is also written as data, entry by entry as the
stage changes the instance, to <work>/hi_journey/undo.json (the undo record: instance, claim owner and ticket, and per
entry its kind, the file and its kept copy's SHA-256 or the provision.py command, done or not), and the instance carries
a marker run\\hi_journey_unrestored.json naming that record from the claim until a restore that ends ok. A run left on a
box (or on a game that would not stop, or a failed restore) keeps both. After the claim's owner has closed the box:

    python -I tools/release/stages/hi_journey.py finish <work folder | its hi_journey folder | undo.json> [--tools F]

checks that no game, no box (the watcher's marker; a crash without a box with no game is not one), no popup of the
patch's helper and no process is left on the instance, and refuses loudly (exit 4, nothing changed) while one is; else
it takes the claim back (renews it for the recorded owner, or claims the instance when it is free; another owner's claim
is refused), runs every entry not yet done in reverse order (files back by hash; set-proxy --original; set-dll back to
the DLL of before the run, checked by its recorded SHA-256; the front end; the made saves), checks the front end and runs
provision.py verify --quick (the installed layout: its re-check by hash), writes the restore record
<work>/hi_journey/restore.json, removes the marker and releases the claim (exit 0). A restore that fails keeps the
claim and the marker (exit 1). The stage's start refuses (failed, its claim released, nothing laid) an instance whose
marker names a record that is not restored. **set-dll back** (0650, lead-0630's dry run 1): the DLL is restored to the
copy kept before the run and checked against its SHA-256 then; a kept copy that does not hash so, or a DLL that differs
after set-dll, is a failed restore entry (never set-dll --original, the instance's first DLL).

Outputs: `journeys` {name: {routes: {product|stock: replay summary}, compare: stockctl's counts and rows, verdict}},
`verdict` passed | failed | incomplete, `crashes` (0533: every crash without a box, when there was one), `gating` (true on a take for the
owner), `candidate` (the build's commits and the
SHA-256 of its product files), `index` and `index_committed`, `owner` (owner_check's answer, D162), `report`
(<work>/hi_journey/report.md and .json), `undo_record` and `restore_record` (0650), `unrestored` (0650: the earlier
run's marker that refused the start).

**The installer-installed layout (issue 2604, MC #3211, 2026-11-16).** With C5_RELEASE_HI_INSTALLED=<instance> the
stage plays on the layout the patch's installer (epic 2600) put on that instance, never one of its own: step 1 claims
that instance only; step 2 is replaced by a check of the installed layout (the install record
<My Games>/VP64/install/record.txt names the VP form and the Community Patch folder; the proxy DLL there, VP64's x64
DLL and VP64's server.exe must have the build's SHA-256, else the stage fails and plays nothing); provision.py set-dll
and set-proxy are never run on that instance, no test pack is written (the installed pack, pack=1, stays), and the
stage needs no instance-aware 32-bit DLL. **Ticket 0640: there it plays the PRODUCT route only** (C5_RELEASE_HI_ROUTES
unset means product there). A route list with stock is refused loudly before anything is claimed (a stop, "cannot be
judged", InstalledStockRefused): mode=off on that layout loads the player's own VP 32-bit DLL, which has no Lua channel
for the judges, and leaves config.ini in the D33 form (a red map; ticket 0626's rehearsal, validation-tiers.md 11, MC
#9064/#9065). A fresh stock route is played on the stage-laid layout with the instance-aware DLL, after the installer's
removal: the tier stage does it in that order (its installed_env(), the same rule), by hand C5_RELEASE_HI_ROUTES=stock
without C5_RELEASE_HI_INSTALLED. So a pass on the installed layout alone is never a D162 yes (the stock route is not
played; D162's rule is unchanged). The settings
files, vpproxy.ini (its mode= flips) and the saves are backed up and restored by hash as on the default path. The
restore's provision.py verify is replaced by the same layout check again plus vpproxy.ini by hash (an installed
instance is not what its provisioning manifest lists; it is verified after the installer's removal). Only the mods
form is played: the journeys are recorded with ModdedSaves and the MODS menu. The outputs carry `layout`
{kind: installer-installed, ...}, and the report, the journey headings and the D162 line name it, so that a pass on
the installed layout is never mistaken for one on the stage's own layout. Without the variable nothing changes.

**The validated plan plays the tier's way (ticket 0671, 2026-11-22; D221).** With the default tools and neither
C5_RELEASE_HI_INSTALLED nor C5_RELEASE_HI_ROUTES set, run() hands the whole stage to the tier stage
(tools/release/tiers/stages/hi_journey.py, `installed_path_why_not`, `_run_tier_path`): the installer's test build
from this take's build and pack on a clean instance of EXE build 26577625, the product route on that installed layout
(the composite loaded, so its pictures are the composite's back-buffer dumps), the removal, and the stock route fresh
on this stage's own layout (no base take: nothing is carried). D162 is answered by the tier's gate (the same check as
owner_check() here when nothing is carried). Before 0671 the plan played both routes on the layout of steps 1-4 below,
which never lays shell_composite.dll: the product route was not the shipped product and its pictures were PrintWindow
(the first beta's usual plan, validation-tiers.md 19). Steps 1-4 are still this stage's own path for the tier's stock
route, for the by-hand variables above, for a test's fake tools, and with C5_RELEASE_HI_STAGE_LAID=1 (diagnostic only).

Settings (environment; part of the inputs): C5_RELEASE_HI_STAGE_LAID (1: the stage-laid layout for both routes, 0671),
C5_RELEASE_HI_INSTANCE, C5_RELEASE_HI_WAIT, C5_RELEASE_HI_TICKET (the
claim's ticket, default 0392), C5_RELEASE_HI_ADDRESS (the claim's live queue address; default the ticket's owner),
C5_RELEASE_HI_BY (claim owner and replay --by; default <who>-release), C5_RELEASE_HI_JOURNEYS (comma list, default
all of the take's major-civilisation count), C5_RELEASE_HI_ROUTES (default product,stock), C5_RELEASE_HI_SAVES (the folder of the start saves, default the
index's `saves_dir`), C5_RELEASE_HI_INDEX (another journeys.json: the planted-fault copy of research section 8).

**The major-civilisation count (ticket 0580).** Each journey of the index names the count its start save was made with
(`major_civs`, absent = 22). The stage plays only the journeys of the take's count (the build's outputs `major_civs`,
else the config's, else 22): a 43-civ build cannot load a 22-civ save (the server refuses it, stage save-variant), so a
`--major-civs 43` take plays release-basic-m43 and release-large-m43, and D162 asks those of it. A journey named in
C5_RELEASE_HI_JOURNEYS of another count is refused; a count with no journeys in the index is skipped (not played).

**The Vox Populi version (ticket 0770, 2026-11-25).** No save of one VP release loads on another (VP 5.5 serialises a
field 5.4.8 does not, research human-input-testing.md 8.11, 8.12), so each VP release has its own start saves and traces. Each
journey of the index names the VP release its start save was made on (`vp`, absent = the index's `default_vp`, absent
= 5.4.8); a VP release's journeys keep the role names D162 asks for (release-basic, release-large, release-basic-m43,
release-large-m43), with their own trace files. load_index(path, vp) gives only the journeys of that VP release, so
every reader below sees one set. The take's VP release (`take_vp`): C5_RELEASE_HI_VP when set, else the version in the
take's tag (`Release-5.5` -> 5.5), else in its descriptor's tag (a `--base-commit` take named otherwise), else the
index's `default_vp`. A VP release with no journeys in the index is skipped (not played). An index that names no
release at all (no `vp` on any journey, no `default_vp`: before 0770, a test's or a planted copy) is one set played on
every release, as before. The outputs record `vp`.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import sys
import time
from pathlib import Path
from types import SimpleNamespace

if __name__ == '__main__':     # 0650: `python -I tools/release/stages/hi_journey.py finish ...` (-I adds no script folder)
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from take_release import GateRefused, StageResult, StopRequested, sha256_file
from stages import _common, report
from stages import instance as I
from stages import validated as V

DEPENDS = ['build']
USES = ['pack', 'purity']
REPO = Path(__file__).resolve().parents[3]
INDEX = REPO / 'tools' / 'hi' / 'journeys' / 'journeys.json'
ROUTES = ('product', 'stock')
MODE = {'product': 'on', 'stock': 'off'}
CLAIM_HOURS = 4
CLAIM_WAIT_MIN = 90
CLAIM_RETRY_S = 120
EXIT_WAIT_S = 60          # MC rule #2382: the process gone within 60 s of the journey's Yes, else report
ACTIVATION_WAIT_S = 600
POLL_S = 5
SETTLE_S = 10
REPLAY_TIMEOUT_S = 3600
GUARD_TRIES = 4
# 0634: after the harness's stop has returned (it waits up to 600 s itself for the process's end, 0700), the stage waits
# at most this long for no game to run on the instance before its restore; past it: LeftRunning (claim kept, no restore)
GAME_END_WAIT_S = 120
# cli.py stop exits: 0 stopped; 4 StopTimeout/StopLingering (the terminate is sent, the end not yet seen: 0700)
STOP_ENDING_RCS = (0, 4)
DLL = 'CvGameCore_Expansion2.dll'
# D162 (the owner, 2026-11-16): the journeys a build for the owner must pass, on both routes
D162_JOURNEYS = ('release-basic', 'release-large')
# ticket 0580 (MC, 0574 Q17, 2026-11-19): a build of another major-civilisation count cannot load the 22-civ start saves
# (the server refuses a save of the other variant, proxy-design.md 30), so it has journeys of its own, recorded on
# start saves made with a build of its count; the index names the count of each journey ("major_civs", absent = 22)
MAJOR_CIVS_DEFAULT = 22
D162_BY_MAJOR = {22: D162_JOURNEYS, 43: ('release-basic-m43', 'release-large-m43')}
# ticket 0770: the VP release a journey's start save was made on ("vp"; absent = the index's "default_vp", else this)
VP_DEFAULT = '5.4.8'
VP_RE = re.compile(r'^\d+\.\d+(\.\d+)?$')
VP_OF_TAG = re.compile(r'^Release-(\d+\.\d+(?:\.\d+)?)$')
D162 = ('D162 (the owner, 2026-11-16): no build goes to the owner to play before both HI release journeys '
        '(release-basic, release-large; for the 43-civ variant release-basic-m43, release-large-m43) pass on that exact build, '
        'product and stock')
# the product files of a build whose SHA-256 the stage records, as the build stage's x64_hashes name them
PRODUCT_FILES = {'proxy_dll': f'proxy/bin/{DLL}', 'x64_dll': f'bin/{DLL}', 'server': 'bin/server.exe'}
USER_DEFAULT = "Documents\\My Games\\Sid Meier's Civilization 5"
STOP_HOOK = 'tools.harness.turns:stop_readiness'


def _py(*parts, isolated=True):
    return [sys.executable] + (['-I'] if isolated else []) + [str(REPO.joinpath(*parts))]


# Tools of this stage that take_release.default_tools() and validated.TOOLS do not name; a test's `tools` wins.
# launch.py, guard_check.py and frontend.py import their sibling modules, so they run without -I (as arm.py runs them).
TOOLS = {
    'launch': _py('proxy', 'tools', 'live', 'launch.py', isolated=False),
    'guard_check': _py('proxy', 'tools', 'live', 'guard_check.py', isolated=False),
    'set_ini': _py('proxy', 'tools', 'live', 'set_ini.py'),
    'turns': _py('tools', 'harness', 'turns.py'),
    'cli': _py('tools', 'harness', 'cli.py'),
    'dialogwatch': _py('tools', 'harness', 'dialogwatch.py'),
    'crash': _py('tools', 'harness', 'crash.py', isolated=False),     # 0533: crash.collect when the watcher did not
    'hi': _py('tools', 'hi', 'hi.py'),
    'stockctl': _py('tools', 'hi', 'stockctl.py'),
}


# Every tool this stage starts. A driver whose tools are not the defaults (a test's fakes, a --tools file) must name
# each of them, or the stage fails before any claim: a test's fake tools never leave a real launch.py, cli.py, hi.py
# or set_ini.py to run against a fake instance (lead-0392, 2026-11-16).
NEEDS = ('pool', 'provision', 'frontend', 'pack', 'set_ini', 'launch', 'guard_check', 'turns', 'cli', 'dialogwatch',
         'crash', 'hi', 'stockctl', 'quietlock')


def custom_tools(ctx) -> bool:
    """True when the driver runs with tools other than take_release's defaults (a test, a --tools file)."""
    import take_release
    tools = getattr(ctx._d, 'tools', None) or {}
    default = {k: [str(x) for x in v] for k, v in take_release.default_tools().items()}
    return any(k not in default or [str(x) for x in v] != default[k] for k, v in tools.items())


def missing_tools(ctx) -> list:
    """The tools of NEEDS that a custom driver does not name (empty with the default tools)."""
    if not custom_tools(ctx):
        return []
    tools = getattr(ctx._d, 'tools', None) or {}
    return [n for n in NEEDS if n not in tools]


def tool(ctx, name) -> list:
    tools = getattr(ctx._d, 'tools', {}) or {}
    if name in tools:
        return ctx.tool(name)
    if custom_tools(ctx):
        raise RuntimeError(f'hi_journey: the tools of the driver are not the defaults and do not name {name!r}; the real '
                           'one is not run')
    if name in TOOLS:
        return [str(x) for x in TOOLS[name]]
    return V.tool(ctx, name)


def settings() -> dict:
    s = V.setting
    st = _settings(s)
    inst = s('C5_RELEASE_HI_INSTALLED', None)
    if inst:                    # 2604: only when set, so the default inputs (and their recorded hash) stay as they were
        st['installed'] = inst
        if not s('C5_RELEASE_HI_ROUTES', None):
            st['routes'] = 'product'     # 0640: the installed layout plays the product route only (installed_routes)
    vp = s('C5_RELEASE_HI_VP', None)
    if vp:                      # 0770: only when set, as above (the default inputs stay as they were)
        st['vp'] = vp
    return st


class InstalledStockRefused(Exception):
    """Ticket 0640: a stock route asked for on the installer-installed layout. It cannot be judged there (no Lua
    channel) and leaves config.ini in the D33 form (desk #9063, the MC's rulings #9064/#9065, 2026-11-21); the same rule
    as the tier stage's installed_env()."""


def installed_routes(name: str, routes: list) -> list:
    """The routes of a play on the installer-installed layout of `name`: the product route only; any other route list
    raises InstalledStockRefused (ticket 0640, validation-tiers.md 11)."""
    if list(routes) != ['product']:
        raise InstalledStockRefused(
            f'routes {",".join(routes)!r} on the {INSTALLED} layout of {name}: only the product route is played there; '
            'the stock route cannot be judged on it (vpproxy.ini mode=off loads the player\'s own VP 32-bit DLL, '
            'which has no Lua channel for the judges, and leaves config.ini in the D33 form: a red map) and is played '
            'on the stage-laid layout with the instance-aware DLL (MC rulings #9064/#9065)')
    return ['product']


def _settings(s) -> dict:
    return {'instance': s('C5_RELEASE_HI_INSTANCE', None) or s('C5_RELEASE_INSTANCE', None),
            'wait_min': s('C5_RELEASE_HI_WAIT', str(CLAIM_WAIT_MIN)), 'ticket': s('C5_RELEASE_HI_TICKET', '0392'),
            'address': s('C5_RELEASE_HI_ADDRESS', None), 'by': s('C5_RELEASE_HI_BY', None),
            'journeys': s('C5_RELEASE_HI_JOURNEYS', None), 'routes': s('C5_RELEASE_HI_ROUTES', 'product,stock'),
            'saves': s('C5_RELEASE_HI_SAVES', None), 'index': s('C5_RELEASE_HI_INDEX', None)}


INSTALLED = 'installer-installed'
RECORD_REL = Path('VP64') / 'install' / 'record.txt'


def read_record(path: Path) -> dict:
    """The fields of an install record (installer-notes 13.2: UTF-8 with a BOM, TAB-separated lines; first line
    VP64-INSTALL-RECORD<TAB>1). Returns {release, variant, config, form, cpdir, mygames}; ValueError when it is not
    one."""
    lines = path.read_text(encoding='utf-8-sig').splitlines()
    if not lines or lines[0].split('\t')[:1] != ['VP64-INSTALL-RECORD']:
        raise ValueError(f'{path} is not an install record')
    if not any(ln.split('\t')[0] == 'end' for ln in lines):
        raise ValueError(f'{path} has no end line (an install that did not finish)')
    out = {}
    for ln in lines[1:]:
        k, _, v = ln.partition('\t')
        if k in ('release', 'variant', 'config', 'form', 'cpdir', 'mygames') and k not in out:
            out[k] = v
    return out


def resolve_cpdir(rec: dict, user: Path) -> Path | None:
    """The record's cpdir (`MG\\<relative to My Games>` or `AB\\<absolute>`) as a path on this instance."""
    v = rec.get('cpdir') or ''
    if v.startswith('MG\\'):
        return user / v[3:].replace('\\', '/')
    if v.startswith('AB\\'):
        return Path(v[3:])
    return None


X64_DLL_NEW = 'CvGameCore_Expansion2_x64.dll'   # issue 2606 (MC #3403): the x64 DLL's name in installer packs


def installed_x64_name(vp64: Path) -> str:
    """Issue 2606: the x64 DLL's file name in an installed VP64\\ as the proxy finds it: the pack manifest's
    x64_dll.file (the installed pack has pack=1), else CvGameCore_Expansion2_x64.dll when present, else the old name
    (a pack written before 2606)."""
    try:
        f = (json.loads((vp64 / 'manifest.json').read_text(encoding='utf-8')).get('x64_dll') or {}).get('file')
        if isinstance(f, str) and f and '/' not in f and '\\' not in f:
            return f
    except (OSError, ValueError, AttributeError):
        pass
    return X64_DLL_NEW if (vp64 / X64_DLL_NEW).is_file() else DLL


def installed_layout(b: dict, user: Path) -> dict:
    """The installer-installed layout under `user` checked against the build `b`: {ok, why, record, form, cpdir,
    release, variant, config, sha256: {role: hash found}, want: {role: build's hash}, fallback_sha256, ini_sha256}.
    ok False names every reason (no record, the modpack form, a file missing or not the build's bytes)."""
    rp = user / RECORD_REL
    res = {'kind': INSTALLED, 'record': str(rp), 'ok': False, 'why': []}
    try:
        rec = read_record(rp)
    except (OSError, ValueError) as e:
        res['why'].append(f'no installer record: {e}')
        return res
    res.update({k: rec.get(k) for k in ('release', 'variant', 'config', 'form')})
    cp = resolve_cpdir(rec, user)
    res['cpdir'] = str(cp) if cp else rec.get('cpdir')
    if cp is None or not cp.is_dir():
        res['why'].append(f'the record\'s Community Patch folder {rec.get("cpdir")!r} is not there')
        return res
    if rec.get('form') != 'mods':
        res['why'].append(f'VP form {rec.get("form")!r}: the journeys are recorded for the mods form (ModdedSaves and '
                          'the MODS menu); a modpack-form install is not played')
    want = product_hashes(b)
    res['x64_file'] = installed_x64_name(cp / 'VP64')
    files = {'proxy_dll': cp / DLL, 'x64_dll': cp / 'VP64' / res['x64_file'], 'server': cp / 'VP64' / 'server.exe'}
    res['want'] = {k: want.get(k) for k in files}
    res['sha256'] = {}
    for role, p in files.items():
        got = sha256_file(p).lower() if p.is_file() else None
        res['sha256'][role] = got
        if not want.get(role):
            res['why'].append(f'the build has no {role} hash to compare')
        elif got != want[role]:
            res['why'].append(f'{p.name} ({role}) is {got[:16] + "..." if got else "missing"}, the build\'s is '
                              f'{want[role][:16]}...')
    fb, ini = cp / 'CvGameCore_Expansion2_x86.dll', cp / 'vpproxy.ini'
    res['fallback_sha256'] = sha256_file(fb).lower() if fb.is_file() else None
    res['ini_sha256'] = sha256_file(ini).lower() if ini.is_file() else None
    if res['fallback_sha256'] is None:
        res['why'].append('CvGameCore_Expansion2_x86.dll (the fallback the stock route loads) is missing')
    if res['ini_sha256'] is None:
        res['why'].append('vpproxy.ini is missing')
    res['ok'] = not res['why']
    return res


class IndexError_(Exception):
    pass


def load_index(path=None, vp=None) -> dict:
    """journeys.json: {"format": 1, "saves_dir", "default_vp", "journeys": [{"name", "trace", "mods", "window": [w, h],
    "start_save": {"name", "file", "sha256"}, "made_saves": [prefix, ...], "ends": "exit"|"menu", "vp"}]}.
    Ticket 0770: only the journeys of the VP release `vp` (None: the index's default_vp) are returned in "journeys"
    ("vp" the release chosen, "vps" every release the index has); a release without journeys is IndexError_."""
    p = Path(path or INDEX)
    try:
        d = json.loads(p.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        raise IndexError_(f'{p}: {e}')
    if d.get('format') != 1 or not isinstance(d.get('journeys'), list) or not d['journeys']:
        raise IndexError_(f'{p}: expected format 1 and a non-empty "journeys" list')
    default_vp = d.get('default_vp', VP_DEFAULT)
    if not isinstance(default_vp, str) or not VP_RE.match(default_vp):
        raise IndexError_(f'{p}: default_vp must be a VP release like "5.5" or "5.4.8", not {default_vp!r}')
    seen = set()
    for j in d['journeys']:
        # the entry is not changed (tiers' entry_hash hashes it as committed): an absent vp is read as default_vp
        jv = j.get('vp', default_vp)
        if not isinstance(jv, str) or not VP_RE.match(jv):
            raise IndexError_(f'{p}: journey {j.get("name")!r}: vp must be a VP release like "5.5", not {jv!r}')
        if (j.get('name'), jv) in seen:
            raise IndexError_(f'{p}: journey {j.get("name")!r} is listed twice for VP {jv}')
        seen.add((j.get('name'), jv))
    d['vps'] = sorted({j.get('vp', default_vp) for j in d['journeys']})
    if 'default_vp' not in d and not any('vp' in j for j in d['journeys']):
        # an index that names no VP release (before 0770; a test's or a planted copy) is one set for every release
        vp = None
    want = vp or default_vp
    if not isinstance(want, str) or not VP_RE.match(want):
        raise IndexError_(f'the take\'s VP release {want!r} is not a release like "5.5" or "5.4.8"')
    d['vp'] = want
    d['journeys'] = [j for j in d['journeys'] if j.get('vp', default_vp) == want]
    if not d['journeys']:
        raise IndexError_(f'{p}: the index has no journey for VP {want} (its releases: {d["vps"]})')
    for j in d['journeys']:
        miss = [k for k in ('name', 'trace', 'mods', 'start_save', 'ends') if not j.get(k)]
        ss = j.get('start_save') or {}
        miss += [f'start_save.{k}' for k in ('name', 'file', 'sha256') if not ss.get(k)]
        if miss:
            raise IndexError_(f'{p}: journey {j.get("name")!r} lacks {", ".join(miss)}')
        if j['ends'] not in ('exit', 'menu'):
            raise IndexError_(f'{p}: journey {j["name"]}: ends must be exit or menu, not {j["ends"]!r}')
        if not re.match(r'^HI-[A-Za-z0-9_.-]+$', ss['name']) or any(
                not re.match(r'^HI-[A-Za-z0-9_.-]+$', m) for m in j.get('made_saves') or []):
            raise IndexError_(f'{p}: journey {j["name"]}: every save name starts with HI- (contract 9)')
        for f in j.get('known_flakes') or []:
            if not (isinstance(f, dict) and isinstance(f.get('step'), int) and isinstance(f.get('exit'), int)
                    and f.get('cond') and f.get('why')):
                raise IndexError_(f'{p}: journey {j["name"]}: a known flake names step, exit, cond and why: {f!r}')
        mc = j.get('major_civs', MAJOR_CIVS_DEFAULT)
        if isinstance(mc, bool) or not isinstance(mc, int) or not 2 <= mc <= 62:
            raise IndexError_(f'{p}: journey {j["name"]}: major_civs must be a whole number 2..62, not {mc!r}')
        j['trace_path'] = str((p.parent / j['trace']).resolve())
    d['path'] = str(p)
    return d


def journey_major(j) -> int:
    """The major-civilisation count a journey's start save was made with (index field major_civs; absent = 22)."""
    return int(j.get('major_civs', MAJOR_CIVS_DEFAULT))


def take_major(ctx) -> int:
    """The take's major-civilisation count: the build stage's outputs (major_civs, written for a count other than 22),
    else the driver's config (--major-civs), else 22."""
    b = (ctx.outputs_if_ok('build') if hasattr(ctx, 'outputs_if_ok') else None) or {}
    v = b.get('major_civs') or (getattr(ctx, 'config', None) or {}).get('major_civs') or MAJOR_CIVS_DEFAULT
    return int(v)


def take_vp(ctx, st=None) -> str | None:
    """Ticket 0770: the take's VP release: C5_RELEASE_HI_VP when set, else the version in the take's tag
    (Release-5.5 -> '5.5'), else in its descriptor's tag; None (the index's default_vp) when none names one."""
    forced = (st or {}).get('vp') if st is not None else settings().get('vp')
    if forced:
        return str(forced)
    tags = [getattr(ctx, 'tag', None)]
    try:
        tags.append(getattr(getattr(ctx, 'desc', None), 'tag', None))
    except Exception:  # noqa: BLE001 - a descriptor error (testdata not run): the tag alone decides
        pass
    for t in tags:
        m = VP_OF_TAG.match(t) if isinstance(t, str) else None
        if m:
            return m.group(1)
    return None


def chosen(idx, st, major: int = MAJOR_CIVS_DEFAULT) -> list:
    """The journeys to play: those of the take's major-civilisation count (ticket 0580), all of them or the ones
    C5_RELEASE_HI_JOURNEYS names; a named journey of another count is refused (its start save cannot load)."""
    want = [n.strip() for n in (st['journeys'] or '').split(',') if n.strip()]
    js = idx['journeys']
    if want:
        unknown = [n for n in want if n not in [j['name'] for j in js]]
        if unknown:
            raise IndexError_(f'C5_RELEASE_HI_JOURNEYS names unknown journeys {unknown}')
        js = [j for j in js if j['name'] in want]
        other = [f'{j["name"]} ({journey_major(j)})' for j in js if journey_major(j) != major]
        if other:
            raise IndexError_(f'C5_RELEASE_HI_JOURNEYS names journeys of another major-civilisation count than the '
                              f'take\'s {major}: {other} (a save of the other variant cannot be loaded)')
        return js
    js = [j for j in js if journey_major(j) == major]
    if not js:
        raise IndexError_(f'the index has no journey for {major} major civilizations (its counts: '
                          f'{sorted({journey_major(j) for j in idx["journeys"]})})')
    return js


def d162_journeys(major: int) -> tuple:
    """The journeys D162 asks for on a build of this major-civilisation count; () when none are defined."""
    return D162_BY_MAJOR.get(int(major), ())


def routes_of(st) -> list:
    r = [x.strip() for x in st['routes'].split(',') if x.strip()]
    bad = [x for x in r if x not in ROUTES]
    if bad or not r:
        raise IndexError_(f'C5_RELEASE_HI_ROUTES {st["routes"]!r}: product and/or stock')
    return r


def inputs(ctx):
    if installed_path_why_not(ctx) is None:
        # 0671: the tier's inputs (its EXE build, release name and harness hash) and the path's name, so that a result
        # recorded on the stage-laid layout is never taken for one played the tier's way
        return dict(tier_module().inputs(ctx), path='tier stage (0671)')
    st = settings()
    out = {'settings': st, 'index': _common.file_sig(st['index'] or INDEX)}
    try:
        idx = load_index(st['index'], take_vp(ctx, st))
        out['traces'] = {j['name']: _common.file_sig(j['trace_path']) for j in idx['journeys']}
    except IndexError_ as e:
        out['index_error'] = str(e)
    return out


# ------------------------------------------------------------------------------------------------- the claim

def owner(ctx, st=None):
    return (st or settings())['by'] or f'{ctx.who}-release'


def claim_one(ctx, st, name=None):
    args = ['claim', '--owner', owner(ctx, st), '--hours', CLAIM_HOURS, '--length', 'short', '--ticket',
            st['ticket'], '--note', f'take_release {ctx.tag} hi_journey']
    if st['address']:
        args += ['--address', st['address']]
    if name:
        args += ['--name', name]
    r = ctx.run(tool(ctx, 'pool') + args, name='claim', heavy=False)
    lines = [ln.strip() for ln in body(r).splitlines() if ln.strip()]
    i = next((k for k, ln in enumerate(lines) if ln.startswith('claimed by')), None)
    if r.rc != 0 or i is None or i < 2:
        return None, f'pool.py claim {name or "(pool choice)"}: exit {r.rc}: {" ".join(lines[-2:])}'
    return (lines[i - 2], Path(lines[i - 1])), None


def claim(ctx, st):
    names = ([st['installed']] if st.get('installed') else            # 2604: that instance only, never the pool's choice
             [n.strip() for n in (st['instance'] or '').split(',') if n.strip()] or [None])
    wait_s = 60 * float(st['wait_min'])
    t0 = time.monotonic()
    while True:
        whys = []
        for n in names:
            got, why = claim_one(ctx, st, n)
            if got:
                return got, None
            whys.append(why)
        left = wait_s - (time.monotonic() - t0)
        if left <= 0:
            return None, '; '.join(whys)
        ctx.say(f'no free instance; next try in {CLAIM_RETRY_S} s')
        end = time.monotonic() + min(CLAIM_RETRY_S, left)
        while time.monotonic() < end:
            ctx.check_stop()
            time.sleep(min(10, max(0.0, end - time.monotonic())))


# ------------------------------------------------------------------------------------------------- files

def set_key(path: Path, key: str, value: str) -> bool:
    """`key = value` on the first uncommented line of `key` (the game's own ini style, line endings kept). False when
    the key has no line (nothing written)."""
    data = path.read_bytes()
    nl = b'\r\n' if b'\r\n' in data else b'\n'
    lines = data.split(nl)
    pat = re.compile(rb'^(\s*' + re.escape(key.encode()) + rb'\s*=\s*)(.*)$')
    for i, ln in enumerate(lines):
        m = pat.match(ln)
        if m and not ln.lstrip().startswith((b';', b'#')):
            lines[i] = m.group(1) + value.encode()
            path.write_bytes(nl.join(lines))
            return True
    return False


def keep(ctx, path: Path, label: str, undo: list, sub='kept') -> str | None:
    """Back `path` up (SHA-256 checked) into <work>/hi_journey/<sub>/ and register its restore by hash; returns the
    hash (None: the file did not exist; the undo removes it)."""
    if not path.exists():
        undo.append((f'{label} removed', undo_fn(ctx, {'kind': 'put_back', 'path': str(path), 'kept': None,
                                                         'sha256': None})))
        return None
    want = sha256_file(path)
    d = ctx.dir('hi_journey', sub)
    bak = d / f'{path.name}.{want[:12]}.bak'
    shutil.copy2(path, bak)
    if sha256_file(bak) != want:
        raise RuntimeError(f'backup of {path} does not hash as the original')
    undo.append((f'{label} back', undo_fn(ctx, {'kind': 'put_back', 'path': str(path), 'kept': str(bak),
                                                  'sha256': want})))
    return want


# ------------------------------------------------------------------------------------------------- the undo record (0650)

UNDO_NAME = 'undo.json'
RESTORE_NAME = 'restore.json'
UNRESTORED = 'hi_journey_unrestored.json'      # in the instance's run\ folder, from the claim to a restore that ends ok
UNDO_FORMAT = 1


def unrestored_marker(inst: Path) -> Path:
    return Path(inst) / 'run' / UNRESTORED


def undo_fn(ctx, spec: dict):
    """The undo callable of one record entry (0650): () -> (ok, why). The same function runs in the stage's restore and
    in `finish`, so a restore finished after a box does what the stage would have done. Kinds: put_back {path, kept,
    sha256} (kept None: the file did not exist, it is removed), rm {path}, provision {args, name}, set_dll {instance,
    path, kept, sha256}, frontend_restore {instance}, clear_made {dir, prefixes}; anything else fails naming itself."""
    k = spec.get('kind')
    if k == 'put_back':
        path, kept, want = Path(spec['path']), spec.get('kept'), spec.get('sha256')

        def f():
            if kept is None:
                if path.exists():
                    path.unlink()
                return not path.exists(), 'removed (it did not exist before)'
            if not Path(kept).is_file():
                return False, f'the kept copy {kept} is missing'
            shutil.copy2(kept, path)
            got = sha256_file(path)
            return got == want, f'restored, SHA-256 {got[:16]}' + ('' if got == want else f' (want {want[:16]})')
    elif k == 'rm':
        path = Path(spec['path'])

        def f():
            if path.exists():
                path.unlink()
            return not path.exists(), 'removed'
    elif k == 'provision':
        args = [str(a) for a in spec['args']]

        def f():
            r = ctx.run(tool(ctx, 'provision') + args, name=spec.get('name') or 'provision', heavy=False)
            return r.rc == 0, f'exit {r.rc}'
    elif k == 'set_dll':
        f = _set_dll_back(ctx, spec)
    elif k == 'frontend_restore':
        def f():
            r = ctx.run(tool(ctx, 'frontend') + ['-i', spec['instance'], 'restore'], name='frontend-restore', heavy=False)
            return r.rc == 0, f'exit {r.rc}'
    elif k == 'clear_made':
        d, prefixes = Path(spec['dir']), list(spec.get('prefixes') or [])

        def f():
            gone = [p for pre in prefixes for p in d.glob(f'{pre}*.Civ5Save')]
            for p in gone:
                p.unlink()
            return True, f'removed {len(gone)}'
    else:
        def f():
            return False, (f'entry of kind {k!r} is not described in the undo record: put it back by hand '
                           f'({json.dumps(spec, default=str)[:200]})')
    f.spec = dict(spec)
    return f


def _set_dll_back(ctx, spec: dict):
    """0650 (lead-0630's dry run 1, MC 2026-11-21 16:26): the instance's DLL back to the one it had before the run, by
    the copy kept then and checked by its SHA-256 before and after provision.py set-dll; never set-dll --original (the
    instance's FIRST DLL, which an earlier set-dll may have replaced). Refuses loudly when it cannot."""
    path, kept, want = Path(spec['path']), Path(spec['kept']), (spec.get('sha256') or '').lower()

    def f():
        if not kept.is_file():
            return False, f'REFUSED: the kept DLL {kept} is missing; the DLL of before the run ({want[:16]}...) is not known'
        got = sha256_file(kept).lower()
        if got != want:
            return False, (f'REFUSED: the kept DLL {kept} is SHA-256 {got[:16]}..., the DLL of before the run was '
                           f'{want[:16]}...; nothing set')
        r = ctx.run(tool(ctx, 'provision') + ['set-dll', spec['instance'], '--dll', str(kept)], name='set-dll-back',
                    heavy=False)
        if r.rc != 0:
            return False, f'provision.py set-dll: exit {r.rc} ({r.log})'
        now_ = sha256_file(path).lower() if path.is_file() else None
        if now_ != want:
            return False, (f'LOUD: after set-dll the instance\'s DLL {path} is {(now_ or "missing")[:16]}, the DLL of '
                           f'before the run was {want[:16]}...')
        return True, f'exit 0, SHA-256 {want[:16]} (the DLL of before the run)'
    return f


def spec_of(fn) -> dict:
    """The record entry of an undo callable: its `spec` (undo_fn), else read from a closure of the instance stage's
    helpers that fix_modinfo appends (stages/instance.py _put_back, _rm, _cmd); else {'kind': 'opaque'}, which `finish`
    reports as left changed."""
    s = getattr(fn, 'spec', None)
    if isinstance(s, dict):
        return dict(s)
    code, q = getattr(fn, '__code__', None), getattr(fn, '__qualname__', '') or ''
    try:
        cells = dict(zip(code.co_freevars, [c.cell_contents for c in (fn.__closure__ or ())])) if code else {}
    except ValueError:          # an empty cell
        cells = {}
    if q.startswith('_put_back.') and {'path', 'kept', 'want'} <= set(cells):
        return {'kind': 'put_back', 'path': str(cells['path']),
                'kept': str(cells['kept']) if cells['kept'] is not None else None, 'sha256': cells['want']}
    if q.startswith('_rm.') and 'path' in cells:
        return {'kind': 'rm', 'path': str(cells['path'])}
    if q.startswith('_cmd.') and 'args' in cells:
        return {'kind': 'provision', 'args': [str(a) for a in cells['args']], 'name': cells.get('name')}
    return {'kind': 'opaque', 'qualname': q}


def _write_json(path: Path, data: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f'.tmp{os.getpid()}')
    tmp.write_text(json.dumps(data, indent=1, default=str) + '\n', encoding='utf-8')
    os.replace(tmp, path)


class Undo(list):
    """The stage's undo list [(label, callable)], also kept as data in the undo record <work>/hi_journey/undo.json
    (0650): written at every append and every entry's result, so a run that ends on a box can be finished from it.
    A plain list still works wherever an undo list is taken (the tier stage's original)."""

    def __init__(self, path: Path, head: dict):
        super().__init__()
        self.path = Path(path)
        self.head = dict(head)
        self.specs = []
        self.save()

    def append(self, item):
        label, fn = item
        super().append(item)
        self.specs.append(dict(spec_of(fn), label=label, done=False))
        self.save()

    def mark(self, i: int, ok: bool, why: str):
        self.specs[i].update(done=bool(ok), result=why, at=time.strftime('%Y-%m-%d %H:%M:%S'))
        self.save()

    def note(self, **kw):
        self.head.update(kw)
        self.save()

    def save(self):
        _write_json(self.path, dict(self.head, format=UNDO_FORMAT, entries=self.specs))

    @classmethod
    def load(cls, ctx, path: Path):
        """The record at `path` as an Undo whose callables are rebuilt from the entries (undo_fn)."""
        d = json.loads(Path(path).read_text(encoding='utf-8'))
        if d.get('format') != UNDO_FORMAT or not isinstance(d.get('entries'), list):
            raise ValueError(f'{path}: not an undo record of format {UNDO_FORMAT}')
        u = cls.__new__(cls)
        list.__init__(u)
        u.path, u.specs = Path(path), []
        u.head = {k: v for k, v in d.items() if k not in ('format', 'entries')}
        for e in d['entries']:
            list.append(u, (e.get('label') or e.get('kind'), undo_fn(ctx, e)))
            u.specs.append(dict(e))
        return u


def undo_record(ctx, name, root, inst, owner_, st) -> Undo:
    """The undo record of this run (0650) and the instance's marker naming it. The marker stays until a restore that
    ends ok; the next start on the instance refuses while it names a record that is not restored (unrestored())."""
    path = ctx.dir('hi_journey') / UNDO_NAME
    u = Undo(path, {'state': 'open', 'instance': name, 'root': str(root), 'inst_dir': str(inst), 'owner': owner_,
                    'ticket': st.get('ticket'), 'address': st.get('address'), 'tag': ctx.tag, 'work': str(ctx.work),
                    'installed': bool(st.get('installed')), 'created': time.strftime('%Y-%m-%d %H:%M:%S')})
    _write_json(unrestored_marker(inst), {'record': str(path), 'instance': name, 'tag': ctx.tag, 'work': str(ctx.work),
                                          'owner': owner_, 'created': u.head['created'],
                                          'why': 'hi_journey changed this instance; it is restored when this file is '
                                                 'gone (ticket 0650)'})
    return u


def unrestored(inst: Path) -> dict | None:
    """The instance's marker when it names an undo record that is not restored (or cannot be read), else None. A
    marker whose record says restored is stale (the removal failed) and is removed here."""
    mp = unrestored_marker(inst)
    if not mp.is_file():
        return None
    try:
        m = json.loads(mp.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        return {'marker': str(mp), 'error': f'unreadable marker: {e}'}
    try:
        rec = json.loads(Path(m.get('record') or '').read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        return dict(m, marker=str(mp), error=f'its undo record cannot be read: {e}')
    if rec.get('state') == 'restored':
        mp.unlink()
        return None
    return dict(m, marker=str(mp), state=rec.get('state'),
                pending=[e.get('label') for e in rec.get('entries') or [] if not e.get('done')])


def left(undo, why: str):
    """The record says the run ended without a restore that put the instance back; the marker stays (0650)."""
    if isinstance(undo, Undo):
        undo.note(state='left', left_why=str(why)[:2000], left_at=time.strftime('%Y-%m-%d %H:%M:%S'))


def restored(undo):
    """The record says restored and the instance's marker is removed (a restore that ended ok, 0650)."""
    if isinstance(undo, Undo):
        undo.note(state='restored', restored_at=time.strftime('%Y-%m-%d %H:%M:%S'))
        mp = unrestored_marker(Path(undo.head.get('inst_dir') or '.'))
        if mp.is_file():
            mp.unlink()


def finish_hint(undo) -> str:
    if not isinstance(undo, Undo):
        return 'restore it by hand from the kept backups by SHA-256'
    return (f'after the box is closed and no game runs, finish the restore with: python -I '
            f'tools/release/stages/hi_journey.py finish "{undo.path}" (ticket 0650)')


def write_restore_record(hj_dir: Path, rs: dict, **kw) -> Path:
    """<work>/hi_journey/restore.json: the restore's result (0650), by the stage or by `finish`."""
    p = Path(hj_dir) / RESTORE_NAME
    _write_json(p, dict(rs, written=time.strftime('%Y-%m-%d %H:%M:%S'), **kw))
    return p


def save_dir(user: Path) -> Path:
    return user / 'ModdedSaves' / 'single'


def clear_made(user: Path, j) -> list:
    gone = []
    d = save_dir(user)
    for prefix in j.get('made_saves') or []:
        for p in d.glob(f'{prefix}*.Civ5Save'):
            p.unlink()
            gone.append(p.name)
    return gone


def place_start(user: Path, j, saves_dir: Path) -> Path:
    """The start save copied under its index name (SHA-256 checked) and made the newest file of ModdedSaves/single."""
    ss = j['start_save']
    src = saves_dir / ss['file']
    if not src.is_file() or sha256_file(src).lower() != ss['sha256'].lower():
        raise FileNotFoundError(f'start save {src} missing or not SHA-256 {ss["sha256"][:16]}...')
    d = save_dir(user)
    d.mkdir(parents=True, exist_ok=True)
    dst = d / f'{ss["name"]}.Civ5Save'
    if not dst.is_file() or sha256_file(dst).lower() != ss['sha256'].lower():
        shutil.copyfile(src, dst)
    t = time.time() + 2          # newer than anything the clock gave the folder so far
    os.utime(dst, (t, t))
    newest = max((p for p in d.iterdir() if p.is_file()), key=lambda p: p.stat().st_mtime)
    if newest != dst:
        raise RuntimeError(f'{dst.name} is not the newest save of {d} ({newest.name} is)')
    return dst


# ------------------------------------------------------------------------------------------------- one replay

class LeftRunning(Exception):
    """The game cannot be stopped by this stage (a box, a refused stop, a game still running, processes left): nothing
    else runs on the instance. A game that ended with a crash and no box is not this (0533: crash_end, crashed)."""


def box(ctx, name) -> str | None:
    r = ctx.run(tool(ctx, 'dialogwatch') + ['marker', '--instance', name], name='box', heavy=False)
    if r.rc != 8:
        return None
    return body(r).strip()[-300:] or 'a box'


def ended_crash(ctx, name) -> str | None:
    """The watcher's marker when it records a crash without a box (kind "crash": the game process ended, nothing to
    press) and no game runs on the instance; else None (a real box, or a game still running: the caller stops)."""
    r = ctx.run(tool(ctx, 'dialogwatch') + ['marker', '--instance', name], name='box-kind', heavy=False)
    t = body(r)
    if r.rc != 8 or not re.search(r'"kind":\s*"crash"', t) or running(ctx, name):
        return None
    m = re.search(r'"signature":\s*"([^"]*)"', t)
    return m.group(1) if m else 'crash without a box'


_DW = None


def _dw():
    """tools/harness/dialogwatch.py (only the standard library at its top) for its readers of the proxy's crash record
    exe-crash.txt (proxy_record, game_crash_record, read_crash_record, record_signature): one parser, the watcher's."""
    global _DW
    if _DW is None:
        import importlib.util
        spec = importlib.util.spec_from_file_location('_hi_journey_dialogwatch',
                                                      REPO / 'tools' / 'harness' / 'dialogwatch.py')
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        _DW = mod
    return _DW


def crash_end(ctx, name, user: Path, pid, since: float) -> dict | None:
    """Ticket 0533 (0517's tool gap 2): the game of this replay ended with a crash and no box. Proved by the dialog
    watcher's marker of kind "crash" (0419, 0531) whose every entry is the crash's stand-in (no box, no patch popup
    carried), or, without a marker, by the proxy's exe-crash.txt naming this pid and not older than the launch
    (dialogwatch.game_crash_record); and in either case no game runs on the instance. Returns {signature, code,
    address, va, record, marker, source, pid, collect} after crash.collect has run once (the watcher's, when its marker
    records one; else this stage's `crash.py collect`), or None: a box is open, the game still runs, or nothing proves a
    crash (the caller keeps LeftRunning / its other handling, D29, D155)."""
    r = ctx.run(tool(ctx, 'dialogwatch') + ['marker', '--instance', name], name='crash-marker', heavy=False)
    dw = _dw()
    m, d = {}, {}
    if r.rc == 8:
        d = json_of(r)
        if d.get('kind') != 'crash' or any((x or {}).get('box_kind') not in (None, 'crash')
                                           for x in d.get('dialogs') or []):
            return None                                   # a box (or the patch's popup) is open: never pressed
        if d.get('marker'):
            try:
                m = json.loads(Path(d['marker']).read_text(encoding='utf-8'))
            except (OSError, ValueError):
                m = {}
    if running(ctx, name):
        return None
    rec = None
    try:
        prox = dw.proxy_record(SimpleNamespace(user_dir=str(user)), int(pid)) if pid else None
        rec, _why = dw.game_crash_record(prox, int(pid), since) if pid else (None, 'no pid')
    except Exception:  # noqa: BLE001 - a record that cannot be read proves nothing
        rec = None
    if r.rc != 8 and rec is None:
        return None                                       # no marker and no record of this pid: not proved a crash
    box0 = ((m.get('dialogs') or [None])[0] or {}) if m else {}
    if rec is None and (box0.get('proxy_record') or {}).get('path'):
        rec = dw.read_crash_record(box0['proxy_record']['path'])
    sig = d.get('signature') or m.get('signature') or (dw.record_signature(rec) if rec else 'crash without a box')
    code = (rec or {}).get('code_hex') or m.get('exit_code_hex') or box0.get('exit_code_hex')
    if not code:
        mc = re.search(r'0x[0-9A-Fa-f]{8}', sig)
        code = mc.group(0) if mc else None
    out = {'signature': sig, 'code': code, 'address': (rec or {}).get('where') or box0.get('exception_address'),
           'va': (rec or {}).get('va'), 'record': (rec or {}).get('path'), 'marker': d.get('marker'),
           'source': 'watcher marker (kind crash)' if r.rc == 8 else 'the proxy\'s exe-crash.txt of this pid',
           'pid': pid}
    got = m.get('crash') if isinstance(m.get('crash'), dict) else None
    if got is not None and 'error' not in got:
        out['collect'] = {'by': 'dialog watcher', 'dest': got.get('dest')}
    else:
        c = ctx.run(tool(ctx, 'crash') + ['collect', '--instance', name, '--since', f'{since:.3f}', '--pid', str(pid)],
                    name='crash-collect', heavy=False)
        out['collect'] = {'by': 'hi_journey (crash.py collect)', 'dest': json_of(c).get('dest'), 'rc': c.rc,
                          'log': str(c.log)}
    return out


def running(ctx, name) -> bool:
    r = ctx.run(tool(ctx, 'cli') + ['-i', name, 'status', '--json'], name='status', heavy=False)
    try:
        t = body(r)
        return bool(json.loads(t[t.index('{'):t.rindex('}') + 1]).get('running'))
    except ValueError:
        return True          # unknown: treat as running (a stop is tried; never a second launch)


def body(r) -> str:
    """A command's own output: RunResult.text without the driver's `=== ...` header and exit lines."""
    return '\n'.join(ln for ln in r.text.splitlines() if not ln.startswith('=== '))


def json_of(r) -> dict:
    """The one JSON object a tool printed over several lines (cli.py status/leftovers --json), or {}."""
    t = body(r)
    try:
        d = json.loads(t[t.index('{'):t.rindex('}') + 1])
    except ValueError:
        return {}
    return d if isinstance(d, dict) else {}


def last_json(text: str) -> dict:
    for ln in reversed(text.splitlines()):
        ln = ln.strip()
        if ln.startswith('{') and ln.endswith('}'):
            try:
                return json.loads(ln)
            except ValueError:
                continue
    return {}


def wait_activation(ctx, name) -> bool:
    t0 = time.monotonic()
    while time.monotonic() - t0 < ACTIVATION_WAIT_S:
        ctx.check_stop()
        if box(ctx, name):
            return False
        r = ctx.run(tool(ctx, 'turns') + ['frontend', '--instance', name], name='frontend-idle', heavy=False)
        last = (body(r).strip().splitlines() or [''])[-1]
        if 'IDLE' in last and 'activation done' in last:
            time.sleep(SETTLE_S)
            return True
        time.sleep(POLL_S)
    return False


def stop_game(ctx, name, why) -> None:
    """The harness's stop (human-turn hook). A refusal is never routed around (COMMON 22): LeftRunning."""
    b = box(ctx, name)
    if b:
        raise LeftRunning(f'a box is open on {name} ({b}); it is left for its owner or a coordinator (D29, D155)')
    r = ctx.run(tool(ctx, 'cli') + ['-i', name, 'stop', '--human-turn-hook', STOP_HOOK], name='stop', heavy=False,
                timeout=1500)
    if r.rc != 0 and running(ctx, name):
        raise LeftRunning(f'cli.py stop on {name} after {why}: exit {r.rc} ({r.log}); the game is left running for '
                          'the MC (a refused stop is not routed around, COMMON 22)')


def end_before_restore(ctx, name, why) -> dict | None:
    """Ticket 0634: a game this stage launched may still run when a replay is cut short (the take's STOP file, a gate's
    refusal or an exception after a launch: the next ctx.run raises before the replay's own end and stop). The restore
    is refused while the game runs (set-proxy --original, set-dll and frontend.py restore: "CivilizationV_DX11.exe is
    running"; 0626's rehearsal take, 2026-11-21 01:46), so the game is ended first, by the harness's stop only (its
    human-turn hook and save settle: never while an autosave is written, never a kill; AGENTS.md 4), and the stage
    waits until no game runs on the instance. None when no game runs; else {stop_rc, stop_log, end_s}. A box (never
    pressed, D29, D155), a refused stop (COMMON 22) or a game still running GAME_END_WAIT_S after the stop returned
    raises LeftRunning: the restore is then not run and the claim is kept."""
    if not running(ctx, name):
        return None
    ctx.say(f'{name}: the game this stage launched still runs after {why}; ending it with the harness\'s stop before '
            'the restore (0634; AGENTS.md 4: the stop waits for the saves to settle)')
    b = box(ctx, name)
    if b:
        raise LeftRunning(f'a box is open on {name} ({b}) after {why}; never pressed (D29, D155)')
    r = ctx.run(tool(ctx, 'cli') + ['-i', name, 'stop', '--human-turn-hook', STOP_HOOK], name='stop-before-restore',
                heavy=False, timeout=1500)
    if r.rc not in STOP_ENDING_RCS and running(ctx, name):
        raise LeftRunning(f'cli.py stop on {name} after {why}: exit {r.rc} ({r.log}); the game is left running for '
                          'the MC (a refused stop is not routed around, COMMON 22)')
    t0 = time.monotonic()
    while running(ctx, name):
        if time.monotonic() - t0 >= GAME_END_WAIT_S:
            raise LeftRunning(f'the game on {name} still runs {GAME_END_WAIT_S:.0f} s after cli.py stop returned '
                              f'(exit {r.rc}, {r.log}) after {why}; no second stop and no kill (0700)')
        time.sleep(POLL_S)
    return {'stop_rc': r.rc, 'stop_log': str(r.log), 'end_s': round(time.monotonic() - t0, 1), 'after': why}


def known_flake(j, row) -> dict | None:
    """The journey's `known_flakes` entry that this replay's stop is (exit, recorded step and wait condition all equal),
    or None. Only such a stop earns the one journey-level rerun (lead-0392, 2026-11-16: the game's city screen not
    opening after founding, research human-input-testing.md 8.5); any other failure stays a failure."""
    for f in j.get('known_flakes') or []:
        if (row.get('ran') and row.get('exit') == f.get('exit') and row.get('stop_step') == f.get('step')
                and f.get('cond') and f.get('cond') == row.get('stop_cond')):
            return f
    return None


def play_with_rerun(ctx, st, j, route, name, user, saves_dir, ini, out_root) -> dict:
    """play(); when it stopped on a known flake of the journey, the whole journey once more from a fresh launch and
    load (a journey-level rerun, never an input retry: contract v1.9 holds). The row is the rerun's, with the first
    run in `first_run` and `known_flake` naming why; a second stop stays the failure it is."""
    row = play(ctx, st, j, route, name, user, saves_dir, ini, out_root)
    f = known_flake(j, row)
    if not f:
        return row
    ctx.say(f'{j["name"]} on {route}: stopped on a known flake ({f.get("why")}); the whole journey once more')
    again = play(ctx, st, j, route, name, user, saves_dir, ini, out_root, suffix='-rerun')
    again['first_run'] = {k: row.get(k) for k in ('exit', 'steps_done', 'checkpoints', 'why', 'out', 'result',
                                                   'stop_step', 'stop_cond', 'took_s')}
    again['known_flake'] = f.get('why')
    return again


def crashed(ctx, name, res, c, when, j, route) -> dict:
    """0533: the row of a replay whose game crashed without a box: the crash (crash_end's record) in `crash`, a failed
    replay (a product finding: journey_verdict makes the journey failed), the leftovers checked as after any end (a
    process left, the patch's popup helper included, stays LeftRunning). The caller goes on with the next replay; the
    instance is restored by hash and released at the stage's end."""
    where = f' at {c["address"]}' if c.get('address') else ''
    res.update(crash=c, crashed_when=when,
               why=(f'the game crashed without a box {when}: {c.get("code") or "?"}{where} (signature '
                    f'{c.get("signature")}; {c.get("source")}; evidence {(c.get("collect") or {}).get("dest")})'))
    if res.get('exit') in (None, 0):
        res['exit'] = 8
    ctx.say(f'{j["name"]} on {route}: {res["why"][:400]}; a crashed game, not a harness stop (0533): the instance is '
            'restored and released as after a normal end')
    lo = ctx.run(tool(ctx, 'cli') + ['-i', name, 'leftovers', '--json'], name='leftovers', heavy=False)
    left = json_of(lo).get('leftovers')
    res['leftovers'] = left if left is not None else f'unreadable ({lo.log})'
    if left:
        raise LeftRunning(f'processes left under {name} after the crash of {j["name"]} on {route}: {left}')
    return res


def play(ctx, st, j, route, name, user, saves_dir, ini, out_root, suffix='') -> dict:
    """One replay of journey j on route. Returns its summary row; raises LeftRunning when the game cannot be left."""
    res = {'route': route, 'journey': j['name']}
    ctx.check_stop()
    r = ctx.run(tool(ctx, 'set_ini') + [ini, f'mode={MODE[route]}'], name=f'mode-{route}', heavy=False)
    if r.rc != 0:
        return dict(res, exit=None, ran=False, why=f'set_ini mode={MODE[route]}: exit {r.rc} ({r.log})')
    r = ctx.run(tool(ctx, 'frontend') + ['-i', name, 'prepare', '--mods', j['mods']], name='frontend-prepare',
                heavy=False)
    if r.rc != 0:
        return dict(res, exit=None, ran=False, why=f'frontend.py prepare: exit {r.rc} ({r.log})')
    res['removed_saves'] = clear_made(user, j)
    try:
        place_start(user, j, saves_dir)
    except (OSError, RuntimeError) as e:
        return dict(res, exit=None, ran=False, why=str(e))
    b = box(ctx, name)
    if b:
        gone = ended_crash(ctx, name)
        if not gone:
            raise LeftRunning(f'a box is open on {name} before the launch ({b})')
        # 2604 reruns (MC #3689): the marker of a game that crashed WITHOUT a box after its replay had ended (the -ia
        # test DLL's known exit crash, 0484) holds no game: nothing runs, nothing is to be pressed. It is recorded in
        # this row and the launch below moves it aside (dialogwatch.archive_stale_marker), as for any next launch.
        res['earlier_exit_crash'] = gone
        ctx.say(f'{name}: the earlier game crashed without a box ({gone[:160]}); nothing runs; going on')
    since = time.time()
    r = ctx.run(tool(ctx, 'launch') + [name], name=f'launch-{route}', gated=True)
    info = last_json(r.text)
    if r.rc != 0 or not info.get('pid'):
        if running(ctx, name):
            stop_game(ctx, name, 'a launch that did not report')
        return dict(res, exit=None, ran=False, why=f'launch.py: exit {r.rc} ({r.log})')
    pid = info['pid']
    res['pid'] = pid
    g = None
    for k in range(GUARD_TRIES):
        g = ctx.run(tool(ctx, 'guard_check') + [str(pid), '--instance', name], name='guard', heavy=False)
        if g.rc != 2:
            break
        time.sleep(POLL_S)
    if g.rc != 0:
        c = crash_end(ctx, name, user, pid, since)
        if c:
            return crashed(ctx, name, dict(res, exit=None, ran=False), c, 'before the replay (guard check)', j, route)
        stop_game(ctx, name, 'a guard check that failed')
        return dict(res, exit=None, ran=False, why=f'guard_check.py: exit {g.rc} ({g.log})')
    if not wait_activation(ctx, name):
        c = crash_end(ctx, name, user, pid, since)
        if c:
            return crashed(ctx, name, dict(res, exit=None, ran=False), c, 'during mod activation', j, route)
        b = box(ctx, name)
        if b:
            raise LeftRunning(f'a box is open on {name} during mod activation ({b})')
        stop_game(ctx, name, 'no mod activation')
        return dict(res, exit=None, ran=False, why=f'the mods were not activated within {ACTIVATION_WAIT_S} s')
    out = out_root / f'{j["name"]}-{route}{suffix}'
    out = _common.fresh_dir(out)
    r = ctx.run(tool(ctx, 'hi') + ['-i', name, '--by', owner(ctx, st), 'replay', j['trace_path'], '--route', route,
                                   '--out', out, '--ticket', st['ticket']],
                name=f'replay-{j["name"]}-{route}{suffix}', heavy=False, timeout=REPLAY_TIMEOUT_S)
    summ = last_json(r.text)
    if not summ:
        summ = {'exit': r.rc, 'steps_done': 0, 'checkpoints': [], 'stop': {'why': f'no result line ({r.log})'}}
    (out.parent / f'{out.name}.result.json').write_text(json.dumps(summ, indent=1, default=str), encoding='utf-8')
    res.update(ran=True, exit=summ.get('exit'), steps_done=summ.get('steps_done'), took_s=summ.get('took_s'),
               checkpoints=[{'name': c.get('name'), 'ok': c.get('ok')} for c in summ.get('checkpoints') or []],
               why=(summ.get('stop') or {}).get('why'), out=str(out), result=str(out.parent / f'{out.name}.result.json'),
               missing_pictures=len(summ.get('missing_pictures') or []),
               stop_step=(summ.get('stop') or {}).get('step'),
               stop_cond=((summ.get('stop') or {}).get('wait') or {}).get('cond'))
    if summ.get('exit') == 8 or (summ.get('exit') != 0 and not running(ctx, name)):
        # 0533: a crash without a box ended the game: a crashed replay (evidence, then the usual restore and release),
        # not a game left running; a box (or a game that still runs) stays LeftRunning (D29, D155)
        c = crash_end(ctx, name, user, pid, since)
        if c:
            return crashed(ctx, name, res, c, f'in the replay (step {res.get("stop_step")})', j, route)
    if summ.get('exit') == 8:
        raise LeftRunning(f'the replay of {j["name"]} on {route} met a box on {name} (exit 8; '
                          f'{res["why"]}); never pressed (D29, D155)')
    if summ.get('exit') == 0 and j['ends'] == 'exit':
        # the journey's last step is the game's own EXIT and Yes: the process must be gone soon after (a player's exit
        # that leaves the game running is a fault of the journey's route); the clean-up is the harness's stop either way
        t0 = time.monotonic()
        while running(ctx, name) and time.monotonic() - t0 < EXIT_WAIT_S:
            time.sleep(POLL_S)
        res['exit_wait_s'] = round(time.monotonic() - t0, 1)
        if running(ctx, name):
            res.update(exit=1, why=f'the game was still running {EXIT_WAIT_S} s after the journey\'s EXIT and Yes')
    if running(ctx, name):
        stop_game(ctx, name, f'the replay of {j["name"]} (exit {res.get("exit")})')
    lo = ctx.run(tool(ctx, 'cli') + ['-i', name, 'leftovers', '--json'], name='leftovers', heavy=False)
    left = json_of(lo).get('leftovers')
    res['leftovers'] = left if left is not None else f'unreadable ({lo.log})'
    if left:
        raise LeftRunning(f'processes left under {name} after {j["name"]} on {route}: {left}')
    return res


def compare(ctx, j, rows, out_root) -> dict:
    p, s = rows.get('product'), rows.get('stock')
    if not (p and s and p.get('ran') and s.get('ran')):
        return {'counts': None, 'why': 'both routes are needed for the comparison'}
    cmp_out = out_root / f'{j["name"]}-compare.json'
    r = ctx.run(tool(ctx, 'stockctl') + ['compare', p['result'], s['result'], '--out', cmp_out],
                name=f'compare-{j["name"]}', heavy=False)
    c = last_json(r.text)
    if r.rc != 0 or not c:
        return {'counts': None, 'why': f'stockctl.py compare: exit {r.rc} ({r.log})'}
    return {'counts': c.get('counts'), 'in_step': c.get('in_step'), 'file': str(cmp_out),
            'rows': [{k: x.get(k) for k in ('name', 'class', 'why')} for x in c.get('rows') or c.get('checkpoints') or []]}


def journey_verdict(rows: dict, want: list) -> str:
    if any((rows.get(r) or {}).get('crash') for r in want):
        return 'failed'          # 0533: a game that crashed without a box is a product finding, whatever the other route did
    if any(not (rows.get(r) or {}).get('ran') for r in want):
        return 'incomplete'
    if all(rows[r].get('exit') == 0 and all(c.get('ok') for c in rows[r].get('checkpoints') or []) for r in want):
        flakes = sorted({rows[r]['known_flake'] for r in want if rows[r].get('known_flake')})
        return f'passed on rerun (known: {"; ".join(flakes)})' if flakes else 'passed'
    return 'failed'


def passed(v: str) -> bool:
    return v == 'passed' or str(v).startswith('passed on rerun')


def overall(journeys: dict) -> str:
    vs = [j['verdict'] for j in journeys.values()]
    if vs and all(passed(v) for v in vs):
        return 'passed'
    if 'failed' in vs:
        return 'failed'
    return 'incomplete'


# ------------------------------------------------------------------------------------------------- the report

def layout_words(o: dict) -> str:
    """' on the installer-installed layout (<instance>)' for outputs played with C5_RELEASE_HI_INSTALLED, else ''
    (the default path's lines stay as they were)."""
    lay = (o or {}).get('layout') or {}
    if lay.get('kind') != INSTALLED:
        return ''
    return f' on the {INSTALLED} layout ({lay.get("instance")})'


def write_report(ctx, out: dict) -> dict:
    d = ctx.dir('hi_journey')
    jp, mp = d / 'report.json', d / 'report.md'
    out = dict(out, report=str(mp), report_json=str(jp))
    jp.write_text(json.dumps(out, indent=1, default=str) + '\n', encoding='utf-8')
    gating = ('gates: a take for the owner; D162 (the owner, 2026-11-16): no build to the owner before both journeys '
              'pass on it' if out.get('gating') else
              'reports only, does not gate: not a take for the owner; a build for the owner needs --for-owner and a '
              'pass, D162')
    lay = layout_words(out)
    L = [f'# HI release journeys of {ctx.tag}{lay}', '',
         f'**Verdict: {out.get("verdict")}{lay}** ({gating})', '',
         f'Written {time.strftime("%Y-%m-%d %H:%M:%S")} by tools/release/stages/hi_journey.py (issues 4506, 0436); '
         f'work folder `{ctx.work}`.', '']
    if out.get('owner'):
        a = out['owner']
        L += [f'**For the owner (D162): {"yes" if a.get("allowed") else "NO"}**'
              + ('' if a.get('allowed') else f': {"; ".join(a.get("why") or [])}'), '']
    if out.get('candidate'):
        L += ['## Release candidate', ''] + [f'- {k}: `{v}`' for k, v in out['candidate'].items()] + ['']
    if out.get('instance'):
        L += [f'Instance `{out["instance"]}`; claim ticket {out.get("ticket")}, by `{out.get("by")}`.', '']
    if lay:
        ly = out['layout']
        L += ['## The installer-installed layout (C5_RELEASE_HI_INSTALLED)', '',
              f'- record `{ly.get("record")}`: release {ly.get("release")}, variant {ly.get("variant")}, config '
              f'{ly.get("config")}, form {ly.get("form")}, Community Patch folder `{ly.get("cpdir")}`',
              f'- found: {json.dumps(ly.get("sha256"))}; the build: {json.dumps(ly.get("want"))}',
              f'- fallback (mode=off; the stock route is not played on this layout, 0640) '
              f'`CvGameCore_Expansion2_x86.dll` {ly.get("fallback_sha256")}; '
              f'vpproxy.ini {ly.get("ini_sha256")}',
              '- never run: provision.py set-dll, set-proxy; no test pack' +
              (f'; refused: {"; ".join(ly["why"])}' if ly.get('why') else ''), '']
    if out.get('not_played'):
        L += [f'Not played: {out["not_played"]}', '']
    for name, j in (out.get('journeys') or {}).items():
        L += [f'## {name}: {j.get("verdict")}{lay}', '', '| Route | Exit | Steps | Checkpoints | Time (s) | Why |',
              '|---|---|---|---|---|---|']
        for route, r in (j.get('routes') or {}).items():
            for label, x in (([(f'{route} (first run)', r['first_run'])] if r.get('first_run') else [])
                             + [(f'{route} (rerun)' if r.get('first_run') else route, r)]):
                cps = ', '.join(f'{c["name"]} {"ok" if c["ok"] else "FAIL"}' for c in x.get('checkpoints') or [])
                why = str(x.get('why') or '').replace('|', '/').replace('\n', ' ')[:240]
                L.append(f'| {label} | {x.get("exit")} | {x.get("steps_done")} | {cps} | {x.get("took_s")} | {why} |')
        reruns = [f'{route}: {r["known_flake"]}' for route, r in (j.get('routes') or {}).items() if r.get('known_flake')]
        if reruns:
            L += ['', 'Rerun once on a known flake (research human-input-testing.md 8.5): ' + '; '.join(reruns)]
        for route, r in (j.get('routes') or {}).items():
            c = r.get('crash')
            if c:
                L += ['', f'**Crashed without a box on {route}** {r.get("crashed_when")} (a product finding, ticket '
                      f'0533; COMMON 30: the root cause goes into research/): code {c.get("code")}, address '
                      f'{c.get("address")} (VA {c.get("va")}), pid {c.get("pid")}; signature `{c.get("signature")}`; '
                      f'proved by {c.get("source")}; record `{c.get("record")}`; crash.collect by '
                      f'{(c.get("collect") or {}).get("by")} into `{(c.get("collect") or {}).get("dest")}`']
        c = j.get('compare') or {}
        L += ['', f'Comparison: {json.dumps(c.get("counts"))}' + (f' ({c.get("why")})' if c.get('why') else ''), '']
        for row in c.get('rows') or []:
            L.append(f'- {row.get("name")}: {row.get("class")}' + (f' ({row.get("why")})' if row.get('why') else ''))
        L.append('')
    if out.get('restore'):
        rs = out['restore']
        L += ['## Restore', '', f'ok: {rs.get("ok")}; verify rc {rs.get("verify_rc")}', '']
        if out.get('ended_before_restore'):
            e = out['ended_before_restore']
            L += [f'The game was still running after {e.get("after")}: ended by cli.py stop (exit {e.get("stop_rc")}) '
                  f'{e.get("end_s")} s before the restore (0634).', '']
        L += [f'- {x}' for x in rs.get('errors') or []]
        L.append('')
    if out.get('left_changed'):
        L += ['## LEFT CHANGED (claim kept)', '', f'Instance `{out.get("instance")}`: '
              + ('; '.join(map(str, out['left_changed']))), '']
        if out.get('left_running'):
            L += [f'Not restored: {out["left_running"]}', '']
    mp.write_text('\n'.join(L) + '\n', encoding='utf-8')
    return out


# ------------------------------------------------------------------------------------------------- run

def run(ctx):
    if installed_path_why_not(ctx) is None:
        return _run_tier_path(ctx)
    return report.finish(ctx, gate(ctx, _run(ctx)))


# ------------------------------------------------------------------------------------------------- 0671: the tier's path

TIER_STAGE = REPO / 'tools' / 'release' / 'tiers' / 'stages' / 'hi_journey.py'
TIER_MODULE = 'take_release_tier_stage_hi_journey'


def installed_path_why_not(ctx) -> str | None:
    """Ticket 0671: None when this take's journeys are played the tier's way (D221: the product route on the layout
    the installer's test build leaves, so with the composite and its back-buffer pictures; the stock route fresh on
    the stage-laid layout after the removal), else why not. Until 0671 the validated plan's journeys were played on
    this stage's own layout, which never lays shell_composite.dll: its product route was not the shipped product and
    its pictures were PrintWindow (validation-tiers.md 19)."""
    if __name__.startswith('take_release_original_'):
        return 'the tier stage\'s own copy of this module (tierlib.original): it plays the routes it is given'
    if V.setting('C5_RELEASE_HI_INSTALLED', None):
        return 'C5_RELEASE_HI_INSTALLED names an installed instance: the by-hand path of 2604'
    if V.setting('C5_RELEASE_HI_ROUTES', None):
        return 'C5_RELEASE_HI_ROUTES is set from outside: a by-hand choice of routes on the stage-laid layout'
    if V.setting('C5_RELEASE_HI_STAGE_LAID', None) == '1':
        return ('C5_RELEASE_HI_STAGE_LAID=1: the stage-laid layout asked for (no composite: PrintWindow pictures, '
                'not the shipped product; diagnostic only)')
    if getattr(ctx, '_d', None) is None:
        return 'no driver (a direct call of this module): the stage-laid path'
    if custom_tools(ctx):
        return 'the driver\'s tools are not the defaults (a test or a --tools file): the stage-laid path'
    return None


def tier_module():
    """tools/release/tiers/stages/hi_journey.py under a module name of its own, compiled from the source (as
    take_release.load_stage_module does: no byte cache hides an edit). It loads this file's code again as its
    original (tierlib.original), whose installed_path_why_not() is never None, so nothing recurses."""
    if TIER_MODULE not in sys.modules:
        import types
        mod = types.ModuleType(TIER_MODULE)
        mod.__file__ = str(TIER_STAGE)
        sys.modules[TIER_MODULE] = mod
        try:
            exec(compile(TIER_STAGE.read_text(encoding='utf-8'), str(TIER_STAGE), 'exec'), mod.__dict__)
        except BaseException:
            del sys.modules[TIER_MODULE]
            raise
    return sys.modules[TIER_MODULE]


def _run_tier_path(ctx):
    T = tier_module()
    ctx.say('hi_journey: the journeys are played the tier\'s way (ticket 0671, D221): the installer\'s test build on a '
            'clean instance of EXE build 26577625, the product route on that layout (with the composite), the stock '
            'route fresh on the stage-laid layout after the removal; C5_RELEASE_HI_STAGE_LAID=1 plays the old layout')
    res = T.gate(ctx, T._run(ctx))
    if isinstance(res, StageResult):
        if res.status == 'stop' and res.unblock:
            # the tier's gate names its own driver; this take is take_release.py's validated plan
            res.unblock = res.unblock.replace(f'take_tier.py {ctx.tag} --tier', f'take_release.py {ctx.tag} --tier')
        res.outputs = dict(res.outputs or {}, played_by='the tier stage\'s path (ticket 0671)')
    return report.finish(ctx, res)


def product_hashes(b: dict) -> dict:
    """{proxy_dll, x64_dll, server, i0ia_dll: SHA-256 (lower case)} of a build stage's outputs: from its x64_hashes,
    else hashed from the files it names; a file that is neither is left out."""
    xh = b.get('x64_hashes') or {}
    out = {}
    for role, rel in PRODUCT_FILES.items():
        h = (xh.get(rel) or {}).get('sha256') if isinstance(xh.get(rel), dict) else None
        if not h:
            p = Path(b['proxy_dll']) if role == 'proxy_dll' and b.get('proxy_dll') else (
                Path(b['x64_layout']) / rel if b.get('x64_layout') else None)
            h = sha256_file(p) if p is not None and p.is_file() else None
        if h:
            out[role] = h.lower()
    ia = b.get('i0ia_dll_sha256')
    if not ia and b.get('i0ia_dll') and Path(b['i0ia_dll']).is_file():
        ia = sha256_file(b['i0ia_dll'])
    if ia:
        out['i0ia_dll'] = ia.lower()
    return out


def candidate_from(b: dict) -> dict:
    keep_keys = ('x64_commit', 'i0_commit', 'x64_layout', 'proxy_dll', 'i0ia_dll', 'tools_commit')
    c = {k: b.get(k) for k in keep_keys if b.get(k)}
    c['sha256'] = product_hashes(b)
    return c


def candidate_of(ctx) -> dict:
    return candidate_from(ctx.outputs('build'))


# ------------------------------------------------------------------------------------------------- D162

def for_owner(ctx) -> bool:
    """The take is for a build meant for the owner (take_release.py --for-owner, recorded in the state's config; a
    config written before ticket 0708 names it for_art)."""
    cfg = getattr(ctx, 'config', None) or {}
    return bool(cfg.get('for_owner', cfg.get('for_art')))


def owner_check(entry: dict | None, build: dict | None) -> dict:
    """D162's answer for a recorded hi_journey entry ({status, outputs, error?, stop?}) and the build stage's outputs
    now: {allowed, verdict, status, report, why: [reasons it is refused], decision}. Pure: reads no file but the
    build's product files when its outputs carry no hashes."""
    e = entry or {}
    o = e.get('outputs') or {}
    status = e.get('status') or 'not run'
    v = o.get('verdict') or 'not run'
    why = []
    if status != 'ok':
        detail = (o.get('skipped_why') or e.get('what') or e.get('error') or (e.get('stop') or {}).get('what')
                  or o.get('not_played') or '')
        why.append(f'the stage is {status}' + (f' ({str(detail)[:300]})' if detail else '')
                   + ': not played is not passed')
    elif v != 'passed':
        why.append(f'verdict {v}')
    js = o.get('journeys') or {}
    major = int((build or {}).get('major_civs') or o.get('major_civs') or MAJOR_CIVS_DEFAULT)
    want = d162_journeys(major)
    if not want:
        why.append(f'no D162 journeys are defined for {major} major civilizations (known: {sorted(D162_BY_MAJOR)})')
    for name in want:
        j = js.get(name)
        if not j:
            if status == 'ok':
                why.append(f'{name} was not played')
            continue
        if not passed(j.get('verdict')):
            why.append(f'{name}: {j.get("verdict")}')
        miss = [r for r in ROUTES if not ((j.get('routes') or {}).get(r) or {}).get('ran')]
        if miss:
            why.append(f'{name}: route {"/".join(miss)} not played')
    if status == 'ok' and o.get('index_committed') is not True:
        why.append(f'the journeys came from {o.get("index") or "an unrecorded index"}, not the committed '
                   'tools/hi/journeys/journeys.json')
    if build is not None and status == 'ok':
        want, got = candidate_from(build), o.get('candidate') or {}
        for k in ('x64_commit', 'i0_commit', 'tools_commit'):
            if want.get(k) and got.get(k) != want.get(k):
                why.append(f'played on another build ({k} {str(got.get(k))[:12]}, the build is {want[k][:12]})')
        ws, gs = want.get('sha256') or {}, got.get('sha256') or {}
        diff = sorted(r for r in ws if gs.get(r) != ws[r])
        if not ws:
            why.append('the build has no product hashes to compare')
        elif diff:
            why.append(f'played on other bytes: {", ".join(diff)} differ from the build\'s')
    res = {'decision': 'D162', 'allowed': not why, 'verdict': v, 'status': status, 'report': o.get('report'),
           'why': why}
    if layout_words(o):
        res['layout'] = layout_words(o).strip()
    return res


# ticket 0708: the name before it, kept for callers outside tools/release (tools/hi/journeys/tests/
# test_m43_selection.py)
art_check = owner_check


def owner_line(chk: dict, tag: str = '') -> str:
    """One line for a summary or a hand-out: the journeys' verdict and whether the build may go to the owner."""
    lay = f' {chk["layout"]}' if chk.get('layout') else ''
    head = (f'D162 HI release journeys{" of " + tag if tag else ""}{lay}: verdict {chk["verdict"]} '
            f'(stage {chk["status"]})')
    rep = f'; report {chk["report"]}' if chk.get('report') else ''
    if chk['allowed']:
        return f'{head}{rep}; for the owner: yes (both journeys passed on this build, product and stock)'
    return f'{head}{rep}; for the owner: NO, refused by D162: {"; ".join(chk["why"])}'


def again(ctx, entry) -> str | None:
    """take_release's hook: on a take for the owner, an ok or skipped entry recorded with unchanged inputs is played
    again when it would not pass D162 (a report-only take that failed or was skipped on this build)."""
    if not for_owner(ctx):
        return None
    chk = owner_check(entry, ctx.outputs_if_ok('build'))
    return None if chk['allowed'] else (f'a take for the owner and the recorded result does not pass D162 '
                                        f'({"; ".join(chk["why"])})')


def gate(ctx, res):
    """D162 on a take for the owner: an ok or skipped result that owner_check() refuses becomes a stop of the tier;
    other takes and failed results pass through unchanged (a failed one carries the refusal in `owner` too)."""
    if not for_owner(ctx) or not isinstance(res, StageResult):
        return res
    e = {'status': res.status, 'outputs': dict(res.outputs or {}), 'what': res.what}
    if res.status == 'skipped':
        e['outputs'].setdefault('skipped_why', res.what)
    chk = owner_check(e, ctx.outputs_if_ok('build'))
    outs = dict(res.outputs or {}, owner=chk, gating=True)
    if outs.get('report_json'):
        try:
            write_report(ctx, outs)
        except OSError as ex:
            ctx.say(f'hi_journey report not rewritten: {ex}')
    ctx.say(owner_line(chk, ctx.tag))
    if res.status not in ('ok', 'skipped') or chk['allowed']:
        res.outputs = outs
        return res
    rep = f'read {chk["report"]}; ' if chk.get('report') else ''
    return StageResult.stop(
        f'D162: a take for the owner and the HI release journeys did not pass on this build: {"; ".join(chk["why"])}',
        f'{rep}fix what it names (a failed journey; or what kept them from being played: a free instance, the start '
        f'saves in C5_RELEASE_HI_SAVES, the instance-aware DLL), then run take_release.py {ctx.tag} --tier validated '
        f'--for-owner again (the stage plays the journeys again); no build goes to the owner before they pass (D162)',
        **outs)


def _run(ctx):
    st = settings()
    idx_path = Path(st['index'] or INDEX)
    major = take_major(ctx)
    out = {'gating': for_owner(ctx), 'candidate': candidate_of(ctx), 'index': str(idx_path),
           'index_committed': idx_path.resolve() == Path(INDEX).resolve(), 'major_civs': major}
    try:
        idx = load_index(st['index'], take_vp(ctx, st))
        out['vp'] = idx['vp']
        js, routes = chosen(idx, st, major), routes_of(st)
    except IndexError_ as e:
        return StageResult.skipped(f'the journeys cannot be played: {e}', **write_report(ctx, dict(out, not_played=str(e),
                                                                                                  verdict='incomplete')))
    if st.get('installed'):
        # 0640: before anything is claimed or started, the routes of the installed layout (product only)
        try:
            routes = installed_routes(st['installed'], routes)
        except InstalledStockRefused as e:
            why = f'REFUSED, nothing claimed or played: {e}'
            ctx.say(f'LOUD: hi_journey {why}')
            return StageResult.stop(
                why, 'unset C5_RELEASE_HI_ROUTES (the installed layout plays the product route) or set it to product; '
                'a fresh stock route is played on the stage-laid layout: after the installer\'s removal, with '
                'C5_RELEASE_HI_ROUTES=stock and C5_RELEASE_HI_INSTANCE=<the instance> and without '
                'C5_RELEASE_HI_INSTALLED, or by the validated plan\'s default path (the tier stage, ticket 0671), which '
                'plays the product route installed, removes the layout, then the stock route stage-laid',
                **write_report(ctx, dict(out, layout={'kind': INSTALLED, 'instance': st['installed']},
                                         refused_routes=routes, not_played=why, verdict='incomplete')))
    saves_dir = Path(st['saves'] or idx.get('saves_dir') or '.')
    missing = [j['start_save']['file'] for j in js if not (saves_dir / j['start_save']['file']).is_file()]
    if missing:
        why = f'start saves missing in {saves_dir}: {missing} (C5_RELEASE_HI_SAVES names their folder)'
        return StageResult.skipped(why, **write_report(ctx, dict(out, not_played=why, verdict='incomplete')))
    b = ctx.outputs('build')
    if st.get('installed'):
        out['layout'] = {'kind': INSTALLED, 'instance': st['installed']}
    if not b.get('i0ia_dll') and not st.get('installed'):
        why = 'the build has no instance-aware 32-bit DLL (i0ia_dll; a run with --skip-ia): no instance can run it'
        return StageResult.skipped(why, **write_report(ctx, dict(out, not_played=why, verdict='incomplete')))
    # the tools guard comes after the reasons to skip (nothing to play is not a fault) and before anything is started
    miss = missing_tools(ctx)
    if miss:
        why = (f'the tools of the driver are not the defaults (a test or a --tools file) and do not name {miss}: the '
               'real tools are not run against what may be a fake instance')
        return StageResult.failed(why, **write_report(ctx, dict(out, not_played=why, verdict='incomplete')))
    got, why = claim(ctx, st)
    if not got:
        why = f'no test instance could be claimed ({why})'
        return StageResult.skipped(why, **write_report(ctx, dict(out, not_played=why, verdict='incomplete')))
    name, root = got
    inst = root / name
    out.update(instance=name, root=str(root), ticket=st['ticket'], by=owner(ctx, st), routes=routes)
    ctx.partial(instance=name)
    prev = unrestored(inst)
    if prev:
        # 0650: an earlier run's restore was never finished (it ended on a box): its journey settings would be taken as
        # this run's "before" state. Nothing is laid; this run's claim is given back (it changed nothing).
        rel = ctx.run(tool(ctx, 'pool') + ['release', name, '--owner', owner(ctx, st)], name='release', heavy=False)
        why = (f'LOUD: {name} still carries an earlier hi_journey run that was never restored (marker {prev["marker"]}, '
               f'record {prev.get("record")}, state {prev.get("state")}, not done: {prev.get("pending")}'
               + (f', {prev["error"]}' if prev.get('error') else '') + '): its journey settings would be this run\'s '
               f'"before" state; nothing laid, this claim released (exit {rel.rc}). Finish it first: python -I '
               f'tools/release/stages/hi_journey.py finish "{prev.get("record")}" (after the box is closed)')
        ctx.say(why)
        return StageResult.failed(why, **write_report(ctx, dict(out, unrestored=prev, claim_released=rel.rc == 0,
                                                                not_played=why, verdict='incomplete')))
    undo = undo_record(ctx, name, root, inst, owner(ctx, st), st)
    out['undo_record'] = str(undo.path)
    before_owner = os.environ.get('C5_POOL_OWNER')
    os.environ['C5_POOL_OWNER'] = owner(ctx, st)       # provision.py, frontend.py and the harness read the claim owner
    try:
        return _claimed(ctx, st, b, js, routes, name, inst, saves_dir, out, undo)
    finally:
        if before_owner is None:
            os.environ.pop('C5_POOL_OWNER', None)
        else:
            os.environ['C5_POOL_OWNER'] = before_owner


def _claimed(ctx, st, b, js, routes, name, inst, saves_dir, out, undo):
    raised = None
    try:
        res = _lay_and_play(ctx, st, b, js, routes, name, inst, saves_dir, out, undo)
    except LeftRunning as e:
        out['left_running'] = str(e)
        res = StageResult.failed(f'{e}; the instance keeps the release layout and the claim ({owner(ctx, st)}) until '
                                 f'a coordinator closes the game and restores it (undo list: {[u[0] for u in undo]})')
    except (GateRefused, StopRequested) as e:
        raised, res = e, None
    except Exception as e:  # noqa: BLE001 - the restore below runs whatever happened
        res = StageResult.failed(f'{type(e).__name__}: {e}')
    if out.get('left_running'):
        left(undo, out['left_running'])
        res.what = f'{res.what}; {finish_hint(undo)}'
        res.outputs.update(write_report(ctx, dict(out, verdict=out.get('verdict', 'incomplete'))))
        return res
    # the restore runs to its end even when the stop file is there (it is what stopped the replays): the instance is
    # never left half laid
    ctx.check_stop = lambda: None
    try:
        if res is None or res.status != 'ok':
            # 0634: a replay cut short (STOP, a gate's refusal, an exception after a launch) can leave the game running;
            # it is ended first, else the restore is refused by every tool that changes the instance
            why = (f'{type(raised).__name__} ({raised})' if raised is not None else
                   f'the stage {res.status if res else "?"}: {str(res.what if res else "")[:200]}')
            try:
                ended = end_before_restore(ctx, name, why)
            except LeftRunning as e:
                out['left_running'] = str(e)
                out['claim_released'] = False
                out['left_changed'] = [u[0] for u in undo]
                left(undo, str(e))
                rep = write_report(ctx, dict(out, verdict=out.get('verdict', 'incomplete'),
                                             not_played=f'interrupted: {why}'))
                return StageResult.failed(
                    f'GAME ON {name} NOT ENDED, NOTHING RESTORED: {e}; {name} keeps the release layout (left changed: '
                    f'{out["left_changed"]}) and the claim ({owner(ctx, st)}) is KEPT until a coordinator ends the game '
                    f'and restores the instance, then releases the claim; {finish_hint(undo)}', **rep)
            if ended:
                out['ended_before_restore'] = ended
        rs = _restore(ctx, name, undo, out if st.get('installed') else None)
        out['restore'] = rs
        out['restore_record'] = str(write_restore_record(ctx.dir('hi_journey'), rs, by='the stage', instance=name))
        if not rs['ok']:
            out['claim_released'] = False
            out['left_changed'] = rs.get('left_changed') or rs['errors']
            left(undo, f'restore failed: {rs["errors"]}')
            rep = write_report(ctx, dict(out, verdict=out.get('verdict', 'incomplete')))
            return StageResult.failed(f'RESTORE OF {name} FAILED: {rs["errors"]}; left changed on {name}: '
                                      f'{out["left_changed"]}; the claim ({owner(ctx, st)}) is KEPT; repair the '
                                      f'instance, then release it; {finish_hint(undo)}', **rep)
        restored(undo)
        rel = ctx.run(tool(ctx, 'pool') + ['release', name, '--owner', owner(ctx, st)], name='release', heavy=False)
        out['claim_released'] = rel.rc == 0
    finally:
        del ctx.check_stop
    if raised is not None:
        write_report(ctx, dict(out, verdict='incomplete', not_played=f'interrupted: {raised}'))
        raise raised
    rep = write_report(ctx, out)
    res.outputs.update(rep)
    return res


def _restore(ctx, name, undo, installed_out=None) -> dict:
    """Every undo in reverse order (files back by SHA-256, provision.py set-proxy --original and set-dll back), the front
    end checked, provision.py verify --quick (the user tree by hash). {ok, errors, done, verify_rc, left_changed: the
    labels of what is not back (0634)}."""
    errors, done, left = [], [], []
    rec = isinstance(undo, Undo)
    for i in reversed(range(len(undo))):
        label, fn = undo[i]
        if rec and undo.specs[i].get('done'):
            continue            # 0650: done in an earlier pass (a restore finished after a box)
        try:
            ok, why = fn()
        except Exception as e:  # noqa: BLE001
            ok, why = False, f'{type(e).__name__}: {e}'
        (done if ok else errors).append(f'{label}: {why}')
        if rec:
            undo.mark(i, ok, why)
        if not ok:
            left.append(label)
    fe = frontend_files(ctx, name)
    if fe is None or not fe or any(v != 'original' for v in fe.values()):
        errors.append(f'front end not original after the restore (frontend.py status): {fe}')
        left.append(f'front end ({fe})')
    if installed_out is not None:
        # 2604: an installer-installed instance is not what its provisioning manifest lists, so provision.py verify
        # would name the installer's files; the layout is checked again instead (its three binaries, the fallback and
        # vpproxy.ini by SHA-256 as found before the run). The instance's verify follows the installer's removal.
        lay = installed_out.get('layout') or {}
        if lay.get('user'):
            again_ = installed_layout(ctx.outputs('build'), Path(lay['user']))
            for k in ('sha256', 'fallback_sha256', 'ini_sha256'):
                if again_.get(k) != lay.get(k):
                    errors.append(f'installed layout changed by the run: {k} {again_.get(k)} (was {lay.get(k)})')
                    left.append(f'installed layout {k}')
        return {'ok': not errors, 'errors': errors, 'done': done, 'verify_rc': None, 'left_changed': left,
                'verify': 'installed layout re-checked by SHA-256 (provision.py verify follows the removal)'}
    try:
        v = ctx.run(tool(ctx, 'provision') + ['verify', name, '--quick'], name='verify',
                    what=f'{ctx.who} take_release {ctx.tag} hi_journey verify --quick')
        vrc = v.rc
        if v.rc != 0:
            errors.append(f'provision.py verify {name}: exit {v.rc} ({v.log})')
            left.append(f'provision.py verify --quick exit {v.rc} (its log names the files)')
    except GateRefused as e:
        vrc = None
        errors.append(f'provision.py verify {name} not run: {e}')
        left.append('unknown: provision.py verify not run')
    return {'ok': not errors, 'errors': errors, 'done': done, 'verify_rc': vrc, 'left_changed': left}


def frontend_files(ctx, name):
    """{file: "original" | ...} of `frontend.py status` (the 4501 trap: a restore can bring a patched MainMenu.lua
    back, so the three files are checked after every restore), or None when unreadable."""
    r = ctx.run(tool(ctx, 'frontend') + ['-i', name, 'status'], name='frontend-status', heavy=False)
    t = '\n'.join(ln for ln in r.text.splitlines() if not ln.startswith('==='))
    try:
        d = json.loads(t[t.index('{'):t.rindex('}') + 1])
    except ValueError:
        return None
    return {k: (v or {}).get('is') for k, v in (d.get('files') or {}).items()}


def _frontend_restore(ctx, name):
    return undo_fn(ctx, {'kind': 'frontend_restore', 'instance': name})


def _lay(ctx, b, name, inst, out, undo):
    """The release onto the instance (the instance stage's way). Returns (user folder, vpproxy.ini) or a StageResult."""
    m = I.manifest(inst)
    user = inst / (m.get('user_dir') or USER_DEFAULT).replace('\\', '/')
    missing = I.mods_check(ctx, user)
    if missing:
        return StageResult.skipped(f'instance {name} lacks the release\'s mods {missing}')
    if (m.get('proxy') or {}).get('state') == 'installed':
        return StageResult.skipped(f'instance {name} already carries a proxy layout; this stage lays its own only on a '
                                   'clean instance (set-proxy --original first)')
    if (user / RECORD_REL).exists():       # 2604: never set-dll/set-proxy over an installer's install
        return StageResult.skipped(f'instance {name} carries an installer-installed layout ({user / RECORD_REL}); '
                                   f'this stage lays its own only on a clean instance (C5_RELEASE_HI_INSTALLED={name} '
                                   'plays on the installed one)')
    undo.append(('frontend restore', _frontend_restore(ctx, name)))
    I.fix_modinfo(ctx, user, out, undo)
    dll_rel = (m.get('dll') or {}).get('path') or f'{USER_DEFAULT}\\MODS\\(1) Community Patch\\{DLL}'
    cur = inst / dll_rel.replace('\\', '/')
    cur_sha, want_sha = sha256_file(cur), sha256_file(b['i0ia_dll'])
    if cur_sha != want_sha:
        # 0650: kept under its own hash (a later run in this work folder never overwrites it) and checked, so that
        # set-dll back restores exactly the DLL of before the run
        kept = ctx.dir('hi_journey', 'kept', f'dll-{cur_sha[:16]}') / DLL
        shutil.copy2(cur, kept)
        if sha256_file(kept) != cur_sha:
            return StageResult.failed(f'the copy {kept} of {name}\'s DLL does not hash as the original; nothing set')
        out['kept_dll'] = {'path': str(kept), 'sha256': cur_sha}
        r = ctx.run(tool(ctx, 'provision') + ['set-dll', name, '--dll', b['i0ia_dll']], name='set-dll', heavy=False)
        if r.rc != 0:
            return StageResult.failed(f'provision.py set-dll {name}: exit {r.rc}; {r.log}')
        undo.append(('set-dll back', undo_fn(ctx, {'kind': 'set_dll', 'instance': name, 'path': str(cur),
                                                     'kept': str(kept), 'sha256': cur_sha})))
    out['dll_sha256'] = want_sha
    pack, why_no = make_pack(ctx, b)
    out['pack'] = str(pack) if pack else why_no
    r = ctx.run(tool(ctx, 'provision') + ['set-proxy', name, '--bin', pack or b['x64_bin'], '--proxy', b['proxy_dll']],
                name='set-proxy', heavy=False)
    if r.rc != 0:
        return StageResult.failed(f'provision.py set-proxy {name}: exit {r.rc}; {r.log}')
    undo.append(('set-proxy --original', undo_fn(ctx, {
        'kind': 'provision', 'name': 'set-proxy-original',
        'args': ['set-proxy', name, '--original', '--runs-to', str(new_dir(ctx, 'vp64-runs'))]})))
    cp = user / 'MODS' / '(1) Community Patch'
    ini = cp / 'vpproxy.ini'
    keep(ctx, ini, 'vpproxy.ini', undo)          # back by hash before set-proxy --original compares it
    if pack:
        for f in ('manifest.json', 'purity.tsv'):
            if (pack / f).is_file():
                shutil.copy2(pack / f, cp / 'VP64' / f)
                undo.append((f'remove VP64/{f}', undo_fn(ctx, {'kind': 'rm', 'path': str(cp / 'VP64' / f)})))
        r = ctx.run(tool(ctx, 'set_ini') + [ini, 'pack=1'], name='pack-on', heavy=False)
        if r.rc != 0:
            return StageResult.failed(f'set_ini pack=1: exit {r.rc}; {r.log}')
    return user, ini


def new_dir(ctx, name) -> Path:
    base = ctx.dir('hi_journey')
    p, n = base / name, 1
    while p.exists():
        n += 1
        p = base / f'{name}-{n}'
    return p


def make_pack(ctx, b):
    """A test pack (pack.py) with the instance-aware DLL as a test-only entry, as the instance stage makes it, in this
    stage's own folder; (None, why) without a pack stage or the tag's DLL."""
    if ctx.outputs_if_ok('pack') is None:
        return None, 'no pack: the pack stage is not ok'
    player = ctx.work / 'inputs' / 'player-x86' / DLL
    if player.is_file():
        x86 = ['--x86-dll', player]
    elif ctx.config.get('fork'):
        x86 = ['--fork', ctx.config['fork'], '--tag', getattr(ctx, 'ref', None) or ctx.tag]   # 3509: --base-commit
    else:
        return None, f'no pack: {player} missing and no --fork'
    pu = ctx.outputs_if_ok('purity')
    purity = ['--purity', pu['purity_tsv']] if pu and pu.get('purity_tsv') else []
    major = take_major(ctx)          # 0580: a 43-civ take's pack names its count (and takes the tag's (3b) DLL)
    majors = ['--major-civs', str(major)] if major != MAJOR_CIVS_DEFAULT else []
    o = _common.fresh_dir(ctx.work / 'hi_journey' / 'pack')
    r = ctx.run(tool(ctx, 'pack') + ['--layout', b['x64_layout']] + purity + majors + x86 +
                ['--test-x86-dll', b['i0ia_dll'], '--test-x86-origin',
                 f'take_release {ctx.tag} instance-aware build, test only', '--release', ctx.tag, '--out', o],
                name='pack', heavy=False)
    pk = o / 'vp64pack'
    if r.rc != 0 or not (pk / 'manifest.json').is_file():
        return None, f'pack.py exit {r.rc}; {r.log}'
    return pk, None


def settings_files(ctx, user, js, out, undo):
    """The journeys' client size, SinglePlayerAutoEndTurnEnabled = 0 and AutoUnitCycle = 1, backed up by hash first.
    AutoUnitCycle (ticket 0530): the journeys were recorded with the game's default (on: after a unit's last move the
    game selects the next unit and centres the camera on it); an instance left with it off by an earlier session plays
    a different scene, and release-large stopped at its step 12 on c5h-b and c5h-f
    (research/06-tooling-and-workflows/release-large-instance-difference.md)."""
    w = {tuple(j.get('window') or (1920, 1080)) for j in js}
    if len(w) != 1:
        raise RuntimeError(f'the chosen journeys want different window sizes {sorted(w)}')
    (ww, hh), = w
    g = user / 'GraphicsSettingsDX11.ini'
    u = user / 'UserSettings.ini'
    out['settings_before'] = {'graphics': keep(ctx, g, 'GraphicsSettingsDX11.ini', undo),
                              'user': keep(ctx, u, 'UserSettings.ini', undo)}
    ok = set_key(g, 'WindowResX', str(ww)) and set_key(g, 'WindowResY', str(hh))
    ok = set_key(u, 'SinglePlayerAutoEndTurnEnabled', '0') and ok
    ok = set_key(u, 'AutoUnitCycle', '1') and ok
    if not ok:
        raise RuntimeError(f'{g.name} or {u.name} lacks WindowResX/Y, SinglePlayerAutoEndTurnEnabled or AutoUnitCycle')
    out['window'] = [ww, hh]
    opt = user / 'VP64Options.ini'
    out['vp64options_sha256'] = sha256_file(opt) if opt.is_file() else None


def _lay_installed(ctx, b, name, inst, out, undo):
    """2604: the installer-installed layout checked, nothing laid (no set-dll, no set-proxy, no test pack). Returns
    (user folder, vpproxy.ini) or a StageResult (failed: not the build's layout; skipped: the modpack form)."""
    m = I.manifest(inst)
    user = inst / (m.get('user_dir') or USER_DEFAULT).replace('\\', '/')
    lay = installed_layout(b, user)
    out['layout'] = dict(lay, instance=name, user=str(user))
    if not lay['ok']:
        why = (f'instance {name}: the {INSTALLED} layout is refused, nothing played: {"; ".join(lay["why"])}')
        modpack_only = all('modpack' in w or 'mods form' in w for w in lay['why'])
        return StageResult.skipped(why) if modpack_only else StageResult.failed(why)
    if (m.get('proxy') or {}).get('state') == 'installed':
        return StageResult.failed(f'instance {name} carries a provision.py set-proxy layout as well as an installer '
                                  'record; refused (one layout at a time)')
    missing = I.mods_check(ctx, user)
    if missing:
        return StageResult.skipped(f'instance {name} lacks the release\'s mods {missing}')
    undo.append(('frontend restore', _frontend_restore(ctx, name)))
    I.fix_modinfo(ctx, user, out, undo)
    cp = Path(lay['cpdir'])
    ini = cp / 'vpproxy.ini'
    keep(ctx, ini, 'vpproxy.ini', undo)          # the routes flip mode=; back by hash at the end
    out['dll_sha256'] = lay['fallback_sha256']
    out['pack'] = f'the installed pack ({cp / "VP64"})'
    ctx.say(f'hi_journey: playing on the {INSTALLED} layout of {name} (record {lay["record"]}, release '
            f'{lay.get("release")}, variant {lay.get("variant")}, config {lay.get("config")})')
    return user, ini


def _lay_and_play(ctx, st, b, js, routes, name, inst, saves_dir, out, undo):
    laid = (_lay_installed(ctx, b, name, inst, out, undo) if st.get('installed')
            else _lay(ctx, b, name, inst, out, undo))
    if isinstance(laid, StageResult):
        out['verdict'] = 'incomplete'
        out['not_played'] = laid.what
        return laid
    user, ini = laid
    if isinstance(undo, Undo) and st.get('installed'):
        undo.note(layout=out.get('layout'))      # 0650: finish re-checks the installed layout by these hashes
    settings_files(ctx, user, js, out, undo)
    for j in js:
        target = save_dir(user) / f'{j["start_save"]["name"]}.Civ5Save'
        if target.exists():
            keep(ctx, target, target.name, undo, sub='kept-saves')
        else:
            undo.append((f'remove {target.name}', undo_fn(ctx, {'kind': 'rm', 'path': str(target)})))
    undo.append(('made saves removed', undo_fn(ctx, {'kind': 'clear_made', 'dir': str(save_dir(user)),
                                                     'prefixes': [p for j in js for p in j.get('made_saves') or []]})))
    out_root = ctx.dir('hi_journey', 'runs')
    journeys = {}
    out['journeys'] = journeys
    for j in js:
        rows = {}
        journeys[j['name']] = {'trace': j['trace_path'], 'routes': rows, 'verdict': 'incomplete'}
        for route in routes:
            rows[route] = play_with_rerun(ctx, st, j, route, name, user, saves_dir, ini, out_root)
            ctx.partial(journeys=journeys)
        journeys[j['name']]['compare'] = compare(ctx, j, rows, out_root)
        journeys[j['name']]['verdict'] = journey_verdict(rows, routes)
        ctx.partial(journeys=journeys)
    out['verdict'] = overall(journeys)
    crashes = [dict(r['crash'], journey=jn, route=rt, when=r.get('crashed_when'))
               for jn, jj in journeys.items() for rt, r in jj['routes'].items() if r.get('crash')]
    if crashes:                  # 0533: every crash without a box of the stage, for the summary and the bug report
        out['crashes'] = crashes
    return StageResult.ok(**out)


# ------------------------------------------------------------------------------------------------- finish (0650)

EXIT_FINISHED, EXIT_FINISH_FAILED, EXIT_FINISH_ARGS, EXIT_FINISH_REFUSED = 0, 1, 2, 4
FINISH_LEASE_H = '1'


class FinishRefused(Exception):
    """A game, a box, the patch's popup, a process or another party's claim is on the instance: nothing is changed."""


def record_path(where) -> Path:
    """The undo record named by a take's work folder, its hi_journey folder or the record itself."""
    p = Path(where)
    for c in (p, p / UNDO_NAME, p / 'hi_journey' / UNDO_NAME):
        if c.is_file():
            return c
    raise FileNotFoundError(f'no undo record at {p} (nor {p / UNDO_NAME}, {p / "hi_journey" / UNDO_NAME})')


def on_instance(ctx, name) -> list:
    """What is still on the instance before a finish (0650): a game, a box (the watcher's marker; a crash without a box
    and with no game is not one, as crash_end reads it), a box or the patch's popup (dialogwatch once: also after the
    game ended, D118), processes left. [] when nothing."""
    if running(ctx, name):
        return [f'a game runs on {name}']
    why = []
    r = ctx.run(tool(ctx, 'dialogwatch') + ['marker', '--instance', name], name='finish-marker', heavy=False)
    if r.rc == 8:
        d = json_of(r)
        if d.get('kind') != 'crash' or any((x or {}).get('box_kind') not in (None, 'crash')
                                           for x in d.get('dialogs') or []):
            why.append(f'a box is open on {name} ({body(r).strip()[-300:]})')
    o = ctx.run(tool(ctx, 'dialogwatch') + ['once', '--instance', name], name='finish-popup', heavy=False)
    if o.rc == 8:
        why.append(f'a box or the patch\'s popup is open on {name} ({body(o).strip()[-300:]})')
    lo = ctx.run(tool(ctx, 'cli') + ['-i', name, 'leftovers', '--json'], name='finish-leftovers', heavy=False)
    lft = json_of(lo).get('leftovers')
    if lft is None:
        why.append(f'the leftovers of {name} cannot be read ({lo.log})')
    elif lft:
        why.append(f'processes left under {name}: {lft}')
    return why


def take_back(ctx, name, head) -> str:
    """The claim for the restore: renewed when the recorded owner still holds it, else claimed for that owner when the
    instance is free (a stale claim of it included); another party's claim is FinishRefused."""
    o = head['owner']
    r = ctx.run(tool(ctx, 'pool') + ['renew', name, '--owner', o, '--hours', FINISH_LEASE_H], name='finish-renew',
                heavy=False)
    if r.rc == 0:
        return f'renewed for {o}'
    args = ['claim', '--owner', o, '--name', name, '--hours', FINISH_LEASE_H, '--length', 'short', '--note',
            f'hi_journey finish of {head.get("tag")} (ticket 0650)']
    if head.get('ticket'):
        args += ['--ticket', head['ticket']]
        if head.get('address'):
            args += ['--address', head['address']]
    c = ctx.run(tool(ctx, 'pool') + args, name='finish-claim', heavy=False)
    if c.rc == 0 and 'claimed by' in body(c):
        return f'claimed for {o}'
    raise FinishRefused(f'{name} is not claimed by {o} and cannot be claimed for it (renew exit {r.rc}: '
                        f'{body(r).strip()[-200:]}; claim exit {c.rc}: {body(c).strip()[-200:]}): another party may '
                        'be using it; nothing changed')


def finish(ctx, where) -> tuple:
    """Ticket 0650: finish the restore of a run left on a box, from its undo record. Returns (exit code, result):
    0 restored and released (or the record was already restored), 1 the restore or the release failed (claim and
    marker kept where the restore failed), 4 refused, nothing changed (a game, a box, the patch's popup, processes or
    another party's claim on the instance)."""
    p = record_path(where)
    u = Undo.load(ctx, p)
    h = u.head
    name = h['instance']
    res = {'record': str(p), 'instance': name, 'owner': h.get('owner'), 'state_before': h.get('state')}
    if h.get('state') == 'restored':
        res.update(ok=True, why='the record says restored; nothing to do')
        return EXIT_FINISHED, res
    had_stop = 'check_stop' in vars(ctx)
    old_stop = vars(ctx).get('check_stop')
    ctx.check_stop = lambda: None          # the restore runs to its end, as in the stage
    before_owner = os.environ.get('C5_POOL_OWNER')
    os.environ['C5_POOL_OWNER'] = h['owner']
    try:
        why = on_instance(ctx, name)
        if why:
            raise FinishRefused('; '.join(why))
        res['claim'] = take_back(ctx, name, h)
        rs = _restore(ctx, name, u, {'layout': h.get('layout')} if h.get('installed') else None)
        res['restore'] = rs
        res['restore_record'] = str(write_restore_record(p.parent, rs, by='finish (ticket 0650)', instance=name,
                                                         record=str(p)))
        if not rs['ok']:
            left(u, f'finish: restore failed: {rs["errors"]}')
            res.update(ok=False, claim_released=False)
            _finish_report(p.parent, res)
            return EXIT_FINISH_FAILED, res
        restored(u)
        u.note(finished_by='finish (ticket 0650)')
        rel = ctx.run(tool(ctx, 'pool') + ['release', name, '--owner', h['owner']], name='finish-release', heavy=False)
        res.update(ok=rel.rc == 0, claim_released=rel.rc == 0)
        _finish_report(p.parent, res)
        return (EXIT_FINISHED if rel.rc == 0 else EXIT_FINISH_FAILED), res
    except FinishRefused as e:
        res.update(ok=False, refused=f'REFUSED, nothing changed: {e}')
        return EXIT_FINISH_REFUSED, res
    finally:
        if before_owner is None:
            os.environ.pop('C5_POOL_OWNER', None)
        else:
            os.environ['C5_POOL_OWNER'] = before_owner
        if had_stop:
            ctx.check_stop = old_stop
        else:
            del ctx.check_stop


def _finish_report(hj_dir: Path, res: dict):
    """A section of the run's report.md naming the finish (when the report is there)."""
    mp = Path(hj_dir) / 'report.md'
    if not mp.is_file():
        return
    rs = res.get('restore') or {}
    L = ['', f'## Restore finished after the run (ticket 0650), {time.strftime("%Y-%m-%d %H:%M:%S")}', '',
         f'ok: {res.get("ok")}; claim {res.get("claim")}, released: {res.get("claim_released")}; verify rc '
         f'{rs.get("verify_rc")}; record `{res.get("restore_record")}`', '']
    L += [f'- done: {x}' for x in rs.get('done') or []] + [f'- FAILED: {x}' for x in rs.get('errors') or []]
    with open(mp, 'a', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')


def main(argv=None, out=print) -> int:
    """`hi_journey.py finish <work folder | hi_journey folder | undo.json> [--tools FILE]` (ticket 0650)."""
    import argparse
    import take_release as tr
    ap = argparse.ArgumentParser(prog='hi_journey.py', description='The hi_journey release stage\'s own commands '
                                 '(ticket 0650).')
    sub = ap.add_subparsers(dest='cmd', required=True)
    f = sub.add_parser('finish', help='finish the restore of a run that ended on a box, from its undo record')
    f.add_argument('where', help='the take\'s work folder, its hi_journey folder or the undo.json')
    f.add_argument('--tools', type=Path, help='a JSON file of tools as take_release.py --tools takes (a test\'s fakes)')
    try:
        a = ap.parse_args(argv)
    except SystemExit as e:
        return EXIT_FINISH_ARGS if e.code not in (0, None) else 0
    try:
        p = record_path(a.where)
        head = json.loads(p.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        out(f'hi_journey finish: error: {e}')
        return EXIT_FINISH_ARGS
    work = Path(head.get('work') or p.parent.parent)
    sp = work / tr.STATE_NAME
    state = json.loads(sp.read_text(encoding='utf-8')) if sp.is_file() else {}
    tag = head.get('tag') or state.get('tag')
    tools = {**tr.default_tools(), **json.loads(a.tools.read_text(encoding='utf-8'))} if a.tools else None
    try:
        d = tr.Driver(tag, work, None, dict(state.get('config') or {}, gate_wait=0), [], tools=tools, out=out)
        lock = d._take_driver_lock()
    except tr.ArgError as e:
        out(f'hi_journey finish: error: {e}')
        return EXIT_FINISH_ARGS
    try:
        code, res = finish(tr.Ctx(d, 'hi_journey'), p)
    finally:
        d._release_driver_lock(lock)
    out(json.dumps(res, indent=1, default=str))
    if code == EXIT_FINISHED:
        out(f'hi_journey finish: {res["instance"]} restored by hash and released '
            f'({res.get("why") or res.get("restore_record")})')
    elif code == EXIT_FINISH_REFUSED:
        out(f'hi_journey finish: LOUD: {res["instance"]}: {res["refused"]}')
    else:
        rs = res.get('restore') or {}
        out(f'hi_journey finish: LOUD: {res["instance"]}: restore FAILED ({rs.get("errors")}); left changed: '
            f'{rs.get("left_changed")}; claim released: {res.get("claim_released")}')
    return code


if __name__ == '__main__':
    sys.exit(main())
