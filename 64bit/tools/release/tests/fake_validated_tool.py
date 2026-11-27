"""A stand-in for every tool the validated-tier stages call (tests of issue 3506). Usage: fake_validated_tool.py NAME ARGS

Each call appends "NAME ARGS" to $FAKE_CALLS. Behaviour switches are environment variables (FAKE_*), set by the tests.
Writes only below the folders its arguments or $FAKE_DIR name (inside the test's temporary folder). Nothing here
starts a game, a server, a testbed job or touches an instance.
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

name, args = sys.argv[1], sys.argv[2:]
calls = os.environ.get('FAKE_CALLS')
if calls:
    with open(calls, 'a', encoding='utf-8') as f:
        f.write(name + ' ' + ' '.join(args) + '\n')
FD = Path(os.environ.get('FAKE_DIR', '.'))


def opt(flag, default=None):
    return args[args.index(flag) + 1] if flag in args else default


def env(k):
    return os.environ.get(k, '')


def fake_save(mods=(), dlc=None, body=b'body'):
    """A .Civ5Save with a real-layout header up to its mod list (ticket 0175), then b'|HDR-END|' and the body."""
    import struct
    import uuid

    def s(x):
        e = str(x).encode('utf-8')
        return struct.pack('<I', len(e)) + e
    h = b'CIV5' + struct.pack('<I', 8) + s('1.0.3.279') + s('403694') + struct.pack('<I', 350) + b'\x00'
    h += b''.join(s('T%d' % i) for i in range(7))
    dl = [dlc] if dlc else []
    h += struct.pack('<I', len(dl)) + b''.join(uuid.UUID(g).bytes_le + struct.pack('<I', 1) + s('DLC') for g in dl)
    h += struct.pack('<I', len(mods)) + b''.join(s(m[0]) + struct.pack('<I', m[1]) + s(m[2]) for m in mods)
    return h + s('') + b'|HDR-END|' + body


if name == 'quietlock':
    if args[0] == 'run':
        sys.exit(subprocess.call(args[args.index('--') + 1:]))
    sys.exit(0)

if name == 'run_server':
    if env('FAKE_GATE_REFUSE'):
        print('launch gate: REFUSED: the quiet-machine lock is held by someone')
        sys.exit(3)
    run = Path(opt('--run'))
    run.mkdir(parents=True, exist_ok=True)
    turns = int(opt('--turns'))
    if '--load' in args:
        start = turns - 2
        if env('FAKE_RS_LOAD_FAIL') and env('FAKE_RS_LOAD_FAIL') in opt('--load'):
            (run / 'server-report.txt').write_text(f'result: failed; turns {start} -> {start} (0 played)\n')
            sys.exit(1)
    else:
        start = 0
        if env('FAKE_RS_GAME_FAIL'):
            print('server crashed')
            sys.exit(5)
    (run / 'server-report.txt').write_text(f'result: passed; turns {start} -> {turns} ({turns - start} played), '
                                           f'autoplay 1 s; Lua errors 0\n')
    if opt('--save-out'):
        # ticket 0175: the server's new-game header: an empty mod list, the DLC of FAKE_RS_SAVE_DLC if set
        (run / (opt('--save-out') + '.Civ5Save')).write_bytes(
            fake_save(dlc=env('FAKE_RS_SAVE_DLC') or None, body=f'save of {run} at {turns}'.encode()))
    probe = opt('--lua-probe') or ''
    if probe:   # ticket 0175: refsave's victory probes print one line each into Logs/Lua.log
        (run / 'Logs').mkdir(exist_ok=True)
        if 'victories-off' in probe and not env('FAKE_RS_PROBE_SILENT'):
            (run / 'Logs' / 'Lua.log').write_text('Main: refsave victories off: 5 of 5\n')
        if 'victories-report' in probe:
            (run / 'Logs' / 'Lua.log').write_text(f"Main: refsave victories valid: {env('FAKE_RS_VALID') or 0} of 5\n")
    sys.exit(0)

if name == 'civ5save':   # ticket 0175: unpack SAVE BASE / pack HEADER PREGAME BODY OUT, split at b'|HDR-END|'
    if env('FAKE_C5S_FAIL'):
        sys.exit(1)
    if args[0] == 'unpack':
        h, _, rest = Path(args[1]).read_bytes().partition(b'|HDR-END|')
        Path(args[2] + '.header.bin').write_bytes(h)
        Path(args[2] + '.pregame.bin').write_bytes(b'')
        Path(args[2] + '.body.bin').write_bytes(rest)
        sys.exit(0)
    if args[0] == 'pack':
        Path(args[4]).write_bytes(Path(args[1]).read_bytes() + b'|HDR-END|' + Path(args[2]).read_bytes()
                                  + Path(args[3]).read_bytes())
        sys.exit(0)
    sys.exit(2)


def majors_of(spec):
    """The majors a fake run seats (issue 0372): FAKE_TB_MAJORS 'NAME=N,...' overrides; a spec: game 30, else 8."""
    name, _kind, game = spec.split(',')[:3]
    over = dict(x.split('=') for x in env('FAKE_TB_MAJORS').split(',') if '=' in x)
    if name in over:
        return int(over[name])
    return 30 if game.startswith('spec:') else 8


def job_out(out, run_specs, host, job):
    """A longgames.py job's TESTBED_OUT: longgames.json and one cmp-*/compare.json per x86 run (tb fetch, local)."""
    out.mkdir(parents=True, exist_ok=True)
    runs = []
    for spec in run_specs:
        r = spec.split(',')[0]
        g = r.rsplit('-', 1)[0]
        fail = env('FAKE_TB_RUNFAIL') == r
        runs.append({'name': r, 'verdict': 'FAIL: stalled' if fail else 'pass: game over (Science, turn 300)',
                     'endTurn': 300, 'rc': 0, 'wallSeconds': 10.0, 'luaErrors': 0,
                     'gameResult': {'victory': [{'turn': 300, 'type': 'Science'}], 'majors': majors_of(spec)}})
        if r.endswith('-x86'):
            c = out / f'cmp-{g}-x64-{g}-x86'
            c.mkdir(parents=True, exist_ok=True)
            tables = [{'table': 'PlayerYields', 'rowsA': 100, 'rowsB': 100, 'columns': 5, 'identical': True}]
            identical = True
            if env('FAKE_TB_DIFF') == g:
                tables.append({'table': 'CityYields', 'rowsA': 50, 'rowsB': 50, 'columns': 4, 'identical': False,
                               'columnsDiffering': {'Food': {'rows': 3, 'firstIndex': 7, 'firstTurn': 120,
                                                             'a': 1, 'b': 2}}, 'firstTurnDiffering': 120})
                identical = False
            if env('FAKE_TB_KNOWN'):
                tables.append({'table': 'ReligionBeliefYields', 'rowsA': 20, 'rowsB': 20, 'columns': 6,
                               'identical': False, 'columnsDiffering': {'IsReligionOwner': {
                                   'rows': 20, 'firstIndex': 0, 'firstTurn': 32, 'a': 1, 'b': 0}}})
                identical = False
            cmp = {'identical': identical, 'tables': tables, 'onlyInA': [], 'onlyInB': [], 'plausibility': [],
                   'tablesCompared': len(tables), 'rowsCompared': sum(t['rowsA'] for t in tables)}
            if env('FAKE_TB_NOSTATS') == g:   # longgames.compare() when a run left no stats.db (0372 live run)
                cmp = {'error': "FileNotFoundError('no stats.db: runs\\\\%s-x86\\\\cache\\\\stats.db')" % g}
            (c / 'compare.json').write_text(json.dumps(cmp))
            (c / 'compare.txt').write_text('compare\n')
    meta = {'job': job, 'host': host, 'db_sha256': env('FAKE_TB_DB') or env('FAKE_DESC_DB'), 'ended': 'x'}
    if env('FAKE_TB_WRONG_DLL'):
        meta['x64_bin'] = {'CvGameCore_Expansion2.dll': 'f' * 64}
    (out / 'longgames.json').write_text(json.dumps({'meta': meta, 'runs': runs, 'compares': []}))


if name == 'longgames':   # a `local` wholegames job (issue 0372): longgames.py's arguments, the runner's environment
    keys = ('TESTBED_OUT', 'TESTBED_WORK', 'TESTBED_DB', 'TESTBED_LOC_DB', 'TESTBED_GAME_TOOLKIT', 'TESTBED_C5H',
            'TESTBED_C5H_EXE', 'TESTBED_HOST', 'C5H_PYTHON')
    with open(FD / 'longgames-env.jsonl', 'a', encoding='utf-8') as f:
        f.write(json.dumps({k: os.environ.get(k) for k in keys}) + '\n')
    if env('FAKE_LOCAL_CRASH'):
        sys.exit(3)
    runs = [args[i + 1] for i, a in enumerate(args) if a == '--run']
    job_out(Path(os.environ['TESTBED_OUT']), runs, os.environ.get('TESTBED_HOST'), 'local')
    sys.exit(0)

if name == 'tb':
    host = opt('--host')
    jobs = FD / 'tbjobs'
    jobs.mkdir(parents=True, exist_ok=True)
    if 'submit' in args:
        if env('FAKE_TB_SUBMIT_FAIL'):
            print('tb: cannot reach ' + host, file=sys.stderr)
            sys.exit(1)
        cmd = args[args.index('--') + 1:]
        runs = [cmd[i + 1] for i, a in enumerate(cmd) if a == '--run']   # 'NAME,KIND,GAME,TO' (0372: GAME kept)
        n = len(list(jobs.glob('*.json'))) + 1
        job = f'2026-{n:03d}-{host}'
        (jobs / f'{job}.json').write_text(json.dumps({'runs': runs, 'cmd': cmd, 'host': host}))
        print(f'submitted {job} to {host} (slots=1)')
        sys.exit(0)
    if 'status' in args:
        job = args[-1]
        print(json.dumps({'host': host, 'jobs': [{'id': job, 'state': env('FAKE_TB_STATE') or 'done'}]}, indent=1))
        print(f'host {host}: slots=2 runner=alive')
        sys.exit(0)
    if 'wait' in args:
        stop = env('FAKE_TB_STOP_ON_WAIT')
        if stop:
            Path(stop).write_text('stop')
        sys.exit(0)
    if 'fetch' in args:
        job = args[args.index('fetch') + 1]
        if env('FAKE_TB_FETCH_FAIL'):
            print('tb: fetch failed', file=sys.stderr)
            sys.exit(1)
        out = Path(opt('--out')) / 'out'
        rec = json.loads((jobs / f'{job}.json').read_text())
        job_out(out, rec['runs'], host, job)
        print(f'fetched 9 files of {job} (done) into {opt("--out")}')
        sys.exit(0)
    sys.exit(2)

if name == 'pool':
    if args[0] == 'claim':
        if env('FAKE_POOL_NONE') or (opt('--name') and opt('--name') in env('FAKE_POOL_BUSY').split(',')):
            print('refused: no free instance', file=sys.stderr)
            sys.exit(2)
        if env('FAKE_POOL_REFUSE'):      # ticket 0640: pool.py's resolve() refusing the ticket or the address
            print('pool.py: %s' % env('FAKE_POOL_REFUSE'), file=sys.stderr)
            sys.exit(2)
        print(opt('--name') or 'c5h-t')
        print(str(FD / 'instances'))
        print('claimed by %s until 2026-11-14T03:00:00-05:00' % opt('--owner'))
        sys.exit(0)
    print('{}')
    sys.exit(0)

if name == 'provision':
    inst = FD / 'instances' / args[1] if len(args) > 1 else None
    user = inst / "Documents/My Games/Sid Meier's Civilization 5" if inst else None
    cp = user / 'MODS' / '(1) Community Patch' if user else None
    if args[0] == 'verify':
        sys.exit(1 if env('FAKE_PROV_VERIFY_FAIL') else 0)
    if args[0] == 'set-dll':
        if env('FAKE_PROV_SETDLL_FAIL'):
            sys.exit(1)
        shutil.copy2(opt('--dll'), cp / 'CvGameCore_Expansion2.dll')
        sys.exit(0)
    if args[0] == 'set-proxy':
        m = json.loads((inst / 'instance.json').read_text())
        if '--original' in args:
            vp = cp / 'VP64'
            left = [p.name for p in vp.glob('*')] if vp.is_dir() else []
            if '--runs-to' in args and os.path.exists(opt('--runs-to')):
                print('error: --runs-to %s exists (it must be new)' % opt('--runs-to'))
                sys.exit(2)
            if (vp / 'run').is_dir() and '--runs-to' not in args:
                print('error: run outputs present under VP64\run: pass --runs-to DIR')
                sys.exit(2)
            if 'manifest.json' in left or 'purity.tsv' in left:
                print('refusing: VP64 holds files set-proxy did not write: %s' % left)
                sys.exit(1)
            shutil.rmtree(vp, ignore_errors=True)
            m.pop('proxy', None)
        else:
            if env('FAKE_PROV_SETPROXY_FAIL'):
                sys.exit(1)
            (cp / 'VP64').mkdir(parents=True, exist_ok=True)
            m['proxy'] = {'state': 'installed', 'bin': opt('--bin')}
        (inst / 'instance.json').write_text(json.dumps(m))
        sys.exit(0)
    sys.exit(2)

if name == 'arm':
    inst, arm = args[0], args[1]
    ev = Path(opt('--ev'))
    d = ev / arm
    d.mkdir(parents=True, exist_ok=True)
    if os.environ.get('C5_VISUAL_OFF') != '1':
        print('the harness would record an undescribed shot')
        sys.exit(7)
    key = arm.split('-', 1)[1]
    if env('FAKE_ARM_REFUSE'):
        (d / 'arm.log').write_text('launch.py: launch gate: REFUSED: the lock is held\n')
        (d / 'arm.json').write_text(json.dumps({'rc': 3, 'error': 'launch failed'}))
        sys.exit(3)
    rc = int(env('FAKE_ARM_RC') or 0) if env('FAKE_ARM_RC_ARM') in ('', key) else 0
    mism = 3 if env('FAKE_ARM_MISMATCH') == key else 0
    vp = FD / 'instances' / inst / "Documents/My Games/Sid Meier's Civilization 5/MODS/(1) Community Patch/VP64"
    packed = (vp / 'manifest.json').is_file()
    # issue 0310: what the game would read at start: the CP modinfo and whether the mods cache is there
    for info in vp.parent.glob('*.modinfo'):
        (d / 'modinfo-seen').write_bytes(info.read_bytes())
    (d / 'modsdb-seen').write_text(str((vp.parents[2] / 'cache' / 'Civ5ModsDatabase.db').exists()))
    lines = ['boot: mode on', f'boot: purity: Load(x) {"ok" if packed else "not called"}']
    if mism:
        lines.append('local: VERIFY MISMATCH ICvUnit1::GetX: mirror 1, server 2')
    lines.append('local: canonical handles fetched 10 (failed 0, invalidations 0, crossed while a generation change was '
                 'in flight 0), local wrappers made 1 (reused 0, released on another thread 0), lock syncs 2, LOCK '
                 f'ERRORS 0; mirror hits 5, misses 1, stores 1, not kept (the server called the engine) 0, entries 3; '
                 f'verified 1234, MISMATCHES {mism}')
    (d / 'vpproxy.log').write_text('\n'.join(lines) + '\n')
    (d / 'end.png').write_bytes(b'png')
    (vp / 'run' / arm).mkdir(parents=True, exist_ok=True)   # the proxy's run folder, as live
    (d / 'arm.json').write_text(json.dumps({'rc': rc, 'packed': packed, 'ini': opt('--ini')}))
    sys.exit(rc)

if name == 'visualcheck':
    sys.exit(0)

if name == 'frontend':
    sys.exit(0)

if name == 'pack':
    out = Path(opt('--out')) / 'vp64pack'
    out.mkdir(parents=True, exist_ok=True)
    for f in ('server.exe', 'CvGameCore_Expansion2.dll', 'purity.tsv'):
        (out / f).write_bytes(f.encode())
    (out / 'manifest.json').write_text(json.dumps({'format': 1, 'test': opt('--test-x86-dll')}))
    sys.exit(0)

print('fake_validated_tool: unknown tool ' + name)
sys.exit(2)
