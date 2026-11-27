"""Ticket 5074: a turned attacking crew without Walk plays stock's crew entry before its first strike, on the install's art
(trigger_scene.exe, built by build_trigger_scene.py; unit-combat-server.md 18.26.4).

    python -I server/tests/effects/run_entry_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases t1,...] [--expect-fail] [--exe-base BASE_TRIGGER_SCENE.EXE] [--csv OUT.csv]
    python -I server/tests/effects/run_entry_scenes.py --table --exe ... --game ... --db ... --work DIR
        [--turns 0,1,2,3] [--seeds 1,2] [--csv OUT.csv]

The knob: CombatParams::attack_entry_after_turn (on by default; trigger_scene --no-attack-entry-after-turn turns it off,
the rule before 5074). Stock's crew without Walk ends its phase 2 (0xF) with state 3's 1000: its machine plays the run
stop and goes idle, and the strike from idle enters combat ready through the whole 1600 entry clip (the Gatling crew's
rifle cock and fortify vox) [L 18.26.2, 18.26.4]. Before 5074 route S's turned crew (turn_first, 1400) requested 1600
straight from its run, so its machine went to combat ready without the entry clip and the crew struck 1.0 s after the
turn.

Cases (flat field; plots and damage are stand-ins; --ranged --no-retaliate; --attacker-turn K: K facings away):
  t1  entry  a Gatling gun turned 2 facings away shoots a rifleman: every 1600 of the attacker is requested from idle
             (attReadyFromIdle = attReady1600 > 0), its entry clip sounds (osReady > 0) and the first strike comes at
             least 0.9 s after the first 1600 (the whole entry).
  t2  entry  the same turned 1 facing away.
  s1  same   an archer turned 2 facings away (Walk and a 1450 clip): the knob changes nothing (on and off identical).
  s2  same   the Gatling gun facing its target (no turn): the same.
  s3  same   a cannon turned 2 facings away (it reforms, which faces it: no turn): the same.
  s4  same   a field gun turned 1 facing away (it reforms): the same.
--expect-fail is the regression arm: every case runs with --no-attack-entry-after-turn and the arm passes when EVERY
`entry` case breaks its expectation (the `same` cases are not run). --exe-base also runs BASE (a trigger_scene built from
a main before 5074) with no flag and requires the knob-off arm's `trigger_scene:` line and WORK/off/<case>/triggers.log
to be byte-identical to BASE's: the knob off is the code before it. --table runs every ranged land attacker of TABLE K
facings away on the seeds, knob on and off, and prints per run both arms' first 1600, first strike, 1600 requests from
idle, entry one-shots and end, and whether the two arms are identical (with --exe-base also the off arm against BASE,
every run identical or the table fails). Prints one row per case and `RESULT PASS|FAIL n/m`;
exit 0 on PASS, 1 on FAIL, 2 on usage. Names from the install go only into WORK, never into git; the CSV holds numbers
and the case labels only. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys

RANGED = ['--ranged', '--no-retaliate']
# label: (expectation, attacker, defender, extra trigger_scene arguments)
CASES = {
    't1': ('entry', '0:5101:UNIT_GATLINGGUN:49:15:0:0:100', '63:5102:UNIT_RIFLEMAN:50:15:0:30:100',
           RANGED + ['--attacker-turn', '2']),
    't2': ('entry', '0:5103:UNIT_GATLINGGUN:49:15:0:0:100', '63:5104:UNIT_RIFLEMAN:50:15:0:30:100',
           RANGED + ['--attacker-turn', '1']),
    's1': ('same', '0:5105:UNIT_ARCHER:49:15:0:0:100', '63:5106:UNIT_WARRIOR:51:15:0:25:100',
           RANGED + ['--attacker-turn', '2']),
    's2': ('same', '0:5107:UNIT_GATLINGGUN:49:15:0:0:100', '63:5108:UNIT_RIFLEMAN:50:15:0:30:100', RANGED),
    's3': ('same', '0:5109:UNIT_CANNON:49:15:0:0:100', '63:5110:UNIT_RIFLEMAN:51:15:0:19:100',
           RANGED + ['--attacker-turn', '2']),
    's4': ('same', '0:5111:UNIT_FIELD_GUN:49:15:0:0:100', '63:5112:UNIT_RIFLEMAN:51:15:0:30:100',
           RANGED + ['--attacker-turn', '1']),
}
OFF = ['--no-attack-entry-after-turn']
NUM = ['p1', 'attReady', 'firstStrike', 'attReady1600', 'attReadyFromIdle', 'osReady', 'oneShots', 'end']
# --table: every ranged land attacker the knob can reach (members without Walk in VP's art, 18.23) and two Walk shooters,
# each K facings away (--turns) on --seeds, knob on and off: what the knob moves (lead-0042 #15103)
TABLE = [
    ('UNIT_GATLINGGUN', 'UNIT_RIFLEMAN'), ('UNIT_MACHINE_GUN', 'UNIT_RIFLEMAN'), ('UNIT_CANNON', 'UNIT_MUSKETMAN'),
    ('UNIT_FIELD_GUN', 'UNIT_RIFLEMAN'), ('UNIT_ARTILLERY', 'UNIT_MACHINE_GUN'), ('UNIT_ROCKET_ARTILLERY', 'UNIT_INFANTRY'),
    ('UNIT_CATAPULT', 'UNIT_SPEARMAN'), ('UNIT_TREBUCHET', 'UNIT_PIKEMAN'), ('UNIT_ARCHER', 'UNIT_WARRIOR'),
    ('UNIT_CROSSBOWMAN', 'UNIT_SPEARMAN'),
]


def run_one(a, exe, out, att, dfn, extra, flags):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', a.seed] + list(extra) + list(flags)
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    lines = text.splitlines()
    d = {}
    for pre in ('ready:', 'phases:'):
        d.update(re.findall(r'(\w+)=(\S+)', next((l for l in lines if l.startswith(pre)), '')))
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


def entry(d):
    n, idle = num(d, 'attReady1600', 0), num(d, 'attReadyFromIdle', -1)
    return (n > 0 and idle == n and num(d, 'osReady', 0) > 0 and
            num(d, 'firstStrike') - num(d, 'attReady') >= 0.9 - 1e-6)


def table(a):
    rows = []
    work = pathlib.Path(a.work) / 'table'
    allok = True
    for ui, (at, dt) in enumerate(TABLE):
        for k in [t for t in a.turns.split(',') if t]:
            for seed in [s for s in a.seeds.split(',') if s]:
                att = '0:%d:%s:49:15:0:0:100' % (5201 + 2 * ui, at)
                dfn = '63:%d:%s:50:15:0:30:100' % (5202 + 2 * ui, dt)
                extra = RANGED + (['--attacker-turn', k] if k != '0' else [])
                a.seed = seed
                r = {'attacker': at, 'defender': dt, 'turn': k, 'seed': seed}
                res = {}
                for arm, flags in (('on', []), ('off', OFF)):
                    d = run_one(a, a.exe, work / arm / ('%s-k%s-s%s' % (at, k, seed)), att, dfn, extra, flags)
                    allok = allok and d['ok']
                    res[arm] = d
                    for x in ('attReady', 'firstStrike', 'attReadyFromIdle', 'osReady', 'end'):
                        r['%s_%s' % (arm, x)] = num(d, x)
                    r[arm + '_ok'] = d['ok']
                r['same'] = res['on']['line'] == res['off']['line'] and res['on']['log'] == res['off']['log']
                if a.exe_base:   # the knob off against BASE (a main before 5074): identical line and trigger log
                    b = run_one(a, a.exe_base, work / 'base' / ('%s-k%s-s%s' % (at, k, seed)), att, dfn, extra, [])
                    r['base_ident'] = b['ok'] and b['line'] == res['off']['line'] and b['log'] == res['off']['log']
                    allok = allok and r['base_ident']
                rows.append(r)
                print('%-22s K%s s%s base=%s same=%d | on ready %.3f strike %.3f fromIdle %d osReady %d end %.3f | off ready %.3f '
                      'strike %.3f fromIdle %d osReady %d end %.3f' % (
                          at, k, seed, r.get('base_ident', '-'), r['same'], r['on_attReady'], r['on_firstStrike'], r['on_attReadyFromIdle'],
                          r['on_osReady'], r['on_end'], r['off_attReady'], r['off_firstStrike'],
                          r['off_attReadyFromIdle'], r['off_osReady'], r['off_end']), flush=True)
                for arm in ('on', 'off'):
                    if not res[arm]['ok']:
                        print('      %s: %s' % (arm, res[arm]['text'].replace('\n', '\n      ')[-800:]))
    if a.csv and rows:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
    print('RESULT %s %d runs, %d moved by the knob' % ('PASS' if allok else 'FAIL', len(rows),
                                                      sum(1 for r in rows if not r['same'])))
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
    ap.add_argument('--turns', default='0,1,2,3')
    ap.add_argument('--seeds', default='1,2')
    a = ap.parse_args()
    for e in [a.exe] + ([a.exe_base] if a.exe_base else []):
        if not pathlib.Path(e).is_file():
            print('missing ' + e)
            return 2
    if a.table:
        return table(a)
    want = [c for c in a.cases.split(',') if c]
    for c in want:
        if c not in CASES:
            print(f'{c}: not a case ({",".join(CASES)})')
            return 2
    if a.expect_fail:
        want = [c for c in want if CASES[c][0] == 'entry']
    rows, failed, breaking = [], 0, 0
    work = pathlib.Path(a.work)
    for label in want:
        kind, att, dfn, extra = CASES[label]
        off = run_one(a, a.exe, work / 'off' / label, att, dfn, extra, OFF)
        on = None if a.expect_fail else run_one(a, a.exe, work / 'on' / label, att, dfn, extra, [])
        d = off if a.expect_fail else on
        if kind == 'entry':
            rule = entry(d)
        else:   # the knob changes nothing: on and off identical (line and trigger log)
            rule = on['ok'] and off['ok'] and on['line'] == off['line'] and on['log'] == off['log']
        same = '-'
        if a.exe_base:
            base = run_one(a, a.exe_base, work / 'base' / label, att, dfn, extra, [])
            ident = base['ok'] and off['ok'] and off['line'] == base['line'] and off['log'] == base['log']
            same = 'same' if ident else 'DIFF(line %s log %s)' % (off['line'] == base['line'], off['log'] == base['log'])
        breaking += not rule
        ok = d['ok'] and off['ok'] and (not rule if a.expect_fail else rule) and same in ('-', 'same')
        failed += not ok
        row = {'case': label, 'kind': kind, 'result': 'pass' if ok else 'FAIL', 'identity': same}
        row.update({k: d.get(k, '') for k in NUM})
        row.update({'off_' + k: off.get(k, '') for k in ('attReady', 'firstStrike', 'attReadyFromIdle', 'osReady', 'end')})
        rows.append(row)
        print('%-3s (%s) %-4s attReady=%s firstStrike=%s attReady1600=%s attReadyFromIdle=%s osReady=%s end=%s | off '
              'attReady=%s firstStrike=%s fromIdle=%s osReady=%s end=%s | base %s' % (
                  label, kind, row['result'], d.get('attReady'), d.get('firstStrike'), d.get('attReady1600'),
                  d.get('attReadyFromIdle'), d.get('osReady'), d.get('end'), off.get('attReady'),
                  off.get('firstStrike'), off.get('attReadyFromIdle'), off.get('osReady'), off.get('end'), same),
              flush=True)
        for x in (off, on):
            if x is not None and not x['ok']:
                print('      ' + x['text'].replace('\n', '\n      ')[-1500:])
    if a.csv and rows:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    n = len(rows)
    passed = failed == 0 and n > 0
    if a.expect_fail:
        print('RESULT %s %d/%d (expect-fail: %d of %d case(s) break their expectation with the knob off)' % (
            'PASS' if passed else 'FAIL', n - failed, n, breaking, n))
    else:
        print('RESULT %s %d/%d' % ('PASS' if passed else 'FAIL', n - failed, n))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
