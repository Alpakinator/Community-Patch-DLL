"""Hermetic tests of the validated tier of tools/release/take_release.py (issue 3506): the real stage modules
refsave, instance, wholegames and report (stages/validated.py's registry) against a fake testbed, a fake instance and
fake tools (tests/fake_validated_tool.py). The candidate stages are recorded as ok in the state beforehand. No gate,
lock, game, server, testbed or instance is touched; nothing is written outside a temporary folder.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_validated_tier*.py" -v
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
import take_release as tr  # noqa: E402
import descriptor  # noqa: E402

PY = sys.executable
FAKE = HERE / 'fake_validated_tool.py'
VTOOLS = ['quietlock', 'run_server', 'tb', 'pool', 'provision', 'arm', 'frontend', 'visualcheck', 'pack', 'civ5save']
TAG = 'Release-9.9.9'
DB_SHA = 'bd9b8aa1c2a83cff0a935dafc6239a509d3c331a4ba871d20682b6aaddfadd6f'
USER = "Documents/My Games/Sid Meier's Civilization 5"


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


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


class Fixture(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.calls = self.tmp / 'calls.txt'
        self.env_keys = []
        for k in [k for k in os.environ if k.startswith(('FAKE_', 'C5_RELEASE_'))]:
            os.environ.pop(k)
        self.setenv(FAKE_CALLS=str(self.calls), FAKE_DIR=str(self.tmp), FAKE_DESC_DB=DB_SHA)
        self.setenv(C5_RELEASE_INSTANCE='c5h-t', C5_RELEASE_INSTANCE_WAIT='0')   # the fake instance; no claim wait
        # ticket 0640: the instance stage claims with the take's ticket and address
        self.setenv(C5_RELEASE_TICKET='0640', C5_RELEASE_ADDRESS='impl-test')
        # issue 4506: the hi_journey stage (validated registry) finds no start saves here, so it is skipped before any
        # claim and touches nothing (its own tests are test_hi_journey.py); skipped counts as reached for the tier
        self.setenv(C5_RELEASE_HI_SAVES=str(self.tmp / 'no-hi-journey-saves'))
        self.tools = {**tr.default_tools(), **{n: [PY, '-I', str(FAKE), n] for n in VTOOLS}}
        self.desc = self.make_desc()
        self.make_builds()
        self.make_instance()
        self.tiny = self.tmp / 'inputs' / 'TINY.Civ5Save'
        self.tiny.parent.mkdir(parents=True, exist_ok=True)
        self.tiny.write_bytes(b'tiny save')
        self.lines = []
        self.seed_state()

    def tearDown(self):
        for k in self.env_keys:
            os.environ.pop(k, None)
        self._td.cleanup()

    def setenv(self, **kv):
        for k, v in kv.items():
            os.environ[k] = v
            self.env_keys.append(k)

    def make_desc(self, **over):
        save = self.tmp / 'saves' / 'REF-HUGE_0350.Civ5Save'
        save.parent.mkdir(exist_ok=True)
        save.write_bytes(b'huge save bytes')
        d = json.loads((tr.HERE / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
        d.update(tag=TAG, reference_saves={'huge': {'path': save.as_posix(), 'sha256': sha(b'huge save bytes'),
                                                    'turn': 350}},
                 # ticket 0574: the fake run_d10's rows, not the real descriptor's (which follow the pins)
                 reference_counts={'tiny': 30666, 'standard': 351496, 'huge': 683123})
        d.update(over)
        return descriptor.parse(d, 'test', expect_tag=TAG)

    def make_builds(self):
        b = self.tmp / 'layout'
        (b / 'x64' / 'bin').mkdir(parents=True)
        (b / 'x64' / 'proxy' / 'bin').mkdir(parents=True)
        (b / 'x64' / 'bin' / 'CvGameCore_Expansion2.dll').write_bytes(b'x64 dll')
        (b / 'x64' / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll').write_bytes(b'proxy dll')
        (b / 'i0').mkdir()
        (b / 'i0' / 'CvGameCore_Expansion2.dll').write_bytes(b'x86 plain')
        (b / 'i0ia').mkdir()
        (b / 'i0ia' / 'CvGameCore_Expansion2.dll').write_bytes(b'x86 instance-aware')
        self.build = {'x64_commit': '04b1211b8' + '0' * 31, 'i0_commit': '26234371e' + '0' * 31,
                      'x64_layout': str(b / 'x64'), 'x64_bin': str(b / 'x64' / 'bin'),
                      'proxy_dll': str(b / 'x64' / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll'),
                      'i0_dll': str(b / 'i0' / 'CvGameCore_Expansion2.dll'),
                      'i0ia_dll': str(b / 'i0ia' / 'CvGameCore_Expansion2.dll')}
        pl = self.work / 'inputs' / 'player-x86'
        pl.mkdir(parents=True)
        (pl / 'CvGameCore_Expansion2.dll').write_bytes(b'player dll')
        (self.tmp / 'purity.tsv').write_text('# vp64-purity 1\n')

    def make_instance(self, name='c5h-t', mods=None):
        inst = self.tmp / 'instances' / name
        cp = inst / USER / 'MODS' / '(1) Community Patch'
        cp.mkdir(parents=True)
        (cp / 'CvGameCore_Expansion2.dll').write_bytes(b'the instance dll')
        for m in (mods if mods is not None else self.desc.mods):
            d = inst / USER / 'MODS' / m.folder
            d.mkdir(parents=True, exist_ok=True)
            (d / f'{m.folder} (v {m.version}).modinfo').write_text('<Mod/>')
        (inst / 'instance.json').write_text(json.dumps({
            'name': name, 'user_dir': USER.replace('/', '\\'),
            'dll': {'path': USER.replace('/', '\\') + '\\MODS\\(1) Community Patch\\CvGameCore_Expansion2.dll'}}))
        return inst

    def seed_state(self, without=()):
        outs = {'build': self.build, 'purity': {'purity_tsv': str(self.tmp / 'purity.tsv')},
                'pack': {'pack': str(self.tmp / 'pack')}}
        st = {'format': tr.FORMAT, 'tag': TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': []}
        for name, cl, _o in tr.CANDIDATE:
            if name in without:
                continue
            st['stages'].append({'name': name, 'cl_step': cl, 'status': 'ok', 'outputs': outs.get(name, {}),
                                 'inputs_sha': 'seed'})
        self.work.mkdir(parents=True, exist_ok=True)
        (self.work / 'state.json').write_text(json.dumps(st))

    def driver(self, only=None):
        stages = tr.tier_stages('validated')[len(tr.CANDIDATE):]
        d = tr.Driver(TAG, self.work, self.desc, {'tier': 'validated', 'who': 'test3506', 'gate_wait': 0}, stages,
                      tools=self.tools, out=self.lines.append)
        inst = d.module('instance')
        inst.TINY_SAVE, inst.TINY_SHA256 = self.tiny, sha(b'tiny save')
        return d

    def go(self, frm=None, only=None):
        return self.driver().run(frm=frm, only=only)

    def state(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}

    def called(self, name):
        if not self.calls.exists():
            return []
        return [ln[len(name) + 1:] for ln in self.calls.read_text().splitlines() if ln.startswith(name + ' ')]

    def state_ctx(self):
        """A stand-in ctx with the test descriptor (for refsave.modpack_dlc)."""
        return type('C', (), {'desc': self.desc})()

    def report(self):
        return json.loads((self.work / 'validated-report.json').read_text(encoding='utf-8'))

    def text(self):
        return '\n'.join(self.lines)


class TinySavePath(unittest.TestCase):
    """Ticket 0193 (D133): the Tiny save is read from a C: or F: copy first; D: only as the last, a storage read."""

    def setUp(self):
        from stages import instance
        self.inst = instance

    def test_candidates_order_and_d_last(self):
        roots = [str(p)[:2].upper() for p in self.inst.TINY_SAVE_CANDIDATES]
        self.assertEqual(roots, ['C:', 'F:', 'D:'])
        self.assertTrue(all(p.name == self.inst.TINY_SAVE_NAME for p in self.inst.TINY_SAVE_CANDIDATES))

    def test_first_existing_then_last_then_env(self):
        with tempfile.TemporaryDirectory() as t:
            a, b, c = (Path(t) / n for n in ('a.Civ5Save', 'b.Civ5Save', 'c.Civ5Save'))
            self.assertEqual(self.inst.tiny_save_path({}, (a, b, c)), c)
            b.write_bytes(b'x')
            c.write_bytes(b'x')
            self.assertEqual(self.inst.tiny_save_path({}, (a, b, c)), b)
            self.assertEqual(self.inst.tiny_save_path({'C5_RELEASE_TINY_SAVE': str(a)}, (a, b, c)), a)

    def test_the_release_picks_its_own_tiny_save(self):
        # ticket 0766 (D259): a 5.5 take plays REL55-TINY_0008 with its own hash (no 5.4.8 save loads on 5.5); 5.4.x
        # tags and other names keep TINY_SAVE / TINY_SHA256; C5_RELEASE_TINY_SAVE still names the path
        from types import SimpleNamespace as NS
        I = self.inst
        for tag in ('Release-5.5', 'Release-5.5.0', 'Release-5.5-beta5'):
            self.assertEqual(I.vp_of_tag(tag), '5.5', tag)
        for tag in ('Release-5.4.8', 'Release-5.4.8-beta4', 'Release-5.5.1', 'master', None):
            self.assertIsNone(I.vp_of_tag(tag), tag)
        self.assertEqual(I.tiny_save(NS(tag='Release-5.4.8'), {}), (I.TINY_SAVE, I.TINY_SHA256))
        p, h = I.tiny_save(NS(tag='Release-5.5'), {})
        self.assertEqual(p.name, 'REL55-TINY_0008.Civ5Save')
        self.assertEqual(h, '6d1f3a1016624dc97bdd48f8f2a54e180ebb2237599e3119b653b19e968c0f57')
        self.assertNotEqual(h, I.TINY_SHA256)
        with tempfile.TemporaryDirectory() as t:
            named = Path(t) / 'x.Civ5Save'
            p, h = I.tiny_save(NS(tag='Release-5.5'), {'C5_RELEASE_TINY_SAVE': str(named)})
            self.assertEqual((p, h[:8]), (named, '6d1f3a10'))


class Registry(unittest.TestCase):
    def test_validated_tier_is_candidate_plus_registry(self):
        names = [s[0] for s in tr.tier_stages('validated')]
        self.assertEqual(names[:len(tr.CANDIDATE)], [s[0] for s in tr.CANDIDATE])
        self.assertEqual(names[len(tr.CANDIDATE):], ['refsave', 'instance', 'wholegames', 'hi_journey', 'report'])
        mod = tr.load_stage_module('validated', tr.STAGES_DIR)
        self.assertFalse(hasattr(mod, 'run'), 'validated.py is a registry, not a stage')
        for n in names[len(tr.CANDIDATE):]:
            m = tr.load_stage_module(n, tr.STAGES_DIR)
            self.assertTrue(callable(getattr(m, 'run', None)), n)


class WholeTier(Fixture):
    def test_all_pass_is_validated(self):
        rc = self.go()
        self.assertEqual(rc, 0, self.text())
        st = self.state()
        for n in ('refsave', 'instance', 'wholegames', 'report'):
            self.assertEqual(st[n]['status'], 'ok', (n, st[n]))
        self.assertEqual(st['refsave']['outputs']['origin'], 'descriptor')
        rep = self.report()
        self.assertEqual(rep['verdict'], 'validated')
        self.assertTrue((self.work / 'validated-report.md').read_text(encoding='utf-8').count('**Verdict: validated**'))
        # the instance: laid, played with the test pack, restored, verified, released
        prov = self.called('provision')
        self.assertTrue(any(c.startswith('set-dll c5h-t --dll') for c in prov))
        self.assertTrue(any(c.startswith('set-proxy c5h-t --original --runs-to') for c in prov), prov)
        self.assertEqual(prov[-1], 'verify c5h-t --quick')   # D110: no full verify
        self.assertEqual(sum(c.startswith('set-dll') for c in prov), 2, prov)   # the release's DLL, then the kept one
        arms = st['instance']['outputs']['arms']
        self.assertTrue(arms['tiny']['ok'] and arms['huge']['ok'])
        self.assertEqual(arms['huge']['verified'], 1234)
        self.assertIn('pack=1', ' '.join(self.called('arm')))
        self.assertTrue(json.loads((self.work / 'instance/ev/rel-huge/arm.json').read_text())['packed'])
        self.assertEqual(self.called('visualcheck'), [])   # D114: no picture from this stage
        self.assertNotIn('--shot', ' '.join(self.called('arm')))
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))
        inst = self.tmp / 'instances' / 'c5h-t' / USER
        self.assertEqual((inst / 'MODS/(1) Community Patch/CvGameCore_Expansion2.dll').read_bytes(),
                         b'the instance dll')
        self.assertEqual(list((inst / 'ModdedSaves' / 'single').glob('*')), [])
        # whole games: one job per host, both hosts compared
        subs = [c for c in self.called('tb') if ' submit ' in c]
        self.assertEqual(len(subs), 2)
        self.assertIn('--compare tc-x64:tc-x86', subs[0])
        self.assertNotIn('--quiet', ' '.join(subs))
        self.assertEqual(st['wholegames']['outputs']['parity'], 'passed')
        self.assertEqual({g['parity'] for g in st['wholegames']['outputs']['games']}, {'identical'})

    def test_gate_refusal_passes_exit_3(self):
        self.setenv(FAKE_GATE_REFUSE='1')
        self.assertEqual(self.go(), 3, self.text())
        self.assertIn('REFUSED refsave', self.text())


class RefSave(Fixture):
    def test_old_save_fails_to_load_new_game_made(self):
        self.setenv(FAKE_RS_LOAD_FAIL='inputs')
        self.assertEqual(self.go(only='refsave'), 0, self.text())
        o = self.state()['refsave']['outputs']
        self.assertEqual(o['origin'], 'new')
        self.assertFalse(o['p10_old']['ok'])
        self.assertTrue(o['p10_new']['ok'])
        self.assertIn('reference_saves.huge', o['descriptor_update'])
        self.assertTrue(Path(o['save']).is_file())
        game = [c for c in self.called('run_server') if 'continents-huge-fixed-seed.json' in c]
        self.assertEqual(len(game), 1)
        self.assertIn('--dll-log-flags 0', game[0])
        self.assertIn('--turns 350', game[0])
        self.assertIn('refsave-victories-off.lua', game[0])        # ticket 0175: every victory off
        from stages import refsave
        modpack = refsave.modpack_dlc(self.state_ctx())
        self.assertIn('--dlc-exclude ' + ','.join([refsave.VP_BLOCKED_DLC] + modpack), game[0])
        self.assertEqual(o['game']['victories_off'], '5 of 5')
        self.assertEqual(o['p10_new']['victories_valid'], '0 of 5')
        new_load = [c for c in self.called('run_server') if '--load' in c and 'REF-HUGE' in c]
        self.assertEqual(len(new_load), 1)
        self.assertIn('refsave-victories-report.lua', new_load[0])
        old_load = [c for c in self.called('run_server') if '--load' in c and 'REF-HUGE' not in c]
        self.assertNotIn('victories', old_load[0])                # the old save keeps its own victories

    def test_victory_probes_are_lua_files_of_the_repository(self):
        from stages import refsave
        self.assertIn('Game.SetVictoryValid(v.ID, false)', refsave.VICTORIES_OFF.read_text(encoding='utf-8'))
        self.assertIn('refsave victories valid:', refsave.VICTORIES_REPORT.read_text(encoding='utf-8'))

    def test_new_save_listing_the_modpack_dlc_fails(self):
        from stages import refsave
        modpack = refsave.modpack_dlc(self.state_ctx())
        self.assertTrue(modpack, 'the descriptor game folder has no VP_MODPACK package to test with')
        self.setenv(FAKE_RS_LOAD_FAIL='inputs', FAKE_RS_SAVE_DLC=modpack[0])
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        self.assertIn('lists the modpack DLC', self.state()['refsave']['error'])

    def test_new_save_gets_the_release_mods_in_its_header(self):
        # 0175: the server writes an empty mod list; the EXE then turns the mods off for the load and aborts
        self.setenv(FAKE_RS_LOAD_FAIL='inputs')
        self.assertEqual(self.go(only='refsave'), 0, self.text())
        o = self.state()['refsave']['outputs']
        want = [[m.guid.lower(), m.version, m.folder] for m in self.desc.mods]
        self.assertEqual(o['header_mods'], want)
        from stages import refsave
        self.assertEqual(refsave.header_info(Path(o['save']).read_bytes())['mods'], want)
        self.assertTrue(Path(o['save']).read_bytes().endswith(b' at 350'))      # the body is unchanged
        self.assertEqual(len(self.called('civ5save')), 2)                         # unpack, pack

    def test_header_rewrite_failure_fails(self):
        self.setenv(FAKE_RS_LOAD_FAIL='inputs', FAKE_C5S_FAIL='1')
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        self.assertIn('civ5save unpack', self.state()['refsave']['error'])

    def test_unreadable_header_fails(self):
        run = self.tmp / 'adopt'
        run.mkdir()
        (run / 'server-report.txt').write_text('result: passed; turns 0 -> 350 (350 played); Lua errors 0\n')
        (run / 'W5-HUGE_0350.Civ5Save').write_bytes(b'not a save')
        (run / 'Logs').mkdir()
        (run / 'Logs' / 'Lua.log').write_text('Main: refsave victories off: 5 of 5\n')
        self.setenv(C5_RELEASE_REFSAVE='new', C5_RELEASE_REFSAVE_RUN=str(run))
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        self.assertIn('header cannot be read', self.state()['refsave']['error'])

    def test_new_game_without_victories_off_line_fails(self):
        self.setenv(FAKE_RS_LOAD_FAIL='inputs', FAKE_RS_PROBE_SILENT='1')
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        st = self.state()['refsave']
        self.assertEqual(st['status'], 'failed')
        self.assertIn('victories not all off', st['error'])
        self.assertFalse([c for c in self.called('run_server') if 'REF-HUGE' in c and '--load' in c])

    def test_new_save_with_a_valid_victory_fails(self):
        self.setenv(FAKE_RS_LOAD_FAIL='inputs', FAKE_RS_VALID='1')
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        st = self.state()['refsave']
        self.assertEqual(st['status'], 'failed')
        self.assertIn('victories valid in the loaded save: 1 of 5', st['error'])
        self.assertIn('candidate, validated tier not reached (refsave failed', self.report()['verdict'])

    def test_new_game_that_fails_is_a_failure(self):
        self.setenv(FAKE_RS_LOAD_FAIL='inputs', FAKE_RS_GAME_FAIL='1')
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        self.assertEqual(self.state()['refsave']['status'], 'failed')
        self.assertIn('candidate, validated tier not reached (refsave failed', self.report()['verdict'])

    def test_hash_mismatch_is_a_stop(self):
        Path(self.desc.reference_saves['huge'].path).write_bytes(b'other bytes')
        self.assertEqual(self.go(only='refsave'), 10, self.text())
        self.assertIn('the descriptor says', self.state()['refsave']['stop']['what'])

    def test_no_save_in_descriptor_makes_a_new_game(self):
        self.desc = self.make_desc(reference_saves={})
        self.assertEqual(self.go(only='refsave'), 0, self.text())
        o = self.state()['refsave']['outputs']
        self.assertEqual(o['origin'], 'new')
        self.assertIn('names no huge reference save', o['why_new'])

    def test_mode_new_and_adopted_run(self):
        run = self.tmp / 'adopt'
        run.mkdir()
        (run / 'server-report.txt').write_text('result: passed; turns 0 -> 350 (350 played); Lua errors 0\n')
        (run / 'W5-HUGE_0350.Civ5Save').write_bytes(fake_save(body=b'adopted'))
        (run / 'Logs').mkdir()
        (run / 'Logs' / 'Lua.log').write_text('Main: refsave victories off: 5 of 5\n')
        self.setenv(C5_RELEASE_REFSAVE='new', C5_RELEASE_REFSAVE_RUN=str(run))
        self.assertEqual(self.go(only='refsave'), 0, self.text())
        o = self.state()['refsave']['outputs']
        self.assertEqual(o['origin'], 'new')
        self.assertTrue(Path(o['save']).read_bytes().endswith(b'adopted'))
        self.assertEqual(o['header_mods'], [[m.guid.lower(), m.version, m.folder] for m in self.desc.mods])
        self.assertFalse([c for c in self.called('run_server') if 'fixed-seed' in c])

    def test_adopted_run_with_victories_on_fails(self):
        # W5.2's run (every victory on) cannot be adopted any more: its Lua.log has no line of the off-probe
        run = self.tmp / 'adopt'
        run.mkdir()
        (run / 'server-report.txt').write_text('result: passed; turns 0 -> 350 (350 played); Lua errors 0\n')
        (run / 'W5-HUGE_0350.Civ5Save').write_bytes(fake_save(body=b'adopted'))
        self.setenv(C5_RELEASE_REFSAVE='new', C5_RELEASE_REFSAVE_RUN=str(run))
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        self.assertIn('victories not all off', self.state()['refsave']['error'])

    def test_adopted_run_at_another_turn_fails(self):
        run = self.tmp / 'adopt'
        run.mkdir()
        (run / 'server-report.txt').write_text('result: passed; turns 0 -> 200 (200 played); Lua errors 0\n')
        (run / 'X.Civ5Save').write_bytes(fake_save(body=b'adopted'))
        self.setenv(C5_RELEASE_REFSAVE='new', C5_RELEASE_REFSAVE_RUN=str(run))
        self.assertEqual(self.go(only='refsave'), 1, self.text())
        self.assertIn('ended at turn 200', self.state()['refsave']['error'])

    def test_bad_mode_fails(self):
        self.setenv(C5_RELEASE_REFSAVE='sometimes')
        self.assertEqual(self.go(only='refsave'), 1)


def I_stage():
    from stages import instance
    return instance


class Instance(Fixture):
    def setUp(self):
        super().setUp()
        self.assertEqual(self.go(only='refsave'), 0, self.text())

    def test_no_free_instance_is_a_stop(self):
        self.setenv(FAKE_POOL_NONE='1')
        self.assertEqual(self.go(only='instance'), 10, self.text())
        self.assertIn('no test instance could be claimed', self.state()['instance']['stop']['what'])
        self.assertIn('instance stop', self.report()['verdict'])
        self.assertEqual(self.called('provision'), [])

    def test_claim_names_the_takes_ticket_and_address(self):
        """Ticket 0640 (a): the claim carries --ticket and --address (D155 part 4), recorded in the outputs."""
        self.assertEqual(self.go(only='instance'), 0, self.text())
        claims = [c for c in self.called('pool') if c.startswith('claim')]
        self.assertEqual(len(claims), 1)
        self.assertIn('--ticket 0640 --address impl-test', claims[0])
        o = self.state()['instance']['outputs']
        self.assertEqual((o['ticket'], o['address']), ('0640', 'impl-test'))

    def test_claim_falls_back_to_the_hi_journey_settings(self):
        for k in ('C5_RELEASE_TICKET', 'C5_RELEASE_ADDRESS'):
            os.environ.pop(k)
        self.setenv(C5_RELEASE_HI_TICKET='0626', C5_RELEASE_HI_ADDRESS='dispatcher-B')
        self.assertEqual(self.go(only='instance'), 0, self.text())
        claims = [c for c in self.called('pool') if c.startswith('claim')]
        self.assertIn('--ticket 0626 --address dispatcher-B', claims[0])
        self.setenv(C5_RELEASE_TICKET='0640')               # the instance stage's own name comes first
        self.assertEqual(I_stage().claim_identity()[0]['ticket'], '0640')

    def test_claim_without_ticket_or_address_is_refused_naming_the_settings(self):
        """Ticket 0640 (a), negative: without either setting nothing is claimed or laid, the stop names the settings,
        and the wait does not run (C5_RELEASE_INSTANCE_WAIT is long here)."""
        for drop, named in (('C5_RELEASE_TICKET', 'C5_RELEASE_TICKET or C5_RELEASE_HI_TICKET'),
                            ('C5_RELEASE_ADDRESS', 'C5_RELEASE_ADDRESS or C5_RELEASE_HI_ADDRESS')):
            with self.subTest(drop=drop):
                self.tearDown()
                self.setUp()
                os.environ.pop(drop)
                self.setenv(C5_RELEASE_INSTANCE_WAIT='60')
                self.assertEqual(self.go(only='instance'), 10, self.text())
                st = self.state()['instance']['stop']
                self.assertIn("claims only with the take's ticket and address", st['what'])
                self.assertIn(f'not set: {named}', st['what'])
                self.assertIn('C5_RELEASE_TICKET', st['unblock'])
                self.assertIn('C5_RELEASE_ADDRESS', st['unblock'])
                self.assertEqual([c for c in self.called('pool') if c.startswith('claim')], [])
                self.assertEqual(self.called('provision'), [])

    def test_pool_refusal_of_the_ticket_ends_the_wait_at_once(self):
        """Ticket 0640 (a), negative: an unknown ticket or a dead address is not retried for the claim wait."""
        for refusal in ('unknown ticket: 9999 is neither on the queue nor on main',
                        'dispatcher-X is not a live address (never acted)'):
            with self.subTest(refusal=refusal):
                self.tearDown()
                self.setUp()
                self.setenv(FAKE_POOL_REFUSE=refusal, C5_RELEASE_INSTANCE_WAIT='60')
                self.assertEqual(self.go(only='instance'), 10, self.text())
                st = self.state()['instance']['stop']
                self.assertIn('no wait cures that', st['what'])
                self.assertIn('live address', st['unblock'])
                self.assertEqual(len([c for c in self.called('pool') if c.startswith('claim')]), 1)
                self.assertEqual(self.called('provision'), [])

    def test_a_list_is_tried_in_order(self):
        self.make_instance('c5h-b')
        self.setenv(FAKE_POOL_BUSY='c5h-a', C5_RELEASE_INSTANCE='c5h-a,c5h-b')
        self.assertEqual(self.go(only='instance'), 0, self.text())
        claims = [c for c in self.called('pool') if c.startswith('claim')]
        self.assertEqual([c.split('--name ')[1].split()[0] for c in claims], ['c5h-a', 'c5h-b'])
        self.assertEqual(self.state()['instance']['outputs']['instance'], 'c5h-b')

    def test_default_is_the_pools_choice(self):
        os.environ.pop('C5_RELEASE_INSTANCE')
        self.assertEqual(self.go(only='instance'), 0, self.text())
        claims = [c for c in self.called('pool') if c.startswith('claim')]
        self.assertEqual(len(claims), 1)
        self.assertNotIn('--name', claims[0])
        self.assertEqual(self.state()['instance']['outputs']['instance'], 'c5h-t')

    def test_hold_file_waits_before_the_claim(self):
        import threading
        from stages import instance as I
        I.HOLD_POLL_S = 0.2
        hold = self.work / I.HOLD_NAME
        hold.parent.mkdir(parents=True, exist_ok=True)
        hold.write_text('a window on the instances')
        timer = threading.Timer(2.0, hold.unlink)
        timer.start()
        try:
            self.assertEqual(self.go(only='instance'), 0, self.text())
        finally:
            timer.cancel()
        self.assertIn('HOLD-INSTANCE present (a window on the instances)', self.text())
        self.assertIn('HOLD-INSTANCE removed; claiming', self.text())

    def test_hold_file_and_stop_file_stop_without_a_claim(self):
        from stages import instance as I
        I.HOLD_POLL_S = 0.2
        (self.work / I.HOLD_NAME).write_text('held')
        (self.work / 'STOP').write_text('stop')
        rc = self.go(only='instance')
        (self.work / 'STOP').unlink()
        self.assertEqual(rc, 10, self.text())
        self.assertFalse([c for c in self.called('pool') if c.startswith('claim')])

    def test_no_free_instance_waits_then_stops(self):
        self.setenv(FAKE_POOL_NONE='1', C5_RELEASE_INSTANCE='c5h-x,c5h-t', C5_RELEASE_INSTANCE_WAIT='0.05')
        self.assertEqual(self.go(only='instance'), 10, self.text())
        claims = [c for c in self.called('pool') if c.startswith('claim')]
        self.assertEqual(len(claims), 4)          # two rounds over two names, 3 s apart
        self.assertIn('waited 0 min', self.state()['instance']['stop']['what'])
        self.assertEqual(self.called('provision'), [])

    def test_mismatch_is_a_stop_and_restores(self):
        self.setenv(FAKE_ARM_MISMATCH='huge')
        self.assertEqual(self.go(only='instance'), 10, self.text())
        e = self.state()['instance']
        self.assertIn('MISMATCHES 3', e['stop']['what'])
        self.assertTrue(e['outputs']['restore']['ok'])
        self.assertTrue(any(c.startswith('set-proxy c5h-t --original') for c in self.called('provision')))
        self.assertTrue(e['outputs']['claim_released'])

    def test_failed_verify_after_restore_is_a_failure(self):
        self.setenv(FAKE_PROV_VERIFY_FAIL='1')
        self.assertEqual(self.go(only='instance'), 1, self.text())
        self.assertIn('RESTORE OF c5h-t FAILED', self.state()['instance']['error'])
        self.assertIn('is KEPT', self.state()['instance']['error'])
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))   # the claim stays

    def test_other_mods_is_a_stop_and_nothing_laid(self):
        import shutil
        shutil.rmtree(self.tmp / 'instances' / 'c5h-t')
        self.make_instance(mods=[])
        self.assertEqual(self.go(only='instance'), 10, self.text())
        self.assertIn('lacks the release\'s mods', self.state()['instance']['stop']['what'])
        prov = self.called('provision')
        self.assertEqual([c for c in prov if c.startswith('set-')], [])
        self.assertTrue(prov[-1].startswith('verify'))
        self.assertTrue(any(c.startswith('release') for c in self.called('pool')))

    def test_dialog_open_leaves_the_game_alone(self):
        self.setenv(FAKE_ARM_RC='8', FAKE_ARM_RC_ARM='tiny')
        self.assertEqual(self.go(only='instance'), 1, self.text())
        e = self.state()['instance']
        self.assertIn('a dialog is open', e['error'])
        prov = self.called('provision')
        self.assertFalse(any(c.startswith('set-proxy c5h-t --original') for c in prov))
        self.assertFalse(any(c.startswith('verify') for c in prov))
        self.assertFalse(any(c.startswith('release') for c in self.called('pool')))
        self.assertEqual(len(self.called('arm')), 1)

    def test_without_pack_stage_runs_without_pack(self):
        self.seed_state(without=('pack',))
        self.assertEqual(self.go(only='refsave'), 0)
        self.assertEqual(self.go(only='instance'), 0, self.text())
        o = self.state()['instance']['outputs']
        self.assertIn('no pack', o['pack'])
        self.assertNotIn('pack=1', ' '.join(self.called('arm')))
        self.assertEqual(self.called('pack'), [])

    def test_set_proxy_failure_restores_the_dll(self):
        self.setenv(FAKE_PROV_SETPROXY_FAIL='1')
        self.assertEqual(self.go(only='instance'), 1, self.text())
        prov = self.called('provision')
        self.assertTrue(prov[-2].startswith('set-dll c5h-t --dll') and 'kept' in prov[-2], prov)
        dll = self.tmp / 'instances/c5h-t' / USER / 'MODS/(1) Community Patch/CvGameCore_Expansion2.dll'
        self.assertEqual(dll.read_bytes(), b'the instance dll')

    def test_gate_refusal_of_the_game_start_is_a_stop_and_restores(self):
        self.setenv(FAKE_ARM_REFUSE='1')
        self.assertEqual(self.go(only='instance'), 10, self.text())
        e = self.state()['instance']
        self.assertIn('a launch gate refused', e['stop']['what'])
        self.assertTrue(e['outputs']['restore']['ok'])
        self.assertEqual(len(self.called('arm')), 1)

    # issue 0310: an install from the fork's folders whose CP modinfo lacks a VFS import of its .civ5proj
    def cp_from_fork(self, project_in_instance=True):
        cp = self.tmp / 'instances/c5h-t' / USER / 'MODS/(1) Community Patch'
        info = next(cp.glob('*.modinfo'))
        info.write_bytes(b'<Mod>\r\n  <Files>\r\n    <File md5="X" import="1">Core Files/a.lua</File>\r\n'
                         b'  </Files>\r\n</Mod>\r\n')
        for rel in ('Core Files/a.lua', 'Gamecore/GamecoreInit.lua'):
            (cp / rel).parent.mkdir(parents=True, exist_ok=True)
            (cp / rel).write_bytes(b'-- ' + rel.encode())
        proj = ('<Project><ItemGroup>' + ''.join(
            f'<Content Include="{r}"><ImportIntoVFS>True</ImportIntoVFS></Content>'
            for r in ('Core Files\\a.lua', 'Gamecore\\GamecoreInit.lua')) + '</ItemGroup></Project>')
        if project_in_instance:
            (cp / 'Community Patch.civ5proj').write_text(proj, encoding='utf-8')
        else:   # the testdata stage's copy of the tag's folder has the project
            tag = self.tmp / 'testdata' / 'mods' / '(1) Community Patch'
            tag.mkdir(parents=True)
            (tag / 'Community Patch.civ5proj').write_text(proj, encoding='utf-8')
            st = json.loads((self.work / 'state.json').read_text())
            next(e for e in st['stages'] if e['name'] == 'testdata')['outputs'] = {'mods_dir': str(tag.parent)}
            (self.work / 'state.json').write_text(json.dumps(st))
        cache = self.tmp / 'instances/c5h-t' / USER / 'cache' / 'Civ5ModsDatabase.db'
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_bytes(b'cached mod file lists')
        return info, info.read_bytes(), cache

    def check_modinfo_fixed_and_restored(self, info, before, cache):
        self.assertEqual(self.go(only='instance'), 0, self.text())
        o = self.state()['instance']['outputs']
        cp = o['modinfo']['(1) Community Patch']
        self.assertEqual((cp['changed'], cp['added'], cp['project_files_missing']),
                         (True, ['Gamecore/GamecoreInit.lua'], []))
        self.assertIn('modinfo regenerated from the .civ5proj: (1) Community Patch', self.text())
        # what each arm's game read: the modinfo with the import, and no stale mods cache
        for arm in ('rel-tiny', 'rel-huge'):
            ev = Path(o['arms'][arm.split('-')[1]]['ev'])
            seen = (ev / 'modinfo-seen').read_bytes()
            self.assertIn(b'import="1">Gamecore/GamecoreInit.lua</File>\r\n  </Files>', seen)
            self.assertIn(b'import="1">Core Files/a.lua</File>', seen)
            self.assertEqual((ev / 'modsdb-seen').read_text(), 'False')
        # restored by hash after the run: the instance's modinfo and its mods cache as they were
        self.assertEqual(info.read_bytes(), before)
        self.assertEqual(cache.read_bytes(), b'cached mod file lists')
        done = ' '.join(o['restore']['done'])
        self.assertIn('(1) Community Patch modinfo back: restored', done)
        self.assertIn('Civ5ModsDatabase.db back: restored', done)

    def test_modinfo_of_a_fork_install_fixed_and_restored(self):
        self.check_modinfo_fixed_and_restored(*self.cp_from_fork())

    def test_modinfo_fixed_from_the_testdata_project(self):
        self.check_modinfo_fixed_and_restored(*self.cp_from_fork(project_in_instance=False))

    def test_modbuddy_install_modinfo_untouched(self):
        # the fixture's install has no .civ5proj and the testdata stage gave none: nothing compared, nothing changed
        self.assertEqual(self.go(only='instance'), 0, self.text())
        self.assertEqual(self.state()['instance']['outputs']['modinfo'], {})
        self.assertFalse((self.work / 'instance' / 'kept-modinfo' / 'Civ5ModsDatabase.db').exists())

    def test_missing_tiny_save_is_a_stop(self):
        self.tiny.unlink()
        self.assertEqual(self.go(only='instance'), 10, self.text())
        self.assertEqual(self.called('pool'), [])


class WholeGames(Fixture):
    def setUp(self):
        super().setUp()

    def test_known_exception_passes(self):
        self.setenv(FAKE_TB_KNOWN='1')
        self.assertEqual(self.go(only='wholegames'), 0, self.text())
        o = self.state()['wholegames']['outputs']
        self.assertEqual(o['parity'], 'passed')
        self.assertEqual({g['parity'] for g in o['games']}, {'identical but known'})
        self.assertIn('ReligionBeliefYields.IsReligionOwner (20 rows)', o['games'][0]['differences'])

    def test_difference_is_parity_failed(self):
        self.setenv(FAKE_TB_DIFF='sf')
        self.assertEqual(self.go(only='wholegames'), 10, self.text())
        e = self.state()['wholegames']
        self.assertEqual(e['outputs']['parity'], 'failed')
        self.assertIn('x64 != x86 in 1 of 4', e['stop']['what'])
        self.assertIn('CityYields', e['stop']['what'])
        self.assertEqual(self.report()['verdict'], 'candidate, parity failed')
        self.assertIn('candidate, parity failed', (self.work / 'validated-report.md').read_text(encoding='utf-8'))

    def test_known_column_plus_another_is_a_difference(self):
        self.setenv(FAKE_TB_KNOWN='1', FAKE_TB_DIFF='tc')
        self.assertEqual(self.go(only='wholegames'), 10, self.text())
        self.assertEqual(self.report()['verdict'], 'candidate, parity failed')

    def test_failed_run_is_a_stop_not_validated(self):
        self.setenv(FAKE_TB_RUNFAIL='sp-x64')
        self.assertEqual(self.go(only='wholegames'), 10, self.text())
        e = self.state()['wholegames']
        self.assertEqual(e['outputs']['parity'], 'incomplete')
        self.assertIn('did not finish', e['stop']['what'])
        self.assertNotEqual(self.report()['verdict'], 'validated')

    def test_submit_failure_is_a_failure(self):
        self.setenv(FAKE_TB_SUBMIT_FAIL='1')
        self.assertEqual(self.go(only='wholegames'), 1, self.text())

    def test_rerun_waits_for_the_same_jobs(self):
        self.setenv(FAKE_TB_FETCH_FAIL='1')
        self.assertEqual(self.go(only='wholegames'), 1, self.text())
        os.environ.pop('FAKE_TB_FETCH_FAIL')
        self.assertEqual(self.driver().run(), 0, self.text())
        self.assertEqual(self.state()['wholegames']['status'], 'ok')
        self.assertEqual(len([c for c in self.called('tb') if ' submit ' in c]), 2)

    def test_other_testbed_database_is_a_stop(self):
        self.setenv(FAKE_TB_DB='0' * 64)
        self.assertEqual(self.go(only='wholegames'), 10, self.text())
        self.assertIn('another database', self.state()['wholegames']['stop']['what'])

    def test_other_x64_dll_on_the_testbed_fails(self):
        self.setenv(FAKE_TB_WRONG_DLL='1')
        self.assertEqual(self.go(only='wholegames'), 1, self.text())

    def test_stop_file_while_waiting(self):
        self.setenv(FAKE_TB_STATE='running', FAKE_TB_STOP_ON_WAIT=str(self.work / 'STOP'))
        self.assertEqual(self.go(only='wholegames'), 10, self.text())
        self.assertIn('stop file', self.text())
        self.assertEqual(len(self.state()['wholegames']['outputs']['jobs']), 2)

    def test_one_host(self):
        self.setenv(C5_RELEASE_TESTBEDS='laptop')
        self.assertEqual(self.go(only='wholegames'), 0, self.text())
        subs = [c for c in self.called('tb') if ' submit ' in c]
        self.assertEqual(len(subs), 1)
        self.assertEqual(subs[0].count('--compare'), 4)


class Report(Fixture):
    def test_report_stops_unless_validated(self):
        self.setenv(FAKE_TB_KNOWN='1')
        self.assertEqual(self.go(), 0, self.text())
        self.assertEqual(self.report()['verdict'], 'validated')
        # a candidate stage that is no longer ok: the report says the tier is not reached
        st = json.loads((self.work / 'state.json').read_text())
        for e in st['stages']:
            if e['name'] == 'drift':
                e['status'] = 'stop'
                e['stop'] = {'what': 'x64 != x86 on Tiny', 'cl_step': '14', 'unblock': 'x'}
        (self.work / 'state.json').write_text(json.dumps(st))
        self.assertEqual(self.go(only='report'), 10, self.text())
        self.assertIn('drift stop: x64 != x86 on Tiny', self.report()['verdict'])

    def test_parity_rules(self):
        wg = tr.load_stage_module('wholegames', tr.STAGES_DIR)
        same = {'identical': True, 'tables': [], 'tablesCompared': 3, 'rowsCompared': 9}
        self.assertEqual(wg.parity(same)[0], 'identical')
        known = {'identical': False, 'tablesCompared': 1, 'rowsCompared': 5, 'onlyInA': [], 'onlyInB': [],
                 'plausibility': [], 'tables': [{'table': 'ReligionBeliefYields', 'identical': False, 'rowsA': 5,
                                                  'rowsB': 5, 'columnsDiffering': {'IsReligionOwner': {'rows': 5}}}]}
        self.assertEqual(wg.parity(known)[0], 'identical but known')
        rows = json.loads(json.dumps(known))
        rows['tables'][0]['rowsB'] = 6
        self.assertEqual(wg.parity(rows)[0], 'different')
        implausible = dict(known, plausibility=['PlayerYields: 0 / 0 rows'])
        self.assertEqual(wg.parity(implausible)[0], 'different')
        self.assertEqual(wg.parity({'error': 'no stats.db'})[0], 'different')
        order = json.loads(json.dumps(known))
        order['tables'][0]['columnsDiffering'] = {}
        self.assertEqual(wg.parity(order)[0], 'different')   # rows only in another order: not the known column


if __name__ == '__main__':
    unittest.main()
