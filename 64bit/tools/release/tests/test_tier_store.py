"""Hermetic tests of the results store (epic 0630, ticket 5011): tools/release/tiers/store.py, its records, its key
parts and its import of a finished take's state.json. A temporary git repository stands for this repository's tool
sources; the take is a fixture state of the shape take_release.py writes. Nothing outside the temporary folder is read
or written.

    python -I -X utf8 tools/release/tests/test_tier_store.py
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import store  # noqa: E402

DLL = 'CvGameCore_Expansion2.dll'
TAG = 'Release-9.9.9'


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def git(repo, *args):
    r = subprocess.run(['git', '-C', str(repo), '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'core.autocrlf=true',
                        *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    return r.stdout.strip()


class Base(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self._environ = dict(os.environ)
        os.environ.pop(store.ENV, None)
        self.results = self.tmp / 'results'
        self.st = store.Store(self.results)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._environ)
        self._td.cleanup()


class Records(Base):
    PARTS = {'x64_dll': 'a' * 64, 'i0_dll': 'b' * 64}

    def rec(self, stage='savetest', parts=None, **kw):
        return store.make_record(stage, parts or dict(self.PARTS), {'rows': {'tiny': 1}, 'wall_seconds': 9}, take='T w',
                                 tag=TAG, work='w', ended='2026-11-20 23:00:00', source='test',
                                 scope={'games': ['tiny', 'standard']}, **kw)

    def test_put_find_atomic(self):
        p = self.st.put(self.rec())
        self.assertEqual(p.parent, self.results / 'savetest')
        self.assertEqual([x.name for x in p.parent.iterdir()], [p.name])   # no temporary file left
        rec, why = self.st.find('savetest', dict(self.PARTS), need_scope=['tiny'])
        self.assertIsNone(why)
        self.assertEqual(rec['outputs']['rows'], {'tiny': 1})

    def test_key_is_content_and_stage_not_path(self):
        a = store.content_key('savetest', self.PARTS)
        self.assertEqual(a, store.content_key('savetest', dict(reversed(list(self.PARTS.items())))))
        self.assertNotEqual(a, store.content_key('wholegames', self.PARTS))

    def test_scope_not_covered(self):
        self.st.put(self.rec())
        rec, why = self.st.find('savetest', dict(self.PARTS), need_scope=['tiny', 'standard', 'huge'])
        self.assertIsNone(rec)
        self.assertIn('needs', why)

    def test_nearest_names_the_difference(self):
        self.st.put(self.rec())
        rec, why = self.st.find('savetest', {**self.PARTS, 'x64_dll': 'c' * 64})
        self.assertIsNone(rec)
        self.assertIn('differs in x64_dll cccccccccccc (record aaaaaaaaaaaa)', why)

    def test_empty_store(self):
        rec, why = self.st.find('census', {'x': 'y'})
        self.assertIsNone(rec)
        self.assertIn('no record of census', why)

    def test_check_record_refusals(self):
        key = store.content_key('savetest', self.PARTS)
        good = self.rec()
        self.assertIsNone(store.check_record(good, 'savetest', key, self.PARTS))
        cases = [
            (dict(good, format=2), 'format'),
            (dict(good, stage='wholegames'), "stage 'wholegames'"),
            (dict(good, parts={'x64_dll': 'a' * 64}), 'has no i0_dll hash'),
            (dict(good, parts={**self.PARTS, 'i0_dll': ''}), 'has no i0_dll hash'),
            (dict(good, parts={**self.PARTS, 'extra': 'e'}), 'does not key on (extra)'),
            (dict(good, parts={**self.PARTS, 'x64_dll': 'f' * 64}), 'x64_dll differ'),
            (dict(good, key='0' * 64), 'its key is not the key of its parts'),
            (dict(good, status='stop'), "status is 'stop'"),
        ]
        for rec, want in cases:
            self.assertIn(want, store.check_record(rec, 'savetest', key, self.PARTS) or 'ACCEPTED', rec)

    def test_tampered_record_file_refused(self):
        p = self.st.put(self.rec())
        rec = json.loads(p.read_text())
        rec['parts']['x64_dll'] = 'f' * 64   # the record no longer has the key its file name says
        p.write_text(json.dumps(rec))
        got, why = self.st.find('savetest', dict(self.PARTS))
        self.assertIsNone(got)
        self.assertIn('does not prove', why)

    def test_unreadable_record_refused(self):
        p = self.st.put(self.rec())
        p.write_text('{not json')
        got, why = self.st.find('savetest', dict(self.PARTS))
        self.assertIsNone(got)
        self.assertIn('not readable', why)

    def test_root_precedence_and_repository_refused(self):
        self.assertEqual(store.root_of({}), store.DEFAULT_ROOT)
        os.environ[store.ENV] = str(self.tmp / 'env')
        self.assertEqual(store.root_of({}), self.tmp / 'env')
        self.assertEqual(store.root_of({'results': str(self.tmp / 'cfg')}), self.tmp / 'cfg')
        with self.assertRaises(store.StoreError):
            store.Store(store.REPO / 'tools' / 'results')
        self.assertEqual(store.DEFAULT_ROOT.parent, Path('C:/c5-64bit-work/3500'))

    def test_every_key_part_defined_and_table_complete(self):
        for stage, parts in store.KEYS.items():
            self.assertIn(stage, store.TOOL_SOURCES)
            for p in parts:
                self.assertIn(p, store.PARTS, (stage, p))
            self.assertIn('stage_module', parts)
            self.assertIn('tool_sources', parts)
        self.assertNotIn('server_exe', store.KEYS['wholegames'])   # MC 2026-11-20: whole games carry over a server
        self.assertIn('server_exe', store.KEYS['savetest'])
        for d in ('x64_dll', 'i0_dll'):
            self.assertIn(d, store.KEYS['savetest'])
            self.assertIn(d, store.KEYS['wholegames'])


class Compiler(Base):
    def test_lines_read_and_build_field_first(self):
        log = self.tmp / 'dll.log'
        log.write_text('x\nTool versions:\n  clang-cl: clang version 19.1.7\n  lld-link: LLD 19.1.7\n'
                       '  MSVC (VCToolsVersion): 14.43.34808\n  Windows SDK: 1\n')
        ba = self.tmp / 'build_all.json'
        ba.write_text(json.dumps({'steps': [{'step': 'stubs', 'log': 'nope'}, {'step': 'dll', 'log': str(log)}]}))
        text, why = store.compiler_of({'build_all': str(ba)})
        self.assertIsNone(why)
        self.assertEqual(text, 'clang-cl: clang version 19.1.7; lld-link: LLD 19.1.7; '
                               'MSVC (VCToolsVersion): 14.43.34808')
        self.assertEqual(store.compiler_of({'build_all': str(ba), 'compiler': 'clang 20'}), ('clang 20', None))

    def test_unreadable(self):
        self.assertIn('missing', store.compiler_of({'build_all': str(self.tmp / 'no.json')})[1])
        ba = self.tmp / 'build_all.json'
        ba.write_text(json.dumps({'steps': [{'step': 'dll', 'log': str(self.tmp / 'gone.log')}]}))
        self.assertIn('missing', store.compiler_of({'build_all': str(ba)})[1])
        (self.tmp / 'gone.log').write_text('MSVC (VCToolsVersion): 14\n')
        self.assertIn('no clang-cl', store.compiler_of({'build_all': str(ba)})[1])


class Import(Base):
    """A finished take of today's driver (fixture state) and a git repository with the tool sources at its commit."""

    def setUp(self):
        super().setUp()
        self.repo = self.tmp / 'repo'
        self.repo.mkdir()
        git(self.repo, 'init', '-q')
        for stage in store.KEYS:
            for pat in store.TOOL_SOURCES[stage] + [f'tools/release/stages/{stage}.py']:
                p = self.repo / pat.replace('*', 'x.py')
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(f'source of {pat}\r\n'.encode())
        git(self.repo, 'add', '-A')
        git(self.repo, 'commit', '-q', '-m', 'tools')
        self.commit = git(self.repo, 'rev-parse', 'HEAD')
        self.work = self.make_take()

    def make_take(self, dirty=False):
        w = self.tmp / 'take'
        lay = w / 'layout'
        (lay / 'bin').mkdir(parents=True)
        files = {f'bin/{DLL}': b'x64 dll', 'bin/server.exe': b'server', 'bin/CvGameDatabase_x64.dll': b'db'}
        logs = w / 'build' / 'logs'
        logs.mkdir(parents=True)
        (logs / 'dll.log').write_text('  clang-cl: clang version 19.1.7\n')
        (w / 'build' / 'build_all.json').write_text(json.dumps({'steps': [{'step': 'dll',
                                                                          'log': str(logs / 'dll.log')}]}))
        files[f'proxy/bin/{DLL}'] = b'proxy dll'   # ticket 5023: the instance stage's key reads the proxy and -ia DLLs
        build = {'x64_hashes': {r: {'sha256': sha(b).upper(), 'bytes': len(b)} for r, b in files.items()},
                 'i0_dll_sha256': sha(b'x86').upper(), 'i0ia_dll_sha256': sha(b'x86 ia').upper(), 'build_all': str(w / 'build' / 'build_all.json'),
                 'tools_commit': self.commit, 'tools_dirty': dirty, 'x64_bin': str(lay / 'bin')}
        lock = {'x64': {'fingerprint': f'v1 {"a" * 64} 479 files'}}
        desc = w / 'testdata' / f'{TAG}.json'
        desc.parent.mkdir(parents=True)
        desc.write_text(json.dumps({'tag': TAG, 'test_db_sha256': 'd' * 64,
                                    'reference_saves': {'huge': {'sha256': 'e' * 64, 'turn': 350}},
                                    'reference_counts': {'tiny': 1, 'standard': 2, 'huge': 3}}))
        (w / 'purity').mkdir()
        (w / 'purity' / 'purity.tsv').write_bytes(b'# vp64-purity 1\n')
        (w / 'purity' / 'report.tsv').write_bytes(b'kind\tkey\tdecision\n')
        purity_out = {'purity_tsv': str(w / 'purity' / 'purity.tsv'), 'report': str(w / 'purity' / 'report.tsv'),
                      'dropped': 0}
        bsha = store.jhash(build)
        dep = {'build': bsha}

        def e(name, outputs, own=None, deps=None, status='ok'):
            return {'name': name, 'status': status, 'ended': '2026-11-20 23:06:17', 'outputs': outputs,
                    'inputs': {'own': own or {}, 'deps_sha': deps if deps is not None else dep}}
        games = [{'name': n, 'parity': 'identical', 'runs_pass': True} for n in ('tc', 'sf', 'sp', 'mp')]
        st = {'format': 1, 'tag': TAG, 'config': {'tier': 'validated'}, 'stages': [
            e('testdata', {'descriptor': str(desc)}, deps={}),
            e('lock', lock, deps={}),
            e('build', build, deps={}),
            e('narrowing', {'sites': 794, 'new': 0}),
            e('census', {'out': 'c', 'x64_only': 0, 'seconds': 606}),
            e('model', {'out': 'm', 'equal': True}, deps={**dep, 'lock': store.jhash(lock)}),
            e('purity', purity_out, deps={**dep, 'model': 'x'}),
            e('savetest', {'rows': {'tiny': 1, 'standard': 2, 'huge': 3}, 'wall_seconds': 1352.9},
              own={'huge': {'sha256': 'e' * 64, 'turn': 350}, 'counts': {'tiny': 1, 'standard': 2, 'huge': 3}}),
            e('wholegames', {'parity': 'passed', 'games': games}),
            # ticket 5022: the fix checks of today's driver (0096, 0084, 0103 and oldsave) on the same x64 build
            e('fixchecks', {'checks': {n: {'rc': 0, 'pass': True} for n in ('0096', '0084', '0103', 'oldsave')}}),
            # ticket 5023: today's instance stage (both arms) on the same build and purity list
            e('instance', {'arms': {'tiny': {'ok': True}, 'huge': {'ok': True}}},
              deps={**dep, 'purity': store.jhash(purity_out)})]}
        (w / 'state.json').write_text(json.dumps(st))
        return w

    def state(self):
        return json.loads((self.work / 'state.json').read_text())

    def edit(self, name, **upd):
        st = self.state()
        for x in st['stages']:
            if x['name'] == name:
                x.update(upd)
        (self.work / 'state.json').write_text(json.dumps(st))

    def imp(self, **kw):
        self.said = []
        return store.import_take(self.work, self.st, repo=self.repo, say=self.said.append, **kw)

    def test_all_six_recorded_with_scope(self):
        before = sorted(p.name for p in self.work.rglob('*'))
        rec, skip = self.imp()
        self.assertEqual(sorted(s for s, _ in rec), sorted(store.KEYS), skip)
        self.assertEqual(skip, [])
        self.assertEqual(sorted(p.name for p in self.work.rglob('*')), before)   # the take's folder is not written
        r = json.loads(dict(rec)['savetest'].read_text())
        self.assertEqual(r['scope'], {'games': ['huge', 'standard', 'tiny']})
        self.assertEqual(r['parts']['huge_save'], 'e' * 64)
        self.assertEqual(r['parts']['x64_dll'], sha(b'x64 dll'))
        self.assertIn(self.commit, r['source'])
        w = json.loads(dict(rec)['wholegames'].read_text())
        self.assertEqual(w['scope'], {'games': ['mp', 'sf', 'sp', 'tc']})
        p = json.loads(dict(rec)['purity'].read_text())
        self.assertEqual(p['files']['purity_tsv']['sha256'], sha(b'# vp64-purity 1\n'))

    def test_import_key_equals_a_live_take_on_the_same_files(self):
        """The first hotfix after the beta carries from the beta candidate through this import: a live take with the
        same bytes and the repository's checkout (CRLF on disk, LF in git) computes the same key."""
        rec, _ = self.imp()
        st = {e['name']: e['outputs'] for e in self.state()['stages']}
        desc = json.loads(Path(st['testdata']['descriptor']).read_text())

        class Live(store.LiveSource):
            def __init__(s, stage):
                s.stage, s.repo = stage, self.repo

            def build(s):
                return st['build']

            def lock(s):
                return st['lock']

            def purity(s):
                return st['purity']

            def huge_sha(s):
                return desc['reference_saves']['huge']['sha256']

            def counts(s):
                return desc['reference_counts']

            def test_db(s):
                return desc['test_db_sha256']

            def major(s):
                return 22
        for stage, path in rec:
            parts, unread = store.key_parts(stage, Live(stage))
            self.assertEqual(unread, {}, stage)
            got, why = self.st.find(stage, parts)
            self.assertIsNone(why, stage)
            self.assertEqual(got['key'], json.loads(path.read_text())['key'])

    def test_dirty_tools_skipped_unless_commit_given(self):
        self.work = self.make_take_dirty()
        rec, skip = self.imp()
        self.assertEqual(rec, [])
        self.assertTrue(all('tools_dirty' in why for _s, why in skip), skip)
        rec, skip = self.imp(tools_commit=self.commit)
        self.assertEqual(len(rec), len(store.KEYS), skip)

    def make_take_dirty(self):
        import shutil
        shutil.rmtree(self.tmp / 'take')
        return self.make_take(dirty=True)

    def test_unknown_tools_commit_skipped(self):
        rec, skip = self.imp(tools_commit='0' * 40)
        self.assertEqual(rec, [])
        self.assertIn('tool_sources', dict(skip)['savetest'])

    def test_build_changed_after_stage_skipped(self):
        st = self.state()
        b = next(x for x in st['stages'] if x['name'] == 'build')
        b['outputs']['i0_dll_sha256'] = 'F' * 64   # the build ran again after the save test
        (self.work / 'state.json').write_text(json.dumps(st))
        rec, skip = self.imp()
        self.assertIn('the outputs of build changed after this stage ran', dict(skip)['savetest'])
        self.assertNotIn('savetest', dict(rec))

    def test_not_passed_and_subset_and_carried_skipped(self):
        self.edit('savetest', status='stop')
        self.edit('wholegames', outputs={'parity': 'passed on a subset', 'subset': ['tc'], 'games': []})
        self.edit('census', outputs={'carried': {'line': 'carried from x'}})
        rec, skip = self.imp()
        sk = dict(skip)
        self.assertEqual(sk['savetest'], 'status stop')
        self.assertIn('passed on a subset', sk['wholegames'])
        self.assertIn('carried in this take', sk['census'])
        self.assertEqual(sorted(dict(rec)), ['fixchecks', 'instance', 'model', 'narrowing', 'purity'])

    def test_missing_purity_file_and_descriptor_skipped(self):
        (self.work / 'purity' / 'purity.tsv').unlink()
        (self.work / 'testdata' / f'{TAG}.json').unlink()
        rec, skip = self.imp()
        sk = dict(skip)
        self.assertIn('purity_tsv', sk['purity'])
        self.assertIn('test_db', sk['savetest'])
        self.assertIn('test_db', sk['wholegames'])

    def test_missing_hash_in_state_skipped(self):
        st = self.state()
        b = next(x for x in st['stages'] if x['name'] == 'build')
        del b['outputs']['x64_hashes'][f'bin/{DLL}']
        for x in st['stages']:   # keep the deps hash right so only the missing hash is the reason
            if x['name'] not in ('testdata', 'lock', 'build'):
                x['inputs']['deps_sha']['build'] = store.jhash(b['outputs'])
        (self.work / 'state.json').write_text(json.dumps(st))
        rec, skip = self.imp()
        self.assertIn('x64_dll', dict(skip)['savetest'])
        self.assertIn('x64_dll', dict(skip)['purity'])
        self.assertIn('census', dict(rec))

    def test_dry_run_writes_nothing(self):
        rec, _ = self.imp(dry_run=True)
        self.assertEqual(len(rec), len(store.KEYS))
        self.assertFalse(self.results.exists())

    def test_command_line(self):
        out = []
        rc = store.main(['import', str(self.work), '--results', str(self.results), '--repo', str(self.repo)],
                        out=out.append)
        self.assertEqual(rc, 0, out)
        self.assertIn(f'{len(store.KEYS)} recorded, 0 skipped', out[-1])
        out.clear()
        self.assertEqual(store.main(['list', '--results', str(self.results), '--stage', 'savetest'], out=out.append), 0)
        self.assertEqual(len(out), 1)
        self.assertEqual(store.main(['import', str(self.tmp / 'nowhere'), '--results', str(self.results)],
                                    out=out.append), 2)
        self.assertIn('not readable', out[-1])
        self.assertEqual(store.main(['import', str(self.work), '--results', str(store.REPO / 'x')], out=out.append), 2)
        self.assertIn('inside the repository', out[-1])
        self.assertEqual(store.main(['bogus'], out=out.append), 2)

    def test_command_line_subprocess_help(self):
        r = subprocess.run([sys.executable, '-I', str(HERE.parent / 'tiers' / 'store.py'), '--help'],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('import', r.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
