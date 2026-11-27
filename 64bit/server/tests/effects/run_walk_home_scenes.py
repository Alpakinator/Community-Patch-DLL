"""Ticket 4742: the survivors' walk home inside the fight (CombatParams::walk_home, unit-combat-server.md 18.18), on the
install's art (trigger_scene.exe, built by build_trigger_scene.py). The regression cases H1-H4 of lead-0042's answer #10244
on thread 0042, and the fight lengths per kind with the walk on and off (the reform on, as the merged head runs).

    python -I server/tests/effects/run_walk_home_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases h1,...] [--csv OUT.csv] [--before-only] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-5050] [--no-4743] [--no-5057] [--no-5061]
        [--no-victory-word]]
    python -I server/tests/effects/run_walk_home_scenes.py --lengths --exe ... --game ... --db ... --work DIR
        [--seeds 1,2,3] [--turns 0,2] [--csv OUT.csv]

Cases (plots and damage are stand-ins on trigger_scene's flat field; --attacker-turn K: the attacker stands K facings away
from its opponent, so that a unit whose ReformBeforeCombat names it re-lays its figures first). Each runs twice: `before`
with --no-walk-home (the rule before 4742) and `after` (the default):
  h1   ranged: an Archer turned away attacks a Warrior: it reforms, shoots and walks back from its re-laid slots.
  h2   siege: a Cannon turned away attacks a Musketman: its crew reforms, fires and walks back.
  h3a  melee: a Swordsman attacks a Warrior: the attackers run to contact and the survivors walk home.
  h3b  melee: a Great War Infantry attacks a Machine Gun (stock type 1): the same.
  h4   the 12 s limit: a Knight kills a Barbarian Warrior (a cavalry kill of a 10-figure unit, 18.2's class): whatever
       walk the limit leaves, no loop outlives the fight's OwnerEnd.
`after` passes (h1-h3) when some survivor walks (a Done step with 1400), every walker refaces (Reface steps = walkers),
the core's end is the later of the last arrival and the end without the walk (the last fall; within one
1/30 s step), the fight's OwnerEnd comes at or after it, run loops of a
movement clip start inside the walk before the OwnerEnd (walkLoops > 0), no loop starts after the OwnerEnd and every
loop is ended by a record (D210); `before` has no walk. h4 passes when no loop starts after the OwnerEnd, every loop is
ended, and no walker was refaced past the limit (refaces <= walkers).
--exe-base (a trigger_scene.exe built without 4742): every case's `before` arm is the base's fight to the frame (its
trigger_scene line and triggers.log equal: knob off = today, byte for byte).
--before-only runs only the `before` arm and applies the `after` expectation to it: FAIL by design (the rule before
4742 has no walk inside the fight: the regression cases fail on it; h4 passes).
--lengths runs the kinds over seeds and facings, walk off and on, and prints the mean end per kind and arm beside stock's
live means of 18.15.1 (W52, c5h-c, build 4390913). Prints one row per case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1
on FAIL, 2 on usage. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import statistics
import subprocess
import sys

STEP = 1.0 / 30.0
# label: (attacker, defender, extra trigger_scene arguments, expectation)
CASES = {
    'h1': ('0:4201:UNIT_ARCHER:49:15:0:0:100', '63:4202:UNIT_WARRIOR:51:15:0:25:100', ['--ranged', '--attacker-turn', '2'],
           'walk'),
    'h2': ('0:4203:UNIT_CANNON:49:15:0:0:100', '63:4204:UNIT_MUSKETMAN:51:15:0:35:100', ['--ranged', '--attacker-turn', '2'],
           'walk'),
    'h3a': ('0:4205:UNIT_SWORDSMAN:50:15:0:20:100', '63:4206:UNIT_WARRIOR:51:15:0:40:100', [], 'walk'),
    'h3b': ('0:4207:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:4208:UNIT_MACHINE_GUN:51:15:0:45:100', [], 'walk'),
    'h4': ('0:4209:UNIT_KNIGHT:10:10:0:30:100', '63:4210:UNIT_BARBARIAN_WARRIOR:11:10:0:100:100', [], 'limit'),
}
# --lengths: kind -> cases (attacker, defender, extra); each over --turns (the attacker K facings away) and --seeds
KINDS = {
    'melee type 1': [
        ('0:4301:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:4302:UNIT_MACHINE_GUN:51:15:0:45:100', []),
        ('0:4303:UNIT_INFANTRY:50:15:0:20:100', '63:4304:UNIT_MACHINE_GUN:51:15:0:45:100', []),
        ('0:4305:UNIT_WWI_TANK:50:15:0:15:100', '63:4306:UNIT_ANTI_TANK_GUN:51:15:0:40:100', []),
        ('0:4307:UNIT_COMMANDO:50:15:0:25:100', '63:4308:UNIT_GREAT_WAR_INFANTRY:51:15:0:40:100', []),
    ],
    'melee type 2': [
        ('0:4311:UNIT_SWORDSMAN:50:15:0:20:100', '63:4312:UNIT_WARRIOR:51:15:0:40:100', []),
        ('0:4313:UNIT_WARRIOR:50:15:0:20:100', '63:4314:UNIT_WARRIOR:51:15:0:40:100', []),
    ],
    'melee on a city': [
        ('0:4321:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:city:0:51:15:-:20:200', []),
        ('0:4323:UNIT_INFANTRY:50:15:0:15:100', '63:city:0:51:15:-:20:200', []),
    ],
    'ranged': [
        ('0:4331:UNIT_GATLINGGUN:49:15:0:0:100', '63:4332:UNIT_RIFLEMAN:51:15:0:30:100', ['--ranged']),
        ('0:4333:UNIT_ARCHER:49:15:0:0:100', '63:4334:UNIT_WARRIOR:51:15:0:25:100', ['--ranged']),
        ('0:4335:UNIT_CROSSBOWMAN:49:15:0:0:100', '63:4336:UNIT_SPEARMAN:51:15:0:30:100', ['--ranged']),
    ],
    'siege': [
        ('0:4341:UNIT_CANNON:49:15:0:0:100', '63:4342:UNIT_MUSKETMAN:51:15:0:35:100', ['--ranged']),
        ('0:4343:UNIT_FIELD_GUN:49:15:0:0:100', '63:4344:UNIT_RIFLEMAN:51:15:0:40:100', ['--ranged']),
        ('0:4345:UNIT_ARTILLERY:49:15:0:0:100', '63:4346:UNIT_MACHINE_GUN:51:15:0:35:100', ['--ranged']),
    ],
}
# stock's live means (unit-combat-server.md 18.15.1, W52 2026-11-22, c5h-c, build 4390913): whole simulation, end phase
STOCK = {
    'melee type 1': ('5.576 [0.521] n23', '1.126 [0.230]'),
    'melee type 2': ('6.984 n1 (Lancer)', '0.812'),
    'melee on a city': ('3.499 [0.571] n10', '0.767 [0.238]'),
    'ranged': ('4.530 [0.321] n32 reforming / 4.114 [0.448] n35 not', '1.119 [0.262] / 0.971 [0.272]'),
    'siege': ('(in ranged land)', '(in ranged land)'),
}


NO_5050 = ['--no-reform-ends-at-arrival', '--no-attack-from-stance']
NO_5050_HELP = ('5050 off on EXE only (both knobs on by default since 5050 W74): with --exe-base, the identity against a '
                'base built before W74')
NO_4743 = ['--no-charge-any']
NO_4743_HELP = ('4743 off on EXE only (charge_any on by default again since 4743 W81): with --exe-base, the identity '
                'against a base built before W81')


def run_one(a, exe, out, att, dfn, extra, off, seed=None):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [exe, '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', seed or a.seed] + extra + (['--no-walk-home'] if off else [])
    if a.no_5050 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5050 W74: both knobs on by default; off on EXE only
        cmd += NO_5050
    if a.no_4743 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 4743 W81: charge_any on by default; off on EXE only
        cmd += NO_4743
    if a.no_5057 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5057 W85: both knobs on by default; off on EXE only
        cmd += ['--no-rush-ride-through', '--no-defender-waits']
    if a.no_5061 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5061 W139: on by default; off on EXE only
        cmd.append('--no-defender-step')
    if a.no_victory_word and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5059 W100: on by default; off on EXE only
        cmd.append('--no-victory-word')
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    d = {}
    for pre in ('trigger_scene:', 'walk:', 'reform:'):
        line = next((l for l in text.splitlines() if l.startswith(pre)), '')
        d['line_' + pre[:-1]] = line
        for k, v in re.findall(r'(\w+)=(\S+)', line):
            d[pre[:-1] + '.' + k] = v
    d['ok'] = p.returncode == 0 and any(l.startswith('trigger_scene: OK') for l in text.splitlines())
    d['text'] = text
    log = out / 'triggers.log'
    d['log'] = log.read_bytes() if log.is_file() else b''
    return d


def num(d, k, dflt=-1.0):
    try:
        return float(d.get(k, dflt))
    except ValueError:
        return dflt


def walkers(d):
    return int(num(d, 'walk.walkAtt', 0)) + int(num(d, 'walk.walkDef', 0))


def d210(d):
    return int(num(d, 'walk.loopsAfterEnd', 1)) == 0 and int(num(d, 'walk.loopsOpen', 1)) == 0


def meets(b, r, want):
    if want == 'limit':
        return d210(r) and int(num(r, 'walk.refaces', 0)) <= walkers(r)
    w, end, arrive, oe = walkers(r), num(r, 'walk.end'), num(r, 'walk.arrive'), num(r, 'walk.ownerEnd')
    # the fight ends at the later of the walk's last arrival and the end without the walk (the last fall), within a step
    late = max(arrive, num(b, 'walk.end'))
    return (w > 0 and int(num(r, 'walk.refaces', 0)) == w and arrive - 1e-6 <= end <= late + STEP + 1e-6 and
            oe >= arrive - 1e-6 and int(num(r, 'walk.walkLoops', 0)) > 0 and d210(r))


def lengths(a):
    rows = []
    seeds = [s for s in a.seeds.split(',') if s]
    turns = [t for t in a.turns.split(',') if t]
    for kind, cases in KINDS.items():
        for ci, (att, dfn, extra) in enumerate(cases):
            for k in turns:
                for seed in seeds:
                    ends = {}
                    for arm in ('off', 'on'):
                        out = pathlib.Path(a.work) / 'len' / arm / ('%s-%d-k%s-s%s' % (kind.replace(' ', '_'), ci, k, seed))
                        ends[arm] = run_one(a, a.exe, out, att, dfn, extra + ['--attacker-turn', k], arm == 'off', seed)
                    on = ends['on']
                    rows.append({'kind': kind, 'case': att.split(':')[2] + '>' + dfn.split(':')[2], 'turn': k, 'seed': seed,
                                 'ok': ends['off']['ok'] and on['ok'],
                                 'end_off': num(ends['off'], 'walk.end'), 'end_on': num(on, 'walk.end'),
                                 'walkers': walkers(on), 'walk_start': num(on, 'walk.start'),
                                 'walk_arrive': num(on, 'walk.arrive'), 'walk_loops': int(num(on, 'walk.walkLoops', 0)),
                                 'loops_after_end': int(num(on, 'walk.loopsAfterEnd', -1)),
                                 'loops_open': int(num(on, 'walk.loopsOpen', -1)),
                                 'at_limit_on': num(on, 'walk.end') >= 11.99})
                    r = rows[-1]
                    print('%-16s %-40s K%s s%s ok=%d off %.2f on %.2f walkers %d walk %.2f-%.2f loops %d after %d open %d' % (
                        kind, r['case'], k, seed, r['ok'], r['end_off'], r['end_on'], r['walkers'], r['walk_start'],
                        r['walk_arrive'], r['walk_loops'], r['loops_after_end'], r['loops_open']), flush=True)
    print('\nMEANS per kind (seconds, the core\'s end; n = cases x facings x seeds; the reform on in both arms)')
    allok = all(r['ok'] for r in rows)
    for kind in KINDS:
        rr = [r for r in rows if r['kind'] == kind and r['ok']]
        if not rr:
            continue
        off = statistics.mean(r['end_off'] for r in rr)
        on = statistics.mean(r['end_on'] for r in rr)
        walk = [r['walk_arrive'] - r['walk_start'] for r in rr if r['walkers'] > 0]
        print('%-16s n=%-3d off %.3f on %.3f on-off %+.3f | walked %d, walk %.3f s | at limit on %d | loops after end %d, '
              'open %d | stock whole %s, end phase %s' % (
                  kind, len(rr), off, on, on - off, len(walk), statistics.mean(walk) if walk else 0.0,
                  sum(1 for r in rr if r['at_limit_on']), sum(r['loops_after_end'] for r in rr),
                  sum(r['loops_open'] for r in rr), STOCK[kind][0], STOCK[kind][1]))
    if a.csv:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ['kind'])
            w.writeheader()
            w.writerows(rows)
    d210ok = all(r['loops_after_end'] == 0 and r['loops_open'] == 0 for r in rows if r['ok'])
    print('RESULT %s %d runs (D210 %s)' % ('PASS' if allok and d210ok else 'FAIL', len(rows), 'ok' if d210ok else 'FAIL'))
    return 0 if allok and d210ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--exe-base', default=None)
    ap.add_argument('--no-5050', action='store_true', help=NO_5050_HELP)
    ap.add_argument('--no-4743', action='store_true', help=NO_4743_HELP)
    ap.add_argument('--no-victory-word', action='store_true',
                    help='5059 off on EXE only (CombatParams::victory_needs_word, on by default since 5059 W100): '
                         'with --exe-base, the identity against a base built before W100')
    ap.add_argument('--no-5057', action='store_true',
                    help='5057 off on EXE only (--no-rush-ride-through --no-defender-waits; both on by default since 5057 W85): with '
                         '--exe-base, the identity against a base built before W85')
    ap.add_argument('--no-5061', action='store_true',
                    help='5061 off on EXE only (--no-defender-step; on by default since 5061 W139): with --exe-base, the '
                         'identity against a base built before 5061')
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    ap.add_argument('--cases', default=','.join(CASES))
    ap.add_argument('--csv', default=None)
    ap.add_argument('--before-only', action='store_true')
    ap.add_argument('--lengths', action='store_true')
    ap.add_argument('--seeds', default='1,2,3')
    ap.add_argument('--turns', default='0,2')
    a = ap.parse_args()
    for e in [a.exe] + ([a.exe_base] if a.exe_base else []):
        if not pathlib.Path(e).is_file():
            print('missing ' + e)
            return 2
    if a.lengths:
        return lengths(a)
    want = [c for c in a.cases.split(',') if c]
    for c in want:
        if c not in CASES:
            print(f'{c}: not a case ({",".join(CASES)})')
            return 2
    rows, passed, total = [], 0, 0
    for label in want:
        att, dfn, extra, expect = CASES[label]
        b = run_one(a, a.exe, pathlib.Path(a.work) / 'before' / label, att, dfn, extra, True)
        r = b if a.before_only else run_one(a, a.exe, pathlib.Path(a.work) / 'after' / label, att, dfn, extra, False)
        before_ok = a.before_only or walkers(b) == 0
        ok = b['ok'] and r['ok'] and before_ok and meets(b, r, expect)
        total += 1
        passed += ok
        row = {'case': label, 'expect': expect, 'result': 'pass' if ok else 'FAIL',
               'end_before': b.get('walk.end', ''), 'end_after': r.get('walk.end', ''),
               'ownerEnd_after': r.get('walk.ownerEnd', ''), 'walkAtt': r.get('walk.walkAtt', ''),
               'walkDef': r.get('walk.walkDef', ''), 'start': r.get('walk.start', ''), 'arrive': r.get('walk.arrive', ''),
               'refaces': r.get('walk.refaces', ''), 'walkLoops': r.get('walk.walkLoops', ''),
               'walkLoops_before': b.get('walk.walkLoops', ''), 'loopsAfterEnd': r.get('walk.loopsAfterEnd', ''),
               'loopsOpen': r.get('walk.loopsOpen', '')}
        if a.exe_base:   # knob off = the base build's fight, to the frame
            base = run_one(a, a.exe_base, pathlib.Path(a.work) / 'base' / label, att, dfn, extra, False)
            same = base['ok'] and b['line_trigger_scene'] == base['line_trigger_scene'] and b['log'] == base['log']
            row['base_end'] = base.get('trigger_scene.elapsed', '')
            row['off_is_base'] = 'pass' if same else 'FAIL'
            total += 1
            passed += same
        rows.append(row)
        print('%-4s %-5s %-4s before: end=%s walkLoops=%s | after: end=%s ownerEnd=%s walk att/def %s/%s %s-%s refaces %s '
              'walkLoops %s after-end %s open %s%s' % (
                  label, expect, row['result'], row['end_before'], row['walkLoops_before'], row['end_after'],
                  row['ownerEnd_after'], row['walkAtt'], row['walkDef'], row['start'], row['arrive'], row['refaces'],
                  row['walkLoops'], row['loopsAfterEnd'], row['loopsOpen'],
                  (' | off = base %s' % row['off_is_base']) if a.exe_base else ''), flush=True)
        for d in (b, r):
            if not d['ok']:
                print('      ' + d['text'].replace('\n', '\n      ')[-1500:])
    if a.csv:
        cols = list(rows[0].keys()) if rows else ['case']
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    print('RESULT %s %d/%d' % ('PASS' if passed == total else 'FAIL', passed, total))
    return 0 if passed == total else 1


if __name__ == '__main__':
    sys.exit(main())
