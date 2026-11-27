"""Ticket 5067 S1: stock's phase 2 before a type 2 fight's charge, on the install's art (trigger_scene.exe, built by
build_trigger_scene.py; the fight goes in as the SrvFeed combat record the bridge sends, through the server's fight clock
and the core stepped live against the controller; unit-combat-server.md 18.29.3).

    python -I server/tests/effects/run_type2_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seeds 1,2,3] [--cases kn_kill,...] [--expect-fail] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-victory-word]]
        [--table] [--csv OUT.csv]

The knob: CombatParams::type2_phase2 (trigger_scene --type2-phase2 / --no-type2-phase2; off by default until the MC's
word). Stock (W91's sampler of these cases, 18.29.3 [L]): phase 2 lasts 0.665 s and the charge starts 0.699 s after the
simulation in every Knight fight; the defender's figures stand in 0xF meanwhile. trigger_scene's `type2:` line gives the
attacker's first movement request (attFirstMove: 1120 / 1400 / 1450), the defender's phase-2 stances before it
(defStances: Turn with 1450) and the attacker's first landing.

Cases: the type 2 pairs of run_4779.py's land cases as W101's live pair ran them (attacker next to the defender, one
order, no move; the after-damage of the live pair), and two controls the rule must not reach (a ranged archer, a type 1
Great War Infantry on a Machine Gun).
  Default arm (--type2-phase2): a type 2 case passes when trigger_scene exits 0, its attFirstMove lies within
  [P2 - 0.04, P2 + 0.10] s (P2 = 0.665, one 1/30 s step of slack below) and every defender with Walk took the stance
  (defStances > 0); a control passes when its `trigger_scene:` line and triggers.log equal the --no-type2-phase2 run's.
  --expect-fail: the cases run with --no-type2-phase2 (the rule before 5067) and the arm passes when EVERY type 2 case
  breaks its expectation (the controls are left out).
  --exe-base BASE: also runs BASE with no knob flag and requires the --no-type2-phase2 run's `trigger_scene:` line and
  WORK/<case>-off/triggers.log to be byte-identical to BASE's (BASE: a trigger_scene built from main before 5067).
  --table: every case x seed, off and on: the end, attFirstMove, the first landing; means per group (cavalry, foot).
  --no-5061: every run of EXE also gets --no-defender-step (5061's knob, on by default since W139): with --exe-base, the
  identity against a base built before 5061.
Ticket 5061 (W139, unit-combat-server.md 18.45), --step: the type 2 defender's step from the attacker's first landing
(CombatParams::defender_step; trigger_scene --defender-step / --no-defender-step and its `defstep:` line). Both arms run
with --type2-phase2 (the server's default). A foot case passes when the on arm exits 0, its defender made approaches or
charges (defMoves > 0), the first of them not before the attacker's first landing, and the off arm made none; a cavalry
case when both arms exit 0 and the off arm made none; a control when its `trigger_scene:` line and triggers.log are the
same on and off. --step --expect-fail: the off arm only, passing when EVERY foot case breaks its expectation.
Prints one row per run and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Names from the install go only
into WORK (triggers.log), never into git; the CSV holds numbers and unit type names only. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import statistics
import subprocess
import sys

P2 = 0.665
LOW, HIGH = P2 - 0.04, P2 + 0.10
RANGED = ['--ranged', '--no-retaliate']
# label: (group, attacker, defender, extra trigger_scene arguments); the after-damage of W101's live pair (18.29.2)
CASES = {
    'melee': ('foot', '0:5001:UNIT_SWORDSMAN:10:10:0:26:100', '63:5002:UNIT_BARBARIAN_SWORDSMAN:11:10:0:26:100', []),
    'wh_warrior': ('foot', '0:5003:UNIT_WARRIOR:10:10:0:39:100', '63:5004:UNIT_WARRIOR:11:10:0:25:100', []),
    'wh_knight': ('cavalry', '0:5005:UNIT_KNIGHT:10:10:0:20:100', '63:5006:UNIT_SPEARMAN:11:10:0:36:100', []),
    'wh_knight_pike': ('cavalry', '0:5007:UNIT_KNIGHT:10:10:0:29:100', '63:5008:UNIT_PIKEMAN:11:10:0:35:100', []),
    'kn_kill': ('cavalry', '0:5009:UNIT_KNIGHT:10:10:20:46:100', '63:5010:UNIT_SPEARMAN:11:10:60:93:100', []),
    'kn_kill_b': ('cavalry', '0:5011:UNIT_KNIGHT:10:10:20:31:100', '63:5012:UNIT_SPEARMAN:11:10:60:100:100', []),
    'c_ranged': ('control', '0:5013:UNIT_ARCHER:49:15:0:0:100', '63:5014:UNIT_WARRIOR:51:15:0:25:100', RANGED),
    'c_type1': ('control', '0:5015:UNIT_GREAT_WAR_INFANTRY:50:15:0:20:100', '63:5016:UNIT_MACHINE_GUN:51:15:0:45:100', []),
}
ON, OFF = ['--type2-phase2'], ['--no-type2-phase2']
STEP_ON, STEP_OFF = ['--type2-phase2', '--defender-step'], ['--type2-phase2', '--no-defender-step']   # 5061


def run(exe, a, label, seed, arm, flags):
    group, att, dfn, extra = CASES[label]
    out = pathlib.Path(a.work) / ('%s-s%s-%s' % (label, seed, arm))
    out.mkdir(parents=True, exist_ok=True)
    cmd = [exe, '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', seed] + extra + flags
    if a.no_5061 and pathlib.Path(exe) == pathlib.Path(a.exe) and '--defender-step' not in flags:   # 5061: EXE only
        cmd.append('--no-defender-step')
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    line = next((l for l in text.splitlines() if l.startswith('trigger_scene:')), '')
    m = re.search(r'^type2: type2Phase2=(\d) attFirstMove=(\S+) defStances=(\d+) attFirstLanding=(\S+) end=(\S+)', text,
                  re.M)
    s = re.search(r'^defstep: defenderStep=(\d) defMoves=(\d+) defMovers=(\d+) defFirstMove=(\S+) defMaxRun=(\S+) '
                  r'attFirstLanding=(\S+) end=(\S+)', text, re.M)   # 5061
    r = {'case': label, 'group': group, 'seed': seed, 'arm': arm, 'rc': p.returncode, 'line': line,
         'log': (out / 'triggers.log').read_bytes() if (out / 'triggers.log').exists() else b'',
         'move': float(m.group(2)) if m else -9.0, 'stances': int(m.group(3)) if m else -1,
         'landing': float(m.group(4)) if m else -9.0, 'end': float(m.group(5)) if m else -9.0,
         'typed': bool(m), 'tail': text[-300:],
         'stepped': bool(s), 'def_moves': int(s.group(2)) if s else -1, 'def_movers': int(s.group(3)) if s else -1,
         'def_first': float(s.group(4)) if s else -9.0, 'def_run': float(s.group(5)) if s else -9.0}
    return r


def expect_step(r):
    """5061: the on arm's expectation for a foot type 2 case."""
    return (r['rc'] == 0 and r['stepped'] and r['def_moves'] > 0 and
            r['def_first'] + 1e-6 >= r['landing'] and r['landing'] >= 0)


def main_step(a, labels, seeds):
    """5061 (--step): the type 2 defender's step on and off."""
    rows, ok, n = [], 0, 0
    for label in labels:
        group = CASES[label][0]
        for seed in seeds:
            off = run(a.exe, a, label, seed, 'stepoff', STEP_OFF)
            on = None if a.expect_fail else run(a.exe, a, label, seed, 'stepon', STEP_ON)
            checks = []
            if a.expect_fail:
                if group == 'foot':
                    checks.append(('breaks', off['rc'] == 0 and off['stepped'] and not expect_step(off)))
            elif group == 'control':
                checks.append(('same', on['rc'] == 0 and off['rc'] == 0 and on['line'] == off['line'] and
                               on['log'] == off['log']))
            else:
                checks.append(('off', off['rc'] == 0 and off['stepped'] and off['def_moves'] == 0))
                checks.append(('on', expect_step(on) if group == 'foot' else on['rc'] == 0 and on['stepped']))
            if not checks:
                continue
            passed = all(c[1] for c in checks)
            n += 1
            ok += passed
            row = {'case': label, 'group': group, 'seed': seed, 'rc_off': off['rc'], 'end_off': off['end'],
                   'def_moves_off': off['def_moves']}
            if on:
                row.update({'rc_on': on['rc'], 'end_on': on['end'], 'landing_on': on['landing'],
                            'def_moves_on': on['def_moves'], 'def_movers_on': on['def_movers'],
                            'def_first_on': on['def_first'], 'def_run_on': on['def_run']})
            row['checks'] = ' '.join('%s=%s' % (k, 'ok' if v else 'NO') for k, v in checks)
            row['result'] = 'PASS' if passed else 'FAIL'
            rows.append(row)
            print('%-15s %-8s s%s off: end %6.3f moves %d%s  %s  %s' % (
                label, group, seed, off['end'], off['def_moves'],
                ('  on: end %6.3f land %6.3f moves %d movers %d first %6.3f run %6.2f' % (
                    on['end'], on['landing'], on['def_moves'], on['def_movers'], on['def_first'], on['def_run']))
                if on else '', row['checks'], row['result']))
            for r in (off, on):
                if r and (r['rc'] != 0 or not r['stepped']):
                    print('    %s rc %d: %s' % (r['arm'], r['rc'], r['tail'].replace('\n', ' | ')))
    if a.table and not a.expect_fail:
        print('table (means over seeds): group  n  end off / on (s)  on - off  defender movers on  longest run on')
        for group in ('cavalry', 'foot', 'control'):
            g = [r for r in rows if r['group'] == group]
            if not g:
                continue
            mean = lambda k: statistics.mean(r[k] for r in g)
            print('  %-8s %2d  %6.3f / %6.3f  %+6.3f  %5.2f  %6.2f' % (
                group, len(g), mean('end_off'), mean('end_on'), mean('end_on') - mean('end_off'),
                mean('def_movers_on'), mean('def_run_on')))
    return rows, ok, n


def expect(r):
    """The default arm's expectation for a type 2 case."""
    return r['rc'] == 0 and r['typed'] and LOW <= r['move'] <= HIGH and r['stances'] > 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seeds', default='1')
    ap.add_argument('--cases', default=','.join(CASES))
    ap.add_argument('--expect-fail', action='store_true')
    ap.add_argument('--exe-base', default=None)
    ap.add_argument('--table', action='store_true')
    ap.add_argument('--csv', default=None)
    ap.add_argument('--no-5061', action='store_true')   # 5061: --no-defender-step on EXE's runs (identity vs a base)
    ap.add_argument('--step', action='store_true')      # 5061: the defender-step arms instead of the phase 2 arms
    ap.add_argument('--no-victory-word', action='store_true',
                    help='5059 off on EXE only (CombatParams::victory_needs_word, on by default since 5059 W100): '
                         'with --exe-base, the identity against a base built before 5059')
    a = ap.parse_args()
    labels = [c for c in a.cases.split(',') if c]
    bad = [c for c in labels if c not in CASES]
    if bad:
        print('unknown case(s): %s' % ', '.join(bad))
        return 2
    seeds = [s for s in a.seeds.split(',') if s]
    if a.step:   # 5061
        if a.exe_base or a.no_5061:
            print('--step takes neither --exe-base nor --no-5061')
            return 2
        rows, ok, n = main_step(a, labels, seeds)
        return finish(a, rows, ok, n)
    rows, ok, n = [], 0, 0
    nv = ['--no-victory-word'] if a.no_victory_word else []   # 5059: on EXE only, never on BASE
    for label in labels:
        group = CASES[label][0]
        for seed in seeds:
            off = run(a.exe, a, label, seed, 'off', OFF + nv)
            on = None if a.expect_fail else run(a.exe, a, label, seed, 'on', ON + nv)
            checks = []
            if a.expect_fail:
                if group != 'control':
                    checks.append(('breaks', off['rc'] == 0 and off['typed'] and not expect(off)))
            elif group == 'control':
                checks.append(('same', on['rc'] == 0 and off['rc'] == 0 and on['line'] == off['line'] and
                               on['log'] == off['log']))
            else:
                checks.append(('phase2', expect(on)))
            if a.exe_base:
                base = run(a.exe_base, a, label, seed, 'base', [])
                checks.append(('base', base['rc'] == 0 and base['line'] == off['line'] and base['log'] == off['log']))
            passed = all(c[1] for c in checks)
            n += 1
            ok += passed
            row = {'case': label, 'group': group, 'seed': seed, 'rc_off': off['rc'], 'move_off': off['move'],
                   'landing_off': off['landing'], 'end_off': off['end'], 'stances_off': off['stances']}
            if on:
                row.update({'rc_on': on['rc'], 'move_on': on['move'], 'landing_on': on['landing'], 'end_on': on['end'],
                            'stances_on': on['stances']})
            row['checks'] = ' '.join('%s=%s' % (k, 'ok' if v else 'NO') for k, v in checks)
            row['result'] = 'PASS' if passed else 'FAIL'
            rows.append(row)
            print('%-15s %-8s s%s off: move %6.3f land %6.3f end %6.3f st %d%s  %s  %s' % (
                label, group, seed, off['move'], off['landing'], off['end'], off['stances'],
                ('  on: move %6.3f land %6.3f end %6.3f st %d' % (on['move'], on['landing'], on['end'], on['stances']))
                if on else '', row['checks'], row['result']))
            for r in (off, on):
                if r and (r['rc'] != 0 or not r['typed']):
                    print('    %s rc %d: %s' % (r['arm'], r['rc'], r['tail'].replace('\n', ' | ')))
    if a.table and not a.expect_fail:
        print('table (means over seeds; s): group  n  move off / on  first landing off / on  end off / on  on - off')
        for group in ('cavalry', 'foot', 'control'):
            g = [r for r in rows if r['group'] == group]
            if not g:
                continue
            mean = lambda k: statistics.mean(r[k] for r in g)
            print('  %-8s %2d  %6.3f / %6.3f  %6.3f / %6.3f  %6.3f / %6.3f  %+6.3f' % (
                group, len(g), mean('move_off'), mean('move_on'), mean('landing_off'), mean('landing_on'),
                mean('end_off'), mean('end_on'), mean('end_on') - mean('end_off')))
    return finish(a, rows, ok, n)


def finish(a, rows, ok, n):
    print('RESULT %s %d/%d' % ('PASS' if ok == n and n > 0 else 'FAIL', ok, n))
    if a.csv and rows:
        keys = []
        for r in rows:
            keys += [k for k in r if k not in keys]
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=keys)
            w.writeheader()
            w.writerows(rows)
    return 0 if ok == n and n > 0 else 1


if __name__ == '__main__':
    sys.exit(main())
