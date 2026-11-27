"""Ticket 5050: stock's phases 1 + 2 for route S fights, on the install's art (trigger_scene.exe, built by
build_trigger_scene.py; unit-combat-server.md 18.22, 18.23, 10.14 section 4).

    python -I server/tests/effects/run_phase_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases p1r,...] [--arm default|own] [--expect-fail] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-4743] [--no-5057] [--no-5061]
        [--no-victory-word]]
        [--csv OUT.csv]
    python -I server/tests/effects/run_phase_scenes.py --table --exe ... --game ... --db ... --work DIR
        [--seeds 1,2,3] [--turns 0,2] [--kinds melee,...] [--arms before,a,b,ab] [--csv OUT.csv]

The two knobs (CombatParams, trigger_scene flags):
  (a) reform_ends_at_arrival (--reform-ends-at-arrival): phase 1 (the reform) ends at the last reforming figure's
      arrival, as stock's (0xD -> 3 on arrival, 18.22), not after that figure's run stop (1440).
  (b) attack_from_stance (--attack-from-stance): an attacker's first Ready, when it has not moved, takes stock's phase-2
      stance (1450 for 0.45 s), so its 1600 meets the machine in its run loop (the stop shuffle to combat ready, no
      enter-combat-ready clip with FOOT_ALWAYS / FORTIFY_VOX).
trigger_scene's `phases:` line gives p1 (phase 1's end: the first step that is not the reform's), p2 (the attacker's
stance: its first 1600 minus p1), its first strike, its stance steps (astances), its 1600 requests made when its last
request was not a movement (attReadyFromIdle: the entry clip that sounds) and the end; the `reform:` line the last
arrival; the `ready:` line the one-shot SoundStarts by clip class.

Both knobs are ON by default since 5050 W74 (MC #10667 / #10726; trigger_scene's --no-... flags turn each off).

Regression cases (flat field, plots and damage are stand-ins). The default arm runs them with no knob flag (both on by
default); --arm own with only the case's own knob on and the other off:
  p1r  (a) Great War Infantry attacks a Machine Gun, turned 2 facings away: the attacker reforms; p1 is within 0.1 s of
       the last arrival.
  p1t  (a) a WWI tank attacks a Lancer turned 2 facings away: the Lancer (the defender's bit) reforms; the same.
  p2a  (b) an archer shoots a warrior (--ranged --no-retaliate): every attacker figure takes the stance
       (astances > 0) and none requests 1600 from idle (attReadyFromIdle 0).
  p2g  (b) a Gatling gun shoots a rifleman: its crew's member has no Walk, so it holds (aholds > 0, no request) and
       its first 1600 comes at least attack_stance_s after p1 (then the whole entry clip before the strike).
--expect-fail is the regression arm: the cases run with both knobs off (--no-reform-ends-at-arrival
--no-attack-from-stance, the rule before 5050) and the arm passes when EVERY case breaks its expectation. --exe-base also
runs BASE with no knob flag and requires the knobs-off arm's `trigger_scene:` line and WORK/<case>/triggers.log to be
byte-identical to BASE's: BASE is a trigger_scene built from a main before W74's default change (knobs off by default
there; W65 used one from before 5050), so the knobs off are the code before them. Since 4743 W81 charge_any is on by
default again: against a BASE built before W81 add --no-4743 (EXE only, --no-charge-any).
Since 5057 W85 the rush ride-through and the type 2 defender wait are on by default: against a base built before W85
add --no-5057 (EXE only, --no-rush-ride-through --no-defender-waits).

--table runs the fight kinds (melee type 1, melee on a city, ranged, siege, air, naval melee, naval_ranged) over the
attacker's facings (--turns, K facings away: K >= 1 makes a reforming unit reform) and seeds, in the arms before / a /
b / ab, and prints per kind and arm the means of p1 (over the runs that reformed, and over all), p2, the first strike,
the end and the one-shots per class. Prints one row per run or case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on
FAIL, 2 on usage. Names from the install go only into WORK, never into git; the CSV holds numbers and the unit types'
names only. Python standard library.
"""
import argparse
import csv
import importlib.util
import json
import pathlib
import re
import statistics
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
CASES_0557 = REPO / 'tools/harness/evidence/0557/batch-s-stock1.json'

RANGED = ['--ranged', '--no-retaliate']
# label: (knob, attacker, defender, extra trigger_scene arguments)
CASES = {
    'p1r': ('a', '0:3001:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:3002:UNIT_MACHINE_GUN:51:15:0:45:100',
            ['--attacker-turn', '2']),
    'p1t': ('a', '0:3003:UNIT_WWI_TANK:50:15:0:15:100', '63:3004:UNIT_LANCER:51:15:0:45:100', ['--defender-turn', '2']),
    'p2a': ('b', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:25:100', RANGED),
    'p2g': ('bh', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:30:100', RANGED),
}
# Both knobs are ON by default since 5050 W74 (MC #10667 / #10726): every arm names both knobs, so an arm means the same
# on a build before W74 (both off by default) and after it.
ARMS = {'before': ['--no-reform-ends-at-arrival', '--no-attack-from-stance'],
        'a': ['--reform-ends-at-arrival', '--no-attack-from-stance'],
        'b': ['--no-reform-ends-at-arrival', '--attack-from-stance'],
        'bh': ['--no-reform-ends-at-arrival', '--attack-from-stance'],
        'ab': ['--reform-ends-at-arrival', '--attack-from-stance']}
OFF = ['--no-reform-ends-at-arrival', '--no-attack-from-stance']
# --table: kind -> (cases (attacker, defender, extra), whether the attacker's facing is turned)
KINDS = {
    'melee': ([
        ('0:3101:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:3102:UNIT_MACHINE_GUN:51:15:0:45:100', []),
        ('0:3103:UNIT_INFANTRY:50:15:0:20:100', '63:3104:UNIT_MACHINE_GUN:51:15:0:45:100', []),
        ('0:3105:UNIT_WWI_TANK:50:15:0:15:100', '63:3106:UNIT_ANTI_TANK_GUN:51:15:0:40:100', []),
        ('0:3107:UNIT_WWI_TANK:50:15:0:15:100', '63:3108:UNIT_RIFLEMAN:51:15:0:40:100', []),
        ('0:3109:UNIT_COMMANDO:50:15:0:25:100', '63:3110:UNIT_GREAT_WAR_INFANTRY:51:15:0:40:100', []),
        ('0:3111:UNIT_PARATROOPER:50:15:0:20:100', '63:3112:UNIT_ANTI_TANK_GUN:51:15:0:40:100', []),
    ], True),
    'city': ([
        ('0:3121:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:city:0:51:15:-:20:200', []),
        ('0:3123:UNIT_INFANTRY:50:15:0:15:100', '63:city:0:51:15:-:20:200', []),
    ], True),
    'ranged': ([
        ('0:3131:UNIT_GATLINGGUN:49:15:0:0:100', '63:3132:UNIT_RIFLEMAN:51:15:0:30:100', RANGED),
        ('0:3133:UNIT_ARCHER:49:15:0:0:100', '63:3134:UNIT_WARRIOR:51:15:0:25:100', RANGED),
        ('0:3135:UNIT_CROSSBOWMAN:49:15:0:0:100', '63:3136:UNIT_SPEARMAN:51:15:0:30:100', RANGED),
    ], True),
    'siege': ([
        ('0:3141:UNIT_CANNON:49:15:0:0:100', '63:3142:UNIT_MUSKETMAN:51:15:0:35:100', RANGED),
        ('0:3143:UNIT_FIELD_GUN:49:15:0:0:100', '63:3144:UNIT_RIFLEMAN:51:15:0:40:100', RANGED),
        ('0:3145:UNIT_ARTILLERY:49:15:0:0:100', '63:3146:UNIT_MACHINE_GUN:51:15:0:35:100', RANGED),
    ], True),
    'air': ([
        ('0:2013:UNIT_BOMBER:49:15:0:0:100', '63:2014:UNIT_RIFLEMAN:51:15:0:30:100', RANGED),
    ], False),
    'naval': (['s-n1', 's-n2', 's-n3'], False),   # 0557's recorded naval melee fights (combat_scenes.py's side_args)
    'naval_ranged': ([
        ('0:2011:UNIT_DREADNOUGHT:49:15:0:0:100', '63:2012:UNIT_LANCER:51:15:0:30:100', RANGED),
        ('0:2009:UNIT_IRONCLAD:49:15:0:0:100', '63:city:0:51:15:-:20:200', RANGED),
    ], True),
}
NUM = ['p1', 'p2', 'attReady', 'firstStrike', 'astances', 'aholds', 'attReady1600', 'attReadyFromIdle', 'arrive', 'reformAtt',
       'reformDef', 'oneShots', 'osReady', 'osDefend', 'osMove', 'osCharge', 'osStrike', 'osDeath', 'osVictory', 'osOther']


def run_one(a, exe, out, att, dfn, extra, flags, seed=None):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', seed or a.seed] + list(extra) + list(flags)
    if a.no_4743 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 4743 W81: charge_any on by default; off on EXE only
        cmd.append('--no-charge-any')
    if a.no_5057 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5057 W85: both knobs on by default; off on EXE only
        cmd += ['--no-rush-ride-through', '--no-defender-waits']
    if a.no_5061 and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5061 W139: on by default; off on EXE only
        cmd.append('--no-defender-step')
    if a.no_victory_word and pathlib.Path(exe) == pathlib.Path(a.exe):   # 5059 W100: on by default; off on EXE only
        cmd.append('--no-victory-word')
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    lines = text.splitlines()
    d = {}
    for pre in ('reform:', 'ready:', 'phases:'):
        d.update(re.findall(r'(\w+)=(\S+)', next((l for l in lines if l.startswith(pre)), '')))
    tline = next((l for l in lines if l.startswith('trigger_scene:')), '')
    d['line'] = tline
    d.setdefault('end', '-1')   # the phases: line's (the trigger_scene: line's end is a word)
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


def meets(knob, d):
    if knob == 'a':   # phase 1 ends within 0.1 s of the last arrival (a reform took place)
        arrive, p1 = num(d, 'arrive'), num(d, 'p1')
        return arrive > 0 and p1 >= arrive - 1e-6 and p1 - arrive < 0.1
    if knob == 'bh':   # (b) without Walk: the hold, and the first 1600 not before its end
        return num(d, 'aholds', 0) > 0 and num(d, 'attReady') - num(d, 'p1') >= 0.45 - 1e-6
    # (b): every attacker figure stood in the stance, none entered combat ready from idle
    return num(d, 'astances', 0) > 0 and num(d, 'attReadyFromIdle', -1) == 0


def regression(a):
    want = [c for c in a.cases.split(',') if c]
    for c in want:
        if c not in CASES:
            print(f'{c}: not a case ({",".join(CASES)})')
            return 2
    rows, failed, breaking = [], 0, 0
    for label in want:
        knob, att, dfn, extra = CASES[label]
        # the default arm passes no knob flag (both on since W74); --arm own turns on only the case's own knob
        flags = OFF if a.expect_fail else (ARMS[knob] if a.arm == 'own' else [])
        d = run_one(a, a.exe, pathlib.Path(a.work) / ('off' if a.expect_fail else a.arm) / label, att, dfn, extra, flags)
        rule = meets(knob, d)
        same = '-'
        if a.exe_base:
            off = d if a.expect_fail else run_one(a, a.exe, pathlib.Path(a.work) / 'off' / label, att, dfn, extra, OFF)
            base = run_one(a, a.exe_base, pathlib.Path(a.work) / 'base' / label, att, dfn, extra, [])
            ident = base['ok'] and off['ok'] and off['line'] == base['line'] and off['log'] == base['log']
            same = 'same' if ident else 'DIFF(line %s log %s)' % (off['line'] == base['line'], off['log'] == base['log'])
        breaking += not rule
        ok = d['ok'] and (not rule if a.expect_fail else rule) and same in ('-', 'same')
        failed += not ok
        row = {'case': label, 'knob': knob, 'result': 'pass' if ok else 'FAIL', 'identity': same, 'end': d['end']}
        row.update({k: d.get(k, '') for k in NUM})
        rows.append(row)
        print('%-4s (%s) %-4s end=%s arrive=%s p1=%s p2=%s firstStrike=%s astances=%s attReadyFromIdle=%s osReady=%s '
              'oneShots=%s aholds=%s %s' % (label, knob, row['result'], d['end'], d.get('arrive'), d.get('p1'),
                                            d.get('p2'), d.get('firstStrike'), d.get('astances'),
                                            d.get('attReadyFromIdle'), d.get('osReady'), d.get('oneShots'),
                                            d.get('aholds'), same), flush=True)
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
            'PASS' if passed else 'FAIL', n - failed, n, breaking, n))
    else:
        print('RESULT %s %d/%d' % ('PASS' if passed else 'FAIL', n - failed, n))
    return 0 if passed else 1


def load_side_args():
    spec = importlib.util.spec_from_file_location('cs4776', REPO / 'render/evidence/0042/4776/combat_scenes.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.side_args


def table(a):
    seeds = [s for s in a.seeds.split(',') if s]
    turns = [t for t in a.turns.split(',') if t]
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
    side_args, rec = None, None
    rows = []
    for kind in kinds:
        cases, turned = KINDS[kind]
        for ci, c in enumerate(cases):
            if isinstance(c, str):   # a 0557 label
                if side_args is None:
                    side_args = load_side_args()
                    rec = {r['label']: r for r in json.load(open(CASES_0557, encoding='utf-8'))}
                att, dfn = side_args(rec[c])
                extra, name = [], c
            else:
                att, dfn, extra = c
                name = att.split(':')[2] + '>' + dfn.split(':')[2]
            for k in (turns if turned else ['0']):
                for seed in seeds:
                    for arm in arms:
                        out = pathlib.Path(a.work) / 'table' / arm / ('%s-%d-k%s-s%s' % (kind, ci, k, seed))
                        tx = extra + (['--attacker-turn', k] if k != '0' else [])
                        d = run_one(a, a.exe, out, att, dfn, tx, ARMS[arm], seed)
                        r = {'kind': kind, 'case': name, 'turn': k, 'seed': seed, 'arm': arm, 'ok': d['ok'],
                             'end': num(d, 'end')}
                        r.update({x: num(d, x) for x in NUM})
                        r['reformed'] = r['reformAtt'] > 0 or r['reformDef'] > 0
                        rows.append(r)
                        print('%-12s %-36s K%s s%s %-6s ok=%d end %.3f p1 %.3f p2 %.3f strike %.3f stances %d fromIdle %d '
                              'oneShots %d ready %d' % (kind, name, k, seed, arm, d['ok'], r['end'], r['p1'], r['p2'],
                                                         r['firstStrike'], r['astances'], r['attReadyFromIdle'],
                                                         r['oneShots'], r['osReady']), flush=True)
                        if not d['ok']:
                            print('      ' + d['text'].replace('\n', '\n      ')[-800:])
    allok = all(r['ok'] for r in rows)

    def mean(rr, key, pos=False):
        v = [r[key] for r in rr if (r[key] >= 0 or not pos)]
        return statistics.mean(v) if v else float('nan')
    print('\nMEANS per kind and arm (seconds; one-shots per fight; p1r = p1 over the runs that reformed)')
    print('%-12s %-6s %4s %4s %6s %6s %6s %7s %6s %6s %5s %5s %5s %5s %5s %5s %5s %5s' % (
        'kind', 'arm', 'n', 'nref', 'p1', 'p1r', 'p2', 'strike', 'end', 'shots', '1600', '1280', 'move', 'chrg',
        'strk', 'death', 'vict', 'other'))
    for kind in kinds:
        for arm in arms:
            rr = [r for r in rows if r['kind'] == kind and r['arm'] == arm and r['ok']]
            if not rr:
                continue
            ref = [r for r in rr if r['reformed']]
            print('%-12s %-6s %4d %4d %6.3f %6.3f %6.3f %7.3f %6.3f %6.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f' % (
                kind, arm, len(rr), len(ref), mean(rr, 'p1', True), mean(ref, 'p1', True) if ref else float('nan'),
                mean(rr, 'p2', True), mean(rr, 'firstStrike', True), mean(rr, 'end'), mean(rr, 'oneShots'),
                mean(rr, 'osReady'), mean(rr, 'osDefend'), mean(rr, 'osMove'), mean(rr, 'osCharge'), mean(rr, 'osStrike'),
                mean(rr, 'osDeath'), mean(rr, 'osVictory'), mean(rr, 'osOther')))
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
    ap.add_argument('--no-4743', action='store_true',
                    help='4743 off on EXE only (--no-charge-any; charge_any on by default again since 4743 W81): with '
                         '--exe-base, the identity against a base built before W81')
    ap.add_argument('--no-victory-word', action='store_true',
                    help='5059 off on EXE only (CombatParams::victory_needs_word, on by default since 5059 W100): '
                         'with --exe-base, the identity against a base built before W100')
    ap.add_argument('--no-5057', action='store_true',
                    help='5057 off on EXE only (--no-rush-ride-through --no-defender-waits; both on by default since 5057 W85): with '
                         '--exe-base, the identity against a base built before W85')
    ap.add_argument('--no-5061', action='store_true',
                    help='5061 off on EXE only (--no-defender-step; on by default since 5061 W139): with --exe-base, the '
                         'identity against a base built before 5061')
    ap.add_argument('--arm', choices=['default', 'own'], default='default',
                    help='default: no knob flag (both on since 5050 W74); own: only the case\'s own knob on')
    ap.add_argument('--csv', default=None)
    ap.add_argument('--table', action='store_true')
    ap.add_argument('--seeds', default='1,2,3')
    ap.add_argument('--turns', default='0,2')
    ap.add_argument('--arms', default='before,a,b,ab')   # bh is b (a regression case's knob name)
    ap.add_argument('--kinds', default=','.join(KINDS))
    a = ap.parse_args()
    for e in [a.exe] + ([a.exe_base] if a.exe_base else []):
        if not pathlib.Path(e).is_file():
            print('missing ' + e)
            return 2
    return table(a) if a.table else regression(a)


if __name__ == '__main__':
    sys.exit(main())
