"""Tickets 5052, 5055, 5058 and 5062: a hit target's sound names by stock's rule, and a city's own projectile, on the
install's art (trigger_scene.exe, built by build_trigger_scene.py; unit-combat-server.md 18.25, 18.25.1, 18.30, 18.34, 18.37).

    python -I server/tests/effects/run_names_scenes.py --exe TRIGGER_SCENE.EXE --game INSTALL --db DB --work DIR
        [--seed N] [--cases aw,...] [--expect-fail] [--exe-base BASE_TRIGGER_SCENE.EXE [--no-5050] [--no-4743] [--no-5057] [--no-victory-word]] [--csv OUT.csv]

The two knobs (TriggerStreamConfig, trigger_scene flags; srv_trigger_emitter.h's rules, the same code the server runs):
  (W) strikerWeapon (--striker-weapon): a defender that does not strike back (DefenderStrikes false: a ranged attack
      without retaliation) takes the attacker's figure-0 weapon for $(WEAPON), so its impact sounds are named by the
      weapon that hits it (stock: IMPACT_ARROW, IMPACT_BULLETHC), not by its own (route S before 5052: IMPACT_BLUNT,
      IMPACT_BULLET).
  (T) plotTerrain (--trigger-terrain, with --plot X:Y:TERRAIN_TYPE:FEATURE_TYPE): $(TERRAIN) from the unit's plot (the
      feature's EffectTypeTag, else the terrain's, else DIRT), so a fall on a forest plot is BODY_FALL_LEAVES (stock), not
      BODY_FALL_DIRT.
  (5055) strikerMelee (--striker-melee, on by default with the striker weapon; --no-striker-melee is 5052's rule): a
      Sound's $(WEAPON) is the striker's for every struck unit, so both sides of a melee are named by the other's weapon
      (stock, 18.25.2: the Warrior's hits on a Spearman IMPACT_SPEAR, the Spearman's IMPACT_BLUNT), with the unit's own
      weapon as the second try when that name is no script.
  (5058) cityEra (--city-era ERA, on by default with the striker weapon; --no-city-era is the rule before 5058): a Sound's
      $(WEAPON) that a city strikes is the city's weapon by its owner's era, stock's selector (18.34): era 0 CITY_EARLY,
      1 CITY_MID, 2 CITY_LATE, 3 and up CITY_EARLY; without --city-era the era is not known and the unit keeps its own.
trigger_scene's `names:` line gives the knobs, the counts, and the tags the check needs: attSound / defSound (each side's
figure-0 sound tag) and defPlot (the defender's plot tag). The check reads WORK/<case>/triggers.log: the defender's
SoundStart names (N lines by index) that hold `_IMPACT_` (its hit reactions) and `BODY_FALL` / `_FOOT` (its falls and
feet).

Regression cases (flat field; the defender on a forest plot, the attacker on grassland; damage high enough for falls):
  aw  (W) an archer shoots a warrior (--ranged --no-retaliate): at least one defender impact, every one ends with
      _<attSound>, none with _<defSound> (attSound != defSound).
  gw  (W) a Gatling gun shoots a rifleman: the same.
  at  (T) the archer on the warrior: at least one defender fall or foot name, every one ends with _<defPlot>, and defPlot
      is not DIRT (a forest: LEAVES).
  gt  (T) the Gatling gun on the rifleman: the same.
  dw  (W) default arm (W76, MC #10847: the striker weapon is ON by default): aw with NO 5052 flag: the same rule.
  dg  (W) default arm: gw with no 5052 flag: the same rule.
  dta (T) default arm (W89, 5052 part D, MC #11356: the terrain is ON by default too): at with NO 5052 flag: the same
      rule.
  dtg (T) default arm: gt with no 5052 flag: the same rule.
  mc  control, both knobs on with 5055 off (--no-striker-melee): a spearman attacks a warrior (melee, the defender
      strikes back) on grassland: the triggers.log is byte-identical to the knobs-off run (5052's rule keeps melee's own
      names; grassland's tag is DIRT). Not run with --expect-fail.
  ms  (5055) default arm (no flag): the same spearman on the warrior: at least one impact on each side, the warrior's all
      end with _<attSound> (SPEAR) and none with _<defSound>, the spearman's all with _<defSound> (BLUNT) and none with
      _<attSound>, and the triggers.log differs from the --no-striker-melee run (5052's names).
  ce0, ce1, ce2, ce3  (5058) a swordsman attacks a city that strikes back, the city's owner in era 0 / 1 / 2 / 3
      (--city-era): at least one attacker impact, every one ends with _CITY_EARLY / _CITY_MID / _CITY_LATE / _CITY_EARLY,
      and the triggers.log differs from the --no-city-era run (the swordsman's own names).
  cu  (5058) control: the same fight without --city-era (the era not known): the triggers.log is byte-identical to the
      --no-city-era run and no attacker impact names a CITY weapon. Not run with --expect-fail.
  cs1, cs3  (5062) a city of an owner in era 1 / 3 strikes a warrior on a forest plot (--ranged --no-retaliate --effects,
      trigger_scene's city attacker): the `cityshot:` line names CITY_BOMBARD_EARLY / CITY_BOMBARD_MID (Eras.
      CityBombardEffectTag), the triggers.log starts that flight and at least one ProjectileEnd child (EARLY:
      PROJ_ARROW_IMPACT; MID: CATAPULT_ROCK_IMPACT_<the target's surface>, every one ending with _<defPlot>), and the
      --no-city-shot run names no CITY_BOMBARD flight and differs. --expect-fail runs them with --no-city-shot too.
--expect-fail is the regression arm (the explicit 0 arm: --no-striker-weapon --no-trigger-terrain, since the striker
--expect-fail is the regression arm (the explicit 0 arm: --no-striker-weapon --no-trigger-terrain, since both knobs
are on by default: the striker weapon since MC #10847, the terrain since MC #11356): the cases (not mc) run with both
knobs off (the rule before 5052) and the arm passes when EVERY case breaks its expectation. --exe-base also runs BASE
(since 5062 W102: a trigger_scene built from main before 5062, 5058 and 5055 in) with each case's own flags and requires
EXE's run of the same flags plus --no-city-shot (5062 off) AND EXE's own run (in --expect-fail the knobs-off run) to give
BASE's `trigger_scene:` line and WORK/<case>/triggers.log byte for byte: 5062 names only a city's own strike, so every
case without a city attacker is unchanged (ce0-ce3 too: the city's answer in a melee is 5063's). For cs1 / cs3 BASE must
refuse the scene (exit 2: the code before 5062 takes no city attacker). From W92 to W102 BASE was a build before 5058
(EXE ran with --no-city-era; ce0-ce3 had to differ from BASE, cu to equal it). Before W92 BASE was a build with 5052 (since
W88 W89's 5052 part D) and without 5055, and EXE ran with --no-striker-melee (W83); before W83 a build without 5052 with
the flags stripped; a base from main has failed that arm since 5052 (W) went default on (W81, 2026-11-23).

Prints one row per case and `RESULT PASS|FAIL n/m`; exit 0 on PASS, 1 on FAIL, 2 on usage. Names from the install go
only into WORK, never into git; the CSV holds counts and the unit types' names only. Python standard library.
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys

RANGED = ['--ranged', '--no-retaliate']
FOREST = ['--plot', '49:15:TERRAIN_GRASS:-', '--plot', '51:15:TERRAIN_GRASS:FEATURE_FOREST']
GRASS = ['--plot', '50:15:TERRAIN_GRASS:-', '--plot', '51:15:TERRAIN_GRASS:-']
# label: (knob, attacker, defender, extra trigger_scene arguments)
CASES = {
    'aw': ('w', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:60:100', RANGED + FOREST),
    'gw': ('w', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:60:100', RANGED + FOREST),
    'at': ('t', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:60:100', RANGED + FOREST),
    'gt': ('t', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:60:100', RANGED + FOREST),
    'dw': ('dw', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:60:100', RANGED + FOREST),
    'dg': ('dw', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:60:100', RANGED + FOREST),
    'dta': ('dt', '0:2003:UNIT_ARCHER:49:15:0:0:100', '63:2004:UNIT_WARRIOR:51:15:0:60:100', RANGED + FOREST),
    'dtg': ('dt', '0:2001:UNIT_GATLINGGUN:49:15:0:0:100', '63:2002:UNIT_RIFLEMAN:51:15:0:60:100', RANGED + FOREST),
    'mc': ('c', '0:2005:UNIT_SPEARMAN:50:15:0:20:100', '63:2006:UNIT_WARRIOR:51:15:0:60:100', GRASS),
    'ms': ('m', '0:2005:UNIT_SPEARMAN:50:15:0:20:100', '63:2006:UNIT_WARRIOR:51:15:0:60:100', GRASS),
    'ce0': ('e0', '0:2007:UNIT_SWORDSMAN:50:15:0:20:100', '63:city:0:51:15:-:20:200', ['--city-era', '0']),
    'ce1': ('e1', '0:2007:UNIT_SWORDSMAN:50:15:0:20:100', '63:city:0:51:15:-:20:200', ['--city-era', '1']),
    'ce2': ('e2', '0:2007:UNIT_SWORDSMAN:50:15:0:20:100', '63:city:0:51:15:-:20:200', ['--city-era', '2']),
    'ce3': ('e3', '0:2007:UNIT_SWORDSMAN:50:15:0:20:100', '63:city:0:51:15:-:20:200', ['--city-era', '3']),
    'cu': ('u', '0:2007:UNIT_SWORDSMAN:50:15:0:20:100', '63:city:0:51:15:-:20:200', []),
    # 5062: a city (player 0, era 1 / 3) strikes a warrior on a forest plot, with the effect routes
    'cs1': ('s1', '0:city:0:49:15:-:0:200', '63:2004:UNIT_WARRIOR:51:15:0:60:100', RANGED + FOREST + ['--city-era', '1', '--effects']),
    'cs3': ('s3', '0:city:0:49:15:-:0:200', '63:2004:UNIT_WARRIOR:51:15:0:60:100', RANGED + FOREST + ['--city-era', '3', '--effects']),
}
# 'dw' / 'dt': the default arms (no 5052 flag: trigger_scene's default = the product's, striker weapon on since MC #10847,
# terrain on since MC #11356); 'm': 5055's default arm
KNOBS = {'w': ['--striker-weapon'], 't': ['--trigger-terrain'], 'c': ['--striker-weapon', '--trigger-terrain', '--no-striker-melee'],
         'dw': [], 'dt': [], 'm': [], 'e0': [], 'e1': [], 'e2': [], 'e3': [], 'u': [], 's1': [], 's3': []}
CITY_TAG = {'e0': 'CITY_EARLY', 'e1': 'CITY_MID', 'e2': 'CITY_LATE', 'e3': 'CITY_EARLY'}   # 5058: stock's selector (18.34)
NO_5058 = ['--no-city-era']   # 5058 off: the rule before 5058 exactly
# 5062: the flight by Eras.CityBombardEffectTag's bucket (VP: eras 0-1 EARLY, 2-4 MID, 5-7 LATE; 18.37) and the stem of its
# ProjectileEnd child (EARLY: no surface variant; MID: by the target's surface)
SHOT_TAG = {'s1': ('CITY_BOMBARD_EARLY', 'PROJ_ARROW_IMPACT'), 's3': ('CITY_BOMBARD_MID', 'CATAPULT_ROCK_IMPACT_')}
NO_5062 = ['--no-city-shot']   # 5062 off: the rule before 5062 exactly
OFF = ['--no-striker-weapon', '--no-trigger-terrain']
NO_5055 = ['--no-striker-melee']   # 5055 off: 5052's rule exactly
NEW_FLAGS = {'--striker-weapon', '--no-striker-weapon', '--trigger-terrain', '--no-trigger-terrain'}


def strip_new(args):
    """The arguments a trigger_scene without 5052 takes: no 5052 flag, no --plot pair."""
    out, skip = [], False
    for x in args:
        if skip:
            skip = False
            continue
        if x == '--plot':
            skip = True
            continue
        if x in NEW_FLAGS:
            continue
        out.append(x)
    return out


def strip_5058(args):
    """The arguments a trigger_scene without 5058 takes: no --city-era ERA, no --no-city-era."""
    out, skip = [], False
    for x in args:
        if skip:
            skip = False
            continue
        if x == '--city-era':
            skip = True
            continue
        if x == '--no-city-era':
            continue
        out.append(x)
    return out


def defender_names(log_bytes, player, unit):
    """The defender's SoundStart names, in record order, from a trigger log (c5r/unitanim/trigger_log.h)."""
    names, out = {}, []
    for line in log_bytes.decode('utf-8', 'replace').splitlines():
        if line.startswith('N '):
            parts = line.split(' ', 3)
            if len(parts) == 4:
                names[parts[1]] = parts[3]
        elif line.startswith('R '):
            kv = dict(re.findall(r'(\w+)=(\S+)', line))
            if kv.get('kind') == 'SoundStart' and kv.get('player') == str(player) and kv.get('unit') == str(unit):
                out.append(names.get(kv.get('name'), ''))
    return out


NO_5050 = ['--no-reform-ends-at-arrival', '--no-attack-from-stance']
NO_5050_HELP = ('5050 off on EXE only (both knobs on by default since 5050 W74): with --exe-base, the identity against a '
                'base built before W74')
NO_4743 = ['--no-charge-any']
NO_4743_HELP = ('4743 off on EXE only (charge_any on by default again since 4743 W81): with --exe-base, the identity '
                'against a base built before W81')


def run_one(a, exe, out, att, dfn, extra, flags):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--game', a.game, '--db', a.db, '--out', str(out), '--attacker', att, '--defender', dfn,
           '--seed', a.seed] + list(extra) + list(flags)
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
    d = dict(re.findall(r'(\w+)=(\S+)', next((x for x in lines if x.startswith('names:')), '')))
    tline = next((x for x in lines if x.startswith('trigger_scene:')), '')
    d['line'] = tline
    d['rc'] = p.returncode
    d['ok'] = p.returncode == 0 and tline.startswith('trigger_scene: OK')
    d.update({'cityShot' + k: v for k, v in re.findall(r'(\w+)=(\S+)', next((x for x in lines if x.startswith('cityshot:')), ''))})
    d.update({'cityEra' + k: v for k, v in re.findall(r'(\w+)=(\S+)', next((x for x in lines if x.startswith('cityera:')), ''))})
    d['text'] = text
    log = out / 'triggers.log'
    d['log'] = log.read_bytes() if log.is_file() else b''
    dp, du = dfn.split(':')[0], dfn.split(':')[1]
    sn = defender_names(d['log'], dp, du)
    d['impacts'] = [n for n in sn if '_IMPACT_' in n]
    d['grounds'] = [n for n in sn if 'BODY_FALL' in n or '_FOOT' in n]
    ap, au = att.split(':')[0], att.split(':')[1]   # 5055: the attacker's own impacts (it is struck back in a melee)
    d['att_impacts'] = [n for n in defender_names(d['log'], ap, au) if '_IMPACT_' in n]
    d['effects'] = effect_names(d['log'])   # 5062: every EffectStart's name, in record order
    return d


def effect_names(log_bytes):
    """5062: every EffectStart's name, in record order, from a trigger log (any owner: a city's flight is its target's)."""
    names, out = {}, []
    for line in log_bytes.decode('utf-8', 'replace').splitlines():
        if line.startswith('N '):
            parts = line.split(' ', 3)
            if len(parts) == 4:
                names[parts[1]] = parts[3]
        elif line.startswith('R '):
            kv = dict(re.findall(r'(\w+)=(\S+)', line))
            if kv.get('kind') == 'EffectStart':
                out.append(names.get(kv.get('name'), ''))
    return out


def meets(knob, d, off=None):
    att, own, plot = d.get('attSound', '-'), d.get('defSound', '-'), d.get('defPlot', '-')
    if knob in ('w', 'dw'):
        imp = d['impacts']
        return (att not in ('-', own) and len(imp) > 0 and all(n.endswith('_' + att) for n in imp) and
                not any(n.endswith('_' + own) for n in imp))
    if knob in ('t', 'dt'):
        g = d['grounds']
        return plot not in ('-', 'DIRT') and len(g) > 0 and all(n.endswith('_' + plot) for n in g)
    if knob == 'm':   # 5055: each side of the melee by the other's weapon; the log differs from 5055 off
        di, ai = d['impacts'], d['att_impacts']
        return (att not in ('-', own) and len(di) > 0 and len(ai) > 0 and
                all(n.endswith('_' + att) for n in di) and not any(n.endswith('_' + own) for n in di) and
                all(n.endswith('_' + own) for n in ai) and not any(n.endswith('_' + att) for n in ai) and
                (off is None or (off['ok'] and d['log'] != off['log'])))
    if knob in CITY_TAG:   # 5058: the attacker struck by the city named by the city's era; the log differs from 5058 off
        ai = d['att_impacts']
        return (len(ai) > 0 and all(n.endswith('_' + CITY_TAG[knob]) for n in ai) and
                (off is None or (off['ok'] and d['log'] != off['log'])))
    if knob in SHOT_TAG:   # 5062: the city's own strike flies its era's projectile and starts its child at the landing
        flight, child = SHOT_TAG[knob]
        fx = d['effects']
        kids = [n for n in fx if child in n]
        return (d.get('cityShoteffect') == flight and any(n.endswith(flight) for n in fx) and len(kids) > 0 and
                (knob != 's3' or all(n.endswith('_' + d.get('defPlot', '?')) for n in kids)) and
                (off is None or (off['ok'] and d['log'] != off['log'] and not any('CITY_BOMBARD' in n for n in off['effects']))))
    if knob == 'u':   # 5058 control: the era not known = 5058 off, byte for byte
        return (off is not None and off['ok'] and d['log'] == off['log'] and len(d['log']) > 0 and
                not any('_CITY_' in n for n in d['att_impacts']))
    # the melee control: knobs on = knobs off, byte for byte
    return off is not None and off['ok'] and d['log'] == off['log'] and len(d['log']) > 0


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
    ap.add_argument('--expect-fail', action='store_true')
    ap.add_argument('--csv', default=None)
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
    if a.expect_fail:
        want = [c for c in want if CASES[c][0] not in ('c', 'u')]
    work = pathlib.Path(a.work)
    rows, failed, breaking = [], 0, 0
    for label in want:
        knob, att, dfn, extra = CASES[label]
        if a.expect_fail:   # 5062's cases: with 5062 off too (the rule before 5062)
            d = run_one(a, a.exe, work / 'off' / label, att, dfn, extra, OFF + (NO_5062 if knob in SHOT_TAG else []))
            off = d
        else:
            d = run_one(a, a.exe, work / 'on' / label, att, dfn, extra, KNOBS[knob])
            off = None
            if knob == 'c':
                off = run_one(a, a.exe, work / 'off' / label, att, dfn, extra, OFF)
            elif knob == 'm':   # 5055 off: 5052's names
                off = run_one(a, a.exe, work / 'no5055' / label, att, dfn, extra, KNOBS[knob] + NO_5055)
            elif knob in CITY_TAG or knob == 'u':   # 5058 off: the names before 5058
                off = run_one(a, a.exe, work / 'no5058' / label, att, dfn, extra, KNOBS[knob] + NO_5058)
            elif knob in SHOT_TAG:   # 5062 off: no projectile named
                off = run_one(a, a.exe, work / 'no5062' / label, att, dfn, extra, KNOBS[knob] + NO_5062)
        rule = meets(knob, d, off)
        same = '-'
        if a.exe_base:
            # since W102 (5062): BASE is main before 5062 (5058 and 5055 in) with the case's flags; EXE with 5062 off must
            # give its `trigger_scene:` line and triggers.log byte for byte, and EXE's default run too for every case
            # without a city attacker (5062 names only a city's own strike; the city's answer in a melee is 5063's). For a
            # city attacker (cs1, cs3) BASE refuses the scene at its argument check (exit 2: the old code cannot play it).
            flags = OFF if a.expect_fail else KNOBS[knob]
            if knob in SHOT_TAG:
                base = run_one(a, a.exe_base, work / 'base' / label, att, dfn, extra, flags)
                ident = base['rc'] == 2 and not base['ok']
                same = 'base-refuses' if ident else 'DIFF(base rc %s)' % base['rc']
            else:
                mine = run_one(a, a.exe, work / 'no5062' / label, att, dfn, extra, flags + NO_5062)
                base = run_one(a, a.exe_base, work / 'base' / label, att, dfn, extra, flags)
                ident = base['ok'] and mine['ok'] and mine['line'] == base['line'] and mine['log'] == base['log']
                ident = ident and d['line'] == base['line'] and d['log'] == base['log']
                same = 'same' if ident else 'DIFF(off line %s log %s, default line %s log %s)' % (
                    mine['line'] == base['line'], mine['log'] == base['log'], d['line'] == base['line'], d['log'] == base['log'])
        breaking += not rule
        ok = d['ok'] and (not rule if a.expect_fail else rule) and same in ('-', 'same', 'base-refuses')
        failed += not ok
        att_imp = sum(1 for n in d['impacts'] if n.endswith('_' + d.get('attSound', '?')))
        own_imp = sum(1 for n in d['impacts'] if n.endswith('_' + d.get('defSound', '?')))
        att_by_def = sum(1 for n in d['att_impacts'] if n.endswith('_' + d.get('defSound', '?')))   # 5055
        att_by_own = sum(1 for n in d['att_impacts'] if n.endswith('_' + d.get('attSound', '?')))
        plot_g = sum(1 for n in d['grounds'] if n.endswith('_' + d.get('defPlot', '?')))
        dirt_g = sum(1 for n in d['grounds'] if n.endswith('_DIRT'))
        row = {'case': label, 'knob': knob, 'result': 'pass' if ok else 'FAIL', 'identity': same,
               'attacker': att.split(':')[2], 'defender': dfn.split(':')[2], 'impacts': len(d['impacts']),
               'impacts_by_attacker': att_imp, 'impacts_by_own': own_imp, 'grounds': len(d['grounds']),
               'grounds_by_plot': plot_g, 'grounds_dirt': dirt_g, 'strikerNamed': d.get('strikerNamed', ''),
               'plotFeature': d.get('plotFeature', ''), 'plotNoTable': d.get('plotNoTable', ''),
               'att_impacts': len(d['att_impacts']), 'att_impacts_by_defender': att_by_def, 'att_impacts_by_own': att_by_own,
               'strikerFallback': d.get('strikerFallback', ''), 'cityEra': d.get('cityEraera', ''),
               'cityEraNamed': d.get('cityEranamed', '')}
        # 5062: the city's flights and their children (counts only in the CSV)
        flight, child = SHOT_TAG.get(knob, ('\0', '\0'))
        row['city_flights'] = sum(1 for n in d['effects'] if n.endswith(flight))
        row['city_children'] = sum(1 for n in d['effects'] if child in n)
        row['city_hits'] = d.get('cityShotcityHits', '')
        rows.append(row)
        print('%-3s (%s) %-4s impacts=%d by_attacker=%d by_own=%d grounds=%d by_plot=%d dirt=%d strikerNamed=%s '
              'plotFeature=%s att_impacts=%d by_defender=%d by_own=%d fallback=%s cityEra=%s/%s cityShot=%s/%d/%d/%s %s' % (
                  label, knob, row['result'], row['impacts'], att_imp, own_imp, row['grounds'], plot_g, dirt_g,
                  row['strikerNamed'], row['plotFeature'], row['att_impacts'], att_by_def, att_by_own,
                  row['strikerFallback'], row['cityEra'] or '-', row['cityEraNamed'] or '-',
                  d.get('cityShoteffect', '-'), row['city_flights'], row['city_children'], row['city_hits'] or '-', same),
              flush=True)
        if not d['ok']:
            print('      ' + d['text'].replace('\n', '\n      ')[-1500:])
    if a.csv and rows:
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
    n = len(rows)
    passed = failed == 0 and n > 0
    if a.expect_fail:
        print('RESULT %s %d/%d (expect-fail: %d of %d case(s) break their expectation with the knobs off)' % (
            'PASS' if passed else 'FAIL', n - failed, n, breaking, n))
    else:
        print('RESULT %s %d/%d' % ('PASS' if passed else 'FAIL', n - failed, n))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
