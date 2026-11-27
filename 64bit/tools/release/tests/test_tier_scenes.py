"""Hermetic tests of the tier stage `scenes` (epic 0630, ticket 5019): tools/release/tiers/stages/scenes.py, the scene
library's regression against the last release's pictures as a gate. A faked sceneshot build, a faked scenelib check, a
faked scenerun writing report.json and baselines as the real one does, a faked quietlock; the repository is a small git
repository made here; the adapter is set by replacing scenes.machine_adapters. No gate, lock, build, GPU or game.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_tier_scenes.py
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import descriptor  # noqa: E402
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402

PY = sys.executable
TAG, BASE_TAG = 'Release-9.9.9', 'Release-9.9.8'
GPU = 'NVIDIA GeForce RTX 4080'
DRIVER = '32.0.16.1088'
ADAPTER = f'{GPU} driver {DRIVER}'
BUNDLES = ('B1', 'B2')
SHOTS = {'B1': {'1001': 'c11', '1002': 'c12'}, 'B2': {'1001': 'c21', '1002': 'c22'}}

FAKE = r'''
import hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
name, a = sys.argv[1], sys.argv[2:]
with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as f:
    f.write(name + ' ' + ' '.join(a) + (' cwd=' + os.getcwd() if name == 'build_sceneshot' else '') + '\n')
def opt(k, d=None):
    return a[a.index(k) + 1] if k in a else d
if name == 'quietlock':
    sys.exit(subprocess.call(a[a.index('--') + 1:]))
if name == 'build_sceneshot':
    rc = int(os.environ.get('FAKE_BUILD_RC', '0'))
    if rc == 0:
        p = Path(opt('--out')) / 'bin' / 'sceneshot.exe'
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(os.environ.get('FAKE_PLAYER', 'player').encode())
        print('build_sceneshot: OK: ' + str(p))
    else:
        print('build_sceneshot: FAILED: fake')
    sys.exit(rc)
if name == 'scenelib':
    rc = int(os.environ.get('FAKE_SCENELIB_RC', '0'))
    print('scenelib: OK fake' if rc == 0 else 'scenelib: FAILED 5 Stale: fake')
    sys.exit(rc)
if name == 'scenerun':
    lib, out, base = Path(opt('--library')), Path(opt('--out')), Path(opt('--baseline'))
    shots = json.loads(os.environ['FAKE_SHOTS'])
    adapter = os.environ.get('FAKE_ADAPTER', 'NVIDIA GeForce RTX 4080')
    write = '--no-baseline-write' not in a
    reset = '--set-baseline' in a
    pre = 'FAKE_RETIRED_' if lib.name == 'retired' else 'FAKE_SCENERUN_'   # the retired copies' run (MC #10544)
    idx = json.loads((lib / 'index.json').read_text())
    for bid, ent in idx['bundles'].items():   # scenerun verifies scene.json against the index before drawing
        if hashlib.sha256((lib / 'bundles' / bid / 'scene.json').read_bytes()).hexdigest() != ent['scene_json_sha256']:
            print('scenerun: FAILED 4 ShaMismatch: ' + bid)
            sys.exit(4)
    omit = set(filter(None, os.environ.get('FAKE_OMIT', '').split(',')))
    ents, differs = [], []
    for bid in sorted(b for b in idx['bundles'] if b not in omit):
        bj = base / bid / 'baseline.json'
        bl = json.loads(bj.read_text()) if bj.is_file() else {'bundle_id': bid, 'shots': {}}
        e = {'bundle_id': bid, 'adapter': adapter, 'same_adapter': adapter == 'NVIDIA GeForce RTX 4080', 'shots': []}
        for shot, colour in sorted(shots.get(bid, {}).items()):
            png = out / 'rep01' / bid / 'frames' / (shot + '.png')
            png.parent.mkdir(parents=True, exist_ok=True)
            png.write_bytes(colour.encode())
            b = bl['shots'].get(shot)
            if b is None:
                st, det = 'new', {'text': 'new'}
            elif b['colour_sha256'] == colour:
                st, det = 'equal', {'text': 'equal'}
            else:
                st, det = 'differs', {'text': 'differs', 'colour': {'differing': 12, 'max_delta': 40}}
                differs.append(bid + ' ' + shot)
            det['status'] = st
            e['shots'].append({'name': shot, 'png': str(png), 'colour_sha256': colour, 'depth_sha256': 'd',
                               'regression': det})
            if write and (st == 'new' or reset):
                (base / bid).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(png, base / bid / (shot + '.png'))
                bl['shots'][shot] = {'colour_sha256': colour, 'depth_sha256': 'd'}
                bj.write_text(json.dumps(bl))
        ents.append(e)
    out.mkdir(parents=True, exist_ok=True)
    rc = int(os.environ.get(pre + 'RC') or (7 if differs else 0))
    (out / 'report.json').write_text(json.dumps({'run_id': 'fake-run', 'player_sha256': 'p', 'numpy': True,
                                                 'bundles': ents}))
    print('scenerun: OK' if rc == 0 else 'scenerun: FAILED %d %s' % (rc, os.environ.get(pre + 'WHY', 'x')))
    sys.exit(rc)
sys.exit(99)
'''


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def git(repo, *args):
    r = subprocess.run(['git', '-C', str(repo), '-c', 'user.name=t', '-c', 'user.email=t@t', '-c',
                        'core.autocrlf=false'] + list(args), capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


class Scenes(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self._env = dict(os.environ)
        for k in [k for k in os.environ if k.startswith(('FAKE_', 'C5_RELEASE_', 'C5_SCENE'))]:
            os.environ.pop(k)
        self.calls = self.tmp / 'calls.txt'
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'), FAKE_CALLS=str(self.calls),
                          FAKE_SHOTS=json.dumps(SHOTS))
        self.mod = tr.load_stage_module('scenes', tt.TIER_STAGES_DIR)
        self.adapters = [{'name': GPU, 'driver': DRIVER}]
        self.mod.machine_adapters = lambda: self.adapters
        # the repository the player is built from
        self.repo = self.tmp / 'repo'
        for pat in self.mod.PLAYER_SOURCES:
            p = self.repo / pat.replace('*', 'x.cpp')
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(f'source of {pat}\n'.encode())
        hdr = self.repo / self.mod.BUNDLE_HEADER   # the player's read floor (MC #10544)
        hdr.parent.mkdir(parents=True, exist_ok=True)
        hdr.write_text('constexpr uint32_t kFormatVersion = 10;\nconstexpr uint32_t kOldestReadFormatVersion = 9;\n')
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        git(self.repo, 'add', '-A')
        git(self.repo, 'commit', '-q', '-m', 'sources')
        self.commit = git(self.repo, 'rev-parse', 'HEAD')
        # the library copy and the asset root
        self.lib = self.tmp / 'lib'
        self.make_library(BUNDLES)
        self.aroot = self.tmp / 'aroot'
        self.aroot.mkdir()
        (self.aroot / 'asset-root.txt').write_text('db abc\n')
        self.results = self.tmp / 'results'
        fake = self.tmp / 'fake_scenes.py'
        fake.write_text(textwrap.dedent(FAKE))
        self.tools = {**tr.default_tools(), **{n: [PY, '-I', str(fake), n]
                                              for n in ('quietlock', 'build_sceneshot', 'scenelib', 'scenerun')}}
        d = json.loads((tr.HERE / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
        d.update(tag=TAG)
        self.desc = descriptor.parse(d, 'test', expect_tag=TAG)
        self.lines = []
        self.n = 0
        self.base0 = self.take(tag=BASE_TAG)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._env)
        self._td.cleanup()

    # --- fixtures
    def make_library(self, bundles, adapter=GPU):
        idx = {}
        for bid in bundles:
            d = self.lib / 'bundles' / bid
            d.mkdir(parents=True, exist_ok=True)
            raw = json.dumps({'adapter': {'luid': '0', 'description': adapter, 'driver': DRIVER}}).encode()
            (d / 'scene.json').write_bytes(raw)
            idx[bid] = {'scene_json_sha256': sha(raw)}
        (self.lib / 'index.json').write_text(json.dumps({'format': 'c5-scene-library-index', 'bundles': idx}))

    def bundle_json(self, fv=9, adapter=GPU, extra=''):
        return json.dumps({'format_version': fv, 'adapter': {'luid': '0', 'description': adapter, 'driver': DRIVER},
                           'members': {'frames.bin': {'bytes': 6}}, 'x': extra}).encode()

    def make_master(self, retired, index=None):
        """The master library: retired/<id>/ (scene.json, frames.bin) for each {id: format_version}, and an index file
        index-before-T.json naming {id: scene.json SHA-256} (`index`: what the base set drew). Returns the root."""
        m = self.tmp / 'master'
        for bid, fv in retired.items():
            d = m / 'retired' / bid
            d.mkdir(parents=True, exist_ok=True)
            (d / 'scene.json').write_bytes(self.bundle_json(fv, extra=bid))
            (d / 'frames.bin').write_bytes(b'frames')
        m.mkdir(exist_ok=True)
        if index is not None:
            (m / 'index-before-T.json').write_text(json.dumps(
                {'format': 'c5-scene-library-index', 'bundles': {k: {'scene_json_sha256': v} for k, v in index.items()}}))
        return m

    def library_hash(self, index):
        """A set manifest's `library`: jhash of {id: scene.json SHA-256} (what live_parts records)."""
        return self.mod.jhash({k: v.lower() for k, v in index.items()})

    def take(self, tag=TAG, server=b'server exe', comp=None, scenes=None, commit=None, dirty=False, x64_out=None):
        self.n += 1
        w = self.tmp / f'take{self.n}'
        w.mkdir()
        build = {'x64_hashes': {'bin/server.exe': {'sha256': sha(server).upper()}},
                 'tools_commit': commit or self.commit, 'tools_dirty': dirty}
        if comp:
            build.update(shell_config='i2', composite_sha256=sha(comp))
        if x64_out:
            build['x64_out'] = str(x64_out)
        stages = [{'name': 'build', 'cl_step': '6', 'status': 'ok', 'outputs': build, 'inputs_sha': 'x'}]
        if scenes:
            stages.append(scenes)
        (w / 'state.json').write_text(json.dumps({'format': 1, 'tag': tag, 'created': 'x', 'config': {}, 'runs': [],
                                                  'stages': stages}))
        return w

    def driver(self, work, base, **cfg):
        config = {'tier': 'intermediate', 'who': 'test5019', 'gate_wait': 0, 'results': str(self.results),
                  'tools_repo': str(self.repo), 'scene_library': str(self.lib), 'scene_asset_root': str(self.aroot),
                  'base_take': str(base), **cfg}
        d = tt.TierDriver(TAG, work, self.desc, config, [('build', '6', 'x'), ('scenes', '-', '5019')],
                          tools=self.tools, stages_dir=tr.STAGES_DIR, out=self.lines.append)
        d.module('scenes').machine_adapters = lambda: self.adapters
        if getattr(self, 'copy_hook', None):
            d.module('scenes')._copy_file = self.copy_hook
        return d

    def run_stage(self, work, base=None, **cfg):
        self.lines.clear()
        return self.driver(work, base or self.base0, **cfg).run(only='scenes')

    def entry(self, work):
        return next(e for e in json.loads((work / 'state.json').read_text())['stages'] if e['name'] == 'scenes')

    def base_set(self, shots=None, base=None, **manifest):
        """The base take's set as the bootstrap leaves it (scenerun's layout and the manifest; `manifest`: more keys,
        e.g. library)."""
        base = base or self.base0
        key, why = self.mod.base_key(json.loads((base / 'state.json').read_text()), ADAPTER)
        self.assertIsNotNone(key, why)
        d = self.results / 'scene-baselines' / key
        for bid, ss in (shots or SHOTS).items():
            (d / bid).mkdir(parents=True, exist_ok=True)
            (d / bid / 'baseline.json').write_text(json.dumps(
                {'bundle_id': bid, 'shots': {s: {'colour_sha256': c, 'depth_sha256': 'd'} for s, c in ss.items()}}))
        (d / 'manifest.json').write_text(json.dumps({'format': 'c5-scene-baseline-set', 'take': f'{BASE_TAG} {base}',
                                                     'adapter': ADAPTER, 'bundles': sorted(shots or SHOTS), **manifest}))
        return d

    def calls_of(self, name):
        return [c for c in (self.calls.read_text().splitlines() if self.calls.is_file() else [])
                if c.startswith(name + ' ')]

    def text(self):
        return '\n'.join(self.lines)

    def sets(self):
        return sorted(p.name for p in (self.results / 'scene-baselines').iterdir()) \
            if (self.results / 'scene-baselines').is_dir() else []


class Fresh(Scenes):
    def test_equal_passes_and_makes_this_takes_set(self):
        bdir = self.base_set()
        w = self.take()
        self.assertEqual(self.run_stage(w), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertEqual(o['counts']['equal'], 4)
        self.assertEqual(o['base_set'], str(bdir))
        self.assertIn('has no scenes stage', o['fresh_why'])
        self.assertNotIn('carried', o)
        run = self.calls_of('scenerun')
        self.assertEqual(len(run), 1)
        self.assertIn('--no-baseline-write', run[0])
        self.assertIn(f'--baseline {bdir}', run[0])
        self.assertIn('--no-record', run[0])
        self.assertIn(f'--asset-root {self.aroot}', run[0])
        self.assertIn('--smoke', self.calls_of('build_sceneshot')[0])
        self.assertNotIn('--c5render', self.calls_of('build_sceneshot')[0])
        self.assertEqual(len(self.calls_of('scenelib')), 1)
        self.assertEqual(len(self.calls_of('quietlock')), 2)   # the build and scenerun go through quietlock.py run
        # this take's set, outside git, keyed by the take, written after the verdict
        new = Path(o['baseline_dir'])
        self.assertEqual(new.parent, self.results / 'scene-baselines')
        self.assertTrue(new.name.startswith(TAG + '__'))
        m = json.loads((new / 'manifest.json').read_text())
        self.assertEqual(m['adapter'], ADAPTER)
        self.assertEqual(m['commit'], self.commit)
        self.assertEqual(m['server_exe'], sha(b'server exe'))
        self.assertEqual(m['shots'], 4)
        self.assertEqual(m['player_sha256'], sha(b'player'))
        bl = json.loads((new / 'B1' / 'baseline.json').read_text())
        self.assertEqual(bl['shots']['1002']['colour_sha256'], 'c12')
        self.assertEqual((new / 'B1' / '1002.png').read_bytes(), b'c12')
        self.assertEqual(o['baseline_key'], new.name)
        self.assertFalse(any(p.name.endswith('.partial') for p in new.parent.iterdir()))

    def test_take_render_lib_is_used_when_the_build_has_it(self):
        self.base_set()
        out = self.tmp / 'x64out'
        (out / 'render' / 'src' / 'units').mkdir(parents=True)
        (out / 'render' / 'c5render.lib').write_bytes(b'lib')
        (out / 'render' / 'src' / 'units' / 'c5r_models.lib').write_bytes(b'lib')
        w = self.take(x64_out=out)
        self.assertEqual(self.run_stage(w), 0, self.text())
        self.assertIn(f'--c5render {out / "render"}', self.calls_of('build_sceneshot')[0])
        # NEGATIVE: without c5r_models.lib beside it the build makes its own c5render
        (out / 'render' / 'src' / 'units' / 'c5r_models.lib').unlink()
        self.calls.unlink()
        w = self.take(x64_out=out)
        self.assertEqual(self.run_stage(w), 0, self.text())
        self.assertNotIn('--c5render', self.calls_of('build_sceneshot')[0])

    def test_regression_stops_naming_each_shot_and_writes_no_set(self):
        self.base_set()
        os.environ['FAKE_SHOTS'] = json.dumps({**SHOTS, 'B1': {'1001': 'c11', '1002': 'CHANGED'},
                                               'B2': {'1001': 'X', '1002': 'c22'}})
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertEqual(e['status'], 'stop')
        what = e['stop']['what']
        self.assertIn('2 shot(s) differ', what)
        self.assertIn('B1 1002: differs (12 pixels differ, largest 40)', what)
        self.assertIn('B2 1001: differs', what)
        self.assertNotIn('B1 1001', what)
        self.assertIn('accept.json', e['stop']['unblock'])
        self.assertEqual([(r['bundle'], r['shot']) for r in e['outputs']['regressions']], [('B1', '1002'), ('B2', '1001')])
        self.assertEqual(len(self.sets()), 1, self.sets())   # only the base set: nothing written on a stop

    def test_accept_file_lets_an_explained_difference_pass_and_is_listed(self):
        self.base_set()
        os.environ['FAKE_SHOTS'] = json.dumps({**SHOTS, 'B1': {'1001': 'c11', '1002': 'CHANGED'}})
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        acc = w / 'scenes' / 'accept.json'
        # NEGATIVE: an accept entry for another shot accepts nothing
        acc.write_text(json.dumps({'accept': [{'bundle': 'B1', 'shot': '1001', 'reason': 'r', 'who': 'owner',
                                               'date': '2026-11-22'}]}))
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('B1 1002: differs', self.entry(w)['stop']['what'])
        acc.write_text(json.dumps({'accept': [{'bundle': 'B1', 'shot': '1002', 'reason': 'the river edge changed by '
                                               'ticket 9999 on purpose', 'who': 'owner', 'date': '2026-11-22'}]}))
        self.assertEqual(self.run_stage(w), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertEqual([(a['bundle'], a['shot'], a['who']) for a in o['accepted']], [('B1', '1002', 'owner')])
        self.assertNotIn('tier_subset', o)                         # listed in its own report section (5023)
        self.assertIn('rejudged', o)                                 # judged again: no second build or draw
        self.assertEqual(len(self.calls_of('scenerun')), 1)
        self.assertEqual(len(self.calls_of('build_sceneshot')), 1)
        new = Path(o['baseline_dir'])
        self.assertEqual((new / 'B1' / '1002.png').read_bytes(), b'CHANGED')   # the accepted picture is the reference
        self.assertEqual(json.loads((new / 'manifest.json').read_text())['accepted'][0]['shot'], '1002')
        # the tier report lists it (tierreport's section "Accepted scene differences", ticket 5023)
        import importlib.util
        spec = importlib.util.spec_from_file_location('tierreport_t', str(tt.TIER_STAGES_DIR / 'tierreport.py'))
        rep = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rep)
        md = rep.markdown(rep.build(json.loads((w / 'state.json').read_text()), ['build', 'scenes']))
        self.assertIn('| B1 | 1002 | 12 pixels differ, largest 40 | the river edge changed by ticket 9999 on purpose | owner | 2026-11-22 |', md)
        # a changed accept file changes the stage's inputs (the driver runs it again); an unchanged one does not
        self.assertEqual(self.run_stage(w), 0, self.text())
        self.assertNotIn('scenes: inputs changed', self.text())
        acc.write_text(acc.read_text().replace('on purpose', 'on purpose (seen by owner)'))
        self.assertEqual(self.run_stage(w), 0, self.text())
        self.assertIn('scenes: inputs changed since it was ok; running again', self.text())
        self.assertEqual(len(self.calls_of('scenerun')), 1)

    def test_invalid_accept_file_stops(self):
        self.base_set()
        w = self.take()
        (w / 'scenes').mkdir()
        (w / 'scenes' / 'accept.json').write_text(json.dumps({'accept': [{'bundle': 'B1', 'shot': '1002',
                                                                          'who': 'owner', 'date': '22.11.2026'}]}))
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('entry 1 has no reason', self.entry(w)['stop']['what'])
        self.assertEqual(self.calls_of('scenerun'), [])

    def test_no_base_set_stops_naming_the_bootstrap_command(self):
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn(f'no baseline set of the base take {BASE_TAG}', e['stop']['what'])
        self.assertIn(f'scenes.py bootstrap --base-take {self.base0.as_posix()} --results {self.results.as_posix()}',
                      e['stop']['unblock'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])
        self.assertEqual(self.calls_of('scenerun'), [])

    def test_bootstrap_makes_the_base_set_then_the_gate_passes(self):
        tools = self.tmp / 'tools.json'
        tools.write_text(json.dumps({k: self.tools[k] for k in ('quietlock', 'build_sceneshot', 'scenelib', 'scenerun')}))
        said = []
        args = ['--base-take', str(self.base0), '--results', str(self.results), '--work', str(self.tmp / 'boot'),
                '--library', str(self.lib), '--asset-root', str(self.aroot), '--tools', str(tools), '--repo',
                str(self.repo), '--who', 'test5019']
        self.assertEqual(self.mod.bootstrap(args, out=said.append), 0, said)
        key = self.mod.base_key(json.loads((self.base0 / 'state.json').read_text()), ADAPTER)[0]
        d = self.results / 'scene-baselines' / key
        m = json.loads((d / 'manifest.json').read_text())
        self.assertEqual((m['bundles'], m['shots'], m['commit']), (list(BUNDLES), 4, self.commit))
        run = self.calls_of('scenerun')[0]
        self.assertIn('--set-baseline', run)
        self.assertIn('--p1 off', run)
        self.assertIn('cwd=' + str((self.tmp / 'boot' / 'src').resolve()), self.calls_of('build_sceneshot')[0])
        self.assertFalse((self.tmp / 'boot' / 'src').exists())                  # its worktree removed afterwards
        self.assertNotIn('boot', git(self.repo, 'worktree', 'list'))
        self.assertEqual(self.mod.bootstrap(args, out=said.append), 0)          # there already: nothing run
        self.assertIn('there already', said[-1])
        self.assertEqual(len(self.calls_of('scenerun')), 1)
        w = self.take()
        self.assertEqual(self.run_stage(w), 0, self.text())
        self.assertEqual(self.entry(w)['outputs']['base_set'], str(d))

    def test_bootstrap_refuses_a_dirty_base_build(self):
        base = self.take(tag=BASE_TAG, dirty=True)
        said = []
        rc = self.mod.bootstrap(['--base-take', str(base), '--results', str(self.results), '--library', str(self.lib),
                                 '--asset-root', str(self.aroot), '--repo', str(self.repo)], out=said.append)
        self.assertEqual(rc, 2)
        self.assertIn('no clean repository commit', said[-1])
        self.assertEqual(self.calls_of('build_sceneshot'), [])


class Carried(Scenes):
    def test_carried_when_every_part_equals_the_base_and_fresh_when_one_changes(self):
        self.base_set()
        w1 = self.take()
        self.assertEqual(self.run_stage(w1), 0, self.text())
        o1 = self.entry(w1)['outputs']
        w2 = self.take()
        self.assertEqual(self.run_stage(w2, base=w1), 0, self.text())
        o2 = self.entry(w2)['outputs']
        self.assertIn('carried', o2)
        self.assertTrue(o2['carried']['line'].startswith(f'carried from {TAG} {w1}'), o2['carried']['line'])
        for k in self.mod.PARTS:
            self.assertEqual(o2['carried']['what'][k], o1['parts'][k])
        self.assertEqual(o2['baseline_dir'], o1['baseline_dir'])      # the next take compares with the same pictures
        self.assertNotIn('minutes', o2)
        self.assertNotIn('seconds', o2)
        self.assertEqual(len(self.calls_of('scenerun')), 1)
        self.assertEqual(len(self.calls_of('build_sceneshot')), 1)
        # a take on the carried one finds the set through it (a carried base's baseline_dir)
        # NEGATIVE: one changed byte of server.exe runs fresh against the base's set
        w3 = self.take(server=b'server exE')
        self.assertEqual(self.run_stage(w3, base=w1), 0, self.text())
        o3 = self.entry(w3)['outputs']
        self.assertNotIn('carried', o3)
        self.assertIn('server_exe', o3['fresh_why'])
        self.assertEqual(o3['base_set'], o1['baseline_dir'])
        self.assertEqual(len(self.calls_of('scenerun')), 2)

    def test_each_part_changed_runs_fresh(self):
        self.base_set()
        w1 = self.take()
        self.assertEqual(self.run_stage(w1), 0, self.text())
        # the compositor
        w = self.take(comp=b'composite')
        self.assertEqual(self.run_stage(w, base=w1), 0, self.text())
        self.assertIn('compositor', self.entry(w)['outputs']['fresh_why'])
        # the player's sources (a changed file in the repository)
        (self.repo / 'render/src/x.cpp').write_bytes(b'changed\n')
        git(self.repo, 'commit', '-q', '-am', 'render change')
        w = self.take(commit=git(self.repo, 'rev-parse', 'HEAD'))
        self.assertEqual(self.run_stage(w, base=w1), 0, self.text())
        self.assertIn('player_sources', self.entry(w)['outputs']['fresh_why'])
        # the asset root
        (self.aroot / 'asset-root.txt').write_text('db def\n')
        w = self.take(commit=git(self.repo, 'rev-parse', 'HEAD'))
        self.assertEqual(self.run_stage(w, base=w1), 0, self.text())
        self.assertIn('asset_root', self.entry(w)['outputs']['fresh_why'])
        # the adapter's driver
        self.adapters = [{'name': GPU, 'driver': '33.0'}]
        w = self.take(commit=git(self.repo, 'rev-parse', 'HEAD'))
        self.assertEqual(self.run_stage(w, base=w1), tr.EXIT_STOP, self.text())   # no base set on this driver
        self.assertIn('adapter', self.entry(w)['outputs']['fresh_why'])
        self.assertIn('scenes.py bootstrap', self.entry(w)['stop']['unblock'])

    def test_library_changed_runs_fresh(self):
        self.base_set()
        w1 = self.take()
        self.assertEqual(self.run_stage(w1), 0, self.text())
        self.make_library(BUNDLES + ('B3',))
        os.environ['FAKE_SHOTS'] = json.dumps({**SHOTS, 'B3': {'1001': 'c31'}})
        w = self.take()
        self.assertEqual(self.run_stage(w, base=w1), 0, self.text())   # B3 is first sight (MC #10544 A)
        self.assertIn('library', self.entry(w)['outputs']['fresh_why'])

    def test_carry_needs_the_base_set_still_there(self):
        self.base_set()
        w1 = self.take()
        self.assertEqual(self.run_stage(w1), 0, self.text())
        import shutil
        shutil.rmtree(self.entry(w1)['outputs']['baseline_dir'])
        w = self.take()
        self.assertEqual(self.run_stage(w, base=w1), tr.EXIT_STOP, self.text())   # and no set to compare with
        e = self.entry(w)
        self.assertNotIn('carried', e['outputs'])
        self.assertIn("the base take's set", e['outputs']['fresh_why'])
        self.assertIn('scenes.py bootstrap', e['stop']['unblock'])

    def test_unreadable_part_is_never_carried(self):
        self.base_set()
        w1 = self.take()
        self.assertEqual(self.run_stage(w1), 0, self.text())
        (self.aroot / 'asset-root.txt').unlink()
        w = self.take()
        self.assertEqual(self.run_stage(w, base=w1), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('cannot prove its inputs', e['stop']['what'])
        self.assertIn('asset_root', e['stop']['what'])


class Negatives(Scenes):
    def test_another_adapter_stops_before_anything_runs(self):
        self.base_set()
        self.adapters = [{'name': 'NVIDIA GeForce RTX 2080', 'driver': '31.0'}]
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn("another adapter than the scene bundles'", e['stop']['what'])
        self.assertIn('RTX 2080', e['stop']['what'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])

    def test_report_drawn_on_another_adapter_stops(self):
        self.base_set()
        os.environ['FAKE_ADAPTER'] = 'Microsoft Basic Render Driver'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn("drawn on another adapter than the bundles'", self.entry(w)['stop']['what'])
        self.assertEqual(len(self.sets()), 1)

    def test_player_no_device_is_an_adapter_stop(self):
        self.base_set()
        os.environ.update(FAKE_SCENERUN_RC='5', FAKE_SCENERUN_WHY='PlayerFailed: B1: exit 7: sceneshot: FAILED 7 NoDevice')
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('NoDevice', self.entry(w)['stop']['what'])

    def test_p1_differs_is_a_stop_and_other_exits_fail(self):
        self.base_set()
        os.environ['FAKE_SCENERUN_RC'] = '6'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('offline does not equal live', self.entry(w)['stop']['what'])
        os.environ['FAKE_SCENERUN_RC'] = '4'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_FAILED, self.text())
        self.assertIn('scenerun.py exit 4', self.entry(w)['error'])
        self.assertEqual(len(self.sets()), 1)

    def test_exit_7_without_a_differing_shot_fails(self):
        self.base_set()
        os.environ['FAKE_SCENERUN_RC'] = '7'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_FAILED, self.text())
        self.assertIn('does not match the exit', self.entry(w)['error'])

    def test_build_failure_fails_and_scenelib_failure_stops(self):
        self.base_set()
        os.environ['FAKE_BUILD_RC'] = '1'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_FAILED, self.text())
        self.assertIn('build_sceneshot.py exit 1', self.entry(w)['error'])
        self.assertEqual(self.calls_of('scenerun'), [])
        os.environ.pop('FAKE_BUILD_RC')
        os.environ['FAKE_SCENELIB_RC'] = '5'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('fails scenelib check', e['stop']['what'])
        self.assertIn('scenelib.py sync', e['stop']['unblock'])
        self.assertEqual(self.calls_of('scenerun'), [])


    def test_base_shot_not_made_stops_and_a_new_shot_passes_listed(self):
        self.base_set({'B1': {**SHOTS['B1'], '1003': 'c13'}, 'B2': SHOTS['B2']})
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('shot(s) of the base set not made: B1 1003', self.entry(w)['stop']['what'])
        # a shot the base set does not have, in a bundle it covers: new, listed, passes
        self.base_set({'B1': {'1001': 'c11'}, 'B2': SHOTS['B2']})
        w = self.take()
        self.assertEqual(self.run_stage(w), 0, self.text())
        self.assertEqual([(r['bundle'], r['shot']) for r in self.entry(w)['outputs']['new_shots']], [('B1', '1002')])

    def test_repository_not_at_the_builds_commit_stops(self):
        self.base_set()
        w = self.take(commit='0' * 40)
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn("player must be built from the take's own sources", self.entry(w)['stop']['what'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])
        (self.repo / 'server/src/x.cpp').write_bytes(b'uncommitted\n')
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('uncommitted changes: yes', self.entry(w)['stop']['what'])

    def test_a_set_made_again_keeps_every_earlier_one(self):
        png = self.tmp / 'p.png'
        png.write_bytes(b'c')
        rep = {'run_id': 'r', 'bundles': [{'bundle_id': 'B1', 'adapter': GPU, 'shots': [
            {'name': '1001', 'png': str(png), 'colour_sha256': 'c', 'depth_sha256': 'd'}]}]}
        root = self.tmp / 'sets'
        for _ in range(3):   # three in one second: the earlier ones get free names, none is lost
            self.mod.write_set(root, 'K', rep, {'made': 'now'})
        self.assertEqual(len([p for p in root.iterdir() if p.name.startswith('K.old-')]), 2)
        self.assertEqual(json.loads((root / 'K' / 'manifest.json').read_text())['shots'], 1)

    def test_base_set_on_another_driver_is_not_used(self):
        d = self.base_set()
        m = json.loads((d / 'manifest.json').read_text())
        m['adapter'] = f'{GPU} driver 31.0'
        (d / 'manifest.json').write_text(json.dumps(m))
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('another adapter', self.entry(w)['stop']['what'])
        self.assertEqual(self.calls_of('scenerun'), [])


RSHOTS = {**SHOTS, 'R1': {'1001': 'r11', '1002': 'r12'}, 'N1': {'1001': 'n11'}}


class LibraryChange(Scenes):
    """MC #10544 (ticket 5024): a library recorded again. (C) the base set's own bundles, retired ones copied from the
    master's retired/ at the stage's start, verified and compared like for like; (A) bundles the base set does not cover
    are first sight. Each check with the case that passes it and the case that stops on it."""

    def rerecorded(self, fv=9, retired=True, recorded_r1=None, library=True):
        """R1 re-recorded as N1: the base set covers B1, B2 and R1; the library copy holds B1, B2 and N1; R1 is in the
        master's retired/ (format `fv`). The master's index-before-T.json is the index the base set drew (its R1 entry
        `recorded_r1`, default the retired bundle's own hash). Returns a take."""
        self.make_library(BUNDLES + ('N1',))
        idx = json.loads((self.lib / 'index.json').read_text())['bundles']
        r1 = sha(self.bundle_json(fv, extra='R1'))
        index = {'B1': idx['B1']['scene_json_sha256'], 'B2': idx['B2']['scene_json_sha256'], 'R1': recorded_r1 or r1}
        self.master = self.make_master({'R1': fv} if retired else {}, index=index)
        os.environ['FAKE_SHOTS'] = json.dumps(RSHOTS)
        self.bdir = self.base_set({**SHOTS, 'R1': RSHOTS['R1']},
                                  **({'library': self.library_hash(index)} if library else {}))
        self.r1 = r1
        return self.take()

    def run_m(self, w, base=None, **cfg):
        return self.run_stage(w, base, **{'scene_master': str(self.master), **cfg})

    def test_retired_bundle_is_copied_verified_and_compared(self):
        w = self.rerecorded()
        self.assertEqual(self.run_m(w), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertEqual(o['compared'], 3)
        self.assertEqual(o['counts']['equal'], 6)                   # B1, B2 and the retired R1: 2 shots each
        self.assertEqual(o['new_shots'], [])
        self.assertEqual([f['bundle'] for f in o['first_sight']], ['N1'])
        self.assertEqual((o['master'], o['master_from']), (str(self.master), 'config scene_master'))
        self.assertEqual((o['read_floor'], o['retired_index']), (9, 'index-before-T.json'))
        r = o['retired']['R1']
        self.assertEqual((r['how'], r['files'], r['scene_json_sha256']), ('copied', 2, self.r1))
        self.assertEqual(r['expect_from'], "the master's index-before-T.json")
        self.assertNotEqual(o['parts']['retired'], 'none')
        rdir = w / 'scenes' / 'retired'
        for f in ('scene.json', 'frames.bin'):                      # the copy equals its source, byte for byte
            self.assertEqual((rdir / 'bundles' / 'R1' / f).read_bytes(), (self.master / 'retired' / 'R1' / f).read_bytes())
        rec = json.loads((rdir / 'copy-record.json').read_text())
        self.assertEqual(rec['bundles']['R1']['files'], {'scene.json': self.r1, 'frames.bin': sha(b'frames')})
        self.assertEqual(json.loads((rdir / 'index.json').read_text())['bundles'], {'R1': {'scene_json_sha256': self.r1}})
        runs = self.calls_of('scenerun')
        self.assertEqual(len(runs), 2)
        self.assertIn(f'--library {self.lib} ', runs[0])
        self.assertNotIn('--p1 off', runs[0])                       # the library's run keeps P1 (rule A)
        self.assertIn(f'--library {rdir} ', runs[1])                 # the retired copies: from C:, never the master
        self.assertNotIn(str(self.master), runs[1])
        self.assertIn('--p1 off', runs[1])
        self.assertIn(f'--baseline {self.bdir} ', runs[1])
        self.assertIn('--no-baseline-write', runs[1])
        self.assertEqual(len(self.calls_of('quietlock')), 3)
        # this take's set: the base set's own bundles (R1 under its old id) and the first-sight N1
        new = Path(o['baseline_dir'])
        m = json.loads((new / 'manifest.json').read_text())
        self.assertEqual(sorted(m['bundles']), ['B1', 'B2', 'N1', 'R1'])
        self.assertEqual((m['retired'], m['first_sight']), ({'R1': self.r1}, ['N1']))
        self.assertEqual((new / 'N1' / '1001.png').read_bytes(), b'n11')
        self.assertEqual((new / 'R1' / '1002.png').read_bytes(), b'r12')
        # a run again in the same folder keeps a copy whose master is unchanged, after hashing it again on C:
        acc = w / 'scenes' / 'accept.json'
        acc.write_text(json.dumps({'accept': [{'bundle': 'B1', 'shot': '1009', 'reason': 'r', 'who': 'owner',
                                               'date': '2026-11-22'}]}))
        (w / 'scenes' / 'run-inputs.json').unlink()
        self.assertEqual(self.run_m(w), 0, self.text())
        self.assertTrue(self.entry(w)['outputs']['retired']['R1']['how'].startswith('kept'))
        # ... and copies again a copy that was changed
        (rdir / 'bundles' / 'R1' / 'frames.bin').write_bytes(b'framez')
        acc.write_text(acc.read_text().replace('"r"', '"r2"'))
        (w / 'scenes' / 'run-inputs.json').unlink()
        self.assertEqual(self.run_m(w), 0, self.text())
        self.assertEqual(self.entry(w)['outputs']['retired']['R1']['how'], 'copied')
        self.assertEqual((rdir / 'bundles' / 'R1' / 'frames.bin').read_bytes(), b'frames')
        # NEGATIVE: compared like for like: a changed picture of the retired bundle is a regression STOP
        os.environ['FAKE_SHOTS'] = json.dumps({**RSHOTS, 'R1': {'1001': 'r11', '1002': 'CHANGED'}})
        w2 = self.take(server=b'server 2')
        self.assertEqual(self.run_m(w2), tr.EXIT_STOP, self.text())
        self.assertIn('R1 1002: differs', self.entry(w2)['stop']['what'])

    def test_master_from_the_copys_manifest(self):
        w = self.rerecorded()
        (self.lib / 'scenelib-manifest.json').write_text(json.dumps({'master': str(self.master)}))
        self.assertEqual(self.run_stage(w), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertEqual((o['master'], o['master_from']), (str(self.master), 'scenelib-manifest.json of the copy'))

    def test_copy_that_differs_from_its_source_stops(self):
        w = self.rerecorded()

        def bad(src, dst):   # a copy that does not keep the bytes it read
            data = Path(src).read_bytes()
            Path(dst).write_bytes(data + b'!' if Path(src).name == 'frames.bin' else data)
            return sha(data)
        self.copy_hook = bad
        self.assertEqual(self.run_m(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('a copy of a retired bundle of the base set is not its source', e['stop']['what'])
        self.assertIn('frames.bin', e['stop']['what'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])     # copied at the stage's start, before the build
        self.assertEqual(self.calls_of('scenerun'), [])

    def test_retired_bundle_not_the_one_the_base_set_drew_stops(self):
        w = self.rerecorded(recorded_r1='0' * 64)
        self.assertEqual(self.run_m(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('are not the bundles the base set drew', e['stop']['what'])
        self.assertIn("the master's index-before-T.json says 0000000000000000", e['stop']['what'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])

    def test_no_matching_index_falls_back_to_copy_equals_source_recorded(self):
        w = self.rerecorded(library=False)
        self.assertEqual(self.run_m(w), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertIn("records no `library`", o['retired_index'])
        self.assertIn('copy equals source only', o['retired']['R1']['expect_from'])
        self.assertEqual(o['retired']['R1']['how'], 'copied')

    def test_first_sight_bundle_is_listed_and_not_compared(self):
        self.base_set()
        self.make_library(BUNDLES + ('N1',))
        os.environ['FAKE_SHOTS'] = json.dumps(RSHOTS)
        w = self.take()
        self.assertEqual(self.run_stage(w), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertEqual(o['first_sight'], [{'bundle': 'N1', 'shots': ['1001']}])
        self.assertEqual(o['new_shots'], [])                         # not compared: not counted as a new shot
        self.assertEqual((o['counts']['equal'], o['counts']['new'], o['counts']['first_sight']), (4, 0, 1))
        self.assertEqual(o['compared'], 2)
        self.assertNotIn('master', o)                                # nothing retired: the master is not needed
        new = Path(o['baseline_dir'])
        self.assertEqual((new / 'N1' / '1001.png').read_bytes(), b'n11')   # its pictures join this take's set
        self.assertEqual(json.loads((new / 'manifest.json').read_text())['first_sight'], ['N1'])
        import importlib.util
        spec = importlib.util.spec_from_file_location('tierreport_fs', str(tt.TIER_STAGES_DIR / 'tierreport.py'))
        rep = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rep)
        md = rep.markdown(rep.build(json.loads((w / 'state.json').read_text()), ['build', 'scenes']))
        self.assertIn("## First-sight scene bundles (drawn, not compared; their pictures joined this take's set)", md)
        self.assertIn('| N1 | 1 |', md)
        # NEGATIVE: a take without a first-sight bundle has no such section
        w2 = self.take(server=b'server 2')
        self.make_library(BUNDLES)
        self.assertEqual(self.run_stage(w2), 0, self.text())
        md = rep.markdown(rep.build(json.loads((w2 / 'state.json').read_text()), ['build', 'scenes']))
        self.assertNotIn('First-sight scene bundles', md)

    def test_bundle_in_neither_library_nor_retired_is_loud(self):
        w = self.rerecorded(retired=False)
        self.assertEqual(self.run_m(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertTrue(e['stop']['what'].startswith('LOUD: the retire rule was broken: 1 bundle(s)'), e['stop']['what'])
        self.assertIn('R1', e['stop']['what'])
        self.assertIn('retired', e['stop']['unblock'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])
        # the master not reachable at all is its own STOP (not a broken retire rule)
        w = self.take(server=b'server 2')
        self.assertEqual(self.run_m(w, scene_master=str(self.tmp / 'no-master')), tr.EXIT_STOP, self.text())
        self.assertIn('the master is not reachable', self.entry(w)['stop']['what'])

    def test_retired_bundle_below_the_read_floor_stops(self):
        w = self.rerecorded(fv=8)
        self.assertEqual(self.run_m(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn("below this take's read floor (format 9, server/src/render/srv_scene_bundle.h", e['stop']['what'])
        self.assertIn('R1 (format_version 8)', e['stop']['what'])
        self.assertEqual(self.calls_of('build_sceneshot'), [])

    def test_player_refusing_a_retired_bundle_as_older_stops(self):
        w = self.rerecorded()
        os.environ.update(FAKE_RETIRED_RC='5', FAKE_RETIRED_WHY='PlayerFailed: R1: exit 26: sceneshot: FAILED 26 OlderVersion')
        self.assertEqual(self.run_m(w), tr.EXIT_STOP, self.text())
        self.assertIn('refuses retired bundles of the base set as OlderVersion', self.entry(w)['stop']['what'])

    def test_newer_version_on_a_library_bundle_stops(self):
        self.base_set()
        os.environ.update(FAKE_SCENERUN_RC='5', FAKE_SCENERUN_WHY='PlayerFailed: B1: exit 27: sceneshot: FAILED 27 NewerVersion')
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('as NewerVersion: the candidate is older than the library', e['stop']['what'])
        self.assertIn('rebuild the candidate from a newer main', e['stop']['unblock'])
        # NEGATIVE: another player failure (exit 5) still fails the stage
        os.environ['FAKE_SCENERUN_WHY'] = 'PlayerFailed: B1: exit 3: sceneshot: FAILED 3 BadArgs'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_FAILED, self.text())
        self.assertIn('scenerun.py exit 5', self.entry(w)['error'])

    def test_no_base_set_bundle_left_stops(self):
        self.base_set()
        self.make_library(BUNDLES + ('N1',))
        os.environ.update(FAKE_SHOTS=json.dumps(RSHOTS), FAKE_OMIT='B1,B2')
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('nothing left to compare', e['stop']['what'])
        self.assertIn('1 first-sight bundle(s) only', e['stop']['what'])
        self.assertEqual(len(self.sets()), 1)
        # NEGATIVE: one base-set bundle drawn is enough to compare; the other is a missing-shots STOP
        os.environ['FAKE_OMIT'] = 'B2'
        w = self.take()
        self.assertEqual(self.run_stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('B2 1001 (bundle not drawn)', self.entry(w)['stop']['what'])

    def test_unchanged_library_behaves_as_before(self):
        """Negative control: every base-set bundle in the library: one run, no copy, the master never needed."""
        self.base_set()
        w = self.take()
        self.assertEqual(self.run_stage(w, scene_master=str(self.tmp / 'no-master')), 0, self.text())
        o = self.entry(w)['outputs']
        self.assertEqual(o['parts']['retired'], 'none')
        for k in ('master', 'retired', 'report_retired', 'retired_index'):
            self.assertNotIn(k, o)
        self.assertEqual((o['first_sight'], o['compared'], o['counts']['equal']), ([], 2, 4))
        self.assertFalse((w / 'scenes' / 'retired').exists())
        self.assertEqual(len(self.calls_of('scenerun')), 1)
        self.assertEqual(len(self.calls_of('quietlock')), 2)
        m = json.loads((Path(o['baseline_dir']) / 'manifest.json').read_text())
        self.assertEqual((m['retired'], m['first_sight']), ({}, []))
        # a base result recorded before MC #10544 (no part `retired`) is still carried
        st = json.loads((w / 'state.json').read_text())
        for e in st['stages']:
            if e['name'] == 'scenes':
                e['outputs']['parts'].pop('retired')
        (w / 'state.json').write_text(json.dumps(st))
        w2 = self.take()
        self.assertEqual(self.run_stage(w2, base=w), 0, self.text())
        self.assertIn('carried', self.entry(w2)['outputs'])

    def test_carried_with_retired_bundles_and_fresh_when_the_retired_bundle_changes(self):
        w1 = self.rerecorded()
        self.assertEqual(self.run_m(w1), 0, self.text())
        w2 = self.take()
        self.assertEqual(self.run_m(w2, base=w1), 0, self.text())
        o2 = self.entry(w2)['outputs']
        self.assertIn('carried', o2)
        self.assertEqual(o2['carried']['what']['retired'], self.entry(w1)['outputs']['parts']['retired'])
        self.assertEqual(len(self.calls_of('scenerun')), 2)          # w1's two runs only
        # the master's retired bundle changed: runs fresh, and it is no longer the bundle w1's set drew
        (self.master / 'retired' / 'R1' / 'scene.json').write_bytes(self.bundle_json(9, extra='R1 again'))
        w3 = self.take()
        self.assertEqual(self.run_m(w3, base=w1), tr.EXIT_STOP, self.text())
        e = self.entry(w3)
        self.assertIn('retired', e['outputs']['fresh_why'])
        self.assertIn("the base set's manifest (retired) says", e['stop']['what'])


if __name__ == '__main__':
    unittest.main()
