"""Ticket 4743: the charge fights that held to the 12 s limit (S15 on 26577625, event 4930), offline through the fight
harness (trigger_scene.exe: the server's fight clock, the core stepped live against the controller, the emitter; built
by build_trigger_scene.py; unit-combat-server.md 18.13).

    python -I server/tests/effects/run_charge_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--charge-any | --no-charge-any] [--all-held] [--no-rush-ride-through] [--no-defender-waits]
        [--no-ride-cases] [--no-defender-step] [--no-victory-word [--expect-fail]] [--no-ride-strikes] [--only-rides]
        [--csv OUT.csv]

The case: S15's fight, UNIT_SHOSHONE_PATHFINDER (6 figures, RunCharge and AttackCharge, no rush formation) at 28 -> 57
damage attacking a UNIT_BARBARIAN_WARRIOR (10 figures) at 75 -> 100 (killed), seed 1, with one long frame of 0.7 s
(`--hitch F:0.7`) at the frames where the core's charge and its end fall into one frame (W46's sweep, 2026-11-21
22:49-22:58: 14 of 91 frames held with 4737's charge on, 0 of 91 with it off), and the same fight without a long frame.
Rush cases (MC #9913 (3a)): rush members still charge (1120) with charge_any off; all of them are cavalry without
AttackCharge, so they stop in combat ready and strike 1100 after the charge. A horseman (0 -> 30) on a barbarian Warrior
(0 -> 40) and a knight (0 -> 30) on a spearman (0 -> 40), steady and with one long frame of 0.7 s at frames 12, 13, 15, 18,
21, 25, 30 and 40 (the fight starts about frame 12: the charge's start and its end); W46's sweep at every frame 2-70, 0.7 s and 0.3 s: 0 of 280 at the
limit. (A cavalry KILL of a 10-12 figure unit runs to 12 s even with steady frames, with the charge on or off: 18.2's
class, unit-combat-server.md 18.13.) Ticket 4744 (18.16): the horseman's kill of a barbarian Warrior created at 50
damage (5 of 10 figures drawn), steady, seeds 1-6 (hm-kill-s1..s6): 12.000 s in 6 of 6 while every held figure stood
(--all-held, the rule before 4744), about 8 s with the fight's standing figures the drawn ones (the default since 4744).
The knight's kill of a spearman at 60 (kn, 18.16) still runs to 12 s with the drawn figures: not in this bar.
Ticket 5057 (18.29, 18.29.1): the knight kills of
W79's live pair, offline, steady, seeds 1-3: kn-kill-s1..s3 (Knight 20 -> 46 on Spearman 60 -> 93) and knb-kill-s1..s3
(Spearman 60 -> killed), and two more rush pairs (ln-kill, a Lancer on a Pikeman; hmsp-kill, a Horseman on a Spearman,
both 20 -> 46 on 60 -> 93). With the ride-through on (CombatParams::rush_ride_through, the default; the `ride:` line)
a ride case passes when the tool exits 0, the fight ends by 8.0 s (stock's 6.90 / 7.40 s live) and no attacking figure
requested 1600 before its first hit (readyBeforeHit 0); today's route S fails it 0/6 (9.60-10.73 s, 4 entries each).
With --no-rush-ride-through (the rule before 5057) the ride cases take the old bar below. --no-ride-cases leaves them out
(the 39 cases of main before 5057). Old bar: a case passes when the tool exits 0 and the fight ended before the limit
(elapsed below 11.9 s), or (4742) its survivors'
walk home inside the fight started before it (the `walk:` line's start: every death played; the knight on a spearman
with a long frame walks into the limit, unit-combat-server.md 18.18). With 4737's charge on
and no fix (main as of 2026-11-21 e61c17d75) the Pathfinder's long-frame cases FAIL; with the charge off
(--no-charge-any; the core's default from 4743 W46 to W81) or with the fix (charge_wait, W46/W48), all PASS. The core's
default is charge_any ON again since 4743 W81 (MC #11142, unit-combat-server.md 18.28.1): the default arm runs the charge
with charge_wait, --no-charge-any the 4775 rush-only rule, --charge-any names the on arm on a build of any date. Prints
one row per case and
`RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Names from the install go only into WORK (triggers.log),
never into git; the CSV holds counts only. Python standard library.

Ticket 5059 (unit-combat-server.md 18.36): the tool's `victory:` line counts the figures that took victory and the
SoundStart records of their 1800 clips (CSV: victory_figs, victory_sounds). Stock takes no victory in VP, so with the
victory rule on (CombatParams::victory_needs_word, the default) every case also needs 0 and 0. --no-victory-word (the
rule before 5059) drops that bar; with --expect-fail as well the run passes only when the victory cases (knb-kill-s*,
hm-kill-s*: 1-2 and 0-2 victory figures per seed with the old rule, 18.36) show victory figures, i.e. the bar catches
the old rule.

Ticket 5071 (unit-combat-server.md 18.46): stock's riders strike from their run (1140) after the ride while the defender
has a need open, and the ride leaves the defender's last planned death to such a strike
(CombatParams::ride_strikes_while_held, the default). With it on, every ride case is run a second time with the rule off
(--no-ride-strikes, into <case>-off) and passes only with the same deaths of each side (the `held:` line's attDeaths and
defDeaths) and a fight at most HELD_LONGER_S longer; a case of HELD_1140_CASES (a rider with a 1140 clip) also needs at
least HELD_STRIKES_MIN hits of 1140 by the attacker (att1140; 5057 alone gives 0, and a build before 5071 prints no
`held:` line, so the bar fails on the old code). --no-ride-strikes drops that bar (and runs the core's off arm);
--only-rides runs the 12 ride cases alone.

Ticket 5072 (unit-combat-server.md 18.48): stock ends no rider of a type 2 fight while a figure of the fight is dying,
so the riders walk home after the last death and their run stop at home (1440) comes after the end
(CombatParams::type2_end_after_dying, the default). With it on, every ride case is also run with the rule off
(--no-type2-end-after-dying, into <case>-tail-off) and passes only when the tool's `tail:` line shows the attacker's
first finish step at or after the fight's last removal (attRelease >= lastRemoved), no home stop before the end
(homeBeforeEnd 0) and the same deaths of each side as the off arm; a build before 5072 prints no `tail:` line, so the
bar fails on the old code. --no-type2-end-after-dying drops that bar (and runs the core's off arm).
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys

ATTACKER = '2:1041:UNIT_SHOSHONE_PATHFINDER:10:10:28:57:100'
DEFENDER = '63:1045:UNIT_BARBARIAN_WARRIOR:11:10:75:100:100'
HITCH_FRAMES = [28, 29, 34, 35, 36, 37, 57, 58, 59, 60, 61, 62, 63, 64]
RUSH = [('hm-bw', '0:1001:UNIT_HORSEMAN:10:10:0:30:100', '63:1002:UNIT_BARBARIAN_WARRIOR:11:10:0:40:100'),
        ('kn-sp', '0:1001:UNIT_KNIGHT:10:10:0:30:100', '63:1002:UNIT_SPEARMAN:11:10:0:40:100')]
RUSH_FRAMES = [12, 13, 15, 18, 21, 25, 30, 40]   # the fight starts about frame 12
# 4744: a cavalry kill of a unit created damaged (unit-combat-server.md 18.16), steady, each seed
KILLS = [('hm-kill', '0:1001:UNIT_HORSEMAN:10:10:0:40:100', '63:1002:UNIT_BARBARIAN_WARRIOR:11:10:50:100:100')]
KILL_SEEDS = ['1', '2', '3', '4', '5', '6']
LIMIT_BELOW = 11.9
# 5057: the knight kills of W79's live pair (18.29) and two more rush pairs, steady, seeds 1-3 (the ride bar)
RIDES = [('kn-kill', '0:1001:UNIT_KNIGHT:10:10:20:46:100', '63:1002:UNIT_SPEARMAN:11:10:60:93:100'),
         ('knb-kill', '0:1001:UNIT_KNIGHT:10:10:20:31:100', '63:1002:UNIT_SPEARMAN:11:10:60:100:100'),
         ('ln-kill', '0:1001:UNIT_LANCER:10:10:20:46:100', '63:1002:UNIT_PIKEMAN:11:10:60:93:100'),
         ('hmsp-kill', '0:1001:UNIT_HORSEMAN:10:10:20:46:100', '63:1002:UNIT_SPEARMAN:11:10:60:93:100')]
RIDE_SEEDS = ['1', '2', '3']
RIDE_END = 8.0
HELD_STRIKES_MIN = 1   # 5071: 1140 hits by the attacker per ride case of HELD_1140_CASES (5057 alone: 0)
HELD_1140_CASES = ('kn-kill-s', 'knb-kill-s', 'ln-kill-s', 'hmsp-kill-s')   # 5071: riders with a 1140 clip (all four)
HELD_LONGER_S = 1.0    # 5071: the fight with the rule on is at most this much longer than with it off (lead-0042)
VICTORY_CASES = ('knb-kill-s', 'hm-kill-s')   # 5059: route S drew victory here before 5059 (18.36)


def held(text):
    """5071: (attStrikeHits, att1140, (attDeaths, defDeaths)) from the tool's `held:` line; (-1, -1, None) without one"""
    hm = re.search(r'^held: .*\battStrikeHits=(\d+) att1140=(\d+) .*\battDeaths=(\d+) defDeaths=(\d+)', text, re.M)
    if not hm:
        return -1, -1, None
    return int(hm.group(1)), int(hm.group(2)), (int(hm.group(3)), int(hm.group(4)))


def tail(text):
    """5072: (attRelease, lastRemoved, homeBeforeEnd, attStopSounds, (attDeaths, defDeaths)) from the tool's `tail:`
    line; None without one"""
    tm = re.search(r'^tail: .*\battRelease=(\S+) lastRemoved=(\S+) homeStops=\d+ homeBeforeEnd=(\d+) runStops=\d+ '
                   r'attStopSounds=(\d+) attDeaths=(\d+) defDeaths=(\d+)', text, re.M)
    if not tm:
        return None
    return (float(tm.group(1)), float(tm.group(2)), int(tm.group(3)), int(tm.group(4)),
            (int(tm.group(5)), int(tm.group(6))))


def cases(seed, rides=True):
    """(name, attacker, defender, hitch or None, seed)"""
    out = [('pf-bw-steady', ATTACKER, DEFENDER, None, seed)]
    out += [('pf-bw-h%d' % f, ATTACKER, DEFENDER, '%d:0.7' % f, seed) for f in HITCH_FRAMES]
    for name, att, dfn in RUSH:
        out.append((name + '-steady', att, dfn, None, seed))
        out += [('%s-h%d' % (name, f), att, dfn, '%d:0.7' % f, seed) for f in RUSH_FRAMES]
    for name, att, dfn in KILLS:
        out += [('%s-s%s' % (name, s), att, dfn, None, s) for s in KILL_SEEDS]
    if rides:   # 5057
        for name, att, dfn in RIDES:
            out += [('%s-s%s' % (name, s), att, dfn, None, s) for s in RIDE_SEEDS]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--game', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--seed', default='1')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--charge-any', action='store_true')
    g.add_argument('--no-charge-any', action='store_true')
    ap.add_argument('--all-held', action='store_true', help='4744: every held figure stands (the rule before 4744)')
    ap.add_argument('--no-reform-first', action='store_true', help='4745 + 4747: no reform before a fight (the rule before 4745)')
    ap.add_argument('--no-rush-ride-through', action='store_true', help='5057 (A) off: the rule before 5057')
    ap.add_argument('--no-defender-waits', action='store_true', help='5057 (B) off: the rule before 5057')
    ap.add_argument('--no-ride-cases', action='store_true', help='5057: leave the ride cases out (main before 5057)')
    ap.add_argument('--no-defender-step', action='store_true', help='5061 off: the type 2 defender stands (the rule before 5061)')
    ap.add_argument('--no-ride-strikes', action='store_true',
                    help='5071 off: the riders stand after the ride once no need is open (5057 rule); drops the held bar')
    ap.add_argument('--only-rides', action='store_true', help='5071: run the ride cases only')
    ap.add_argument('--no-type2-end-after-dying', action='store_true',
                    help='5072 off: the riders walk home in the step of the last landing (the rule before 5072); '
                         'drops the tail bar')
    ap.add_argument('--no-victory-word', action='store_true', help='5059 off: victory by the 1800 of the machine alone (the rule before 5059)')
    ap.add_argument('--expect-fail', action='store_true',
                    help='5059: with --no-victory-word, pass only when the victory cases show victory (the bar catches it)')
    ap.add_argument('--csv', default=None)
    a = ap.parse_args()
    work = pathlib.Path(a.work)
    rows, ok = [], 0
    vfigs_cases, vcases = 0, []   # 5059
    ride_names = tuple(n + '-s' for n, _, _ in RIDES)
    for name, att, dfn, hitch, seed in cases(a.seed, not a.no_ride_cases):
        if a.only_rides and not name.startswith(ride_names):   # 5071
            continue
        out = work / name
        out.mkdir(parents=True, exist_ok=True)
        cmd = [a.exe, '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
               '--seed', seed]
        if hitch:
            cmd += ['--hitch', hitch]
        if a.charge_any:
            cmd.append('--charge-any')
        if a.no_charge_any:
            cmd.append('--no-charge-any')
        if a.all_held:
            cmd.append('--all-held')
        if a.no_reform_first:
            cmd.append('--no-reform-first')
        if a.no_rush_ride_through:
            cmd.append('--no-rush-ride-through')
        if a.no_defender_waits:
            cmd.append('--no-defender-waits')
        if a.no_defender_step:   # 5061
            cmd.append('--no-defender-step')
        if a.no_victory_word:   # 5059
            cmd.append('--no-victory-word')
        if a.no_ride_strikes:   # 5071
            cmd.append('--no-ride-strikes')
        if a.no_type2_end_after_dying:   # 5072
            cmd.append('--no-type2-end-after-dying')
        p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
        text = (p.stdout + p.stderr).strip()
        line = next((l for l in text.splitlines() if l.startswith('trigger_scene:')), text[-300:])
        m = re.search(r'end=(\S+) elapsed=(\S+)', line)
        end, elapsed = (m.group(1), float(m.group(2))) if m else ('-', -1.0)
        # 4742: with the walk home inside the fight (CombatParams::walk_home) a long fight's walk may reach the limit; the
        # fight itself (every death played, the hold this bar looks for absent) ended when the walk started
        wm = re.search(r'^walk: .*\bstart=(\S+)', text, re.M)
        walk = float(wm.group(1)) if wm else -1.0
        passed = p.returncode == 0 and (0 <= elapsed < LIMIT_BELOW or 0 <= walk < LIMIT_BELOW)
        # 5057: the ride bar (the `ride:` line) for the ride cases while the ride-through is on
        rm = re.search(r'^ride: .*\breadyBeforeHit=(\d+)', text, re.M)
        before = int(rm.group(1)) if rm else -1
        bar = 'old'
        # 5071: the riders' strikes after the ride (the `held:` line; a build before 5071 prints none: -1)
        strikes, s1140, deaths = held(text)
        off_deaths, off_elapsed = None, -1.0
        if name.startswith(ride_names) and not a.no_rush_ride_through:
            bar = 'ride'
            passed = p.returncode == 0 and 0 <= elapsed <= RIDE_END and before == 0
            if not a.no_ride_strikes:
                # the same case with the rule off: equal deaths, and a fight at most HELD_LONGER_S longer
                bar = 'held'
                off = work / (name + '-off')
                off.mkdir(parents=True, exist_ok=True)
                q = subprocess.run([str(off) if x == str(out) else x for x in cmd] + ['--no-ride-strikes'],
                                   capture_output=True, text=True, errors='replace')
                qtext = (q.stdout + q.stderr).strip()
                qm = re.search(r'^trigger_scene: .*\belapsed=(\S+)', qtext, re.M)
                off_elapsed = float(qm.group(1)) if qm else -1.0
                off_deaths = held(qtext)[2]
                need = HELD_STRIKES_MIN if name.startswith(HELD_1140_CASES) else 0
                passed = (passed and q.returncode == 0 and s1140 >= need and deaths is not None and
                          deaths == off_deaths and 0 <= off_elapsed and elapsed <= off_elapsed + HELD_LONGER_S)
        # 5072: the riders' tail (the `tail:` line; a build before 5072 prints none)
        tl = tail(text)
        tail_off = None
        if name.startswith(ride_names) and not a.no_rush_ride_through and not a.no_type2_end_after_dying:
            toff = work / (name + '-tail-off')
            toff.mkdir(parents=True, exist_ok=True)
            r = subprocess.run([str(toff) if x == str(out) else x for x in cmd] + ['--no-type2-end-after-dying'],
                               capture_output=True, text=True, errors='replace')
            tail_off = tail((r.stdout + r.stderr).strip())
            passed = (passed and r.returncode == 0 and tl is not None and tail_off is not None and
                      tl[0] + 1e-6 >= tl[1] and tl[2] == 0 and tl[4] == tail_off[4])
        # 5059: the victory counts (the `victory:` line); bar 0 with the victory rule on
        vm = re.search(r'^victory: .*\bvictoryFigs=(\d+) .*\bvictorySounds=(\d+)', text, re.M)
        vfigs, vsounds = (int(vm.group(1)), int(vm.group(2))) if vm else (-1, -1)
        if name.startswith(VICTORY_CASES):
            vcases.append(name)
            vfigs_cases += max(vfigs, 0)
        if not a.no_victory_word and not (vfigs == 0 and vsounds == 0):
            passed = False
        ok += passed
        rows.append({'case': name, 'hitch': hitch or '-', 'rc': p.returncode, 'end': end, 'elapsed': elapsed,
                     'walk_start': walk, 'ready_before_hit': before, 'bar': bar, 'victory_figs': vfigs,
                     'victory_sounds': vsounds, 'att_strike_hits': strikes, 'att_1140': s1140,
                     'deaths': '-' if deaths is None else '%d/%d' % deaths,
                     'off_deaths': '-' if off_deaths is None else '%d/%d' % off_deaths, 'off_elapsed': off_elapsed,
                     'tail': '-' if tl is None else 'rel %.3f rem %.3f homeBefore %d stopSounds %d' % tl[:4],
                     'tail_off': '-' if tail_off is None else 'rel %.3f rem %.3f homeBefore %d stopSounds %d' % tail_off[:4],
                     'result': 'PASS' if passed else 'FAIL'})
        print('%-14s hitch %-7s rc %d end %-8s elapsed %6.3f walk %6.3f before %2d %-4s victory %2d/%2d strikes %2d '
              '1140 %2d deaths %s off %s %6.3f tail %s off %s  %s%s' % (
                  name, hitch or '-', p.returncode, end, elapsed, walk, before, bar, vfigs, vsounds, strikes, s1140,
                  '-' if deaths is None else '%d/%d' % deaths, '-' if off_deaths is None else '%d/%d' % off_deaths,
                  off_elapsed, '-' if tl is None else '%.3f/%.3f/%d/%d' % tl[:4],
                  '-' if tail_off is None else '%.3f/%.3f/%d/%d' % tail_off[:4],
                  'PASS' if passed else 'FAIL', '' if m else '  (' + line[:200] + ')'))
    n = len(rows)
    allok = ok == n
    if a.expect_fail:   # 5059: the victory bar must catch the old rule in the victory cases
        vok = bool(vcases) and vfigs_cases > 0
        print('VICTORY expect-fail: %d victory figures over %d victory cases: %s' % (vfigs_cases, len(vcases),
                                                                                  'caught' if vok else 'NOT CAUGHT'))
        allok = allok and vok
    else:
        print('VICTORY bar %s: %d victory figures over %d victory cases' % (
            'off (--no-victory-word)' if a.no_victory_word else '0', vfigs_cases, len(vcases)))
    print('RESULT %s %d/%d' % ('PASS' if allok else 'FAIL', ok, n))
    if a.csv:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
    return 0 if allok else 1


if __name__ == '__main__':
    sys.exit(main())
