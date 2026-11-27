"""Hermetic tests of the hi_journey stage of tools/release/take_release.py (issue 4506, epic 0392 W4): the real stage
module against a fake instance and fake tools (a stand-in script written into the test's temporary folder), with the
real tools/hi/stockctl.py for the comparison (offline). No pool, gate, lock, game, instance or queue is touched;
nothing is written outside a temporary folder. The driver's pins and clones folders are temporary settings
(C5_PINS_DIR, C5_CLONES_DIR, no settings file: setUpModule), so no deps/local-settings.ini is needed.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_hi_journey.py" -v
"""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import take_release as tr  # noqa: E402
import descriptor  # noqa: E402

PY = sys.executable
TAG = 'Release-9.9.9'
USER = "Documents/My Games/Sid Meier's Civilization 5"
TOOLS = ['quietlock', 'pool', 'provision', 'frontend', 'pack', 'launch', 'guard_check', 'set_ini', 'turns', 'cli',
         'dialogwatch', 'crash', 'hi']
GRAPHICS = b'[GraphicsDetailSettings]\r\nFullScreen = 0\r\nWindowResX = 2560\r\nWindowResY = 1440\r\n'
USERSET = (b'[GameSettings]\r\nSinglePlayerAutoEndTurnEnabled = 1\r\nMultiplayerAutoEndTurnEnabled = 1\r\n'
           b'[InterfaceSettings]\r\nAutoUnitCycle = 0\r\n')     # 0530: off, as c5h-b and c5h-f had it
FE_FILES = ('MainMenu.lua', 'FrontEnd.lua', 'LoadScreen.lua')

# The stand-in for every tool the stage calls: "NAME ARGS" appended to $FAKE_CALLS; behaviour from FAKE_* variables.
FAKE = r'''
import json, os, re, shutil, subprocess, sys
from pathlib import Path
name, args = sys.argv[1], sys.argv[2:]
with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as f:
    f.write(name + ' ' + ' '.join(args) + '  [owner=' + os.environ.get('C5_POOL_OWNER', '-') + ']\n')
FD = Path(os.environ['FAKE_DIR'])
RUN = FD / 'running'
USER = "Documents/My Games/Sid Meier's Civilization 5"
def env(k):
    return os.environ.get(k, '')
def opt(flag, default=None):
    return args[args.index(flag) + 1] if flag in args else default
def inst(n):
    return FD / 'instances' / n
REC_FIRST = ('exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at CivilizationV_DX11.exe+0x37C6DA (VA 0x0077C6DA), '
             'thread 13176 (the proxy\'s boot thread)')
def crash_now(n, pid=4242):
    """0533: the game ends with a crash and no box: the proxy's exe-crash.txt for pid (unless FAKE_NO_RECORD; with
    FAKE_STALE_RECORD it names another pid), the watcher's marker of kind crash (unless FAKE_NO_MARKER; with
    FAKE_WATCHER_COLLECT it records its crash.collect; with FAKE_CRASH_POPUP it carries the patch's popup)."""
    if RUN.exists():
        RUN.unlink()
    rp = None
    if not env('FAKE_NO_RECORD'):
        rd = inst(n) / USER / 'Logs' / 'VP64' / 'run' / ('20261118-011612-%d-1' % pid)
        rd.mkdir(parents=True, exist_ok=True)
        rp = rd / 'exe-crash.txt'
        rp.write_text('\r\n'.join([REC_FIRST, 'code: 0xC0000005 EXCEPTION_ACCESS_VIOLATION',
                                   'where: CivilizationV_DX11.exe+0x37C6DA',
                                   'process: %d' % (pid + 1 if env('FAKE_STALE_RECORD') else pid), '']))
    if env('FAKE_NO_MARKER'):
        return
    box = {'kind': 'crash', 'crash_kind': 'crash', 'title': '(no box)', 'exit_code_hex': '0xC0000005',
           'proxy_record': {'path': str(rp), 'first_line': REC_FIRST} if rp else None,
           'signature': 'exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at CivilizationV_DX11.exe+0x37C6DA'}
    m = {'kind': 'crash', 'game_pid': pid, 'signature': box['signature'], 'dialogs': [box],
         'exit_code_hex': '0xC0000005'}
    if env('FAKE_CRASH_POPUP'):
        m['dialogs'].append({'kind': 'patch', 'title': 'Vox Populi 64-bit', 'process': 'vp64helper.exe'})
    if env('FAKE_WATCHER_COLLECT'):
        m['crash'] = {'dest': str(FD / 'watcher-crash'), 'pid': pid, 'files': [], 'errors': []}
    mp = FD / 'dialog_open.json'
    mp.write_text(json.dumps(m))
    (FD / 'crashmarker').write_text(str(mp))
if name == 'quietlock':
    sys.exit(subprocess.call(args[args.index('--') + 1:]) if args[0] == 'run' else 0)
if name == 'pool':
    if env('FAKE_CLAIMED_BY') and args[0] in ('claim', 'renew', 'release'):   # 0650: another party holds the instance
        print('refused: c5h-t is claimed by ' + env('FAKE_CLAIMED_BY'), file=sys.stderr)
        sys.exit(2)
    if args[0] == 'claim':
        if env('FAKE_NO_INSTANCE'):
            print('refused: no free instance', file=sys.stderr)
            sys.exit(2)
        n = opt('--name') or 'c5h-t'
        print(n); print(FD / 'instances'); print('claimed by %s until later (short run)' % opt('--owner'))
        sys.exit(0)
    print(json.dumps({'released': args[1]}))
    sys.exit(0)
if name == 'provision':
    cmd, n = args[0], args[1]
    cp = inst(n) / USER / 'MODS' / '(1) Community Patch'
    if cmd in ('set-proxy', 'set-dll') and RUN.exists():    # 0634: as the real provision.py refuses while a game runs
        print('refused: CivilizationV_DX11.exe is running on ' + n); sys.exit(1)
    if cmd == 'set-proxy' and '--original' not in args:
        (cp / 'VP64').mkdir(parents=True, exist_ok=True)
        (cp / 'vpproxy.ini').write_bytes(b'mode=on\r\nfallback_dll=x86.dll\r\nx64_dir=VP64\r\n')
    elif cmd == 'set-proxy':
        if (cp / 'vpproxy.ini').read_bytes() != b'mode=on\r\nfallback_dll=x86.dll\r\nx64_dir=VP64\r\n':
            print('vpproxy.ini changed: refused'); sys.exit(1)
        if any((cp / 'VP64').glob('*.json')):
            print('VP64 not as installed: refused'); sys.exit(1)
        shutil.rmtree(cp / 'VP64'); (cp / 'vpproxy.ini').unlink()
    elif cmd == 'set-dll':
        shutil.copyfile(opt('--dll'), cp / 'CvGameCore_Expansion2.dll')
    elif cmd == 'verify':
        sys.exit(int(env('FAKE_VERIFY_RC') or 0))
    sys.exit(0)
if name == 'pack':
    pk = Path(opt('--out')) / 'vp64pack'
    pk.mkdir(parents=True)
    (pk / 'manifest.json').write_text('{}'); (pk / 'purity.tsv').write_text('# vp64-purity 1\n')
    sys.exit(0)
if name == 'set_ini':
    p = Path(args[0]); data = p.read_bytes()
    for kv in args[1:]:
        k, v = kv.split('=', 1)
        pat = re.compile(rb'(?m)^' + re.escape(k.encode()) + rb'=.*$')
        data = pat.sub((k + '=' + v).encode(), data) if pat.search(data) else data + (k + '=' + v + '\r\n').encode()
    p.write_bytes(data)
    sys.exit(0)
if name == 'frontend':
    n, cmd = args[1], args[2]
    st = FD / ('fe-' + n + '.json')
    if cmd == 'prepare':
        if RUN.exists():
            print('refused: the game runs'); sys.exit(2)
        st.write_text('patched')
    elif cmd == 'restore':
        if RUN.exists():                    # 0634: the real frontend.py restore refuses while the game runs
            print('refused: the game runs'); sys.exit(2)
        if st.exists(): st.unlink()
    elif cmd == 'status':
        bad = st.exists() or env('FAKE_FE_PATCHED')
        print(json.dumps({'files': {f: {'is': 'patched' if bad and f == 'MainMenu.lua' else 'original'}
                                    for f in ('MainMenu.lua', 'FrontEnd.lua', 'LoadScreen.lua')}}, indent=1))
    sys.exit(0)
if name == 'launch':
    if env('FAKE_LAUNCH_REFUSE'):
        print('launch gate: REFUSED: the quiet-machine lock is held by someone'); sys.exit(3)
    RUN.write_text('1')
    if (FD / 'crashmarker').exists():      # the real launch moves an earlier game's marker aside (archive_stale_marker)
        (FD / 'crashmarker').unlink()
    mode = (inst(args[0]) / USER / 'MODS' / '(1) Community Patch' / 'vpproxy.ini').read_text()
    (FD / 'launches.txt').open('a').write(re.search(r'mode=(\w+)', mode).group(1) + '\n')
    print(json.dumps({'instance': args[0], 'pid': 4242}))
    sys.exit(0)
if name == 'guard_check':
    sys.exit(0)
if name == 'turns':
    if env('FAKE_STOP_FILE') and RUN.exists():   # 0634: the take's STOP file written while the launched game runs
        Path(env('FAKE_STOP_FILE')).write_text('stop')
    if env('FAKE_ACT_CRASH') and not (FD / 'act-crashed').exists():    # 0533: the first game crashes in activation
        (FD / 'act-crashed').write_text('1')
        crash_now(args[args.index('--instance') + 1])
        print('frontend: LOADING')
        sys.exit(0)
    print('frontend: IDLE (activation done)')
    sys.exit(0)
if name == 'crash':                       # 0533: crash.py collect
    print(json.dumps({'dest': str(FD / 'stage-crash'), 'pid': int(opt('--pid')), 'files': [], 'errors': []}, indent=2))
    sys.exit(0)
if name == 'dialogwatch':
    if args[0] == 'once':                  # 0650: the boxes of a running game, else the patch's popup (D118)
        if env('FAKE_POPUP') or env('FAKE_BOX') == 'always' or (
                env('FAKE_BOX_FILE') and Path(env('FAKE_BOX_FILE')).exists()):
            print('[{"kind": "message", "class": "VP64HelperPopup", "title": "Vox Populi 64-bit"}]'); sys.exit(8)
        if not RUN.exists():
            print('instance is not running (and no popup of the patch\'s helper for its last game)'); sys.exit(1)
        sys.exit(0)
    if (FD / 'crashmarker').exists():      # 2604: a crash without a box after the game ended (the -ia exit crash)
        mp = (FD / 'crashmarker').read_text().strip()
        if mp and mp != '1':               # 0533: crash_now's marker, printed as marker_dialog() prints it
            m = json.loads(Path(mp).read_text())
            print(json.dumps({'state': 'dialog', 'kind': 'crash', 'signature': m['signature'], 'marker': mp,
                              'dialogs': [{'kind': 'crash', 'box_kind': x.get('kind')} for x in m['dialogs']]},
                             indent=1))
            sys.exit(8)
        print(json.dumps({'state': 'dialog', 'kind': 'crash', 'signature': 'exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION'
                          ' (no proxy crash record)', 'dialogs': [{'kind': 'crash', 'box_kind': 'crash'}]}, indent=1))
        sys.exit(8)
    if (env('FAKE_BOX') == 'always' or (env('FAKE_BOX') == 'after-replay' and (FD / 'replayed').exists())
            or (env('FAKE_BOX_FILE') and Path(env('FAKE_BOX_FILE')).exists())):
        print('BOX: Game Crash'); sys.exit(8)
    sys.exit(0)
if name == 'cli':
    n, cmd = args[1], args[2]
    if cmd == 'status':
        print(json.dumps({'instance': n, 'running': RUN.exists()}, indent=1))
    elif cmd == 'stop':
        if env('FAKE_STOP_REFUSE'):
            print('stop refused: needs a decision'); sys.exit(6)
        if env('FAKE_STOP_LINGER'):         # 0634/0700: the terminate is sent, the process does not end (exit 4)
            print('stop_lingering: LOUD: the process has not ended'); sys.exit(4)
        if RUN.exists(): RUN.unlink()
    elif cmd == 'leftovers':
        # indented over several lines, as the real cli.py prints it (the 11:07 live run read it as unreadable)
        print(json.dumps({'instance': n, 'leftovers': ['server.exe 77'] if env('FAKE_LEFTOVERS') else []}, indent=1))
    sys.exit(0)
if name == 'hi':
    trace = Path(args[args.index('replay') + 1]).stem
    route = opt('--route')
    out = Path(opt('--out')); out.mkdir(parents=True, exist_ok=True)
    (FD / 'replayed').write_text('1')
    fl = env('FAKE_HI_FLAKE')   # ROUTE:N[:STEP]: the first N replays of that route stop as the known flake (at STEP)
    if fl and (not env('FAKE_HI_ONLY') or env('FAKE_HI_ONLY') == trace):
        parts = fl.split(':')
        cnt = FD / ('flake-%s-%s' % (trace, route))
        k = int(cnt.read_text()) if cnt.exists() else 0
        if parts[0] == route and k < int(parts[1]):
            cnt.write_text(str(k + 1))
            print(json.dumps({'exit': 5, 'steps_done': 9, 'checkpoints': [], 'took_s': 3.0,
                              'stop': {'exit': 5, 'step': int(parts[2]) if len(parts) > 2 else 10,
                                       'why': 'the wait reached its deadline',
                                       'wait': {'result': 'deadline', 'cond': 'screen_open CityView'}}}))
            sys.exit(5)
    if env('FAKE_HI_CRASH') == route and (not env('FAKE_HI_ONLY') or env('FAKE_HI_ONLY') == trace):
        # 0533: the game crashes without a box at step 5 (the load), as release-large did on c5h-b 2026-11-18 01:18;
        # hi.py's wait ends on the watcher's marker (exit 8), or without a marker on the game gone (exit 4)
        crash_now(args[1])
        code = 4 if env('FAKE_NO_MARKER') else 8
        why = ('the game is gone' if code == 4 else 'step 5 action 0 refused: the game process crashed without a box '
               '(nothing to press; the owner relaunches or releases the instance and files the bug, COMMON rule 29)')
        print(json.dumps({'exit': code, 'steps_done': 4, 'checkpoints': [], 'took_s': 40.0,
                          'stop': {'exit': code, 'step': 5, 'why': why}}))
        sys.exit(code)
    code = int(env('FAKE_HI_' + route.upper()) or 0)
    if env('FAKE_HI_ONLY') and env('FAKE_HI_ONLY') != trace:
        code = 0
    cps = [{'name': 'founded', 'step': 10, 'ok': True, 'expect': {'turn': 0}, 'judge': {'turn': 0, 'available': True},
            'checks': [], 'picture': str(out / 'c1.png')}]
    if code == 6:
        cps.append({'name': 'turn1', 'step': 16, 'ok': False, 'expect': {'turn': 1},
                    'judge': {'turn': 0, 'available': True}, 'diff': [{'field': 'turn', 'expect': 1, 'got': 0}],
                    'checks': [], 'picture': str(out / 'c2.png')})
    else:
        cps.append({'name': 'turn1', 'step': 16, 'ok': True, 'expect': {'turn': 1},
                    'judge': {'turn': 1, 'available': True}, 'checks': [], 'picture': str(out / 'c2.png')})
    res = {'exit': code, 'steps_done': 20 if code == 0 else 15, 'checkpoints': cps, 'took_s': 12.5,
           'stop': None if code == 0 else {'exit': code, 'why': 'checkpoint turn1 failed'}}
    if code == 0 and trace.endswith('-exit') and not env('FAKE_NO_EXIT') and RUN.exists():
        RUN.unlink()
    if code == 0 and trace.endswith('-exit') and env('FAKE_EXIT_CRASH') == route:
        (FD / 'crashmarker').write_text('1')   # the journey's EXIT, then a crash without a box (with FAKE_NO_EXIT: still running)
    print('replaying'); print(json.dumps(res))
    sys.exit(code)
print('unknown tool ' + name); sys.exit(1)
'''


SETTING_VARS = ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE')
_SETTINGS = {}


def setUpModule():
    """Ticket 4702: take_release.main's guard folders are settings (tools/release/settings.py: C5_PINS_DIR /
    C5_CLONES_DIR, else deps/local-settings.ini). The module sets temporary ones and names no settings file, so the
    tests pass in a checkout without this machine's deps/local-settings.ini (an exported one) and never read this
    machine's folders. Restored by tearDownModule."""
    td = tempfile.TemporaryDirectory()
    root = Path(td.name)
    _SETTINGS.update(td=td, old={k: os.environ.get(k) for k in SETTING_VARS})
    for name in ('pins', 'clones'):
        (root / name).mkdir()
    os.environ.update(C5_PINS_DIR=str(root / 'pins'), C5_CLONES_DIR=str(root / 'clones'),
                      C5_SETTINGS_FILE=str(root / 'no-settings.ini'))


def tearDownModule():
    for k, v in _SETTINGS.pop('old', {}).items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
    td = _SETTINGS.pop('td', None)
    if td is not None:
        td.cleanup()


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


class Fixture(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.calls = self.tmp / 'calls.txt'
        self.env_keys = []
        self._old_owner = os.environ.get('C5_POOL_OWNER')
        for k in [k for k in os.environ if k.startswith(('FAKE_', 'C5_RELEASE_'))]:
            os.environ.pop(k)
        fake = self.tmp / 'fake_hi_tool.py'
        fake.write_text(FAKE, encoding='utf-8')
        self.tools = {**tr.default_tools(), **{n: [PY, '-I', str(fake), n] for n in TOOLS},
                      'stockctl': [PY, '-I', str(REPO / 'tools' / 'hi' / 'stockctl.py')]}   # offline, real
        self.desc = self.make_desc()
        self.make_builds()
        self.inst = self.make_instance()
        self.make_journeys()
        self.setenv(FAKE_CALLS=str(self.calls), FAKE_DIR=str(self.tmp), C5_RELEASE_HI_WAIT='0',
                    C5_RELEASE_HI_INDEX=str(self.index), C5_RELEASE_HI_BY='rel-test')
        self.lines = []
        self.seed_state()

    def tearDown(self):
        for k in self.env_keys:
            os.environ.pop(k, None)
        if self._old_owner is None:
            os.environ.pop('C5_POOL_OWNER', None)
        else:
            os.environ['C5_POOL_OWNER'] = self._old_owner
        self._td.cleanup()

    def setenv(self, **kv):
        for k, v in kv.items():
            os.environ[k] = v
            self.env_keys.append(k)

    def make_desc(self):
        d = json.loads((tr.HERE / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
        save = self.tmp / 'REF.Civ5Save'
        save.write_bytes(b'ref')
        d.update(tag=TAG, reference_saves={'huge': {'path': save.as_posix(), 'sha256': sha(b'ref'), 'turn': 350}})
        return descriptor.parse(d, 'test', expect_tag=TAG)

    def make_builds(self):
        b = self.tmp / 'layout'
        for p in ('x64/bin', 'x64/proxy/bin', 'i0ia'):
            (b / p).mkdir(parents=True)
        (b / 'x64' / 'bin' / 'CvGameCore_Expansion2.dll').write_bytes(b'x64 dll')
        (b / 'x64' / 'bin' / 'server.exe').write_bytes(b'server exe')
        (b / 'x64' / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll').write_bytes(b'proxy dll')
        (b / 'i0ia' / 'CvGameCore_Expansion2.dll').write_bytes(b'x86 instance-aware')
        self.build = {'x64_commit': '04b1211b8' + '0' * 31, 'i0_commit': '26234371e' + '0' * 31,
                      'x64_layout': str(b / 'x64'), 'x64_bin': str(b / 'x64' / 'bin'),
                      'proxy_dll': str(b / 'x64' / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll'),
                      'i0ia_dll': str(b / 'i0ia' / 'CvGameCore_Expansion2.dll')}
        pl = self.work / 'inputs' / 'player-x86'
        pl.mkdir(parents=True)
        (pl / 'CvGameCore_Expansion2.dll').write_bytes(b'player dll')
        (self.tmp / 'purity.tsv').write_text('# vp64-purity 1\n')

    def make_instance(self, name='c5h-t'):
        inst = self.tmp / 'instances' / name
        u = inst / USER
        cp = u / 'MODS' / '(1) Community Patch'
        cp.mkdir(parents=True)
        (cp / 'CvGameCore_Expansion2.dll').write_bytes(b'the instance dll')
        for m in self.desc.mods:
            d = u / 'MODS' / m.folder
            d.mkdir(parents=True, exist_ok=True)
            (d / f'{m.folder} (v {m.version}).modinfo').write_text('<Mod/>')
        (u / 'GraphicsSettingsDX11.ini').write_bytes(GRAPHICS)
        (u / 'UserSettings.ini').write_bytes(USERSET)
        sd = u / 'ModdedSaves' / 'single'
        sd.mkdir(parents=True)
        (sd / 'Other-T5.Civ5Save').write_bytes(b'somebody else')
        (inst / 'instance.json').write_text(json.dumps({
            'name': name, 'user_dir': USER.replace('/', '\\'),
            'dll': {'path': USER.replace('/', '\\') + '\\MODS\\(1) Community Patch\\CvGameCore_Expansion2.dll'}}))
        return inst

    def make_journeys(self, ends=('exit', 'menu'), **over):
        jd = self.tmp / 'journeys'
        jd.mkdir(exist_ok=True)
        saves = self.tmp / 'saves'
        saves.mkdir(exist_ok=True)
        js = []
        for i, name in enumerate(('basic', 'large')):
            trace = f'release-{name}-{ends[i]}.jsonl'
            (jd / trace).write_text('{"t": "header"}\n')
            body = f'start save {name}'.encode()
            (saves / f'HI-T-{name}.Civ5Save').write_bytes(body)
            j = {'name': f'release-{name}', 'trace': trace, 'mods': 'cp,vp', 'window': [1920, 1080],
                 'start_save': {'name': f'HI-T-{name.upper()}-START', 'file': f'HI-T-{name}.Civ5Save',
                                'sha256': sha(body)},
                 'made_saves': ['HI-T-SAVED'], 'ends': ends[i]}
            if name == 'basic':
                j['known_flakes'] = [{'step': 10, 'exit': 5, 'cond': 'screen_open CityView',
                                      'why': 'city screen did not auto-open after founding'}]
            j.update(over.get(name, {}))
            js.append(j)
        self.index = jd / 'journeys.json'
        self.index.write_text(json.dumps({'format': 1, 'saves_dir': str(saves), 'journeys': js}))
        return js

    def seed_state(self):
        outs = {'build': self.build, 'purity': {'purity_tsv': str(self.tmp / 'purity.tsv')},
                'pack': {'pack': str(self.tmp / 'pack')}}
        st = {'format': tr.FORMAT, 'tag': TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': []}
        for name, cl, _o in tr.CANDIDATE:
            st['stages'].append({'name': name, 'cl_step': cl, 'status': 'ok', 'outputs': outs.get(name, {}),
                                 'inputs_sha': 'seed'})
        self.work.mkdir(parents=True, exist_ok=True)
        (self.work / 'state.json').write_text(json.dumps(st))

    def driver(self, for_owner=False, committed=True):
        """committed: the fixture's index stands for the committed tools/hi/journeys/journeys.json (D162 counts only
        that one; the planted-index test sets it False)."""
        stages = [s for s in tr.tier_stages('validated') if s[0] == 'hi_journey']
        cfg = {'tier': 'validated', 'who': 'test4506', 'gate_wait': 0}
        if for_owner:
            cfg['for_owner'] = True
        d = tr.Driver(TAG, self.work, self.desc, cfg, stages, tools=self.tools, out=self.lines.append)
        m = d.module('hi_journey')
        m.POLL_S = m.SETTLE_S = 0
        m.EXIT_WAIT_S = 0.3
        m.GAME_END_WAIT_S = 0.3
        m.CLAIM_RETRY_S = 0
        if committed:
            m.INDEX = self.index
        return d

    def go(self, for_owner=False, committed=True):
        return self.driver(for_owner, committed).run(only='hi_journey')

    def entry(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}['hi_journey']

    def called(self, name):
        if not self.calls.exists():
            return []
        return [ln[len(name) + 1:] for ln in self.calls.read_text().splitlines() if ln.startswith(name + ' ')]

    def user(self):
        return self.inst / USER

    def report(self):
        return json.loads((self.work / 'hi_journey' / 'report.json').read_text(encoding='utf-8'))

    def text(self):
        return '\n'.join(self.lines)

    def assert_restored(self):
        u = self.user()
        cp = u / 'MODS' / '(1) Community Patch'
        self.assertEqual((u / 'GraphicsSettingsDX11.ini').read_bytes(), GRAPHICS)
        self.assertEqual((u / 'UserSettings.ini').read_bytes(), USERSET)
        self.assertFalse((cp / 'vpproxy.ini').exists())
        self.assertFalse((cp / 'VP64').exists())
        self.assertEqual((cp / 'CvGameCore_Expansion2.dll').read_bytes(), b'the instance dll')
        self.assertEqual(sorted(p.name for p in (u / 'ModdedSaves' / 'single').iterdir()), ['Other-T5.Civ5Save'])
        self.assertFalse((self.tmp / 'fe-c5h-t.json').exists())
        self.assertTrue(any(c.startswith('verify c5h-t --quick') for c in self.called('provision')))
        self.assertTrue(any(c.startswith('release c5h-t --owner rel-test') for c in self.called('pool')))


class Journeys(Fixture):
    def test_both_pass_on_both_routes(self):
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'ok')
        o = e['outputs']
        self.assertEqual(o['verdict'], 'passed')
        self.assertIs(o['gating'], False)
        self.assertEqual(sorted(o['journeys']), ['release-basic', 'release-large'])
        for j in o['journeys'].values():
            self.assertEqual(j['verdict'], 'passed')
            self.assertEqual(sorted(j['routes']), ['product', 'stock'])
            self.assertEqual(j['compare']['counts'], {'same': 2})
        self.assertEqual((self.tmp / 'launches.txt').read_text().split(), ['on', 'off', 'on', 'off'])
        rep = Path(o['report'])
        self.assertTrue(rep.is_file() and Path(o['report_json']).is_file())
        self.assertIn('**Verdict: passed**', rep.read_text(encoding='utf-8'))
        self.assertIn('does not gate', rep.read_text(encoding='utf-8'))
        self.assertTrue((self.work / 'validated-report.json').is_file())   # the tier's report knows the stage
        self.assert_restored()
        # the claim: short, the ticket, the owner every harness call carries
        claim = self.called('pool')[0]
        self.assertIn('--length short', claim)
        self.assertIn('--ticket 0392', claim)
        self.assertTrue(all('[owner=rel-test]' in c for c in self.called('provision')))
        self.assertNotEqual(os.environ.get('C5_POOL_OWNER'), 'rel-test')
        # the replays: the journey file, the route, the claim ticket; the basic journey's EXIT ended the game itself
        reps = self.called('hi')
        self.assertEqual(len(reps), 4)
        self.assertIn('--route product', reps[0])
        self.assertIn('--ticket 0392', reps[0])
        basic = o['journeys']['release-basic']['routes']['product']
        self.assertEqual(basic['leftovers'], [])
        self.assertIn('exit_wait_s', basic)
        stops = self.called('cli')
        self.assertEqual(sum(1 for c in stops if ' stop ' in f' {c} '), 2)    # only the two 'menu' replays

    def test_settings_written_for_the_run(self):
        seen = {}
        d = self.driver()
        m = d.module('hi_journey')
        real = m.play

        def spy(ctx, st, j, route, *a):
            u = self.user()
            seen[route] = ((u / 'GraphicsSettingsDX11.ini').read_bytes(), (u / 'UserSettings.ini').read_bytes(),
                           (u / 'MODS' / '(1) Community Patch' / 'vpproxy.ini').read_bytes())
            return real(ctx, st, j, route, *a)
        m.play = spy
        self.assertEqual(d.run(only='hi_journey'), tr.EXIT_OK, self.text())
        g, u, ini = seen['product']
        self.assertIn(b'WindowResX = 1920\r\n', g)
        self.assertIn(b'WindowResY = 1080\r\n', g)
        self.assertIn(b'SinglePlayerAutoEndTurnEnabled = 0\r\n', u)
        self.assertIn(b'MultiplayerAutoEndTurnEnabled = 1\r\n', u)
        self.assertIn(b'[InterfaceSettings]\r\nAutoUnitCycle = 1\r\n', u)    # 0530: the recordings' value
        self.assertIn(b'pack=1', ini)
        self.assert_restored()

    def test_user_settings_without_auto_unit_cycle_fail_and_restore(self):
        """0530 negative: an instance whose UserSettings.ini has no AutoUnitCycle line is refused (nothing played,
        the stage failed and named the key), and every file is back by hash, the claim released."""
        bare = USERSET.replace(b'[InterfaceSettings]\r\nAutoUnitCycle = 0\r\n', b'')
        (self.user() / 'UserSettings.ini').write_bytes(bare)
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'failed')
        self.assertIn('AutoUnitCycle', json.dumps(e))
        self.assertEqual(self.called('hi'), [])
        self.assertEqual((self.user() / 'UserSettings.ini').read_bytes(), bare)
        self.assertEqual((self.user() / 'GraphicsSettingsDX11.ini').read_bytes(), GRAPHICS)
        self.assertTrue(any(c.startswith('release c5h-t --owner rel-test') for c in self.called('pool')))

    def test_start_save_is_the_newest_and_made_saves_go_first(self):
        sd = self.user() / 'ModdedSaves' / 'single'
        (sd / 'HI-T-SAVED.Civ5Save').write_bytes(b'left from an earlier run')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        rows = self.entry()['outputs']['journeys']['release-basic']['routes']
        self.assertEqual(rows['product']['removed_saves'], ['HI-T-SAVED.Civ5Save'])
        self.assert_restored()

    def test_failed_checkpoint_is_reported_not_gated(self):
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        self.assertEqual(self.entry()['status'], 'ok')
        self.assertEqual(o['verdict'], 'failed')
        lg = o['journeys']['release-large']
        self.assertEqual(lg['verdict'], 'failed')
        self.assertEqual(lg['routes']['product']['exit'], 6)
        self.assertEqual(lg['compare']['counts'], {'same': 1, 'patch only': 1})
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'passed')
        self.assertIn('turn1 FAIL', Path(o['report']).read_text(encoding='utf-8'))
        self.assert_restored()

    def test_journey_exit_that_leaves_the_game_running(self):
        self.setenv(FAKE_NO_EXIT='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        row = o['journeys']['release-basic']['routes']['product']
        self.assertEqual(row['exit'], 1)
        self.assertIn('still running', row['why'])
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'failed')
        self.assertEqual(sum(1 for c in self.called('cli') if ' stop ' in f' {c} '), 4)   # the stop cleaned up
        self.assert_restored()

    def test_one_journey_one_route(self):
        self.setenv(C5_RELEASE_HI_JOURNEYS='release-large', C5_RELEASE_HI_ROUTES='stock')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        self.assertEqual(list(o['journeys']), ['release-large'])
        self.assertEqual(list(o['journeys']['release-large']['routes']), ['stock'])
        self.assertEqual(o['journeys']['release-large']['compare']['counts'], None)
        self.assertEqual(o['verdict'], 'passed')
        self.assert_restored()


class KnownFlake(Fixture):
    """lead-0392 (2026-11-16): a stop on a journey's known flake (release-basic: the city screen not opening after
    founding) earns one rerun of the whole journey from a fresh launch; anything else stays a failure."""

    def launches(self):
        return (self.tmp / 'launches.txt').read_text().split()

    def test_flake_then_pass_is_passed_on_rerun(self):
        self.setenv(FAKE_HI_FLAKE='stock:1', FAKE_HI_ONLY='release-basic-exit')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        b = o['journeys']['release-basic']
        self.assertEqual(b['verdict'], 'passed on rerun (known: city screen did not auto-open after founding)')
        self.assertEqual(o['verdict'], 'passed')
        st = b['routes']['stock']
        self.assertEqual(st['exit'], 0)
        self.assertEqual(st['first_run']['exit'], 5)
        self.assertEqual(st['first_run']['stop_step'], 10)
        self.assertTrue(st['out'].endswith('release-basic-stock-rerun'))
        self.assertTrue(Path(st['first_run']['result']).is_file() and Path(st['result']).is_file())
        self.assertEqual(self.launches(), ['on', 'off', 'off', 'on', 'off'])   # one more launch: a fresh load
        rep = Path(o['report']).read_text(encoding='utf-8')
        self.assertIn('stock (first run)', rep)
        self.assertIn('Rerun once on a known flake', rep)
        self.assert_restored()

    def test_flake_twice_stays_failed(self):
        self.setenv(FAKE_HI_FLAKE='stock:2', FAKE_HI_ONLY='release-basic-exit')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        b = o['journeys']['release-basic']
        self.assertEqual(b['verdict'], 'failed')
        self.assertEqual(o['verdict'], 'failed')
        self.assertEqual(b['routes']['stock']['exit'], 5)
        self.assertEqual(b['routes']['stock']['first_run']['exit'], 5)
        self.assertEqual(self.launches(), ['on', 'off', 'off', 'on', 'off'])   # exactly one rerun, never two
        self.assert_restored()

    def test_other_stop_is_not_rerun(self):
        self.setenv(FAKE_HI_FLAKE='stock:1:12', FAKE_HI_ONLY='release-basic-exit')   # same wait, another step
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        st = o['journeys']['release-basic']['routes']['stock']
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'failed')
        self.assertNotIn('first_run', st)
        self.assertEqual(self.launches(), ['on', 'off', 'on', 'off'])
        self.assert_restored()

    def test_a_flake_is_never_rerun_for_a_journey_without_it(self):
        self.setenv(FAKE_HI_FLAKE='product:1', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        lg = self.entry()['outputs']['journeys']['release-large']
        self.assertEqual(lg['verdict'], 'failed')
        self.assertNotIn('first_run', lg['routes']['product'])


class NotPlayed(Fixture):
    def test_no_free_instance_is_skipped(self):
        self.setenv(FAKE_NO_INSTANCE='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertEqual(e['outputs']['verdict'], 'incomplete')
        self.assertIn('no test instance', Path(e['outputs']['report']).read_text(encoding='utf-8'))
        self.assertEqual(self.called('provision'), [])

    def test_missing_start_save_is_skipped_before_a_claim(self):
        (self.tmp / 'saves' / 'HI-T-large.Civ5Save').unlink()
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('HI-T-large.Civ5Save', e['outputs']['not_played'])
        self.assertEqual(self.called('pool'), [])

    def test_changed_start_save_is_not_played(self):
        (self.tmp / 'saves' / 'HI-T-basic.Civ5Save').write_bytes(b'another save')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        row = o['journeys']['release-basic']['routes']['product']
        self.assertFalse(row['ran'])
        self.assertIn('SHA-256', row['why'])
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'incomplete')
        self.assertEqual(o['verdict'], 'incomplete')
        self.assert_restored()

    def test_bad_index_is_skipped(self):
        self.make_journeys(large={'start_save': {'name': 'NOT-HI', 'file': 'x', 'sha256': 'ab'}})
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('HI-', e['what'] if e.get('what') else e['outputs']['not_played'])
        self.assertEqual(self.called('pool'), [])

    def test_unknown_journey_name_is_skipped(self):
        self.setenv(C5_RELEASE_HI_JOURNEYS='release-nothing')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry()['status'], 'skipped')
        self.assertIn('release-nothing', self.entry()['outputs']['not_played'])

    def test_no_instance_aware_dll_is_skipped(self):
        self.build.pop('i0ia_dll')
        self.seed_state()
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry()['status'], 'skipped')
        self.assertIn('instance-aware', self.entry()['outputs']['not_played'])

    def test_instance_with_a_proxy_layout_is_left_alone(self):
        m = json.loads((self.inst / 'instance.json').read_text())
        m['proxy'] = {'state': 'installed'}
        (self.inst / 'instance.json').write_text(json.dumps(m))
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertFalse(any(c.startswith('set-') for c in self.called('provision')))
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))


class Failures(Fixture):
    def test_exit_crash_after_a_passed_replay_is_not_a_box(self):
        # 2604 reruns (MC #3689): the stock game crashed without a box after the journey's own EXIT (the -ia test DLL's
        # exit crash, 0484): nothing runs, so the next replay is played, the marker is named in its row, and the stage
        # restores and releases as usual.
        self.setenv(FAKE_EXIT_CRASH='stock')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'ok')
        self.assertEqual(e['outputs']['verdict'], 'passed')
        self.assertNotIn('left_running', e['outputs'])
        self.assertEqual((self.tmp / 'launches.txt').read_text().split(), ['on', 'off', 'on', 'off'])
        rows = [r for j in e['outputs']['journeys'].values() for r in j['routes'].values()]
        self.assertTrue(any('0xC0000005' in (r.get('earlier_exit_crash') or '') for r in rows))
        self.assertTrue(any('--original' in c for c in self.called('provision')))
        self.assertTrue(any(c.startswith('release') for c in self.called('pool')))

    def test_crash_marker_while_the_game_runs_still_stops(self):
        # a crash marker while a game still runs on the instance is not taken as ended: LeftRunning as for a box
        self.setenv(FAKE_EXIT_CRASH='stock', FAKE_NO_EXIT='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('box', e['outputs']['left_running'])
        self.assertFalse(any('--original' in c for c in self.called('provision')))

    def test_box_leaves_the_game_and_the_claim(self):
        self.setenv(FAKE_HI_PRODUCT='8', FAKE_BOX='after-replay')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'failed')
        self.assertIn('box', e['outputs']['left_running'])
        self.assertFalse(any('--original' in c for c in self.called('provision')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertFalse(any(' stop ' in f' {c} ' for c in self.called('cli')))      # never pressed, never stopped
        self.assertTrue(Path(e['outputs']['report']).is_file())

    def test_refused_stop_is_not_routed_around(self):
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_STOP_REFUSE='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('COMMON 22', e['outputs']['left_running'])
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))

    def test_leftovers_fail_the_stage(self):
        self.setenv(FAKE_LEFTOVERS='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        self.assertIn('server.exe', self.entry()['outputs']['left_running'])

    def test_failed_verify_keeps_the_claim(self):
        self.setenv(FAKE_VERIFY_RC='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('RESTORE OF c5h-t FAILED', e['error'] if e.get('error') else self.text())
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertIs(e['outputs']['claim_released'], False)

    def test_front_end_not_original_fails_the_restore(self):
        self.setenv(FAKE_FE_PATCHED='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        self.assertIn('front end not original', json.dumps(self.entry()['outputs']['restore']))

    def test_gate_refusal_restores_and_passes_exit_3(self):
        self.setenv(FAKE_LAUNCH_REFUSE='1')
        self.assertEqual(self.go(), tr.EXIT_REFUSED, self.text())
        self.assertEqual(self.entry()['status'], 'pending')
        self.assert_restored()

    def test_stop_file_restores_and_stops(self):
        d = self.driver()
        m = d.module('hi_journey')
        real = m.play

        def stop_then_play(ctx, *a):
            (self.work / 'STOP').write_text('stop')
            return real(ctx, *a)
        m.play = stop_then_play
        self.assertEqual(d.run(only='hi_journey'), tr.EXIT_STOP, self.text())
        self.assertIn('STOP hi_journey', self.text())
        self.assert_restored()
        # 0634: STOP with no game running: nothing to end, no stop sent before the restore
        self.assertFalse(any(c.split()[2:3] == ['stop'] for c in self.called('cli')), self.called('cli'))
        self.assertNotIn('ended_before_restore', self.report())


class StopDuringJourney(Fixture):
    """Ticket 0634: the take's STOP file written while hi_journey's launched game runs (0626's rehearsal take,
    2026-11-21 01:46: the restore was refused, "CivilizationV_DX11.exe is running", and the claim kept). The fake tools
    refuse set-proxy, set-dll and frontend.py restore while the game runs, as the real ones do."""

    def setUp(self):
        super().setUp()
        self.setenv(FAKE_STOP_FILE=str(self.work / 'STOP'))   # written by the activation wait, the game running

    def order(self):
        """The fake calls in order, as (tool, args)."""
        return [tuple(ln.split(' ', 1)) for ln in self.calls.read_text().splitlines()]

    def test_reproduced_without_the_fix_the_restore_is_refused_and_loud(self):
        # the incident offline: with the end before the restore taken out, STOP goes straight to the restore while the
        # game runs; set-proxy --original, set-dll back and the front end are refused; the failure names the instance
        # and what is left changed, and keeps the claim
        d = self.driver()
        m = d.module('hi_journey')
        m.end_before_restore = lambda ctx, name, why: None
        self.assertEqual(d.run(only='hi_journey'), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('RESTORE OF c5h-t FAILED', e['error'])
        self.assertIn('left changed on c5h-t', e['error'])
        left = ' '.join(e['outputs']['left_changed'])
        for label in ('set-proxy --original', 'set-dll back', 'front end'):
            self.assertIn(label, left)
        self.assertIs(e['outputs']['claim_released'], False)
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertTrue((self.tmp / 'running').exists())
        self.assertIn('LEFT CHANGED', Path(e['outputs']['report']).read_text(encoding='utf-8'))

    def test_stop_with_a_running_game_ends_it_then_restores(self):
        self.assertEqual(self.go(), tr.EXIT_STOP, self.text())
        self.assertIn('STOP hi_journey', self.text())
        self.assertEqual(self.entry()['status'], 'pending')
        self.assertFalse((self.tmp / 'running').exists())
        self.assert_restored()
        calls = self.order()
        i_stop = next(i for i, (t, a) in enumerate(calls) if t == 'cli' and a.split()[2:3] == ['stop'])
        self.assertIn('--human-turn-hook', calls[i_stop][1])          # the harness's stop, autosave rule kept
        i_orig = next(i for i, (t, a) in enumerate(calls) if t == 'provision' and '--original' in a)
        self.assertLess(i_stop, i_orig)                                 # ended first, then restored
        self.assertEqual(sum(1 for t, a in calls if t == 'cli' and a.split()[2:3] == ['stop']), 1)
        r = self.report()
        self.assertEqual(r['ended_before_restore']['stop_rc'], 0)
        self.assertIn('StopRequested', r['ended_before_restore']['after'])
        self.assertTrue(r['restore']['ok'])

    def test_failure_after_a_launch_ends_the_game_too(self):
        # not only STOP: an exception after the launch (here in the activation wait) ends the game before the restore
        self.setenv(FAKE_STOP_FILE='')
        d = self.driver()
        m = d.module('hi_journey')

        def broken(ctx, name):
            raise RuntimeError('activation wait broke')
        m.wait_activation = broken
        self.assertEqual(d.run(only='hi_journey'), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('activation wait broke', e['error'])
        self.assertFalse((self.tmp / 'running').exists())
        self.assert_restored()
        self.assertIn('RuntimeError', e['outputs']['ended_before_restore']['after'])

    def test_game_that_does_not_end_fails_loud_and_keeps_the_claim(self):
        self.setenv(FAKE_STOP_LINGER='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('GAME ON c5h-t NOT ENDED, NOTHING RESTORED', e['error'])
        self.assertIn('still runs', e['error'])
        self.assertIn('claim (rel-test) is KEPT', e['error'])
        self.assertIs(e['outputs']['claim_released'], False)
        self.assertIn('set-proxy --original', ' '.join(e['outputs']['left_changed']))
        # nothing of the restore ran: no set-proxy --original, no front-end restore, no release; one stop, no kill
        self.assertFalse(any('--original' in c for c in self.called('provision')))
        self.assertFalse(any(c.split()[2:3] == ['restore'] for c in self.called('frontend')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertEqual(sum(1 for c in self.called('cli') if c.split()[2:3] == ['stop']), 1)
        self.assertTrue((self.tmp / 'running').exists())
        rep = Path(e['outputs']['report']).read_text(encoding='utf-8')
        self.assertIn('LEFT CHANGED', rep)
        self.assertIn('c5h-t', rep)

    def test_refused_stop_before_restore_fails_loud(self):
        self.setenv(FAKE_STOP_REFUSE='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('NOT ENDED', e['error'])
        self.assertIn('COMMON 22', e['error'])
        self.assertFalse(any('--original' in c for c in self.called('provision')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))

    def test_box_at_the_stop_is_never_pressed(self):
        d = self.driver()
        m = d.module('hi_journey')
        real = m.end_before_restore

        def box_then_end(ctx, name, why):
            (self.tmp / 'box-now').write_text('1')
            return real(ctx, name, why)
        m.end_before_restore = box_then_end
        self.setenv(FAKE_BOX_FILE=str(self.tmp / 'box-now'))
        self.assertEqual(d.run(only='hi_journey'), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('box', e['error'])
        self.assertFalse(any(c.split()[2:3] == ['stop'] for c in self.called('cli')))
        self.assertFalse(any('--original' in c for c in self.called('provision')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))


class FinishAfterBox(Fixture):
    """Ticket 0650 (found by 0643, 2026-11-21, human-input-testing.md 8.7): a journey that ends on a box leaves the game
    running and the instance laid (D29, D155: never pressed); after the claim's owner has closed the box, `hi_journey.py
    finish` puts every file of the undo record back by hash and releases the claim; it refuses while a game, a box or
    the patch's popup is there; and the next start refuses an instance whose restore was never finished."""

    def box_run(self):
        """The 0643 case offline: a box at the first replay. Returns the undo record's path."""
        self.setenv(FAKE_HI_PRODUCT='8', FAKE_BOX='after-replay')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('box', e['outputs']['left_running'])
        self.assertIn('hi_journey.py finish', e['error'])
        u = self.user()
        self.assertNotEqual((u / 'GraphicsSettingsDX11.ini').read_bytes(), GRAPHICS)     # the journey's settings stay
        self.assertNotEqual((u / 'UserSettings.ini').read_bytes(), USERSET)
        rec = Path(e['outputs']['undo_record'])
        d = json.loads(rec.read_text(encoding='utf-8'))
        self.assertEqual(d['state'], 'left')
        self.assertEqual(d['instance'], 'c5h-t')
        self.assertEqual(d['owner'], 'rel-test')
        kinds = {x['kind'] for x in d['entries']}
        self.assertLessEqual({'put_back', 'rm', 'provision', 'set_dll', 'frontend_restore', 'clear_made'}, kinds)
        self.assertNotIn('opaque', kinds)
        self.assertFalse(any(x['done'] for x in d['entries']))
        self.assertTrue(self.marker().is_file())
        self.assertFalse((self.work / 'hi_journey' / 'restore.json').exists())
        return rec

    def marker(self):
        return self.inst / 'run' / 'hi_journey_unrestored.json'

    def owner_closes_the_box(self):
        """What the claim's owner does by hand (D155): the box closed, the game ended."""
        os.environ.pop('FAKE_BOX', None)
        os.environ.pop('FAKE_HI_PRODUCT', None)
        if (self.tmp / 'running').exists():
            (self.tmp / 'running').unlink()

    def finish(self, where):
        tf = self.tmp / 'tools.json'
        tf.write_text(json.dumps({k: [str(x) for x in v] for k, v in self.tools.items()}), encoding='utf-8')
        m = self.driver().module('hi_journey')
        self.calls.write_text('')           # the finish's own calls only
        return m.main(['finish', str(where), '--tools', str(tf)], out=self.lines.append)

    def test_a_left_running_run_finished_files_back_by_hash(self):
        rec = self.box_run()
        self.owner_closes_the_box()
        self.assertEqual(self.finish(self.work), 0, self.text())
        self.assert_restored()                      # ini files, vpproxy, VP64, the DLL, saves, front end; verify; release
        self.assertTrue(any(c.startswith('renew c5h-t --owner rel-test') for c in self.called('pool')))
        prov = self.called('provision')
        self.assertTrue(any(c.startswith('set-proxy c5h-t --original') for c in prov), prov)
        self.assertTrue(any(c.startswith('set-dll c5h-t --dll') and 'kept' in c for c in prov), prov)
        self.assertFalse(any('--original' in c for c in prov if c.startswith('set-dll')))   # never the first DLL
        rs = json.loads((self.work / 'hi_journey' / 'restore.json').read_text(encoding='utf-8'))
        self.assertTrue(rs['ok'])
        self.assertEqual(rs['by'], 'finish (ticket 0650)')
        self.assertEqual(rs['verify_rc'], 0)
        self.assertTrue(any('GraphicsSettingsDX11.ini back: restored, SHA-256 ' + sha(GRAPHICS)[:16] in x
                            for x in rs['done']), rs['done'])
        self.assertTrue(any('UserSettings.ini back: restored, SHA-256 ' + sha(USERSET)[:16] in x for x in rs['done']))
        d = json.loads(rec.read_text(encoding='utf-8'))
        self.assertEqual(d['state'], 'restored')
        self.assertTrue(all(x['done'] for x in d['entries']))
        self.assertFalse(self.marker().exists())
        self.assertIn('Restore finished after the run (ticket 0650)',
                      (self.work / 'hi_journey' / 'report.md').read_text(encoding='utf-8'))
        self.assertIn('restored by hash and released', self.text())
        # a second finish has nothing to do and changes nothing
        self.assertEqual(self.finish(rec), 0, self.text())
        self.assertEqual(self.called('provision'), [])

    def test_finish_refused_with_a_game_a_box_or_the_popup_present(self):
        self.box_run()
        u = self.user()
        before = {p: (u / p).read_bytes() for p in ('GraphicsSettingsDX11.ini', 'UserSettings.ini')}
        cases = {'game': {}, 'box': {'FAKE_BOX': 'always'}, 'popup': {'FAKE_POPUP': '1'},
                 'leftovers': {'FAKE_LEFTOVERS': '1'}, 'claimed': {'FAKE_CLAIMED_BY': 'someone-else'}}
        for what, kv in cases.items():
            with self.subTest(what):
                os.environ.pop('FAKE_BOX', None)
                (self.tmp / 'running').write_text('1') if what == 'game' else (
                    (self.tmp / 'running').unlink() if (self.tmp / 'running').exists() else None)
                self.setenv(**kv)
                self.assertEqual(self.finish(self.work), 4, self.text())
                for k in kv:
                    os.environ.pop(k, None)
                self.assertIn('REFUSED, nothing changed', self.text())
                self.assertEqual({p: (u / p).read_bytes() for p in before}, before)       # nothing put back
                self.assertEqual(self.called('provision'), [])
                self.assertFalse(any(c.split()[2:3] == ['restore'] for c in self.called('frontend')))
                self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
                self.assertFalse(any(c.split()[2:3] in (['stop'], ['dismiss']) for c in self.called('cli')))
                self.assertTrue(self.marker().is_file())
                self.assertFalse((self.work / 'hi_journey' / 'restore.json').exists())
        self.assertIn('a game runs on c5h-t', self.text())
        self.assertIn('popup', self.text())
        self.assertIn('server.exe', self.text())
        self.assertIn('someone-else', self.text())

    def test_start_on_an_instance_with_unrestored_journey_settings_is_refused(self):
        self.box_run()
        self.owner_closes_the_box()                 # and releases the claim by hand, without the restore
        u = self.user()
        journey = {p: (u / p).read_bytes() for p in ('GraphicsSettingsDX11.ini', 'UserSettings.ini')}
        self.calls.write_text('')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('never restored', e['error'])
        self.assertIn('hi_journey.py finish', e['error'])
        self.assertIn('nothing laid', e['error'])
        self.assertEqual(e['outputs']['unrestored']['state'], 'left')
        self.assertIn('GraphicsSettingsDX11.ini back', e['outputs']['unrestored']['pending'])
        self.assertEqual({p: (u / p).read_bytes() for p in journey}, journey)      # not taken as a new "before"
        self.assertEqual(self.called('provision'), [])
        self.assertEqual(self.called('launch'), [])
        self.assertTrue(any(c.startswith('release c5h-t --owner rel-test') for c in self.called('pool')))
        # finished, the next start plays from the instance's own settings
        self.assertEqual(self.finish(self.work), 0, self.text())
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        self.assertEqual(o['settings_before'], {'graphics': sha(GRAPHICS), 'user': sha(USERSET)})
        self.assert_restored()
        self.assertFalse(self.marker().exists())

    def test_a_kept_dll_that_does_not_hash_is_refused_and_keeps_the_claim(self):
        rec = self.box_run()
        self.owner_closes_the_box()
        d = json.loads(rec.read_text(encoding='utf-8'))
        sd = next(x for x in d['entries'] if x['kind'] == 'set_dll')
        self.assertEqual(sd['sha256'], sha(b'the instance dll'))       # the DLL of before the run, by hash
        Path(sd['kept']).write_bytes(b'another dll')
        self.assertEqual(self.finish(self.work), 1, self.text())
        rs = json.loads((self.work / 'hi_journey' / 'restore.json').read_text(encoding='utf-8'))
        self.assertFalse(rs['ok'])
        self.assertTrue(any('REFUSED: the kept DLL' in x for x in rs['errors']), rs['errors'])
        self.assertIn('set-dll back', rs['left_changed'])
        self.assertFalse(any(c.startswith('set-dll') for c in self.called('provision')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertTrue(self.marker().is_file())
        self.assertEqual(json.loads(rec.read_text(encoding='utf-8'))['state'], 'left')
        self.assertIn('restore FAILED', self.text())
        # the other entries are done; a second finish after the copy is put right does only what is left
        Path(sd['kept']).write_bytes(b'the instance dll')
        self.assertEqual(self.finish(self.work), 0, self.text())
        self.assertEqual([c.split()[0] for c in self.called('provision')], ['set-dll', 'verify'])
        self.assert_restored()

    def test_a_normal_run_leaves_a_restored_record_and_no_marker(self):
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        d = json.loads(Path(o['undo_record']).read_text(encoding='utf-8'))
        self.assertEqual(d['state'], 'restored')
        self.assertTrue(all(x['done'] for x in d['entries']))
        self.assertFalse(self.marker().exists())
        rs = json.loads(Path(o['restore_record']).read_text(encoding='utf-8'))
        self.assertTrue(rs['ok'])
        self.assertEqual(rs['by'], 'the stage')
        self.assertEqual(o['kept_dll']['sha256'], sha(b'the instance dll'))

    def test_finish_without_a_record_is_an_argument_error(self):
        self.assertEqual(self.finish(self.tmp / 'nowhere'), 2, self.text())
        self.assertIn('no undo record', self.text())


class BoxlessCrash(Fixture):
    """Ticket 0533 (0517's tool gap 2): a crash without a box during a replay ends the game; the stage records the
    replay as crashed (code, address, evidence), crash.collect has run once, the journey is failed, the other replays
    are played, and the instance is restored by hash and released as after a normal end. A box or a popup stays
    LeftRunning (D29, D155)."""

    def large_product_crash(self, **kv):
        self.setenv(FAKE_HI_CRASH='product', FAKE_HI_ONLY='release-large-menu', **kv)

    def assert_crashed_and_restored(self):
        e = self.entry()
        self.assertEqual(e['status'], 'ok', self.text())
        o = e['outputs']
        self.assertNotIn('left_running', o)
        self.assertEqual(o['verdict'], 'failed')
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'passed')
        lg = o['journeys']['release-large']
        self.assertEqual(lg['verdict'], 'failed')
        row = lg['routes']['product']
        c = row['crash']
        self.assertEqual(c['code'], '0xC0000005')
        self.assertEqual(c['address'], 'CivilizationV_DX11.exe+0x37C6DA')
        self.assertEqual(c['pid'], 4242)
        self.assertIn('0x37C6DA', c['signature'])
        self.assertIn('crashed without a box', row['why'])
        self.assertEqual(row['exit'], 8 if 'watcher' in c['source'] else 4)
        self.assertEqual(lg['routes']['stock']['exit'], 0)               # the other route was played after the crash
        self.assertEqual((self.tmp / 'launches.txt').read_text().split(), ['on', 'off', 'on', 'off'])
        self.assertEqual([x['journey'] + '/' + x['route'] for x in o['crashes']], ['release-large/product'])
        self.assertIn('Crashed without a box on product', Path(o['report']).read_text(encoding='utf-8'))
        self.assertIs(o['claim_released'], True)
        self.assert_restored()
        self.assertTrue(any('--original' in x for x in self.called('provision')))
        return c

    def test_crash_with_the_watchers_marker_and_collect(self):
        self.large_product_crash(FAKE_WATCHER_COLLECT='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        c = self.assert_crashed_and_restored()
        self.assertIn('watcher', c['source'])
        self.assertEqual(c['va'], '0x0077C6DA')
        self.assertTrue(c['record'].endswith('exe-crash.txt'))
        self.assertEqual(c['collect']['by'], 'dialog watcher')
        self.assertEqual(self.called('crash'), [])                       # collected once: the watcher's
        # nothing to stop for the crashed game: the one stop is the stock 'menu' replay's
        self.assertEqual(sum(1 for x in self.called('cli') if ' stop ' in f' {x} '), 1)

    def test_crash_with_a_marker_without_collect_runs_crash_collect_once(self):
        self.large_product_crash()
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        c = self.assert_crashed_and_restored()
        self.assertEqual(c['collect']['by'], 'hi_journey (crash.py collect)')
        calls = self.called('crash')
        self.assertEqual(len(calls), 1, calls)
        self.assertTrue(calls[0].startswith('collect --instance c5h-t --since '))
        self.assertIn('--pid 4242', calls[0])
        self.assertTrue(c['collect']['dest'].endswith('stage-crash'))

    def test_crash_proved_by_the_proxy_record_alone(self):
        self.large_product_crash(FAKE_NO_MARKER='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        c = self.assert_crashed_and_restored()
        self.assertIn('exe-crash.txt', c['source'])
        self.assertEqual(len(self.called('crash')), 1)

    def test_crash_during_mod_activation(self):
        self.setenv(FAKE_ACT_CRASH='1', FAKE_WATCHER_COLLECT='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        row = o['journeys']['release-basic']['routes']['product']
        self.assertIs(row['ran'], False)
        self.assertEqual(row['crashed_when'], 'during mod activation')
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'failed')   # a crash, not 'incomplete'
        self.assertEqual(len(self.called('hi')), 3)
        self.assert_restored()

    def test_crash_for_owner_stops_on_d162(self):
        self.large_product_crash(FAKE_WATCHER_COLLECT='1')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        self.assertIn('release-large: failed', self.text())
        self.assert_restored()

    # --- negative: what is not a boxless crash keeps today's handling ---------------------------------------------

    def test_crash_marker_carrying_the_patch_popup_stays_left_running(self):
        self.large_product_crash(FAKE_CRASH_POPUP='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('box', e['outputs']['left_running'])
        self.assertFalse(any('--original' in x for x in self.called('provision')))
        self.assertFalse(any(x.startswith('release') for x in self.called('pool')))
        self.assertEqual(self.called('crash'), [])

    def test_game_gone_without_marker_or_record_is_no_crash(self):
        self.large_product_crash(FAKE_NO_MARKER='1', FAKE_NO_RECORD='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        row = o['journeys']['release-large']['routes']['product']
        self.assertNotIn('crash', row)
        self.assertEqual(row['exit'], 4)
        self.assertNotIn('crashes', o)
        self.assertEqual(self.called('crash'), [])
        self.assert_restored()

    def test_a_stale_record_of_another_pid_is_no_crash(self):
        self.large_product_crash(FAKE_NO_MARKER='1', FAKE_STALE_RECORD='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        row = self.entry()['outputs']['journeys']['release-large']['routes']['product']
        self.assertNotIn('crash', row)
        self.assertEqual(self.called('crash'), [])
        self.assert_restored()


class FakeToolsGuard(Fixture):
    def test_missing_fake_fails_before_any_claim(self):
        """A driver with fake tools that does not name every tool of the stage: failed before any claim, and no
        command is started at all (the real launch.py, cli.py, hi.py, set_ini.py never run against a fake)."""
        del self.tools['launch']
        started = []
        real_run = tr.subprocess.run

        def spy(cmd, *a, **k):
            started.append([str(c) for c in cmd])
            return real_run(cmd, *a, **k)
        tr.subprocess.run = spy
        try:
            self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        finally:
            tr.subprocess.run = real_run
        e = self.entry()
        self.assertEqual(e['status'], 'failed')
        self.assertIn("'launch'", e['error'])
        self.assertEqual(started, [])
        self.assertFalse(self.calls.exists() and self.calls.read_text())


class ForOwner(Fixture):
    """D162 (the owner, 2026-11-16; issue 0436): a take for the owner (--for-owner) stops the tier unless both journeys passed on
    this build, product and stock; a report-only take keeps the stage's result. The three outcomes on both kinds."""

    def summary(self):
        return [ln for ln in self.lines if 'D162 HI release journeys' in ln]

    # --- passed
    def test_passed_for_owner_reaches_and_says_yes(self):
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'ok')
        self.assertIs(e['outputs']['gating'], True)
        self.assertIs(e['outputs']['owner']['allowed'], True)
        self.assertEqual(sorted(e['outputs']['candidate']['sha256']), ['i0ia_dll', 'proxy_dll', 'server', 'x64_dll'])
        rep = Path(e['outputs']['report']).read_text(encoding='utf-8')
        self.assertIn('gates: a take for the owner; D162', rep)
        self.assertIn('**For the owner (D162): yes**', rep)
        self.assertTrue(self.summary() and 'for the owner: yes' in self.summary()[-1], self.text())
        st = json.loads((self.work / 'state.json').read_text())
        self.assertIs(st['d162']['allowed'], True)
        self.assertIs(st['d162']['for_owner'], True)
        self.assert_restored()

    def test_passed_report_only_says_yes_and_does_not_gate(self):
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertIs(self.entry()['outputs']['gating'], False)
        self.assertNotIn('owner', self.entry()['outputs'])
        self.assertIn('for the owner: yes', self.summary()[-1])
        self.assertIn('not a take for the owner', self.summary()[-1])

    # --- failed
    def test_failed_for_owner_stops_the_tier_naming_d162(self):
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('D162', e['stop']['what'])
        self.assertIn('verdict failed', e['stop']['what'])
        self.assertIn('--for-owner', e['stop']['unblock'])
        self.assertIn('take_release: STOP hi_journey', self.text())
        self.assertIs(e['outputs']['owner']['allowed'], False)
        self.assertIn('**For the owner (D162): NO**', Path(e['outputs']['report']).read_text(encoding='utf-8'))
        self.assertIn('for the owner: NO, refused by D162', self.summary()[-1])
        rep = json.loads((self.work / 'validated-report.json').read_text(encoding='utf-8'))
        self.assertEqual(rep['stages']['hi_journey']['status'], 'stop')   # the tier's report: not validated
        self.assertIn('D162', rep['stages']['hi_journey']['what'])
        from stages import report as R
        st = {n: {'status': 'ok', 'outputs': {}} for n in rep['stages']}
        st['wholegames']['outputs'] = {'parity': 'passed'}
        st['hi_journey'] = rep['stages']['hi_journey']
        self.assertIn('hi_journey stop: D162', R.verdict(st))
        self.assert_restored()

    def test_failed_report_only_stays_ok_and_says_no(self):
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry()['status'], 'ok')
        self.assertIn('for the owner: NO, refused by D162: verdict failed', self.summary()[-1])

    # --- incomplete or skipped
    def test_skipped_for_owner_stops_not_played_is_not_passed(self):
        self.setenv(FAKE_NO_INSTANCE='1')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('not played is not passed', e['stop']['what'])
        self.assertIn('no test instance', e['stop']['what'])
        self.assertEqual(self.called('provision'), [])

    def test_missing_save_for_owner_stops(self):
        (self.tmp / 'saves' / 'HI-T-large.Civ5Save').unlink()
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        self.assertIn('HI-T-large.Civ5Save', self.entry()['stop']['what'])

    def test_incomplete_for_owner_stops(self):
        (self.tmp / 'saves' / 'HI-T-basic.Civ5Save').write_bytes(b'another save')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        e = self.entry()
        self.assertIn('verdict incomplete', e['stop']['what'])
        self.assertIn('release-basic: route product/stock not played', e['stop']['what'])
        self.assert_restored()

    def test_skipped_and_incomplete_report_only_keep_their_status(self):
        self.setenv(FAKE_NO_INSTANCE='1')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry()['status'], 'skipped')
        self.assertIn('for the owner: NO', self.summary()[-1])
        self.assertIn('not played is not passed', self.summary()[-1])

    # --- what D162 counts
    def test_one_journey_or_one_route_is_not_enough_for_owner(self):
        self.setenv(C5_RELEASE_HI_JOURNEYS='release-large')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        self.assertIn('release-basic was not played', self.entry()['stop']['what'])
        self.lines.clear()
        self.setenv(C5_RELEASE_HI_JOURNEYS='', C5_RELEASE_HI_ROUTES='product')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        self.assertIn('route stock not played', self.entry()['stop']['what'])

    def test_a_planted_index_never_counts_for_owner(self):
        self.assertEqual(self.go(for_owner=True, committed=False), tr.EXIT_STOP, self.text())
        self.assertIn('not the committed', self.entry()['stop']['what'])
        self.assertEqual(self.entry()['outputs']['verdict'], 'passed')

    def test_a_pass_on_rerun_counts_for_owner(self):
        self.setenv(FAKE_HI_FLAKE='stock:1', FAKE_HI_ONLY='release-basic-exit')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        self.assertIs(self.entry()['outputs']['owner']['allowed'], True)

    # --- a recorded result from an earlier take
    def test_report_only_fail_then_for_owner_plays_again(self):
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        before = len(self.called('hi'))
        os.environ.pop('FAKE_HI_PRODUCT')
        os.environ.pop('FAKE_HI_ONLY')
        d = self.driver(for_owner=True)
        self.assertEqual(d.run(), tr.EXIT_OK, self.text())          # not --only: the inputs are unchanged
        self.assertEqual(len(self.called('hi')) - before, 4)        # played again: the recorded fail did not count
        self.assertIn('does not pass D162', self.text())
        self.assertIs(self.entry()['outputs']['owner']['allowed'], True)

    def test_report_only_pass_then_for_owner_is_not_played_again(self):
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        before = len(self.called('hi'))
        self.assertEqual(self.driver(for_owner=True).run(), tr.EXIT_OK, self.text())
        self.assertEqual(len(self.called('hi')), before)
        self.assertIn('tier validated reached', self.text())

    def test_tier_end_check_without_the_stage_hook(self):
        """The driver's own check at the tier's end: a recorded fail that is not played again (the hook removed) still
        keeps a take for the owner from its tier."""
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        d = self.driver(for_owner=True)
        d.module('hi_journey').again = lambda ctx, e: None
        self.lines.clear()
        self.assertEqual(d.run(), tr.EXIT_STOP, self.text())
        self.assertIn('take_release: STOP hi_journey (CL step -): D162', self.text())
        self.assertNotIn('tier validated reached', self.text())
        self.assertNotIn('reached', json.loads((self.work / 'state.json').read_text()))

    def test_another_build_is_refused(self):
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        Path(self.build['proxy_dll']).write_bytes(b'a rebuilt proxy')
        d = self.driver(for_owner=True)
        chk = d.d162_check()
        self.assertIs(chk['allowed'], False)
        self.assertIn('proxy_dll differ', '; '.join(chk['why']))

    # --- the hand-out check
    def owner_check(self, *extra):
        out = []
        rc = tr.main([TAG, '--owner-check', '--work', str(self.work), *extra], out=out.append)
        return rc, '\n'.join(out)

    def staged(self, proxy=b'proxy dll'):
        f = self.tmp / 'staged' / 'MODS' / '(1) Community Patch'
        (f / 'VP64').mkdir(parents=True, exist_ok=True)
        (f / 'CvGameCore_Expansion2.dll').write_bytes(proxy)
        (f / 'VP64' / 'CvGameCore_Expansion2.dll').write_bytes(b'x64 dll')
        (f / 'VP64' / 'server.exe').write_bytes(b'server exe')
        return self.tmp / 'staged'

    def test_owner_check_cli(self):
        rc, txt = self.owner_check()
        self.assertEqual(rc, tr.EXIT_STOP, txt)                      # nothing played yet
        self.assertIn('refused by D162', txt)
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        rc, txt = self.owner_check()
        self.assertEqual(rc, tr.EXIT_OK, txt)
        self.assertIn('for the owner: yes', txt)
        self.assertIn('played on proxy_dll ' + sha(b'proxy dll'), txt)
        rc, txt = self.owner_check('--build-folder', str(self.staged()))
        self.assertEqual(rc, tr.EXIT_OK, txt)
        rc, txt = self.owner_check('--build-folder', str(self.staged(proxy=b'other proxy')))
        self.assertEqual(rc, tr.EXIT_STOP, txt)
        self.assertIn('no file with the proxy_dll SHA-256', txt)
        self.assertIn('D162', txt)

    def test_old_option_names_still_work(self):
        """Ticket 0708: --for-art and --art-check (the old names) still mark and check a take, with one note each."""
        out = []
        self.assertEqual(tr.main([TAG, '--for-art', '--work', str(self.tmp / 'w3')], out=out.append), tr.EXIT_ARGS)
        self.assertIn('take_release: note: --for-art is the old name of --for-owner; it still works, use --for-owner',
                      out)
        self.assertIn('--tier validated', '\n'.join(out))           # the marker was set, as by the new name
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        out = []
        rc = tr.main([TAG, '--art-check', '--work', str(self.work)], out=out.append)
        self.assertEqual(rc, tr.EXIT_OK, out)
        self.assertEqual(sum('old name of --owner-check' in ln for ln in out), 1, out)
        self.assertTrue(any('for the owner: yes' in ln for ln in out), out)

    def test_owner_check_reads_a_state_written_before_0708(self):
        """A take marked before ticket 0708 records config for_art; --owner-check still counts it as marked."""
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        p = self.work / 'state.json'
        st = json.loads(p.read_text())
        st['config'].pop('for_owner', None)
        st['config']['for_art'] = True
        p.write_text(json.dumps(st))
        rc, txt = self.owner_check()
        self.assertEqual(rc, tr.EXIT_OK, txt)
        self.assertNotIn('not marked', txt)

    def test_owner_check_cli_failed_and_no_state(self):
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        rc, txt = self.owner_check('--build-folder', str(self.staged()))
        self.assertEqual(rc, tr.EXIT_STOP, txt)
        self.assertIn('verdict failed', txt)
        self.assertIn('not marked --for-owner', txt)
        out = []
        self.assertEqual(tr.main([TAG, '--owner-check', '--work', str(self.tmp / 'none')], out=out.append),
                         tr.EXIT_ARGS)
        self.assertIn('no state', '\n'.join(out))

    def kit(self):
        import importlib.util
        p = REPO / 'research' / '06-tooling-and-workflows' / 'evidence' / '0324' / 'stage_first_playable.py'
        spec = importlib.util.spec_from_file_location('kit0324', str(p))
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        return m

    def test_play_first_build_kit_refuses_without_a_pass(self):
        """The kit (research evidence/0324/stage_first_playable.py): `stage` refuses before writing anything without
        --release-work, and on a take whose journeys did not pass; the kit's check command answers the hand-out."""
        import types as _t
        k = self.kit()
        k.art_gate.__defaults__ = (TAG,)                            # the fixture's tag, not the kit's 5.4.8
        out = self.tmp / 'kitout'
        a = _t.SimpleNamespace(out=str(out), layout=str(Path(self.build['x64_layout'])), not_for_art=False,
                               release_work=None)
        with self.assertRaises(SystemExit) as cm:
            k.stage(a)
        self.assertIn('REFUSED by D162', str(cm.exception.code))
        self.setenv(FAKE_HI_PRODUCT='6', FAKE_HI_ONLY='release-large-menu')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        rc, lines = k.art_gate(self.work, Path(self.build['x64_layout']), tag=TAG)
        self.assertEqual(rc, tr.EXIT_STOP, lines)
        a.release_work = str(self.work)
        with self.assertRaises(SystemExit) as cm:
            k.stage(a)
        self.assertIn('REFUSED by D162', str(cm.exception.code))
        self.assertFalse(out.exists())                              # nothing staged
        os.environ.pop('FAKE_HI_PRODUCT')
        os.environ.pop('FAKE_HI_ONLY')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_OK, self.text())
        rc, lines = k.art_gate(self.work, Path(self.build['x64_layout']), tag=TAG)   # the layout holds the bytes
        self.assertEqual(rc, tr.EXIT_OK, lines)
        self.assertTrue(any('for the owner: yes' in ln for ln in lines))
        rc, lines = k.art_gate(self.work, self.staged(proxy=b'other proxy'), tag=TAG)
        self.assertEqual(rc, tr.EXIT_STOP, lines)
        self.assertEqual(k.art(_t.SimpleNamespace(folder=str(self.staged()), release_work=str(self.work))), 0)

    def test_for_owner_needs_the_validated_tier(self):
        cfg = {'tier': 'candidate', 'for_owner': True, 'fork': None}
        with self.assertRaises(tr.ArgError) as cm:
            tr.check_config(cfg, self.work)
        self.assertIn('D162', str(cm.exception))
        out = []
        self.assertEqual(tr.main([TAG, '--for-owner', '--work', str(self.tmp / 'w2')], out=out.append), tr.EXIT_ARGS)
        self.assertIn('--tier validated', '\n'.join(out))


INSTALLED_INI = b'# installed\r\nmode=on\r\nfallback_dll=CvGameCore_Expansion2_x86.dll\r\nx64_dir=VP64\r\npack=1\r\n'


class Installed(Fixture):
    """Issue 2604 (MC #3211): C5_RELEASE_HI_INSTALLED=<instance> plays on the layout the installer put there: the
    proxy, x64 DLL and server.exe checked against the build by SHA-256, never set-dll or set-proxy, no test pack, the
    verdict lines naming the installer-installed layout. The default path is unchanged (the other classes, and
    test_default_inputs_carry_no_installed_key here)."""

    def install(self, form='mods', x64=b'x64 dll', record=True, end=True, x64_name='CvGameCore_Expansion2.dll',
                manifest_name=None):
        u = self.user()
        cp = u / 'MODS' / '(1) Community Patch'
        (cp / 'CvGameCore_Expansion2_x86.dll').write_bytes(b'the instance dll')     # VP's own DLL, kept
        (cp / 'CvGameCore_Expansion2.dll').write_bytes(b'proxy dll')
        (cp / 'VP64').mkdir()
        (cp / 'VP64' / x64_name).write_bytes(x64)        # issue 2606: an installer pack's name is _x64
        (cp / 'VP64' / 'server.exe').write_bytes(b'server exe')
        (cp / 'VP64' / 'manifest.json').write_text(
            json.dumps({'x64_dll': {'file': manifest_name}}) if manifest_name else '{}')
        (cp / 'vpproxy.ini').write_bytes(INSTALLED_INI)
        if record:
            rd = u / 'VP64' / 'install'
            rd.mkdir(parents=True)
            lines = ['VP64-INSTALL-RECORD\t1', 'release\tRelease-9.9.9', 'variant\tstd', 'config\tthin',
                     f'form\t{form}', 'cpdir\tMG\\MODS\\(1) Community Patch', f'mygames\t{u}',
                     f'replaced\t{sha(b"the instance dll")}\t{sha(b"proxy dll")}\tCP\\CvGameCore_Expansion2.dll']
            lines += ['end\t1'] if end else ['partial\t1']
            (rd / 'record.txt').write_bytes(b'\xef\xbb\xbf' + '\r\n'.join(lines).encode('utf-8') + b'\r\n')
        self.setenv(C5_RELEASE_HI_INSTALLED='c5h-t')
        return cp

    def tree(self):
        return {str(p.relative_to(self.inst)): p.read_bytes() for p in sorted(self.inst.rglob('*')) if p.is_file()}

    def test_plays_on_the_installed_layout_without_laying_one(self):
        cp = self.install()
        before = self.tree()
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'ok', e)
        o = e['outputs']
        self.assertEqual(o['verdict'], 'passed')
        self.assertEqual(o['layout']['kind'], 'installer-installed')
        self.assertEqual(o['layout']['form'], 'mods')
        self.assertEqual(o['layout']['sha256'], {'proxy_dll': sha(b'proxy dll'), 'x64_dll': sha(b'x64 dll'),
                                                 'server': sha(b'server exe')})
        self.assertEqual(o['layout']['fallback_sha256'], sha(b'the instance dll'))
        # never set-dll, set-proxy or a test pack on that instance; no provision.py verify (it would name the
        # installer's files); the claim names the instance
        prov = self.called('provision')
        self.assertFalse([c for c in prov if c.startswith(('set-dll', 'set-proxy', 'verify'))], prov)
        self.assertFalse(self.called('pack'))
        self.assertIn('--name c5h-t', self.called('pool')[0])
        # 0640: the installed layout plays the product route only (C5_RELEASE_HI_ROUTES unset), no mode=off launch
        self.assertEqual((self.tmp / 'launches.txt').read_text().split(), ['on', 'on'])
        self.assertEqual(o['routes'], ['product'])
        self.assertEqual({j: sorted(r['routes']) for j, r in o['journeys'].items()},
                         {'release-basic': ['product'], 'release-large': ['product']})
        # the instance is back byte for byte: the layout, vpproxy.ini (its mode= flips), settings, saves
        after = self.tree()
        self.assertEqual(sorted(after), sorted(before))
        self.assertEqual([k for k in after if after[k] != before[k]], [])
        self.assertEqual((cp / 'vpproxy.ini').read_bytes(), INSTALLED_INI)
        self.assertTrue(o['restore']['ok'], o['restore'])
        self.assertIsNone(o['restore']['verify_rc'])
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))
        # the verdict lines name the layout
        rep = Path(o['report']).read_text(encoding='utf-8')
        self.assertIn('**Verdict: passed on the installer-installed layout (c5h-t)**', rep)
        self.assertIn('## release-basic: passed on the installer-installed layout (c5h-t)', rep)
        self.assertIn('never run: provision.py set-dll, set-proxy', rep)
        line = [ln for ln in self.lines if 'D162 HI release journeys' in ln][-1]
        self.assertIn('on the installer-installed layout (c5h-t)', line)

    def test_installed_x64_dll_by_the_manifests_name(self):
        """Issue 2606 (MC #3403): an installer pack names the x64 DLL CvGameCore_Expansion2_x64.dll in its manifest;
        the installed check hashes that file (the proxy loads it), not VP64/CvGameCore_Expansion2.dll."""
        from stages import hi_journey
        cp = self.install(x64_name='CvGameCore_Expansion2_x64.dll', manifest_name='CvGameCore_Expansion2_x64.dll')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        o = self.entry()['outputs']
        self.assertEqual(o['verdict'], 'passed')
        self.assertEqual(o['layout']['x64_file'], 'CvGameCore_Expansion2_x64.dll')
        self.assertEqual(o['layout']['sha256']['x64_dll'], sha(b'x64 dll'))
        self.assertFalse((cp / 'VP64' / 'CvGameCore_Expansion2.dll').exists())
        v = cp / 'VP64'
        self.assertEqual(hi_journey.installed_x64_name(v), 'CvGameCore_Expansion2_x64.dll')
        (v / 'manifest.json').write_text('{}')            # no name in the manifest: the _x64 file when present
        self.assertEqual(hi_journey.installed_x64_name(v), 'CvGameCore_Expansion2_x64.dll')
        (v / 'CvGameCore_Expansion2_x64.dll').unlink()     # else the old name (a pack from before 2606)
        self.assertEqual(hi_journey.installed_x64_name(v), 'CvGameCore_Expansion2.dll')
        (v / 'manifest.json').write_text('{"x64_dll": {"file": "..\\\\x.dll"}}')   # never a path out of VP64
        self.assertEqual(hi_journey.installed_x64_name(v), 'CvGameCore_Expansion2.dll')

    def test_manifest_names_a_missing_x64_dll_refused(self):
        """Issue 2606, negative: the manifest names _x64 but only the old name is there: refused, nothing played."""
        self.install(manifest_name='CvGameCore_Expansion2_x64.dll')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        self.assertIn('missing', json.dumps(self.entry()))
        self.assertFalse(self.called('launch'))

    def test_needs_no_instance_aware_dll(self):
        self.install()
        self.build.pop('i0ia_dll')
        self.seed_state()
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry()['outputs']['verdict'], 'passed')

    def test_other_x64_bytes_are_refused_and_nothing_played(self):
        cp = self.install(x64=b'another x64 dll')
        self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
        e = self.entry()
        self.assertIn('x64_dll', e['error'] if 'error' in e else e.get('what', ''))
        self.assertFalse(self.called('hi'))
        self.assertFalse(self.called('launch'))
        self.assertFalse([c for c in self.called('provision') if c.startswith(('set-dll', 'set-proxy'))])
        self.assertEqual((cp / 'VP64' / 'CvGameCore_Expansion2.dll').read_bytes(), b'another x64 dll')
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))

    def test_no_record_or_an_unfinished_one_is_refused(self):
        for kw in ({'record': False}, {'end': False}):
            with self.subTest(**kw):
                self.tearDown()
                self.setUp()
                self.install(**kw)
                self.assertEqual(self.go(), tr.EXIT_FAILED, self.text())
                self.assertIn('installer record', json.dumps(self.entry()))
                self.assertFalse(self.called('launch'))

    def test_modpack_form_is_skipped(self):
        self.install(form='modpack')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('mods form', json.dumps(e))
        self.assertFalse(self.called('launch'))

    def test_for_owner_line_names_the_layout(self):
        """0640: the installed layout plays the product route only, so it alone is never a D162 yes (D162's rule is
        unchanged: both journeys on both routes); the line names the layout and the stock route not played."""
        self.install()
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        o = self.entry()['outputs']
        self.assertEqual(o['verdict'], 'passed')
        self.assertIs(o['owner']['allowed'], False)
        self.assertEqual(o['owner']['layout'], 'on the installer-installed layout (c5h-t)')
        self.assertIn('release-basic: route stock not played', o['owner']['why'])
        self.assertIn('(D162): NO**', Path(o['report']).read_text(encoding='utf-8'))
        line = [ln for ln in self.lines if 'D162 HI release journeys' in ln][-1]
        self.assertIn('on the installer-installed layout (c5h-t)', line)
        self.assertIn(': NO, refused by D162', line)

    def test_stock_route_on_the_installed_layout_is_refused_loudly(self):
        """0640 (b), negative: a route list with stock on the installed layout is a stop before anything is claimed,
        laid or launched; the instance is untouched and the report names the refusal and the stage-laid way."""
        for routes in ('stock', 'product,stock', 'stock,product'):
            with self.subTest(routes=routes):
                self.tearDown()
                self.setUp()
                cp = self.install()
                before = self.tree()
                self.setenv(C5_RELEASE_HI_ROUTES=routes)
                self.assertEqual(self.go(), tr.EXIT_STOP, self.text())
                e = self.entry()
                self.assertEqual(e['status'], 'stop', e)
                what = e['stop']['what']
                self.assertIn('REFUSED, nothing claimed or played', what)
                self.assertIn('cannot be judged', what)
                self.assertIn('stage-laid layout', e['stop']['unblock'])
                self.assertIn('LOUD: hi_journey REFUSED', self.text())
                self.assertEqual(self.called('pool'), [])          # nothing claimed
                self.assertEqual(self.called('provision'), [])
                self.assertFalse(self.called('launch'))
                self.assertFalse(self.called('hi'))
                self.assertFalse((self.tmp / 'launches.txt').exists())
                self.assertEqual(self.tree(), before)
                self.assertEqual((cp / 'vpproxy.ini').read_bytes(), INSTALLED_INI)
                rep = self.report()
                self.assertEqual(rep['verdict'], 'incomplete')
                self.assertEqual(rep['refused_routes'], routes.split(','))
                self.assertEqual(rep['layout']['kind'], 'installer-installed')

    def test_stock_refused_on_the_installed_layout_for_owner_too(self):
        """0640 (b): the refusal is a stop on a --for-owner take as well (not a D162 'not played' skip)."""
        self.install()
        self.setenv(C5_RELEASE_HI_ROUTES='product,stock')
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.text())
        self.assertIn('cannot be judged', self.entry()['stop']['what'])
        self.assertEqual(self.called('pool'), [])

    def test_product_route_named_on_the_installed_layout_plays(self):
        """0640 (b): C5_RELEASE_HI_ROUTES=product set from outside (as the tier stage sets it) plays as the default."""
        self.install()
        self.setenv(C5_RELEASE_HI_ROUTES='product')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry()['outputs']['verdict'], 'passed')
        self.assertEqual((self.tmp / 'launches.txt').read_text().split(), ['on', 'on'])

    def test_installed_routes_rule(self):
        from stages import hi_journey
        self.assertEqual(hi_journey.installed_routes('c5h-t', ['product']), ['product'])
        for rs in (['stock'], ['product', 'stock'], ['stock', 'product'], []):
            with self.assertRaises(hi_journey.InstalledStockRefused) as e:
                hi_journey.installed_routes('c5h-t', rs)
            self.assertIn('cannot be judged', str(e.exception))
        # the default settings: product only when installed, both routes otherwise (the default inputs unchanged)
        self.assertEqual(hi_journey.settings()['routes'], 'product,stock')
        self.setenv(C5_RELEASE_HI_INSTALLED='c5h-t')
        self.assertEqual(hi_journey.settings()['routes'], 'product')
        self.setenv(C5_RELEASE_HI_ROUTES='stock')
        self.assertEqual(hi_journey.settings()['routes'], 'stock')     # refused by _run, not rewritten here

    def test_default_path_never_lays_over_an_installed_layout(self):
        self.install()
        os.environ.pop('C5_RELEASE_HI_INSTALLED')
        self.assertEqual(self.go(), tr.EXIT_OK, self.text())
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('installer-installed layout', json.dumps(e))
        self.assertFalse([c for c in self.called('provision') if c.startswith(('set-dll', 'set-proxy'))])

    def test_default_inputs_carry_no_installed_key(self):
        from stages import hi_journey
        self.assertNotIn('installed', hi_journey.settings())     # the default inputs hash as before 2604
        self.setenv(C5_RELEASE_HI_INSTALLED='c5h-t')
        self.assertEqual(hi_journey.settings()['installed'], 'c5h-t')


class WholeTierFixture(unittest.TestCase):
    """The stage under 3506's whole-tier fixture (tests/test_validated_tier.py), whose fakes do not cover this stage's
    tools, with start saves present: it fails before any claim and no real tool path is started."""

    def test_no_real_tool_under_the_tier_fixture(self):
        import test_validated_tier as T

        class F(T.Fixture):
            def runTest(self):
                pass
        f = F()
        f.setUp()
        try:
            body = b'a start save'
            (f.tmp / 'hi').mkdir()
            (f.tmp / 'hi' / 's.Civ5Save').write_bytes(body)
            (f.tmp / 'hi' / 'j.jsonl').write_text('{"t": "header"}\n')
            (f.tmp / 'hi' / 'journeys.json').write_text(json.dumps({
                'format': 1, 'saves_dir': str(f.tmp / 'hi'), 'journeys': [
                    {'name': 'j', 'trace': 'j.jsonl', 'mods': 'cp,vp,eui', 'ends': 'menu',
                     'start_save': {'name': 'HI-J', 'file': 's.Civ5Save', 'sha256': sha(body)}}]}))
            f.setenv(C5_RELEASE_HI_SAVES=str(f.tmp / 'hi'), C5_RELEASE_HI_INDEX=str(f.tmp / 'hi' / 'journeys.json'))
            started = []
            real_run = tr.subprocess.run

            def spy(cmd, *a, **k):
                started.append(' '.join(str(c) for c in cmd))
                return real_run(cmd, *a, **k)
            stages = [s for s in tr.tier_stages('validated') if s[0] == 'hi_journey']
            d = tr.Driver(T.TAG, f.work, f.desc, {'tier': 'validated', 'who': 't4506', 'gate_wait': 0}, stages,
                          tools=f.tools, out=f.lines.append)
            tr.subprocess.run = spy
            try:
                rc = d.run(only='hi_journey')
            finally:
                tr.subprocess.run = real_run
            self.assertEqual(rc, tr.EXIT_FAILED, '\n'.join(f.lines))
            e = f.state()['hi_journey']
            self.assertIn('do not name', e['error'])
            for real in ('launch.py', 'guard_check.py', 'set_ini.py', 'cli.py', 'hi.py', 'turns.py', 'dialogwatch.py'):
                self.assertFalse([c for c in started if real in c], real)
            self.assertFalse([c for c in f.called('pool') if c.startswith('claim')])
        finally:
            f.tearDown()


class Units(unittest.TestCase):
    def setUp(self):
        from stages import hi_journey
        self.m = hi_journey
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)

    def tearDown(self):
        self._td.cleanup()

    def test_set_key_keeps_line_endings_and_other_keys(self):
        p = self.tmp / 'u.ini'
        p.write_bytes(USERSET)
        self.assertTrue(self.m.set_key(p, 'SinglePlayerAutoEndTurnEnabled', '0'))
        self.assertEqual(p.read_bytes(), USERSET.replace(b'SinglePlayerAutoEndTurnEnabled = 1',
                                                         b'SinglePlayerAutoEndTurnEnabled = 0'))
        self.assertFalse(self.m.set_key(p, 'NoSuchKey', '1'))
        self.assertEqual(p.read_bytes().count(b'\r\n'), USERSET.count(b'\r\n'))

    def test_set_key_ignores_comments(self):
        p = self.tmp / 'g.ini'
        p.write_bytes(b'; WindowResX = 1\nWindowResX = 2560\n')
        self.assertTrue(self.m.set_key(p, 'WindowResX', '1920'))
        self.assertEqual(p.read_bytes(), b'; WindowResX = 1\nWindowResX = 1920\n')

    def test_index_refusals(self):
        def write(js):
            p = self.tmp / 'journeys.json'
            p.write_text(json.dumps({'format': 1, 'journeys': js}))
            return p
        good = {'name': 'a', 'trace': 'a.jsonl', 'mods': 'cp,vp', 'ends': 'menu',
                'start_save': {'name': 'HI-A', 'file': 'a.Civ5Save', 'sha256': 'ab'}}
        self.assertEqual(self.m.load_index(write([good]))['journeys'][0]['name'], 'a')
        for bad, word in (({'ends': 'quit'}, 'ends'), ({'mods': ''}, 'mods'),
                          ({'start_save': {'name': 'X-A', 'file': 'f', 'sha256': 'ab'}}, 'HI-'),
                          ({'made_saves': ['Autosave']}, 'HI-'), ({'start_save': {'name': 'HI-A'}}, 'sha256'),
                          ({'known_flakes': [{'step': '10', 'exit': 5, 'cond': 'x', 'why': 'y'}]}, 'known flake')):
            with self.assertRaises(self.m.IndexError_) as cm:
                self.m.load_index(write([dict(good, **bad)]))
            self.assertIn(word, str(cm.exception))
        with self.assertRaises(self.m.IndexError_):
            self.m.load_index(write([]))
        with self.assertRaises(self.m.IndexError_):
            self.m.load_index(self.tmp / 'missing.json')

    def test_only_listed_journeys_other_files_of_the_folder_ignored(self):
        """4509's real-input journey (focus-window.jsonl, another format) lives in the same folder: the stage takes
        only what the index names, never a glob of the folder."""
        (self.tmp / 'focus-window.jsonl').write_text('{"not": "a hi trace"}\n')
        (self.tmp / 'a.jsonl').write_text('{"t": "header"}\n')
        p = self.tmp / 'journeys.json'
        p.write_text(json.dumps({'format': 1, 'journeys': [
            {'name': 'a', 'trace': 'a.jsonl', 'mods': 'cp,vp', 'ends': 'menu',
             'start_save': {'name': 'HI-A', 'file': 'a.Civ5Save', 'sha256': 'ab'}}]}))
        idx = self.m.load_index(p)
        self.assertEqual([Path(j['trace_path']).name for j in idx['journeys']], ['a.jsonl'])
        os.environ['C5_RELEASE_HI_INDEX'] = str(p)
        try:
            ins = self.m.inputs(None)
        finally:
            os.environ.pop('C5_RELEASE_HI_INDEX', None)
        self.assertEqual(list(ins['traces']), ['a'])
        # the committed index names exactly the two release journeys of each major-civilisation count (ticket 0580:
        # 22 = release-basic/-large, 43 = their -m43 copies; ticket 0574 corrected this check after 0580's merge)
        real = self.m.load_index()
        by_major = {}
        for j in real['journeys']:
            by_major.setdefault(int(j.get('major_civs', 22)), []).append(j['name'])
        self.assertEqual(by_major, {22: ['release-basic', 'release-large'],
                                    43: ['release-basic-m43', 'release-large-m43']})
        self.assertFalse(any('focus-window' in j['trace'] for j in real['journeys']))

    def test_place_start_refuses_a_changed_save(self):
        saves, user = self.tmp / 'saves', self.tmp / 'user'
        saves.mkdir()
        (saves / 's.Civ5Save').write_bytes(b'one')
        j = {'start_save': {'name': 'HI-S', 'file': 's.Civ5Save', 'sha256': sha(b'two')}}
        with self.assertRaises(FileNotFoundError):
            self.m.place_start(user, j, saves)
        j['start_save']['sha256'] = sha(b'one')
        dst = self.m.place_start(user, j, saves)
        self.assertEqual(dst.name, 'HI-S.Civ5Save')

    def test_verdicts(self):
        ok = {'ran': True, 'exit': 0, 'checkpoints': [{'name': 'a', 'ok': True}]}
        bad = {'ran': True, 'exit': 0, 'checkpoints': [{'name': 'a', 'ok': False}]}
        self.assertEqual(self.m.journey_verdict({'product': ok, 'stock': ok}, ['product', 'stock']), 'passed')
        self.assertEqual(self.m.journey_verdict({'product': ok, 'stock': bad}, ['product', 'stock']), 'failed')
        self.assertEqual(self.m.journey_verdict({'product': ok, 'stock': {'ran': False}}, ['product', 'stock']),
                         'incomplete')
        self.assertEqual(self.m.overall({'a': {'verdict': 'passed'}, 'b': {'verdict': 'failed'}}), 'failed')
        self.assertEqual(self.m.overall({'a': {'verdict': 'passed'}, 'b': {'verdict': 'incomplete'}}), 'incomplete')

    def test_the_tier_verdict_is_not_gated_by_a_failed_journey(self):
        """The validated tier's verdict (stages/report.py) with every stage ok: a hi_journey that is ok with verdict
        failed, or skipped, leaves it 'validated' (the stage reports; the owner decides whether it gates)."""
        from stages import report as R
        from stages import validated as V
        names = [n for n, _c, _o in tr.CANDIDATE + V.STAGES]
        self.assertIn('hi_journey', names)
        self.assertLess(names.index('wholegames'), names.index('hi_journey'))
        self.assertLess(names.index('hi_journey'), names.index('report'))
        for hj in ({'status': 'ok', 'outputs': {'verdict': 'failed'}}, {'status': 'skipped', 'outputs': {}}):
            st = {n: {'status': 'ok', 'outputs': {}} for n in names}
            st['wholegames']['outputs'] = {'parity': 'passed'}
            st['hi_journey'] = hj
            self.assertEqual(R.verdict(st), 'validated')


class TierPath(unittest.TestCase):
    """Ticket 0671: with the default tools and no by-hand variable the validated plan's stage hands the journeys to the
    tier stage (the installer-laid product route, with the composite); otherwise the stage-laid path as before."""

    VARS = ('C5_RELEASE_HI_INSTALLED', 'C5_RELEASE_HI_ROUTES', 'C5_RELEASE_HI_STAGE_LAID')

    def setUp(self):
        self.m = tr.load_stage_module('hi_journey', tr.STAGES_DIR)
        self.old = {k: os.environ.pop(k, None) for k in self.VARS}
        self.said = []
        driver = type('D', (), {'tools': tr.default_tools()})()
        self.ctx = type('C', (), {'_d': driver, 'tag': TAG, 'stage': 'hi_journey', 'work': Path('.'),
                                  'say': lambda s, t: self.said.append(t)})()

    def tearDown(self):
        for k, v in self.old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    def test_the_tier_path_is_the_default_of_a_real_take(self):
        self.assertIsNone(self.m.installed_path_why_not(self.ctx))
        for k, v, word in (('C5_RELEASE_HI_INSTALLED', 'c5h-x', '2604'), ('C5_RELEASE_HI_ROUTES', 'stock', 'by-hand'),
                           ('C5_RELEASE_HI_STAGE_LAID', '1', 'diagnostic')):
            os.environ[k] = v
            self.assertIn(word, self.m.installed_path_why_not(self.ctx))
            del os.environ[k]
        os.environ['C5_RELEASE_HI_STAGE_LAID'] = '0'
        self.assertIsNone(self.m.installed_path_why_not(self.ctx))
        fake = type('D', (), {'tools': dict(tr.default_tools(), pool=['fake-pool'])})()
        self.ctx._d = fake
        self.assertIn('not the defaults', self.m.installed_path_why_not(self.ctx))

    def test_the_tier_stages_own_copy_never_hands_on(self):
        import types
        copy = types.ModuleType('take_release_original_hi_journey_0671test')
        copy.__file__ = str(tr.STAGES_DIR / 'hi_journey.py')
        exec(compile((tr.STAGES_DIR / 'hi_journey.py').read_text(encoding='utf-8'), copy.__file__, 'exec'),
             copy.__dict__)
        self.assertIn('own copy', copy.installed_path_why_not(self.ctx))

    def test_run_hands_the_stage_to_the_tier_and_names_take_release_in_a_stop(self):
        calls = []

        class T:
            @staticmethod
            def _run(ctx):
                calls.append('tier _run')
                return tr.StageResult.stop('D162: ...', f'run take_tier.py {TAG} --tier validated --for-owner again',
                                           verdict='failed')

            @staticmethod
            def gate(ctx, res):
                calls.append('tier gate')
                return res
        self.m.tier_module = lambda: T
        self.m._run = lambda ctx: self.fail('the stage-laid path ran')
        res = self.m.run(self.ctx)
        self.assertEqual(calls, ['tier _run', 'tier gate'])
        self.assertEqual(res.status, 'stop')
        self.assertIn(f'take_release.py {TAG} --tier validated', res.unblock)
        self.assertNotIn('take_tier.py', res.unblock)
        self.assertIn('0671', res.outputs['played_by'])
        self.assertTrue(any('tier\'s way' in t for t in self.said))

    def test_inputs_name_the_path(self):
        self.m.tier_module = lambda: type('T', (), {'inputs': staticmethod(lambda ctx: {'harness': 'h'})})
        self.assertEqual(self.m.inputs(self.ctx), {'harness': 'h', 'path': 'tier stage (0671)'})
        os.environ['C5_RELEASE_HI_STAGE_LAID'] = '1'
        self.assertNotIn('path', self.m.inputs(self.ctx))

    def test_the_real_tier_module_loads_and_its_original_plays_what_it_is_given(self):
        T = self.m.tier_module()
        self.assertTrue(hasattr(T, '_run') and hasattr(T, 'gate') and hasattr(T, 'inputs'))
        self.assertIsNot(T.O, self.m)
        self.assertIsNotNone(T.O.installed_path_why_not(self.ctx))


if __name__ == '__main__':
    unittest.main()
