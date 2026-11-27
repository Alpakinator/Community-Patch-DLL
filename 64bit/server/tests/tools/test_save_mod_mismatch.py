"""Ticket 0681: a save loaded with a different set of mods is told to the player (server/src/remote/srv_remote_savemods.*).

    python -I -X utf8 server/tests/tools/test_save_mod_mismatch.py              (hermetic checks only)
    set C5_SERVER_BIN=<build_server out>/bin                                    (also the offline load arms)
    set C5_0681_WORK=<folder laid by server/evidence/0681/setup_repro.py>       (the save copy and the two databases)
    set C5_0681_ASAN=1                                                          (also the pure block under MSVC ASan)

Hermetic (needs the x64 clang-cl of tools/x64-dll/build.py): the "BEGIN 0681 PURE" block of srv_remote_savemods.cpp is
compiled on its own with a test main: the DLL's CustomMods.log text of CvGame::Read's mismatch warning gives both
hashes, any other text is refused, a warning without its hash lines still counts; the body's 16 hash bytes print as the
DLL prints them (the crash save's bytes give the hash its warning named); the mods line names the header's mods, the
active ones, the difference (none / another version / only in the save / only active, never a mod that saves do not
list) and the AffectsSavedGames = 0 mods; the player's summary says it is not a failure of the patch. When
C5_0681_WORK holds the save copy: its body bytes 4..19 are that hash (server/tools/civ5save.py inflates it).
Offline arms (C5_SERVER_BIN and C5_0681_WORK): server/run_server.py --load of the save copy with the database of the
session that made it loads (exit 0, body used up exactly, no mismatch line); with the crash session's database it
fails (exit 2) and server.log carries the LOUD mismatch line with both hashes, the header's mods and the FAILED end
line (the negative arm: the outputs and the exit code after the failure are checked).
"""
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = REPO / 'server' / 'src' / 'remote' / 'srv_remote_savemods.cpp'
HDR = REPO / 'server' / 'src' / 'remote' / 'srv_remote_savemods.h'
BLOCK = re.compile(r'^// BEGIN 0681 PURE.*?^// END 0681 PURE$', re.S | re.M)
NOWIN = 0x08000000
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''
SAVE_HASH = '0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49'   # the crash save's (2026-11-22, CustomMods.log of the crash)
GAME_HASH = '0x4510836f-0x6b3aac37-0x0fa52c26-0x20ec3e4f'   # the crash session's running game
SAVE_SHA256 = 'f1f153f4c8baa2055ae6855b6116e587e5d2702cc055248b0b64a4ed9265c29d'

TEST_MAIN = r'''
#include "srv_remote_savemods.h"
#include <stdio.h>
#include <string.h>
#include "savemods_pure.gen.h"
using namespace SrvSaveMods;
static int g_fail = 0;
static void check(const char* name, bool ok, const std::string& got)
{
	printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
	if(!ok) { printf("      got: %s\n", got.c_str()); ++g_fail; }
}
static Mod M(const char* id, unsigned v, const char* name, int aff)
{
	Mod m; m.id = id; m.version = v; m.name = name; m.affectsSaves = aff; return m;
}
static bool has(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }
int main()
{
	const std::string dll =
	    "CvGameCoreDLL_Expansion2\\CvGame.cpp[11017]: CvGame::Read - WARNING - Save data hash mismatch!\n"
	    "\tLoad will be attempted but corruption or crash is likely.\n"
	    "\tSave Hash = [0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49]\n"
	    "\tGame Hash = [0x4510836f-0x6b3aac37-0x0fa52c26-0x20ec3e4f]";
	std::string s, g;
	const bool b1 = ParseHashLine(dll, &s, &g);
	check("hash line: both hashes", b1 && s == "0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49" &&
	      g == "0x4510836f-0x6b3aac37-0x0fa52c26-0x20ec3e4f", s + " / " + g);
	s = g = "untouched";
	const bool b2 = ParseHashLine("CvGame::Read - Savefile was generated from gamecore version Release-5.4.8", &s, &g);
	check("other line: refused, outputs untouched", !b2 && s == "untouched" && g == "untouched", s + " / " + g);
	const bool b3 = ParseHashLine("CvGame::Read - WARNING - Save data hash mismatch!", &s, &g);
	check("warning without hash lines: counts, hashes named missing", b3 && s == "(not in the line)" &&
	      g == "(not in the line)", s + " / " + g);
	const bool b4 = ParseHashLine("", &s, &g);
	check("empty text: refused", !b4, "");
	const unsigned char h[16] = {0xf9, 0x10, 0x1c, 0x9b, 0xa1, 0xb0, 0x3d, 0xbd, 0xfe, 0xec, 0x27, 0x27, 0x49, 0x6a, 0x65, 0x06};
	check("hash bytes in the DLL's form", HashText(h) == "0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49", HashText(h));

	std::vector<Mod> hdr, act;
	hdr.push_back(M("d1b6328c-ff44-4b0d-aad7-c657f83610cd", 151, "(1) Community Patch", 1));
	hdr.push_back(M("8411a7a8-dad3-4622-a18e-fcc18324c799", 17, "(2) Vox Populi", 1));
	act = hdr;
	act.push_back(M("24923240-e4fb-4bf6-8f0e-6e5b6cf4d3c2", 1, "(3a) VP - EUI Compatibility Files", 0));
	act.push_back(M("170c8ed1-b516-4fe2-b571-befeac39d220", 41, "InGame Editor+", 0));
	std::string d = DescribeMods(hdr, true, act, true);
	check("same save-affecting mods: difference none", has(d, "difference in the mods a save lists: none") &&
	      has(d, "the save's header lists 2 mods: (1) Community Patch v151, (2) Vox Populi v17") &&
	      has(d, "active now: 4"), d);
	check("AffectsSavedGames = 0 mods named, not as a difference", has(d, "never list (AffectsSavedGames = 0): "
	      "(3a) VP - EUI Compatibility Files v1, InGame Editor+ v41") && !has(d, "only active: (3a)"), d);
	std::vector<Mod> act2 = act;
	act2[0].version = 150;
	d = DescribeMods(hdr, true, act2, true);
	check("another version named", has(d, "another version: (1) Community Patch (save v151, active v150)"), d);
	std::vector<Mod> hdr3 = hdr;
	hdr3.push_back(M("B2EB6D1E-1FCB-4A0D-PROJ-51691AA312DA", 1, "League Project Alerts", 1));
	d = DescribeMods(hdr3, true, act, true);
	check("only in the save (header id upper case)", has(d, "only in the save: League Project Alerts v1"), d);
	std::vector<Mod> act4 = act;
	act4.push_back(M("d9ece224-6cd8-4519-a27a-c417b59cdf35", 6, "Future Worlds", 1));
	act4.push_back(M("aaaaaaaa-0000-0000-0000-000000000000", 2, "", -1));
	d = DescribeMods(hdr, true, act4, true);
	check("only active (save-affecting and unknown), id when no name", has(d, "only active: Future Worlds v6") &&
	      has(d, "only active: aaaaaaaa-0000-0000-0000-000000000000 v2"), d);
	d = DescribeMods(std::vector<Mod>(), false, act, true);
	check("header unknown: said, no difference claimed", has(d, "the save's header was not read") &&
	      !has(d, "difference in the mods"), d);
	d = DescribeMods(hdr, true, std::vector<Mod>(), false);
	check("active unknown: said", has(d, "the active mods could not be read") && !has(d, "difference"), d);
	d = DescribeMods(std::vector<Mod>(), true, std::vector<Mod>(), true);
	check("both empty: none", has(d, "lists 0 mods") && has(d, "difference in the mods a save lists: none"), d);
	const std::string p = PlayerSummary();
	check("summary: mods named, not a patch failure", has(p, "different set of mods") &&
	      has(p, "not a failure of the 64-bit patch") && has(p, "fails in Vox Populi without the patch too") &&
	      p.find('\n') == std::string::npos, p);
	printf("%d failure(s)\n", g_fail);
	return g_fail ? 1 : 0;
}
'''


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Hermetic(unittest.TestCase):
    def test_pure_block(self):
        text = SRC.read_text(encoding='utf-8')
        m = BLOCK.search(text)
        self.assertIsNotNone(m, 'no BEGIN/END 0681 PURE block in srv_remote_savemods.cpp')
        x64build = load('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
        out = Path(tempfile.mkdtemp(prefix='c5-0681-'))
        try:
            (out / 'savemods_pure.gen.h').write_text(
                '// Generated by test_save_mod_mismatch.py from srv_remote_savemods.cpp. Do not edit.\n#pragma once\n'
                '#include <stdio.h>\n#include <string>\n#include <vector>\n' + m.group(0) + '\n', encoding='utf-8')
            (out / 'test_main.cpp').write_text(TEST_MAIN, encoding='utf-8')
            env = x64build.load_vcvars_env(lambda msg, echo=True: None, out / 'vcvars-env.json')
            exe = out / 'test_savemods.exe'
            cp = run([x64build.CLANG_CL, '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/O2', '/W3', '/WX',
                      '-D_CRT_SECURE_NO_WARNINGS', '-fuse-ld=lld', f'/I{out}', f'/I{HDR.parent}', f'/Fe{exe}',
                      f'/Fo{out}\\', out / 'test_main.cpp'], env=env, cwd=str(out))
            self.assertTrue(cp.returncode == 0 and exe.exists(), 'build failed:\n' + (cp.stdout + cp.stderr)[-3000:])
            cp = run([exe], cwd=str(out))
            print(cp.stdout)
            self.assertEqual(cp.returncode, 0, cp.stdout[-3000:])
            self.assertIn('0 failure(s)', cp.stdout)
            self.assertEqual(cp.stdout.count('PASS'), 14, 'a check did not run')
            if os.environ.get('C5_0681_ASAN') == '1':
                # COMMON rule 9: the native code once under MSVC /fsanitize=address, after a canary that must be caught
                cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
                self.assertTrue(cl, 'cl.exe not found in the vcvars environment')
                (out / 'canary.cpp').write_text(CANARY, encoding='ascii')
                for name, src in (('canary.exe', out / 'canary.cpp'), ('test_savemods_asan.exe', out / 'test_main.cpp')):
                    cp = run([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', '/EHsc', '/std:c++14', '/MD',
                              '/D_CRT_SECURE_NO_WARNINGS', f'/I{out}', f'/I{HDR.parent}', src, '/Fe' + str(out / name),
                              '/Fo' + str(out) + '\\', '/Fd' + str(out / (name + '.pdb'))], cwd=str(out), env=env)
                    self.assertEqual(cp.returncode, 0, (cp.stdout + cp.stderr)[-3000:])
                cp = run([out / 'canary.exe'], cwd=str(out), env=env)
                self.assertTrue(cp.returncode != 0 and 'heap-buffer-overflow' in cp.stdout + cp.stderr,
                                'ASan canary not caught: ' + (cp.stdout + cp.stderr)[-1000:])
                cp = run([out / 'test_savemods_asan.exe'], cwd=str(out), env=env)
                print('ASan:', cp.stdout.splitlines()[-1] if cp.stdout else '(no output)')
                self.assertEqual(cp.returncode, 0, (cp.stdout + cp.stderr)[-3000:])
                self.assertNotIn('AddressSanitizer', cp.stdout + cp.stderr)
                self.assertEqual(cp.stdout.count('PASS'), 14)
        finally:
            shutil.rmtree(out, ignore_errors=True)


def work():
    w = os.environ.get('C5_0681_WORK')
    return Path(w) if w else None


@unittest.skipUnless(work() and (work() / 'save' / 'cath58-copy.Civ5Save').exists(), 'C5_0681_WORK with the save copy')
class SaveBytes(unittest.TestCase):
    def test_body_hash_is_the_warning_hash(self):
        import hashlib
        sys.path.insert(0, str(REPO / 'server' / 'tools'))
        import civ5save
        data = (work() / 'save' / 'cath58-copy.Civ5Save').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), SAVE_SHA256)
        _h, _hb, _pre, pieces = civ5save.split(data)
        body = civ5save.inflate(pieces[:1])
        words = [int.from_bytes(body[4 + 4 * i:8 + 4 * i], 'little') for i in range(4)]
        self.assertEqual('-'.join(f'0x{w:08x}' for w in words), SAVE_HASH)


@unittest.skipUnless(os.environ.get('C5_SERVER_BIN') and work(), 'C5_SERVER_BIN and C5_0681_WORK')
class OfflineArms(unittest.TestCase):
    def arm(self, db):
        w = work()
        run_dir = Path(tempfile.mkdtemp(prefix='c5-0681-arm-', dir=str(w))) / 'run'
        cp = run([sys.executable, '-I', '-X', 'utf8', REPO / 'server' / 'run_server.py', '--load',
                  w / 'save' / 'cath58-copy.Civ5Save', '--turns', '59', '--run', run_dir, '--bin',
                  os.environ['C5_SERVER_BIN'], '--db', w / 'db' / db, '--timeout', '900'])
        log = (run_dir / 'server.log').read_text(encoding='utf-8', errors='replace') if (run_dir / 'server.log').exists() else ''
        shutil.rmtree(run_dir.parent, ignore_errors=True)
        return cp.returncode, log, cp.stdout + cp.stderr

    def test_with_the_mods_it_was_made_with(self):
        rc, log, out = self.arm('with-extra-mods.db')
        self.assertEqual(rc, 0, out[-2000:])
        self.assertIn('used up exactly', log)
        self.assertIn("save mods: load of 'cath58-copy.Civ5Save'", log)
        self.assertIn(f'game data hash {SAVE_HASH}', log)
        self.assertNotIn('save mods: LOUD', log)

    def test_without_them_fails_and_says_why(self):
        rc, log, out = self.arm('crash-session.db')
        self.assertEqual(rc, 2, out[-2000:])
        self.assertIn('EXCEPTION 0xE06D7363 in load', log)
        loud = [ln for ln in log.splitlines() if 'save mods: LOUD:' in ln]
        self.assertEqual(len(loud), 1, log[-3000:])
        self.assertIn(f'[{SAVE_HASH}]', loud[0])
        self.assertIn(f'[{GAME_HASH}]', loud[0])
        self.assertIn("the save's header lists 2 mods: (1) Community Patch v151, (2) Vox Populi v17", loud[0])
        self.assertIn('the load after the game data mismatch FAILED', log)
        self.assertIn('deserialization error', log)


if __name__ == '__main__':
    unittest.main(verbosity=2)
