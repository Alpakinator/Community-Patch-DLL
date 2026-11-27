"""Hermetic tests of the tier stage modpack (epic 0630 M2, ticket 5021): tools/release/tiers/stages/modpack.py run by
the tier driver against a fake modpack-form instance and fake tools (one stand-in script in the test's temporary
folder plays the pool, provision, the installer and its build, treehash, the harness's frontend.py, cli.py, lua.py,
turns.py and dialogwatch.py). No pool, gate, lock, installer, game or instance is touched; nothing is written outside a
temporary folder.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_tier_modpack.py
"""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402

TIER_STAGES = HERE.parent / 'tiers' / 'stages'
TAG = 'Release-9.9.9'
NAME = 'c5h-mpk-t'
USER = "Documents/My Games/Sid Meier's Civilization 5"
INST_DLL = 'install/Assets/DLC/VP_MODPACK/CvGameCore_Expansion2.dll'
SAVE = 'Saves/single/auto/AutoSave_0019 BC-3240.Civ5Save'

# The fake game: FD/running (the EXE runs), FD/turn (the game turn), FD/marker.json (the dialog watcher's marker),
# FD/loaded (the load screen has been dismissed). Switches: FAKE_* environment variables.
FAKE = r'''
import hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
name, args = sys.argv[1], sys.argv[2:]
with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as f:
    f.write(json.dumps([name] + args) + '\n')
FD = Path(os.environ['FAKE_DIR'])
INST = FD / 'instances' / 'c5h-mpk-t'
MG = INST / "Documents/My Games/Sid Meier's Civilization 5"
DLC = INST / 'install' / 'Assets' / 'DLC'
def env(k):
    return os.environ.get(k, '')
def opt(flag):
    return args[args.index(flag) + 1] if flag in args else None
def running():
    return (FD / 'running').exists()
def stop():
    (FD / 'running').unlink(missing_ok=True)
def marker(kind, sig):
    (FD / 'marker.json').write_text(json.dumps({'kind': kind, 'signature': sig, 'game_pid': 4711,
                                                'dialogs': [{'box_kind': kind}]}))
def crash():
    stop(); marker('crash', 'EXCEPTION_ACCESS_VIOLATION at CvGameCore_Expansion2.dll+0x1234')
def at(step):
    if env('FAKE_CRASH_AT') == step:
        crash(); return True
    if env('FAKE_BOX_AT') == step:
        marker('dialog', 'Runtime Error: assertion'); return True
    return False
if name == 'quietlock':
    sys.exit(subprocess.call(args[args.index('--') + 1:]) if args[0] == 'run' else 0)
if name == 'pool':
    if args[0] == 'claim':
        n = opt('--name') or 'c5h-mpk-t'
        if env('FAKE_NO_INSTANCE'):
            print('no free instance of profile ' + str(opt('--profile')), file=sys.stderr); sys.exit(2)
        print(n); print(FD / 'instances'); print('claimed by %s until later (short run)' % opt('--owner'))
        sys.exit(0)
    print(json.dumps({'released': args[1]})); sys.exit(0)
if name == 'provision':
    if args[0] == 'verify':
        sys.exit(int(env('FAKE_VERIFY_RC') or 0))
    if args[0] == 'set-dll' and '--dll' in args:
        (INST / 'install/Assets/DLC/VP_MODPACK/CvGameCore_Expansion2.dll').write_bytes(Path(opt('--dll')).read_bytes())
    sys.exit(0)
if name == 'build_installer':
    o = Path(opt('--out')); o.mkdir(parents=True, exist_ok=True)
    exe = o / 'VP64-Patch-x-for-VP5.4.8-TEST.exe'; exe.write_bytes(b'installer')
    (o / 'build-record.json').write_text(json.dumps({'exe': {'file': exe.name,
                                         'sha256': hashlib.sha256(b'installer').hexdigest()}}))
    sys.exit(0)
if name == 'run_installer':
    d = Path(opt('--dir')); act = opt('--action')
    Path(opt('--log')).write_text('P64: ' + act)
    rec = d / 'VP64' / 'install' / 'record.txt'
    patch = DLC / 'VP_MODPACK' / 'VP64'
    if act == 'install':
        if env('FAKE_INSTALL_RC'):
            sys.exit(int(env('FAKE_INSTALL_RC')))
        (d / 'VP64Options.ini').write_text('[Extras]\n')
        rec.parent.mkdir(parents=True, exist_ok=True)
        rec.write_text('VP64-INSTALL-RECORD\t1\nrelease\tx\nform\t%s\nend\t1\n' % (env('FAKE_FORM') or 'modpack'),
                       encoding='utf-8-sig')
        patch.mkdir(parents=True, exist_ok=True); (patch / 'server.exe').write_bytes(b'server')
        sys.exit(0)
    if env('FAKE_REMOVE_RC'):
        sys.exit(int(env('FAKE_REMOVE_RC')))
    if not env('FAKE_REMOVE_LEAVES'):
        (d / 'VP64Options.ini').unlink()
    shutil.rmtree(d / 'VP64')
    if env('FAKE_DLC_LEAVES'):
        (patch / 'server.exe').unlink()
    else:
        shutil.rmtree(patch)
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
if name == 'frontend':
    verb = args[2]
    if verb == 'prepare':
        (FD / 'prepared').write_text(json.dumps(args)); sys.exit(0)
    if verb == 'restore':
        (FD / 'restored').write_text('1'); sys.exit(0)
    if verb == 'ingame':
        if (FD / 'marker.json').exists() and json.loads((FD / 'marker.json').read_text())['kind'] == 'dialog':
            print('NOT IN GAME [dialog] a box'); sys.exit(8)
        if env('FAKE_LOAD') == 'never':
            print('NOT IN GAME [front_end] the save was not loaded'); sys.exit(1)
        if (FD / 'loaded').exists():
            print('IN GAME [in_game] ok'); sys.exit(0)
        print('NOT IN GAME [load_screen_ready] the load screen waits'); sys.exit(1)
    if verb == 'dismiss':
        (FD / 'loaded').write_text('1'); print(json.dumps({'ok': True})); sys.exit(0)
    sys.exit(0)
if name == 'cli':
    verb = args[2]
    if verb == 'launch':
        if env('FAKE_LAUNCH_ENV'):
            Path(env('FAKE_LAUNCH_ENV')).write_text(os.environ.get('C5_FRAME_CAP_AFTER', '-'))
        (FD / 'running').write_text('1'); (FD / 'turn').write_text('19'); sys.exit(0)
    if verb == 'status':
        print(json.dumps({'running': running()})); sys.exit(0)
    if verb == 'stop':
        stop(); (FD / 'stopped').write_text('1'); sys.exit(0)
    sys.exit(0)
if name == 'lua':
    code = args[-1]
    if not running():
        print(json.dumps({'ok': None, 'timeout': True})); sys.exit(3)
    if 'GetGameTurn' in code:
        print(json.dumps({'ok': True, 'results': [int((FD / 'turn').read_text())]}, indent=2)); sys.exit(0)
    if 'ExitGame' in code:
        if at('exit'):
            sys.exit(3)
        if not env('FAKE_EXIT_HANGS'):
            stop()
        print(json.dumps({'ok': True, 'results': []})); sys.exit(0)
    sys.exit(0)
if name == 'turns':
    verb = args[0]
    if verb == 'end':
        if at('end'):
            sys.exit(8)
        sys.exit(0)
    if verb == 'wait':
        if at('wait'):
            sys.exit(2)
        if not env('FAKE_NO_ADVANCE'):
            (FD / 'turn').write_text(str(int((FD / 'turn').read_text()) + 1))
        print('advanced'); sys.exit(0)
    if verb == 'ready':
        print('READY settled'); sys.exit(0)
    sys.exit(0)
if name == 'dialogwatch':
    if (FD / 'marker.json').exists():
        print((FD / 'marker.json').read_text()); sys.exit(8)
    print('no marker'); sys.exit(0)
sys.exit(0)
'''


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Base(unittest.TestCase):
    PROFILE = 'modpack548'
    TAG = TAG

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.calls = self.tmp / 'calls.txt'
        fake = self.tmp / 'fake.py'
        fake.write_text(FAKE)
        names = set(tr.default_tools()) | {'pool', 'provision', 'frontend', 'cli', 'turns', 'lua', 'dialogwatch',
                                           'crash', 'build_installer', 'run_installer', 'treehash'}
        self.tools = {n: [sys.executable, '-I', str(fake), n] for n in names}
        self.lines = []
        keys = [k for k in os.environ if k.startswith(('FAKE_', 'C5_RELEASE_MODPACK_', 'C5_RELEASE_HI_'))] + [
            'C5_POOL_OWNER', 'C5_SETTINGS_FILE', 'C5_RELEASE_INSTANCE', 'C5_TIER_CRASH_GRACE_S']
        self._env = {k: os.environ.get(k) for k in keys}
        for k in keys:
            os.environ.pop(k, None)
        # The load limit is a hang guard here, not a race (ticket 0695): the stage's load clock starts BEFORE
        # cli.py launch, so the window holds four fake-tool process starts (launch through quietlock, ingame,
        # dismiss, ingame), each a fresh Python. That is 0.7 s on a quiet workstation and passed 3 s under a
        # suites barrier beside 9 heavy jobs (5024 take 5). The fake reaches its loaded state by the file its
        # dismiss writes, so a loading test ends at the first ingame after the dismiss whatever the limit; only
        # test_load_failure (a save that never loads) waits the limit out, and it sets its own short one.
        os.environ.update(FAKE_CALLS=str(self.calls), FAKE_DIR=str(self.tmp), C5_RELEASE_HI_WAIT='0',
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'), C5_TIER_CRASH_GRACE_S='0',
                          C5_RELEASE_MODPACK_LOAD_TIMEOUT_S='120', C5_RELEASE_MODPACK_EXIT_WAIT_S='2',
                          C5_RELEASE_MODPACK_POLL_S='0.05')
        self.make_instance()
        self.make_build()
        self.seed_state()

    def tearDown(self):
        for k in [k for k in os.environ if k.startswith('FAKE_')]:
            os.environ.pop(k, None)
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    def make_instance(self):
        inst = self.tmp / 'instances' / NAME
        u = inst / USER
        (u / 'Saves' / 'single' / 'auto').mkdir(parents=True)
        (u / SAVE).write_bytes(b'turn 19 autosave, modpack form')
        (u / 'UserSettings.ini').write_text('[GameSettings]\n')
        vp = inst / 'install' / 'Assets' / 'DLC' / 'VP_MODPACK'
        vp.mkdir(parents=True)
        (vp / 'CvGameCore_Expansion2.dll').write_bytes(b'the instance own vp dll')
        (vp / 'VP.Civ5Pkg').write_text('pkg')
        (inst / 'instance.json').write_text(json.dumps({
            'name': NAME, 'user_dir': USER.replace('/', '\\'), 'install_dir': 'install', 'profile': self.PROFILE,
            'dll': {'path': INST_DLL}, 'exe_build': {'build': '26577625', 'stamp': '0x6AF3837D', 'size': 15466264}}))
        self.inst, self.user = inst, u

    def make_build(self):
        b = self.tmp / 'layout' / 'x64'
        (b / 'bin').mkdir(parents=True)
        self.build = {'x64_commit': 'a' * 40, 'i0_commit': 'b' * 40, 'x64_layout': str(b),
                      'x64_hashes': {'bin/CvGameCore_Expansion2.dll': {'sha256': 'c' * 64}},
                      'composite_dll': str(b / 'proxy' / 'bin' / 'shell_composite.dll'), 'composite_sha256': 'd' * 64}
        pl = self.work / 'inputs' / 'player-x86'
        pl.mkdir(parents=True)
        (pl / 'CvGameCore_Expansion2.dll').write_bytes(b'player dll')

    def seed_state(self):
        st = {'format': tr.FORMAT, 'tag': self.TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': [
            {'name': 'build', 'cl_step': '6', 'status': 'ok', 'outputs': self.build, 'inputs_sha': 'seed'},
            {'name': 'pack', 'cl_step': '-', 'status': 'ok', 'outputs': {}, 'inputs_sha': 'seed'}]}
        self.work.mkdir(parents=True, exist_ok=True)
        (self.work / 'state.json').write_text(json.dumps(st))

    def go(self):
        cfg = {'tier': 'intermediate', 'who': 'test5021', 'gate_wait': 0, 'tier_stages_dir': str(TIER_STAGES)}
        stages = [('build', '6', '5012'), ('pack', '-', '3504'), ('modpack', '-', '5021')]
        d = tt.TierDriver(self.TAG, self.work, object(), cfg, stages, tools=self.tools, out=self.lines.append)
        return d.run(only='modpack')

    def entry(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}['modpack']

    def called(self, name):
        if not self.calls.exists():
            return []
        rows = [json.loads(ln) for ln in self.calls.read_text().splitlines()]
        return [r[1:] for r in rows if r[0] == name]

    def released(self):
        return [c for c in self.called('pool') if c[0] == 'release']

    def assert_back(self):
        """The instance as before: the DLL by hash, no patch file in My Games or Assets/DLC, the save copy gone."""
        self.assertEqual((self.inst / INST_DLL).read_bytes(), b'the instance own vp dll')
        self.assertFalse((self.user / 'VP64Options.ini').exists())
        self.assertFalse((self.inst / 'install' / 'Assets' / 'DLC' / 'VP_MODPACK' / 'VP64').exists())
        self.assertFalse(list((self.user / 'Saves' / 'single').glob('C5H-*')))


class Pass(Base):
    def test_pass(self):
        launch_env = self.tmp / 'launch-env.txt'
        os.environ['FAKE_LAUNCH_ENV'] = str(launch_env)
        self.assertEqual(self.go(), 0, self.lines[-5:])
        e = self.entry()
        self.assertEqual(e['status'], 'ok')
        o = e['outputs']
        self.assertEqual(o['verdict'], 'passed')
        p = o['play']
        self.assertEqual((p['loaded'], p['turn_from'], p['turn_to'], p['clean_exit']), (True, 19, 20, True))
        self.assertEqual(p['save']['sha256'], sha(b'turn 19 autosave, modpack form'))
        self.assertTrue(o['removal']['ok'], o['removal'])
        self.assertEqual(o['removal']['dlc_tree']['count'], 0)
        self.assertEqual(o['install']['record']['form'], 'modpack')
        for k in ('installer', 'claim', 'dlc_before', 'player_state', 'tree_before', 'install', 'tree_installed',
                  'play', 'removal'):
            self.assertIn(k, o['steps'])
        claim = next(c for c in self.called('pool') if c[0] == 'claim')
        self.assertEqual(claim[claim.index('--profile') + 1], 'modpack548')
        self.assertEqual(claim[claim.index('--exe-build') + 1], '26577625')
        self.assertTrue(claim[claim.index('--owner') + 1].endswith('-modpack'))
        prep = next(c for c in self.called('frontend') if c[2] == 'prepare')
        self.assertEqual(prep[3:], ['--mods', '', '--load', 'C5H-test5021-MPK-T0019', '--restore-first'])
        self.assertEqual(launch_env.read_text(), 'shell_composite.dll')         # the modpack composite's slot
        self.assertTrue(any(c[2] == 'dismiss' for c in self.called('frontend')))
        self.assertTrue(any(c[-1] == 'UI.ExitGame()' for c in self.called('lua')))
        self.assertTrue((self.tmp / 'stopped').exists())                        # cli.py stop: the front end back
        self.assertEqual(len(self.released()), 1)
        self.assert_back()
        # the installer built into this stage's own folder, never hi_journey's
        self.assertTrue(str(o['installer']['exe']).startswith(str(self.work / 'modpack' / 'installer')))

    def test_mods_profile_instance_skipped_and_released(self):
        self.tearDown()
        self.PROFILE = 'mods548'
        self.setUp()
        self.go()
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('not modpack548', e['outputs']['skipped_why'])
        self.assertEqual(len(self.released()), 1)
        self.assertEqual(self.called('run_installer'), [])

    def test_no_instance_skipped(self):
        os.environ['FAKE_NO_INSTANCE'] = '1'
        self.go()
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('no modpack-form test instance', e['outputs']['skipped_why'])


class Failures(Base):
    def failed(self):
        self.assertNotEqual(self.go(), 0)
        e = self.entry()
        self.assertEqual(e['status'], 'failed', e)
        return e

    def test_load_failure(self):
        os.environ['FAKE_LOAD'] = 'never'
        os.environ['C5_RELEASE_MODPACK_LOAD_TIMEOUT_S'] = '3'                 # waited out in full: keep it short
        e = self.failed()
        self.assertIn('did not load', e['error'])
        self.assertIn('within 3 s (NOT IN GAME [front_end]', e['error'])     # the limit, not a box or a stop
        self.assertFalse(e['outputs'].get('play', {}).get('loaded', True))
        self.assertTrue((self.tmp / 'stopped').exists())                        # stopped by the harness
        self.assertTrue(e['outputs']['removal']['ok'])
        self.assert_back()

    def test_crash_during_the_turn(self):
        os.environ['FAKE_CRASH_AT'] = 'wait'
        e = self.failed()
        self.assertIn('CRASHED', e['error'])
        self.assertIn('EXCEPTION_ACCESS_VIOLATION', e['error'])
        self.assertTrue((self.tmp / 'restored').exists())                       # the front end put back
        self.assertTrue(e['outputs']['removal']['ok'])
        self.assert_back()

    def test_crash_at_the_exit(self):
        os.environ['FAKE_CRASH_AT'] = 'exit'
        e = self.failed()
        self.assertIn('CRASHED at its exit', e['error'])

    def test_box_leaves_the_game_running_and_keeps_the_claim(self):
        os.environ['FAKE_BOX_AT'] = 'end'
        e = self.failed()
        self.assertIn('BOX is open', e['error'])
        self.assertIn('REMOVAL ON', e['error'])
        self.assertIn('is KEPT', e['error'])
        self.assertEqual(self.released(), [])
        self.assertFalse((self.tmp / 'stopped').exists())                       # never stopped over a box
        self.assertTrue((self.tmp / 'running').exists())

    def test_dirty_removal_in_my_games_keeps_the_claim(self):
        os.environ['FAKE_REMOVE_LEAVES'] = '1'
        e = self.failed()
        self.assertIn('REMOVAL ON', e['error'])
        self.assertIn('VP64Options.ini', e['error'])
        self.assertEqual(e['outputs']['verdict'], 'failed')
        self.assertEqual(self.released(), [])

    def test_dirty_removal_in_assets_dlc_keeps_the_claim(self):
        os.environ['FAKE_DLC_LEAVES'] = '1'
        e = self.failed()
        self.assertIn('Assets/DLC', e['error'])
        self.assertIn('VP_MODPACK\\\\VP64', json.dumps(e['outputs']['removal']['dlc_tree']))
        rm = e['outputs']['removal']
        self.assertFalse(rm['claim_released'])                                  # re-taken after _remove released it
        claims = [c for c in self.called('pool') if c[0] == 'claim']
        self.assertEqual(len(claims), 2)
        self.assertEqual(claims[1][claims[1].index('--name') + 1], NAME)

    def test_other_form_recorded_fails_and_removes(self):
        os.environ['FAKE_FORM'] = 'mods'
        e = self.failed()
        self.assertIn("form 'mods', not modpack", e['error'])
        self.assertEqual(self.called('cli'), [])                                # nothing launched
        self.assertTrue(e['outputs']['removal']['ok'])
        self.assert_back()

    def test_turn_that_does_not_advance_fails(self):
        os.environ['FAKE_NO_ADVANCE'] = '1'
        e = self.failed()
        self.assertIn('the turn did not advance', e['error'])
        p = e['outputs']['play']
        self.assertEqual((p['loaded'], p['turn_from'], p['turn_to']), (True, 19, 19))   # failed at the turn, not before

    def test_exit_that_hangs_fails_and_stops(self):
        os.environ['FAKE_EXIT_HANGS'] = '1'
        e = self.failed()
        self.assertIn('after UI.ExitGame()', e['error'])
        self.assertTrue((self.tmp / 'stopped').exists())

    def test_install_failure_plays_nothing(self):
        os.environ['FAKE_INSTALL_RC'] = '4'
        e = self.failed()
        self.assertIn('install on', e['error'])
        self.assertEqual(self.called('cli'), [])

    def test_missing_save_installs_nothing(self):
        (self.user / SAVE).unlink()
        e = self.failed()
        self.assertIn('is not on', e['error'])
        self.assertEqual(self.called('run_installer'), [])
        self.assertEqual((self.inst / INST_DLL).read_bytes(), b'the instance own vp dll')

    def roll_autosaves(self):
        # 5024's live check (2026-11-22): c5h-new-mpk's autosaves had rolled to 0020-0031, AutoSave_0019 was gone
        (self.user / SAVE).unlink()
        auto = self.user / 'Saves' / 'single' / 'auto'
        for n, body in ((20, b'turn 20'), (31, b'turn 31 newest'), (9, b'turn 9')):
            (auto / f'AutoSave_{n:04d} BC-3200.Civ5Save').write_bytes(body)
        (auto / 'AutoSave_Post_0040 BC-2400.Civ5Save').write_bytes(b'a Post save is never taken')
        return auto

    def test_rolled_default_save_falls_back_to_the_newest_autosave(self):
        self.roll_autosaves()
        self.assertEqual(self.go(), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertEqual(o['play']['save']['sha256'], sha(b'turn 31 newest'))
        self.assertTrue(o['save_fallback'].endswith('AutoSave_0031 BC-3200.Civ5Save'))

    def test_a_named_missing_save_still_fails(self):
        # negative control: a save NAMED by C5_RELEASE_MODPACK_SAVE that is missing fails, newer autosaves or not
        self.roll_autosaves()
        os.environ['C5_RELEASE_MODPACK_SAVE'] = 'Saves\\single\\auto\\AutoSave_0019 BC-3240.Civ5Save'
        try:
            e = self.failed()
        finally:
            del os.environ['C5_RELEASE_MODPACK_SAVE']
        self.assertIn('is not on', e['error'])
        self.assertNotIn('save_fallback', e['outputs'])

    def test_custom_tools_must_name_every_tool(self):
        del self.tools['lua']
        e = self.failed()
        self.assertIn("'lua'", e['error'])
        self.assertEqual(self.called('pool'), [])


SAVE55 = 'Saves/single/MPK55-STD-T0019.Civ5Save'


class VP55(Base):
    """Ticket 0779 (D259): a take of a VP 5.5 tag claims a modpack55 instance and plays its named turn-19 save; the
    environment settings still override; other tags keep modpack548 and the old autosave (Pass above)."""
    PROFILE = 'modpack55'
    TAG = 'Release-5.5'

    def make_instance(self):
        Base.make_instance(self)
        (self.user / SAVE55).write_bytes(b'turn 19 save, VP 5.5 modpack form')

    def test_a_5_5_take_claims_modpack55_and_plays_the_5_5_save(self):
        self.assertEqual(self.go(), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertEqual((o['verdict'], o['vp'], o['profile']), ('passed', '5.5', 'modpack55'))
        self.assertEqual(o['save_setting'], SAVE55.replace('/', '\\'))
        self.assertEqual(o['play']['save']['sha256'], sha(b'turn 19 save, VP 5.5 modpack form'))
        self.assertNotIn('save_fallback', o)
        claim = next(c for c in self.called('pool') if c[0] == 'claim')
        self.assertEqual(claim[claim.index('--profile') + 1], 'modpack55')
        self.assert_back()

    def test_the_tag_forms_of_5_5_and_others(self):
        m = tr.load_stage_module('modpack', TIER_STAGES)
        for tag in ('Release-5.5', 'Release-5.5.0', 'Release-5.5-beta5'):
            self.assertEqual(m.vp_defaults(type('C', (), {'tag': tag})())[:2], ('5.5', 'modpack55'), tag)
        for tag in ('Release-5.4.8', 'Release-5.5.1', 'Release-9.9.9', None):
            self.assertEqual(m.vp_defaults(type('C', (), {'tag': tag})()),
                             (None, 'modpack548', m.SAVE_DEFAULT), tag)
        self.assertNotIn('vp', m.settings(type('C', (), {'tag': 'Release-5.4.8'})()))   # 5.4.8 inputs unchanged

    def test_a_modpack548_instance_is_skipped_on_a_5_5_take(self):
        self.tearDown()
        self.PROFILE = 'modpack548'
        self.setUp()
        self.go()
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('not modpack55', e['outputs']['skipped_why'])
        self.assertEqual(self.called('run_installer'), [])

    def test_the_environment_still_overrides(self):
        os.environ['C5_RELEASE_MODPACK_SAVE'] = 'Saves\\single\\auto\\AutoSave_0019 BC-3240.Civ5Save'
        try:
            self.assertEqual(self.go(), 0, self.lines[-5:])
        finally:
            del os.environ['C5_RELEASE_MODPACK_SAVE']
        o = self.entry()['outputs']
        self.assertEqual(o['play']['save']['sha256'], sha(b'turn 19 autosave, modpack form'))

    def test_a_missing_5_5_save_falls_back_to_the_newest_autosave(self):
        (self.user / SAVE55).unlink()
        self.assertEqual(self.go(), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertTrue(o['save_fallback'].endswith('AutoSave_0019 BC-3240.Civ5Save'))


if __name__ == '__main__':
    unittest.main()
