"""Ticket 4735: the run and terrain-surface loops of figures that turn before a fight whose figures stand (a ranged,
siege or naval attack), on the install's art (trigger_scene.exe, built by build_trigger_scene.py; unit-combat-server.md
10.11). The regression case of 4735: before it, the server's ranged, siege and naval figures sent no run loop.

    python -I server/tests/effects/run_turn_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases g1,...] [--csv OUT.csv] [--before-only]

The cases take their unit types from W37's recorded stock pair (unit-combat-server.md 10.8: the Gatling, field gun,
artillery, cruiser and dreadnought fights of the huge save's AI turn; the Tiny save's archer); plots and damage are
stand-ins on trigger_scene's flat field, and each attacker (or defender) stands K facings away from its opponent, as a
unit that arrived from elsewhere does. Each case runs twice: `before` with --no-turn-first (the rule before 4735) and
`after` (the default). A case passes when both runs exit 0 (trigger_scene's own checks: the fight ended, every loop
stopped by a SoundStop or its owner's end, nothing after its owner's end) and the `after` run meets the case's
expectation: `att` the attacker turns and sends run loops, `def` the defender does, `turn` somebody turns (loops not
required), `none` nobody turns (a unit already facing its opponent). It also prints how the run moved the fight's length (elapsed after - before). --before-only runs
only the `before` arm and applies the expectation to it (it fails on a build without 4735's turn: the regression).
Both arms run with --no-reform-first (4745: a reforming unit faces its opponent by the reform and takes no turn).
Prints one row per case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys

# label: (attacker, defender, extra trigger_scene arguments, expectation)
CASES = {
    'g1': ('0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:30:100', ['--ranged', '--attacker-turn', '2'], 'att'),
    'a1': ('0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:25:100', ['--ranged', '--attacker-turn', '3'], 'att'),
    'f1': ('0:2005:UNIT_FIELD_GUN:49:15:0:0:100', '63:2006:UNIT_RIFLEMAN:51:15:0:40:100', ['--ranged', '--attacker-turn', '1'], 'att'),
    'r1': ('0:2007:UNIT_ARTILLERY:49:15:0:0:100', '63:2008:UNIT_MACHINE_GUN:51:15:0:35:100', ['--ranged', '--attacker-turn', '2'], 'att'),
    'n1': ('0:2009:UNIT_IRONCLAD:49:15:0:0:100', '63:city:0:51:15:-:20:200', ['--ranged', '--attacker-turn', '2'], 'att'),
    'n3': ('0:2017:UNIT_CRUISER:49:15:0:0:100', '63:city:0:51:15:-:20:200', ['--ranged', '--attacker-turn', '2'], 'att'),
    # the dreadnought turns, but its run clip crosses no loop trigger within the turn (unit-combat-server.md 10.11, open)
    'n2': ('0:2011:UNIT_DREADNOUGHT:49:15:0:0:100', '63:2012:UNIT_LANCER:51:15:0:30:100', ['--ranged', '--attacker-turn', '3'], 'turn'),
    'd1': ('0:2013:UNIT_GATLINGGUN:49:15:0:0:100', '63:2014:UNIT_RIFLEMAN:51:15:0:30:100', ['--ranged', '--defender-turn', '3'], 'def'),
    'c0': ('0:2015:UNIT_GATLINGGUN:49:15:0:0:100', '63:2016:UNIT_RIFLEMAN:51:15:0:30:100', ['--ranged'], 'none'),
}
KEYS = ['end', 'elapsed', 'SoundStart', 'loopsStarted', 'turns', 'runLoops', 'runLoopsAtt', 'runLoopsDef']


def run_one(a, label, arm, att, dfn, extra):
    out = pathlib.Path(a.work) / arm / label
    out.mkdir(parents=True, exist_ok=True)
    cmd = [a.exe, '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', a.seed] + extra + (['--no-turn-first'] if arm == 'before' else [])
    # 4745: the cases test 4735's turn alone; the reform (on by default since 4745) replaces the turn of a unit whose
    # ReformBeforeCombat names it (archer, field gun, artillery), so both arms run without it (run_reform_scenes.py has it)
    cmd.append('--no-reform-first')
    if a.no_5057:   # 5057 W85: the ride-through and the type 2 defender wait off (the rules before 5057)
        cmd += ['--no-rush-ride-through', '--no-defender-waits']
    if a.no_victory_word:   # 5059 W100: victory by the machine's 1800 alone (the rule before 5059)
        cmd.append('--no-victory-word')
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    d = {}
    for pre in ('trigger_scene:', 'turn:'):
        line = next((l for l in text.splitlines() if l.startswith(pre)), '')
        d.update(re.findall(r'(\w+)=(\S+)', line))
    d['ok'] = p.returncode == 0 and any(l.startswith('trigger_scene: OK') for l in text.splitlines())
    d['text'] = text
    return d


def meets(d, want):
    n = lambda k: int(d.get(k, '0') or 0)
    if want == 'att':
        return n('turns') > 0 and n('runLoopsAtt') > 0
    if want == 'def':
        return n('turns') > 0 and n('runLoopsDef') > 0
    if want == 'turn':
        return n('turns') > 0
    return n('turns') == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    ap.add_argument('--cases', default=','.join(CASES))
    ap.add_argument('--csv', default=None)
    ap.add_argument('--before-only', action='store_true')
    ap.add_argument('--no-victory-word', action='store_true')   # 5059: victory by the machine's 1800 alone (the rule before 5059)
    ap.add_argument('--no-5057', action='store_true', help='5057: both knobs off (the rules before 5057)')
    a = ap.parse_args()
    if not pathlib.Path(a.exe).is_file():
        print('missing ' + a.exe)
        return 2
    want = [c for c in a.cases.split(',') if c]
    for c in want:
        if c not in CASES:
            print(f'{c}: not a case ({",".join(CASES)})')
            return 2
    rows, passed = [], 0
    for label in want:
        att, dfn, extra, expect = CASES[label]
        b = run_one(a, label, 'before', att, dfn, extra)
        r = b if a.before_only else run_one(a, label, 'after', att, dfn, extra)
        ok = b['ok'] and r['ok'] and meets(r, expect)
        passed += ok
        try:
            delta = '%+.3f' % (float(r.get('elapsed', 'nan')) - float(b.get('elapsed', 'nan')))
        except ValueError:
            delta = '?'
        row = {'case': label, 'expect': expect, 'result': 'pass' if ok else 'FAIL'}
        for k in KEYS:
            row['before_' + k] = b.get(k, '')
            row['after_' + k] = r.get(k, '')
        row['delta_s'] = delta
        rows.append(row)
        print('%-3s %-4s %-4s before: end=%s %ss turns=%s runLoops=%s/%s loops=%s | after: end=%s %ss turns=%s runLoops=%s/%s '
              'loops=%s | length %s s' % (
                  label, expect, row['result'], b.get('end', '?'), b.get('elapsed', '?'), b.get('turns', '?'),
                  b.get('runLoopsAtt', '?'), b.get('runLoopsDef', '?'), b.get('loopsStarted', '?'), r.get('end', '?'),
                  r.get('elapsed', '?'), r.get('turns', '?'), r.get('runLoopsAtt', '?'), r.get('runLoopsDef', '?'),
                  r.get('loopsStarted', '?'), delta), flush=True)
        for d in (b, r):
            if not d['ok']:
                print('      ' + d['text'].replace('\n', '\n      ')[-1500:])
    if a.csv:
        cols = ['case', 'expect', 'result', 'delta_s'] + ['before_' + k for k in KEYS] + ['after_' + k for k in KEYS]
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
            w.writeheader()
            for r in rows:
                w.writerow(r)
    print('RESULT %s %d/%d' % ('PASS' if passed == len(want) else 'FAIL', passed, len(want)))
    return 0 if passed == len(want) else 1


if __name__ == '__main__':
    sys.exit(main())
