// trigger_scene.cpp - ticket 4722: one recorded fight of 0557 played on the install's own art through the server's
// fight clock (srv_units_fights.h) and the 4775 core stepped live against the controller (as 4774's adapter does), with
// the trigger emitter (srv_trigger_emitter.h) fed each frame from the controller's crossed triggers; the stream goes to
// a section in memory (read back each frame as the shell would, R2) and to the offline trigger log (D137), which is read
// back and checked (trigger_checks.h). A tool, not a ctest: it needs the install and a database (run_trigger_scenes.py
// drives 0557's 19 cases; unit-combat-server.md section 11).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
//   trigger_scene --game INSTALL --db DB --out DIR [--mods "A;B"]
//       --attacker PLAYER:ID:UNIT_TYPE:X:Y:DAMAGE_BEFORE:FINAL_DAMAGE:MAX_HP   (5062: or PLAYER:city:CITY_ID:X:Y:-:FINAL:MAX,
//                  a city's own strike on a unit defender; pass --ranged --no-retaliate as the DLL's city strike)
//       --defender PLAYER:ID:UNIT_TYPE:X:Y:DAMAGE_BEFORE:FINAL_DAMAGE:MAX_HP   (or PLAYER:city:CITY_ID:X:Y:-:FINAL:MAX)
//       [--ranged] [--no-retaliate] [--seed N] [--tail FRAMES] [--hitch FRAME:SECONDS] [--strict-movement] [--loops-by-instance]
//       [--death-weapon own|opponent] [--terrain TAG] [--no-tier-gate] [--no-charge-any] (4737) [--charge-any] (4743: on;
//       the default is CombatParams::charge_any, off from 4743 W46, on again since 4743 W81)
//       [--no-turn-first] [--attacker-turn K] [--defender-turn K] (4735: the turn before a fight whose figures stand
//       off; a unit faces K x 60 degrees (K 0-5, clockwise in facing order) away from the facing nearest its opponent, as
//       a unit that arrived from elsewhere stands; a second line "turn: ..." counts the turns
//       and the run loops, SoundStarts with the loop flag of a movement clip 1400-1499 / 2400-2499)
//       [--all-held] (4744: every held figure of a unit created damaged stands in the fight, the rule before 4744; the
//       default, CombatParams::fight_drawn_figures, takes only the figures the placement draws)
//       [--no-reform-first] (4745 + 4747: no unit reforms before its fight, the rule before 4745; the default,
//       CombatParams::reform_first, re-lays a unit whose ReformBeforeCombat names its side; a line "reform: ..." gives
//       the reform runs per side, the last arrival, the first strike and the first landing)
//       [--dump-core] (4745: also writes the core's result as DIR/steps.csv, DIR/moves.csv and DIR/hits.csv)
//       [--no-walk-home] (4742: the survivors walk home after the fight's end, the rule before 4742; the default,
//       CombatParams::walk_home, walks them inside the fight; a line "walk: ..." gives the walkers per side, the walk's
//       start and last arrival, the refaces, the fight's OwnerEnd, the run loops started inside the walk before the
//       OwnerEnd, the loop starts after it and the loops no record ended)
//       [--no-city-in-place] (4748: a melee attacker on a city approaches its centre first, the rule before 4748; the
//       default, CombatParams::city_in_place, strikes the city from where the figures stand)
//       [--no-ready-after-move] (4746: every waiting figure requests combat ready 1600, the rule before 4746; the
//       default, CombatParams::ready_after_move, follows stock's state 2)
//       [--no-defend-from-stance] (4750: a non-answering defender's figures request bombard defend 1280 at once, the
//       rule before 4750; the default, CombatParams::defend_from_stance, requests stock's phase-2 shuffle 1450 first)
//       [--reform-ends-at-arrival | --no-reform-ends-at-arrival] (5050 (a), CombatParams::reform_ends_at_arrival, on by
//       default since 5050 W74 as (b): phase 1 ends at the last reforming figure's arrival; off, the rule before 5050:
//       after its run stop 1440)
//       [--attack-from-stance | --no-attack-from-stance] (5050 (b), CombatParams::attack_from_stance: an attacker's first
//       Ready takes stock's phase-2 stance 1450 first; off, the rule before 5050: 1600 at once with its entry clip)
//       [--no-attack-hold-without-walk] (5050 (b): a member without Walk takes no phase-2 hold; the default holds it
//       attack_stance_s with no request, then plays its whole 1600 entry before the strike)
//       [--no-attack-entry-after-turn] (5074, CombatParams::attack_entry_after_turn, on by default: a turned attacking
//       crew without Walk stops (1000, its run stop 1440) and then plays its whole 1600 entry from idle; off, the rule
//       before 5074: 1600 at the turn's end) [--clip-times] (5074: prints a `clips:` line per side, every animation
//       of its members with the length read)
//       [--no-rush-ride-through] [--no-defender-waits] (5057, CombatParams::rush_ride_through and
//       defender_waits_first_landing, both on by default: a rush rider lands its hit at its arrival and rides on in its
//       1120 / the type 2 defender strikes from the attacker's first landing on; off: the rules before 5057)
//       [--no-ride-strikes] (5071, CombatParams::ride_strikes_while_held, on by default: a rider standing after its ride
//       strikes from its run (1140) while the defender has a need open, and the ride leaves the defender's last planned
//       death to such a strike; off: 5057's rule. The `held:` line counts the attacker's hits with an attack clip and
//       those of 1140, the riders' stops into combat ready, the last such hit and each side's deaths)
//       [--no-type2-end-after-dying] (5072, CombatParams::type2_end_after_dying, on by default: a type 2 fight's riders
//       wait for the last dying figure before they walk home; off: they walk home in the step of the last landing. The
//       `tail:` line gives the attacker's first finish step, the last removal, the riders' home stops (Reface 1000 after
//       a 1400 walk) and those before the end, their stops from the run (Wait 1000 after a 1120), the attacker's run
//       stop sounds sent (SoundStart of ec 1440 / 2440: the sounds of a stop at the end come after it and are not sent)
//       and each side's deaths)
//       [--death-by-variant | --no-death-by-variant] [--end-after-dying | --no-end-after-dying] (5053,
//       CombatParams::death_by_variant and end_after_dying, both the core's default (on since the MC's word of 2026-11-25): a dying
//       figure lasts the death clip its machine plays (A 1200 or B 2200) / the standing figures wait for the dying and a
//       land defender leaves 1280 with a 1450 shuffle; off: the rules before 5053; the environment variable
//       C5_TRIGGER_SCENE_NO_5053=1 starts both off, for a runner's byte identity with a main build, the flags still win)
//       [--no-victory-word] (5059, CombatParams::victory_needs_word, on by default: victory only for a member whose
//       EnableActions carry the word Victory, as stock (none in VP); off: the machine's 1800 alone, the rule before 5059.
//       The `victory:` line counts the figures that took victory and the SoundStart records of their 1800 clips)
//       [--type2-phase2 | --no-type2-phase2] (5067 S1, CombatParams::type2_phase2, off by default until the MC's word: in
//       fight type 2 nobody looks before stock's 0.665 s phase 2 ends, the defender in the 1450 stance meanwhile; a line
//       `type2: type2Phase2= attFirstMove= defStances= attFirstLanding= end=`)
//       [--defender-step | --no-defender-step] (5061, CombatParams::defender_step, on by default: in fight type 2 the
//       defender's land figures run to stock's reach of the attackers from the attacker's first landing on; off: they
//       strike from where they stand, the rule before 5061; a line `defstep: defenderStep= defMoves= defMovers=
//       defFirstMove= end=`)
//       [--striker-weapon | --no-striker-weapon] (5052 (W), the product's TriggerStreamConfig::strikerWeapon, default
//       ON since MC #10847: a defender that does not strike back (srv_trigger_emitter.h DefenderStrikes) takes the
//       attacker's figure-0 weapon for $(WEAPON); --no-striker-weapon: its own, the rule before 5052)
//       [--striker-melee | --no-striker-melee] (5055, the product's TriggerStreamConfig::strikerMelee, server.cfg
//       render_units_trigger_striker_melee (MC #11607), on by default, in effect only with the striker weapon: a Sound's $(WEAPON) is the striker's for every struck unit, both sides of a melee, with its
//       own weapon as the second try when that name is no script; --no-striker-melee: 5052's rule exactly)
//       [--city-era ERA] [--no-city-era] (5058, the product's TriggerStreamConfig::cityEra, on by default with the
//       striker weapon: a defending city's owner era, EraTypes >= 0; a Sound $(WEAPON) the city strikes takes
//       CityWeaponTag(ERA), stock's CITY_EARLY / _MID / _LATE by era 0 / 1 / 2, EARLY from 3 on; without --city-era the
//       era is not known and the names are as before 5058; --no-city-era: the rule before 5058 exactly;
//       a line "cityera: era=ERA named=N" follows the names line only when the era is given and the switch on;
//       5062: with a city attacker ERA is the striking city's owner era, for both rules)
//       [--no-city-shot] (5062, the product's AnimRunner::setCityShot, render_units_trigger_city_shot, on by default: a
//       city attacker's strike names CombatParams::city_effect, Eras.CityBombardEffectTag of --city-era's era (not
//       given: no name); --no-city-shot: the rule before 5062; a line "cityshot: on=0|1 era=ERA effect=NAME|- cityHits=N"
//       follows the names lines only for a city attacker)
//       [--trigger-terrain | --no-trigger-terrain] [--plot X:Y:TERRAIN_TYPE:FEATURE_TYPE]... (5052 (T), the product's
//       plotTerrain, default ON since MC #11356 (5052 part D, W89; was off): $(TERRAIN) from the unit's plot in the table
//       the scene hands in, every plot not given without terrain or feature (so DIRT: a scene without --plot names every
//       unit DIRT, as the rule before 5052 does), by PlotTags::tagOf over the database's Terrains / Features
//       EffectTypeTag; FEATURE_TYPE or TERRAIN_TYPE "-" = none; --no-trigger-terrain: the rule before 5052, --terrain
//       TAG, DIRT by default; --terrain is read only with --no-trigger-terrain)
//       [--quick-v2] [--quick-rate R] [--quick-pre-rate P] [--quick-react S] [--quick-report] (0796, Quick Combat
//       Animations T1, quick_combat.h: the fight played at the rule's rates, the run-up at P, the first strike through
//       its landing plus S at 1x, the rest at R, 1 < R <= 8, 0 < P <= 8; --quick-v2 = the product's QuickCombatV2(),
//       the other three flags after it change single values; the fight clock, the core and the fighters' clip clocks
//       together, the effect routes' hit times mapped to the real clock. Off (no flag): the rule before 0796 byte for
//       byte. A line "quick: rate= pre= react= firstStrike= firstLanding= rateFrom= fightEnd= wallEnd= atLimit= endReason=
//       transfersAtRate=" (fight time but wallEnd, the real seconds from the fight's start frame to its OwnerEnd;
//       transfersAtRate: projectiles released in the phase at the rate, whose flights keep their real-time path) with the
//       mode on or with --quick-report)
//   Always (5052): a line "names: ..." gives the two knobs (and 5055's strikerMelee, strikerFallback at its end), the
//       $(WEAPON) values taken from the striker (strikerNamed),
//       the non-striking units whose striker is a city (strikerCity), the plot tags' sources, and the tags a check needs
//       (attSound, defSound: each side's figure-0 sound tag; defPlot: the defender's plot tag); run_names_scenes.py
//   Always (5057): a line "ride: ..." gives the two knobs, the attacker's landings at an arrival (rideLandings: hits with
//       code 1120), its ride-ons (Charge steps with no request), its stops (Wait with 1000 after a charge), its 1600
//       requests, the attacking figures whose first 1600 request came before their first hit (readyBeforeHit), the
//       defender's first landing and the attacker's first landing, and the end (run_charge_scenes.py)
//   Always (5053): a line "end: ..." gives the two knobs, the deaths, the last death (Dying step), the last removal, the
//       release (the first Done or Victory step of a figure that is not the attacker's), the end shuffles (Done with
//       1450) and the last reface after one, the shortest and longest dying time (Removed - Dying), the deaths whose
//       machine was seen playing B (2200) or A (1200) through the feedback (death_by_variant only), and the end
//       (run_end_scenes.py)
//   Always (5050): a line "phases: ..." gives phase 1's end (p1), the attacker's stance (p2) and first 1600, its first
//       strike, its stance steps (astances: 1450; aholds: the hold without Walk), its 1600 requests and those made from
//       idle (the sounding entry clip), and the end
//   Always (4750): a line "defend: ..." gives the defender's stance steps (Defend with 1450), its 1280 requests, those
//       of them made from idle (no movement before: the machine's sounding 1280 branch) and the 1280 one-shots
//   Always (4746): a line "ready: ..." gives the combat-ready requests (1600) per state, the Wait steps among them that
//       break stock's rule (exe-audio.md 13.3: a waiting figure requests 1600 only after a movement and only when its
//       member enables Walk), per side, and the one-shot SoundStarts by their clip's code (run_ready_scenes.py)
//
// Writes DIR/triggers.log (names from the install: never into git) and prints one line "trigger_scene: ..." with the
// counts. Exit 0 when the run and every check pass; 1 a check failed; 2 usage; 4 inputs; 5 an output.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/driver.h"
#include "c5r/unitanim/motion.h"
#include "c5r/unitanim/quick_combat.h"   // 0796
#include "c5r/unitanim/timeline.h"
#include "c5r/unitanim/trigger_log.h"
#include "c5r/units/art.h"
#include "c5r/units/placement.h"
#include "c5r/effects/effect_routes.h"        // 4793 (--effects)
#include "c5r/effects/projectile.h"           // 4793
#include "c5r/effects/projectile_model.h"     // 4793
#include "effects/srv_effect_routes.h"        // 4793
#include "effects/srv_fight_geometry.h"       // 4728 (--geometry)
#include "effects/srv_trigger_controller.h"
#include "effects/srv_trigger_emitter.h"
#include "effects/srv_trigger_logsink.h"
#include "trigger_checks.h"
#include "units/srv_units_fights.h"

using namespace c5r;
using namespace c5r::unitanim;
using namespace srveffects;

namespace {

int fail(int code, const std::string& msg)
{
	std::fprintf(stderr, "trigger_scene: exit %d: %s\n", code, msg.c_str());
	return code;
}

std::vector<std::string> split(const std::string& s, char c)
{
	std::vector<std::string> out;
	size_t at = 0;
	for(;;)
	{
		size_t p = s.find(c, at);
		out.push_back(s.substr(at, p == std::string::npos ? std::string::npos : p - at));
		if(p == std::string::npos)
			break;
		at = p + 1;
	}
	return out;
}

struct SideArg
{
	int32_t player = -1, id = -1, city = -1, x = 0, y = 0, before = 0, final_damage = 0, max_hp = 100;
	std::string type;
};
bool parseSide(const std::string& s, SideArg& out)
{
	std::vector<std::string> f = split(s, ':');
	if(f.size() != 8)
		return false;
	out.player = std::atoi(f[0].c_str());
	if(f[1] == "city")
		out.city = std::atoi(f[2].c_str());
	else
	{
		out.id = std::atoi(f[1].c_str());
		out.type = f[2];
		out.before = std::atoi(f[5].c_str());
	}
	out.x = std::atoi(f[3].c_str());
	out.y = std::atoi(f[4].c_str());
	out.final_damage = std::atoi(f[6].c_str());
	out.max_hp = std::atoi(f[7].c_str());
	return !(out.city < 0 && out.type.empty());
}

std::string lower(std::string s)
{
	for(char& c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

// Every *3DScripts.xml under the install (Assets/Sounds first, then the rest in path order) into the table.
void findScripts(const std::wstring& dir, std::vector<std::wstring>& out)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		const std::wstring n = fd.cFileName;
		if(n == L"." || n == L"..")
			continue;
		const std::wstring p = dir + L"\\" + n;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			findScripts(p, out);
		else if(n.size() > 13)
		{
			std::wstring tail = n.substr(n.size() - 13);
			for(wchar_t& c : tail)
				c = (wchar_t)towlower(c);
			if(tail == L"3dscripts.xml")
				out.push_back(p);
		}
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}

bool readFile(const std::wstring& p, std::string& out)
{
	FILE* f = _wfopen(p.c_str(), L"rb");
	if(!f)
		return false;
	char buf[65536];
	size_t n;
	out.clear();
	while((n = fread(buf, 1, sizeof buf, f)) > 0)
		out.append(buf, n);
	fclose(f);
	return true;
}

// The weapons of a member type: (WeaponTypeTag, WeaponTypeSoundOverrideTag) of Index 0.
struct Weapon
{
	std::string tag, sound;
};

class SceneSubs : public Substitutions
{
public:
	std::string terrainTag = "DIRT";
	bool deathOpponent = false;
	std::map<std::string, Weapon> weapons;                       // member type -> Index 0 weapon
	std::map<std::pair<int32_t, int32_t>, const CombatUnitFigures*> units;
	std::map<std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>> opponent;
	uint64_t noWeapon = 0;
	// 5052 (W, --striker-weapon): the product's rule (srv_trigger_emitter.h FightWeaponRoles, as TriggerStream's Subs)
	bool strikerWeapon = false;
	bool strikerMelee = false;   // 5055 (with strikerWeapon): FightWeaponRoles::namer's `struck` for a Sound, weaponFallback
	bool cityEra = false;        // 5058 (with strikerWeapon): a city striker's Sound $(WEAPON) by CityWeaponTag (--city-era)
	FightWeaponRoles roles;
	uint64_t strikerNamed = 0, strikerCity = 0, strikerFallback = 0, strikerCityEra = 0;
	// 5052 (T, --trigger-terrain): the unit's plot in the table the scene hands in (--plot), the tag by PlotTags::tagOf
	bool plotTerrain = false;
	PlotTags plotTags;
	std::vector<SrvFeed::PlotRecord> plots;                      // width x height of the scene's grid
	std::map<std::pair<int32_t, int32_t>, int32_t> plotOf;       // unit -> plot index
	uint64_t plotFeature = 0, plotTerr = 0, plotDirt = 0, plotNoTable = 0;
	bool terrain(int32_t player, int32_t unit, std::string& out) override
	{
		if(plotTerrain)
		{
			auto it = plotOf.find(std::make_pair(player, unit));
			if(it == plotOf.end() || it->second < 0 || (size_t)it->second >= plots.size())
			{
				++plotNoTable;   // the product's stand-in: DIRT
				out = "DIRT";
				return true;
			}
			const SrvFeed::PlotRecord& r = plots[(size_t)it->second];
			const PlotTags::From from = plotTags.tagOf(r.terrain, r.feature, out);
			++(from == PlotTags::From::Feature ? plotFeature : from == PlotTags::From::Terrain ? plotTerr : plotDirt);
			return true;
		}
		out = terrainTag;
		return true;
	}
	bool weapon(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec, std::string& out) override
	{
		int32_t p = player, u = unit, fig = figure;
		bool fromOpponent = false;
		if(deathOpponent && classOfCode(ec) == ClipClass::Death)
		{
			auto o = opponent.find(std::make_pair(player, unit));
			if(o == opponent.end() || o->second.second < 0)
				return false;
			p = o->second.first;
			u = o->second.second;
			fig = 0;
		}
		else if(strikerWeapon)   // 5052 (W)
		{
			auto isAir = [this](int32_t pp, int32_t uu) { return air(pp, uu); };
			int32_t op = -1, ou = -1, era = -1;
			switch(roles.namer(player, unit, isAir, op, ou, strikerMelee && type == TrigType::Sound, &era))   // 5055: struck
			{
			case FightWeaponRoles::Namer::Opponent:
				if(units.count(std::make_pair(op, ou)))
				{
					p = op;
					u = ou;
					fig = 0;
					fromOpponent = true;
				}
				break;
			case FightWeaponRoles::Namer::OpponentCity:
				if(cityEra && type == TrigType::Sound && era >= 0)   // 5058, as TriggerStream::Subs::weapon
				{
					out = CityWeaponTag(era);
					++strikerCityEra;
					return true;
				}
				++strikerCity;
				break;
			default:
				break;
			}
		}
		const int r = memberWeapon(p, u, fig, type, out);
		if(r < 0)
			++noWeapon;
		if(r > 0 && fromOpponent)
			++strikerNamed;
		return r > 0;
	}
	// 5055: the product's second try (TriggerStream::Subs::weaponFallback): the figure's own weapon when weapon() gave its
	// striker's for this Sound
	bool weaponFallback(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec, std::string& out) override
	{
		if(!strikerWeapon || (!strikerMelee && !cityEra) || type != TrigType::Sound || !roles.size() ||
		   (deathOpponent && classOfCode(ec) == ClipClass::Death))
			return false;
		auto isAir = [this](int32_t pp, int32_t uu) { return air(pp, uu); };
		int32_t op = -1, ou = -1, era = -1;
		const FightWeaponRoles::Namer nm = roles.namer(player, unit, isAir, op, ou, strikerMelee, &era);
		const bool striker = strikerMelee && nm == FightWeaponRoles::Namer::Opponent && units.count(std::make_pair(op, ou));
		const bool city = cityEra && nm == FightWeaponRoles::Namer::OpponentCity && era >= 0;   // 5058
		if(!striker && !city)
			return false;
		if(memberWeapon(player, unit, figure, type, out) <= 0)
			return false;
		++strikerFallback;
		return true;
	}

private:
	bool air(int32_t p, int32_t u) const
	{
		auto f = units.find(std::make_pair(p, u));
		if(f != units.end())
			for(const CombatMemberArt& m : f->second->members)
				if(m.domain == "Air")
					return true;
		return false;
	}
	// 1: out set; -1: the figure's member has no weapon row; 0: no such unit or figure
	int memberWeapon(int32_t p, int32_t u, int32_t fig, TrigType type, std::string& out) const
	{
		auto it = units.find(std::make_pair(p, u));
		if(it == units.end())
			return 0;
		int32_t at = 0;
		for(const CombatMemberArt& m : it->second->members)
		{
			if(fig >= at && fig < at + m.count)
			{
				auto w = weapons.find(m.member);
				if(w == weapons.end())
					return -1;
				out = type == TrigType::Sound && !w->second.sound.empty() ? w->second.sound : w->second.tag;
				return 1;
			}
			at += m.count;
		}
		return 0;
	}
};

class ScenePositions : public FigurePositions
{
public:
	std::map<std::pair<int32_t, int32_t>, std::pair<float, float>> centres;
	const FightMotion* motion = nullptr;
	double t = 0;   // seconds since the fight's start
	bool figure(int32_t player, int32_t unit, int32_t figure, float out[3]) override
	{
		FigurePose ps;
		if(!motion || figure < 0 || !motion->pose(t, player, unit, (uint16_t)figure, ps))
			return false;
		out[0] = ps.x;
		out[1] = ps.y;
		out[2] = 0;   // the flat stand-in field
		return true;
	}
	bool unit(int32_t player, int32_t unit, float out[3]) override
	{
		auto it = centres.find(std::make_pair(player, unit));
		if(it == centres.end())
			return false;
		out[0] = it->second.first;
		out[1] = it->second.second;
		out[2] = 0;
		return true;
	}
};

// The fight clock's player: the 4775 core stepped live against the controller (4774's adapter does the same).
class ScenePlayer : public srvunits::FightPlayer
{
public:
	TriggerEmitter* em = nullptr;
	EffectRoutes* routes = nullptr;   // 4793 (--effects): null = W16's runs as they were
	FightGeometry* geo = nullptr;     // 4728 (--geometry): the server's positions, bones and flight planner
	double fightStart = 0;            // 4793: the routes' clock at the fight's time 0
	double frameDt = 0;               // 4793: the current frame's dt
	CombatRun run;
	CombatInput in;
	CombatUnitFigures* fa = nullptr;
	CombatUnitFigures* fd = nullptr;
	CombatClips* clips = nullptr;
	CombatParams params;
	CombatFeedback* fb = nullptr;
	std::vector<TimelineEvent> events;
	bool started = false;
	uint64_t startFrame = 0;
	srvunits::FightPlan start(const srvunits::Fight& f) override
	{
		em->fightStarted(f);   // the hand-off (unit-combat-server.md 11.5)
		if(routes)
		{
			routes->fightStarted(f);   // 4793
			fightStart = routes->now() - frameDt;
		}
		if(geo)
			geo->fightStarted(f);   // 4728
		started = true;
		startFrame = f.startFrame;
		srvunits::FightPlan p;
		CombatStatus st = run.start(in, *fa, *fd, *clips, params, fb);
		p.ok = st.ok();
		p.why = st.text;
		return p;
	}
	// 0796: as srv_units_anim.cpp's ControllerFightPlayer::rate (quick_combat.h's rule on the run's result); the fight
	// time at which the rate reached quick.rate
	QuickCombat quick;
	double rateFrom = -1;
	double rate(const srvunits::Fight& f) override
	{
		if(!quick.on())
			return 1.0;
		const bool rangedFight = (f.rec.flags & SrvFeed::kCombatRanged) != 0;
		if(rangedFight && !released && ctl)   // the first projectile's release: an attacker's Transfer in the last apply
			for(const TriggerEvent& t : ctl->triggers())
				if(t.type == c5m::TriggerType::Transfer && t.player == f.attacker.owner && t.id == f.attacker.unit)
					released = true;
		const double r = QuickCombatRate(run.result(), f.elapsed, quick, rangedFight, released);
		if(rateFrom < 0 && r == quick.rate && !run.result().hits.empty())   // the rate after a landing (not a run-up at it)
			rateFrom = f.elapsed;
		stepRate = r;
		return r;
	}
	double stepRate = 1.0;   // 0796: the rate of this frame's step (the loop resets it to 1 after each frame)
	const Controller* ctl = nullptr;   // 0796: the scene's controller (its last apply's triggers)
	bool released = false;             // 0796: a Transfer of the attacker's figures seen (a ranged fight's release)
	bool advance(const srvunits::Fight& f, double dt) override
	{
		(void)f;
		std::vector<TimedEvent> evs;
		const bool end = run.advance(dt, evs);
		for(auto& te : evs)
			events.push_back(te.ev);
		return end;
	}
};

// 5053: the controller's feedback passed through unchanged; the death variants it answered (1200 A, 2200 B) are kept
// per figure for the "end:" line. The core asks playingVariant only with death_by_variant on.
class VariantSeen : public CombatFeedback
{
public:
	explicit VariantSeen(CombatFeedback& in) : in_(in) {}
	bool accepts(int32_t player, int32_t id, uint16_t figure, int32_t code) override
	{
		return in_.accepts(player, id, figure, code);
	}
	bool playing(int32_t player, int32_t id, uint16_t figure, int32_t code) override
	{
		return in_.playing(player, id, figure, code);
	}
	int32_t playingVariant(int32_t player, int32_t id, uint16_t figure, int32_t code) override
	{
		const int32_t v = in_.playingVariant(player, id, figure, code);
		if(v != 0)
			seen[std::make_tuple(player, id, figure, code)] = v;
		return v;
	}
	std::map<std::tuple<int32_t, int32_t, uint16_t, int32_t>, int32_t> seen;   // the core stops asking at its first answer: the one it acted on
private:
	CombatFeedback& in_;
};

}  // namespace

int main(int argc, char** argv)
{
	std::string game, db, out, mods, deathWeapon = "own", terrainTag = "DIRT";
	SideArg A, D;
	bool haveA = false, haveD = false, ranged = false, retaliate = true, strictMovement = false, loopsByInstance = false;
	bool tierGate = true;   // 4737: --no-tier-gate (the rule before 4737)
	bool chargeAny = CombatParams().charge_any;   // 4737 / 4743: the core's default (on again since 4743 W81); --charge-any, --no-charge-any
	bool turnFirst = true;                    // 4735: --no-turn-first
	bool fightDrawn = CombatParams().fight_drawn_figures;   // 4744: the core's default (on); --all-held (the rule before 4744)
	bool reformFirst = CombatParams().reform_first;         // 4745: the core's default (on); --no-reform-first
	bool dumpCore = false;                                  // 4745: --dump-core
	bool readyAfterMove = CombatParams().ready_after_move;  // 4746: the core's default (on); --no-ready-after-move
	bool walkHome = CombatParams().walk_home;               // 4742: the core's default (on); --no-walk-home
	bool cityInPlace = CombatParams().city_in_place;        // 4748: the core's default (on); --no-city-in-place
	bool defendFromStance = CombatParams().defend_from_stance;   // 4750: the core's default (on); --no-defend-from-stance
	bool reformEndsAtArrival = CombatParams().reform_ends_at_arrival;   // 5050 (a): the core's default; --[no-]reform-ends-at-arrival
	bool attackFromStance = CombatParams().attack_from_stance;          // 5050 (b): the core's default; --[no-]attack-from-stance
	bool attackHoldWithoutWalk = CombatParams().attack_hold_without_walk;   // 5050 (b): --no-attack-hold-without-walk
	bool attackEntryAfterTurn = CombatParams().attack_entry_after_turn;   // 5074: --no-attack-entry-after-turn
	bool clipTimesOut = false;                                            // 5074: --clip-times
	bool rushRideThrough = CombatParams().rush_ride_through;                 // 5057 (A): --no-rush-ride-through
	bool defenderWaits = CombatParams().defender_waits_first_landing;        // 5057 (B): --no-defender-waits
	bool rideStrikesWhileHeld = CombatParams().ride_strikes_while_held;      // 5071: --no-ride-strikes
	bool type2EndAfterDying = CombatParams().type2_end_after_dying;          // 5072: --no-type2-end-after-dying
	bool deathByVariant = CombatParams().death_by_variant;                   // 5053 R2: --[no-]death-by-variant
	bool endAfterDying = CombatParams().end_after_dying;                     // 5053 R1 + R3: --[no-]end-after-dying
	// 5053 (W153): C5_TRIGGER_SCENE_NO_5053=1 starts both knobs off (the rules before 5053), so every runner's arm can be
	// compared with a main build byte for byte without a flag of its own; the flags above still win
	if(const char* no5053 = std::getenv("C5_TRIGGER_SCENE_NO_5053"))
		if(no5053[0] == '1')
			deathByVariant = endAfterDying = false;
	bool victoryWord = CombatParams().victory_needs_word;                    // 5059: --no-victory-word
	bool type2Phase2 = CombatParams().type2_phase2;                          // 5067 S1: --[no-]type2-phase2
	bool defenderStep = CombatParams().defender_step;                        // 5061: --[no-]defender-step
	bool strikerWeapon = true;    // 5052 (W): --[no-]striker-weapon (the product's default, TriggerStreamConfig: on)
	bool strikerMelee = true;     // 5055: --[no-]striker-melee (the product's default, TriggerStreamConfig: on)
	bool cityEraOn = true;        // 5058: --no-city-era (the product's default, TriggerStreamConfig::cityEra: on)
	int32_t cityEraArg = -1;      // 5058: --city-era ERA (-1: not given, the era not known)
	bool cityShotOn = true;       // 5062: --no-city-shot (the product's default, AnimRunner::setCityShot: on)
	bool triggerTerrain = true;   // 5052 (T): --[no-]trigger-terrain (the product's default, TriggerStreamConfig: on since
	                              // MC #11356; off: --terrain TAG, the rule before 5052)
	std::vector<std::string> plotArgs;   // 5052 (T): --plot X:Y:TERRAIN_TYPE:FEATURE_TYPE (FEATURE_TYPE "-": none)
	int turnA = 0, turnD = 0;                // 4735: --attacker-turn, --defender-turn (0: facing the opponent)
	bool effects = false;   // 4793: the effect routes in front of the emitter
	bool geometry = false;  // 4728: the server's FightGeometry (positions, bones, planner) instead of the stand-ins
	bool projectilePose = true;   // 5064: --no-projectile-pose (the product's RouteParams::projectilePose: on)
	bool cityVolley = true;   // 0742: --no-city-volley (the product's RouteParams::cityVolley: on)
	uint64_t seed = 1;
	int tail = 120, hitchFrame = -1;
	double hitchDt = 0;
	QuickCombat quick;            // 0796: --quick-v2, --quick-rate, --quick-pre-rate, --quick-react (off: rate 0)
	bool quickReport = false;     // 0796: --quick-report (the quick: line with the mode off too)
	for(int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		auto next = [&](std::string& v) {
			if(i + 1 >= argc)
				return false;
			v = argv[++i];
			return true;
		};
		std::string v;
		if(a == "--game" && next(game))
			continue;
		if(a == "--db" && next(db))
			continue;
		if(a == "--out" && next(out))
			continue;
		if(a == "--mods" && next(mods))
			continue;
		if(a == "--attacker" && next(v))
		{
			haveA = parseSide(v, A);
			if(!haveA)
				return fail(2, "bad --attacker " + v);
			continue;
		}
		if(a == "--defender" && next(v))
		{
			haveD = parseSide(v, D);
			if(!haveD)
				return fail(2, "bad --defender " + v);
			continue;
		}
		if(a == "--ranged")
		{
			ranged = true;
			continue;
		}
		if(a == "--no-retaliate")
		{
			retaliate = false;
			continue;
		}
		if(a == "--strict-movement")
		{
			strictMovement = true;
			continue;
		}
		if(a == "--no-tier-gate")   // 4737
		{
			tierGate = false;
			continue;
		}
		if(a == "--no-charge-any")   // 4737
		{
			chargeAny = false;
			continue;
		}
		if(a == "--no-turn-first")   // 4735
		{
			turnFirst = false;
			continue;
		}
		if(a == "--no-reform-first")   // 4745 + 4747
		{
			reformFirst = false;
			continue;
		}
		if(a == "--quick-v2")   // 0796: the product's V2 values (quick_combat.h)
		{
			quick = QuickCombatV2();
			continue;
		}
		if((a == "--quick-rate" || a == "--quick-pre-rate" || a == "--quick-react") && i + 1 < argc)   // 0796
		{
			const double q = std::atof(argv[++i]);
			if(a == "--quick-rate" && (!(q > 1.0) || q > 8.0))
				return fail(2, "--quick-rate: above 1, at most 8");
			if(a == "--quick-pre-rate" && (!(q > 0.0) || q > 8.0))
				return fail(2, "--quick-pre-rate: above 0, at most 8");
			if(a == "--quick-react" && (!(q >= 0.0) || q > 2.0))
				return fail(2, "--quick-react: 0 to 2 seconds");
			(a == "--quick-rate" ? quick.rate : a == "--quick-pre-rate" ? quick.pre_rate : quick.react) = q;
			continue;
		}
		if(a == "--quick-report")   // 0796
		{
			quickReport = true;
			continue;
		}
		if(a == "--dump-core")   // 4745
		{
			dumpCore = true;
			continue;
		}
		if(a == "--no-walk-home")   // 4742: the walk after the fight's end (the rule before 4742)
		{
			walkHome = false;
			continue;
		}
		if(a == "--no-city-in-place")   // 4748: a melee attacker on a city approaches first (the rule before 4748)
		{
			cityInPlace = false;
			continue;
		}
		if(a == "--no-defend-from-stance")   // 4750: a non-answering defender requests 1280 at once (the rule before 4750)
		{
			defendFromStance = false;
			continue;
		}
		if(a == "--reform-ends-at-arrival")   // 5050 (a): phase 1 ends at the last arrival
		{
			reformEndsAtArrival = true;
			continue;
		}
		if(a == "--no-reform-ends-at-arrival")   // 5050 (a): phase 1 ends after the run stop (the rule before 5050)
		{
			reformEndsAtArrival = false;
			continue;
		}
		if(a == "--attack-from-stance")   // 5050 (b): an attacker's first Ready takes stock's phase-2 stance 1450 first
		{
			attackFromStance = true;
			continue;
		}
		if(a == "--no-attack-hold-without-walk")   // 5050 (b): a member without Walk takes no hold
		{
			attackHoldWithoutWalk = false;
			continue;
		}
		if(a == "--no-attack-entry-after-turn")   // 5074: 1600 at the turn's end (the rule before 5074)
		{
			attackEntryAfterTurn = false;
			continue;
		}
		if(a == "--clip-times")   // 5074: a clips: line per side
		{
			clipTimesOut = true;
			continue;
		}
		if(a == "--no-rush-ride-through")   // 5057 (A): every rider stops, enters 1600 and strikes 1100 (before 5057)
		{
			rushRideThrough = false;
			continue;
		}
		if(a == "--no-defender-waits")   // 5057 (B): the type 2 defender strikes at once (the rule before 5057)
		{
			defenderWaits = false;
			continue;
		}
		if(a == "--no-ride-strikes")   // 5071: the riders stand after the ride once no need is open (5057's rule)
		{
			rideStrikesWhileHeld = false;
			continue;
		}
		if(a == "--no-type2-end-after-dying")   // 5072: the riders walk home in the step of the last landing
		{
			type2EndAfterDying = false;
			continue;
		}
		if(a == "--death-by-variant" || a == "--no-death-by-variant")   // 5053 R2
		{
			deathByVariant = a == "--death-by-variant";
			continue;
		}
		if(a == "--end-after-dying" || a == "--no-end-after-dying")   // 5053 R1 + R3
		{
			endAfterDying = a == "--end-after-dying";
			continue;
		}
		if(a == "--no-victory-word")   // 5059: victory by the machine's 1800 alone (the rule before 5059)
		{
			victoryWord = false;
			continue;
		}
		if(a == "--type2-phase2" || a == "--no-type2-phase2")   // 5067 S1: stock's type 2 phase 2 / none (before 5067)
		{
			type2Phase2 = a == "--type2-phase2";
			continue;
		}
		if(a == "--defender-step" || a == "--no-defender-step")   // 5061: stock's type 2 defender step / none (before)
		{
			defenderStep = a == "--defender-step";
			continue;
		}
		if(a == "--no-attack-from-stance")   // 5050 (b): Ready requests 1600 at once (the rule before 5050)
		{
			attackFromStance = false;
			continue;
		}
		if(a == "--no-ready-after-move")   // 4746: every waiting figure requests 1600 (the rule before 4746)
		{
			readyAfterMove = false;
			continue;
		}
		if(a == "--all-held")   // 4744: every held figure of a unit created damaged stands (the rule before 4744)
		{
			fightDrawn = false;
			continue;
		}
		if((a == "--attacker-turn" || a == "--defender-turn") && next(v))   // 4735
		{
			if(v.size() != 1 || v[0] < '0' || v[0] > '5')
				return fail(2, "bad " + a + " " + v + " (0-5)");
			(a == "--attacker-turn" ? turnA : turnD) = v[0] - '0';
			continue;
		}
		if(a == "--charge-any")   // 4743: 4737's rule (on by default again since 4743 W81; off from W46 to W81)
		{
			chargeAny = true;
			continue;
		}
		if(a == "--loops-by-instance")
		{
			loopsByInstance = true;   // the note 1.5 item 2 as first written
			continue;
		}
		if(a == "--seed" && next(v))
		{
			seed = std::strtoull(v.c_str(), nullptr, 10);
			continue;
		}
		if(a == "--tail" && next(v))
		{
			tail = std::atoi(v.c_str());
			continue;
		}
		if(a == "--hitch" && next(v))
		{
			std::vector<std::string> f = split(v, ':');
			if(f.size() != 2)
				return fail(2, "bad --hitch " + v);
			hitchFrame = std::atoi(f[0].c_str());
			hitchDt = std::atof(f[1].c_str());
			continue;
		}
		if(a == "--death-weapon" && next(deathWeapon))
			continue;
		if(a == "--effects")
		{
			effects = true;   // 4793
			continue;
		}
		if(a == "--geometry")
		{
			effects = geometry = true;   // 4728
			continue;
		}
		if(a == "--no-city-volley")
		{
			cityVolley = false;   // 0742: the rule before 0742 (a city volley planned at its landing, scale 1)
			continue;
		}
		if(a == "--no-projectile-pose")
		{
			projectilePose = false;   // 5064: the rule before 5064 (held starts at scale 1, identity; yaw 0 launches)
			continue;
		}
		if(a == "--terrain" && next(terrainTag))
			continue;
		if(a == "--striker-weapon" || a == "--no-striker-weapon")   // 5052 (W)
		{
			strikerWeapon = a == "--striker-weapon";
			continue;
		}
		if(a == "--striker-melee" || a == "--no-striker-melee")   // 5055
		{
			strikerMelee = a == "--striker-melee";
			continue;
		}
		if(a == "--no-city-era")   // 5058
		{
			cityEraOn = false;
			continue;
		}
		if(a == "--no-city-shot")   // 5062
		{
			cityShotOn = false;
			continue;
		}
		if(a == "--city-era" && i + 1 < argc)   // 5058
		{
			cityEraArg = std::atoi(argv[++i]);
			if(cityEraArg < 0)
			{
				std::fprintf(stderr, "trigger_scene: --city-era takes an era index >= 0\n");
				return 2;
			}
			continue;
		}
		if(a == "--trigger-terrain" || a == "--no-trigger-terrain")   // 5052 (T)
		{
			triggerTerrain = a == "--trigger-terrain";
			continue;
		}
		if(a == "--plot" && next(v))   // 5052 (T)
		{
			if(split(v, ':').size() != 4)
				return fail(2, "bad --plot " + v);
			plotArgs.push_back(v);
			continue;
		}
		return fail(2, "unknown or incomplete argument " + a);
	}
	// 5062: the attacker may be a city (its own ranged strike, a unit defender); a city never attacks a city (the core's rule)
	if(game.empty() || db.empty() || out.empty() || !haveA || !haveD || (A.city >= 0 && D.city >= 0) || tail < 0 ||
	   (deathWeapon != "own" && deathWeapon != "opponent"))
		return fail(2, "needs --game, --db, --out, --attacker and --defender (not both cities)");

	gamefs::GameFiles gf;
	gamefs::GameFiles::Options gopt;
	for(auto& d : split(mods, ';'))
		if(!d.empty())
			gopt.extraLooseRoots.push_back(gamefs::fromUtf8(d));
	Status s = gf.open(gamefs::fromUtf8(game), gopt);
	if(!s.ok())
		return fail(4, "game files: " + s.str());
	c5m::SqliteTables tables;
	c5m::Error e;
	if(!c5m::SqliteTables::available() || !tables.open(db, &e))
		return fail(4, "database: " + e.what);
	units::GameFilesProvider provider(gf);
	units::ArtResolver resolver;
	units::UnitStatus us = resolver.open(tables, provider);
	if(!us.ok())
		return fail(4, "art tables: " + us.str());
	c5m::StringDatabase strings;
	us = units::loadStringDatabases(gf.names(), provider, strings);
	if(!us.ok())
		return fail(4, "string databases: " + us.str());
	BridgeTables bt;
	AnimStatus as = loadBridgeTables(tables, bt);
	if(!as.ok())
		return fail(4, as.text);
	CombatArtTables ct;
	std::string err;
	if(!loadCombatTables(tables, ct, err))
		return fail(4, err);

	// the 3D scripts' loop flags (exe-audio.md 3): the install's *3DScripts.xml, then the database's Audio_3DSounds rows
	ScriptTable scripts;
	std::vector<std::wstring> sfiles;
	findScripts(gamefs::fromUtf8(game) + L"\\Assets", sfiles);
	std::stable_sort(sfiles.begin(), sfiles.end(), [](const std::wstring& a, const std::wstring& b) {
		const bool sa = a.find(L"\\Assets\\Sounds\\") != std::wstring::npos, sb = b.find(L"\\Assets\\Sounds\\") != std::wstring::npos;
		return sa != sb ? sa : a < b;
	});
	size_t scriptRows = 0, loopChanged = 0;
	for(auto& p : sfiles)
	{
		std::string text;
		size_t ch = 0;
		if(readFile(p, text))
			scriptRows += scripts.loadXml(text, &ch);
		loopChanged += ch;
	}
	{
		std::vector<c5m::DbRow> rows;
		if(tables.rows("Audio_3DSounds", {"ScriptID", "Looping"}, rows, &e))
			for(auto& r : rows)
				if(!r[0].null && !r[0].text.empty())
				{
					scripts.set(r[0].text, !r[1].null && (r[1].text == "1" || lower(r[1].text) == "true"));
					++scriptRows;
				}
	}
	if(scripts.empty())
		return fail(4, "no 3D script found under the install's Assets");
	SceneSubs subs;
	subs.terrainTag = terrainTag;
	subs.deathOpponent = deathWeapon == "opponent";
	{
		std::vector<c5m::DbRow> rows;
		if(!tables.rows("ArtDefine_UnitMemberCombatWeapons", {"UnitMemberType", "Index", "WeaponTypeTag", "WeaponTypeSoundOverrideTag"},
		                rows, &e))
			return fail(4, "ArtDefine_UnitMemberCombatWeapons: " + e.what);
		for(auto& r : rows)
			if(!r[0].null && !r[1].null && r[1].text == "0")
				subs.weapons[r[0].text] = Weapon{r[2].null ? "" : r[2].text, r[3].null ? "" : r[3].text};
	}

	// the start scene: the two units at their plots, damage before the fight, facing each other (4776's set-up)
	units::UnitScene scene;
	for(int32_t p : {A.player, D.player})
	{
		bool have = false;
		for(auto& q : scene.players)
			have = have || q.player == p;
		if(have)
			continue;
		units::PlayerInput pi;
		pi.player = p;
		pi.colour = "PLAYERCOLOR_DEFAULT";
		pi.art_style = "ARTSTYLE_EUROPEAN";
		pi.era = "ERA_ANCIENT";
		scene.players.push_back(pi);
	}
	std::vector<std::string> clips;
	heightfield::HeightGrid grid;
	grid.mapW = static_cast<uint32_t>(std::max(A.x, D.x) + 4);
	grid.mapH = static_cast<uint32_t>(std::max(A.y, D.y) + 8);
	grid.samplesX = grid.mapW * 64;
	grid.samplesY = grid.mapH * 64;
	grid.ox = static_cast<float>(grid.mapW * 32);
	grid.oy = static_cast<float>(grid.mapH * 32) * grid.sy;
	const float h2w[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 1, 0, 32, 128, 0, 1};
	for(int k = 0; k < 16; ++k)
		grid.hexToWorld[k] = h2w[k];
	grid.h.assign(static_cast<size_t>(grid.samplesX) * grid.samplesY, 0);
	float ax = 0, ay = 0, dx = 0, dy = 0;
	if(!units::plotCentre(grid, A.x, A.y, ax, ay).ok() || !units::plotCentre(grid, D.x, D.y, dx, dy).ok())
		return fail(2, "a plot outside the stand-in height field");
	auto facingTo = [](float fx, float fy, float tx, float ty) {
		const float want = yawFacing(tx - fx, ty - fy);
		int best = 0;
		float bestDiff = 10;
		for(int d = 0; d < 6; ++d)
		{
			float diff = std::fabs(std::remainder(units::facingYaw(d) - want, 6.2831853f));
			if(diff < bestDiff)
			{
				bestDiff = diff;
				best = d;
			}
		}
		return best;
	};
	std::map<int32_t, units::UnitArt> arts;
	for(const SideArg* sa : {&A, &D})
	{
		if(sa->city >= 0)
			continue;
		units::UnitInput u;
		u.player = sa->player;
		u.id = sa->id;
		u.unit_type = sa->type;
		us = resolver.art_row(sa->type, "", "", u.art_row);
		if(!us.ok())
			return fail(4, sa->type + ": " + us.str());
		u.plot_x = sa->x;
		u.plot_y = sa->y;
		u.damage = static_cast<int16_t>(sa->before);
		u.max_hp = static_cast<int16_t>(sa->max_hp);
		u.facing = static_cast<int8_t>(sa == &A ? facingTo(ax, ay, dx, dy) : facingTo(dx, dy, ax, ay));
		u.facing = static_cast<int8_t>((u.facing + (sa == &A ? turnA : turnD)) % 6);   // 4735: turned away by K facings
		scene.units.push_back(u);
		units::UnitArt art;
		us = resolver.resolve(u.art_row, art);
		if(!us.ok())
			return fail(4, u.art_row + ": " + us.str());
		for(auto& m : art.members)
			for(auto& an : m.files.animations)
				clips.push_back(an.file.name);
		arts[sa->player] = art;
	}

	ResolverArtSource artSource(resolver);
	units::ClipFileLoader loader = units::clipLoaderFromProvider(provider, &strings);
	ClipGraphFactory graphs(loader);
	ClipGraphFactory::Durations durs;
	ClipGraphFactory::readDurations(loader, clips, durs);
	if(clipTimesOut)   // 5074: every clip of both sides' members with the length read (0: not read)
		for(const std::string& cn : clips)
		{
			auto it = durs.find(c5m::basename_lower(cn));
			std::printf("clips: %s %.4f\n", cn.c_str(), it == durs.end() ? 0.0 : static_cast<double>(it->second.first));
		}
	graphs.seed(durs);
	ActionBridge bridge(bt);
	ControllerOptions co;
	co.bridge = &bridge;
	co.strings = &strings;
	co.seed = seed;
	Controller c;
	c.open(provider, artSource, graphs, co);
	c.start(scene);
	TimelineFrame first;
	first.seq = 1;
	first.dt = 1.0 / 30.0;
	as = c.apply(first);
	if(!as.ok())
		return fail(4, "first frame: " + as.text);
	for(auto& st : c.statuses())
		if(!st.status.ok())
			return fail(4, std::to_string(st.player) + "/" + std::to_string(st.id) + " " + name(st.status.code) + ": " + st.status.text);

	ScenePlayer player;
	player.in.seq = 1;
	player.in.kind = D.city >= 0 || A.city >= 0 ? kCombatKindCity : kCombatKindUnit;   // 5062: a city attacker too
	player.in.flags = static_cast<uint16_t>(kCombatVisualize | (retaliate ? kCombatDefenderRetaliates : 0) | (ranged ? kCombatRanged : 0));
	player.in.attacker.owner = A.player;
	player.in.attacker.unit = A.city >= 0 ? -1 : A.id;   // 5062
	player.in.attacker.city = A.city;                    // 5062 (-1 for a unit, as before)
	player.in.attacker.final_damage = A.final_damage;
	player.in.attacker.max_hp = A.max_hp;
	player.in.defender.owner = D.player;
	player.in.defender.unit = D.city >= 0 ? -1 : D.id;
	player.in.defender.city = D.city;
	player.in.defender.final_damage = D.final_damage;
	player.in.defender.max_hp = D.max_hp;
	auto clipTimes = c.combatClips([&](const std::string& clip) {
		auto it = durs.find(c5m::basename_lower(clip));
		return it == durs.end() ? 0.f : it->second.first;
	});
	player.params.seed = seed;
	player.params.charge_any = chargeAny;   // 4737
	player.params.turn_first = turnFirst;   // 4735
	player.params.fight_drawn_figures = fightDrawn;   // 4744
	player.params.reform_first = reformFirst;         // 4745 + 4747
	player.params.ready_after_move = readyAfterMove;  // 4746
	player.params.walk_home = walkHome;               // 4742
	player.params.city_in_place = cityInPlace;        // 4748
	player.params.defend_from_stance = defendFromStance;   // 4750
	player.params.reform_ends_at_arrival = reformEndsAtArrival;   // 5050 (a)
	player.params.attack_from_stance = attackFromStance;          // 5050 (b)
	player.params.attack_hold_without_walk = attackHoldWithoutWalk;   // 5050 (b)
	player.params.attack_entry_after_turn = attackEntryAfterTurn;     // 5074
	player.params.rush_ride_through = rushRideThrough;                // 5057 (A)
	player.params.defender_waits_first_landing = defenderWaits;       // 5057 (B)
	player.params.ride_strikes_while_held = rideStrikesWhileHeld;     // 5071
	player.params.type2_end_after_dying = type2EndAfterDying;         // 5072
	player.params.death_by_variant = deathByVariant;                  // 5053 R2
	player.params.end_after_dying = endAfterDying;                    // 5053 R1 + R3
	player.params.victory_needs_word = victoryWord;                   // 5059
	player.params.type2_phase2 = type2Phase2;                         // 5067 S1
	player.params.defender_step = defenderStep;                       // 5061
	// 5062 (the product's AnimRunner rule, unit-combat-server.md 18.37): a city's own strike names its projectile, the
	// Eras.CityBombardEffectTag of its owner's era (--city-era ERA; not given: not known, no name); --no-city-shot: none
	std::string cityShot;
	if(A.city >= 0 && cityShotOn && cityEraArg >= 0)
	{
		std::vector<c5m::DbRow> rows;
		if(tables.rows("Eras", {"ID", "CityBombardEffectTag"}, rows, &e))
			for(auto& r : rows)
				if(r.size() == 2 && !r[0].null && !r[1].null && std::atoi(r[0].text.c_str()) == cityEraArg)
					cityShot = r[1].text;
		player.params.city_effect = cityShot;
	}
	player.quick = quick;   // 0796
	player.ctl = &c;
	CombatUnitFigures fa, fd;
	if(A.city < 0)   // 5062: a city attacker has no figures (as is ok here: the first frame's apply passed)
		as = c.combatFigures(A.player, A.id, ct, fa);
	if(as.ok() && D.city < 0)
		as = c.combatFigures(D.player, D.id, ct, fd);
	if(!as.ok())
		return fail(4, "combat figures: " + as.text);
	units::FormationTable ft;
	s = ft.load(gf, gamefs::fromUtf8(game));
	if(!s.ok())
		return fail(4, "UnitFormations.xml: " + s.str());
	for(CombatUnitFigures* f : {&fa, &fd})
	{
		const SideArg& sa = f == &fa ? A : D;
		if(sa.city >= 0)   // a city side: only its centre (5062: the attacker's too)
		{
			f->has_centre = true;
			f->centre_x = f == &fa ? ax : dx;
			f->centre_y = f == &fa ? ay : dy;
			continue;
		}
		const units::UnitArt& art = arts[sa.player];
		units::PlacementInput pin;
		pin.formation = art.formation;
		for(auto& m : art.members)
			for(int k = 0; k < m.count; ++k)
				pin.zOffset.push_back(m.zoffset);
		pin.plotX = sa.x;
		pin.plotY = sa.y;
		for(auto& u : scene.units)
			if(u.player == sa.player)
				pin.facing = u.facing;
		pin.seed = 0;
		pin.damage = static_cast<int16_t>(sa.before);
		pin.maxHp = static_cast<int16_t>(sa.max_hp);
		s = combatPositions(ft, grid, pin, *f);
		if(!s.ok())
			return fail(4, "combat positions: " + s.str());
	}
	ControllerFeedback cfb(c);
	VariantSeen fb(cfb);   // 5053: the controller's answers as they are, with the death variants it gave recorded
	player.fa = &fa;
	player.fd = &fd;
	player.clips = clipTimes.get();
	player.fb = &fb;

	if(A.city < 0)   // 5062: a city attacker has no figures
		subs.units[std::make_pair(A.player, A.id)] = &fa;
	if(D.city < 0)
	{
		subs.units[std::make_pair(D.player, D.id)] = &fd;
		if(A.city < 0)   // 5062: a city attacker is nobody's unit opponent
		{
			subs.opponent[std::make_pair(A.player, A.id)] = std::make_pair(D.player, D.id);
			subs.opponent[std::make_pair(D.player, D.id)] = std::make_pair(A.player, A.id);
		}
	}
	// 5052 (W): the fight's roles, as TriggerStream::fightArt records them (the attacker's damage in this combat is not
	// known here: the retaliate flag decides, as the core's)
	subs.strikerWeapon = strikerWeapon;
	subs.strikerMelee = strikerWeapon && strikerMelee;   // 5055
	subs.cityEra = strikerWeapon && cityEraOn;           // 5058
	if(strikerWeapon)
	{
		FightWeaponRoles::Fight r;
		r.seq = 1;
		r.attPlayer = A.player;
		r.attUnit = A.city >= 0 ? -1 : A.id;   // 5062: -1, a city strikes
		r.attCity = A.city;                    // 5062 (-1 for a unit, as before)
		r.defPlayer = D.player;
		r.defUnit = D.city >= 0 ? -1 : D.id;
		r.retaliates = retaliate;
		r.attackerDamage = -1;
		if(subs.cityEra && D.city >= 0)
			r.defCityEra = cityEraArg;   // 5058 (-1 when not given: not known)
		if(subs.cityEra && A.city >= 0)
			r.attCityEra = cityEraArg;   // 5062's scene: the striking city's era, as TriggerStream records it (5058)
		subs.roles.started(r);
	}
	// 5052 (T): the plot table handed in (--plot; every other plot without terrain or feature) and the tags from the
	// database's Terrains / Features rows, as TriggerStream::open reads them (loadPlotTags)
	subs.plotTerrain = triggerTerrain;
	if(triggerTerrain)
	{
		std::map<std::string, int32_t> terrainId, featureId;
		for(int k = 0; k < 2; ++k)
		{
			std::vector<c5m::DbRow> rows;
			const char* table = k ? "Features" : "Terrains";
			if(!tables.rows(table, {"ID", "Type", "EffectTypeTag"}, rows, &e))
				return fail(4, std::string(table) + ": " + e.what);
			for(auto& r : rows)
				if(!r[0].null && !r[1].null)
				{
					const int32_t id = (int32_t)std::atoi(r[0].text.c_str());
					(k ? featureId : terrainId)[r[1].text] = id;
					(k ? subs.plotTags.feature : subs.plotTags.terrain)[id] = r[2].null ? std::string() : r[2].text;
				}
		}
		SrvFeed::PlotRecord none;
		memset(&none, 0, sizeof(none));
		none.terrain = -1;
		none.feature = -1;
		subs.plots.assign((size_t)grid.mapW * grid.mapH, none);
		for(const std::string& pa : plotArgs)
		{
			const std::vector<std::string> f = split(pa, ':');
			const int32_t x = std::atoi(f[0].c_str()), y = std::atoi(f[1].c_str());
			if(x < 0 || y < 0 || x >= (int32_t)grid.mapW || y >= (int32_t)grid.mapH)
				return fail(2, "--plot outside the scene's grid: " + pa);
			SrvFeed::PlotRecord& pr = subs.plots[(size_t)y * grid.mapW + x];
			if(f[2] != "-")
			{
				auto t = terrainId.find(f[2]);
				if(t == terrainId.end())
					return fail(4, "--plot: no terrain " + f[2]);
				pr.terrain = (int8_t)t->second;
			}
			if(f[3] != "-")
			{
				auto t = featureId.find(f[3]);
				if(t == featureId.end())
					return fail(4, "--plot: no feature " + f[3]);
				pr.feature = (int8_t)t->second;
			}
		}
		if(A.city < 0)   // 5062
			subs.plotOf[std::make_pair(A.player, A.id)] = A.y * (int32_t)grid.mapW + A.x;
		if(D.city < 0)
			subs.plotOf[std::make_pair(D.player, D.id)] = D.y * (int32_t)grid.mapW + D.x;
	}
	ScenePositions pos;
	if(A.city < 0)   // 5062
		pos.centres[std::make_pair(A.player, A.id)] = std::make_pair(ax, ay);
	if(D.city < 0)
		pos.centres[std::make_pair(D.player, D.id)] = std::make_pair(dx, dy);

	// the stream: a section in memory (the shell reads it each frame) and the offline log
	std::vector<uint8_t> section(VPTRIG_SECTION_BYTES, 0);
	SectionSink sec;
	sec.attach(section.data());
	LogSink log;
	const std::string logPath = out + "/triggers.log";
	if(!log.open(logPath, err))
		return fail(5, "triggers.log: " + err);
	TeeSink tee(sec, log);
	EmitterParams ep;
	ep.movementInFight = !strictMovement;
	ep.loopsByFight = !loopsByInstance;
	ep.tierGate = tierGate;   // 4737
	TriggerEmitter em(tee, ep);
	em.setScripts(&scripts);
	em.setSubstitutions(&subs);
	em.setPositions(&pos);
	player.em = &em;

	// 4793 (--effects): the effect routes (effects-formats.md "E8 as built (4793)"): the install's effect library, the
	// controller's trigger files for the held projectiles, the weapon rows, and a stand-in launch planner (the held
	// effect's point to the opponent's root, the arc of 4792 with the EXE's flight time rule; the core's own target point
	// is not exposed at the Transfer moment, named in E8)
	c5m::EffectLibrary fxlib;
	std::unique_ptr<c5r::effects::EffectNames> fxnames;
	std::unique_ptr<EffectRoutes> routes;
	std::unique_ptr<FightGeometry> geo;   // 4728 (--geometry)
	c5r::effects::ProjectileWeaponTable pweapons;
	if(effects)
	{
		if(!c5r::effects::loadInstallEffects(gf, provider, fxlib, err))
			return fail(4, "effects: " + err);
		if(!c5r::effects::loadProjectileWeapons(tables, pweapons, err))
			return fail(4, "projectile weapons: " + err);
		fxnames.reset(new c5r::effects::EffectNames(fxlib));
		{
			RouteParams rp;
			rp.projectilePose = projectilePose;   // 5064
			rp.cityVolley = cityVolley;   // 0742
			routes.reset(new EffectRoutes(em, *fxnames, rp));
		}
		routes->setSubstitutions(&subs);
		routes->setPositions(&pos);
		routes->setScripts(&scripts);
		routes->setTriggerFiles([&c](const std::string& file) -> const c5m::TriggerSystem* {
			const c5m::TriggerSystem* ts = nullptr;
			return c.logic() && c.logic()->triggers(file, ts).ok() ? ts : nullptr;
		});
		const float cityX = dx, cityY = dy;
		const bool cityTarget = D.city >= 0;
		routes->setFlightPlanner([&, cityX, cityY, cityTarget](const TriggerIn& t, const std::string& effect, const float from[3],
		                                                       c5r::effects::ProjectileSpec& outSpec, std::string& why) {
			using namespace c5r::effects;
			auto opp = subs.opponent.find(std::make_pair(t.player, t.unit));
			float to[3] = {cityX, cityY, 0};
			if(!cityTarget || t.unit == D.id)
			{
				if(opp == subs.opponent.end() || !pos.unit(opp->second.first, opp->second.second, to))
				{
					why = "no target";
					return false;
				}
			}
			// the firing member's weapon row (effects-server.md 1.1 item 1) and the speed rule (1.1 item 3)
			const ProjectileWeapon* w = nullptr;
			auto fu = subs.units.find(std::make_pair(t.player, t.unit));
			if(fu != subs.units.end())
			{
				int32_t at = 0;
				for(const CombatMemberArt& m : fu->second->members)
				{
					if(t.figure >= at && t.figure < at + m.count)
					{
						auto rows = pweapons.find(m.member);
						if(rows != pweapons.end())
							w = pickWeapon(rows->second, effect, 0);
						break;
					}
					at += m.count;
				}
			}
			float speed = fxnames->movementSpeed(effect);
			if(!(speed > 0))
				speed = w ? w->speed : kNullSpeed;
			const V3 L{from[0], from[1], from[2]}, T{to[0], to[1], to[2]};
			V3 d{T.x - L.x, T.y - L.y, 0};
			const float hl = std::sqrt(d.x * d.x + d.y * d.y);
			V3 D3 = hl > 1e-6f ? V3{d.x / hl * 0.7071f, d.y / hl * 0.7071f, 0.7071f} : V3{0, 0, 1};
			arcPoints(L, T, D3, outSpec.path.p);
			outSpec.path.kind = PathKind::Uniform;
			const float dist = std::sqrt((T.x - L.x) * (T.x - L.x) + (T.y - L.y) * (T.y - L.y) + (T.z - L.z) * (T.z - L.z));
			outSpec.path.duration = flightTime(dist, speed);
			outSpec.t0 = routes->now();
			outSpec.effect = effect;
			NameValues tv;
			std::string landTag = subs.terrainTag;
			if(!cityTarget && subs.plotTerrain)
				subs.terrain(D.player, D.id, landTag);   // 5052 (T): the target's plot
			tv.set_terrain(cityTarget ? "CITY" : landTag);
			fxnames->children(effect, tv, outSpec.children);
			if(w)
			{
				outSpec.child_scale = w->child_scale;
				std::string hn;
				const NameResult r = w->hit_effect.empty() ? NameResult::NotDefined : fxnames->resolve(w->hit_effect, tv, hn);
				if(r == NameResult::Exact || r == NameResult::Fallback)
				{
					outSpec.hit_effect = hn;
					// the product's rule (srv_fight_geometry.cpp); this scene keeps the flight at scale 1 (no member scale)
					outSpec.hit_scale = c5r::effects::hitScaleOf(*w, outSpec.scale);
				}
			}
			return true;
		});
		player.routes = routes.get();
		if(geometry)
		{
			// 4728: the server's geometry: figure poses from the fight's motion on the stand-in field, the bones on the
			// controller's programs, the flight planner with the core's target (srv_fight_geometry.h)
			geo.reset(new FightGeometry());
			if(!geo->open(tables, err))
				return fail(4, "geometry: " + err);
			geo->setBoneSource(&provider, &strings);
			geo->setNames(fxnames.get());
			geo->setGrid(&grid);
			geo->setWidth(grid.mapW);
			geo->setProjectilePose(projectilePose);   // 5064
			geo->setUnitRoot([&pos](int32_t p, int32_t u, float o[3]) { return pos.unit(p, u, o); });
			geo->setSubstitutions(&subs);
			geo->setTriggerFiles([&c](const std::string& file) -> const c5m::TriggerSystem* {
				const c5m::TriggerSystem* ts = nullptr;
				return c.logic() && c.logic()->triggers(file, ts).ok() ? ts : nullptr;
			});
			for(const SideArg* sa : {&A, &D})
				if(sa->city < 0)
					geo->unitArt(sa->player, sa->id, std::make_shared<units::UnitArt>(arts[sa->player]));
			em.setPositions(geo.get());
			routes->setPositions(geo.get());
			FightGeometry* g = geo.get();
			EffectRoutes* rt = routes.get();
			routes->setFlightPlanner([g, rt](const TriggerIn& t, const std::string& effect, const float from[3],
			                                 c5r::effects::ProjectileSpec& outSpec, std::string& why) {
				return g->plan(t, effect, from, rt->now(), outSpec, why);
			});
			player.geo = geo.get();
		}
	}
	size_t hitsTaken = 0;   // 4793: the core's hits already handed to the routes
	size_t launchesTaken = 0;   // 0742: the core's city launches already handed over

	srvunits::FightClock clock(player);
	SrvFeed::CombatRecord rec;
	memset(&rec, 0, sizeof(rec));
	rec.seq = 1;
	rec.kind = D.city >= 0 || A.city >= 0 ? SrvFeed::kCityCombat : SrvFeed::kUnitCombat;   // 5062: a city attacker too
	rec.flags = (uint16_t)(SrvFeed::kCombatVisualize | SrvFeed::kCombatReturnedKnown | (retaliate ? SrvFeed::kCombatDefenderRetaliates : 0) |
	                       (ranged ? SrvFeed::kCombatRanged : 0));
	rec.returned = 7001;   // the EXE's event id stand-in
	rec.plot = geometry && D.city >= 0 ? D.y * (int32_t)grid.mapW + D.x : -1;   // 4728: the city target's centre
	rec.attackerOwner = A.player;
	rec.attackerUnit = A.city >= 0 ? -1 : A.id;   // 5062
	rec.attackerCity = A.city;                    // 5062 (-1 for a unit, as before)
	rec.defenderOwner = D.player;
	rec.defenderUnit = D.city >= 0 ? -1 : D.id;
	rec.defenderCity = D.city;
	rec.finalDamage[0] = A.final_damage;
	rec.finalDamage[1] = D.final_damage;
	if(!clock.add(rec, nullptr, 0, err))
		return fail(4, "fight clock: " + err);

	VPTrigCursor cur = {0, 0, 0};
	uint64_t shellRead = 0, shellNewGen = 0;
	uint64_t soundTrig = 0, soundDur = 0;   // Sound triggers crossed, and those with a duration attribute > 0
	double soundDurMax = 0;
	const double dt0 = 1.0 / 30.0;
	std::shared_ptr<FightMotion> motion;   // 4728: shared, as the server's runner holds it (FightGeometry::fightRun)
	size_t motionMoves = (size_t)-1, motionSteps = (size_t)-1;
	double fightT = 0;
	double quickWall = 0, quickWallEnd = -1;   // 0796: real seconds from the fight's start frame to its OwnerEnd
	uint64_t quickLateTransfers = 0;           // 0796 (diagnostic): Transfer triggers crossed in the phase at quick.rate
	std::map<int64_t, double> frameT;   // 4742: the fight's clock at each frame F (from the frame after its start)
	int afterEnd = -1;
	uint8_t endReason = 255;
	double endElapsed = 0;
	std::vector<std::pair<int32_t, int32_t>> fighters;
	if(A.city < 0)   // 5062: a city attacker has no clip instances
		fighters.push_back({A.player, A.id});
	if(D.city < 0)
		fighters.push_back({D.player, D.id});
	for(int k = 0; k < 30 * 40 && afterEnd < tail; ++k)
	{
		const uint64_t F = (uint64_t)k + 2;
		const double dt = (k == hitchFrame) ? hitchDt : dt0;
		if(!em.beginFrame((int64_t)F, dt, err))
			return fail(4, "emitter frame: " + err);
		player.frameDt = dt;
		if(routes && !routes->beginFrame(dt, err))
			return fail(4, "routes frame: " + err);
		// the fighting units' clip instances as the controller holds them (the last apply)
		std::vector<srvunits::FigureInstance> figs;
		for(auto& p : c.programs())
			for(auto& u : fighters)
				if(p.player == u.first && p.id == u.second && em.inFight(p.player, p.id))
				{
					srvunits::FigureInstance fi;
					fi.player = p.player;
					fi.unit = p.id;
					fi.figure = (int32_t)p.figure;
					fi.instance = p.instance;
					figs.push_back(fi);
				}
		std::vector<srvunits::OwnerEnd> ends;
		player.events.clear();
		if(!clock.frame(F, dt, &figs, ends, err))
			return fail(4, "fight clock frame: " + err);
		for(auto& oe : ends)
			if(oe.instance == 0)
			{
				afterEnd = 0;
				endReason = (uint8_t)oe.why;
				endElapsed = oe.elapsed;
				if(oe.why == srvunits::FightEnd::Refused)
					return fail(4, "the fight was refused: " + oe.text);
			}
		const double qr = player.stepRate;   // 0796: the rate the clock stepped the fight by in this frame (1: not stepped)
		player.stepRate = 1.0;
		if(quick.on())   // 0796: the fighters' clip clocks at their fight's rate of this frame (as AnimRunner::apply)
		{
			c.clearRates();
			for(auto& u : fighters)
				c.setRate(Controller::UnitKey(u.first, u.second), clock.unitRate(u.first, u.second));
		}
		if(player.started && afterEnd < 0)
			quickWall += dt;
		if(afterEnd == 0 && quickWallEnd < 0)
			quickWallEnd = quickWall;
		TimelineFrame f;
		f.seq = F;
		f.dt = dt;
		f.events = player.events;
		as = c.apply(f);
		if(!as.ok())
			return fail(4, "frame: " + as.text);
		// positions on this frame's poses: the fight's motion so far (4776's FightMotion from the core's moves)
		if(player.started)
		{
			fightT += dt * qr;   // 0796: the fight's own time (dt x 1 = dt exactly with the mode off)
			frameT[(int64_t)F] = fightT;   // 4742
			const CombatResult& r = player.run.result();
			if(r.status.ok() && (r.moves.size() != motionMoves || r.steps.size() != motionSteps))
			{
				std::unique_ptr<FightMotion> m(new FightMotion);
				if(m->build(r, fa, fd).ok())
				{
					motion = std::move(m);
					motionMoves = r.moves.size();
					motionSteps = r.steps.size();
				}
			}
			pos.motion = motion.get();
			pos.t = fightT;
			if(geo)
				geo->fightRun(1, r, motion, fightT);   // 4728: the core's targets and the motion at the fight's clock
		}
		if(geo)
			geo->beginFrame(c);   // 4728: this frame's pose programs
		std::vector<TriggerIn> trig;
		triggersOf(c.triggers(), em, trig);
		for(auto& t : trig)
			if(t.type == TrigType::Sound)
			{
				++soundTrig;
				if(t.duration > 0)
				{
					++soundDur;
					soundDurMax = std::max(soundDurMax, (double)t.duration);
				}
			}
		if(routes)
		{
			// 0796: with the mode on the core's times are the fight's own (scaled): a time T maps to the real clock as
			// W - (fightT - T) / rate (W: the real seconds since the fight's time 0), so a hit taken in the frame it lands is
			// neither late nor dropped (as the server's AnimTriggerTap::fightClock does)
			const double hitStart = player.fightStart;
			const double wallNow = routes->now() - player.fightStart;
			auto toReal = [&](HitIn& h) {
				if(!quick.on())
					return;
				h.hit.time = wallNow - (fightT - h.hit.time) / qr;
				h.hit.launch = wallNow - (fightT - h.hit.launch) / qr;
			};
			// 0796 (diagnostic): projectiles released in the phase after the first landing (Transfer triggers of this frame's
			// apply while the clock stepped at quick.rate after a hit): their routes flights keep the real-time path
			if(quick.on() && qr == quick.rate && !player.run.result().hits.empty())
				for(const TriggerEvent& t : c.triggers())
					quickLateTransfers += t.type == c5m::TriggerType::Transfer ? 1 : 0;
			// 0742: the core's city shots at their launch, before the landings (as AnimTriggerTap::fightRun)
			const std::vector<CombatHit>& ls = player.run.result().launches;
			if(ls.size() > launchesTaken)
			{
				std::vector<HitIn> in;
				for(size_t lk = launchesTaken; lk < ls.size(); ++lk)
				{
					HitIn h;
					h.hit = ls[lk];
					h.fightSeq = 1;
					h.targetIsCity = h.hit.target_id < 0 && D.city >= 0;
					toReal(h);   // 0796
					in.push_back(h);
				}
				launchesTaken = ls.size();
				routes->launches(in, hitStart);
			}
			// 4793: the core's new hits, the triggers' names and hand-overs, the fights' ends, the flights
			const std::vector<CombatHit>& hs = player.run.result().hits;
			if(hs.size() > hitsTaken)
			{
				std::vector<HitIn> in;
				for(size_t hk = hitsTaken; hk < hs.size(); ++hk)
				{
					HitIn h;
					h.hit = hs[hk];
					h.fightSeq = 1;
					h.targetIsCity = h.hit.target_id < 0 && D.city >= 0;
					toReal(h);   // 0796
					in.push_back(h);
				}
				hitsTaken = hs.size();
				routes->hits(in, hitStart);
			}
			routes->intake(trig);
			routes->ownerEnds(ends);
			routes->endFrame();
		}
		em.triggers(trig);
		em.ownerEnds(ends);
		em.endFrame();
		for(auto& oe : ends)   // 5052 (W): the fight's own end drops its roles after its records (TriggerStream's afterFrame)
			if(oe.why != srvunits::FightEnd::ClipEnded)
				subs.roles.ended(oe.seq);
		if(geo)
			geo->ownerEnds(ends);   // 4728
		// the shell's read at the insertion of frame F (R2)
		VPTrigRecord rr;
		VPTrigReadResult res;
		while((res = VPTrigReadNext(section.data(), cur, (int64_t)F, rr)) != VPTRIG_READ_NONE)
		{
			if(res == VPTRIG_READ_OK)
				++shellRead;
			else if(res == VPTRIG_READ_NEWGEN)
				++shellNewGen;
		}
		VPTrigPublishReader(section.data(), cur, 1);
		if(afterEnd >= 0)
			++afterEnd;
	}
	if(!log.close(err))
		return fail(5, "triggers.log: " + err);
	if(endReason == 255)
		return fail(1, "the fight did not end within 40 s");

	TriggerLog back;
	s = readTriggerLog(logPath, back);
	if(!s.ok())
		return fail(1, "the log does not read back: " + s.str());
	trigchecks::StreamCheck sc = trigchecks::checkStream(back.records, !strictMovement);
	const EmitterCounters& cn = em.counters();
	// the loops' lives (frames from SoundStart to the record that ends them) and how each ended
	std::map<uint32_t, int64_t> loopStart;
	std::vector<double> lives;
	std::map<std::tuple<int32_t, int32_t, int32_t, uint64_t>, std::vector<uint32_t>> byInst;
	std::map<uint32_t, std::vector<uint32_t>> byFight;
	for(auto& r : back.records)
	{
		if(r.kind == VPTRIG_SOUND_START && (r.flags & VPTRIG_FLAG_LOOP))
		{
			loopStart[r.handle] = r.frameId;
			byInst[std::make_tuple(r.player, r.unit, r.figure, r.instance)].push_back(r.handle);
			byFight[r.fightSeq].push_back(r.handle);
		}
		auto close = [&](uint32_t h) {
			auto it = loopStart.find(h);
			if(it != loopStart.end())
			{
				lives.push_back((double)(r.frameId - it->second) * dt0);
				loopStart.erase(it);
			}
		};
		if(r.kind == VPTRIG_SOUND_STOP)
			close(r.handle);
		if(r.kind == VPTRIG_OWNER_END)
		{
			if(VPTrigIsFightEnd(r))
			{
				for(uint32_t h : byFight[r.fightSeq])
					close(h);
			}
			else
				for(uint32_t h : byInst[std::make_tuple(r.player, r.unit, r.figure, r.instance)])
					close(h);
		}
	}
	std::sort(lives.begin(), lives.end());
	std::string codes;
	for(auto& x : cn.exeCodes)
		codes += (codes.empty() ? "" : "|") + std::to_string(x.first) + ":" + std::to_string(x.second);
	const VPTrigServer* srv = VPTRIG_SERVER(section.data());
	const bool shellOk = shellRead == back.records.size() && cur.lost == 0 && shellNewGen == 1;
	const bool ok = sc.ok() && shellOk && log.refused() == 0 && tee.mismatches() == 0 && cn.sinkDropped == 0 &&
	                (uint64_t)VPTrigLoad64(&srv->droppedLate) == cn.droppedLate + cn.droppedLateLasting;
	std::printf("trigger_scene: %s end=%s elapsed=%.3f records=%zu names=%zu scripts=%zu loopFlagChanged=%zu %s droppedLate=%llu "
	            "droppedLateLasting=%llu startedLate=%llu exeClass=%llu exeCodes=%s noFight=%llu unknownScript=%llu unresolved=%llu "
	            "noWeapon=%llu noPosition=%llu victoryTail=%llu loopsStarted=%llu renewed=%llu heartbeat=%llu loopsOwnerEnd=%llu "
	            "effectsEnded=%llu effectsOwnerEnd=%llu actions=%llu transfers=%llu loopLife=%.3f/%.3f/%.3f shellRead=%llu lost=%lld "
	            "soundTriggers=%llu soundWithDuration=%llu soundDurationMax=%.3f droppedTier=%llu\n",
	            ok ? "OK" : "FAIL", endReason < 6 ? srvunits::fightEndName((srvunits::FightEnd)endReason) : "?", endElapsed,
	            back.records.size(), back.names.size(), scripts.size(), loopChanged, trigchecks::summary(sc).c_str(),
	            (unsigned long long)cn.droppedLate, (unsigned long long)cn.droppedLateLasting, (unsigned long long)cn.startedLate,
	            (unsigned long long)cn.exeClass, codes.empty() ? "-" : codes.c_str(), (unsigned long long)cn.noFight,
	            (unsigned long long)cn.unknownScript, (unsigned long long)cn.unresolved, (unsigned long long)subs.noWeapon,
	            (unsigned long long)cn.noPosition, (unsigned long long)cn.victoryTail, (unsigned long long)cn.loopsStarted,
	            (unsigned long long)cn.loopsRenewed, (unsigned long long)cn.loopsHeartbeat, (unsigned long long)cn.loopsOwnerEnd,
	            (unsigned long long)cn.effectsEnded, (unsigned long long)cn.effectsOwnerEnd, (unsigned long long)cn.actions,
	            (unsigned long long)cn.transfers, lives.empty() ? 0.0 : lives.front(), lives.empty() ? 0.0 : lives[lives.size() / 2],
	            lives.empty() ? 0.0 : lives.back(), (unsigned long long)shellRead, (long long)cur.lost,
	            (unsigned long long)soundTrig, (unsigned long long)soundDur, soundDurMax, (unsigned long long)cn.droppedTier);
	{
		// 5052: the two knobs, what they did, and the tags a check needs (each side's figure-0 sound tag, the defender's
		// plot tag by PlotTags; "-" when not known). Names from the database: this line stays in the run's output, not git.
		auto soundTag = [&](const CombatUnitFigures& f) -> std::string {
			for(const CombatMemberArt& m : f.members)
				if(m.count > 0)
				{
					auto w = subs.weapons.find(m.member);
					if(w == subs.weapons.end())
						return "-";
					return !w->second.sound.empty() ? w->second.sound : w->second.tag.empty() ? "-" : w->second.tag;
				}
			return "-";
		};
		std::string defPlot = "-";
		if(subs.plotTerrain && D.city < 0)
		{
			auto it = subs.plotOf.find(std::make_pair(D.player, D.id));
			if(it != subs.plotOf.end() && it->second >= 0 && (size_t)it->second < subs.plots.size())
				subs.plotTags.tagOf(subs.plots[(size_t)it->second].terrain, subs.plots[(size_t)it->second].feature, defPlot);
		}
		std::printf("names: strikerWeapon=%d triggerTerrain=%d strikerNamed=%llu strikerCity=%llu plotFeature=%llu plotTerrain=%llu "
		            "plotDirt=%llu plotNoTable=%llu attSound=%s defSound=%s defPlot=%s strikerMelee=%d strikerFallback=%llu\n",
		            subs.strikerWeapon ? 1 : 0, subs.plotTerrain ? 1 : 0, (unsigned long long)subs.strikerNamed,
		            (unsigned long long)subs.strikerCity, (unsigned long long)subs.plotFeature, (unsigned long long)subs.plotTerr,
		            (unsigned long long)subs.plotDirt, (unsigned long long)subs.plotNoTable, soundTag(fa).c_str(),
		            D.city < 0 ? soundTag(fd).c_str() : "-", defPlot.c_str(), subs.strikerMelee ? 1 : 0,
		            (unsigned long long)subs.strikerFallback);
		if(subs.cityEra && cityEraArg >= 0)   // 5058: only with the era given (the line as before otherwise)
			std::printf("cityera: era=%d named=%llu\n", (int)cityEraArg, (unsigned long long)subs.strikerCityEra);
		if(A.city >= 0)   // 5062: only for a city attacker (the output as before otherwise)
		{
			size_t cityHits = 0;
			for(const CombatHit& h : player.run.result().hits)
				cityHits += h.city ? 1 : 0;
			std::printf("cityshot: on=%d era=%d effect=%s cityHits=%zu\n", cityShotOn ? 1 : 0, (int)cityEraArg,
			            cityShot.empty() ? "-" : cityShot.c_str(), cityHits);
		}
	}
	{
		// 4735: the turns the core made and the run loops sent (a loop of a movement clip), per side
		size_t turns = 0;
		for(const FigureMove& mv : player.run.result().moves)
			turns += mv.turn ? 1 : 0;
		uint64_t runA = 0, runD = 0;
		for(auto& r : back.records)
			if(r.kind == VPTRIG_SOUND_START && (r.flags & VPTRIG_FLAG_LOOP) &&
			   ((r.ec >= 1400 && r.ec <= 1499) || (r.ec >= 2400 && r.ec <= 2499)))
				(r.player == A.player && r.unit == A.id ? runA : runD)++;
		std::printf("turn: turnFirst=%d turns=%zu runLoops=%llu runLoopsAtt=%llu runLoopsDef=%llu\n", turnFirst ? 1 : 0, turns,
		            (unsigned long long)(runA + runD), (unsigned long long)runA, (unsigned long long)runD);
	}
	{
		// 4742: the walk home inside the fight: the walkers per side (Done steps that request the run 1400), the walk's start
		// and last arrival (the walk moves' end), the refaces (Reface steps), the core's end and the fight's OwnerEnd (its
		// record's frame, on the fight's clock), the run loops (SoundStart with the loop flag of a movement clip) started
		// from the walk's start up to the OwnerEnd, loop starts after the OwnerEnd, and the loops no record ended (D210: 0)
		const CombatResult& wr = player.run.result();
		size_t walkA = 0, walkD = 0, refaces = 0;
		double wstart = -1, warrive = -1;
		for(const FigureStep& s : wr.steps)
		{
			if(s.state == FigState::Done && s.code == 1400)
			{
				(s.player == A.player && s.id == A.id ? walkA : walkD)++;
				if(wstart < 0 || s.time < wstart)
					wstart = s.time;
				for(const FigureMove& mv : wr.moves)
					if(!mv.turn && mv.player == s.player && mv.id == s.id && mv.figure == s.figure && mv.t0 == s.time)
					{
						warrive = std::max(warrive, mv.t1);
						break;
					}
			}
			if(s.state == FigState::Reface)
				++refaces;
		}
		int64_t endF = -1;
		for(auto& r : back.records)
			if(r.kind == VPTRIG_OWNER_END && VPTrigIsFightEnd(r))
			{
				endF = r.frameId;
				break;
			}
		auto tOf = [&](int64_t fr) {
			auto it = frameT.upper_bound(fr);
			if(it == frameT.begin())
				return 0.0;
			--it;
			return it->second;
		};
		uint64_t walkLoops = 0, loopsAfter = 0;
		for(auto& r : back.records)
		{
			if(r.kind != VPTRIG_SOUND_START || !(r.flags & VPTRIG_FLAG_LOOP))
				continue;
			if(endF >= 0 && r.frameId > endF)
			{
				++loopsAfter;
				continue;
			}
			const bool move = (r.ec >= 1400 && r.ec <= 1499) || (r.ec >= 2400 && r.ec <= 2499);
			if(move && wstart >= 0 && tOf(r.frameId) + 1e-9 >= wstart)
				++walkLoops;
		}
		std::printf("walk: walkHome=%d walkAtt=%zu walkDef=%zu start=%.3f arrive=%.3f refaces=%zu end=%.3f ownerEnd=%.3f "
		            "walkLoops=%llu loopsAfterEnd=%llu loopsOpen=%zu\n", walkHome ? 1 : 0, walkA, walkD, wstart, warrive, refaces,
		            wr.end, endF >= 0 ? tOf(endF) : -1.0, (unsigned long long)walkLoops, (unsigned long long)loopsAfter,
		            loopStart.size());
	}
	{
		// 4745 + 4747: the reform runs per side (FigState::Reform steps), the last reform arrival (its move's end), the first
		// strike (a Strike step) and the first landing of either side; -1 when none
		const CombatResult& rr = player.run.result();
		size_t refA = 0, refD = 0;
		double arrive = -1, strike = -1, landing = -1;
		for(const FigureStep& s : rr.steps)
		{
			if(s.state == FigState::Reform)
			{
				(s.player == A.player && s.id == A.id ? refA : refD)++;
				for(const FigureMove& mv : rr.moves)
					if(!mv.turn && mv.player == s.player && mv.id == s.id && mv.figure == s.figure && mv.t0 == s.time)
					{
						arrive = std::max(arrive, mv.t1);
						break;
					}
			}
			if(s.state == FigState::Strike && strike < 0)
				strike = s.time;
		}
		for(const CombatHit& h : rr.hits)
			if(landing < 0 || h.time < landing)
				landing = h.time;
		std::printf("reform: reformFirst=%d reformAtt=%zu reformDef=%zu arrive=%.3f firstStrike=%.3f firstLanding=%.3f "
		            "end=%.3f\n", reformFirst ? 1 : 0, refA, refD, arrive, strike, landing, rr.end);
		if(quick.on() || quickReport)
		{
			// 0796: the run-up's end as the rule sees it (the first Strike step, a city shot's launch or the first landing),
			// the first landing, the fight time the rate reached quick.rate, the fight's end (fight time) and its real length
			double runUp = strike;
			for(const CombatHit& h : rr.launches)
				if(runUp < 0 || h.launch < runUp)
					runUp = h.launch;
			if(landing >= 0 && (runUp < 0 || landing < runUp))
				runUp = landing;
			std::printf("quick: rate=%g pre=%g react=%g firstStrike=%.3f firstLanding=%.3f rateFrom=%.3f fightEnd=%.3f "
			            "wallEnd=%.3f atLimit=%d endReason=%d transfersAtRate=%llu\n", quick.on() ? quick.rate : 1.0,
			            quick.on() ? quick.pre_rate : 1.0, quick.react, runUp, landing, player.rateFrom, endElapsed, quickWallEnd,
			            rr.at_limit ? 1 : 0, (int)endReason, (unsigned long long)quickLateTransfers);
		}
		if(dumpCore)
		{
			if(FILE* f = std::fopen((out + "/steps.csv").c_str(), "w"))
			{
				std::fprintf(f, "time,player,id,figure,state,code,target_player,target_id,target_figure,approach\n");
				for(const FigureStep& s : rr.steps)
					std::fprintf(f, "%.4f,%d,%d,%u,%s,%d,%d,%d,%d,%u\n", s.time, s.player, s.id, (unsigned)s.figure,
					             figStateName(s.state), s.code, s.target_player, s.target_id, s.target_figure, (unsigned)s.approach);
				std::fclose(f);
			}
			if(FILE* f = std::fopen((out + "/moves.csv").c_str(), "w"))
			{
				std::fprintf(f, "player,id,figure,code,t0,t1,x0,y0,x1,y1,turn\n");
				for(const FigureMove& m : rr.moves)
					std::fprintf(f, "%d,%d,%u,%d,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%d\n", m.player, m.id, (unsigned)m.figure, m.code, m.t0,
					             m.t1, m.x0, m.y0, m.x1, m.y1, m.turn ? 1 : 0);
				std::fclose(f);
			}
			if(FILE* f = std::fopen((out + "/hits.csv").c_str(), "w"))
			{
				std::fprintf(f, "time,launch,player,id,figure,code,target_player,target_id,target_figure,result\n");
				for(const CombatHit& h : rr.hits)
					std::fprintf(f, "%.4f,%.4f,%d,%d,%u,%d,%d,%d,%d,%d\n", h.time, h.launch, h.player, h.id, (unsigned)h.figure,
					             h.code, h.target_player, h.target_id, h.target_figure, (int)h.result);
				std::fclose(f);
			}
		}
	}
	{
		// 4746: the combat-ready entries (1600) the core requested, per state, and the Wait steps among them that break
		// stock's rule (exe-audio.md 13.3 [C]: a waiting figure requests 1600 only when its last request is a movement,
		// 1400 / 1450 / 1120, and its member enables Walk 1450; state 5 always requests 1600); and the one-shot SoundStarts
		// (no loop flag) by their clip's code: combat ready (1600: FOOT_ALWAYS, FORTIFY_VOX), bombard defend (1280), movement
		// (1400-1499, 2400-2499), run charge (1120), strike (the other 11xx), death (12xx), victory (18xx), the rest
		const CombatResult& qr = player.run.result();
		auto walks = [&](int32_t pl, int32_t id, uint16_t fig) {
			const CombatUnitFigures* u = pl == A.player && id == A.id ? &fa : (pl == D.player && id == D.id ? &fd : nullptr);
			if(!u)
				return false;
			uint32_t first = 0;
			for(const CombatMemberArt& m : u->members)
			{
				if(fig < first + m.count)
					return m.enable.count("Walk") != 0;
				first += m.count;
			}
			return false;
		};
		std::map<std::tuple<int32_t, int32_t, uint16_t>, int32_t> lastCode;
		size_t waitReady = 0, waitAgainst = 0, waitAgainstAtt = 0, waitAgainstDef = 0, readyReady = 0;
		for(const FigureStep& s : qr.steps)
		{
			const auto k = std::make_tuple(s.player, s.id, s.figure);
			const auto it = lastCode.find(k);
			const int32_t prev = it == lastCode.end() ? 1000 : it->second;
			if(s.code == 1600 && s.state == FigState::Ready)
				++readyReady;
			if(s.code == 1600 && s.state == FigState::Wait)
			{
				++waitReady;
				if(!((prev == 1400 || prev == 1450 || prev == 1120) && walks(s.player, s.id, s.figure)))
				{
					++waitAgainst;
					(s.player == A.player && s.id == A.id ? waitAgainstAtt : waitAgainstDef)++;
				}
			}
			if(s.code != 0)
				lastCode[k] = s.code;
		}
		uint64_t os = 0, osReady = 0, osDefend = 0, osMove = 0, osCharge = 0, osStrike = 0, osDeath = 0, osVictory = 0, osOther = 0;
		for(auto& r : back.records)
		{
			if(r.kind != VPTRIG_SOUND_START || (r.flags & VPTRIG_FLAG_LOOP))
				continue;
			++os;
			const int32_t ec = r.ec;
			if(ec == 1600)
				++osReady;
			else if(ec == 1280)
				++osDefend;
			else if((ec >= 1400 && ec <= 1499) || (ec >= 2400 && ec <= 2499))
				++osMove;
			else if(ec == 1120)
				++osCharge;
			else if(ec >= 1100 && ec <= 1199)
				++osStrike;
			else if(ec >= 1200 && ec <= 1299)
				++osDeath;
			else if(ec >= 1800 && ec <= 1899)
				++osVictory;
			else
				++osOther;
		}
		std::printf("ready: readyAfterMove=%d waitReady=%zu waitAgainstStock=%zu waitAgainstAtt=%zu waitAgainstDef=%zu readyReady=%zu oneShots=%llu "
		            "osReady=%llu osDefend=%llu osMove=%llu osCharge=%llu osStrike=%llu osDeath=%llu osVictory=%llu osOther=%llu end=%.3f\n",
		            readyAfterMove ? 1 : 0, waitReady, waitAgainst, waitAgainstAtt, waitAgainstDef, readyReady, (unsigned long long)os,
		            (unsigned long long)osReady, (unsigned long long)osDefend, (unsigned long long)osMove, (unsigned long long)osCharge,
		            (unsigned long long)osStrike, (unsigned long long)osDeath, (unsigned long long)osVictory,
		            (unsigned long long)osOther, qr.end);
		// 4750: the defender's bombard defend, and whether its figures meet 1280 from idle (path 6 = 0, the clip that
		// sounds) or after a movement (the stance, a turn: path 6 = 1, the silent 1285 loop) [unit-combat-server.md 10.14]
		size_t stances = 0, defend1280 = 0, defendFromIdle = 0;
		std::map<std::tuple<int32_t, int32_t, uint16_t>, int32_t> prevCode;
		for(const FigureStep& s : qr.steps)
		{
			const auto k = std::make_tuple(s.player, s.id, s.figure);
			const auto it = prevCode.find(k);
			const int32_t prev = it == prevCode.end() ? 1000 : it->second;
			if(s.state == FigState::Defend && s.code == 1450)
				++stances;
			if(s.state == FigState::Defend && s.code == 1280)
			{
				++defend1280;
				if(!(prev == 1400 || prev == 1450 || prev == 1120))
					++defendFromIdle;
			}
			if(s.code != 0)
				prevCode[k] = s.code;
		}
		std::printf("defend: defendFromStance=%d stances=%zu defend1280=%zu defendFromIdle=%zu osDefend=%llu end=%.3f\n",
		            defendFromStance ? 1 : 0, stances, defend1280, defendFromIdle, (unsigned long long)osDefend, qr.end);
		// 5050: stock's phases 1 + 2 in the core. p1: the first step of any figure that is not the reform (Reform, or
		// Look with 1000 / nothing): phase 1's end. ready: the attacker's first 1600 (Ready) request; p2 = ready - p1
		// (the attacker's phase-2 stance, 0 without it). astances: the attacker's stance steps (Turn with 1450);
		// attReadyFromIdle: the attacker's 1600 requests made when its last request was not a movement (the machine's
		// enter-combat-ready clip, which sounds); firstStrike: the attacker's first Strike step [unit-combat-server.md 18.23]
		double p1 = -1, attReady = -1, firstStrike = -1;
		size_t astances = 0, aholds = 0, attReady1600 = 0, attReadyFromIdle = 0;   // aholds: Turn with no request (no Walk)
		std::map<std::tuple<int32_t, int32_t, uint16_t>, int32_t> prevPh;
		for(const FigureStep& s : qr.steps)
		{
			const auto k = std::make_tuple(s.player, s.id, s.figure);
			const auto it = prevPh.find(k);
			const int32_t prev = it == prevPh.end() ? 1000 : it->second;
			const bool att = s.player == A.player && s.id == A.id;
			const bool reformStep = s.state == FigState::Reform || (s.state == FigState::Look && (s.code == 1000 || s.code == 0));
			if(p1 < 0 && !reformStep)
				p1 = s.time;
			if(att && s.state == FigState::Turn && s.code == 1450)
				++astances;
			if(att && s.state == FigState::Turn && s.code == 0)
				++aholds;
			if(att && s.state == FigState::Ready && s.code == 1600)
			{
				++attReady1600;
				if(attReady < 0)
					attReady = s.time;
				if(!(prev == 1400 || prev == 1450 || prev == 1120))
					++attReadyFromIdle;
			}
			if(att && s.state == FigState::Strike && firstStrike < 0)
				firstStrike = s.time;
			if(s.code != 0)
				prevPh[k] = s.code;
		}
		std::printf("phases: reformEndsAtArrival=%d attackFromStance=%d p1=%.3f p2=%.3f attReady=%.3f firstStrike=%.3f "
		            "astances=%zu aholds=%zu attReady1600=%zu attReadyFromIdle=%zu osReady=%llu end=%.3f\n",
		            reformEndsAtArrival ? 1 : 0, attackFromStance ? 1 : 0, p1, (p1 >= 0 && attReady >= 0) ? attReady - p1 : -1.0,
		            attReady, firstStrike, astances, aholds, attReady1600, attReadyFromIdle, (unsigned long long)osReady, qr.end);
	}
	{
		// 5057: the ride-through and the type 2 wait (unit-combat-server.md 18.29.1)
		const CombatResult& vr = player.run.result();
		size_t rideLandings = 0, rideOns = 0, rideStops = 0, att1600 = 0, readyBeforeHit = 0;
		double defFirst = -1, attFirst = -1;
		std::map<uint16_t, double> firstHit, first1600;
		for(const CombatHit& h : vr.hits)
		{
			const bool att = h.player == A.player && h.id == A.id && !h.city;
			if(att && h.code == 1120)
				++rideLandings;
			if(att && (attFirst < 0 || h.time < attFirst))
				attFirst = h.time;
			if(!att && h.player == D.player && h.id == D.id && (defFirst < 0 || h.time < defFirst))
				defFirst = h.time;
			if(att && !firstHit.count(h.figure))
				firstHit[h.figure] = h.time;
		}
		std::map<uint16_t, int32_t> prevR;
		for(const FigureStep& s : vr.steps)
		{
			if(!(s.player == A.player && s.id == A.id))
				continue;
			if(s.state == FigState::Charge && s.code == 0)
				++rideOns;
			if(s.state == FigState::Wait && s.code == 1000 && prevR.count(s.figure) && prevR[s.figure] == 1120)
				++rideStops;
			if(s.code == 1600)
			{
				++att1600;
				if(!first1600.count(s.figure))
					first1600[s.figure] = s.time;
			}
			if(s.code != 0)
				prevR[s.figure] = s.code;
		}
		for(auto& kv : first1600)
		{
			const auto it = firstHit.find(kv.first);
			if(it == firstHit.end() || kv.second < it->second)
				++readyBeforeHit;
		}
		std::printf("ride: rushRideThrough=%d defenderWaits=%d rideLandings=%zu rideOns=%zu rideStops=%zu att1600=%zu "
		            "readyBeforeHit=%zu defFirstLanding=%.3f attFirstLanding=%.3f end=%.3f\n", rushRideThrough ? 1 : 0,
		            defenderWaits ? 1 : 0, rideLandings, rideOns, rideStops, att1600, readyBeforeHit, defFirst, attFirst, vr.end);
		// 5071: the riders' strikes after the ride (CombatParams::ride_strikes_while_held, unit-combat-server.md 18.46):
		// the attacker's hits with an attack clip (1100 / 1140: each plays the clip's swing and attack vox) and those of
		// 1140 (a strike from the run), the riders' stops into combat ready from the run (Wait with 1600 after a 1120),
		// the attacker's last such hit, and the deaths of each side (the bar: equal to the off arm's)
		size_t attStrikeHits = 0, att1140 = 0, rideStops1600 = 0, attDeaths = 0, defDeaths = 0;
		double attLastStrike = -1;
		std::map<uint16_t, int32_t> prevH;
		for(const CombatHit& h : vr.hits)
		{
			if(h.result == HitResult::Death)
				++((h.target_player == A.player && h.target_id == A.id) ? attDeaths : defDeaths);
			if(h.player == A.player && h.id == A.id && !h.city && (h.code == 1100 || h.code == 1140))
			{
				++attStrikeHits;
				att1140 += h.code == 1140 ? 1 : 0;
				attLastStrike = std::max(attLastStrike, h.time);
			}
		}
		for(const FigureStep& s : vr.steps)
		{
			if(!(s.player == A.player && s.id == A.id))
				continue;
			if(s.state == FigState::Wait && s.code == 1600 && prevH.count(s.figure) && prevH[s.figure] == 1120)
				++rideStops1600;
			if(s.code != 0)
				prevH[s.figure] = s.code;
		}
		std::printf("held: rideStrikesWhileHeld=%d attStrikeHits=%zu att1140=%zu rideStops1600=%zu attLastStrike=%.3f "
		            "attDeaths=%zu defDeaths=%zu end=%.3f\n", rideStrikesWhileHeld ? 1 : 0, attStrikeHits, att1140,
		            rideStops1600, attLastStrike, attDeaths, defDeaths, vr.end);
		// 5067 S1: the type 2 phase 2 (unit-combat-server.md 18.29.3): the attacker's first movement request (1120 / 1400 /
		// 1450), the defender's phase-2 stances (Turn with 1450 before that) and the attacker's first landing
		double attFirstMove = -1;
		size_t defStances = 0;
		for(const FigureStep& s : vr.steps)
		{
			const bool att = s.player == A.player && s.id == A.id;
			if(att && attFirstMove < 0 && (s.code == 1120 || s.code == 1400 || s.code == 1450))
				attFirstMove = s.time;
			if(!att && s.player == D.player && s.id == D.id && s.state == FigState::Turn && s.code == 1450 &&
			   (attFirstMove < 0 || s.time <= attFirstMove))
				++defStances;
		}
		std::printf("type2: type2Phase2=%d attFirstMove=%.3f defStances=%zu attFirstLanding=%.3f end=%.3f\n",
		            type2Phase2 ? 1 : 0, attFirstMove, defStances, attFirst, vr.end);
		// 5061: the type 2 defender's step (unit-combat-server.md 18.45): its approaches and charges (not the walk home),
		// the figures that made one, the first one's time and the longest run
		size_t defMoves = 0;
		std::set<uint16_t> defMovers;
		double defFirstMove = -1, defMaxRun = 0;
		for(const FigureStep& s : vr.steps)
		{
			if(!(s.player == D.player && s.id == D.id) || (s.state != FigState::Approach && s.state != FigState::Charge))
				continue;
			++defMoves;
			defMovers.insert(s.figure);
			if(defFirstMove < 0 || s.time < defFirstMove)
				defFirstMove = s.time;
			for(const FigureMove& mv : vr.moves)
				if(mv.player == D.player && mv.id == D.id && mv.figure == s.figure && std::fabs(mv.t0 - s.time) < 1e-9)
					defMaxRun = std::max(defMaxRun, static_cast<double>(std::hypot(mv.x1 - mv.x0, mv.y1 - mv.y0)));
		}
		std::printf("defstep: defenderStep=%d defMoves=%zu defMovers=%zu defFirstMove=%.3f defMaxRun=%.2f "
		            "attFirstLanding=%.3f end=%.3f\n", defenderStep ? 1 : 0, defMoves, defMovers.size(), defFirstMove,
		            defMaxRun, attFirst, vr.end);
	}
	{
		// 5053: the fight's end after the deaths (unit-combat-server.md 18.26)
		const CombatResult& er = player.run.result();
		size_t deaths = 0, shuffles = 0, deathB = 0, deathA = 0;
		double lastDeath = -1, lastRemoved = -1, release = -1, lastReface = -1, dyingMin = -1, dyingMax = -1;
		std::map<std::tuple<int32_t, int32_t, uint16_t>, double> dyingAt;
		std::set<std::tuple<int32_t, int32_t, uint16_t>> shuffled;
		for(const FigureStep& s : er.steps)
		{
			const auto k = std::make_tuple(s.player, s.id, s.figure);
			const bool att = s.player == A.player && s.id == A.id;
			if(s.state == FigState::Dying)
			{
				++deaths;
				lastDeath = std::max(lastDeath, s.time);
				dyingAt[k] = s.time;
			}
			if(s.state == FigState::Removed)
			{
				lastRemoved = std::max(lastRemoved, s.time);
				const auto it = dyingAt.find(k);
				if(it != dyingAt.end())
				{
					const double d = s.time - it->second;
					dyingMin = dyingMin < 0 ? d : std::min(dyingMin, d);
					dyingMax = std::max(dyingMax, d);
				}
			}
			if(!att && (s.state == FigState::Done || s.state == FigState::Victory) && (release < 0 || s.time < release))
				release = s.time;
			if(!att && s.state == FigState::Done && s.code == 1450)
			{
				++shuffles;
				shuffled.insert(k);
			}
			if(!att && s.state == FigState::Reface && shuffled.count(k))
				lastReface = std::max(lastReface, s.time);
		}
		for(const auto& kv : fb.seen)
			if(std::get<3>(kv.first) == 1200)
				++(kv.second == 2200 ? deathB : deathA);
		std::printf("end: deathByVariant=%d endAfterDying=%d deaths=%zu lastDeath=%.3f lastRemoved=%.3f release=%.3f "
		            "shuffles=%zu shuffleReface=%.3f dyingMin=%.3f dyingMax=%.3f deathB=%zu deathA=%zu end=%.3f\n",
		            deathByVariant ? 1 : 0, endAfterDying ? 1 : 0, deaths, lastDeath, lastRemoved, release, shuffles,
		            lastReface, dyingMin, dyingMax, deathB, deathA, er.end);
	}
	{
		// 5059: victory (unit-combat-server.md 18.36): the figures that took victory per side (a Victory step), and the
		// SoundStart records of a victory clip (code 1800: the victory vox among them); stock: 0 in VP
		const CombatResult& vr = player.run.result();
		std::set<std::tuple<int32_t, int32_t, uint16_t>> vfigs;
		size_t vAtt = 0, vDef = 0;
		for(const FigureStep& s : vr.steps)
			if(s.state == FigState::Victory && vfigs.insert(std::make_tuple(s.player, s.id, s.figure)).second)
				(s.player == A.player && s.id == A.id ? vAtt : vDef)++;
		uint64_t vSounds = 0;
		for(auto& r : back.records)
			if(r.kind == VPTRIG_SOUND_START && r.ec == 1800)
				++vSounds;
		std::printf("victory: victoryWord=%d victoryFigs=%zu victoryAtt=%zu victoryDef=%zu victorySounds=%llu\n",
		            victoryWord ? 1 : 0, vfigs.size(), vAtt, vDef, (unsigned long long)vSounds);
	}
	{
		// 5072: the attacker's tail (CombatParams::type2_end_after_dying, unit-combat-server.md 18.48): its first finish
		// step (Done or Victory) against the last removal of the fight, its home stops (Reface 1000 after its 1400 walk:
		// the run loop's 1440) and those before the end, its stops from the run (Wait 1000 after a 1120), its run stop
		// sounds sent (SoundStart of ec 1440 / 2440; a stop at the end plays its sounds after the end, which are not sent)
		const CombatResult& tr = player.run.result();
		double attRelease = -1, lastRemoved = -1;
		size_t homeStops = 0, homeBeforeEnd = 0, runStops = 0, attDeaths = 0, defDeaths = 0;
		std::map<uint16_t, int32_t> prevT;
		for(const FigureStep& s : tr.steps)
		{
			if(s.state == FigState::Removed)
				lastRemoved = std::max(lastRemoved, s.time);
			if(s.state == FigState::Dying)
				++((s.player == A.player && s.id == A.id) ? attDeaths : defDeaths);
			if(!(s.player == A.player && s.id == A.id))
				continue;
			if((s.state == FigState::Done || s.state == FigState::Victory) && (attRelease < 0 || s.time < attRelease))
				attRelease = s.time;
			const auto it = prevT.find(s.figure);
			const int32_t prev = it == prevT.end() ? 0 : it->second;
			if(s.state == FigState::Reface && s.code == 1000 && prev == 1400)
			{
				++homeStops;
				homeBeforeEnd += s.time + 1e-6 < tr.end ? 1 : 0;
			}
			if(s.state == FigState::Wait && s.code == 1000 && prev == 1120)
				++runStops;
			if(s.code != 0)
				prevT[s.figure] = s.code;
		}
		uint64_t stopSounds = 0;
		for(auto& r : back.records)
			if(r.kind == VPTRIG_SOUND_START && r.player == A.player && r.unit == A.id && (r.ec == 1440 || r.ec == 2440))
				++stopSounds;
		std::printf("tail: type2EndAfterDying=%d attRelease=%.3f lastRemoved=%.3f homeStops=%zu homeBeforeEnd=%zu "
		            "runStops=%zu attStopSounds=%llu attDeaths=%zu defDeaths=%zu end=%.3f\n", type2EndAfterDying ? 1 : 0,
		            attRelease, lastRemoved, homeStops, homeBeforeEnd, runStops, (unsigned long long)stopSounds, attDeaths,
		            defDeaths, tr.end);
	}
	bool effectsOk = true;
	if(routes)
	{
		// 4793: every effect name in the log is defined in the install's library; counts per route
		uint64_t effStart = 0, effMove = 0, effStop = 0, undefined = 0, oneShotStops = 0, follows = 0;
		std::set<uint32_t> started;
		for(auto& r : back.records)
		{
			if(r.kind == VPTRIG_EFFECT_START)
			{
				++effStart;
				started.insert(r.handle);
				if(r.flags & VPTRIG_FLAG_FOLLOWS_OWNER)
					++follows;
				const std::string n = r.name >= 1 && r.name <= back.names.size() ? back.names[r.name - 1].text : std::string();
				if(!fxnames->defined(n))
					++undefined;
			}
			if(r.kind == VPTRIG_EFFECT_MOVE)
				++effMove;
			if(r.kind == VPTRIG_EFFECT_STOP)
			{
				++effStop;
				if(!started.count(r.handle))
					++oneShotStops;
			}
		}
		const RouteCounters& rc = routes->counters();
		const c5r::effects::NameStats& ns = fxnames->stats();
		effectsOk = undefined == 0 && oneShotStops == 0 && rc.recRefused == 0;
		std::printf("effects: %s EffectStart=%llu follows=%llu EffectMove=%llu EffectStop=%llu undefined=%llu effectTriggers=%llu "
		            "exact=%llu fallback=%llu weaponEmpty=%llu refused=%llu held=%llu heldTaken=%llu heldExpired=%llu "
		            "heldRefused=%llu transfers=%llu planned=%llu noHeld=%llu plannerRefused=%llu hits=%llu oneShot=%llu flight=%llu "
		            "cityVolley=%llu cityDefence=%llu hitNone=%llu hitRefused=%llu hitLate=%llu hitSounds=%llu hitSoundNotScript=%llu "
		            "flights=%llu forced=%llu recStart=%llu recMove=%llu recStop=%llu recChild=%llu recHit=%llu recRefused=%llu "
		            "namesFellBack=%zu\n",
		            effectsOk ? "OK" : "FAIL", (unsigned long long)effStart, (unsigned long long)follows, (unsigned long long)effMove,
		            (unsigned long long)effStop, (unsigned long long)undefined, (unsigned long long)rc.effectTriggers,
		            (unsigned long long)rc.exact, (unsigned long long)rc.fallback, (unsigned long long)rc.weaponEmpty,
		            (unsigned long long)rc.refused, (unsigned long long)rc.held, (unsigned long long)rc.heldTaken,
		            (unsigned long long)rc.heldExpired, (unsigned long long)rc.heldRefused, (unsigned long long)rc.transfers,
		            (unsigned long long)rc.transfersPlanned, (unsigned long long)rc.transfersNoHeld, (unsigned long long)rc.plannerRefused,
		            (unsigned long long)rc.hits, (unsigned long long)rc.hitOneShot, (unsigned long long)rc.hitFlight,
		            (unsigned long long)rc.hitCityVolley, (unsigned long long)rc.hitCityDefence, (unsigned long long)rc.hitNone,
		            (unsigned long long)rc.hitRefused, (unsigned long long)rc.hitLate, (unsigned long long)rc.hitSounds,
		            (unsigned long long)rc.hitSoundNotScript, (unsigned long long)rc.flightsAdded, (unsigned long long)rc.flightsForced,
		            (unsigned long long)rc.recStart, (unsigned long long)rc.recMove, (unsigned long long)rc.recStop,
		            (unsigned long long)rc.recChild, (unsigned long long)rc.recHit, (unsigned long long)rc.recRefused, ns.fell_back.size());
		if(geo)
		{
			// 4728: the server's geometry: poses, bones, the planner with the core's target
			const GeometryCounters& gc = geo->counters();
			const c5r::effects::BonePoseCounts* bc = geo->boneCounts();
			std::printf("geometry: figureAsks=%llu figurePosed=%llu figureNoMotion=%llu boneAsks=%llu boneOk=%llu boneNoFigure=%llu "
			            "boneNoArt=%llu boneNoProgram=%llu boneRefused=%llu boneTransforms=%llu plans=%llu planOk=%llu planCity=%llu "
			            "planStepTarget=%llu planOpponent=%llu planNoTarget=%llu planNoWeapon=%llu planNoArt=%llu planRefused=%llu "
			            "planNoTangent=%llu poserModels=%zu poserBlends=%zu poserNoBone=%zu poserModelFails=%zu poserClipFails=%zu "
			            "boneDistMean=%.2f boneDistMax=%.2f boneDzMin=%.2f boneDzMax=%.2f\n",
			            (unsigned long long)gc.figureAsks, (unsigned long long)gc.figurePosed, (unsigned long long)gc.figureNoMotion,
			            (unsigned long long)gc.boneAsks, (unsigned long long)gc.boneOk, (unsigned long long)gc.boneNoFigure,
			            (unsigned long long)gc.boneNoArt, (unsigned long long)gc.boneNoProgram, (unsigned long long)gc.boneRefused,
			            (unsigned long long)cn.boneTransforms, (unsigned long long)gc.plans, (unsigned long long)gc.planned,
			            (unsigned long long)gc.planCity, (unsigned long long)gc.planStepTarget, (unsigned long long)gc.planOpponent,
			            (unsigned long long)gc.planNoTarget, (unsigned long long)gc.planNoWeapon, (unsigned long long)gc.planNoArt,
			            (unsigned long long)gc.planRefused, (unsigned long long)gc.planNoTangent, bc ? bc->models : 0, bc ? bc->blends : 0,
			            bc ? bc->noBone : 0, bc ? bc->modelFails : 0, bc ? bc->clipFails : 0,
			            gc.boneOk ? gc.boneDistSum / (double)gc.boneOk : 0.0, gc.boneDistMax, gc.boneOk ? gc.boneHeightMin : 0.0,
			            gc.boneOk ? gc.boneHeightMax : 0.0);
			// a planned flight that the planner refused, or a bone of the scene's art that no skeleton has, fails the case
			effectsOk = effectsOk && gc.planRefused == 0 && gc.planNoWeapon == 0 && gc.planNoArt == 0 && (!bc || bc->modelFails == 0);
		}
		if(projectilePose)   // 5064: held starts posed and scaled, launchers without a heading (absent off: the output as before)
			std::printf("pose5064: heldBoneTransform=%llu heldNoBoneTransform=%llu heldScaleMember=%llu heldScaleOne=%llu "
			            "figureYawUnknown=%llu planYawUnknown=%llu planYawToTarget=%llu\n",
			            (unsigned long long)rc.heldBoneTransform, (unsigned long long)rc.heldNoBoneTransform,
			            (unsigned long long)rc.heldScaleMember, (unsigned long long)rc.heldScaleUnknown,
			            (unsigned long long)(geo ? geo->counters().figureYawUnknown : 0),
			            (unsigned long long)(geo ? geo->counters().planYawUnknown : 0),
			            (unsigned long long)(geo ? geo->counters().planYawToTarget : 0));
		for(auto& w : rc.why)
			std::printf("  effects why: %llu x %s\n", (unsigned long long)w.second, w.first.c_str());
		for(auto& w : rc.refusedNames)
			std::printf("  effects refused name: %llu x %s\n", (unsigned long long)w.second, w.first.c_str());
	}
	for(auto& f : sc.faults)
		std::printf("  fault: %s\n", f.c_str());
	if(log.refused())
		std::printf("  log refused %llu: %s\n", (unsigned long long)log.refused(), log.firstRefusal().c_str());
	(void)scriptRows;
	return ok && effectsOk ? 0 : 1;
}
