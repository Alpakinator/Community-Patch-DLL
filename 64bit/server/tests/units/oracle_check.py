"""DoD B part of issue 4454 (epic 0375; 2026-11-16): the server unit scene's .c5units for 3907's catalogue frames through
0041's module, against 0041's own scenes. Own code, numbers only (D83).

    python -I server/tests/units/oracle_check.py --tests C:/c5-64bit-work/0375/4454/build/bin/test_scene_units.exe
        --unitscene <render build>/tools/unitscene/unitscene.exe --out F:/c5-64bit-work/0375/4454/oracle
        [--scenes render/evidence/0041/4355/scenes] [--db ...] [--heightfield ...] [--game ...]

1. `test_scene_units.exe oracle`: per 0041 oracle scene a synthetic feed (players, unit records, creations in seed order)
   through the scene's own code (srvunits::UnitsCore); its UnitScene compared field by field with 0041's (exceptions by
   design: art_row "" (the module resolves it), the visible flag, no figures), OUT/scene/*.c5units and, with 0041's figure
   lines added (the poses forced as in note 12.2), OUT/merged/*.c5units.
2. `unitscene prepare` (0041's tool: UnitRenderer::prepare, the module's whole CPU path) on 0041's scenes and on the merged
   scenes, same install, mods, database, EXE and height field.
3. Per unit (scene, player, id): the module's code, blocks, draws and warnings, and the palette rows of its blocks
   (98 float4 per block, bytes), 0041's against the scene's. 0041's merged numbers rest on these rows (B, E) and on the
   draws made from them (C), so equal rows leave them unchanged.
Exit 0 report written (the numbers say how many units are equal), 1 a step failed, 2 usage.
"""
import argparse
import json
import os
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
# The instance whose install and mods the oracle reads: C5_ORACLE_INSTANCE, else c5h-c under <home>/c5-instances
# (ticket 4714: the same folder the literal named before).
INST = os.environ.get('C5_ORACLE_INSTANCE') or (Path.home() / 'c5-instances' / 'c5h-c').as_posix()
MODS = INST + "/Documents/My Games/Sid Meier's Civilization 5/MODS"
MODS_LIST = ';'.join(MODS + '/' + m for m in ('(1) Community Patch', '(2) Vox Populi', '(3a) VP - EUI Compatibility Files'))


def run(cmd, log):
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace')
    log.write('+ ' + ' '.join(str(c) for c in cmd)[:2000] + '\n' + cp.stdout + cp.stderr + '\nexit %d\n' % cp.returncode)
    return cp


def prepare(a, scenes, rows, log):
    rows.mkdir(parents=True, exist_ok=True)
    cp = run([a.unitscene, 'prepare', '--game', a.game, '--mods', MODS_LIST, '--db', a.db, '--exe',
              os.path.join(a.game, 'CivilizationV_DX11.exe'), '--heightfield', a.heightfield, '--rows', str(rows) + os.sep] +
             sorted(str(p) for p in Path(scenes).glob('*.c5units')), log)
    units = {}
    for line in cp.stdout.splitlines():
        if line.startswith('unit '):
            f = line.split(' ', 5)
            units[(f[1].replace('.c5units', ''), int(f[2]), int(f[3]))] = f[4] + ' ' + f[5].split(' | ', 1)[0]
    return cp.returncode, units


def palette_rows(rows, scene):
    """(player, id) -> the bytes of the unit's blocks (98 float4 each)."""
    f32 = (rows / (scene + '.c5units.f32')).read_bytes() if (rows / (scene + '.c5units.f32')).exists() else b''
    out = {}
    tsv = rows / (scene + '.c5units.blocks.tsv')
    if not tsv.exists():
        return out
    for line in tsv.read_text(encoding='utf-8').splitlines()[1:]:
        p, i, first, n, code = line.split('\t')
        out[(int(p), int(i))] = f32[int(first) * 98 * 16:(int(first) + int(n)) * 98 * 16]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tests', required=True)
    ap.add_argument('--unitscene', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--scenes', default=str(REPO / 'render' / 'evidence' / '0041' / '4355' / 'scenes'))
    ap.add_argument('--db', default='F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db')
    ap.add_argument('--heightfield', default='C:/c5-64bit-work/0364/bundles/C5H-0301-TINY_0009_20261115-225124_workstation/'
                                             'heightfield/load0003.c5hf')
    ap.add_argument('--game', default=INST + '/install')
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with open(out / 'oracle_check.log', 'w', encoding='utf-8') as log:
        cp = run([a.tests, 'oracle', '--scenes', a.scenes, '--db', a.db, '--heightfield', a.heightfield, '--out', out], log)
        if cp.returncode != 0:
            print('oracle_check: test_scene_units oracle exit %d (see %s)' % (cp.returncode, out / 'oracle_check.log'))
            return 1
        fields = [l for l in cp.stdout.splitlines() if l.startswith('oracle: files') or l.startswith('oracle: difference')]
        rc1, ref = prepare(a, a.scenes, out / 'prep_ref', log)
        rc2, mine = prepare(a, out / 'merged', out / 'prep_scene', log)
    if not ref or not mine:
        print('oracle_check: unitscene prepare gave no units (exits %d, %d; see %s)' % (rc1, rc2, out / 'oracle_check.log'))
        return 1
    status_eq = rows_eq = rows_cmp = 0
    differ = []
    for key, v in sorted(ref.items()):
        m = mine.get(key)
        if m is None:
            differ.append((key, 'not in the scene', v))
            continue
        if m == v:
            status_eq += 1
        else:
            differ.append((key, 'module status', v + ' -> ' + m))
    pal = Counter()
    for scene in sorted({k[0] for k in ref}):
        r1, r2 = palette_rows(out / 'prep_ref', scene), palette_rows(out / 'prep_scene', scene)
        for k, b in r1.items():
            if k in r2:
                rows_cmp += 1
                if r2[k] == b:
                    rows_eq += 1
                else:
                    pal[scene] += 1
                    differ.append(((scene,) + k, 'palette rows', '%d vs %d bytes' % (len(b), len(r2[k]))))
    summary = {'date': '2026-11-16', 'scene_fields': fields, 'units_0041': len(ref), 'units_scene': len(mine),
               'module_status_equal': status_eq, 'palette_units_compared': rows_cmp, 'palette_units_equal': rows_eq,
               'prepare_exits': [rc1, rc2], 'differences': [[list(k), w, d] for k, w, d in differ]}
    (out / 'summary.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')
    for l in fields:
        print(l)
    print('oracle_check: units %d (0041) / %d (scene); module status equal %d; palette rows equal %d of %d compared' % (
        len(ref), len(mine), status_eq, rows_eq, rows_cmp))
    for k, w, d in differ:
        print('oracle_check: differs %s: %s: %s' % (k, w, d))
    return 0


if __name__ == '__main__':
    sys.exit(main())
