"""Ticket 4722: the trigger emitter on 0557's 19 recorded stock melee fights, on the install's art (trigger_scene.exe,
built by build_trigger_scene.py; unit-combat-server.md section 11).

    python -I server/tests/effects/run_trigger_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases s-w1,...] [--hitch FRAME:SECONDS] [--strict-movement] [--death-weapon own|opponent]
        [--no-tier-gate] [--no-charge-any]   (4737: the emitter's and the choreography's rules before 4737)
        [--no-turn-first]   (4735: no turn before a fight whose figures stand; 0557's melee fights do not turn)
        [--no-reform-first]   (4745 + 4747: no reform before a fight, the rule before 4745)
        [--charge-any]   (4743: 4737's charge rule on; the core's default was off from 4743 W46 and is on again since
                          4743 W81, MC #11142: --no-charge-any is the off arm)
        [--no-victory-word [--expect-fail]]   (5059: victory by the machine's 1800 alone, the rule before 5059)
        [--no-ride-strikes]   (5071: the riders strike from the run after the ride off: 5057 rule)
        [--no-type2-end-after-dying]   (5072: a type 2 fight's riders walk home in the step of the last landing)
        [--csv OUT.csv] [--effects [--effects-csv OUT.csv]]

Input: tools/harness/evidence/0557/batch-s-stock1.json (the cases: unit types, plots, hit points before and after),
read through render/evidence/0042/4776/combat_scenes.py's side_args (the same set-up as 4775 / 4776 / 4782). Per case
trigger_scene.exe writes WORK/<case>/triggers.log (names from the install: never into git) and prints its counts; the
CSV (counts only, no names) goes to --csv. A case passes when the tool exits 0: the fight ended, the log reads back,
every loop is stopped by a SoundStop or by its owner's end, no record follows its owner's end, no record of a clip
the EXE's figures own, no stop of an unknown handle, the shell's per-frame read got every record, the section's
droppedLate equals the emitter's. Prints one row per case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on
usage. Python standard library.

4793 (--effects): the tool runs with the effect routes in front of the emitter (srv_effect_routes.h) and prints a second
line `effects: OK|FAIL key=value ...`; a case then also needs `effects: OK` (every EffectStart's name defined in the
install's library, no EffectStop of a handle never started, no refused flight record). Its counts go to --effects-csv
(counts only, no names). Without --effects the runs are 4722's as before.

5059 (unit-combat-server.md 18.36): the tool's `victory:` line counts the figures that took victory and the SoundStart
records of their 1800 clips (victoryFigs, victorySounds; in the CSV). Stock takes no victory in VP, so with the victory
rule on (CombatParams::victory_needs_word, the default) a case also needs victoryFigs=0 and victorySounds=0. With
--no-victory-word (the rule before 5059) there is no victory bar; with --expect-fail as well, the run passes only when the
victory cases s-w2 and s-k1 (8 and 5 victory figures with the old rule, 18.36) show victory figures, i.e. the bar catches
the old rule.
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
CASES = REPO / 'tools/harness/evidence/0557/batch-s-stock1.json'
DEFAULT = ['s-w1', 's-w2', 's-s1', 's-h1', 's-r1', 's-a1', 's-k1', 's-m1', 's-m2', 's-i3', 's-d1', 's-d2', 's-d3',
           's-n1', 's-n2', 's-n3', 's-c1', 's-c2', 's-c3']
KEYS = ['end', 'elapsed', 'records', 'SoundStart', 'SoundStop', 'EffectStart', 'EffectMove', 'EffectStop', 'OwnerEnd',
        'loops', 'afterEnd', 'exeRecords', 'unknownHandle', 'droppedLate', 'droppedLateLasting', 'startedLate', 'exeClass',
        'exeCodes', 'noFight', 'unknownScript', 'unresolved', 'noWeapon', 'noPosition', 'victoryTail', 'loopsStarted',
        'renewed', 'heartbeat', 'loopsOwnerEnd', 'effectsEnded', 'effectsOwnerEnd', 'actions', 'transfers', 'loopLife',
        'shellRead', 'lost', 'soundTriggers', 'soundWithDuration', 'soundDurationMax', 'droppedTier',
        'victoryFigs', 'victorySounds']   # 5059
VICTORY_CASES = ('s-w2', 's-k1')   # 5059: route S drew victory here before 5059 (18.36)


def load_side_args():
    spec = importlib.util.spec_from_file_location('cs4776', REPO / 'render/evidence/0042/4776/combat_scenes.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.side_args


def parse(line):
    d = dict(re.findall(r'(\w+)=(\S+)', line))
    m = re.search(r'OwnerEnd=(\d+) \(fight (\d+), clip (\d+)\)', line)
    if m:
        d['OwnerEnd'], d['fightEnds'], d['clipEnds'] = m.groups()
    m = re.search(r'loops=(\d+) \(stop (\d+), clip end (\d+), fight end (\d+), unstopped (\d+)\)', line)
    if m:
        d['loops'], d['loopsByStop'], d['loopsByClip'], d['loopsByFight'], d['unstopped'] = m.groups()
    return d


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    ap.add_argument('--cases', default=','.join(DEFAULT))
    ap.add_argument('--hitch', default=None)
    ap.add_argument('--strict-movement', action='store_true')
    ap.add_argument('--loops-by-instance', action='store_true')
    ap.add_argument('--no-tier-gate', action='store_true')     # 4737: the emitter's rule before 4737
    ap.add_argument('--no-victory-word', action='store_true')   # 5059: victory by the machine's 1800 alone (the rule before 5059)
    ap.add_argument('--no-5057', action='store_true')          # 5057: the choreography before 5057 (both knobs off)
    ap.add_argument('--no-5061', action='store_true')          # 5061: the type 2 defender stands (the rule before 5061)
    ap.add_argument('--no-ride-strikes', action='store_true')  # 5071: the riders' strikes after the ride off (5057's rule)
    ap.add_argument('--no-type2-end-after-dying', action='store_true')  # 5072: the riders' wait for the dying off
    ap.add_argument('--no-charge-any', action='store_true')    # 4737: the choreography's approach before 4737
    ap.add_argument('--no-turn-first', action='store_true')    # 4735: the choreography before 4735
    ap.add_argument('--no-reform-first', action='store_true')  # 4745 + 4747: the choreography before 4745
    ap.add_argument('--charge-any', action='store_true')       # 4743: 4737's charge rule on (the default again since W81)
    ap.add_argument('--death-weapon', default='own')
    ap.add_argument('--csv', default=None)
    ap.add_argument('--effects', action='store_true')        # 4793
    ap.add_argument('--effects-csv', default=None)           # 4793
    ap.add_argument('--geometry', action='store_true')       # 4728: the server's FightGeometry (implies --effects)
    ap.add_argument('--expect-fail', action='store_true')    # 5059: with --no-victory-word, the victory bar must catch it
    ap.add_argument('--no-projectile-pose', action='store_true')   # 5064: with --effects / --geometry, the rule before 5064
    a = ap.parse_args()
    if a.geometry:
        a.effects = True
    exe = pathlib.Path(a.exe)
    if not exe.is_file():
        print('missing ' + str(exe))
        return 2
    side_args = load_side_args()
    cases = {c['label']: c for c in json.load(open(CASES, encoding='utf-8'))}
    want = [c for c in a.cases.split(',') if c]
    rows, passed = [], 0
    erows = []   # 4793
    vfigs_cases = 0   # 5059: victory figures over VICTORY_CASES
    for label in want:
        case = cases.get(label)
        if not case:
            print(f'{label}: not in {CASES.name}')
            return 2
        att, dfn = side_args(case)
        out = pathlib.Path(a.work) / label
        out.mkdir(parents=True, exist_ok=True)
        cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
               '--seed', a.seed, '--death-weapon', a.death_weapon]
        if a.hitch:   # 0557's cases are melee: no --ranged, as 4775 / 4776's driver
            cmd += ['--hitch', a.hitch]
        if a.strict_movement:
            cmd.append('--strict-movement')
        if a.loops_by_instance:
            cmd.append('--loops-by-instance')
        if a.no_tier_gate:
            cmd.append('--no-tier-gate')
        if a.no_5057:   # 5057 W85: the ride-through and the type 2 defender wait off (the rules before 5057)
            cmd += ['--no-rush-ride-through', '--no-defender-waits']
        if a.no_5061:   # 5061 W139: the type 2 defender's step off (the rule before 5061)
            cmd.append('--no-defender-step')
        if a.no_victory_word:   # 5059 W100: victory by the machine's 1800 alone (the rule before 5059)
            cmd.append('--no-victory-word')
        if a.no_ride_strikes:   # 5071: the riders' strikes from the run after the ride off (5057's rule)
            cmd.append('--no-ride-strikes')
        if a.no_type2_end_after_dying:   # 5072: the riders' wait for the last dying figure off (the rule before 5072)
            cmd.append('--no-type2-end-after-dying')
        if a.no_charge_any:
            cmd.append('--no-charge-any')
        if a.no_turn_first:
            cmd.append('--no-turn-first')
        if a.no_reform_first:
            cmd.append('--no-reform-first')
        if a.charge_any:
            cmd.append('--charge-any')
        if a.effects:
            cmd.append('--geometry' if a.geometry else '--effects')
            if a.no_projectile_pose:   # 5064: the held projectile's rule before 5064 (scale 1, identity)
                cmd.append('--no-projectile-pose')
        p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
        text = (p.stdout + p.stderr).strip()
        line = next((l for l in text.splitlines() if l.startswith('trigger_scene:')), '')
        d = parse(line)
        ok = p.returncode == 0 and line.startswith('trigger_scene: OK')
        if a.effects:   # 4793
            eline = next((l for l in text.splitlines() if l.startswith('effects:')), '')
            ed = dict(re.findall(r'(\w+)=(\S+)', eline))
            ed['case'] = label
            ed['effects'] = 'ok' if eline.startswith('effects: OK') and p.returncode == 0 else 'FAIL'
            if a.geometry:   # 4728: the geometry's counts in the same row
                gline = next((l for l in text.splitlines() if l.startswith('geometry:')), '')
                gd = dict(re.findall(r'(\w+)=(\S+)', gline))
                ed.update(gd)
                print('      geometry posed=%s/%s bones=%s/%s (noFigure %s noProgram %s refused %s) boneTransforms=%s plans=%s ok=%s '
                      'stepTarget=%s opponent=%s city=%s refused=%s noTangent=%s models=%s blends=%s noBone=%s' % (
                          gd.get('figurePosed'), gd.get('figureAsks'), gd.get('boneOk'), gd.get('boneAsks'), gd.get('boneNoFigure'),
                          gd.get('boneNoProgram'), gd.get('boneRefused'), gd.get('boneTransforms'), gd.get('plans'), gd.get('planOk'),
                          gd.get('planStepTarget'), gd.get('planOpponent'), gd.get('planCity'), gd.get('planRefused'),
                          gd.get('planNoTangent'), gd.get('poserModels'), gd.get('poserBlends'), gd.get('poserNoBone')), flush=True)
            pline = next((l for l in text.splitlines() if l.startswith('pose5064:')), '')
            if pline:   # 5064: held starts posed and scaled, launchers without a heading (branch builds, key on)
                print('      ' + pline, flush=True)
            erows.append(ed)
            ok = ok and ed['effects'] == 'ok'
            print('      effects %s ES=%s follows=%s Emove=%s Estop=%s undefined=%s trig=%s fallback=%s refused=%s held=%s '
                  'taken=%s expired=%s transfers=%s planned=%s hits=%s oneShot=%s flight=%s volley=%s children=%s hitEff=%s' % (
                      ed['effects'], ed.get('EffectStart'), ed.get('follows'), ed.get('EffectMove'), ed.get('EffectStop'),
                      ed.get('undefined'), ed.get('effectTriggers'), ed.get('fallback'), ed.get('refused'), ed.get('held'),
                      ed.get('heldTaken'), ed.get('heldExpired'), ed.get('transfers'), ed.get('planned'), ed.get('hits'),
                      ed.get('oneShot'), ed.get('flight'), ed.get('cityVolley'), ed.get('recChild'), ed.get('recHit')), flush=True)
        # 5059: the victory counts (the `victory:` line); bar 0 with the victory rule on
        vline = next((l for l in text.splitlines() if l.startswith('victory:')), '')
        vd = dict(re.findall(r'(\w+)=(\S+)', vline))
        d['victoryFigs'], d['victorySounds'] = vd.get('victoryFigs', '?'), vd.get('victorySounds', '?')
        if label in VICTORY_CASES and d['victoryFigs'].isdigit():
            vfigs_cases += int(d['victoryFigs'])
        if not a.no_victory_word and not (d['victoryFigs'] == '0' and d['victorySounds'] == '0'):
            ok = False
        passed += ok
        d['case'] = label
        d['recorded_s'] = case.get('run_to_end_s', '')
        d['result'] = 'pass' if ok else 'FAIL (rc %d)' % p.returncode
        rows.append(d)
        print('%-5s %-5s end=%s %ss records=%s SS=%s Sstop=%s ES=%s Emove=%s Estop=%s OE=%s/%s loops=%s (stop %s, clip %s, '
              'fight %s, unstopped %s) late=%s exe=%s[%s] unknownScript=%s unresolved=%s tail=%s life=%s victory=%s/%s' % (
                  label, d['result'][:5], d.get('end', '?'), d.get('elapsed', '?'), d.get('records', '?'),
                  d.get('SoundStart', '?'), d.get('SoundStop', '?'), d.get('EffectStart', '?'), d.get('EffectMove', '?'),
                  d.get('EffectStop', '?'), d.get('fightEnds', '?'), d.get('clipEnds', '?'), d.get('loops', '?'),
                  d.get('loopsByStop', '?'), d.get('loopsByClip', '?'), d.get('loopsByFight', '?'), d.get('unstopped', '?'),
                  d.get('droppedLate', '?'), d.get('exeClass', '?'), d.get('exeCodes', '?'), d.get('unknownScript', '?'),
                  d.get('unresolved', '?'), d.get('victoryTail', '?'), d.get('loopLife', '?'), d['victoryFigs'],
                  d['victorySounds']), flush=True)
        if not ok:
            print('      ' + text.replace('\n', '\n      ')[-1500:])
    if a.csv:
        cols = ['case', 'result', 'recorded_s'] + KEYS + ['fightEnds', 'clipEnds', 'loopsByStop', 'loopsByClip', 'loopsByFight',
                                                          'unstopped']
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
            w.writeheader()
            for r in rows:
                w.writerow(r)
    if a.effects and a.effects_csv and erows:   # 4793: counts only
        ecols = ['case', 'effects'] + [k for k in erows[0].keys() if k not in ('case', 'effects')]
        with open(a.effects_csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=ecols, extrasaction='ignore')
            w.writeheader()
            for r in erows:
                w.writerow(r)
        etot = {}
        for k in ecols[2:]:
            etot[k] = sum(int(r.get(k, 0) or 0) for r in erows if str(r.get(k, '0')).isdigit())
        print('EFFECTS TOTALS ' + ' '.join('%s=%d' % kv for kv in etot.items()))
    tot = {}
    for k in ('records', 'SoundStart', 'SoundStop', 'EffectStart', 'EffectMove', 'EffectStop', 'loops', 'unstopped',
              'droppedLate', 'droppedLateLasting', 'exeClass', 'unknownScript', 'unresolved', 'victoryTail', 'afterEnd',
              'exeRecords', 'clipEnds', 'fightEnds', 'victoryFigs', 'victorySounds'):
        tot[k] = sum(int(r.get(k, 0) or 0) for r in rows if str(r.get(k, '0')).isdigit())
    print('TOTALS ' + ' '.join('%s=%d' % kv for kv in tot.items()))
    allok = passed == len(want)
    vcases = [c for c in want if c in VICTORY_CASES]
    if a.expect_fail:   # 5059: the victory bar must catch the old rule in the victory cases
        vok = bool(vcases) and vfigs_cases > 0
        print('VICTORY expect-fail: %d victory figures over %s: %s' % (vfigs_cases, ','.join(vcases) or 'no victory case',
                                                                      'caught' if vok else 'NOT CAUGHT'))
        allok = allok and vok
    else:
        print('VICTORY bar %s: %d victory figures over %s' % ('off (--no-victory-word)' if a.no_victory_word else '0',
                                                             vfigs_cases, ','.join(vcases) or 'no victory case'))
    print('RESULT %s %d/%d' % ('PASS' if allok else 'FAIL', passed, len(want)))
    return 0 if allok else 1


if __name__ == '__main__':
    sys.exit(main())
