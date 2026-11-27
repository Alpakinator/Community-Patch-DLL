"""Issue 0197: run_server.py leaves the toolkit's "VP Modpack" package out of a new save's DLC list by default.

    python -I server/tests/tools/test_dlc_exclude.py            (static checks only)
    set C5_SERVER_BIN=<build_server out>/bin                    (also the live checks: two direct-mode new games)
    set C5_TEST_RUN_ROOT=<folder for the run folders>           (default: a temporary folder, removed afterwards)

Static: the default of --dlc-exclude names DLC_SP_Maps and the modpack GUID, and every VP_MODPACK* package of the
release descriptor's game folder carries a GUID the default names (so a new toolkit folder with another GUID fails
here, not in the front end). Live: a Tiny Continents new game with --save-at-start and the default writes a header
whose DLC list has neither GUID but keeps the rest of the folder's SteamApp packages; the same game with
--dlc-exclude <DLC_SP_Maps only> lists "VP Modpack" (the option still overrides the default, and the check is not
vacuous). The header is read with server/tools/civ5save.py.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'server'))
sys.path.insert(0, str(ROOT / 'server' / 'tools'))
import run_server  # noqa: E402
import civ5save  # noqa: E402

SPEC = ROOT / 'server' / 'specs' / 'continents-tiny-fixed-seed.json'
GUID_RE = re.compile(r'<GUID>\s*(\{[0-9A-Fa-f-]{36}\})\s*</GUID>')


def norm(g: str) -> str:
    return str(uuid.UUID(g))


def header_dlc(save: Path) -> list:
    """[(guid as text, name)] of a .Civ5Save header (the file holds Data1-3 little-endian, as the game writes them)."""
    h, _ = civ5save.parse_header(save.read_bytes())
    return [(str(uuid.UUID(bytes_le=bytes.fromhex(g))), name) for g, _one, name in h['dlc']]


def modpack_guids(game_dir: Path) -> list:
    out = []
    for pkg in sorted((game_dir / 'Assets' / 'DLC').glob('VP_MODPACK*/*.Civ5Pkg')):
        m = GUID_RE.search(pkg.read_text(encoding='utf-8', errors='replace'))
        if m:
            out.append(norm(m.group(1)))
    return out


class Static(unittest.TestCase):
    def test_default_names_both(self):
        d = [norm(g) for g in run_server.DEFAULT_DLC_EXCLUDE.split(',')]
        self.assertIn(norm(run_server.VP_BLOCKED_DLC), d)
        self.assertIn(norm('{b5932ae4-0f4f-498f-9333-e2d31b20e095}'), d)

    def test_descriptor_modpack_covered(self):
        gd = Path(run_server.GAME_DIR)
        if not (gd / 'Assets' / 'DLC').is_dir():
            self.skipTest(f'descriptor game folder not here: {gd}')
        found = modpack_guids(gd)
        self.assertTrue(found, f'no VP_MODPACK*/*.Civ5Pkg in {gd}')
        d = {norm(g) for g in run_server.DEFAULT_DLC_EXCLUDE.split(',')}
        for g in found:
            self.assertIn(g, d)


@unittest.skipUnless(os.environ.get('C5_SERVER_BIN'), 'set C5_SERVER_BIN for the live checks')
class Live(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        keep = os.environ.get('C5_TEST_RUN_ROOT')
        cls.root = Path(keep) if keep else Path(tempfile.mkdtemp(prefix='t0197-'))
        cls.keep = bool(keep)
        cls.root.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        if not cls.keep:
            shutil.rmtree(cls.root, ignore_errors=True)

    def new_game(self, name, extra):
        run = self.root / name
        cmd = [sys.executable, '-I', str(ROOT / 'server' / 'run_server.py'), str(SPEC), '--turns', '0',
               '--run', str(run), '--bin', os.environ['C5_SERVER_BIN'], '--save-at-start', 's0'] + extra
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
        save = run / 's0.Civ5Save'
        self.assertTrue(save.is_file(), f'{name}: no save (rc {p.returncode})\n{p.stdout[-2000:]}\n{p.stderr[-2000:]}')
        return header_dlc(save)

    def test_default_and_override(self):
        mp = norm(run_server.VP_MODPACK_DLC)
        sp = norm(run_server.VP_BLOCKED_DLC)
        dflt = self.new_game('default', [])
        print('default DLC list:', [n for _, n in dflt])
        guids = [g for g, _ in dflt]
        self.assertNotIn(mp, guids)
        self.assertNotIn(sp, guids)
        self.assertNotIn('VP Modpack', [n for _, n in dflt])
        self.assertGreaterEqual(len(dflt), 13, 'the SteamApp packages of the folder are still listed')
        over = self.new_game('override', ['--dlc-exclude', run_server.VP_BLOCKED_DLC])
        print('override DLC list:', [n for _, n in over])
        self.assertIn(mp, [g for g, _ in over])
        self.assertEqual(len(over), len(dflt) + 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
