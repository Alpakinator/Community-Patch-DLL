"""Hermetic tests of the tier's hi_journey (epic 0630, ticket 5013): tools/release/tiers/stages/hi_journey.py run by the
tier driver against a fake instance, fake tools (a stand-in script in the test's temporary folder) and a stand-in for the
original stage's play (tierlib.original('hi_journey')._run, replaced per test: the original's own tests cover its play).
No pool, gate, lock, installer, game or instance is touched; nothing is written outside a temporary folder.

    python -I -X utf8 tools/release/tests/test_tier_hi_journey.py
"""
import hashlib
import json
import os
import sys
import tempfile
import time
import unittest
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
import tierlib  # noqa: E402

TIER_STAGES = HERE.parent / 'tiers' / 'stages'
TAG = 'Release-9.9.9'
USER = "Documents/My Games/Sid Meier's Civilization 5"
FALLBACK = 'f' * 64
O = tierlib.original('hi_journey')

FAKE = r'''
import hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
name, args = sys.argv[1], sys.argv[2:]
with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as f:
    f.write(name + ' ' + ' '.join(args) + '\n')
FD = Path(os.environ['FAKE_DIR'])
def env(k):
    return os.environ.get(k, '')
def opt(flag):
    return args[args.index(flag) + 1] if flag in args else None
if name == 'quietlock':
    sys.exit(subprocess.call(args[args.index('--') + 1:]) if args[0] == 'run' else 0)
if name == 'pool':
    if args[0] == 'claim':
        n = opt('--name') or 'c5h-t'
        if env('FAKE_NO_RECLAIM') and (FD / 'claimed-once').exists():
            print('refused: claimed by someone else', file=sys.stderr); sys.exit(2)
        (FD / 'claimed-once').write_text('1')
        print(n); print(FD / 'instances'); print('claimed by %s until later (short run)' % opt('--owner'))
        sys.exit(0)
    print(json.dumps({'released': args[1]})); sys.exit(0)
if name == 'provision':
    if args[0] == 'verify':
        sys.exit(int(env('FAKE_VERIFY_RC') or 0))
    if args[0] == 'set-dll' and '--dll' in args and not env('FAKE_SETDLL_NOOP'):
        m = json.loads((FD / 'instances' / args[1] / 'instance.json').read_text())
        if (m.get('dll') or {}).get('path'):
            t = FD / 'instances' / args[1] / m['dll']['path'].replace(chr(92), '/')
            wrong = env('FAKE_SETDLL_WRONG') and 'dll-before' in opt('--dll')
            t.write_bytes(b'not the DLL before' if wrong else Path(opt('--dll')).read_bytes())
    sys.exit(0)
if name == 'build_installer':
    o = Path(opt('--out')); o.mkdir(parents=True, exist_ok=True)
    if env('FAKE_BUILD_RC'):
        sys.exit(int(env('FAKE_BUILD_RC')))
    exe = o / 'VP64-Patch-x-for-VP5.4.8-TEST.exe'; exe.write_bytes(b'installer')
    (o / 'build-record.json').write_text(json.dumps({'exe': {'file': exe.name,
                                         'sha256': hashlib.sha256(b'installer').hexdigest(), 'size': 9}}))
    sys.exit(0)
if name == 'run_installer':
    d = Path(opt('--dir')); act = opt('--action')
    Path(opt('--log')).write_text('P64: ' + act)
    if act == 'install':
        if env('FAKE_INSTALL_RC') and not env('FAKE_INSTALL_PARTIAL'):
            sys.exit(int(env('FAKE_INSTALL_RC')))
        (d / 'VP64Options.ini').write_text('[Extras]\n')
        (d / 'MODS' / '(1) Community Patch' / 'VP64').mkdir(parents=True, exist_ok=True)
        (d / 'MODS' / '(1) Community Patch' / 'VP64' / 'server.exe').write_bytes(b'server')
        (d / 'Logs').mkdir(exist_ok=True); (d / 'Logs' / 'proxy.log').write_text('run output')
        sys.exit(int(env('FAKE_INSTALL_RC') or 0))
    if env('FAKE_REMOVE_RC'):
        sys.exit(int(env('FAKE_REMOVE_RC')))
    if not env('FAKE_REMOVE_LEAVES'):
        (d / 'VP64Options.ini').unlink()
    shutil.rmtree(d / 'MODS' / '(1) Community Patch' / 'VP64')
    sys.exit(0)
if name == 'treehash':
    root, out = Path(args[1]), Path(args[2])
    rows = []
    for p in sorted(root.rglob('*')):
        r = str(p.relative_to(root)).replace('/', '\\')
        rows.append(('D', '-', '-', r + '\\') if p.is_dir() else
                    ('F', hashlib.sha256(p.read_bytes()).hexdigest(), str(p.stat().st_size), r))
    out.write_text(''.join('\t'.join(x) + '\n' for x in rows), encoding='utf-8')
    sys.exit(0)
if name == 'stockctl':
    print(json.dumps({'counts': {'same': 3}, 'rows': []})); sys.exit(0)
sys.exit(0)
'''


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Base(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.base = self.tmp / 'base'
        self.calls = self.tmp / 'calls.txt'
        fake = self.tmp / 'fake.py'
        fake.write_text(FAKE)
        names = set(tr.default_tools()) | set(O.NEEDS) | {'build_installer', 'run_installer', 'treehash'}
        self.tools = {n: [sys.executable, '-I', str(fake), n] for n in names}
        self.lines = []
        keys = ('FAKE_CALLS', 'FAKE_DIR', 'FAKE_INSTALL_RC', 'FAKE_INSTALL_PARTIAL', 'FAKE_REMOVE_LEAVES',
                'FAKE_REMOVE_RC', 'FAKE_VERIFY_RC',
                'FAKE_BUILD_RC', 'FAKE_NO_RECLAIM', 'FAKE_SETDLL_NOOP', 'FAKE_SETDLL_WRONG', 'C5_RELEASE_HI_INSTALLED', 'C5_RELEASE_HI_ROUTES',
                'C5_RELEASE_HI_SAVES', 'C5_RELEASE_HI_INSTANCE', 'C5_RELEASE_HI_WAIT', 'C5_RELEASE_HI_EXE_BUILD',
                'C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE', 'C5_POOL_OWNER', 'C5_TIER_CRASH_GRACE_S',
                # ticket 5022 (runbook 1.12's trap, 2026-11-22): a release shell's C5_RELEASE_INSTANCE made 29 tests
                # read a live instance ("EXE build unknown"); the tests never read the operator's instance
                'C5_RELEASE_INSTANCE', 'C5_RELEASE_INSTANCE_WAIT')
        self._env = {k: os.environ.get(k) for k in keys}
        for k in keys:
            os.environ.pop(k, None)
        os.environ.update(FAKE_CALLS=str(self.calls), FAKE_DIR=str(self.tmp), C5_RELEASE_HI_WAIT='0',
                          C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'), C5_TIER_CRASH_GRACE_S='0')
        self.pids = {}
        self.during_play = {}
        self._orig = {k: getattr(O, k) for k in ('_run', 'INDEX', 'CLAIM_RETRY_S')}
        O.CLAIM_RETRY_S = 0
        self.plays = []
        self.product_exit = 0
        O._run = self.fake_play
        self.make_journeys()
        self.make_instance()
        self.make_build()
        self.seed_state()

    def tearDown(self):
        for k, v in self._orig.items():
            setattr(O, k, v)
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    # --- fixtures
    def make_journeys(self):
        jd = self.tmp / 'journeys'
        jd.mkdir()
        self.saves = self.tmp / 'saves'
        self.saves.mkdir()
        js = []
        for n in ('basic', 'large'):
            (jd / f'release-{n}.jsonl').write_text('{"t": "header"}\n')
            body = f'start save {n}'.encode()
            (self.saves / f'HI-T-{n}.Civ5Save').write_bytes(body)
            js.append({'name': f'release-{n}', 'trace': f'release-{n}.jsonl', 'mods': 'cp,vp', 'window': [1920, 1080],
                       'start_save': {'name': f'HI-T-{n.upper()}', 'file': f'HI-T-{n}.Civ5Save', 'sha256': sha(body)},
                       'made_saves': [], 'ends': 'menu'})
        self.index = jd / 'journeys.json'
        self.index.write_text(json.dumps({'format': 1, 'saves_dir': str(self.saves), 'journeys': js}))
        O.INDEX = self.index        # the fixture stands for the committed index

    def make_instance(self, build='26577625'):
        inst = self.tmp / 'instances' / 'c5h-t'
        u = inst / USER
        (u / 'MODS' / '(1) Community Patch').mkdir(parents=True)
        (u / 'MODS' / '(1) Community Patch' / 'CvGameCore_Expansion2.dll').write_bytes(b'vp dll')
        (u / 'UserSettings.ini').write_text('[GameSettings]\n')
        (inst / 'install').mkdir()
        (inst / 'instance.json').write_text(json.dumps({
            'name': 'c5h-t', 'user_dir': USER.replace('/', '\\'), 'install_dir': 'install', 'profile': 'mods548',
            'dll': {'path': USER + '/MODS/(1) Community Patch/CvGameCore_Expansion2.dll'},
            'exe_build': {'build': build, 'stamp': '0x6AF3837D', 'size': 15466264}}))
        self.inst, self.user = inst, u

    def make_build(self):
        b = self.tmp / 'layout' / 'x64'
        (b / 'bin').mkdir(parents=True)
        (b / 'proxy' / 'bin').mkdir(parents=True)
        for rel, body in (('bin/CvGameCore_Expansion2.dll', b'x64'), ('bin/server.exe', b'srv'),
                          ('proxy/bin/CvGameCore_Expansion2.dll', b'proxy')):
            (b / rel).write_bytes(body)
        self.build = {'x64_commit': 'a' * 40, 'i0_commit': 'b' * 40, 'x64_layout': str(b),
                      'i0ia_dll_sha256': FALLBACK,      # the stage-laid stock route's DLL (desk #9063, MC #9065)
                      'proxy_dll': str(b / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll')}
        pl = self.work / 'inputs' / 'player-x86'
        pl.mkdir(parents=True)
        (pl / 'CvGameCore_Expansion2.dll').write_bytes(b'player dll')

    def seed_state(self):
        st = {'format': tr.FORMAT, 'tag': TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': [
            {'name': 'build', 'cl_step': '6', 'status': 'ok', 'outputs': self.build, 'inputs_sha': 'seed'},
            {'name': 'pack', 'cl_step': '-', 'status': 'ok', 'outputs': {}, 'inputs_sha': 'seed'}]}
        self.work.mkdir(parents=True, exist_ok=True)
        (self.work / 'state.json').write_text(json.dumps(st))

    def mod(self):
        return tr.load_stage_module('hi_journey', TIER_STAGES)

    def base_take(self, change=None, drop=None, stock_exit=0, today=False):
        """A base take whose stock route passed with the inputs this take has (one changed or dropped on request)."""
        m = self.mod()
        idx = O.load_index(str(self.index))
        ins = m.stock_inputs(idx['journeys'], self.saves, FALLBACK, '26577625 0x6AF3837D 15466264')
        if change:
            ins[change] = '0' * 64
        if drop:
            ins.pop(drop)
        js = {}
        for j in idx['journeys']:
            res = self.base / 'runs' / f'{j["name"]}-stock.result.json'
            res.parent.mkdir(parents=True, exist_ok=True)
            res.write_text('{}')
            js[j['name']] = {'verdict': 'passed', 'routes': {
                'product': {'ran': True, 'exit': 0, 'checkpoints': [{'name': 'c', 'ok': True}]},
                'stock': {'ran': True, 'exit': stock_exit, 'checkpoints': [{'name': 'c', 'ok': True}],
                          'result': str(res), 'took_s': 300}}}
        outs = {'journeys': js, 'verdict': 'passed', 'layout': {'fallback_sha256': FALLBACK}}
        if not today:
            outs['stock_inputs'] = ins
        st = {'format': tr.FORMAT, 'tag': 'Release-9.9.8', 'stages': [
            {'name': 'hi_journey', 'status': 'ok', 'ended': '2026-11-20 10:00:00', 'outputs': outs}]}
        self.base.mkdir(parents=True, exist_ok=True)
        (self.base / 'state.json').write_text(json.dumps(st))

    # --- the stand-in for the original's play
    def fake_play(self, ctx):
        routes = os.environ.get('C5_RELEASE_HI_ROUTES')
        inst = os.environ.get('C5_RELEASE_HI_INSTALLED')
        self.plays.append((routes, inst))
        if routes == 'stock':
            self.instance_at_stock = os.environ.get('C5_RELEASE_HI_INSTANCE')
            self.removed_before_stock = any('--action remove' in c for c in self.called('run_installer'))
        js = {}
        for n in ('release-basic', 'release-large'):
            ex = self.product_exit if routes == 'product' else 0
            res = ctx.dir('hi_journey', 'runs') / f'{n}-{routes}.result.json'
            res.write_text('{}')
            row = {'route': routes, 'journey': n, 'ran': True, 'exit': ex, 'took_s': 100,
                   'checkpoints': [{'name': 'c', 'ok': ex == 0}], 'result': str(res)}
            if (n, routes) in self.pids:
                row['pid'] = self.pids[(n, routes)]
            if routes == 'product' and n in getattr(self, 'product_flake', {}):
                # the original's play_with_rerun: the row is the rerun's, the first run kept (MC #10389, 5022)
                row.update(known_flake=self.product_flake[n], first_run={'exit': 3, 'stop_step': 4})
            js[n] = {'routes': {routes: row}, 'verdict': 'incomplete'}
        for f in self.during_play.get(routes, []):     # a crash's evidence, written while the stage runs
            f()
        out = {'journeys': js, 'verdict': 'incomplete', 'index': str(self.index), 'index_committed': True,
               'candidate': O.candidate_of(ctx), 'layout': {'kind': O.INSTALLED, 'instance': inst,
                                                             'fallback_sha256': FALLBACK},
               'restore': {'ok': True}, 'claim_released': True}
        return tr.StageResult.ok(**O.write_report(ctx, out))

    # --- running
    def go(self, for_owner=False):
        cfg = {'tier': 'hotfix', 'who': 'test5013', 'gate_wait': 0, 'base_take': str(self.base),
               'tier_stages_dir': str(TIER_STAGES)}
        if for_owner:
            cfg['for_owner'] = True
        stages = [('build', '6', '5012'), ('pack', '-', '3504'), ('hi_journey', '-', '5013')]
        d = tt.TierDriver(TAG, self.work, object(), cfg, stages, tools=self.tools, out=self.lines.append)
        return d.run(only='hi_journey')

    def entry(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}['hi_journey']

    def called(self, name):
        if not self.calls.exists():
            return []
        return [ln[len(name) + 1:] for ln in self.calls.read_text().splitlines() if ln.startswith(name + ' ')]


class Install(Base):
    def test_install_fails_no_journeys_removal_runs_stage_fails(self):
        self.base_take()
        os.environ['FAKE_INSTALL_RC'] = '5'
        rc = self.go()
        e = self.entry()
        self.assertEqual(rc, tr.EXIT_FAILED, self.lines)
        self.assertEqual(e['status'], 'failed')
        self.assertIn('install', e['error'])
        self.assertEqual(self.plays, [])                           # no journey played
        self.assertIn('it wrote nothing', e['error'])
        self.assertFalse(any('--action remove' in c for c in self.called('run_installer')))
        self.assertTrue(any(c.startswith('set-dll c5h-t --dll') and 'dll-before' in c
                            for c in self.called('provision')), self.called('provision'))   # the player's state undone
        self.assertEqual((self.user / 'MODS' / '(1) Community Patch' / 'CvGameCore_Expansion2.dll').read_bytes(),
                         b'vp dll')
        self.assertTrue(e['outputs']['removal']['ok'], e['outputs']['removal'])
        self.assertEqual(e['outputs']['install']['rc'], 5)
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))

    def test_partial_install_fails_removal_takes_it_off(self):
        self.base_take()
        os.environ.update(FAKE_INSTALL_RC='5', FAKE_INSTALL_PARTIAL='1')
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertEqual(self.plays, [])
        self.assertTrue(any('--action remove' in c for c in self.called('run_installer')))
        self.assertTrue(e['outputs']['removal']['ok'], e['outputs']['removal'])
        self.assertFalse((self.user / 'VP64Options.ini').exists())

    def test_installer_build_fails_nothing_claimed(self):
        self.base_take()
        os.environ['FAKE_BUILD_RC'] = '4'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertIn('build_installer', self.entry()['error'])
        self.assertEqual(self.called('pool'), [])
        self.assertEqual(self.plays, [])

    def test_steps_timed_and_layout_back(self):
        self.base_take()
        self.assertEqual(self.go(), 0, self.lines)
        o = self.entry()['outputs']
        for s in ('installer', 'claim', 'player_state', 'tree_before', 'install', 'tree_installed', 'product',
                  'removal'):
            self.assertIn(s, o['steps'])
        self.assertEqual(o['removal']['tree']['outside_count'], 0)
        self.assertGreater(o['removal']['tree']['apart_count'], 0)    # the Logs file, listed apart
        self.assertFalse((self.user / 'VP64Options.ini').exists())
        self.assertEqual(o['exe_build'], '26577625 0x6AF3837D 15466264')
        self.assertEqual(o['installer']['sha256'], sha(b'installer'))
        cl = self.called('provision')
        self.assertTrue(any(c.startswith('set-dll c5h-t --dll') and 'player-x86' in c for c in cl), cl)
        self.assertTrue(any(c.startswith('verify c5h-t --quick') for c in cl), cl)
        self.assertEqual(self.called('build_installer')[0].count('--test-build'), 1)

    def test_removal_leaves_a_file_fails_and_keeps_claim(self):
        self.base_take()
        os.environ['FAKE_REMOVE_LEAVES'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('REMOVAL ON c5h-t FAILED', e['error'])
        self.assertIn('VP64Options.ini', e['error'])
        self.assertFalse(e['outputs']['removal']['claim_released'])
        # releases: after the install (for the original's own claim) only; none after the failed removal
        self.assertEqual(sum(1 for c in self.called('pool') if c.startswith('release')), 1)

    def test_removal_exit_fails(self):
        self.base_take()
        os.environ['FAKE_REMOVE_RC'] = '7'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertIn('removal: exit 7', self.entry()['error'])

    def test_verify_fails_after_removal(self):
        self.base_take()
        os.environ['FAKE_VERIFY_RC'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertIn('verify c5h-t --quick: exit 1', self.entry()['error'])

    def test_reclaim_refused_fails(self):
        self.base_take()
        os.environ['FAKE_NO_RECLAIM'] = '1'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertIn('could not be claimed again', self.entry()['error'])
        self.assertFalse(any('--action remove' in c for c in self.called('run_installer')))

    # the DLL before the run is put back by its hash, not the instance's first DLL (MC #9582, 5015 run 1) ----------

    def cp_dll(self):
        return self.user / 'MODS' / '(1) Community Patch' / 'CvGameCore_Expansion2.dll'

    def test_dll_changed_by_an_earlier_set_dll_is_put_back(self):
        self.base_take()
        self.cp_dll().write_bytes(b'an earlier set-dll')        # not the instance's first DLL
        self.go()
        e = self.entry()
        self.assertEqual(self.cp_dll().read_bytes(), b'an earlier set-dll')
        self.assertNotIn('set-dll c5h-t --original', self.called('provision'))
        self.assertEqual(e['outputs']['dll_before']['sha256'], hashlib.sha256(b'an earlier set-dll').hexdigest())
        self.assertTrue(e['outputs']['removal']['ok'], e['outputs']['removal'])

    def test_dll_not_restored_by_hash_fails_loudly_and_keeps_the_claim(self):
        self.base_take()
        os.environ['FAKE_SETDLL_WRONG'] = '1'
        rc = self.go()
        e = self.entry()
        self.assertEqual(rc, tr.EXIT_FAILED, self.lines)
        self.assertFalse(e['outputs']['removal']['ok'])
        errs = ' '.join(e['outputs']['removal']['errors'])
        self.assertIn('after set-dll', errs)
        self.assertIn('KEPT', e['error'])

    def test_no_dll_path_in_the_manifest_changes_nothing(self):
        self.base_take()
        m = json.loads((self.inst / 'instance.json').read_text())
        m.pop('dll')
        (self.inst / 'instance.json').write_text(json.dumps(m))
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        e = self.entry()
        self.assertIn('names no dll.path', e['error'])
        self.assertFalse(any(c.startswith('set-dll') for c in self.called('provision')))
        self.assertFalse(any('--action install' in c for c in self.called('run_installer')))

    def test_other_exe_build_skipped_and_released(self):
        self.base_take()
        (self.inst / 'instance.json').write_text(json.dumps({'name': 'c5h-t', 'user_dir': USER.replace('/', '\\'),
                                                             'exe_build': {'build': '4390913'}}))
        self.assertEqual(self.go(), 0)
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('4390913', e['outputs']['not_played'])
        self.assertEqual(self.called('run_installer'), [])
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))

    def test_installed_instance_refused(self):
        self.base_take()
        rp = self.user / 'VP64' / 'install' / 'record.txt'
        rp.parent.mkdir(parents=True)
        rp.write_text('VP64-INSTALL-RECORD\t1\n')
        self.go()
        self.assertEqual(self.entry()['status'], 'skipped')
        self.assertIn('not a clean instance', self.entry()['outputs']['not_played'])

    def test_outside_installed_variable_refused(self):
        self.base_take()
        os.environ['C5_RELEASE_HI_INSTALLED'] = 'c5h-x'
        self.assertEqual(self.go(), tr.EXIT_FAILED)
        self.assertIn('installs its own layout', self.entry()['error'])
        self.assertEqual(self.called('build_installer'), [])

    def test_env_put_back_after_play(self):
        self.base_take()
        self.go()
        self.assertIsNone(os.environ.get('C5_RELEASE_HI_INSTALLED'))
        self.assertIsNone(os.environ.get('C5_RELEASE_HI_ROUTES'))
        self.assertEqual(self.plays[0], ('product', 'c5h-t'))


class Stock(Base):
    def test_all_equal_stock_carried(self):
        self.base_take()
        self.assertEqual(self.go(), 0, self.lines)
        o = self.entry()['outputs']
        self.assertEqual(self.plays, [('product', 'c5h-t')])          # stock not played
        self.assertEqual(o['stock']['mode'], 'carried')
        c = o['carried_parts']['stock']
        self.assertTrue(c['line'].startswith('carried from Release-9.9.8'), c['line'])
        self.assertIn('stock_dll ffffffffffffffff equal', c['line'])
        row = o['journeys']['release-basic']['routes']['stock']
        self.assertFalse(row['ran'])
        self.assertTrue(row['carried'])
        self.assertNotIn('took_s', row)                               # a carried result has no seconds of its own
        self.assertNotIn('stock', o['steps'])
        self.assertEqual(o['verdict'], 'passed')
        self.assertEqual(o['journeys']['release-basic']['compare']['counts'], {'same': 3})
        self.assertTrue(any('stock carried from Release-9.9.8' in ln for ln in self.lines), self.lines)

    def test_product_fails_stock_fresh(self):
        self.base_take()
        self.product_exit = 3
        self.assertEqual(self.go(), 0)
        o = self.entry()['outputs']
        self.assertEqual(self.plays, [('product', 'c5h-t'), ('stock', None)])     # stock: stage-laid, not installed
        self.assertEqual(o['stock']['where'], 'stage-laid, after the removal')
        self.assertEqual(o['stock']['mode'], 'fresh')
        self.assertIn('product route did not pass', o['fresh_why'])
        self.assertNotIn('carried_parts', o)
        self.assertEqual(o['verdict'], 'failed')
        self.assertTrue(o['journeys']['release-basic']['routes']['stock']['ran'])

    def test_named_flake_rerun_passed_still_carries_stock(self):
        # MC #10389 (ticket 5022): a NAMED known flake whose one rerun passed carries stock, said in the line
        self.base_take()
        self.product_flake = {'release-basic': 'the city screen not opening after founding (HI 8.5)'}
        self.assertEqual(self.go(), 0, self.lines)
        o = self.entry()['outputs']
        self.assertEqual(self.plays, [('product', 'c5h-t')])
        c = o['carried_parts']['stock']
        self.assertIn('passed on its one rerun after a known flake: release-basic: the city screen', c['line'])
        self.assertEqual(c['product_flakes']['release-basic']['first_run_exit'], 3)
        self.assertEqual(o['verdict'], 'passed')

    def test_flake_rerun_that_failed_runs_stock_fresh(self):
        # negative control: the rerun's own row failed, so stock is played (no carry on a failed product route)
        self.base_take()
        self.product_flake = {'release-basic': 'the city screen not opening after founding (HI 8.5)'}
        self.product_exit = 3
        self.assertEqual(self.go(), 0)
        o = self.entry()['outputs']
        self.assertEqual(o['stock']['mode'], 'fresh')
        self.assertIn('product route did not pass', o['fresh_why'])

    def test_stock_input_changed_fresh(self):
        for key in ('trace:release-basic', 'stock_dll', 'exe_build', 'harness', 'start_save:release-large',
                    'journey:release-large'):
            with self.subTest(key=key):
                self.tearDown()
                self.setUp()
                self.base_take(change=key)
                self.assertEqual(self.go(), 0)
                o = self.entry()['outputs']
                self.assertEqual(o['stock']['mode'], 'fresh')
                self.assertIn(key, o['fresh_why'])
                self.assertEqual([p[0] for p in self.plays], ['product', 'stock'])

    def test_base_by_todays_driver_fresh_names_missing(self):
        self.base_take(today=True)
        self.go()
        o = self.entry()['outputs']
        self.assertEqual(o['stock']['mode'], 'fresh')
        self.assertIn('recorded no hash', o['fresh_why'])
        self.assertIn('exe_build', o['fresh_why'])
        self.assertIn("'stock_dll'", o['fresh_why'])   # a base by today's driver recorded no stage-laid stock DLL

    def test_fresh_stock_is_played_after_the_removal_on_the_stage_laid_layout(self):
        # 0626's rehearsal, 2026-11-21 03:21 (desk #9063): on the installed layout the stock route cannot be judged
        self.base_take(change='harness')
        self.assertEqual(self.go(), 0, self.lines)
        o = self.entry()['outputs']
        self.assertEqual(self.plays, [('product', 'c5h-t'), ('stock', None)])
        self.assertEqual(self.instance_at_stock, 'c5h-t')
        self.assertTrue(self.removed_before_stock, 'the stock route ran before the installer was removed')
        self.assertEqual(o['verdict'], 'passed')
        self.assertIn('stock', o['steps'])

    def test_stock_on_the_installed_layout_is_refused_loudly(self):
        m = self.mod()
        with self.assertRaises(m.InstalledStockRefused) as e:
            m.installed_env('c5h-t', 'stock')
        self.assertIn('cannot be judged', str(e.exception))
        with self.assertRaises(m.InstalledStockRefused):
            m.installed_env('c5h-t', 'product,stock')
        self.assertEqual(m.installed_env('c5h-t', 'product'),
                         {'C5_RELEASE_HI_INSTALLED': 'c5h-t', 'C5_RELEASE_HI_ROUTES': 'product'})
        self.assertIsNone(m.stage_laid_stock_env('c5h-t')['C5_RELEASE_HI_INSTALLED'])

    def test_no_stage_laid_stock_when_the_removal_fails(self):
        self.base_take(change='harness')
        os.environ['FAKE_REMOVE_RC'] = '7'
        try:
            rc = self.go()
        finally:
            os.environ.pop('FAKE_REMOVE_RC', None)
        self.assertNotEqual(rc, 0)
        self.assertEqual([p[0] for p in self.plays], ['product'])

    def test_base_stock_failed_fresh(self):
        self.base_take(stock_exit=2)
        self.go()
        self.assertIn('did not pass', self.entry()['outputs']['fresh_why'])

    def test_base_stock_result_gone_fresh(self):
        self.base_take()
        (self.base / 'runs' / 'release-basic-stock.result.json').unlink()
        self.go()
        self.assertIn('is gone', self.entry()['outputs']['fresh_why'])

    def test_carried_stock_recorded_for_the_next_take(self):
        self.base_take()
        self.go()
        o = self.entry()['outputs']
        m = self.mod()
        self.assertEqual(sorted(k for k in o['stock_inputs'] if o['stock_inputs'][k]),
                         sorted(m.required_stock_inputs(['release-basic', 'release-large'])))


class D162(Base):
    def outputs(self):
        self.base_take()
        self.assertEqual(self.go(), 0, self.lines)
        return self.entry()

    def test_owner_check_counts_complete_carried_stock(self):
        e = self.outputs()
        m = self.mod()
        b = dict(self.build)
        chk = m.owner_check(e, b)
        self.assertTrue(chk['allowed'], chk['why'])
        self.assertEqual(chk['stock_carried'].split()[0], 'Release-9.9.8')
        line = m.owner_line(chk, TAG)
        self.assertIn('stock carried from Release-9.9.8', line)
        self.assertIn('product fresh, stock carried', line)
        self.assertIn('for the owner: yes', line)

    def test_owner_check_refuses_incomplete_carried(self):
        e = self.outputs()
        m = self.mod()
        e['outputs']['carried_parts']['stock']['what'].pop('harness')
        chk = m.owner_check(e, dict(self.build))
        self.assertFalse(chk['allowed'])
        self.assertTrue(any('harness' in w for w in chk['why']), chk['why'])
        e['outputs'].pop('carried_parts')
        chk = m.owner_check(e, dict(self.build))
        self.assertFalse(chk['allowed'])
        self.assertTrue(any('without a carried dict' in w for w in chk['why']), chk['why'])
        self.assertIn('for the owner: NO', m.owner_line(chk, TAG))

    def test_original_owner_check_does_not_count_carried(self):
        # take_release.py --owner-check reads the original module: a carried row is not a run there (fail safe)
        e = self.outputs()
        chk = O.owner_check(e, dict(self.build))
        self.assertFalse(chk['allowed'])
        self.assertTrue(any('route stock not played' in w for w in chk['why']), chk['why'])

    def test_for_owner_take_with_carried_stock_reaches(self):
        self.base_take()
        self.assertEqual(self.go(for_owner=True), 0, self.lines)
        e = self.entry()
        self.assertTrue(e['outputs']['owner']['allowed'], e['outputs']['owner'])
        self.assertTrue(any('stock carried from' in ln and 'for the owner: yes' in ln for ln in self.lines), self.lines)

    def test_for_owner_take_with_failed_product_stops(self):
        self.base_take()
        self.product_exit = 3
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP)
        self.assertTrue(any('D162' in ln for ln in self.lines))


class ExitCrash(Base):
    """MC #9582 (c): a game that crashed without a box AFTER its replay passed (K24, the -ia DLL's exit crash) is one
    line in the stage's outputs and report; the journey stays passed, and nothing else is reported."""

    def marker(self, pid, code='0xC0000005', stale=True):
        run = self.inst / 'run'
        when = datetime.now().astimezone().isoformat(timespec='seconds')
        dest = run / 'crash' / (datetime.now().strftime('%Y%m%d_%H%M%S') + f'_pid{pid}')
        dest.mkdir(parents=True)
        (dest / 'manifest.json').write_text(json.dumps({'pid': pid, 'found_crash': False}))
        m = {'game_pid': pid, 'kind': 'crash', 'first_seen_at': when, 'exit_code_hex': code,
             'signature': f'exe-crash: {code} EXCEPTION_ACCESS_VIOLATION (no proxy crash record)',
             'crash': {'dest': str(dest)}}
        p = run / 'evidence' / f'dialog_stale_{pid}.json' if stale else run / 'dialog_open.json'
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(m))

    def test_stock_exit_crash_after_pass_is_one_line_k24(self):
        self.base_take(change='stock_dll')
        self.pids = {('release-basic', 'product'): 101, ('release-large', 'product'): 102,
                     ('release-basic', 'stock'): 201, ('release-large', 'stock'): 202}
        self.during_play = {'stock': [lambda: self.marker(201),
                                      lambda: self.marker(999, stale=False)]}   # 999: not a game of this stage
        self.assertEqual(self.go(), 0, self.lines)
        o = self.entry()['outputs']
        self.assertEqual(o['journeys']['release-basic']['verdict'], 'passed')
        c = o['crashes_after_pass']
        self.assertEqual(len(c), 1, c)
        self.assertEqual((c[0]['route'], c[0]['journey'], c[0]['pid']), ('stock', 'release-basic', 201))
        self.assertEqual(c[0]['known_cause'], 'K24, after the replay passed')
        self.assertIn('known cause K24, after the replay passed', c[0]['line'])
        self.assertIn('0xC0000005', c[0]['line'])
        self.assertIn('crashed without a box', Path(o['report']).read_text(encoding='utf-8'))
        rep = tr.load_stage_module('tierreport', TIER_STAGES)
        r = rep.build(json.loads((self.work / 'state.json').read_text()), ['hi_journey', 'tierreport'])
        self.assertEqual(r['verdict'], 'hotfix tier reached')
        md = rep.markdown(r)
        self.assertIn('## Crashes after a passed journey', md)
        self.assertIn('stock route, journey release-basic: the game (pid 201)', md)

    def test_product_crash_folder_only_has_no_known_cause(self):
        self.base_take(change='stock_dll')
        self.pids = {('release-large', 'product'): 102}

        def folder():
            time.sleep(1.1)                            # the folder's stamp has whole seconds
            d = self.inst / 'run' / 'crash' / (datetime.now().strftime('%Y%m%d_%H%M%S') + '_pid102')
            d.mkdir(parents=True)
            (d / 'manifest.json').write_text('{}')
        self.during_play = {'product': [folder]}
        self.assertEqual(self.go(), 0, self.lines)
        c = self.entry()['outputs']['crashes_after_pass']
        self.assertEqual([(x['route'], x['pid'], x['known_cause']) for x in c], [('product', 102, None)])
        self.assertIn('no known cause: file it', c[0]['line'])

    def test_failed_row_and_old_crash_not_reported(self):
        self.base_take(change='stock_dll')
        self.product_exit = 3                          # product failed: its rows are the original's failures
        self.pids = {('release-basic', 'product'): 101}
        self.during_play = {'product': [lambda: self.marker(101)]}
        self.go()
        self.assertEqual(self.entry()['outputs'].get('crashes_after_pass') or [], [])
        m = self.mod()
        old = self.inst / 'run' / 'crash' / '20200101_000000_pid7'
        old.mkdir(parents=True)
        js = {'j': {'routes': {'stock': {'pid': 7, 'exit': 0, 'checkpoints': []}}}}
        self.assertEqual(m.crashes_after_pass(js, self.inst / 'run', since=time.time() - 60), [])
        self.assertEqual(len(m.crashes_after_pass(js, self.inst / 'run', since=0)), 1)

    def test_no_crash_says_none(self):
        self.base_take(change='stock_dll')
        self.pids = {('release-basic', 'stock'): 201}
        self.assertEqual(self.go(), 0, self.lines)
        o = self.entry()['outputs']
        self.assertEqual(o['crashes_after_pass'], [])
        rep = tr.load_stage_module('tierreport', TIER_STAGES)
        md = rep.markdown(rep.build(json.loads((self.work / 'state.json').read_text()), ['hi_journey']))
        self.assertIn('None: no game of a passed journey crashed without a box.', md)


class Helpers5022(Base):
    """Ticket 5022: claim/claim_one take profile= (and note=), build_installer takes dest=; the defaults are as before."""

    class Ctx:
        def __init__(self, work, tools):
            self.work, self.tag, self.who, self.cmds = work, TAG, 'test5022', []
            self._d = type('D', (), {'tools': tools})()

        def tool(self, n):
            return [str(x) for x in self._d.tools[n]]

        def run(self, cmd, name=None, **kw):
            import subprocess
            cmd = [str(c) for c in cmd]
            self.cmds.append(cmd)
            r = subprocess.run(cmd, capture_output=True, text=True)
            return tr.RunResult(r.returncode, self.work / f'{name}.log', 0.0, r.stdout + r.stderr)

        def say(self, *a):
            pass

        def dir(self, *parts):   # the driver's Ctx.dir: a folder under the work folder, made (5023)
            p = self.work.joinpath(*parts)
            p.mkdir(parents=True, exist_ok=True)
            return p

        def check_stop(self):
            pass

    def ctx(self):
        return self.Ctx(self.work, self.tools)

    def st(self):
        return {'ticket': '5022', 'address': None, 'by': 'own', 'instance': None, 'wait_min': '0'}

    def pool_args(self, c):
        return c.cmds[-1][c.cmds[-1].index('pool') + 1:]

    def test_claim_one_without_profile_is_as_before(self):
        c = self.ctx()
        got, why = self.mod().claim_one(c, self.st(), {'exe_build': '26577625'})
        self.assertIsNotNone(got, why)
        a = self.pool_args(c)
        self.assertNotIn('--profile', a)
        self.assertIn(f'take_tier {TAG} hi_journey install', a)
        self.assertEqual(a[a.index('--exe-build') + 1], '26577625')

    def test_claim_with_profile_and_note(self):
        c = self.ctx()
        got, why = self.mod().claim(c, self.st(), {'exe_build': '26577625'}, names=['c5h-x'], profile='modpack548',
                                    note='take_tier x modpack smoke')
        self.assertEqual(got[0], 'c5h-x', why)
        a = self.pool_args(c)
        self.assertEqual(a[a.index('--profile') + 1], 'modpack548')
        self.assertEqual(a[a.index('--name') + 1], 'c5h-x')
        self.assertEqual(a[a.index('--note') + 1], 'take_tier x modpack smoke')
        c2 = self.ctx()
        self.mod().claim(c2, self.st(), {'exe_build': '26577625'}, profile='modpack548')   # the pool's choice
        a = self.pool_args(c2)
        self.assertEqual((a[a.index('--profile') + 1], a[a.index('--exe-build') + 1]), ('modpack548', '26577625'))

    def test_unknown_ticket_ends_the_claim_wait_at_once(self):
        # 5024's modpack check (2026-11-22): C5_RELEASE_HI_TICKET=5024 (not on main) was retried as "no free
        # instance" every 120 s; a refusal no wait cures now ends the wait at once
        m = self.mod()
        calls = []
        refusals = {'final': 'pool.py claim c5h-x: exit 2: refused: unknown ticket: 5024 is neither on the queue',
                    'busy': 'pool.py claim c5h-x: exit 2: c5h-x is claimed by someone else'}
        saved = (m.claim_one, m.O.CLAIM_RETRY_S)
        try:
            m.O.CLAIM_RETRY_S = 0.05
            for kind, wait_min, at_most in (('final', '5', 1), ('busy', '0.02', None)):
                calls.clear()
                m.claim_one = lambda *a, k=kind, **kw: (calls.append(1), (None, refusals[k]))[1]
                t0 = time.monotonic()
                got, why = m.claim(self.ctx(), self.st(), {'exe_build': '26577625'}, names=['c5h-x'], wait_min=wait_min)
                self.assertIsNone(got)
                self.assertIn(refusals[kind].split(': ', 2)[-1][:20], why)
                if at_most:
                    self.assertEqual(len(calls), at_most)
                    self.assertLess(time.monotonic() - t0, 5)
                else:                                   # negative control: an instance in use is still waited for
                    self.assertGreater(len(calls), 1)
        finally:
            m.claim_one, m.O.CLAIM_RETRY_S = saved

    def test_build_installer_dest(self):
        m = self.mod()
        ins, why = m.build_installer(self.ctx(), self.build, {'release_name': 'x'})
        self.assertIsNotNone(ins, why)
        self.assertEqual(Path(ins['exe']).parent, self.work / 'hi_journey' / 'installer')      # the default as before
        dest = self.work / 'other' / 'installer'
        ins2, why = m.build_installer(self.ctx(), self.build, {'release_name': 'x'}, dest=dest)
        self.assertIsNotNone(ins2, why)
        self.assertEqual(Path(ins2['exe']).parent, dest)
        self.assertTrue(Path(ins['exe']).is_file())                    # the default folder is left alone
        self.assertEqual(ins2['sha256'], ins['sha256'])


if __name__ == '__main__':
    unittest.main()
