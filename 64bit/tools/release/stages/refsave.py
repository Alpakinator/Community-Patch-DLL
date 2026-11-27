"""refsave (CL step 2, the reference saves; validated tier, issue 3506): the huge reference save of the release.

1. P10 (research/06-tooling-and-workflows/release-driver.md section 1): a copy of the descriptor's huge reference save
   (<work>/refsave/inputs/, checked by SHA-256) is loaded by the release's x64 server and plays two turns
   (server/run_server.py --load, a gated launch point). Loads and plays = the old save stays the reference.
2. Otherwise (no save in the descriptor, the file missing, the load or the turns failing), or with
   C5_RELEASE_REFSAVE=new: a new fixed-seed huge game (tools/i0/specs/continents-huge-fixed-seed.json: Continents
   128x80, seeds 1001/2002, standard speed, Emperor) with every victory type off (the Lua probe
   tools/release/specs/refsave-victories-off.lua, before the first turn) played headless from turn 0 to the reference
   turn on the x64 server, AI logging off (D49, as run_d10.py runs the server), saved at the end; then the same
   two-turn load check on the new save, which must still hold every victory type off
   (tools/release/specs/refsave-victories-report.lua). Victories are off (ticket 0175) because with every victory on
   the game ends in a Maya space-race victory at turn 350 on the D96 pins, so its turn-350 save cannot play on
   (release-driver.md W5.2). Hours of play: C5_RELEASE_REFSAVE_RUN=<run folder> adopts a finished run of the same
   command instead (its server-report.txt must say "passed" at the turn, its save must exist and its Lua.log must show
   the victories turned off). A new save is reported for the descriptor (issue 3501 owns it): this stage never edits
   the descriptor.

A save hash mismatch is a stop; a server that does not reach its turn on the new game, or a new game or save whose
victories are not all off, is a failure.
"""
from __future__ import annotations

import re
import shutil
import struct
import uuid
from pathlib import Path

from take_release import REPO, StageResult, sha256_file
from stages import _common, report
from stages import validated as V

DEPENDS = ['build']
SPEC = REPO / 'tools' / 'i0' / 'specs' / 'continents-huge-fixed-seed.json'
# Ticket 0175: every victory type off, so the game cannot end before (or two turns after) the reference turn. A probe,
# not the spec's "victories" list: on the D96 pins' server that list only sets types valid (release-driver.md W5.11).
VICTORIES_OFF = REPO / 'tools' / 'release' / 'specs' / 'refsave-victories-off.lua'
VICTORIES_REPORT = REPO / 'tools' / 'release' / 'specs' / 'refsave-victories-report.lua'
OFF_RE = re.compile(r'refsave victories off: (\d+) of (\d+)')
VALID_RE = re.compile(r'refsave victories valid: (\d+) of (\d+)')
# Ticket 0175: the DLC a server-written new save must not list. run_server.py's default leaves out only DLC_SP_Maps
# (Vox Populi blocks it); the descriptor's game folder (the headless toolkit's) also carries the "VP Modpack" package
# (Assets/DLC/VP_MODPACK_*/, with <SteamApp>), which a player's game and the test instances do not have: a save that
# lists it is refused by the front end's load (CanLoadSavedGame = 2, "needs DLC that is not installed"; c5h-a,
# 2026-11-14 18:0x, release-driver.md W5.11).
VP_BLOCKED_DLC = '{3F49DF54-68B6-44d1-A930-A168628FAA59}'
MODPACK_GLOB = 'VP_MODPACK*/*.Civ5Pkg'
GUID_RE = re.compile(r'<GUID>\s*(\{[0-9A-Fa-f-]{36}\})\s*</GUID>')
HEADER_BYTES = 8192
DEFAULT_TURN = 350
CHECK_TURNS = 2
GAME_TIMEOUT_S = 6 * 3600
RESULT_RE = re.compile(r'^result: (\w+); turns (\d+) -> (\d+)', re.M)


def inputs(ctx):
    s = (getattr(ctx.desc, 'reference_saves', None) or {}).get('huge')
    env = V.env_settings()
    return {'huge': {'path': str(s.path), 'sha256': s.sha256, 'turn': s.turn} if s else None,
            'mode': env['refsave'], 'adopt': env['refsave_run'],
            'spec': _common.file_sig(SPEC), 'victories_off': _common.file_sig(VICTORIES_OFF),
            'victories_report': _common.file_sig(VICTORIES_REPORT), 'dlc_exclude': [VP_BLOCKED_DLC] + modpack_dlc(ctx)}


def modpack_dlc(ctx) -> list:
    """The GUIDs of the modpack packages in the descriptor's game folder (left out of a new save's DLC list)."""
    root = Path(getattr(ctx.desc, 'game_dir', '') or '.') / 'Assets' / 'DLC'
    out = []
    for pkg in sorted(root.glob(MODPACK_GLOB)) if root.is_dir() else []:
        m = GUID_RE.search(pkg.read_text(encoding='utf-8', errors='replace'))
        if m:
            out.append(m.group(1))
    return out


def save_lists_dlc(save: Path, guids) -> list:
    """The GUIDs of `guids` whose 16 bytes (as the server writes them, Data1-3 little-endian) are in the save's header."""
    head = Path(save).read_bytes()[:HEADER_BYTES]
    return [g for g in guids if uuid.UUID(g).bytes_le in head]


def _u32(b: bytes, p: int) -> tuple:
    if p + 4 > len(b):
        raise ValueError(f'header ends at {len(b)}, want 4 bytes at {p}')
    return struct.unpack_from('<I', b, p)[0], p + 4


def _str(b: bytes, p: int) -> tuple:
    n, p = _u32(b, p)
    if n > 1 << 16 or p + n > len(b):
        raise ValueError(f'string length {n} at {p - 4}')
    return b[p:p + n].decode('utf-8', 'replace'), p + n


def header_info(b: bytes) -> dict:
    """The .Civ5Save header's mod list and the byte span of its mod block (the layout of server/tools/civ5save.py
    parse_header: magic, u32, 2 strings, u32 turn, u8, 7 type strings, DLC list, mod list, ...)."""
    if b[:4] != b'CIV5':
        raise ValueError('not a .Civ5Save (no CIV5 magic)')
    p = 8
    _, p = _str(b, p)
    _, p = _str(b, p)
    p += 5
    for _ in range(7):
        _, p = _str(b, p)
    n, p = _u32(b, p)
    for _ in range(n):
        p += 16
        _, p = _u32(b, p)
        _, p = _str(b, p)
    start = p
    n, p = _u32(b, p)
    mods = []
    for _ in range(n):
        mid, p = _str(b, p)
        ver, p = _u32(b, p)
        nm, p = _str(b, p)
        mods.append([mid, ver, nm])
    return {'mods': mods, 'mod_span': (start, p)}


def mod_block(mods) -> bytes:
    def s(x):
        e = str(x).encode('utf-8')
        return struct.pack('<I', len(e)) + e
    return struct.pack('<I', len(mods)) + b''.join(s(m[0]) + struct.pack('<I', int(m[1])) + s(m[2]) for m in mods)


def release_mods(ctx) -> list:
    """The descriptor's mods as a save header lists them: [guid, version, folder] (the real game's names, as in the
    descriptor's own huge save: '(1) Community Patch' v151, '(2) Vox Populi' v17 for 5.4.8)."""
    return [[m.guid.lower(), int(m.version), m.folder] for m in (getattr(ctx.desc, 'mods', None) or [])]


def with_release_mods(ctx, save: Path, dst: Path, bin_dir):
    """Copy `save` to `dst` with the header's mod list set to the descriptor's mods. Ticket 0175: the server writes a
    new game's header with an empty mod list, and the EXE, loading such a save, turns the mods off, unloads the mod's
    DLL and aborts in the C++ runtime (c5h-b, 2026-11-14 20:13; release-driver.md W5.11). The pregame and body are
    unchanged: civ5save.exe (the build's, the server's own zlib 1.2.3) unpacks and packs them, byte-exact. Returns a
    StageResult failure or None."""
    want = release_mods(ctx)
    hb = Path(save).read_bytes()[:HEADER_BYTES]
    try:
        info = header_info(hb)
    except (ValueError, struct.error) as e:
        return StageResult.failed(f'the new save {save}: its header cannot be read ({e})')
    if info['mods'] == want:
        if Path(save).resolve() != dst.resolve():
            shutil.copy2(save, dst)
        return None
    tool = ctx.tool('civ5save') if 'civ5save' in (getattr(ctx._d, 'tools', {}) or {}) else \
        [str(Path(bin_dir) / 'civ5save.exe')]
    work = _common.fresh_dir(ctx.work / 'refsave' / 'header')
    work.mkdir()
    base = work / 'save'
    r = ctx.run(tool + ['unpack', save, base], name='civ5save-unpack', heavy=False)
    parts = [Path(f'{base}.{x}.bin') for x in ('header', 'pregame', 'body')]
    if r.rc != 0 or not all(p.is_file() for p in parts):
        return StageResult.failed(f'civ5save unpack of {save}: exit {r.rc}; {r.tail(4)}')
    h = parts[0].read_bytes()
    a, b = header_info(h)['mod_span']
    parts[0].write_bytes(h[:a] + mod_block(want) + h[b:])
    tmp = work / dst.name
    r = ctx.run(tool + ['pack'] + parts + [tmp], name='civ5save-pack', heavy=False)
    if r.rc != 0 or not tmp.is_file():
        return StageResult.failed(f'civ5save pack of {tmp}: exit {r.rc}; {r.tail(4)}')
    got = header_info(tmp.read_bytes()[:HEADER_BYTES])['mods']
    if got != want:
        return StageResult.failed(f'the repacked save {tmp} lists the mods {got}, want {want}')
    shutil.move(str(tmp), str(dst))
    return None


def lua_count(run_dir: Path, rx) -> tuple:
    """(a, n) of the last line of a run folder's Logs/Lua.log that matches rx, or None."""
    lp = Path(run_dir) / 'Logs' / 'Lua.log'
    txt = lp.read_text(encoding='utf-8', errors='replace') if lp.is_file() else ''
    m = rx.findall(txt)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def server_result(run_dir: Path) -> dict:
    """result, start and end turn and Lua errors from a run_server.py run folder's server-report.txt."""
    rp = Path(run_dir) / 'server-report.txt'
    txt = rp.read_text(encoding='utf-8', errors='replace') if rp.is_file() else ''
    m = RESULT_RE.search(txt)
    lua = re.search(r'Lua errors (\d+)', txt)
    return {'report': str(rp), 'result': m.group(1) if m else None, 'start': int(m.group(2)) if m else None,
            'end': int(m.group(3)) if m else None, 'lua_errors': int(lua.group(1)) if lua else None}


def load_check(ctx, save: Path, turn: int, name: str, bin_dir, victories_off=False) -> dict:
    """Load `save` in the release's server and play CHECK_TURNS turns. {'ok', 'rc', 'result', 'end', 'why', ...}
    victories_off: also run the report probe, and the save must hold every victory type off (a new reference save)."""
    run_dir = _common.fresh_dir(ctx.work / 'refsave' / name)
    probe = ['--lua-probe', VICTORIES_REPORT] if victories_off else []
    r = ctx.run(V.tool(ctx, 'run_server') + ['--load', save, '--turns', turn + CHECK_TURNS, '--run', run_dir,
                                             '--bin', bin_dir, '--dll-log-flags', '0'] + probe, name=name, gated=True)
    res = server_result(run_dir)
    res.update(rc=r.rc, run=str(run_dir), log=str(r.log), seconds=round(r.seconds))
    bad = []
    if r.rc != 0:
        bad.append(f'exit {r.rc}')
    if res['result'] != 'passed':
        bad.append(f"result {res['result']}")
    if res['end'] != turn + CHECK_TURNS:
        bad.append(f"ended at turn {res['end']}, target {turn + CHECK_TURNS}")
    if victories_off:
        vc = lua_count(run_dir, VALID_RE)
        res['victories_valid'] = f'{vc[0]} of {vc[1]}' if vc else None
        if not vc or vc[0] != 0 or vc[1] == 0:
            bad.append(f"victories valid in the loaded save: {res['victories_valid'] or 'no report line in Lua.log'}, "
                       'want 0 of N')
    res['ok'] = not bad
    res['why'] = '; '.join(bad) if bad else f'loaded at {turn} and played to {turn + CHECK_TURNS}'
    return res


def old_save(ctx):
    """(copy, turn, None) of the descriptor's huge save, or (None, turn, why it cannot be used), or a StageResult stop
    for a hash mismatch."""
    s = (getattr(ctx.desc, 'reference_saves', None) or {}).get('huge')
    if not s:
        return None, DEFAULT_TURN, 'the descriptor names no huge reference save'
    dst = ctx.dir('refsave', 'inputs') / f'{ctx.tag}-HUGE_{s.turn:04d}.Civ5Save'
    if not (dst.is_file() and sha256_file(dst) == s.sha256):
        if not Path(s.path).is_file():
            return None, s.turn, f'the huge reference save {s.path} is missing'
        shutil.copy2(s.path, dst)
    got = sha256_file(dst)
    if got != s.sha256:
        return StageResult.stop(f'the huge reference save {s.path} is {got[:16]}..., the descriptor says '
                                f'{s.sha256[:16]}...', 'fix the descriptor or the save copy (issue 3501)'), s.turn, None
    return dst, s.turn, None


def new_game(ctx, turn, bin_dir, name):
    """Play (or adopt) the fixed-seed huge game to `turn`. (run dir, save path, None) or (.., .., StageResult)."""
    adopt = V.env_settings()['refsave_run']
    prev = ctx.previous_outputs().get('game_run')
    if adopt:
        run_dir = Path(adopt)
    elif prev and server_result(prev)['result'] == 'passed':
        run_dir = Path(prev)
        ctx.say(f'new game: the earlier attempt {run_dir} finished; reused')
    else:
        run_dir = _common.fresh_dir(ctx.work / 'refsave' / 'game')
        r = ctx.run(V.tool(ctx, 'run_server') + [SPEC, '--turns', turn, '--run', run_dir, '--bin', bin_dir,
                                                 '--dll-log-flags', '0', '--save-out', name,
                                                 '--lua-probe', VICTORIES_OFF,
                                                 '--dlc-exclude', ','.join([VP_BLOCKED_DLC] + modpack_dlc(ctx)),
                                                 '--timeout', GAME_TIMEOUT_S], name='game', gated=True)
        if r.rc != 0 and server_result(run_dir)['result'] is None:
            return run_dir, None, StageResult.failed(f'run_server.py exit {r.rc} on the new huge game; {r.log}\n'
                                                     f'{r.tail(8)}', game_run=str(run_dir))
    res = server_result(run_dir)
    saves = sorted(run_dir.glob('*.Civ5Save')) if run_dir.is_dir() else []
    want = run_dir / f'{name}.Civ5Save'
    save = want if want.is_file() else (saves[-1] if adopt and saves else None)
    bad = []
    if res['result'] != 'passed':
        bad.append(f"result {res['result']}")
    if res['end'] != turn:
        bad.append(f"ended at turn {res['end']}, target {turn}")
    if res['start'] not in (0, None) and not adopt:
        bad.append(f"started at turn {res['start']}")
    if save is None:
        bad.append(f'no {want.name}')
    off = lua_count(run_dir, OFF_RE)
    res['victories_off'] = f'{off[0]} of {off[1]}' if off else None
    if not off or off[0] != off[1] or off[1] == 0:
        bad.append(f"victories not all off: {res['victories_off'] or f'no line from {VICTORIES_OFF.name} in Lua.log'}")
    listed = save_lists_dlc(save, modpack_dlc(ctx)) if save is not None else []
    if listed:
        bad.append(f'the save lists the modpack DLC {listed} that a player\'s game does not have (the front end refuses '
                   'to load it)')
    if bad:
        return run_dir, None, StageResult.failed(f'the new huge game in {run_dir}: ' + '; '.join(bad),
                                                 game_run=str(run_dir), game=res)
    ctx.partial(game_run=str(run_dir))
    return run_dir, save, None


def run(ctx):
    return report.finish(ctx, _run(ctx))


def _run(ctx):
    b = ctx.outputs('build')
    bin_dir = b['x64_bin']
    mode = V.env_settings()['refsave']
    if mode not in ('auto', 'new'):
        return StageResult.failed(f'C5_RELEASE_REFSAVE={mode!r}: auto or new')
    old, turn, why_not = old_save(ctx)
    if isinstance(old, StageResult):
        return old
    out = {}
    if old is not None:
        chk = load_check(ctx, old, turn, 'p10-old', bin_dir)
        out['p10_old'] = chk
        if chk['ok'] and mode == 'auto':
            return StageResult.ok(origin='descriptor', save=str(old), sha256=sha256_file(old), turn=turn, **out,
                                  note=f'P10: the descriptor\'s huge save {chk["why"]} on the release\'s server')
        why_not = f'P10: the descriptor\'s huge save did not load and play ({chk["why"]})' if not chk['ok'] else \
            'C5_RELEASE_REFSAVE=new'
    elif mode == 'new':
        why_not = 'C5_RELEASE_REFSAVE=new'
    ctx.say(f'a new huge reference game: {why_not}')
    name = f'{ctx.tag}-REF-HUGE_{turn:04d}'
    run_dir, save, err = new_game(ctx, turn, bin_dir, name)
    if err:
        err.outputs.update(out, why_new=why_not)
        return err
    dst = ctx.dir('refsave') / f'{name}.Civ5Save'
    err = with_release_mods(ctx, Path(save), dst, bin_dir)
    if err:
        err.outputs.update(out, why_new=why_not, game_run=str(run_dir))
        return err
    out['header_mods'] = header_info(dst.read_bytes())['mods']
    chk = load_check(ctx, dst, turn, 'p10-new', bin_dir, victories_off=True)
    game = server_result(run_dir)
    off = lua_count(run_dir, OFF_RE)
    game['victories_off'] = f'{off[0]} of {off[1]}' if off else None
    out.update(origin='new', why_new=why_not, game_run=str(run_dir), game=game, save=str(dst),
               sha256=sha256_file(dst), turn=turn, p10_new=chk,
               descriptor_update=f'reference_saves.huge = {{"path": "{dst.as_posix()}", "sha256": '
                                 f'"{sha256_file(dst)}", "turn": {turn}}} (issue 3501 owns the descriptor)')
    if not chk['ok']:
        return StageResult.failed(f'the new huge save {dst} does not load and play: {chk["why"]}', **out)
    return StageResult.ok(**out)
