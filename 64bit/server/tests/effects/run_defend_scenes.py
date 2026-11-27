"""Ticket 4750: a ranged attack's non-answering target meets bombard defend (1280) after stock's phase-2 stance (1450),
not from idle, on the install's art (trigger_scene.exe, built by build_trigger_scene.py; unit-combat-server.md 10.14).

    python -I server/tests/effects/run_defend_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases a1,...] [--no-defend-from-stance] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-5050] [--no-4743] [--no-5057] [--no-victory-word]] [--expect-fail]
        [--csv OUT.csv]

Stock [C 18.14 / 18.15, L W52; 10.14]: the target's figures go 0xF (1450 shuffle, phase 2) -> 3 -> 0xE (1280); the
machine then takes 1280 from its run loop or combat ready on the path-6 = 1 branch, the 1285 loop, which has no Sound
trigger; from idle the 1280 clip plays with its tier -1 FOOT_ALWAYS x2 and FORTIFY_VOX per figure. Route S before 4750
requested 1280 from idle (about 32 one-shots per ranged fight offline, 10.12).

The cases: run_ready_scenes.py's six ranged fights on trigger_scene's flat field (archer on a warrior, Gatling on a
rifleman: ranged; field gun on a rifleman, artillery on a machine gun: siege; dreadnought on a lancer, ironclad on a
city: naval_ranged), --ranged --no-retaliate, plus `a1t` (the archer's warrior target facing away: it turns with 1400
first, so it takes no stance and meets 1280 after a movement all the same) and `b1` (a bomber on a rifleman: an air
attacker, no stance by the agreed skip). trigger_scene's `defend:` line gives the stance steps (Defend with 1450), the
1280 requests, those made from idle (`defendFromIdle`: no movement before) and the 1280 one-shots (`osDefend`). A case
passes when the tool exits 0 with `trigger_scene: OK` and: stances > 0 and defendFromIdle 0 for a stance case (a1, g1,
f1, n2); stances 0 for r1 (the machine gun member has no Walk), a1t, b1 and n1 (the city has no figures). --expect-fail is the regression arm (with
--no-defend-from-stance, or a build before 4750): it passes when some stance case has defendFromIdle > 0. --exe-base
also runs BASE (a trigger_scene built from the branch's base: no `defend:` line, no --no-defend-from-stance) with the
same arguments minus --no-defend-from-stance and requires the `trigger_scene:` line and WORK/<case>/triggers.log to be
byte-identical (the knob off equals the code before it). Prints one row per case, the means per kind and `RESULT PASS|FAIL
n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Names from the install go only into WORK, never into git; the CSV holds
counts only. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys

# label: (kind, attacker, defender, extra arguments, a stance is expected)
CASES = {
    'a1': ('ranged', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:25:100', [], True),
    'g1': ('ranged', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:30:100', [], True),
    'f1': ('siege', '0:2005:UNIT_FIELD_GUN:49:15:0:0:100', '63:2006:UNIT_RIFLEMAN:51:15:0:40:100', [], True),
    # the machine gun's member has no Walk (VP's database): no stance, 1280 from idle, as stock's state 3 gives 1000
    'r1': ('siege', '0:2007:UNIT_ARTILLERY:49:15:0:0:100', '63:2008:UNIT_MACHINE_GUN:51:15:0:35:100', [], False),
    'n2': ('naval_ranged', '0:2011:UNIT_DREADNOUGHT:49:15:0:0:100', '63:2012:UNIT_LANCER:51:15:0:30:100', [], True),
    'n1': ('naval_ranged', '0:2009:UNIT_IRONCLAD:49:15:0:0:100', '63:city:0:51:15:-:20:200', [], False),
    'a1t': ('ranged', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:25:100',
            ['--defender-turn', '3'], False),
    'b1': ('air', '0:2013:UNIT_BOMBER:49:15:0:0:100', '63:2014:UNIT_RIFLEMAN:51:15:0:30:100', [], False),
}
COLS = ['stances', 'defend1280', 'defendFromIdle', 'osDefend', 'oneShots', 'osReady', 'osMove', 'osStrike', 'osDeath',
        'osOther']


NO_5050 = ['--no-reform-ends-at-arrival', '--no-attack-from-stance']
NO_5050_HELP = ('5050 off on EXE only (both knobs on by default since 5050 W74): with --exe-base, the identity against a '
                'base built before W74')
NO_4743 = ['--no-charge-any']
NO_4743_HELP = ('4743 off on EXE only (charge_any on by default again since 4743 W81): with --exe-base, the identity '
                'against a base built before W81')


def run(exe, a, att, dfn, extra, out, new_flags):
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', a.seed, '--ranged', '--no-retaliate'] + extra + new_flags
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
    lines = text.splitlines()
    tline = next((l for l in lines if l.startswith('trigger_scene:')), '')
    dline = next((l for l in lines if l.startswith('defend:')), '')
    rline = next((l for l in lines if l.startswith('ready:')), '')
    d = dict(re.findall(r'(\w+)=(\S+)', rline))
    d.update(dict(re.findall(r'(\w+)=(\S+)', dline)))
    t = dict(re.findall(r'(\w+)=(\S+)', tline))
    d['elapsed'] = t.get('elapsed', '-1')
    d['fightEnd'] = t.get('end', '-')
    d['ok'] = p.returncode == 0 and tline.startswith('trigger_scene: OK')
    d['rc'] = p.returncode
    return d, tline, text


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
    ap.add_argument('--no-defend-from-stance', action='store_true', help='4750 off: 1280 at once')
    ap.add_argument('--expect-fail', action='store_true')
    ap.add_argument('--csv', default=None)
    a = ap.parse_args()
    exe = pathlib.Path(a.exe)
    if not exe.is_file() or (a.exe_base and not pathlib.Path(a.exe_base).is_file()):
        print('missing exe')
        return 2
    flags = ['--no-defend-from-stance'] if a.no_defend_from_stance else []
    rows, breaking, failed = [], 0, 0
    for label in [c for c in a.cases.split(',') if c]:
        if label not in CASES:
            print(f'{label}: unknown case')
            return 2
        kind, att, dfn, extra, want = CASES[label]
        out = pathlib.Path(a.work) / label
        d, tline, text = run(exe, a, att, dfn, extra, out, flags)
        same = '-'
        if a.exe_base:
            bout = pathlib.Path(a.work) / 'base' / label
            bd, btline, _ = run(a.exe_base, a, att, dfn, extra, bout, [])
            try:
                logs = (out / 'triggers.log').read_bytes() == (bout / 'triggers.log').read_bytes()
            except OSError:
                logs = False
            same = 'same' if (tline == btline and logs and bd['ok']) else 'DIFF(line %s log %s)' % (tline == btline, logs)
        stances = int(d.get('stances', '-1'))
        idle = int(d.get('defendFromIdle', '-1'))
        if want:
            rule = stances > 0 and idle == 0
            breaking += idle > 0
        else:
            rule = stances == 0
        ok = d['ok'] and stances >= 0 and (a.expect_fail or rule) and same in ('-', 'same')
        failed += not ok
        d.update({'case': label, 'kind': kind, 'identity': same, 'result': 'pass' if ok else 'FAIL'})
        rows.append(d)
        print('%-4s %-12s %-4s end=%-8s %6ss stances=%s defend1280=%s fromIdle=%s osDefend=%s oneShots=%s ready=%s '
              'move=%s strike=%s death=%s other=%s %s' % (
                  label, kind, 'ok' if ok else 'FAIL', d['fightEnd'], d['elapsed'], d.get('stances'),
                  d.get('defend1280'), d.get('defendFromIdle'), d.get('osDefend'), d.get('oneShots'), d.get('osReady'),
                  d.get('osMove'), d.get('osStrike'), d.get('osDeath'), d.get('osOther'), same), flush=True)
        if not d['ok']:
            print('      ' + text.replace('\n', '\n      ')[-1200:])
    kinds = {}
    for r in rows:
        kinds.setdefault(r['kind'], []).append(r)
    print('MEANS per kind (fights; one-shots per fight by clip class; fight length s)')
    for k, rs in kinds.items():
        def m(key):
            v = [float(r.get(key, 0) or 0) for r in rs]
            return sum(v) / len(v)
        print('  %-12s n=%d oneShots=%.1f defend=%.1f ready=%.1f move=%.1f strike=%.1f death=%.1f other=%.1f '
              'stances=%.1f fromIdle=%.1f length=%.3f' % (k, len(rs), m('oneShots'), m('osDefend'), m('osReady'),
                                                          m('osMove'), m('osStrike'), m('osDeath'), m('osOther'),
                                                          m('stances'), m('defendFromIdle'), m('elapsed')))
    if a.csv and rows:
        cols = ['case', 'kind', 'result', 'identity', 'rc', 'fightEnd', 'elapsed'] + COLS
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    n = len(rows)
    if a.expect_fail:
        passed = failed == 0 and breaking > 0
        print('RESULT %s %d/%d (expect-fail: %d stance case(s) meet 1280 from idle)' % (
            'PASS' if passed else 'FAIL', n - failed, n, breaking))
    else:
        passed = failed == 0
        print('RESULT %s %d/%d' % ('PASS' if passed else 'FAIL', n - failed, n))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
