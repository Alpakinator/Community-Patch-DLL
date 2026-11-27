"""Ticket 5053: the end of a route S fight after its deaths, on the install's art (trigger_scene.exe, built by
build_trigger_scene.py; unit-combat-server.md 18.26).

    python -I server/tests/effects/run_end_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases e1,...] [--expect-fail] [--exe-base BASE_TRIGGER_SCENE.EXE] [--csv OUT.csv]
    python -I server/tests/effects/run_end_scenes.py --table --exe ... --game ... --db ... --work DIR
        [--seeds 1,2,3] [--kinds gatling,...] [--arms off,v,e,ve] [--csv OUT.csv]

The two knobs (CombatParams, trigger_scene flags; both ON by default since the MC's word of 2026-11-25; every arm here
names both, so the runner does not depend on the default):
  death_by_variant (--death-by-variant), R2: a dying figure lasts the death clip its machine plays (A 1200 or B 2200,
      read from the controller), not always A's length.
  end_after_dying (--end-after-dying), R1 + R3: the standing figures of every side but the attacker wait until the last
      dying figure is removed (stock's 0x10), and a land defender in 1280 with Walk and a 1450 clip leaves it with one
      1450 (Done) for end_stance_s (0.45 s) before it refaces.
trigger_scene's `end:` line gives the deaths, the last death, the last removal, the release (the first Done or Victory
of a figure that is not the attacker's), the end shuffles, the shortest and longest dying time, the deaths the
controller was seen playing as B or A, and the end; the `phases:` line the attacker's first strike.

Regression cases (flat field, plots and damage are stand-ins; the arm names both knobs on):
  e1  a Gatling gun shoots a Rifleman (--ranged --no-retaliate): deaths, the release no earlier than the last removal,
      at least one end shuffle, the end at least 0.45 s after the release, and the variant read for the deaths.
  e2  a Cannon shoots a Rifleman: the same.
  e3  a Swordsman kills a Warrior (melee, the Warrior at 50 -> 100): no standing defender figure is left, so no shuffle;
      the variant read for the deaths (the end moves only by B).
  e4  a Dreadnought shoots an Ironclad (a ship target: no Walk, no shuffle): no shuffle and the same end as both knobs
      off (R1 only; it breaks nothing with the knobs off, so --expect-fail does not count it).
--expect-fail is the regression arm: the cases run with both knobs off (--no-death-by-variant --no-end-after-dying) and
the arm passes when every case but e4 breaks its expectation. --exe-base also runs BASE with no knob flag and requires
the knobs-off arm's `trigger_scene:` line and WORK/<arm>/<case>/triggers.log to be byte-identical to BASE's (BASE: a
trigger_scene built from main before 5053, where neither knob exists).

--table runs the kinds (gatling, cannon, field_gun, melee, melee_kill) over the seeds in the arms off / v
(death_by_variant) / e (end_after_dying) / ve (both), prints per run and the means per kind and arm: the end, the first
strike, the time after it, the release after the last death, the shuffles and the B deaths. Prints `RESULT PASS|FAIL
n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Names from the install go only into WORK, never into git; the CSV holds
numbers and the unit types' names only. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import statistics
import subprocess
import sys

RANGED = ['--ranged', '--no-retaliate']
ON = ['--death-by-variant', '--end-after-dying']
OFF = ['--no-death-by-variant', '--no-end-after-dying']
# label: (expectation, attacker, defender, extra trigger_scene arguments)
CASES = {
    'e1': ('land', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:30:100', RANGED),
    'e2': ('land', '0:2005:UNIT_CANNON:49:15:0:0:100', '63:2006:UNIT_RIFLEMAN:51:15:0:30:100', RANGED),
    'e3': ('kill', '0:3201:UNIT_SWORDSMAN:50:15:0:20:100', '63:3202:UNIT_WARRIOR:51:15:50:100:100', []),
    'e4': ('ship', '0:2011:UNIT_DREADNOUGHT:49:15:0:0:100', '63:2016:UNIT_IRONCLAD:51:15:0:30:100', RANGED),
}
ARMS = {'off': OFF, 'v': ['--death-by-variant', '--no-end-after-dying'], 'e': ['--no-death-by-variant', '--end-after-dying'],
        've': ON}
KINDS = {
    'gatling': [('0:3301:UNIT_GATLINGGUN:49:15:0:0:100', '63:3302:UNIT_RIFLEMAN:51:15:0:30:100', RANGED)],
    'cannon': [('0:3303:UNIT_CANNON:49:15:0:0:100', '63:3304:UNIT_RIFLEMAN:51:15:0:30:100', RANGED)],
    'field_gun': [('0:3305:UNIT_FIELD_GUN:49:15:0:0:100', '63:3306:UNIT_RIFLEMAN:51:15:0:40:100', RANGED)],
    'melee': [('0:3307:UNIT_SWORDSMAN:50:15:0:20:100', '63:3308:UNIT_WARRIOR:51:15:0:45:100', [])],
    'melee_kill': [('0:3309:UNIT_SWORDSMAN:50:15:0:20:100', '63:3310:UNIT_WARRIOR:51:15:50:100:100', [])],
}
NUM = ['deaths', 'lastDeath', 'lastRemoved', 'release', 'shuffles', 'shuffleReface', 'dyingMin', 'dyingMax', 'deathB',
       'deathA', 'end', 'firstStrike']


def run_one(a, exe, out, att, dfn, extra, flags, seed=None):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', seed or a.seed] + list(extra) + list(flags)
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    lines = text.splitlines()
    d = {}
    ph = dict(re.findall(r'(\w+)=(\S+)', next((l for l in lines if l.startswith('phases:')), '')))
    d['firstStrike'] = ph.get('firstStrike', '-1')
    d.update(re.findall(r'(\w+)=(\S+)', next((l for l in lines if l.startswith('end:')), '')))
    tline = next((l for l in lines if l.startswith('trigger_scene:')), '')
    d['line'] = tline
    d.setdefault('end', '-1')
    d['ok'] = p.returncode == 0 and tline.startswith('trigger_scene: OK')
    d['text'] = text
    log = out / 'triggers.log'
    d['log'] = log.read_bytes() if log.is_file() else b''
    return d


def num(d, k, dflt=-1.0):
    try:
        return float(d.get(k, dflt))
    except (TypeError, ValueError):
        return dflt


def meets(kind, d, off=None):
    variant = num(d, 'deathA', 0) + num(d, 'deathB', 0) > 0
    longer_b = num(d, 'deathB', 0) == 0 or num(d, 'dyingMax') > num(d, 'dyingMin') + 0.1 or num(d, 'deathA', 0) == 0
    if kind == 'land':
        return (num(d, 'deaths', 0) > 0 and num(d, 'release') >= num(d, 'lastRemoved') - 1e-6 and
                num(d, 'shuffles', 0) >= 1 and num(d, 'end') >= num(d, 'release') + 0.45 - 1e-6 and variant and longer_b)
    if kind == 'kill':
        return num(d, 'deaths', 0) > 0 and num(d, 'shuffles', 0) == 0 and variant and longer_b
    # ship: no shuffle; with no death the end is the knobs-off end
    same_end = off is None or num(d, 'deaths', 0) > 0 or abs(num(d, 'end') - num(off, 'end')) < 1e-6
    return num(d, 'shuffles', 0) == 0 and same_end


def regression(a):
    want = [c for c in a.cases.split(',') if c]
    for c in want:
        if c not in CASES:
            print(f'{c}: not a case ({",".join(CASES)})')
            return 2
    rows, failed, breaking, counted = [], 0, 0, 0
    arm = 'off' if a.expect_fail else 'on'
    for label in want:
        kind, att, dfn, extra = CASES[label]
        d = run_one(a, a.exe, pathlib.Path(a.work) / arm / label, att, dfn, extra, OFF if a.expect_fail else ON)
        off = d if a.expect_fail else None
        if kind == 'ship' and off is None:
            off = run_one(a, a.exe, pathlib.Path(a.work) / 'off' / label, att, dfn, extra, OFF)
        rule = meets(kind, d, off)
        same = '-'
        if a.exe_base:
            if off is None:
                off = run_one(a, a.exe, pathlib.Path(a.work) / 'off' / label, att, dfn, extra, OFF)
            base = run_one(a, a.exe_base, pathlib.Path(a.work) / 'base' / label, att, dfn, extra, [])
            ident = base['ok'] and off['ok'] and off['line'] == base['line'] and off['log'] == base['log']
            same = 'same' if ident else 'DIFF(line %s log %s)' % (off['line'] == base['line'], off['log'] == base['log'])
        if a.expect_fail:
            ok = d['ok'] and (kind == 'ship' or not rule) and same in ('-', 'same')
            if kind != 'ship':
                counted += 1
                breaking += not rule
        else:
            ok = d['ok'] and rule and same in ('-', 'same')
        failed += not ok
        row = {'case': label, 'kind': kind, 'result': 'pass' if ok else 'FAIL', 'identity': same}
        row.update({k: d.get(k, '') for k in NUM})
        rows.append(row)
        print('%-3s (%s) %-4s end=%s firstStrike=%s deaths=%s lastDeath=%s lastRemoved=%s release=%s shuffles=%s '
              'dying=%s..%s B=%s A=%s %s' % (label, kind, row['result'], d.get('end'), d.get('firstStrike'),
                                             d.get('deaths'), d.get('lastDeath'), d.get('lastRemoved'), d.get('release'),
                                             d.get('shuffles'), d.get('dyingMin'), d.get('dyingMax'), d.get('deathB'),
                                             d.get('deathA'), same), flush=True)
        if not d['ok']:
            print('      ' + d['text'].replace('\n', '\n      ')[-1500:])
    if a.csv and rows:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    n = len(rows)
    passed = failed == 0
    if a.expect_fail:
        print('RESULT %s %d/%d (expect-fail: %d of %d case(s) break their expectation with the knobs off)' % (
            'PASS' if passed else 'FAIL', n - failed, n, breaking, counted))
    else:
        print('RESULT %s %d/%d' % ('PASS' if passed else 'FAIL', n - failed, n))
    return 0 if passed else 1


def table(a):
    seeds = [s for s in a.seeds.split(',') if s]
    arms = [x for x in a.arms.split(',') if x]
    kinds = [k for k in a.kinds.split(',') if k]
    for x in arms:
        if x not in ARMS:
            print(f'{x}: not an arm ({",".join(ARMS)})')
            return 2
    for k in kinds:
        if k not in KINDS:
            print(f'{k}: not a kind ({",".join(KINDS)})')
            return 2
    rows = []
    for kind in kinds:
        for ci, (att, dfn, extra) in enumerate(KINDS[kind]):
            name = att.split(':')[2] + '>' + dfn.split(':')[2]
            for seed in seeds:
                for arm in arms:
                    out = pathlib.Path(a.work) / 'table' / arm / ('%s-%d-s%s' % (kind, ci, seed))
                    d = run_one(a, a.exe, out, att, dfn, extra, ARMS[arm], seed)
                    r = {'kind': kind, 'case': name, 'seed': seed, 'arm': arm, 'ok': d['ok']}
                    r.update({x: num(d, x) for x in NUM})
                    r['afterFirst'] = r['end'] - r['firstStrike'] if r['firstStrike'] >= 0 else -1.0
                    r['releaseAfterDeath'] = r['release'] - r['lastDeath'] if r['release'] >= 0 and r['lastDeath'] >= 0 else -1.0
                    rows.append(r)
                    print('%-10s %-34s s%s %-3s ok=%d end %.3f strike %.3f after %.3f deaths %d release-lastDeath %.3f '
                          'shuffles %d B %d A %d dying %.3f..%.3f' % (
                              kind, name, seed, arm, d['ok'], r['end'], r['firstStrike'], r['afterFirst'], r['deaths'],
                              r['releaseAfterDeath'], r['shuffles'], r['deathB'], r['deathA'], r['dyingMin'],
                              r['dyingMax']), flush=True)
                    if not d['ok']:
                        print('      ' + d['text'].replace('\n', '\n      ')[-800:])
    allok = all(r['ok'] for r in rows)

    def mean(rr, key):
        v = [r[key] for r in rr]
        return statistics.mean(v) if v else float('nan')
    print('\nMEANS per kind and arm (seconds)')
    print('%-10s %-4s %3s %7s %7s %7s %8s %6s %5s' % ('kind', 'arm', 'n', 'strike', 'end', 'after', 'release', 'shuf',
                                                       'B'))
    for kind in kinds:
        for arm in arms:
            rr = [r for r in rows if r['kind'] == kind and r['arm'] == arm and r['ok']]
            if not rr:
                continue
            print('%-10s %-4s %3d %7.3f %7.3f %7.3f %8.3f %6.2f %5.2f' % (
                kind, arm, len(rr), mean(rr, 'firstStrike'), mean(rr, 'end'), mean(rr, 'afterFirst'),
                mean(rr, 'releaseAfterDeath'), mean(rr, 'shuffles'), mean(rr, 'deathB')))
    if a.csv and rows:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
    print('RESULT %s %d runs' % ('PASS' if allok else 'FAIL', len(rows)))
    return 0 if allok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--exe-base', default=None)
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    ap.add_argument('--cases', default=','.join(CASES))
    ap.add_argument('--expect-fail', action='store_true')
    ap.add_argument('--csv', default=None)
    ap.add_argument('--table', action='store_true')
    ap.add_argument('--seeds', default='1,2,3')
    ap.add_argument('--arms', default='off,v,e,ve')
    ap.add_argument('--kinds', default=','.join(KINDS))
    a = ap.parse_args()
    for e in [a.exe] + ([a.exe_base] if a.exe_base else []):
        if not pathlib.Path(e).is_file():
            print('missing ' + e)
            return 2
    return table(a) if a.table else regression(a)


if __name__ == '__main__':
    sys.exit(main())
