"""Ticket 4748: a melee attack on a city strikes from where the attacking figures stand (CombatParams::city_in_place,
unit-combat-server.md 18.21), on the install's art (trigger_scene.exe, built by build_trigger_scene.py).

    python -I server/tests/effects/run_city_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases cm1,cm2] [--csv OUT.csv] [--after-only] [--before-only] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-5050] [--no-4743] [--no-5057] [--no-victory-word]]

Cases (plots and damage are stand-ins on trigger_scene's flat field; the attacker K = 0 and K = 2 facings away):
  cm1  a Lancer (a rush member, 5 figures, one death planned) attacks a city: stock's five Lancer attacks on city 13132
       (W52, 18.15.1 / 18.21) struck with every rider where it stood, 2.57-3.95 s.
  cm2  a Great War Infantry (14 figures, 0 -> 20 damage) attacks a city: stock's GWI city attacks did not move either
       (W52's melee-on-city class, 0.00 world units in the strike).
Each runs twice: `before` with --no-city-in-place (the rule before 4748) and `after` (the default). `after` passes when
the attacker has no Charge or Approach step (the core's steps, --dump-core), no attacker move but a reform run starts before its first
strike, and the core's end is at most 4.5 s; `before` must differ (it approaches).
--after-only runs only the default arm (on a trigger_scene built before 4748, whose default is the old rule: FAIL by
design, the regression case failing today). --before-only runs only `before` and applies the `after` expectation to it
(FAIL by design). --exe-base (a trigger_scene.exe built without 4748): every case's `before` arm equals the base's
default fight to the frame (its trigger_scene line and triggers.log byte for byte).
Prints one row per case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys

END_BAR = 4.5
ATT = 5901
CITY = '63:city:0:51:15:-:20:200'
CASES = {
    'cm1': '0:%d:UNIT_LANCER:50:15:0:25:100' % ATT,
    'cm2': '0:%d:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100' % ATT,
}
TURNS = ('0', '2')


NO_5050 = ['--no-reform-ends-at-arrival', '--no-attack-from-stance']
NO_5050_HELP = ('5050 off on EXE only (both knobs on by default since 5050 W74): with --exe-base, the identity against a '
                'base built before W74')
NO_4743 = ['--no-charge-any']
NO_4743_HELP = ('4743 off on EXE only (charge_any on by default again since 4743 W81): with --exe-base, the identity '
                'against a base built before W81')


def run_one(a, exe, out, att, k, off):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [exe, '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', CITY,
           '--seed', a.seed, '--attacker-turn', k, '--dump-core'] + (['--no-city-in-place'] if off else [])
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
    d = {'ok': p.returncode == 0 and any(l.startswith('trigger_scene: OK') for l in text.splitlines()), 'text': text}
    line = next((l for l in text.splitlines() if l.startswith('trigger_scene:')), '')
    d['line'] = line
    for kk, v in re.findall(r'(\w+)=(\S+)', next((l for l in text.splitlines() if l.startswith('walk:')), '')):
        d['walk.' + kk] = v
    log = out / 'triggers.log'
    d['log'] = log.read_bytes() if log.is_file() else b''
    d['approach'] = d['moved'] = -1
    d['end'] = float(d.get('walk.end', -1))
    if d['ok']:
        with open(out / 'steps.csv', newline='', encoding='utf-8') as f:
            steps = [s for s in csv.DictReader(f) if int(s['id']) == ATT]
        with open(out / 'moves.csv', newline='', encoding='utf-8') as f:
            moves = [m for m in csv.DictReader(f) if int(m['id']) == ATT]
        first = min((float(s['time']) for s in steps if s['state'].lower() == 'strike'), default=1e9)
        d['approach'] = sum(1 for s in steps if s['state'].lower() in ('charge', 'approach'))
        # moves before the first strike, other than phase 1's reform runs (stock reforms before a city attack too)
        reform = set((s['figure'], round(float(s['time']), 3)) for s in steps if s['state'].lower() == 'reform')
        d['moved'] = sum(1 for m in moves if float(m['t0']) < first - 1e-6 and
                         (m['figure'], round(float(m['t0']), 3)) not in reform)
        d['first_strike'] = first
    return d


def meets(r):
    return r['ok'] and r['approach'] == 0 and r['moved'] == 0 and 0 < r['end'] <= END_BAR + 1e-6


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
    ap.add_argument('--after-only', action='store_true')
    ap.add_argument('--before-only', action='store_true')
    a = ap.parse_args()
    for e in [a.exe] + ([a.exe_base] if a.exe_base else []):
        if not pathlib.Path(e).is_file():
            print('missing ' + e)
            return 2
    want = [c for c in a.cases.split(',') if c]
    for c in want:
        if c not in CASES:
            print(f'{c}: not a case ({",".join(CASES)})')
            return 2
    rows, passed, total = [], 0, 0
    for label in want:
        for k in TURNS:
            name = '%s-k%s' % (label, k)
            b = None if a.after_only else run_one(a, a.exe, pathlib.Path(a.work) / 'before' / name, CASES[label], k, True)
            if a.before_only:
                r = b
            else:
                r = run_one(a, a.exe, pathlib.Path(a.work) / 'after' / name, CASES[label], k, False)
            ok = meets(r) and (b is None or a.before_only or (b['ok'] and not meets(b)))
            total += 1
            passed += ok
            row = {'case': name, 'result': 'pass' if ok else 'FAIL', 'end_after': r['end'],
                   'approach_after': r['approach'], 'moved_after': r['moved'],
                   'first_strike_after': r.get('first_strike', -1),
                   'end_before': b['end'] if b else '', 'approach_before': b['approach'] if b else ''}
            if a.exe_base and b is not None:   # knob off = the base build's default fight, to the frame
                base = run_one(a, a.exe_base, pathlib.Path(a.work) / 'base' / name, CASES[label], k, False)
                same = base['ok'] and b['line'] == base['line'] and b['log'] == base['log']
                row['off_is_base'] = 'pass' if same else 'FAIL'
                total += 1
                passed += same
            rows.append(row)
            print('%-7s %-4s after: end=%.3f approach=%s moved=%s firstStrike=%.3f | before: end=%s approach=%s%s' % (
                name, row['result'], r['end'], r['approach'], r['moved'], r.get('first_strike', -1),
                row['end_before'], row['approach_before'],
                (' | off = base %s' % row['off_is_base']) if 'off_is_base' in row else ''), flush=True)
            for d in (b, r):
                if d is not None and not d['ok']:
                    print('      ' + d['text'].replace('\n', '\n      ')[-1500:])
    if a.csv:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=sorted(set(k for r in rows for k in r)), extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    print('RESULT %s %d/%d' % ('PASS' if passed == total else 'FAIL', passed, total))
    return 0 if passed == total else 1


if __name__ == '__main__':
    sys.exit(main())
