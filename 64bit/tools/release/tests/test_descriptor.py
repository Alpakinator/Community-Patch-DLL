"""Issue 3501: tools/release/descriptor.py. Hermetic: every bad descriptor is written into a temporary releases folder;
the committed Release-5.4.8 descriptor is read but none of the files it names is opened (no hashing), except the huge
reference save (2.2 MB, ticket 0533), hashed only where its drive is on this machine."""
from __future__ import annotations

import contextlib
import copy
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import descriptor  # noqa: E402
from descriptor import DescriptorError  # noqa: E402

GOOD = json.loads((descriptor.RELEASES_DIR / 'Release-5.4.8.json').read_text(encoding='utf-8'))
TAG = 'Release-5.4.8'


class Env:
    """C5_RELEASE set (or removed) for the block."""

    def __init__(self, value):
        self.value, self.old = value, None

    def __enter__(self):
        self.old = os.environ.pop(descriptor.ENV_TAG, None)
        if self.value is not None:
            os.environ[descriptor.ENV_TAG] = self.value

    def __exit__(self, *exc):
        os.environ.pop(descriptor.ENV_TAG, None)
        if self.old is not None:
            os.environ[descriptor.ENV_TAG] = self.old


class Folder:
    """A temporary releases folder with current.json and one descriptor."""

    def __init__(self, data=None, tag=TAG, current=True, raw=None):
        self.d = tempfile.TemporaryDirectory()
        self.dir = Path(self.d.name)
        if current:
            (self.dir / 'current.json').write_text(json.dumps({'tag': tag}), encoding='utf-8')
        if raw is not None:
            (self.dir / f'{tag}.json').write_text(raw, encoding='utf-8')
        elif data is not None:
            (self.dir / f'{tag}.json').write_text(json.dumps(data), encoding='utf-8')

    def __enter__(self):
        return self.dir

    def __exit__(self, *exc):
        self.d.cleanup()


def without(field):
    d = copy.deepcopy(GOOD)
    del d[field]
    return d


def with_(field, value):
    d = copy.deepcopy(GOOD)
    d[field] = value
    return d


class Committed(unittest.TestCase):
    def test_current_is_5_4_8_with_the_old_tool_values(self):
        with Env(None):
            d = descriptor.load()
        self.assertEqual(d.tag, TAG)
        self.assertEqual(d.upstream_commit[:9], '7307aefdf')
        self.assertEqual(d.mod_folders, ['(1) Community Patch', '(2) Vox Populi'])
        self.assertEqual(d.mod_args, ['d1b6328c-ff44-4b0d-aad7-c657f83610cd:151', '8411a7a8-dad3-4622-a18e-fcc18324c799:17'])
        self.assertEqual(d.mod('(2)').version, 17)
        self.assertEqual(d.game_dir.name, "Sid Meier's Civilization V")
        self.assertIn('VP_MODPACK_5_4_8_NEUTRALS', d.game_dir.as_posix())
        # ticket 4711: the folder part is this machine's, so only the pinned database's own path is named here
        self.assertTrue(d.test_db.is_absolute())
        self.assertTrue(d.test_db.as_posix().endswith('/civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db'),
                        d.test_db)
        self.assertTrue(d.test_db_sha256.startswith('bd9b8aa1'))
        self.assertEqual(d.loc_db.name, 'Localization-Merged.db')
        self.assertEqual(d.reference_saves['huge'].turn, 350)
        self.assertTrue(d.reference_saves['huge'].sha256.startswith('c499d3c6'))
        self.assertEqual(d.reference_counts, {'tiny': 30569, 'standard': 359261, 'huge': 685488})  # since ticket 0551 (pin batch 5, 4813's finder fix; was 31226 / 349074 / 683261 since 0517)
        self.assertEqual(d.economic_strategies, 37)

    def test_huge_reference_save_is_a_durable_copy(self):
        """Ticket 0533 (0517's tool gap 3): the huge reference save is a durable copy, never a temp folder (the old
        path, a temp folder of 2026-11-10, was gone and run_d10.py without --huge-save could not play the huge game).
        Where the path's drive is on this machine the file must be there with the descriptor's SHA-256; elsewhere
        (another machine without that drive) the existence check is skipped with the reason."""
        s = descriptor.load(TAG).reference_saves['huge']
        p = s.path.as_posix().lower()
        for bad in ('/temp/', '/tmp/', '/appdata/'):
            self.assertNotIn(bad, p, f'{s.path}: a temporary folder is not a durable copy')
        if not Path(s.path.anchor).exists():
            self.skipTest(f'{s.path.anchor} is not on this machine: the huge save {s.path.name} cannot be checked here')
        self.assertTrue(s.path.is_file(), f'{s.path}: the huge reference save is missing on this machine')
        self.assertEqual(descriptor.sha256(s.path).lower(), s.sha256.lower())

    def test_available_lists_the_release_not_current(self):
        self.assertIn(TAG, descriptor.available())
        self.assertNotIn('current', descriptor.available())

    def test_round_trip_through_parse(self):
        d = descriptor.parse(copy.deepcopy(GOOD), 'x.json', expect_tag=TAG)
        self.assertEqual(d.raw, GOOD)


class Selection(unittest.TestCase):
    def test_named_tag(self):
        with Folder(GOOD) as rd:
            self.assertEqual(descriptor.load(TAG, rd).tag, TAG)

    def test_env_overrides_current(self):
        other = with_('tag', 'Release-9.9.9')
        with Folder(GOOD) as rd:
            (rd / 'Release-9.9.9.json').write_text(json.dumps(other), encoding='utf-8')
            with Env('Release-9.9.9'):
                self.assertEqual(descriptor.load(releases_dir=rd).tag, 'Release-9.9.9')
            with Env(None):
                self.assertEqual(descriptor.load(releases_dir=rd).tag, TAG)

    def test_unknown_tag(self):
        with Folder(GOOD) as rd, self.assertRaises(DescriptorError) as e:
            descriptor.load('Release-0.0.1', rd)
        self.assertEqual(e.exception.field, 'tag')
        self.assertIn('unknown release', str(e.exception))
        self.assertIn(TAG, str(e.exception))  # the known ones are named

    def test_unknown_tag_from_env(self):
        with Folder(GOOD) as rd, Env('Release-0.0.1'), self.assertRaises(DescriptorError) as e:
            descriptor.load(releases_dir=rd)
        self.assertIn('Release-0.0.1', str(e.exception))

    def test_bad_tag_strings(self):
        for bad in ('', '../x', 'a/b', 'current', 5, None):
            with self.subTest(bad=bad), Folder(GOOD) as rd, self.assertRaises(DescriptorError) as e:
                descriptor.path_for(bad, rd)
            self.assertEqual(e.exception.field, 'tag')

    def test_current_missing(self):
        with Folder(GOOD, current=False) as rd, Env(None), self.assertRaises(DescriptorError) as e:
            descriptor.load(releases_dir=rd)
        self.assertTrue(e.exception.file.endswith('current.json'))
        self.assertIn('file missing', str(e.exception))

    def test_current_without_tag(self):
        with Folder(GOOD) as rd, Env(None):
            (rd / 'current.json').write_text('{"release": "x"}', encoding='utf-8')
            with self.assertRaises(DescriptorError) as e:
                descriptor.load(releases_dir=rd)
        self.assertEqual(e.exception.field, 'tag')

    def test_release_file_not_json(self):
        with Folder(raw='{"format": 1,') as rd, self.assertRaises(DescriptorError) as e:
            descriptor.load(TAG, rd)
        self.assertIn('not JSON', str(e.exception))
        self.assertTrue(e.exception.file.endswith(f'{TAG}.json'))

    def test_release_file_not_an_object(self):
        with Folder([1, 2]) as rd, self.assertRaises(DescriptorError) as e:
            descriptor.load(TAG, rd)
        self.assertIn('must be an object', str(e.exception))

    def test_tag_does_not_match_the_file(self):
        with Folder(with_('tag', 'Release-5.4.7')) as rd, self.assertRaises(DescriptorError) as e:
            descriptor.load(TAG, rd)
        self.assertEqual(e.exception.field, 'tag')
        self.assertIn('Release-5.4.7', str(e.exception))

    def test_env_names_a_file(self):
        # issue 3512: the driver points the tools it starts at a candidate descriptor in its work folder
        with Folder(GOOD) as rd:
            cand = rd / 'work' / 'testdata' / 'Release-9.9.10.json'
            cand.parent.mkdir(parents=True)
            cand.write_text(json.dumps(with_('tag', 'Release-9.9.10')), encoding='utf-8')
            for value in (str(cand), cand.as_posix()):
                with self.subTest(value=value), Env(value):
                    d = descriptor.load(releases_dir=rd)
                    self.assertEqual((d.tag, d.file), ('Release-9.9.10', cand))
                    self.assertEqual(descriptor.current_tag(rd), 'Release-9.9.10')
            # any file name: the tag is the file's own
            other = rd / 'stand-in.json'
            other.write_text(json.dumps(with_('tag', 'master-ba8aafdc')), encoding='utf-8')
            with Env(str(other)):
                self.assertEqual(descriptor.load(releases_dir=rd).tag, 'master-ba8aafdc')
            # a named tag ignores the variable
            with Env(str(cand)):
                self.assertEqual(descriptor.load(TAG, rd).tag, TAG)

    def test_env_file_expands_variables(self):
        # a testbed job gets C5_RELEASE=%TESTBED_BIN%/rel/release/<tag>.json (stages/wholegames.py)
        with Folder(GOOD) as rd:
            (rd / 'rel').mkdir()
            (rd / 'rel' / 'x.json').write_text(json.dumps(with_('tag', 'Release-9.9.10')), encoding='utf-8')
            os.environ['C5_TEST_3512_BIN'] = str(rd)
            try:
                with Env('%C5_TEST_3512_BIN%/rel/x.json'):
                    self.assertEqual(descriptor.load(releases_dir=rd).tag, 'Release-9.9.10')
            finally:
                os.environ.pop('C5_TEST_3512_BIN', None)

    def test_env_file_missing_or_bad(self):
        with Folder(GOOD) as rd:
            with Env(str(rd / 'none.json')), self.assertRaises(DescriptorError) as e:
                descriptor.load(releases_dir=rd)
            self.assertIn('file missing', str(e.exception))
            (rd / 'bad.json').write_text(json.dumps(without('test_db')), encoding='utf-8')
            with Env(str(rd / 'bad.json')), self.assertRaises(DescriptorError) as e:
                descriptor.load(releases_dir=rd)
            self.assertEqual(e.exception.field, 'test_db')

    def test_env_file_detection(self):
        self.assertIsNone(descriptor.env_file('Release-5.4.8'))
        self.assertIsNone(descriptor.env_file(''))
        self.assertEqual(descriptor.env_file('x.json'), Path('x.json'))
        self.assertEqual(descriptor.env_file('C:/w/Release-5.4.9.json'), Path('C:/w/Release-5.4.9.json'))
        self.assertEqual(descriptor.env_file('C:\\w\\desc'), Path('C:\\w\\desc'))


class Fields(unittest.TestCase):
    def load(self, data):
        with Folder(data) as rd:
            return descriptor.load(TAG, rd)

    def assertRefused(self, data, field, text=None):
        with self.assertRaises(DescriptorError) as e:
            self.load(data)
        self.assertEqual(e.exception.field, field, str(e.exception))
        self.assertTrue(e.exception.file.endswith(f'{TAG}.json'), e.exception.file)
        self.assertIn(field, str(e.exception))
        if text:
            self.assertIn(text, str(e.exception))

    def test_every_required_field_missing(self):
        for f in descriptor._REQUIRED:
            with self.subTest(field=f):
                self.assertRefused(without(f), f, 'missing (required)')

    def test_optional_fields_may_be_missing(self):
        d = copy.deepcopy(GOOD)
        for f in descriptor._OPTIONAL:
            d.pop(f, None)
        got = self.load(d)
        self.assertIsNone(got.stock_x86_dll)
        self.assertIsNone(got.loc_db_sha256)

    def test_unknown_field(self):
        self.assertRefused(with_('test_database', 'C:/x.db'), 'test_database', 'unknown field')

    def test_wrong_types(self):
        cases = {
            'format': [2, '1', True, None],
            'tag': [5, ''],
            'upstream_commit': ['7307aefdf', 7307, 'G' * 40],
            'mods': [[], {}, 'cp'],
            'game_dir': [5, 'relative/dir', 'C:\\back\\slashes', ''],
            'test_db': [None, 'x.db'],
            'test_db_sha256': ['bd9b8aa1', 5, 'X' * 64],
            'loc_db': [[], 'loc.db'],
            'reference_saves': [[], 'huge'],
            'reference_counts': [[1, 2], 5],
            'economic_strategies': ['37', 0, -1, 3.0, True],
            'loc_db_sha256': ['short', 1],
            'stock_x86_dll': ['dll', 3],
            'stock_x86_dll_sha256': [True],
            'player_dll_sha256': ['0' * 63],
            'player_dll_43_sha256': ['0' * 63, 7],   # issue 4252
            'test_db_source': [1, ''],
            'reference_counts_pins': [['x']],
            'notes': [{}],
            'shell_config': ['I2', 'full', '', 2, ['i2']],   # issue 2602
            'options': [[], 'ServerUnits=1', 5],             # ticket 0574 (also refused without shell_config i3)
            'purity_allowed_drops': [[], 'x', 5],   # ticket 0574 (MC #7046); bad entries: test_purity_allowed_drops
        }
        self.assertEqual(set(cases), set(descriptor._REQUIRED) | set(descriptor._OPTIONAL))
        for f, values in cases.items():
            for v in values:
                with self.subTest(field=f, value=v):
                    self.assertRefused(with_(f, v), f)

    def test_mod_entries(self):
        for i, (key, value, field) in enumerate([
                ('guid', '{D1B6328C-FF44-4B0D-AAD7-C657F83610CD}', 'mods[0].guid'),
                ('version', '151', 'mods[0].version'),
                ('version', 0, 'mods[0].version'),
                ('folder', '', 'mods[0].folder')]):
            d = copy.deepcopy(GOOD)
            d['mods'][0][key] = value
            with self.subTest(key=key, value=value):
                self.assertRefused(d, field)
        d = copy.deepcopy(GOOD)
        del d['mods'][1]['guid']
        self.assertRefused(d, 'mods[1].guid', 'missing')
        d = copy.deepcopy(GOOD)
        d['mods'][0]['url'] = 'x'
        self.assertRefused(d, 'mods[0].url', 'unknown field')
        d = copy.deepcopy(GOOD)
        d['mods'][1] = dict(d['mods'][0])
        self.assertRefused(d, 'mods', 'twice')
        d = copy.deepcopy(GOOD)
        d['mods'][0] = 'cp'
        self.assertRefused(d, 'mods[0]', 'object')

    def test_save_entries(self):
        for key, value in (('path', 'save.Civ5Save'), ('sha256', 'c499'), ('turn', '350'), ('turn', -1)):
            d = copy.deepcopy(GOOD)
            d['reference_saves']['huge'][key] = value
            with self.subTest(key=key, value=value):
                self.assertRefused(d, f'reference_saves.huge.{key}')
        d = copy.deepcopy(GOOD)
        del d['reference_saves']['huge']['turn']
        self.assertRefused(d, 'reference_saves.huge.turn', 'missing')

    def test_count_entries(self):
        d = copy.deepcopy(GOOD)
        d['reference_counts']['tiny'] = '30666'
        self.assertRefused(d, 'reference_counts.tiny')

    def test_purity_allowed_drops(self):   # ticket 0574 (MC #7046)
        d = copy.deepcopy(GOOD)
        self.assertIsNone(self.load(d).purity_allowed_drops)
        d['purity_allowed_drops'] = [['method', 'ICvUnit1::GetFacingDirection(bool) const'], ['lua', 'Plot.GetPlotCity']]
        self.assertEqual(self.load(d).purity_allowed_drops,
                         (('method', 'ICvUnit1::GetFacingDirection(bool) const'), ('lua', 'Plot.GetPlotCity')))
        for bad, field in (([['method']], 'purity_allowed_drops[0]'), ([['func', 'a']], 'purity_allowed_drops[0]'),
                           ([['lua', '']], 'purity_allowed_drops[0]'), ([['lua', 'a\tb']], 'purity_allowed_drops[0]'),
                           ([['lua', 'A.b'], ['lua', 'A.b']], 'purity_allowed_drops[1]')):
            d['purity_allowed_drops'] = bad
            with self.subTest(bad=bad):
                self.assertRefused(d, field)

    def test_i3_options(self):   # ticket 0574
        def i3(options='absent'):
            d = copy.deepcopy(GOOD)
            d['shell_config'] = 'i3'
            if options != 'absent':
                d['options'] = options
            return d
        got = self.load(i3({'Renderer': {'ServerUnits': 1}, 'Performance': {'AiUpdateTickMs': 0}}))
        self.assertEqual(got.shell_config, 'i3')
        self.assertEqual(got.shell_configuration, 'i3')
        self.assertEqual(got.options, {'Renderer': {'ServerUnits': 1}, 'Performance': {'AiUpdateTickMs': 0}})
        self.assertEqual(self.load(i3()).options, {})                       # i3 without options: an empty set
        self.assertEqual(self.load(i3({'Performance': {'AiUpdateTickMs': -1}})).options['Performance'],
                         {'AiUpdateTickMs': -1})                            # negative values are values
        for shell in (None, 'thin', 'i2'):                                  # options only with i3
            d = copy.deepcopy(GOOD)
            if shell:
                d['shell_config'] = shell
            else:
                d.pop('shell_config', None)
            if shell != 'i3':
                self.assertIsNone(self.load(d).options)
            d['options'] = {'Renderer': {'ServerUnits': 1}}
            with self.subTest(shell=shell):
                self.assertRefused(d, 'options', 'only with shell_config i3')
        for options, field, text in (
                ({'Renderer': {'ServerRenderer': 1}}, 'options.Renderer.ServerRenderer', 'vp64helper on'),
                ({'renderer': {'serverrenderer': 1}}, 'options.renderer.serverrenderer', 'vp64helper on'),
                ({'Renderer': {'ServerUnits': True}}, 'options.Renderer.ServerUnits', 'integer'),
                ({'Renderer': {'ServerUnits': '1'}}, 'options.Renderer.ServerUnits', 'integer'),
                ({'Renderer': {'ServerUnits': 1.0}}, 'options.Renderer.ServerUnits', 'integer'),
                ({'Renderer': {'Server Units': 1}}, 'options.Renderer.Server Units', 'key name'),
                ({'Renderer': {'1x': 1}}, 'options.Renderer.1x', 'key name'),
                ({'[Renderer]': {'ServerUnits': 1}}, 'options.[Renderer]', 'section name'),
                ({'Renderer': {}}, 'options.Renderer', 'empty section'),
                ({'Renderer': 1}, 'options.Renderer', 'object'),
                ({'Renderer': {'ServerUnits': 1, 'serverunits': 0}}, 'options.Renderer.serverunits', 'twice'),
                ({'Renderer': {'ServerUnits': 1}, 'RENDERER': {'ServerArc': 1}}, 'options.RENDERER', 'twice')):
            with self.subTest(options=options):
                self.assertRefused(i3(options), field, text)

    def test_mod_lookup(self):
        d = self.load(GOOD)
        with self.assertRaises(DescriptorError) as e:
            d.mod('(9)')
        self.assertEqual(e.exception.field, 'mods')
        with self.assertRaises(DescriptorError):
            d.mod('(')  # two match


class CheckFiles(unittest.TestCase):
    def test_present_and_hashed(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            (t / 'game').mkdir()
            for n in ('test.db', 'loc.db', 'huge.Civ5Save'):
                (t / n).write_bytes(n.encode())
            d = copy.deepcopy(GOOD)
            for k in ('stock_x86_dll', 'stock_x86_dll_sha256', 'loc_db_sha256'):
                d.pop(k)
            d.update(game_dir=(t / 'game').as_posix(), test_db=(t / 'test.db').as_posix(),
                     test_db_sha256=descriptor.sha256(t / 'test.db'), loc_db=(t / 'loc.db').as_posix())
            d['reference_saves'] = {'huge': {'path': (t / 'huge.Civ5Save').as_posix(),
                                             'sha256': descriptor.sha256(t / 'huge.Civ5Save'), 'turn': 350}}
            with Folder(d) as rd:
                self.assertEqual(descriptor.load(TAG, rd).check_files(), [])
                d['test_db_sha256'] = '0' * 64
                (rd / f'{TAG}.json').write_text(json.dumps(d), encoding='utf-8')
                (t / 'loc.db').unlink()
                bad = descriptor.load(TAG, rd).check_files()
            self.assertEqual(len(bad), 2, bad)
            self.assertTrue(any(b.startswith('test_db: SHA-256') for b in bad), bad)
            self.assertTrue(any(b.startswith('loc_db: no file') for b in bad), bad)


class Cli(unittest.TestCase):
    def run_main(self, argv, rd=None):
        out, err = io.StringIO(), io.StringIO()
        old = descriptor.RELEASES_DIR
        try:
            if rd is not None:
                descriptor.RELEASES_DIR = rd
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), Env(None):
                rc = descriptor.main(argv)
        finally:
            descriptor.RELEASES_DIR = old
        return rc, out.getvalue(), err.getvalue()

    def test_field(self):
        rc, out, _ = self.run_main(['--field', 'economic_strategies'])
        self.assertEqual((rc, out.strip()), (0, '37'))

    def test_unknown_tag_exits_2(self):
        rc, out, err = self.run_main(['--tag', 'Release-0.0.1'])
        self.assertEqual(rc, 2)
        self.assertEqual(out, '')
        self.assertIn('unknown release', err)

    def test_unknown_field_exits_2(self):
        rc, _, err = self.run_main(['--field', 'nope'])
        self.assertEqual(rc, 2)
        self.assertIn('nope: not in the descriptor', err)

    def test_bad_file_exits_2(self):
        with Folder(without('test_db')) as rd:
            rc, _, err = self.run_main([], rd)
        self.assertEqual(rc, 2)
        self.assertIn('test_db: missing', err)

    def test_check_files_missing_exits_1(self):
        d = copy.deepcopy(GOOD)
        d['game_dir'] = 'C:/no/such/folder-3501'
        with Folder(d) as rd:
            # hashing the real 5.4.8 files is not hermetic: point every file at a missing path
            d['test_db'] = d['loc_db'] = d['stock_x86_dll'] = 'C:/no/such/file-3501.db'
            d['reference_saves']['huge']['path'] = 'C:/no/such/save-3501.Civ5Save'
            (rd / f'{TAG}.json').write_text(json.dumps(d), encoding='utf-8')
            rc, _, err = self.run_main(['--check-files'], rd)
        self.assertEqual(rc, 1)
        self.assertIn('game_dir: no folder', err)
        self.assertEqual(err.count('no file'), 4, err)


if __name__ == '__main__':
    unittest.main()
