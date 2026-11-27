"""Tickets 4745 + 4747: stock's phase 1, the reform before a fight (CombatParams::reform_first, unit-combat-server.md 18.17),
on the install's art (trigger_scene.exe, built by build_trigger_scene.py). The regression cases R1-R4 of lead-0042's
thread 0042 #10200 (4), and the fight lengths per kind with the reform on and off.

    python -I server/tests/effects/run_reform_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases r1,...] [--csv OUT.csv] [--before-only] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-5050] [--no-4743] [--no-5057] [--no-victory-word]]
    python -I server/tests/effects/run_reform_scenes.py --lengths --exe ... --game ... --db ... --work DIR
        [--seeds 1,2,3] [--turns 0,1,2,3] [--csv OUT.csv]

Cases (plots and damage are stand-ins on trigger_scene's flat field; a unit stands K facings away from its opponent, as a
unit that arrived from elsewhere, --attacker-turn / --defender-turn). Each runs twice: `before` with --no-reform-first
(the rule before 4745) and `after` (the default):
  r1   Great War Infantry attacks a Machine Gun, the attacker turned away: the attacker reforms (ReformBeforeCombat 1),
       and the reform's last arrival comes before the first strike and the first landing of either side.
  r2a  a WWI tank attacks a Lancer turned away: the Lancer (ReformBeforeCombat 2, the defender's bit) reforms.
  r2b  a Lancer turned away attacks a Destroyer: nobody reforms (2 names the defender only; ships have no column).
  r3   Warrior on Warrior (no ReformBeforeCombat), turned away: `after` is the same fight as `before` to the frame (the
       trigger_scene line and triggers.log equal).
  r4   with --exe-base (a trigger_scene.exe built without 4745): every case's `before` arm ends at the base's end
       (knob off = today's lengths).
--before-only runs only the `before` arm and applies r1 / r2a's expectation to it (it fails on a build without 4745).
--lengths runs the kinds (melee type 1, melee on a city, ranged, siege) over seeds and facings, on and off, and prints
the mean end per kind and arm. Prints one row per case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage.
Python standard library.
"""
import argparse
import csv
import pathlib
import re
import statistics
import subprocess
import sys

# label: (attacker, defender, extra trigger_scene arguments, expectation)
CASES = {
    'r1': ('0:3001:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:3002:UNIT_MACHINE_GUN:51:15:0:45:100',
           ['--attacker-turn', '2'], 'att'),
    'r2a': ('0:3003:UNIT_WWI_TANK:50:15:0:15:100', '63:3004:UNIT_LANCER:51:15:0:45:100', ['--defender-turn', '2'], 'def'),
    'r2b': ('0:3005:UNIT_LANCER:50:15:0:25:100', '63:3006:UNIT_DESTROYER:51:15:0:30:100', ['--attacker-turn', '2'], 'none'),
    'r3': ('0:3007:UNIT_WARRIOR:50:15:0:20:100', '63:3008:UNIT_WARRIOR:51:15:0:40:100', ['--attacker-turn', '2'], 'same'),
}
# --lengths: kind -> cases (attacker, defender, extra); each over --turns (the attacker K facings away) and --seeds
KINDS = {
    'melee type 1': [
        ('0:3101:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:3102:UNIT_MACHINE_GUN:51:15:0:45:100', []),
        ('0:3103:UNIT_INFANTRY:50:15:0:20:100', '63:3104:UNIT_MACHINE_GUN:51:15:0:45:100', []),
        ('0:3105:UNIT_WWI_TANK:50:15:0:15:100', '63:3106:UNIT_ANTI_TANK_GUN:51:15:0:40:100', []),
        ('0:3107:UNIT_WWI_TANK:50:15:0:15:100', '63:3108:UNIT_RIFLEMAN:51:15:0:40:100', []),
        ('0:3109:UNIT_COMMANDO:50:15:0:25:100', '63:3110:UNIT_GREAT_WAR_INFANTRY:51:15:0:40:100', []),
        ('0:3111:UNIT_PARATROOPER:50:15:0:20:100', '63:3112:UNIT_ANTI_TANK_GUN:51:15:0:40:100', []),
    ],
    'melee on a city': [
        ('0:3121:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:city:0:51:15:-:20:200', []),
        ('0:3123:UNIT_INFANTRY:50:15:0:15:100', '63:city:0:51:15:-:20:200', []),
    ],
    'ranged': [
        ('0:3131:UNIT_GATLINGGUN:49:15:0:0:100', '63:3132:UNIT_RIFLEMAN:51:15:0:30:100', ['--ranged']),
        ('0:3133:UNIT_ARCHER:49:15:0:0:100', '63:3134:UNIT_WARRIOR:51:15:0:25:100', ['--ranged']),
        ('0:3135:UNIT_CROSSBOWMAN:49:15:0:0:100', '63:3136:UNIT_SPEARMAN:51:15:0:30:100', ['--ranged']),
    ],
    'siege': [
        ('0:3141:UNIT_CANNON:49:15:0:0:100', '63:3142:UNIT_MUSKETMAN:51:15:0:35:100', ['--ranged']),
        ('0:3143:UNIT_FIELD_GUN:49:15:0:0:100', '63:3144:UNIT_RIFLEMAN:51:15:0:40:100', ['--ranged']),
        ('0:3145:UNIT_ARTILLERY:49:15:0:0:100', '63:3146:UNIT_MACHINE_GUN:51:15:0:35:100', ['--ranged']),
    ],
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
           '--seed', seed or a.seed] + extra + (['--no-reform-first'] if off else [])
    if a.no_5050 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5050 W74: both knobs on by default; off on EXE only
        cmd += NO_5050
    if a.no_4743 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 4743 W81: charge_any on by default; off on EXE only
        cmd += NO_4743
    if a.no_5057 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5057 W85: both knobs on by default; off on EXE only
        cmd += ['--no-rush-ride-through', '--no-defender-waits']
    if a.no_victory_word and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5059 W100: on by default; off on EXE only
        cmd.append('--no-victory-word')
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    d = {}
    for pre in ('trigger_scene:', 'reform:'):
        line = next((l for l in text.splitlines() if l.startswith(pre)), '')
        d['line_' + pre[:-1]] = line
        d.update(re.findall(r'(\w+)=(\S+)', line))
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


def meets(b, r, want):
    ra, rd = int(num(r, 'reformAtt', 0)), int(num(r, 'reformDef', 0))
    arrive, strike, land = num(r, 'arrive'), num(r, 'firstStrike'), num(r, 'firstLanding')
    if want == 'att':
        return ra > 0 and arrive > 0 and strike >= arrive and land >= arrive
    if want == 'def':
        return rd > 0 and arrive > 0 and strike >= arrive and land >= arrive
    if want == 'none':
        return ra == 0 and rd == 0
    # 'same': the same fight to the frame
    return ra == 0 and rd == 0 and b['line_trigger_scene'] == r['line_trigger_scene'] and b['log'] == r['log']


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
                        d = run_one(a, a.exe, out, att, dfn, extra + ['--attacker-turn', k], arm == 'off', seed)
                        ends[arm] = d
                    rows.append({'kind': kind, 'case': att.split(':')[2] + '>' + dfn.split(':')[2], 'turn': k, 'seed': seed,
                                 'ok': ends['off']['ok'] and ends['on']['ok'],
                                 'end_off': num(ends['off'], 'end'), 'end_on': num(ends['on'], 'end'),
                                 'reform_att': ends['on'].get('reformAtt', ''), 'reform_def': ends['on'].get('reformDef', ''),
                                 'arrive': num(ends['on'], 'arrive')})
                    r = rows[-1]
                    print('%-16s %-40s K%s s%s ok=%d off %.2f on %.2f reform %s/%s arrive %.2f' % (
                        kind, r['case'], k, seed, r['ok'], r['end_off'], r['end_on'], r['reform_att'], r['reform_def'],
                        r['arrive']), flush=True)
    print('\nMEANS per kind (seconds, the core\'s end; n = cases x facings x seeds)')
    allok = all(r['ok'] for r in rows)
    for kind in KINDS:
        for k in ['all'] + turns:
            rr = [r for r in rows if r['kind'] == kind and r['ok'] and (k == 'all' or r['turn'] == k)]
            if not rr:
                continue
            off = statistics.mean(r['end_off'] for r in rr)
            on = statistics.mean(r['end_on'] for r in rr)
            print('%-16s K=%-3s n=%-3d off %.3f on %.3f on-off %+.3f reformed %d' % (
                kind, k, len(rr), off, on, on - off, sum(1 for r in rr if (r['reform_att'] not in ('', '0')) or
                                                        (r['reform_def'] not in ('', '0')))))
    if a.csv:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ['kind'])
            w.writeheader()
            w.writerows(rows)
    print('RESULT %s %d runs' % ('PASS' if allok else 'FAIL', len(rows)))
    return 0 if allok else 1


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
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    ap.add_argument('--cases', default=','.join(CASES))
    ap.add_argument('--csv', default=None)
    ap.add_argument('--before-only', action='store_true')
    ap.add_argument('--lengths', action='store_true')
    ap.add_argument('--seeds', default='1,2,3')
    ap.add_argument('--turns', default='0,1,2,3')
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
        ok = b['ok'] and r['ok'] and meets(b, r, expect)
        total += 1
        passed += ok
        row = {'case': label, 'expect': expect, 'result': 'pass' if ok else 'FAIL',
               'end_before': b.get('end', ''), 'end_after': r.get('end', ''), 'reformAtt': r.get('reformAtt', ''),
               'reformDef': r.get('reformDef', ''), 'arrive': r.get('arrive', ''), 'firstStrike': r.get('firstStrike', ''),
               'firstLanding': r.get('firstLanding', '')}
        if a.exe_base:   # r4: knob off = the base build's fight
            base = run_one(a, a.exe_base, pathlib.Path(a.work) / 'base' / label, att, dfn, extra, False)
            base_end = re.search(r'\bend=(\S+)', base['line_trigger_scene'])
            r4 = base['ok'] and b['line_trigger_scene'] == base['line_trigger_scene'] and b['log'] == base['log']
            row['end_base'] = base_end.group(1) if base_end else ''
            row['r4'] = 'pass' if r4 else 'FAIL'
            total += 1
            passed += r4
        rows.append(row)
        print('%-4s %-5s %-4s before: end=%s | after: end=%s reform att/def %s/%s arrive %s firstStrike %s firstLanding %s%s' % (
            label, expect, row['result'], row['end_before'], row['end_after'], row['reformAtt'], row['reformDef'],
            row['arrive'], row['firstStrike'], row['firstLanding'],
            (' | r4 base end=%s %s' % (row['end_base'], row['r4'])) if a.exe_base else ''), flush=True)
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
