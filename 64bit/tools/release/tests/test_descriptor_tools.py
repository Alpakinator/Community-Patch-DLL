"""Issue 3501: every tool that hard-wired 5.4.8 reads the release descriptor, and on the 5.4.8 descriptor its values are
the ones it held before (behaviour on the current pins unchanged). Hermetic: the tools are imported, nothing is run
and no game file is opened; the refusal case starts one Python process with C5_RELEASE naming no release."""
from __future__ import annotations

import importlib.util
import json
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import descriptor  # noqa: E402

# The 5.4.8 values as the descriptor file writes them, read here as plain JSON (not through descriptor.py), so the
# test still compares each tool with the file and names no folder of this machine (ticket 4711).
_RAW = json.loads((REPO / 'tools' / 'release' / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
TOOLKIT = _RAW['game_dir']
PINNED = _RAW['test_db'].rsplit('/', 1)[0]
HUGE_SAVE = _RAW['reference_saves']['huge']['path']
assert TOOLKIT.endswith("/VP_MODPACK_5_4_8_NEUTRALS/Sid Meier's Civilization V"), TOOLKIT
assert PINNED.endswith('/civ5-headless/pinned/vp-modpack-5.4.8-cdrift'), PINNED
assert _RAW['loc_db'] == PINNED + '/Localization-Merged.db'

# The files issue 3501 converted (tools/release/README.md section 6, and the MC's grant of 2026-11-13).
OWNED = ['tools/d10/run_d10.py', 'tools/drift/run_pair.py', 'tools/drift/run_huge.py', 'server/run_server.py',
         'proxy/tools/test_dbfp.py', 'proxy/tools/test_proxy_rt.py', 'server/tools/explore_sets.py', 'tools/i0/ab.py',
         'tools/d10/savecmp.py', 'perf/turnbench/turnbench.py', 'tools/testbed/jobs/longgames.py',
         # issue 3502 (after 0131 handed them back, lead 2026-11-13)
         'tools/x64-dll/build_all.py', 'tools/x64-dll/cvgamedatabase/ab/run_ab.py',
         'tools/x64-dll/cvlocalization/ab/run_ab.py']
# A 5.4.8 value as code: a path, a commit, a database name, a GUID:version pair or the strategy count as a default.
CONSTANT = re.compile(r"VP_MODPACK_5_4_8|vp-modpack-5\.4\.8|vp548|7307aefdf|E2000-HUGE|c499d3c6|bd9b8aa1"
                      r"|:151'|:17'|default=37\b|default=350\b")


def load(rel, name):
    spec = importlib.util.spec_from_file_location(name, REPO / rel)
    m = importlib.util.module_from_spec(spec)
    old = list(sys.path)
    try:
        spec.loader.exec_module(m)
    finally:
        sys.path[:] = old
    return m


def code_lines(rel):
    """(line number, text) of the lines outside docstrings and comments."""
    out, in_doc = [], False
    for i, line in enumerate((REPO / rel).read_text(encoding='utf-8').splitlines(), 1):
        s = line.strip()
        n = s.count('"""') + s.count("'''")
        if in_doc or (n == 1 and s[:3] in ('"""', "'''")) or (n >= 2 and s[:3] in ('"""', "'''")):
            if n % 2 == 1:
                in_doc = not in_doc
            continue
        if s.startswith('#'):
            continue
        out.append((i, line.split('  #')[0]))
    return out


class ToolsReadTheDescriptor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ.pop(descriptor.ENV_TAG, None)
        cls.d = descriptor.load()
        assert cls.d.tag == 'Release-5.4.8', 'these checks hold the 5.4.8 values; current.json moved'

    def test_run_d10(self):
        m = load('tools/d10/run_d10.py', 'rd10_3501')
        self.assertEqual(m.TOOLKIT_GAMEDIR, self.d.game_dir)
        self.assertEqual(m.TOOLKIT_GAMEDIR.as_posix(), TOOLKIT)
        self.assertEqual(m.MODS_DB.as_posix(), PINNED + '/Civ5DebugDatabase.db')
        self.assertTrue(self.d.upstream_commit.startswith('7307aefdf'))
        self.assertEqual(m.MODS_COMMIT, self.d.upstream_commit)
        self.assertEqual(m.MOD_FOLDERS, ['(1) Community Patch', '(2) Vox Populi'])
        self.assertEqual(m.DEFAULT_HUGE.as_posix(), HUGE_SAVE)
        # ticket 0533: the durable copy (was the temp folder's E2000-HUGE_0350.Civ5Save, gone; same bytes)
        self.assertTrue(HUGE_SAVE.endswith('/reference-saves/Release-5.4.8-HUGE_0350.Civ5Save'), HUGE_SAVE)
        self.assertEqual(m.HUGE_SHA256, 'c499d3c6cf83b8baacb57eb950302b68e0aff80510acd5cdbdbdfdd0535188c3')
        self.assertEqual(m.GAMES['huge'], {'start': 350, 'early': 375, 'final': 400})

    def test_drift(self):
        p = load('tools/drift/run_pair.py', 'run_pair')  # run_huge imports it by this name
        self.assertEqual(p.GAMEDIR.as_posix(), TOOLKIT)
        self.assertEqual(p.OFFICIAL_X86.as_posix(),
                         TOOLKIT + '/Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS/Mods/(1) Community Patch/CvGameCore_Expansion2.dll')
        self.assertEqual(p.LOC_DB.as_posix(), PINNED + '/Localization-Merged.db')
        sys.modules['run_pair'] = p
        try:
            h = load('tools/drift/run_huge.py', 'rh_3501')
        finally:
            sys.modules.pop('run_pair', None)
        self.assertEqual(h.DB.as_posix(), PINNED + '/Civ5DebugDatabase.db')
        self.assertEqual(h.MODS, ['d1b6328c-ff44-4b0d-aad7-c657f83610cd:151', '8411a7a8-dad3-4622-a18e-fcc18324c799:17'])

    def test_run_server(self):
        m = load('server/run_server.py', 'rs_3501')
        self.assertEqual(Path(m.GAME_DIR).as_posix(), TOOLKIT)
        # was the copy cv64-deps/testdb/vp548-cdrift-Civ5DebugDatabase.db: the same bytes (bd9b8aa1...)
        self.assertEqual(Path(m.DEFAULT_DB).as_posix(), PINNED + '/Civ5DebugDatabase.db')
        self.assertEqual(Path(m.DEFAULT_LOC_DB).as_posix(), PINNED + '/Localization-Merged.db')

    def test_proxy_tools(self):
        f = load('proxy/tools/test_dbfp.py', 'dbfp_3501')
        self.assertEqual(f.CLOSED_DIR.as_posix(), TOOLKIT)
        self.assertEqual(f.DB, self.d.test_db)
        r = load('proxy/tools/test_proxy_rt.py', 'prt_3501')
        self.assertEqual(Path(r.DEFAULT_LUA_DIR).as_posix(), TOOLKIT)

    def test_explore_sets_and_ab(self):
        e = load('server/tools/explore_sets.py', 'es_3501')
        self.assertEqual(e.STRATEGIES, 37)
        a = load('tools/i0/ab.py', 'ab_3501')
        self.assertEqual(a.GAME_DIR.as_posix(), TOOLKIT)

    def test_longgames(self):
        src = (REPO / 'tools/testbed/jobs/longgames.py').read_text(encoding='utf-8')
        self.assertIn('MODS = descriptor.load().mod_args', src)
        self.assertEqual(self.d.mod_args, ['d1b6328c-ff44-4b0d-aad7-c657f83610cd:151',
                                           '8411a7a8-dad3-4622-a18e-fcc18324c799:17'])

    def test_turnbench_pin_defaults(self):
        sys.path.insert(0, str(REPO / 'tools/d10'))
        try:
            t = load('perf/turnbench/turnbench.py', 'tb_3501')
        finally:
            sys.path.remove(str(REPO / 'tools/d10'))
        self.assertEqual(t.PINNED_DB, self.d.test_db)
        src = (REPO / 'perf/turnbench/turnbench.py').read_text(encoding='utf-8')
        self.assertIn("pin_srv_bin = getattr(run_d10, 'DEFAULT_SRV_BIN', None)", src)
        self.assertIn("pin_x86_dll = getattr(run_d10, 'DEFAULT_X86_DLL', None)", src)
        self.assertIn("'--x64-bin', type=Path, default=pin_srv_bin", src)
        self.assertIn("'--x86-dll', type=Path, default=pin_x86_dll", src)

    def test_x64_dll_tools(self):
        # issue 3502: build_all's smoke database and the two A/B tools' folders and databases
        b = load('tools/x64-dll/build_all.py', 'ba_3502')
        # was the copy <cache root>/testdb/vp548-cdrift-Civ5DebugDatabase.db: the same bytes (bd9b8aa1...)
        self.assertEqual(b.default_db().as_posix(), PINNED + '/Civ5DebugDatabase.db')
        # MC #414: the release driver's test database first ($C5_RELEASE_TEST_DB), then the descriptor's
        os.environ['C5_RELEASE_TEST_DB'] = 'C:/some/release/testdata/db/Civ5DebugDatabase.db'
        try:
            self.assertEqual(b.default_db().as_posix(), 'C:/some/release/testdata/db/Civ5DebugDatabase.db')
        finally:
            os.environ.pop('C5_RELEASE_TEST_DB')
        self.assertEqual(b.default_db().as_posix(), PINNED + '/Civ5DebugDatabase.db')
        g = load('tools/x64-dll/cvgamedatabase/ab/run_ab.py', 'abdb_3502')
        self.assertEqual(g.CLOSED_DIR.as_posix(), TOOLKIT)
        self.assertEqual(g.DB_MASTER.as_posix(), PINNED + '/Civ5DebugDatabase.db')
        self.assertEqual(g.DB_SHA, 'bd9b8aa1c2a83cff0a935dafc6239a509d3c331a4ba871d20682b6aaddfadd6f')
        loc = load('tools/x64-dll/cvlocalization/ab/run_ab.py', 'abloc_3502')
        self.assertEqual(loc.CLOSED_DIR.as_posix(), TOOLKIT)
        self.assertEqual(loc.LOCDB.as_posix(), PINNED + '/Localization-Merged.db')
        self.assertEqual(loc.LOCDB_SHA, 'ff9a0997d643da690f3d548ff4a46d11907869483a4277250449fa352f156be0')

    def test_no_5_4_8_constant_left_in_code(self):
        hits = []
        for rel in OWNED:
            for i, text in code_lines(rel):
                if CONSTANT.search(text):
                    hits.append(f'{rel}:{i}: {text.strip()}')
        self.assertEqual(hits, [])

    def test_an_unknown_release_stops_the_tool(self):
        env = dict(os.environ, C5_RELEASE='Release-0.0.0-3501')
        p = subprocess.run([sys.executable, '-I', '-c',
                            f"import runpy,sys; sys.argv=['run_server.py','--help']; "
                            f"runpy.run_path({(REPO / 'server/run_server.py').as_posix()!r}, run_name='__main__')"],
                           env=env, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('DescriptorError', p.stderr)
        self.assertIn("unknown release 'Release-0.0.0-3501'", p.stderr)
        self.assertEqual(p.stdout, '')


if __name__ == '__main__':
    unittest.main()
