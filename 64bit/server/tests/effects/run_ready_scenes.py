"""Ticket 4746: which figures enter combat ready (1600) in a fight, against stock's rule, on the install's art
(trigger_scene.exe, built by build_trigger_scene.py; unit-combat-server.md 10.12).

    python -I server/tests/effects/run_ready_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases s-w1,...,a1,...] [--no-ready-after-move] [--charge-any] [--no-reform-first] [--no-5050]
        [--no-4743] [--no-5057] [--no-victory-word] [--exe-base BASE_TRIGGER_SCENE.EXE] [--expect-fail] [--csv OUT.csv]

Stock's rule (research/02-exe-internals/exe-audio.md 13.3 [C], the state setter): a waiting figure (state 2) requests
combat ready (1600) only when its current request, or with none its last action, is a movement (1400 run, 1450 shuffle,
1120 run charge) and its member enables Walk (1450); otherwise it makes no new request (an idle defender stays idle).
State 5 (ready to strike) always requests 1600. Route S before 4746 requested 1600 for every figure it put into Wait
(combat.cpp ready()), so its waiting defenders and attackers sent the 1600 clip's tier -1 FOOT_ALWAYS and FORTIFY_VOX
(W44's Tiny pair: about 10 fortify vox and 10 foot dirt per land fight, none on stock [L]).

The cases: 0557's 19 recorded stock fights (the melee, kill, naval-melee and city cases of run_trigger_scenes.py, the
same set-up through combat_scenes.py's side_args) and six ranged fights on trigger_scene's flat field with the unit types
of run_turn_scenes.py (archer, Gatling: ranged; field gun, artillery: siege; dreadnought on a lancer, ironclad on a
city: naval_ranged), facing their opponent, with --no-retaliate (a ranged attack's defender does not answer), the
core's defaults otherwise. trigger_scene's `ready:` line gives the 1600
requests per state, the Wait steps that break stock's rule (waitAgainstStock, per side) and the one-shot SoundStarts by
their clip's code (osReady 1600, osDefend 1280, osMove 14xx / 24xx, osCharge 1120, osStrike the other 11xx, osDeath
12xx, osVictory 18xx, osOther). A case passes when the tool exits 0 with `trigger_scene: OK` and waitAgainstStock is 0.
--expect-fail inverts the bar (the regression arm: it passes when some case breaks the rule, as on a build before 4746
or with --no-ready-after-move). --exe-base also runs BASE (a trigger_scene built from the branch's base, which has no
`ready:` line and no --no-ready-after-move) with the same arguments minus --no-ready-after-move and requires the
`trigger_scene:` line and WORK/<case>/triggers.log to be byte-identical (the knob off equals the code before it). Since
5050 W74 both 5050 knobs are on by default: against a base built before W74 add --no-5050 (EXE only, both knobs off).
Since 4743 W81 charge_any is on by default again: against a base built before W81 add --no-4743 (EXE only,
--no-charge-any). --charge-any goes to both EXE and BASE (4737's rule on in both).
Prints one row per case, the means per fight kind, and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage.
Names from the install go only into WORK (triggers.log), never into git; the CSV holds counts only. Python standard
library.
"""
import argparse
import csv
import importlib.util
import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
CASES_0557 = REPO / 'tools/harness/evidence/0557/batch-s-stock1.json'
MELEE = ['s-w1', 's-w2', 's-s1', 's-h1', 's-r1', 's-a1', 's-k1', 's-m1', 's-m2', 's-i3', 's-d1', 's-d2', 's-d3',
         's-n1', 's-n2', 's-n3', 's-c1', 's-c2', 's-c3']
# label: (kind, attacker, defender) on trigger_scene's flat field, all with --ranged --no-retaliate (a ranged attack's
# defender does not answer: the game's flag, as 18.2 / 18.14's ranged runs; run_turn_scenes.py's unit types)
RANGED = {
    'a1': ('ranged', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:25:100'),
    'g1': ('ranged', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:30:100'),
    'f1': ('siege', '0:2005:UNIT_FIELD_GUN:49:15:0:0:100', '63:2006:UNIT_RIFLEMAN:51:15:0:40:100'),
    'r1': ('siege', '0:2007:UNIT_ARTILLERY:49:15:0:0:100', '63:2008:UNIT_MACHINE_GUN:51:15:0:35:100'),
    'n2': ('naval_ranged', '0:2011:UNIT_DREADNOUGHT:49:15:0:0:100', '63:2012:UNIT_LANCER:51:15:0:30:100'),
    'n1': ('naval_ranged', '0:2009:UNIT_IRONCLAD:49:15:0:0:100', '63:city:0:51:15:-:20:200'),
}
READY = ['waitReady', 'waitAgainstStock', 'waitAgainstAtt', 'waitAgainstDef', 'readyReady', 'oneShots', 'osReady',
         'osDefend', 'osMove', 'osCharge', 'osStrike', 'osDeath', 'osVictory', 'osOther']


def load_side_args():
    spec = importlib.util.spec_from_file_location('cs4776', REPO / 'render/evidence/0042/4776/combat_scenes.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.side_args


def melee_kind(case):
    order, after = case.get('order', ''), case.get('after', '')
    if 'kind=city' in order:
        return 'city'
    if 'kind=naval' in order:
        return 'naval'
    return 'kill' if 'D=dead' in after else 'melee'


def run(exe, a, label, att, dfn, extra, out, new_flags):
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', a.seed] + extra + new_flags
    if a.charge_any:
        cmd.append('--charge-any')
    if a.no_reform_first:
        cmd.append('--no-reform-first')
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    text = (p.stdout + p.stderr).strip()
    lines = text.splitlines()
    tline = next((l for l in lines if l.startswith('trigger_scene:')), '')
    rline = next((l for l in lines if l.startswith('ready:')), '')
    d = dict(re.findall(r'(\w+)=(\S+)', rline))
    t = dict(re.findall(r'(\w+)=(\S+)', tline))
    d['elapsed'] = t.get('elapsed', '-1')
    d['fightEnd'] = t.get('end', '-')
    d['SoundStart'] = re.search(r'SoundStart=(\d+)', tline).group(1) if 'SoundStart=' in tline else '-1'
    d['ok'] = p.returncode == 0 and tline.startswith('trigger_scene: OK')
    d['rc'] = p.returncode
    return d, tline, text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--exe-base', default=None)
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    ap.add_argument('--cases', default=','.join(MELEE + list(RANGED)))
    ap.add_argument('--no-ready-after-move', action='store_true', help='4746 off: every waiting figure requests 1600')
    ap.add_argument('--charge-any', action='store_true')
    ap.add_argument('--no-reform-first', action='store_true')
    ap.add_argument('--no-5050', action='store_true',
                    help='5050 off on EXE only (--no-reform-ends-at-arrival --no-attack-from-stance; both on by default '
                         'since W74): the arm that --exe-base compares with a base built before W74')
    ap.add_argument('--no-4743', action='store_true',
                    help='4743 off on EXE only (--no-charge-any; charge_any on by default again since 4743 W81): the arm '
                         'that --exe-base compares with a base built before W81')
    ap.add_argument('--no-victory-word', action='store_true',
                    help='5059 off on EXE only (CombatParams::victory_needs_word, on by default since 5059 W100): '
                         'with --exe-base, the identity against a base built before W100')
    ap.add_argument('--no-5057', action='store_true',
                    help='5057 off on EXE only (--no-rush-ride-through --no-defender-waits; both on by default since 5057 W85): with '
                         '--exe-base, the identity against a base built before W85')
    ap.add_argument('--expect-fail', action='store_true')
    ap.add_argument('--csv', default=None)
    a = ap.parse_args()
    exe = pathlib.Path(a.exe)
    if not exe.is_file() or (a.exe_base and not pathlib.Path(a.exe_base).is_file()):
        print('missing exe')
        return 2
    side_args = load_side_args()
    rec = {c['label']: c for c in json.load(open(CASES_0557, encoding='utf-8'))}
    flags = ['--no-ready-after-move'] if a.no_ready_after_move else []
    if a.no_5050:
        flags += ['--no-reform-ends-at-arrival', '--no-attack-from-stance']
    if a.no_4743:
        if a.charge_any:
            print('--no-4743 and --charge-any exclude each other')
            return 2
        flags += ['--no-charge-any']
    if a.no_5057:   # 5057 W85: both knobs on by default; off on EXE only
        flags += ['--no-rush-ride-through', '--no-defender-waits']
    if a.no_victory_word:   # 5059 W100: on by default; off on EXE only
        flags.append('--no-victory-word')
    rows, bad_cases, failed = [], 0, 0
    for label in [c for c in a.cases.split(',') if c]:
        if label in RANGED:
            kind, att, dfn = RANGED[label]
            extra = ['--ranged', '--no-retaliate']
        elif label in rec:
            kind = melee_kind(rec[label])
            att, dfn = side_args(rec[label])
            extra = []
        else:
            print(f'{label}: unknown case')
            return 2
        out = pathlib.Path(a.work) / label
        d, tline, text = run(exe, a, label, att, dfn, extra, out, flags)
        same = '-'
        if a.exe_base:
            bout = pathlib.Path(a.work) / 'base' / label
            bd, btline, btext = run(a.exe_base, a, label, att, dfn, extra, bout, [])
            try:
                logs = (out / 'triggers.log').read_bytes() == (bout / 'triggers.log').read_bytes()
            except OSError:
                logs = False
            same = 'same' if (tline == btline and logs and bd['ok']) else 'DIFF(line %s log %s)' % (tline == btline, logs)
        against = int(d.get('waitAgainstStock', '-1'))
        breaks = against != 0
        bad_cases += breaks
        ok = d['ok'] and against >= 0 and (a.expect_fail or not breaks) and same in ('-', 'same')
        failed += not ok
        d.update({'case': label, 'kind': kind, 'identity': same, 'result': 'pass' if ok else 'FAIL'})
        rows.append(d)
        print('%-5s %-12s %-4s end=%-8s %6ss waitReady=%s against=%s (att %s def %s) readyReady=%s oneShots=%s ready=%s '
              'defend=%s move=%s charge=%s strike=%s death=%s victory=%s other=%s %s' % (
                  label, kind, 'ok' if ok else 'FAIL', d['fightEnd'], d['elapsed'], d.get('waitReady'), against,
                  d.get('waitAgainstAtt'), d.get('waitAgainstDef'), d.get('readyReady'), d.get('oneShots'),
                  d.get('osReady'), d.get('osDefend'), d.get('osMove'), d.get('osCharge'), d.get('osStrike'),
                  d.get('osDeath'), d.get('osVictory'), d.get('osOther'), same), flush=True)
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
        print('  %-12s n=%d oneShots=%.1f ready=%.1f defend=%.1f move=%.1f charge=%.1f strike=%.1f death=%.1f victory=%.1f '
              'other=%.1f waitAgainst=%.1f length=%.3f' % (k, len(rs), m('oneShots'), m('osReady'), m('osDefend'),
                                                            m('osMove'), m('osCharge'), m('osStrike'), m('osDeath'),
                                                            m('osVictory'), m('osOther'), m('waitAgainstStock'),
                                                            m('elapsed')))
    if a.csv and rows:
        cols = ['case', 'kind', 'result', 'identity', 'rc', 'fightEnd', 'elapsed', 'SoundStart'] + READY
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    n = len(rows)
    if a.expect_fail:
        passed = failed == 0 and bad_cases > 0
        print('RESULT %s %d/%d (expect-fail: %d case(s) break stock\'s rule)' % ('PASS' if passed else 'FAIL', n - failed,
                                                                                n, bad_cases))
    else:
        passed = failed == 0
        print('RESULT %s %d/%d' % ('PASS' if passed else 'FAIL', n - failed, n))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
