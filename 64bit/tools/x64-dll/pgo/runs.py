"""Ticket 4801 (epic 0504 P1): the three headless game windows that train a PGO profile and check identity.

H: Huge Continental Drift, 0438's save TS0438-H-T300 played 300 -> 305; M: Standard Communitu_79a, 0438's save
TS0438-M-T150 played 150 -> 160 (both with 0438's statistics-on client database); S: the D24 save test's Standard game
(civ5-headless examples/specs/continents-standard-fixed-seed.json) new, 0 -> 50, on the release descriptor's test
database. All headless through server/run_server.py (gated), AI logging off (--dll-log-flags 0, D49).
On the VP 5.5 pins (ticket 0774) the windows are G (huge 350 -> 355), L (Large 200 -> 210) and S, with C5_RELEASE set to
the pin's release so run_server.py plays them on that descriptor's game folder and test database (DEFAULT_SETS).
research/08-performance/speedup-p1-pgo.md section 1.3.
"""
from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
C5H = Path.home() / 'Documents' / 'civ5-headless'   # as tools/d10/run_d10.py: no user path written here
DLL_NAME = 'CvGameCore_Expansion2.dll'

SETS = {
    'H': {'load': 'TS0438-H-T300.Civ5Save',
          'sha256': '308a96bb1404e68eb42672ecd51777c791dca151de482a1e90819c9b7df7e584', 'from': 300, 'turns': 305,
          'stats_db': True},
    'M': {'load': 'TS0438-M-T150.Civ5Save',
          'sha256': '98fc454a5b87d0d8752696895b9ff7fd5dd1858023f1fa426542e9e7a7e7a0c1', 'from': 150, 'turns': 160,
          'stats_db': True},
    'S': {'spec': 'examples/specs/continents-standard-fixed-seed.json', 'from': 0, 'turns': 50, 'stats_db': False},
    # the D24 save test's huge input (tools/d10/run_d10.py DEFAULT_HUGE), on the release descriptor's test database
    'E': {'load': 'E2000-HUGE_0350.Civ5Save',
          'sha256': 'c499d3c6cf83b8baacb57eb950302b68e0aff80510acd5cdbdbdfdd0535188c3', 'from': 350, 'turns': 355,
          'stats_db': False, 'release': 'Release-5.4.8'},
    # Ticket 0774 (the VP 5.5 pins): 0438's H and M are 5.4.8 saves and do not load on 5.5 (upstream 0848d52cc). Their
    # 5.5 counterparts of the same kind are ticket 0771's saves (tools/hi/evidence/0771/saves.tsv), on the 5.5
    # descriptor's test database: G = the huge reference VP55-HUGE_0350-B (Continents, Huge, 8 majors; = the 5.5
    # descriptor's reference_saves.huge) 350 -> 355 as H; L = HI-0771-LC21B55-T200 (Communitu_79a, Large, 21 majors)
    # 200 -> 210 as M.
    'G': {'load': 'VP55-HUGE_0350-B.Civ5Save',
          'sha256': '87fbdfecc920f89d8e7c6be8be6ee0b6ae3bb404c124e0c5f95d56fd6f8af1a8', 'from': 350, 'turns': 355,
          'stats_db': False, 'release': 'Release-5.5'},
    'L': {'load': 'HI-0771-LC21B55-T200.Civ5Save',
          'sha256': '3a660ea8d34b4e5808df2e5c9b0c2d6b9f10abb50bdf4051bf4e5400d6539825', 'from': 200, 'turns': 210,
          'stats_db': False, 'release': 'Release-5.5'},
}
for _k in ('H', 'M'):
    SETS[_k]['release'] = 'Release-5.4.8'
# the training and identity windows of a pin, by its VP release (deps/dll.lock `base`)
DEFAULT_SETS = {'Release-5.4.8': 'H,M,S', 'Release-5.5': 'G,L,S'}


def lock_release() -> str:
    """The VP release of deps/dll.lock's pin: the first word of its `base` line ("Release-5.5 (b0bce8c2f)")."""
    for line in (REPO / 'deps' / 'dll.lock').read_text(encoding='utf-8').splitlines():
        if line.startswith('base') and '=' in line:
            return line.split('=', 1)[1].split()[0]
    raise SystemExit('deps/dll.lock: no base line')


def check_release(release: str, names) -> list:
    """Sets bound to another VP release than the run's (their saves do not load there)."""
    return [f'{n}: a {SETS[n]["release"]} save, the run is on {release} (--release)' for n in names
            if SETS[n].get('release') and SETS[n]['release'] != release]
STATS_ON_DB_SHA256 = '9335186b14eb63a4644e7782263a34d41a3c4588ab4eae1ddc07b788700ed480'  # 0438's clientdb/stats-on


def sha256(path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def check_inputs(saves: Path, db: Path, names) -> list:
    """Problems with the inputs of the named sets (wrong or missing save or database), [] when all are right."""
    bad = []
    for n in names:
        s = SETS[n]
        if 'load' in s:
            p = saves / s['load']
            if not p.is_file():
                bad.append(f'{n}: {p} missing')
            elif sha256(p) != s['sha256']:
                bad.append(f'{n}: {p} is not the set\'s save (sha256 {sha256(p)[:16]}..., want {s["sha256"][:16]}...)')
        elif not (C5H / s['spec']).is_file():
            bad.append(f'{n}: spec {C5H / s["spec"]} missing')
    if any(SETS[n]['stats_db'] for n in names):
        if db is None:
            bad.append('--db (0438\'s statistics-on database) is needed for ' +
                       ','.join(n for n in names if SETS[n]['stats_db']))
        elif not db.is_file():
            bad.append(f'database {db} missing')
        elif sha256(db) != STATS_ON_DB_SHA256:
            bad.append(f'database {db} is not 0438\'s statistics-on database')
    return bad


def make_layout(template_bin: Path, dll: Path, dest: Path) -> Path:
    """dest/bin = every file of the pin layout's bin except the DLL (and its PDB), plus `dll` (and its PDB if there).
    Returns dest/bin. An existing dest is replaced."""
    b = dest / 'bin'
    if dest.exists():
        shutil.rmtree(dest)
    b.mkdir(parents=True)
    for f in template_bin.iterdir():
        if f.is_file() and f.name not in (DLL_NAME, 'CvGameCore_Expansion2.pdb', 'build.json'):
            shutil.copy2(f, b / f.name)
    shutil.copy2(dll, b / DLL_NAME)
    pdb = dll.with_suffix('.pdb')
    if pdb.is_file():
        shutil.copy2(pdb, b / pdb.name)
    return b


def run_cmd(name: str, bin_dir: Path, run: Path, saves: Path, db: Path, timeout: int = 7200) -> list:
    s = SETS[name]
    cmd = [sys.executable, '-I', str(REPO / 'server' / 'run_server.py')]
    cmd += ['--load', str(saves / s['load'])] if 'load' in s else [str(C5H / s['spec']), '--save-mods', 'none']
    cmd += ['--turns', str(s['turns']), '--run', str(run), '--bin', str(bin_dir), '--dll-log-flags', '0',
            '--timeout', str(timeout)]
    if s['stats_db']:
        cmd += ['--db', str(db)]
    return cmd


def run_one(name: str, bin_dir: Path, run: Path, saves: Path, db: Path, extra_env: dict, log, release=None) -> dict:
    """One window, logged to <run>.out. Returns {set, rc, wall_s, result (the server.log result line), run}."""
    if run.exists():
        shutil.rmtree(run)
    run.parent.mkdir(parents=True, exist_ok=True)
    cmd = run_cmd(name, bin_dir, run, saves, db)
    env = dict(os.environ, **extra_env)
    env.setdefault('C5_JOB_OWNER', 'impl-4801')
    if release:   # ticket 0774: server/run_server.py takes its game folder and test database from this descriptor
        env['C5_RELEASE'] = release
    log(f'{time.strftime("%H:%M:%S")} {name}: {"C5_RELEASE=" + release + " " if release else ""}{" ".join(cmd)}')
    t0 = time.time()
    with open(str(run) + '.out', 'w', encoding='utf-8') as f:
        rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode
    wall = time.time() - t0
    result = ''
    sl = run / 'server.log'
    if sl.is_file():
        for line in sl.read_text(encoding='utf-8', errors='replace').splitlines():
            if 'result:' in line:
                result = line.strip()
    log(f'{time.strftime("%H:%M:%S")} {name}: rc {rc} in {wall:.0f} s; {result}')
    return {'set': name, 'rc': rc, 'wall_s': round(wall, 1), 'result': result, 'run': str(run)}
