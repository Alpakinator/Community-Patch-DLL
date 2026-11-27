"""Hermetic tests of the tier stage `sweep` (epic 0630, ticket 5020): tools/release/tiers/stages/sweep.py run by the
tier driver against a fake instance and fake tools (pool, provision, the installer and its runner, treehash, the
harness's cli and dialogwatch, and sweep.py: one stand-in script in the test's temporary folder), with the REAL
tools/hi/phase/phase.py (plan --tier intermediate, record, verdict) over a fake kit. No pool, gate, lock, installer,
game or instance is touched; nothing is written outside a temporary folder.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_tier_sweep.py
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
sys.path.insert(0, str(HERE.parent / 'tiers'))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
import tierlib  # noqa: E402

TIER_STAGES = HERE.parent / 'tiers' / 'stages'
TAG = 'Release-9.9.9'
USER = "Documents/My Games/Sid Meier's Civilization 5"
SAVE = 'HI-KIT-MEDIEVAL-LARGE-T200'
O = tierlib.original('hi_journey')
CP = 'MODS/(1) Community Patch'

FAKE = r'''
import hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path
name, args = sys.argv[1], sys.argv[2:]
with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as f:
    f.write(name + ' ' + ' '.join(args) + '\n')
FD = Path(os.environ['FAKE_DIR'])
CP = 'MODS/(1) Community Patch'
def env(k):
    return os.environ.get(k, '')
def opt(flag):
    return args[args.index(flag) + 1] if flag in args else None
def sha(b):
    return hashlib.sha256(b).hexdigest()
if name == 'quietlock':
    sys.exit(subprocess.call(args[args.index('--') + 1:]) if args[0] == 'run' else 0)
if name == 'pool':
    if args[0] == 'claim':
        n = opt('--name') or 'c5h-t'
        print(n); print(FD / 'instances'); print('claimed by %s until later (short run)' % opt('--owner'))
        sys.exit(0)
    print(json.dumps({'released': args[1]})); sys.exit(0)
if name == 'provision':
    if args[0] == 'verify':
        sys.exit(int(env('FAKE_VERIFY_RC') or 0))
    if args[0] == 'set-dll' and '--dll' in args:
        m = json.loads((FD / 'instances' / args[1] / 'instance.json').read_text())
        t = FD / 'instances' / args[1] / m['dll']['path'].replace(chr(92), '/')
        t.write_bytes(Path(opt('--dll')).read_bytes())
    sys.exit(0)
if name == 'build_installer':
    o = Path(opt('--out')); o.mkdir(parents=True, exist_ok=True)
    exe = o / 'VP64-Patch-x-TEST.exe'; exe.write_bytes(b'installer')
    (o / 'build-record.json').write_text(json.dumps({'exe': {'file': exe.name, 'sha256': sha(b'installer')}}))
    sys.exit(0)
if name == 'run_installer':
    d = Path(opt('--dir')); act = opt('--action'); cp = d / CP
    Path(opt('--log')).write_text('P64: ' + act)
    if env('FAKE_INSTALLER_REFUSES_RUNNING') and (FD / 'game-running').exists():
        print('REFUSED: Civilization V is running')   # the real installer's refusal (5024's live sweep, 2026-11-22)
        sys.exit(7)
    if act == 'install':
        if env('FAKE_INSTALL_RC'):
            sys.exit(int(env('FAKE_INSTALL_RC')))
        player = (cp / 'CvGameCore_Expansion2.dll').read_bytes()
        (cp / 'CvGameCore_Expansion2_x86.dll').write_bytes(player)
        (cp / 'CvGameCore_Expansion2.dll').write_bytes(b'proxy')
        (cp / 'VP64').mkdir(parents=True, exist_ok=True)
        (cp / 'VP64' / 'CvGameCore_Expansion2_x64.dll').write_bytes(b'x64')
        (cp / 'VP64' / 'server.exe').write_bytes(b'srv' if not env('FAKE_INSTALL_WRONG') else b'not the server')
        (cp / 'vpproxy.ini').write_text('[proxy]\nmode=on\n')
        rec = d / 'VP64' / 'install' / 'record.txt'; rec.parent.mkdir(parents=True, exist_ok=True)
        rec.write_text('VP64-INSTALL-RECORD\t1\nrelease\tR\nvariant\tstd\nconfig\ti2\nform\tmods\n'
                       'cpdir\tMG\\MODS\\(1) Community Patch\nend\t1\n', encoding='utf-8-sig')
        (d / 'Logs').mkdir(exist_ok=True); (d / 'Logs' / 'proxy.log').write_text('run output')
        sys.exit(0)
    if env('FAKE_REMOVE_RC'):
        sys.exit(int(env('FAKE_REMOVE_RC')))
    (cp / 'CvGameCore_Expansion2.dll').write_bytes((cp / 'CvGameCore_Expansion2_x86.dll').read_bytes())
    (cp / 'CvGameCore_Expansion2_x86.dll').unlink(); (cp / 'vpproxy.ini').unlink()
    shutil.rmtree(cp / 'VP64')
    if not env('FAKE_REMOVE_LEAVES'):
        shutil.rmtree(d / 'VP64')
    sys.exit(0)
if name == 'treehash':
    root, out = Path(args[1]), Path(args[2])
    rows = []
    for p in sorted(root.rglob('*')):
        r = str(p.relative_to(root)).replace('/', '\\')
        rows.append(('D', '-', '-', r + '\\') if p.is_dir() else ('F', sha(p.read_bytes()), str(p.stat().st_size), r))
    out.write_text(''.join('\t'.join(x) + '\n' for x in rows), encoding='utf-8')
    sys.exit(0)
if name == 'dialogwatch':
    sys.exit(8 if (FD / 'box-open').exists() else 0)
if name == 'cli':
    if 'status' in args:
        print(json.dumps({'running': (FD / 'game-running').exists()}))
    if 'stop' in args:
        (FD / 'game-running').unlink(missing_ok=True)
    sys.exit(0)
if name == 'sweep':
    if args[0] == 'compare':
        o = Path(opt('--out')); o.mkdir(parents=True, exist_ok=True)
        cls = env('FAKE_COMPARE_CLASS') or 'stock too'
        sigs = [] if env('FAKE_COMPARE_NAMES_NONE') else ['process_ended-step1']
        (o / 'compare.json').write_text(json.dumps({'rows': [{'control': '/InGame/X', 'mark': 'left',
                                                              'product': 'process_ended', 'class': cls,
                                                              'findings': sigs}]}))
        print(json.dumps({'written': str(o / 'compare.json')})); sys.exit(0)
    inst, route, save, out = opt('-i'), opt('--route'), opt('--save'), Path(opt('--out'))
    m = json.loads((FD / 'instances' / inst / 'instance.json').read_text())
    user = FD / 'instances' / inst / m['user_dir'].replace(chr(92), '/')
    placed = user / 'ModdedSaves' / 'single' / (save + '.Civ5Save')
    dll = (FD / 'instances' / inst / m['dll']['path'].replace(chr(92), '/')).read_bytes()
    with open(FD / 'sweeps.txt', 'a') as f:
        f.write(json.dumps({'route': route, 'dll': dll.decode('latin-1'), 'placed': placed.is_file(),
                            'installed': (user / 'VP64' / 'install' / 'record.txt').is_file()}) + '\n')
    if env('FAKE_SWEEP_NOTHING'):
        sys.exit(1)
    stopped = None
    if env('FAKE_TAKE_STOP') and route == 'product':
        # an operator creates the take's stop file while the sweep runs; the sweeper reads only <out>/STOP
        Path(env('FAKE_TAKE_STOP')).write_text('stop')
        end = time.time() + 20
        while not (out / 'STOP').exists() and time.time() < end:
            time.sleep(0.05)
        stopped = {'why': 'STOP file'} if (out / 'STOP').exists() else None
        with open(FD / 'sweeps.txt', 'a') as f:
            f.write(json.dumps({'route': route, 'saw_stop': bool(stopped)}) + '\n')
    (out / 's01').mkdir(parents=True, exist_ok=True)
    recs = [{'t': 'header', 'ms': 0, 'hi': '1', 'ticket': opt('--ticket'), 'by': opt('--by'), 'instance': inst,
             'route': route, 'pure': False, 'width': 1280, 'client': [1914, 1051], 'host': 'workstation',
             'save': {'name': save, 'sha256': sha(placed.read_bytes())},
             'build': {'exe_sha256': 'e' * 64, 'dll_sha256': sha(dll), 'server_sha256': None},
             'started': '2026-11-22T15:00:00-05:00'},
            {'t': 'step', 'ms': 100, 'n': 1, 'actions': [{'a': 'key', 'spec': 'ESC', 'ok': True}], 'wait': None,
             'shot': {'file': None, 'sha256': None, 'source': 'printwindow'}, 'judge': {'turn': 200}, 'exit': 0}]
    if route == 'product' and env('FAKE_SWEEP_FINDING'):
        recs.append({'t': 'finding', 'ms': 150, 'kind': 'process_ended', 'step': 1, 'what': None, 'where': None,
                     'signature': 'process_ended-step1', 'text': 'the game ended', 'route': route})
    recs.append({'t': 'end', 'ms': 900, 'exit': 0, 'steps': 1})
    (out / 's01' / 'trace.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in recs))
    (out / 'sweep.json').write_text(json.dumps({'sweep': '1', 'route': route, 'save': save,
                                                'ended': None if stopped else '2026-11-22T15:50:00-05:00',
                                                'stopped': stopped,
                                                'sessions': [{'n': 1, 'trace': str(out / 's01' / 'trace.jsonl')}],
                                                'findings': [], 'screens': {}}))
    if not env('FAKE_BOX_NO_GAME'):
        (FD / 'game-running').write_text('1')        # the sweeper leaves the game running at its end
    if env('FAKE_BOX') and route == 'product':
        (FD / 'box-open').write_text('1')
    print(json.dumps({'exit': 1 if stopped else 0, 'out': str(out)}))
    sys.exit(1 if stopped else 0)
if name == 'wrapper':
    print(json.dumps({'result': 'updated'}, indent=1))
    sys.exit(int(env('FAKE_WRAPPER_RC') or 0))
sys.exit(0)
'''


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Base(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.calls = self.tmp / 'calls.txt'
        fake = self.tmp / 'fake.py'
        fake.write_text(FAKE)
        names = set(tr.default_tools()) | {'pool', 'provision', 'build_installer', 'run_installer', 'treehash', 'cli',
                                           'dialogwatch', 'sweep', 'wrapper'}
        self.tools = {n: [sys.executable, '-I', str(fake), n] for n in names}
        self.tools['phase'] = [sys.executable, '-I', str(REPO / 'tools' / 'hi' / 'phase' / 'phase.py')]
        self.lines = []
        keys = ('FAKE_CALLS', 'FAKE_DIR', 'FAKE_INSTALL_RC', 'FAKE_INSTALL_WRONG', 'FAKE_REMOVE_LEAVES',
                'FAKE_REMOVE_RC', 'FAKE_VERIFY_RC', 'FAKE_SWEEP_FINDING', 'FAKE_SWEEP_NOTHING', 'FAKE_BOX',
                'FAKE_COMPARE_CLASS', 'FAKE_BOX_NO_GAME', 'FAKE_COMPARE_NAMES_NONE', 'FAKE_TAKE_STOP',
                'FAKE_WRAPPER_RC', 'C5_RELEASE_SWEEP_STOP_POLL_S',
                'C5_RELEASE_SWEEP_KIT', 'C5_RELEASE_SWEEP_KEPT', 'C5_RELEASE_SWEEP_INSTANCE',
                'C5_RELEASE_SWEEP_EXE_BUILD', 'C5_RELEASE_SWEEP_BY', 'C5_RELEASE_SWEEP_TIMEOUT_MIN',
                'C5_RELEASE_HI_INSTANCE', 'C5_RELEASE_HI_WAIT', 'C5_RELEASE_HI_TICKET', 'C5_RELEASE_HI_BY',
                'C5_RELEASE_INSTANCE', 'C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE', 'C5_POOL_OWNER')
        self._env = {k: os.environ.get(k) for k in keys}
        for k in keys:
            os.environ.pop(k, None)
        self.make_kit()
        os.environ.update(FAKE_CALLS=str(self.calls), FAKE_DIR=str(self.tmp), C5_RELEASE_HI_WAIT='0',
                          C5_RELEASE_SWEEP_KIT=str(self.kit), C5_RELEASE_SWEEP_KEPT=str(self.kept),
                          C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'))
        self._retry = O.CLAIM_RETRY_S
        O.CLAIM_RETRY_S = 0
        self.make_instance()
        self.make_build()
        self.seed_state()

    def tearDown(self):
        O.CLAIM_RETRY_S = self._retry
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    # --- fixtures
    def make_kit(self, traces=()):
        self.save = self.tmp / 'kit' / (SAVE + '.Civ5Save')
        self.save.parent.mkdir(parents=True, exist_ok=True)
        self.save.write_bytes(b'the medieval save')
        self.kit = self.tmp / 'kit' / 'kit.json'
        self.kit.write_text(json.dumps({'format': 1, 'situations': {'era-medieval': {
            'save': SAVE, 'path': str(self.save), 'sha256': sha(b'the medieval save')}}}))
        self.kept = self.tmp / 'kept'
        self.kept.mkdir(exist_ok=True)
        (self.kept / 'index.json').write_text(json.dumps({'format': 1, 'traces': list(traces)}))

    def make_instance(self, build='26577625', profile='mods548'):
        inst = self.tmp / 'instances' / 'c5h-t'
        u = inst / USER
        (u / CP).mkdir(parents=True, exist_ok=True)
        (u / CP / 'CvGameCore_Expansion2.dll').write_bytes(b'vp dll')
        (inst / 'install').mkdir(exist_ok=True)
        (inst / 'instance.json').write_text(json.dumps({
            'name': 'c5h-t', 'user_dir': USER.replace('/', '\\'), 'install_dir': 'install', 'profile': profile,
            'dll': {'path': USER + '/' + CP + '/CvGameCore_Expansion2.dll'},
            'exe_build': {'build': build, 'stamp': '0x6AF3837D', 'size': 15466264}}))
        self.inst, self.user = inst, u

    def make_build(self, ia=True):
        b = self.tmp / 'layout' / 'x64'
        (b / 'bin').mkdir(parents=True, exist_ok=True)
        (b / 'proxy' / 'bin').mkdir(parents=True, exist_ok=True)
        for rel, body in (('bin/CvGameCore_Expansion2.dll', b'x64'), ('bin/server.exe', b'srv'),
                          ('proxy/bin/CvGameCore_Expansion2.dll', b'proxy')):
            (b / rel).write_bytes(body)
        self.build = {'x64_commit': 'a' * 40, 'i0_commit': 'b' * 40, 'x64_layout': str(b),
                      'proxy_dll': str(b / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll')}
        if ia:
            (self.tmp / 'ia.dll').write_bytes(b'ia dll')
            self.build['i0ia_dll'] = str(self.tmp / 'ia.dll')
        pl = self.work / 'inputs' / 'player-x86'
        pl.mkdir(parents=True, exist_ok=True)
        (pl / 'CvGameCore_Expansion2.dll').write_bytes(b'player dll')

    def seed_state(self):
        st = {'format': tr.FORMAT, 'tag': TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': [
            {'name': 'build', 'cl_step': '6', 'status': 'ok', 'outputs': self.build, 'inputs_sha': 'seed'},
            {'name': 'pack', 'cl_step': '-', 'status': 'ok', 'outputs': {}, 'inputs_sha': 'seed'}]}
        self.work.mkdir(parents=True, exist_ok=True)
        (self.work / 'state.json').write_text(json.dumps(st))

    # --- running
    def go(self, tools=None):
        cfg = {'tier': 'intermediate', 'who': 'test5020', 'gate_wait': 0, 'tier_stages_dir': str(TIER_STAGES)}
        stages = [('build', '6', '5012'), ('pack', '-', '3504'), ('sweep', '-', '5020')]
        d = tt.TierDriver(TAG, self.work, object(), cfg, stages, tools=tools or self.tools, out=self.lines.append)
        return d.run(only='sweep')

    def entry(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}['sweep']

    def called(self, name):
        if not self.calls.exists():
            return []
        return [ln[len(name) + 1:] for ln in self.calls.read_text().splitlines() if ln.startswith(name + ' ')]

    def sweeps(self):
        p = self.tmp / 'sweeps.txt'
        return [json.loads(ln) for ln in p.read_text().splitlines()] if p.exists() else []

    def dll_now(self):
        return (self.user / CP / 'CvGameCore_Expansion2.dll').read_bytes()


class Pass(Base):
    def test_a_clean_product_sweep_passes_and_the_instance_is_as_before(self):
        rc = self.go()
        e = self.entry()
        self.assertEqual(rc, tr.EXIT_OK, self.lines[-6:])
        self.assertEqual(e['status'], 'ok', e.get('error'))
        o = e['outputs']
        self.assertEqual(o['verdict']['result'], 'PASS')
        self.assertEqual(o['verdict']['clauses_off'], [3, 4])
        self.assertEqual(o['item'], 'sweep:era-medieval:product')
        # one sweep, product, on the installed layout, the kit save placed; no stock sweep
        self.assertEqual([(s['route'], s['dll'], s['placed'], s['installed']) for s in self.sweeps()],
                         [('product', 'proxy', True, True)])
        self.assertNotIn('stock', o)
        sw = self.called('sweep')[0]
        for part in ('-i c5h-t', '--by test5020-sweep', 'run --ticket', '--route product', f'--save {SAVE}',
                     '--launch'):
            self.assertIn(part, sw)
        # recorded in the tier plan's run book with the take's tag as the build
        book = json.loads((self.work / 'sweep' / 'phase' / 'runbook.json').read_text())['runs']
        self.assertEqual([(r['item'], r['build'], r['status']) for r in book],
                         [('sweep:era-medieval:product', TAG, 'completed')])
        # the removal: installer removed, tree equal, the DLL before back by hash, save gone, game stopped, released
        self.assertTrue(o['removal']['ok'], o['removal'])
        self.assertEqual(self.dll_now(), b'vp dll')
        self.assertFalse((self.user / 'ModdedSaves' / 'single' / (SAVE + '.Civ5Save')).exists())
        self.assertFalse((self.tmp / 'game-running').exists())
        self.assertTrue(any(c.startswith('release c5h-t --owner test5020-sweep') for c in self.called('pool')))
        self.assertTrue(any('stop' in c for c in self.called('cli')))
        self.assertEqual(sorted(o['steps']), sorted(['plan', 'installer', 'claim', 'player_state', 'install',
                                                     'product', 'verdict', 'removal']))

    def test_a_save_already_there_is_put_back_by_hash(self):
        p = self.user / 'ModdedSaves' / 'single' / (SAVE + '.Civ5Save')
        p.parent.mkdir(parents=True)
        p.write_bytes(b'an older copy')
        self.assertEqual(self.go(), tr.EXIT_OK, self.lines[-6:])
        self.assertEqual(p.read_bytes(), b'an older copy')

    def test_an_import_wrapper_on_the_instance_is_brought_up_to_date_before_the_sweep(self):
        (self.inst / 'install' / 'c5_wrapper.json').write_text('{}')
        self.assertEqual(self.go(), tr.EXIT_OK, self.lines[-6:])
        self.assertEqual(self.called('wrapper'), [f'install {self.inst / "install"}'])
        w = self.entry()['outputs']['wrapper']
        self.assertEqual((w['action'], w['result']), ('wrapper.py install', 'updated'))
        calls = self.calls.read_text().splitlines()
        self.assertLess(next(i for i, c in enumerate(calls) if c.startswith('wrapper ')),
                        next(i for i, c in enumerate(calls) if c.startswith('sweep ')))

    def test_without_an_import_wrapper_nothing_is_installed(self):
        self.assertEqual(self.go(), tr.EXIT_OK, self.lines[-6:])
        self.assertEqual(self.called('wrapper'), [])
        self.assertIn('route "early"', self.entry()['outputs']['wrapper']['action'])

    def test_inputs_unchanged_the_sweep_is_not_run_again(self):
        self.assertEqual(self.go(), tr.EXIT_OK)
        n = len(self.sweeps())
        d = tt.TierDriver(TAG, self.work, object(), {'tier': 'intermediate', 'who': 'test5020', 'gate_wait': 0,
                                                     'tier_stages_dir': str(TIER_STAGES)},
                          [('sweep', '-', '5020')], tools=self.tools, out=self.lines.append)
        self.assertEqual(d.run(), tr.EXIT_OK, self.lines[-4:])
        self.assertTrue(any('sweep ok (inputs unchanged), not run again' in ln for ln in self.lines))
        self.assertEqual(len(self.sweeps()), n)


class Fail(Base):
    def test_a_patch_only_fault_fails_after_the_stock_sweep_for_comparison(self):
        os.environ.update(FAKE_SWEEP_FINDING='1', FAKE_COMPARE_CLASS='patch only')
        rc = self.go()
        e = self.entry()
        self.assertEqual(rc, tr.EXIT_FAILED)
        o = e['outputs']
        self.assertEqual(o['verdict']['result'], 'FAIL')
        self.assertEqual(o['verdict']['failing_clauses'], [2])
        # stock after the removal, on the instance-aware DLL, no installer layout; then the DLL before back
        self.assertEqual([(s['route'], s['dll'], s['installed']) for s in self.sweeps()],
                         [('product', 'proxy', True), ('stock', 'ia dll', False)])
        self.assertEqual(o['stock']['mode'], 'fresh')
        self.assertEqual(o['stock']['restore_errors'], [])
        self.assertEqual(o['compare']['counts'], {'patch only': 1})
        self.assertEqual(o['verdict_with_compare']['result'], 'FAIL')
        self.assertIn('the stock sweep for comparison: patch only 1', e['error'])
        self.assertIn('verdict is FAIL', e['error'])
        self.assertEqual(self.dll_now(), b'vp dll')
        self.assertEqual(len([c for c in self.called('pool') if c.startswith('release')]), 2)

    def test_findings_all_stock_too_pass_with_the_comparison(self):
        os.environ['FAKE_SWEEP_FINDING'] = '1'           # the comparison classes it "stock too"
        rc = self.go()
        e = self.entry()
        self.assertEqual(rc, tr.EXIT_OK, self.lines[-4:])
        o = e['outputs']
        self.assertEqual(o['verdict']['result'], 'FAIL')                  # the product sweep alone
        self.assertEqual(o['verdict_with_compare']['result'], 'PASS')     # with the comparison
        self.assertEqual(len(o['stock_too']), 1)
        self.assertIn('process_ended-step1', o['stock_too'][0])
        v = json.loads((self.work / 'sweep' / 'phase' / 'verdict.json').read_text())
        self.assertTrue(any('stock too' in n for n in v['clauses'][1]['notes']))
        self.assertEqual(self.dll_now(), b'vp dll')

    def test_a_finding_the_comparison_does_not_name_still_fails(self):
        os.environ.update(FAKE_SWEEP_FINDING='1', FAKE_COMPARE_NAMES_NONE='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertEqual(self.entry()['outputs']['verdict_with_compare']['failing_clauses'], [2])

    def test_stock_not_run_without_an_instance_aware_dll(self):
        os.environ['FAKE_SWEEP_FINDING'] = '1'
        self.make_build(ia=False)
        self.seed_state()
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertEqual(e['outputs']['stock']['mode'], 'not run')
        self.assertIn('i0ia_dll', e['error'])
        self.assertEqual([s['route'] for s in self.sweeps()], ['product'])

    def test_a_sweep_that_wrote_nothing_fails_without_a_stock_sweep(self):
        os.environ['FAKE_SWEEP_NOTHING'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('record refused', e['error'])
        self.assertEqual(e['outputs']['verdict']['failing_clauses'], [1])
        self.assertNotIn('stock', e['outputs'])
        self.assertTrue(e['outputs']['removal']['ok'])

    def test_a_box_keeps_the_claim_and_removes_nothing(self):
        os.environ['FAKE_BOX'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('a box is open', e['error'])
        self.assertIn('REMOVAL ON c5h-t FAILED', e['error'])
        self.assertFalse(any('--action remove' in c for c in self.called('run_installer')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertNotIn('record', e['outputs'])

    def test_a_box_without_a_running_game_keeps_the_claim_too(self):
        # a crash box whose game process is gone: the harness's status says not running, the marker says a box
        os.environ.update(FAKE_BOX='1', FAKE_BOX_NO_GAME='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('a box is open', e['error'])
        self.assertFalse(any('--action remove' in c for c in self.called('run_installer')))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))

    def test_a_wrapper_that_cannot_be_updated_fails_before_the_sweep(self):
        (self.inst / 'install' / 'c5_wrapper.json').write_text('{}')
        os.environ['FAKE_WRAPPER_RC'] = '2'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('import wrapper', e['error'])
        self.assertEqual(self.sweeps(), [])
        self.assertTrue(e['outputs']['removal']['ok'])

    def test_the_takes_stop_file_reaches_the_sweeper_and_ends_the_take_with_a_stop(self):
        os.environ.update(FAKE_TAKE_STOP=str(self.work / 'STOP'), C5_RELEASE_SWEEP_STOP_POLL_S='0.2')
        rc = self.go()
        self.assertEqual(rc, tr.EXIT_STOP, self.lines[-4:])
        self.assertEqual(self.sweeps()[-1], {'route': 'product', 'saw_stop': True})   # <out>/STOP was copied
        e = self.entry()
        self.assertEqual(e['status'], 'pending')
        self.assertFalse((self.work / 'sweep' / 'phase' / 'runbook.json').exists())  # nothing recorded
        self.assertTrue(any('--action remove' in c for c in self.called('run_installer')))   # removal ran
        self.assertEqual(self.dll_now(), b'vp dll')
        self.assertTrue(any('stop file' in ln and ln.startswith('take_release: STOP sweep') for ln in self.lines))

    def test_a_stop_ends_the_sweepers_game_before_the_removal(self):
        # 5024's live sweep (2026-11-22 20:43): the sweeper ended on the STOP with its game still running, the stage's
        # own stop was skipped (ctx.run raised StopRequested) and the installer refused the removal. With an installer
        # that refuses while the game runs, the removal must still pass and the game be gone.
        os.environ.update(FAKE_TAKE_STOP=str(self.work / 'STOP'), C5_RELEASE_SWEEP_STOP_POLL_S='0.2',
                          FAKE_INSTALLER_REFUSES_RUNNING='1')
        self.assertEqual(self.go(), tr.EXIT_STOP, self.lines[-4:])   # a stop writes no outputs: read the calls
        self.assertFalse((self.tmp / 'game-running').exists())       # the game was stopped ...
        self.assertTrue(any(' stop ' in f' {c} ' for c in self.called('cli')), self.called('cli'))
        self.assertTrue(any('--action remove' in c for c in self.called('run_installer')))   # ... before the removal
        self.assertFalse(any('removal' in ln and 'FAILED' in ln for ln in self.lines), self.lines[-6:])
        self.assertEqual(self.dll_now(), b'vp dll')

    def test_install_fails_no_sweep(self):
        os.environ['FAKE_INSTALL_RC'] = '5'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('install', e['error'])
        self.assertEqual(self.sweeps(), [])
        self.assertEqual(self.dll_now(), b'vp dll')

    def test_a_layout_that_is_not_the_builds_is_not_swept_and_is_removed(self):
        os.environ['FAKE_INSTALL_WRONG'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('not the build\'s', e['error'])
        self.assertIn('server', e['error'])
        self.assertEqual(self.sweeps(), [])
        self.assertTrue(any('--action remove' in c for c in self.called('run_installer')))
        self.assertTrue(e['outputs']['removal']['ok'])

    def test_a_removal_that_leaves_files_fails_the_stage(self):
        os.environ['FAKE_REMOVE_LEAVES'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('REMOVAL ON c5h-t FAILED', e['error'])
        self.assertIn('record.txt', e['error'])


class Refused(Base):
    def test_the_save_missing_is_a_stop_and_nothing_is_claimed(self):
        self.save.unlink()
        self.assertEqual(self.go(), tr.EXIT_STOP)
        e = self.entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('no sweep, no pass', e['stop']['what'])
        self.assertEqual(self.called('pool'), [])

    def test_the_save_with_other_bytes_is_a_stop(self):
        self.save.write_bytes(b'another save')
        self.assertEqual(self.go(), tr.EXIT_STOP)
        self.assertIn('not the kit\'s bytes', self.entry()['stop']['what'])

    def test_another_exe_build_is_released_and_a_stop(self):
        self.make_instance(build='4390913')
        self.assertEqual(self.go(), tr.EXIT_STOP)
        e = self.entry()
        self.assertIn('EXE build 4390913', e['stop']['what'])
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))
        self.assertEqual(self.sweeps(), [])

    def test_a_modpack_instance_is_a_stop(self):
        self.make_instance(profile='modpack548')
        self.assertEqual(self.go(), tr.EXIT_STOP)
        self.assertIn('modpack548', self.entry()['stop']['what'])

    def test_an_active_kept_trace_fails_before_any_claim(self):
        self.make_kit([{'file': 'city-screen-1.jsonl', 'charter': 'city-screen', 'status': 'active'}])
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertIn('kept:city-screen-1.jsonl', self.entry()['error'])
        self.assertEqual(self.called('pool'), [])

    def test_custom_tools_must_name_every_tool(self):
        tools = dict(self.tools)
        tools.pop('sweep')
        self.assertEqual(self.go(tools), tr.EXIT_FAILED)
        self.assertIn("['sweep']", self.entry()['error'])
        self.assertEqual(self.called('pool'), [])


if __name__ == '__main__':
    unittest.main()
