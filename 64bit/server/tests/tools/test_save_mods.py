"""Issue 0303: a direct-mode new game's save written by run_server.py lists the release descriptor's mods in its header.

    python -I server/tests/tools/test_save_mods.py              (hermetic checks only)
    set C5_SERVER_BIN=<build_server out>/bin                    (also the live checks: a new game and its load)
    set C5_TEST_RUN_ROOT=<folder for the run folders>           (default: a temporary folder, removed afterwards)

Hermetic: set_save_mods on a synthetic .Civ5Save (header with an empty mod list, a pregame, a zlib body) writes the
descriptor's mods and leaves every byte outside the mod block unchanged (pregame and body pieces identical); a second
call changes nothing; a header that already lists mods is kept; a file that is not a save, or a cut header, raises
and stays as it was. When the descriptor's 32-bit huge reference save is on this machine: its mod list removed and set
again gives the original file byte for byte. run_server.main() in-process with the server replaced by a fake that
writes a save (and the launch gate by a stub): a new game's save gets the mods and the run exits 0; --save-mods none
keeps the empty list; --load keeps the header; a save the fake writes broken gives exit 5. Live (C5_SERVER_BIN): a
Tiny Continents new game with --save-at-start lists the descriptor's mods; the same save loads in the server and plays
two turns; --save-mods none lists none. Headers are read with server/tools/civ5save.py.
"""
import io
import os
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'server'))
sys.path.insert(0, str(ROOT / 'server' / 'tools'))
import run_server  # noqa: E402
import civ5save  # noqa: E402

SPEC = ROOT / 'server' / 'specs' / 'continents-tiny-fixed-seed.json'


def s(x: str) -> bytes:
    e = x.encode('utf-8')
    return struct.pack('<I', len(e)) + e


def synthetic_save(mods=(), body=b'body ' * 40000, pregame=b'\x07pregame bytes\x00' * 30) -> bytes:
    """A .Civ5Save as the server writes a new game: header (save version 8, the 7 type strings, one DLC entry, the mod
    list, 3 names, DLL GUID and version, the constant GUID, u32 0), the pregame, u32 2, the body in pieces."""
    h = b'CIV5' + struct.pack('<I', 8) + s('1.0.3.279 (403694)') + s('403694') + struct.pack('<I', 0) + b'\x00'
    for t in ('CIVILIZATION_EGYPT', 'HANDICAP_PRINCE', 'ERA_ANCIENT', 'ERA_ANCIENT', 'GAMESPEED_STANDARD',
              'WORLDSIZE_TINY', 'Assets\\Maps\\Continents.lua'):
        h += s(t)
    h += struct.pack('<I', 1) + bytes(range(16)) + struct.pack('<I', 1) + s('Mongolia')
    h += run_server.mod_block(list(mods))
    h += s('') + s('') + s('PLAYERCOLOR_EGYPT') + bytes(16) + s('1.0.0') + civ5save.CONST_GUID + struct.pack('<I', 0)
    tail = pregame + struct.pack('<I', 2) + b''.join(struct.pack('<I', len(p)) + p for p in civ5save.deflate(body))
    return h + tail


def mods_of(save: Path) -> list:
    return civ5save.parse_header(save.read_bytes())[0]['mods']


class Hermetic(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='t0303-'))
        self.want = run_server.descriptor_mods()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_descriptor_mods(self):
        self.assertTrue(self.want)
        self.assertEqual(self.want, [[m.guid, m.version, m.folder] for m in run_server.REL.mods])
        for g, v, n in self.want:
            self.assertEqual(g, g.lower())
            self.assertNotIn('{', g)
            self.assertIsInstance(v, int)
            self.assertTrue(n)

    def test_set_keeps_every_other_byte(self):
        save = self.dir / 'a.Civ5Save'
        old = synthetic_save()
        save.write_bytes(old)
        _, a, z = run_server.header_mods(old)
        self.assertEqual(mods_of(save), [])
        self.assertEqual(run_server.set_save_mods(save, self.want), 'set')
        new = save.read_bytes()
        self.assertEqual(mods_of(save), self.want)
        blk = run_server.mod_block(self.want)
        self.assertEqual(new, old[:a] + blk + old[z:])
        h0, hb0, pre0, pc0 = civ5save.split(old)
        h1, hb1, pre1, pc1 = civ5save.split(new)
        self.assertEqual(pre0, pre1)
        self.assertEqual(pc0, pc1)
        h0.pop('mods'), h1.pop('mods')
        self.assertEqual(h0, h1)
        self.assertFalse(list(self.dir.glob('*.tmp')))
        self.assertEqual(run_server.set_save_mods(save, self.want), 'same')
        self.assertEqual(save.read_bytes(), new)

    def test_header_with_mods_is_kept(self):
        save = self.dir / 'b.Civ5Save'
        other = [['00000000-0000-0000-0000-000000000001', 3, 'Other']]
        save.write_bytes(synthetic_save(other))
        before = save.read_bytes()
        self.assertEqual(run_server.set_save_mods(save, self.want), 'kept')
        self.assertEqual(save.read_bytes(), before)

    def test_not_a_save_raises_and_keeps_file(self):
        for name, data in (('nomagic', b'XIV5' + synthetic_save()[4:]), ('cut', synthetic_save()[:60]),
                           ('empty', b'')):
            save = self.dir / f'{name}.Civ5Save'
            save.write_bytes(data)
            with self.assertRaises(ValueError, msg=name):
                run_server.set_save_mods(save, self.want)
            self.assertEqual(save.read_bytes(), data, name)
        self.assertFalse(list(self.dir.glob('*.tmp')))

    def test_reference_save_round_trip(self):
        ref = run_server.REL.reference_saves.get('huge') if run_server.REL.reference_saves else None
        if not ref or not Path(ref.path).is_file():
            self.skipTest('the descriptor\'s huge reference save is not on this machine')
        orig = Path(ref.path).read_bytes()
        mods, a, z = run_server.header_mods(orig)
        self.assertEqual(mods, self.want, 'the 32-bit save lists the descriptor\'s mods in the same form')
        save = self.dir / 'ref.Civ5Save'
        save.write_bytes(orig[:a] + run_server.mod_block([]) + orig[z:])
        self.assertEqual(mods_of(save), [])
        self.assertEqual(run_server.set_save_mods(save, self.want), 'set')
        self.assertEqual(save.read_bytes(), orig)


def fake_db(path: Path):
    c = sqlite3.connect(path)
    c.execute('CREATE TABLE Worlds (ID INTEGER, Type TEXT, DefaultPlayers INTEGER, DefaultMinorCivs INTEGER)')
    c.execute("INSERT INTO Worlds VALUES (0, 'WORLDSIZE_STANDARD', 8, 16)")
    for t, typ in (('GameSpeeds', 'GAMESPEED_STANDARD'), ('Eras', 'ERA_ANCIENT'), ('HandicapInfos', 'HANDICAP_EMPEROR'),
                   ('Victories', 'VICTORY_TIME')):
        c.execute(f'CREATE TABLE {t} (ID INTEGER, Type TEXT)')
        c.execute(f'INSERT INTO {t} VALUES (0, ?)', (typ,))
    c.commit()
    c.close()


class MainInProcess(unittest.TestCase):
    """run_server.main() with the server and the launch gate replaced (no process is started)."""

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='t0303m-'))
        self.db = self.dir / 'fake.db'
        fake_db(self.db)
        self.spec = self.dir / 'spec.json'
        self.spec.write_text('{"seeds": {"map": 1, "sync": 2}}', encoding='utf-8')
        (self.dir / 'bin').mkdir()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def main(self, extra, save_bytes, names=('s0',)):
        # The server is started by subprocess.run (before ticket 0488) or jobcap.run (since 0488, main 4a515a43f).
        # Both are replaced by the fake. Every other way of starting a process is refused, and a run in which neither
        # fake started the server fails. Ticket 0627: from 2026-11-17 to 2026-11-20 this test patched only
        # subprocess.run, and jobcap.run started a real process (WinError 2: no server.exe in the fake bin).
        started = []

        def fake_run(cmd, *a, **kw):
            cfg = Path(cmd[2])
            run = cfg.parent
            for n in names:
                (run / f'{n}.Civ5Save').write_bytes(save_bytes)
            started.append(cmd)
            cp = subprocess.CompletedProcess(cmd, 0)
            cp.jobcap_events = []
            return cp

        def refuse_popen(cmd, *a, **kw):
            raise AssertionError(f'run_server.main() started a real process outside the fakes: {cmd}')
        gate = types.ModuleType('gate')
        gate.require = lambda *a, **k: None
        gate.require_memory = lambda *a, **k: None
        argv = ['run_server.py', '--turns', '0', '--run', str(self.dir / 'run'), '--bin', str(self.dir / 'bin'),
                '--db', str(self.db), '--loc-db', 'none'] + extra
        out = io.StringIO()
        with mock.patch.dict(sys.modules, {'gate': gate}), mock.patch.object(sys, 'argv', argv), \
                mock.patch.object(run_server.subprocess, 'run', fake_run), \
                mock.patch.object(run_server.jobcap, 'run', fake_run), \
                mock.patch.object(subprocess, 'Popen', refuse_popen), redirect_stdout(out):
            with self.assertRaises(SystemExit) as cm:
                run_server.main()
        self.assertEqual(len(started), 1, f'the server was started {len(started)} times through the fakes: {out.getvalue()}')
        return cm.exception.code, out.getvalue()

    def test_new_game_gets_mods(self):
        rc, out = self.main([str(self.spec), '--save-at-start', 's0', '--save-out', 's1'], synthetic_save(),
                            names=('s0', 's1'))
        self.assertEqual(rc, 0, out)
        want = run_server.descriptor_mods()
        for n in ('s0', 's1'):
            self.assertEqual(mods_of(self.dir / 'run' / f'{n}.Civ5Save'), want, n)
            self.assertIn(f'{n}.Civ5Save header mods: set', out)

    def test_save_mods_none(self):
        rc, out = self.main([str(self.spec), '--save-at-start', 's0', '--save-mods', 'none'], synthetic_save())
        self.assertEqual(rc, 0, out)
        self.assertEqual(mods_of(self.dir / 'run' / 's0.Civ5Save'), [])
        self.assertNotIn('header mods', out)

    def test_load_keeps_header(self):
        loaded = self.dir / 'in.Civ5Save'
        loaded.write_bytes(synthetic_save())
        rc, out = self.main(['--load', str(loaded), '--save-at-start', 's0'], synthetic_save())
        self.assertEqual(rc, 0, out)
        self.assertEqual(mods_of(self.dir / 'run' / 's0.Civ5Save'), [])

    def test_broken_save_exits_5(self):
        bad = b'CIV5' + b'\xff' * 40
        rc, out = self.main([str(self.spec), '--save-at-start', 's0'], bad)
        self.assertEqual(rc, run_server.SAVE_MODS_RC, out)
        self.assertEqual(rc, 5)
        self.assertIn('FAILED', out)
        self.assertEqual((self.dir / 'run' / 's0.Civ5Save').read_bytes(), bad)


@unittest.skipUnless(os.environ.get('C5_SERVER_BIN'), 'set C5_SERVER_BIN for the live checks')
class Live(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        keep = os.environ.get('C5_TEST_RUN_ROOT')
        cls.root = Path(keep) if keep else Path(tempfile.mkdtemp(prefix='t0303-live-'))
        cls.keep = bool(keep)
        cls.root.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        if not cls.keep:
            shutil.rmtree(cls.root, ignore_errors=True)

    def run_server(self, name, args):
        run = self.root / name
        cmd = [sys.executable, '-I', str(ROOT / 'server' / 'run_server.py')] + args + \
              ['--run', str(run), '--bin', os.environ['C5_SERVER_BIN']]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        print(f'--- {name}: rc {p.returncode}\n{p.stdout[-1500:]}')
        return run, p

    def test_new_game_then_load(self):
        want = run_server.descriptor_mods()
        run, p = self.run_server('new', [str(SPEC), '--turns', '0', '--save-at-start', 's0'])
        save = run / 's0.Civ5Save'
        self.assertTrue(save.is_file(), p.stderr[-2000:])
        self.assertEqual(p.returncode, 0)
        h = civ5save.parse_header(save.read_bytes())[0]
        print('header mods:', h['mods'])
        print('header DLC:', [n for _, _, n in h['dlc']])
        self.assertEqual(h['mods'], want)
        self.assertNotIn('VP Modpack', [n for _, _, n in h['dlc']])
        run2, p2 = self.run_server('load', ['--load', str(save), '--turns', '2', '--save-out', 's2'])
        self.assertEqual(p2.returncode, 0, p2.stdout[-2000:])
        self.assertEqual(mods_of(run2 / 's2.Civ5Save'), want, 'the loaded game keeps the header\'s mods')

    def test_none_lists_none(self):
        run, p = self.run_server('none', [str(SPEC), '--turns', '0', '--save-at-start', 's0', '--save-mods', 'none'])
        self.assertEqual(p.returncode, 0, p.stdout[-2000:])
        self.assertEqual(mods_of(run / 's0.Civ5Save'), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
