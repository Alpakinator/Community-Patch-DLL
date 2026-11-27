// srv_trigger_stream.h - the trigger stream as one object for a host (the server's UnitsHost, unitfeed's offline runs):
// the emitter, its sinks (the <base>.Trig section and / or the offline trigger log), the 3D scripts' loop flags read from
// the install and the database, $(WEAPON) from the database's weapon rows and the controller's members, the units' roots
// from the host's scene, and the tap the unit runner calls (epic 0042, ticket 4722; unit-combat-server.md 11.5).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "c5models/art.h"
#include "c5models/effect_xml.h"
#include "c5r/effects/effect_routes.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"
#include "srv_effect_routes.h"
#include "srv_fight_geometry.h"
#include "srv_trigger_emitter.h"
#include "srv_trigger_logsink.h"
#include "srv_trigger_tap.h"
#include "units/srv_units_core.h"

namespace srveffects {

struct TriggerStreamConfig
{
	std::wstring section;            // <base> of the frame section: the stream maps <base>.Trig ("" = no section)
	std::string logPath;             // the offline trigger log (D137; "" = none). Names from the install: outside git
	std::wstring install;            // the game install: its *3DScripts.xml files give the loop flags
	EmitterParams params;
	bool requireRouteS = true;       // R9 (ticket 4783): the section's records only while the shell answers ARMED
	// 4728: a fight's effects through 4793's routes (srv_effect_routes.h): the install's effect library (files and
	// provider: the host's GameFiles and its provider, both outliving the stream), the bones (strings: the .gr2 string
	// databases, may be null), 4792's flight planner with the core's target (srv_fight_geometry.h). effects false, or no
	// files / provider: no routes, the stream as 4722 built it (effect triggers go to the emitter unresolved).
	bool effects = true;
	const c5r::gamefs::GameFiles* files = nullptr;
	c5m::FileProvider* provider = nullptr;
	const c5m::StringDatabase* strings = nullptr;
	bool bones = true;               // false: effects start at the figure's position (no bone posing)
	RouteParams routes;
	// Ticket 5052 (rule A, unit-combat-server.md 18.25; strikerWeapon default ON since the live pair, MC #10847,
	// unit-combat-server.md 18.25.2; plotTerrain default ON since its live check, MC #11356, 18.25.4 / 18.25.5; was off,
	// MC #10565):
	// strikerWeapon (render_test_units_trigger_striker_weapon): a unit that does not strike in its running fight (the
	// defender when DefenderStrikes is false, srv_trigger_emitter.h) takes its opponent's figure-0 member weapon for
	// $(WEAPON) (the sound tag for a Sound, the weapon tag for an Effect); a city opponent leaves its own (counted). Off:
	// every figure its own member's weapon, the rule before 5052.
	bool strikerWeapon = true;
	// Ticket 5055 (with strikerWeapon; unit-combat-server.md 18.30): a Sound's $(WEAPON) is the striker's for every unit
	// struck in its running fight, as stock names both sides of a melee: the defender's by its attacker whether it strikes
	// back or not, the attacker's by its defender when the defender strikes back (FightWeaponRoles::namer's `struck`).
	// When that name is no script of the unit, the emitter tries the unit's own weapon (Substitutions::weaponFallback).
	// Effects keep 5052's rule. Its own key (MC #11607): render_units_trigger_striker_melee, default 1. It refines the
	// striker-weapon naming and is in effect only when strikerWeapon is on too (strikerMelee() below): strikerWeapon off is
	// the rule before 5052 whatever this says; false with strikerWeapon on (the key at 0, the offline tools'
	// --no-[trigger-]striker-melee) is 5052's rule exactly.
	bool strikerMelee = true;
	// Ticket 5058 (with strikerWeapon; unit-combat-server.md 18.34): a Sound's $(WEAPON) whose striker is a city takes the
	// city's weapon by its owner's era (CityWeaponTag: era 0 CITY_EARLY, 1 CITY_MID, 2 CITY_LATE, 3 and up CITY_EARLY,
	// stock's selector), the era read from the owner's player record at the fight's start (useCore); the unit's own weapon
	// is the emitter's second try when that name is no script of the unit. Era not known (no core, no record): the unit's
	// own as before. Host key render_units_trigger_city_era (default 1; absent in a bundle = 0). Off (the offline tools'
	// --no-[trigger-]city-era): the rule before 5058 exactly. Effects unchanged.
	bool cityEra = true;
	// plotTerrain (render_test_units_trigger_terrain): the Terrains / Features EffectTypeTag rows are read at open
	// (plotTags()) for the host's UnitTerrainFn (setUnitTerrain). Off: not read; $(TERRAIN) as before 5052 (the host's
	// function, else DIRT).
	bool plotTerrain = true;
};

// Ticket 5052 (T): the Terrains and Features rows' EffectTypeTag by ID. False with why (out then holds what was read).
bool loadPlotTags(c5m::TableSource& db, PlotTags& out, std::string& why);

// Ticket 5052: what the two knobs did (TriggerStream::nameCounters()).
struct NameCounters
{
	uint64_t strikerNamed = 0;       // (W) $(WEAPON) values taken from the opponent's figure 0
	uint64_t strikerCity = 0;        // (W) a non-striking unit whose opponent is a city: its own weapon kept
	uint64_t strikerNoWeapon = 0;    // (W) the opponent's figure 0 has no weapon row: the name is not sent
	uint64_t strikerFallback = 0;    // 5055: a striker-named Sound that was no script, tried with the unit's own weapon
	uint64_t strikerCityEra = 0;     // 5058: a Sound's $(WEAPON) given by a striking city's era (CityWeaponTag)
	uint64_t plotFeature = 0, plotTerrain = 0, plotDirt = 0;   // (T) the tag's source per lookup (PlotTags::tagOf)
	uint64_t plotNoTable = 0;        // (T) the host had no plot table (or the unit no plot): DIRT (also terrainStandIn)
};

// Where a unit stands (the host's scene) and what its plot is made of. Both may be unset: a unit then has no position
// (its records are not sent, counted noPosition) and $(TERRAIN) takes DIRT (counted terrainStandIn).
using UnitRootFn = std::function<bool(int32_t player, int32_t unit, float out[3])>;
using UnitTerrainFn = std::function<bool(int32_t player, int32_t unit, std::string& effectTypeTag)>;

class TriggerStream
{
public:
	TriggerStream();
	~TriggerStream();
	TriggerStream(const TriggerStream&) = delete;
	TriggerStream& operator=(const TriggerStream&) = delete;
	// Reads the loop flags and the weapon and combat tables, opens the sinks. False with why (nothing is sent then).
	bool open(const TriggerStreamConfig& cfg, c5m::TableSource& db, std::string& why);
	void setUnitRoot(UnitRootFn f) { root_ = std::move(f); }
	// The server's own roots: a unit's newest position (mode exe, UnitsCore::rootOf), else its plot's centre on the
	// frame's height grid (z = the grid's height there). Replaces setUnitRoot. The core outlives the stream; the grid is
	// the frame's (setGrid each frame; null: no position).
	void useCore(const srvunits::UnitsCore* core);
	void setGrid(const c5r::heightfield::HeightGrid* grid)
	{
		grid_ = grid;
		if(geo_)
			geo_->setGrid(grid);   // 4728
	}
	void setUnitTerrain(UnitTerrainFn f) { terrain_ = std::move(f); }
	AnimTriggerTap* tap() { return tap_.get(); }
	TriggerEmitter* emitter() { return em_.get(); }
	// The orderly stop: the section's magic cleared (R6), the log closed. False with why when the log could not be closed.
	bool close(std::string& why);
	size_t scripts() const { return scripts_.size(); }
	uint64_t terrainStandIn() const;
	// Ticket 5052 (T): a plot's $(TERRAIN) by stock's rule (PlotTags::tagOf on the tags read at open with
	// cfg.plotTerrain; counted in nameCounters()). Always true (DIRT when no tag; no tags read: DIRT).
	bool plotTag(int32_t terrainId, int32_t featureId, std::string& out);
	const PlotTags& plotTags() const { return plotTags_; }
	const std::string& plotTagsWhy() const { return plotTagsWhy_; }   // "" when read (or not asked for)
	NameCounters nameCounters() const;
	bool strikerWeapon() const { return cfg_.strikerWeapon; }
	bool strikerMelee() const { return cfg_.strikerWeapon && cfg_.strikerMelee; }   // 5055
	bool cityEra() const { return cfg_.strikerWeapon && cfg_.cityEra; }   // 5058
	bool plotTerrain() const { return cfg_.plotTerrain; }
	const SectionSink* section() const { return sec_.get(); }
	// R9 (ticket 4783): once per frame: the shell's answer read, the decision taken; the lines for the host's log.
	void pollRouteS(std::vector<std::string>& lines);
	// 4728: the effect routes (null: none; effectsWhy() says why when they were asked for), the geometry (always).
	EffectRoutes* routes() { return routes_.get(); }
	const std::string& effectsWhy() const { return effectsWhy_; }
	FightGeometry* geometry() { return geo_.get(); }
	// 4728: the fight's start (AnimTriggerTap::fightArt): its units' art through the host's art source, the projectile
	// effects' speeds applied to `tables` for their members (applyEffectSpeeds, once per member). Returns the weapons
	// changed.
	size_t fightArt(const srvunits::Fight& f, c5r::unitanim::Controller& c, c5r::unitanim::ArtSource& art,
	                c5r::unitanim::CombatArtTables& tables);

	class Subs;

private:
	std::unique_ptr<SectionSink> sec_;
	std::unique_ptr<LogSink> log_;
	std::unique_ptr<TeeSink> tee_;
	std::unique_ptr<TriggerEmitter> em_;
	std::unique_ptr<AnimTriggerTap> tap_;
	std::unique_ptr<Subs> subs_;
	std::unique_ptr<FightGeometry> geo_;          // 4728: the positions (figures, bones) and the flight planner
	c5m::EffectLibrary fxlib_;                    // 4728
	std::unique_ptr<c5r::effects::EffectNames> names_;
	std::unique_ptr<EffectRoutes> routes_;
	std::string effectsWhy_;
	std::set<std::string> speedDone_;             // members whose speeds were applied
	c5r::unitanim::Controller* ctl_ = nullptr;    // the runner's controller (its trigger files), from the first fight
	const srvunits::UnitsCore* core_ = nullptr;
	ScriptTable scripts_;
	UnitRootFn root_;
	UnitTerrainFn terrain_;
	const c5r::heightfield::HeightGrid* grid_ = nullptr;
	TriggerStreamConfig cfg_;                     // 5052: the knobs as opened
	PlotTags plotTags_;                           // 5052 (T)
	std::string plotTagsWhy_;
	int32_t ownerEra(int32_t owner) const;        // 5058: the player record's era (-1: no core, no record)
};

// The loop flags of every *3DScripts.xml under <install>\Assets (Assets\Sounds first, then the rest in path order), then
// the database's Audio_3DSounds rows (ScriptID, Looping). Returns the rows read.
size_t loadScriptTable(const std::wstring& install, c5m::TableSource* db, ScriptTable& out);

}  // namespace srveffects
