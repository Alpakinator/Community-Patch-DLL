// srv_trigger_emitter.h - the server's trigger emitter (epic 0042, ticket 4722, stage 2 of 4795; route S, D210).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// Turns the timed triggers that the server's figures cross while they play a fight into the trigger stream of
// shell/common/vp_trigger.h (4721): one record per sound or effect start / move / stop and per owner end, stamped with
// the frame F whose insertion executes it. Design: the Architect's route S note
// (research/07-architecture-decisions/architect-2026-11-20-route-s-combat.md) 1.4-1.6 with the MC's correction of 1.6
// (4799); the EXE's own trigger policy that the server takes over: research/02-exe-internals/exe-audio.md 2 and 4.2; as
// built: research/05-graphics-composition/unit-combat-server.md section 11 "The trigger emitter (4722)".
//
// The rules, as built:
//   1.4  Triggers are taken for every figure of a running fight, on screen or not (the caller steps every fighter; the
//        controller reports each trigger with the seconds into the frame at which it was crossed), and stamped with the
//        frame F whose poses the server draws. Positions come from the caller's FigurePositions: the trigger's bone,
//        else the figure's root, else the unit's root ("a figure that is not posed gives its root").
//   1.3  $(WEAPON), $(TERRAIN) and $(BASETERRAIN) are resolved before a name is sent (the caller's Substitutions); a
//        name that cannot be resolved is not sent (counted). Names go into the stream's name table once (R5).
//   1.5  (1) A one-shot whose trigger lies more than lateDrop (0.1 s) before frame F's time is dropped (counted,
//        droppedLate). (2) An owner's end (the fight's OwnerEnd, or a clip instance's ClipEnded, from the fight clock
//        srv_units_fights.h) is written after the owner's records of that frame; every later record of that owner is
//        dropped (counted afterEnd); its lasting effects are stopped with EffectStop before the OwnerEnd; its loops are
//        left to the OwnerEnd (R7: the shell stops an ended owner's loops; one-shots ring out). (4) An effect with a
//        duration starts late while its interval still covers frame F (it then lasts what is left of it), otherwise
//        not at all; a duration-0 effect is a one-shot.
//   1.6  The ownership table by action code (ownerOfCode): the combat clips (attacks, charges, combat ready, bombard
//        defend, the fight's deaths, victory, with their B variants code + 1000) are the server's; idles, fidgets,
//        fortify, work and everything else are the EXE figures' and are never sent (counted per code). Movement
//        (the approach run) of a fighting figure is sent too by default (EmitterParams::movementInFight, lead-0042
//        #8019: stock's fight sound has it and the EXE's figure stands still under route S).
//   Loops (exe-audio.md 4.2, the EXE's unit-loop table, which the server now owns): a looping script (ScriptTable)
//        started by a figure is started once (SoundStart with the loop flag) and renewed, not started again, while the
//        same unit triggers it again; when it has not been triggered for the heartbeat (0.1 s while the unit fights,
//        0.07 s otherwise; counted from the end of the Sound trigger's duration, which keeps it alive over that
//        interval [I]) the server sends SoundStop; at most loopSlots (64) loops at once (a new one beyond is not
//        started, counted). A loop is owned by its fight (loopsByFight, the correction of note 1.5 item 2) and is the
//        shell's to stop at the fight's OwnerEnd (R7); its slot is freed then.
// Not built here (named, unit-combat-server.md 11): the trigger bones' world positions (the caller passes roots until
// the pose source gives bones), Transfer triggers (the projectile's hand-off is 4792's, through startEffect below),
// the figure-less sounds and effects of 4777 (through startSound / startEffect), the visibility test the EXE makes
// before a unit sound (exe-audio.md 2 step 3).
//
// One thread (the server's render thread). Nothing throws. No EXE address, no name from the game's files: names reach
// the stream at run time through the caller's tables (D83).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "vp_trigger.h"
#include "units/srv_units_fights.h"

namespace srveffects {

// ---------------------------------------------------------------------------------------------- the ownership table (1.6)
enum class ClipOwner : uint8_t { Server, Exe };
enum class ClipClass : uint8_t { Attack, Charge, CombatReady, BombardDefend, Death, Victory, Movement, Idle, Other };
const char* clipClassName(ClipClass c);
// The class of an action code (the clip's ec); a B variant (2000-2999) is its A code's class.
ClipClass classOfCode(int32_t ec);
// Who sends a trigger of that clip while its figure fights: Server for Attack ... Victory, Exe for the rest.
ClipOwner ownerOfCode(int32_t ec);

// ---------------------------------------------------------------------------------------------- what the emitter reads
// One timed trigger a figure crossed in this frame (the controller's TriggerEvent, c5r/unitanim/controller.h, plus the
// trigger's duration from its file: srv_trigger_controller.h converts).
enum class TrigType : uint8_t { Unknown = 0, Sound, StopSound, Effect, Transfer, Action };
struct TriggerIn
{
	int32_t player = -1, unit = -1, figure = -1;
	uint64_t instance = 0;           // the clip instance that crossed it (FigureProgram::instance)
	TrigType type = TrigType::Unknown;
	int32_t ec = 0, trigger = -1;    // its clip's action code and its id in the file
	float clipTime = 0;              // its time in the clip (s)
	float offset = 0;                // seconds into this frame's steps when it was crossed (0 .. dt)
	float duration = 0;              // Effect: seconds the effect lasts (0 = a one-shot); Sound of a looping script: the
	                                 // interval over which the clip keeps the loop alive; <= 0 = none
	std::string event, bone;         // the name as written (may carry $(...)), the attachment bone ("" = none)
	std::string file;                // the trigger file (for the log; "" = not known)
	// 4737 (additive; research/02-exe-internals/exe-audio.md 13): the trigger's tier attribute (-1 = every unit) and
	// the unit's living figures when it was crossed (-1 = not known); see unitTierOf and EmitterParams::tierGate.
	int32_t tier = -1;
	int32_t unitFigures = -1;
};

// 4737: the unit's tier as the EXE's Sound-trigger handler computes it from the unit's figures (exe-audio.md 13.1):
// 0 for 3 or fewer, 1 for 4 to 8, 2 for 9 or more; -1 when the count is not known (n < 0).
inline int32_t unitTierOf(int32_t n)
{
	if(n < 0)
		return -1;
	return n <= 3 ? 0 : (n <= 8 ? 1 : 2);
}

// 4737: whether the EXE's handler would play a Sound trigger of this tier for a unit of this many figures: the
// trigger's tier is -1, the unit's tier is not known, or the two are equal.
inline bool tierPlays(int32_t tier, int32_t unitFigures)
{
	const int32_t u = unitTierOf(unitFigures);
	return tier == -1 || u == -1 || tier == u;
}

// Looping scripts (exe-audio.md 3: Audio_3DSounds.Looping, the *3DScripts.xml files' bLooping), by script id.
class ScriptTable
{
public:
	void set(const std::string& scriptId, bool looping) { m_[upper(scriptId)] = looping; }
	// known: the script is in the table; looping its flag
	bool find(const std::string& scriptId, bool& looping) const;
	size_t size() const { return m_.size(); }
	bool empty() const { return m_.empty(); }
	static std::string upper(const std::string& s);
	// Reads the Script3DSound rows (ScriptID, bLooping) of one *3DScripts.xml text; a later row overrides an earlier
	// one. Returns the rows read; `changed` counts overrides that changed the loop flag.
	size_t loadXml(const std::string& text, size_t* changed = nullptr);

private:
	std::map<std::string, bool> m_;
};

// $(WEAPON) / $(TERRAIN) / $(BASETERRAIN) for a figure (1.3; the values: Terrains / Features.EffectTypeTag of the
// figure's plot, the weapon's WeaponTypeSoundOverrideTag or WeaponTypeTag; unit-combat-server.md 11.3).
class Substitutions
{
public:
	virtual ~Substitutions() {}
	// false: the variable has no value for this figure (the name is then not sent)
	virtual bool terrain(int32_t player, int32_t unit, std::string& out) = 0;
	virtual bool baseTerrain(int32_t player, int32_t unit, std::string& out) { return terrain(player, unit, out); }
	// type: the trigger's type (a Sound takes the sound tag, an Effect the weapon tag); ec: its clip
	virtual bool weapon(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec, std::string& out) = 0;
	// Ticket 5055: the $(WEAPON) value to try when the Sound name made with weapon() is not a script (the emitter's
	// second try): the figure's own weapon when weapon() gave its striker's. False: no second try (the default, and every
	// caller before 5055).
	// Arguments as weapon()'s.
	virtual bool weaponFallback(int32_t /*player*/, int32_t /*unit*/, int32_t /*figure*/, TrigType /*type*/, int32_t /*ec*/,
	                            std::string& /*out*/)
	{
		return false;
	}
};
// Replaces $(TERRAIN), $(BASETERRAIN), $(WEAPON); false with `why` for any other variable, an unterminated "$(" or a
// variable without a value.
bool substitute(const std::string& name, Substitutions* subs, int32_t player, int32_t unit, int32_t figure, TrigType type,
                int32_t ec, std::string& out, std::string& why);

// ---------------------------------------------------------------------------------------------- ticket 5052
// A hit target's sound names as stock fills them (unit-combat-server.md 18.25, stock's rule read on build 4390913,
// description only, D83): $(WEAPON) from the unit's own entry in its running combat, else the other participant's
// figure-0 weapon; $(TERRAIN) from the plot's feature EffectTypeTag, else its terrain's, else DIRT. Header-only, so that
// trigger_scene (which does not link the trigger stream) runs the same rules as TriggerStream (srv_trigger_stream.cpp).
// Both are behind knobs: render_test_units_trigger_striker_weapon default ON (MC #10847, after the live pair 18.25.2),
// _trigger_terrain default ON (MC #11356, after its live pair 18.25.4; 18.25.5; was off, MC #10565). Ticket 5055 widens
// the first to both sides of a melee (FightWeaponRoles::namer's `struck`, Substitutions::weaponFallback;
// unit-combat-server.md 18.30) under its own key render_units_trigger_striker_melee (default ON, MC #11607), in effect
// only with the striker-weapon key on.

// (W) Whether a fight's defender strikes back: the core's rule (render/src/unitanim/combat.cpp, 4777's def_answers_): the
// retaliate flag, except in an air strike's opening (the attacker's side air, the defender's not, the defender with
// figures, the attacker's damage in this combat known, >= 0), where it answers when that damage is above 0.
inline bool DefenderStrikes(bool retaliates, int32_t attackerDamage, bool attackerAir, bool defenderAir, bool defenderFigures)
{
	if(attackerAir && !defenderAir && defenderFigures && attackerDamage >= 0)
		return attackerDamage > 0;
	return retaliates;
}

// (W) The running fights, for $(WEAPON): a unit that does not strike in its running fight (its defender, when
// DefenderStrikes is false) is named by its opponent's figure 0 (Opponent; a city opponent has no figure: OpponentCity,
// the caller keeps the unit's own and counts it). Every other unit (an attacker, a defender that strikes back: melee, a
// unit in no fight) keeps its own (Own). One thread (the render thread).
// Ticket 5055 (`struck`, the caller's strikerMelee, for a Sound): every unit struck in its running fight is named by its
// striker, as stock names both sides of a melee (unit-combat-server.md 18.25.2, 18.30): the defender by its attacker
// whether it strikes back or not, the attacker by its defender when the defender strikes back (DefenderStrikes); an
// attacker whose defender does not strike keeps its own, and a city striker gives OpponentCity as above. struck false:
// the rule of 5052 exactly.
// Ticket 5058 (unit-combat-server.md 18.34): a city side's owner era (attCityEra / defCityEra, -1 = not known); namer's
// `cityEra` out gives the striking city's on OpponentCity, for CityWeaponTag.
class FightWeaponRoles
{
public:
	struct Fight
	{
		uint32_t seq = 0;
		int32_t attPlayer = -1, attUnit = -1, attCity = -1;   // attUnit -1: a city strikes
		int32_t defPlayer = -1, defUnit = -1;                 // defUnit -1: a city (or a plot) is struck
		bool retaliates = false;                              // the record's DefenderRetaliates
		int32_t attackerDamage = -1;                          // the attacker's damage in this combat (-1: not known)
		int32_t attCityEra = -1, defCityEra = -1;             // 5058: a city side's owner era (EraTypes; -1: not known
		                                                      // or not a city: a plot target never has one)
	};
	enum class Namer : uint8_t { Own, Opponent, OpponentCity };
	void started(const Fight& f) { fights_[f.seq] = f; }
	void ended(uint32_t seq) { fights_.erase(seq); }
	void clear() { fights_.clear(); }
	size_t size() const { return fights_.size(); }
	// isAir(player, unit): the unit has an air member (the core's Part::air: a member whose domain is Air).
	template <class IsAir>
	Namer namer(int32_t player, int32_t unit, IsAir isAir, int32_t& oppPlayer, int32_t& oppUnit, bool struck = false,
	            int32_t* cityEra = nullptr) const
	{
		if(cityEra)
			*cityEra = -1;
		for(const auto& kv : fights_)
		{
			const Fight& f = kv.second;
			const bool isDef = f.defUnit >= 0 && f.defPlayer == player && f.defUnit == unit;
			const bool isAtt = struck && f.attUnit >= 0 && f.attPlayer == player && f.attUnit == unit;   // 5055
			if(!isDef && !isAtt)
				continue;
			const bool attAir = f.attUnit >= 0 && isAir(f.attPlayer, f.attUnit);
			const bool defAir = f.defUnit >= 0 && isAir(f.defPlayer, f.defUnit);
			const bool defStrikes = DefenderStrikes(f.retaliates, f.attackerDamage, attAir, defAir, f.defUnit >= 0);
			if(isDef)
			{
				if(defStrikes && !struck)
					return Namer::Own;
				if(f.attUnit < 0)
				{
					if(cityEra)
						*cityEra = f.attCityEra;   // 5058
					return Namer::OpponentCity;
				}
				oppPlayer = f.attPlayer;
				oppUnit = f.attUnit;
				return Namer::Opponent;
			}
			if(!defStrikes)   // 5055: an attacker nobody strikes back keeps its own (as 5052); look on
				continue;
			if(f.defUnit < 0)
			{
				if(cityEra)
					*cityEra = f.defCityEra;   // 5058
				return Namer::OpponentCity;   // a city strikes back: it has no figure
			}
			oppPlayer = f.defPlayer;
			oppUnit = f.defUnit;
			return Namer::Opponent;
		}
		return Namer::Own;
	}

private:
	std::map<uint32_t, Fight> fights_;   // by the fight's seq
};

// Ticket 5058 (unit-combat-server.md 18.34, stock's rule read on build 4390913, description only, D83): the weapon tag a
// striking city gives a Sound's $(WEAPON). Stock keeps three city weapons, CITY_EARLY, CITY_MID, CITY_LATE, and takes the
// one at the city owner's era index when that index is below 3, else the first: era 0 EARLY, 1 MID, 2 LATE, 3 and up
// EARLY. The caller passes only a known era (>= 0); an unknown one keeps the unit's own weapon.
inline const char* CityWeaponTag(int32_t era)
{
	switch(era)
	{
	case 1:
		return "CITY_MID";
	case 2:
		return "CITY_LATE";
	default:
		return "CITY_EARLY";
	}
}

// (T) The plots' EffectTypeTag (Terrains / Features rows by ID, read from the player's database at run time).
struct PlotTags
{
	std::map<int32_t, std::string> terrain, feature;   // ID -> EffectTypeTag ("" or absent: no tag)
	enum class From : uint8_t { Feature, Terrain, Dirt };
	// Stock's rule: the feature's tag when the plot has a feature with a tag, else the terrain's, else DIRT.
	From tagOf(int32_t terrainId, int32_t featureId, std::string& out) const
	{
		if(featureId >= 0)
		{
			auto f = feature.find(featureId);
			if(f != feature.end() && !f->second.empty())
			{
				out = f->second;
				return From::Feature;
			}
		}
		if(terrainId >= 0)
		{
			auto t = terrain.find(terrainId);
			if(t != terrain.end() && !t->second.empty())
			{
				out = t->second;
				return From::Terrain;
			}
		}
		out = "DIRT";
		return From::Dirt;
	}
};

// World positions (x, y, z with z up, HexToWorld's order) of what fights.
class FigurePositions
{
public:
	virtual ~FigurePositions() {}
	// The bone's world position on the figure's pose of this frame; false: not known (bone "" = no bone).
	virtual bool bone(int32_t player, int32_t unit, int32_t figure, const std::string& bone, float out[3])
	{
		(void)player; (void)unit; (void)figure; (void)bone; (void)out;
		return false;
	}
	// 4793 (additive): the bone's world point and orientation (quaternion x, y, z, w) on the figure's pose of this frame;
	// tried before bone(); false: not known. Stage A's effect start takes the bone's world matrix (effects-formats.md 8.1).
	virtual bool boneTransform(int32_t player, int32_t unit, int32_t figure, const std::string& bone, float pos[3], float orient[4])
	{
		(void)player; (void)unit; (void)figure; (void)bone; (void)pos; (void)orient;
		return false;
	}
	// 4793 (additive): the figure's member scale for its trigger effects (1 = as authored); <= 0 or not finite: 1, counted.
	virtual float figureScale(int32_t player, int32_t unit, int32_t figure)
	{
		(void)player; (void)unit; (void)figure;
		return 1.0f;
	}
	// The figure's root; false: not known.
	virtual bool figure(int32_t player, int32_t unit, int32_t figure, float out[3]) = 0;
	// The unit's root; false: not known (the record is then not sent: counted noPosition).
	virtual bool unit(int32_t player, int32_t unit, float out[3]) = 0;
};

// Where the records go: the stream's section (SectionSink below), the offline log (srv_trigger_logsink.h), or both.
class TriggerSink
{
public:
	virtual ~TriggerSink() {}
	// R5: a name's index (1..), 0 when refused
	virtual uint32_t name(uint32_t kind, const std::string& text) = 0;
	// R1: one record, its seq assigned by the sink; false when it was dropped (R3)
	virtual bool record(const VPTrigRecord& r) = 0;
	// the emitter's catch-up drops of this frame (VPTrigServer::droppedLate)
	virtual void droppedLate(uint64_t n) { (void)n; }
};

// The stream's section (vp_trigger.h): created or opened as <base>.Trig, or a caller's memory (tests).
class SectionSink : public TriggerSink
{
public:
	SectionSink() {}
	~SectionSink() override;
	SectionSink(const SectionSink&) = delete;
	SectionSink& operator=(const SectionSink&) = delete;
	// Maps <base>.Trig (Local\ names as vp_frame.h's) and initialises it with the next generation (the old one + 1 when
	// the section held one, else 1). False with why.
	bool open(const std::wstring& base, std::string& why);
	// A caller's buffer of VPTRIG_SECTION_BYTES (zero-filled or a previous generation), initialised the same way.
	void attach(void* section);
	void stop();                     // VPTrigStop (an orderly stop: magic 0)
	void* section() const { return m_section; }
	uint32_t generation() const { return m_gen; }
	uint32_t name(uint32_t kind, const std::string& text) override;
	bool record(const VPTrigRecord& r) override;
	void droppedLate(uint64_t n) override;

	// R9 (ticket 4783, MC ruling #8543): route S arms as one. With it on (the product's stream: TriggerStream with a
	// section), the sink asks the shell at each init (routeSAsk = the generation) and writes no record of a generation
	// whose answer is not ARMED (counted routeSDropped): the decision is TAKEN when the answer is ARMED, REFUSED when it
	// is NOT ARMED or turns DISARMED, NO ANSWER when the first fight record (owner or fightSeq set) comes before any
	// answer. Each decision queues one line for the host's log (takeLine). Off (tests, offline tools): every record is
	// written as before.
	void setRequireRouteS(bool on);
	bool requireRouteS() const { return m_requireRouteS; }
	void pollRouteS();                         // once per frame (and before each record): the answer, the decision
	bool takeLine(std::string& line);          // the next queued line, false when none
	uint32_t routeSTaken() const { return m_taken; }   // VPTrigRouteSTaken
	uint64_t routeSDropped() const { return m_routeSDropped; }

private:
	void init();
	void decide(uint32_t taken, const std::string& line);
	void* m_section = nullptr;
	void* m_mapping = nullptr;       // HANDLE
	uint32_t m_gen = 0;
	bool m_requireRouteS = false;
	uint32_t m_taken = 0;            // VPTRIG_TAKEN_UNDECIDED
	uint32_t m_seenState = 0;        // the last answer state read (VPTRIG_ROUTES_*)
	uint64_t m_routeSDropped = 0;
	std::vector<std::string> m_lines;
};

// Two sinks at once (the section and the log in offline runs): names must get the same index in both.
class TeeSink : public TriggerSink
{
public:
	TeeSink(TriggerSink& a, TriggerSink& b) : a_(a), b_(b) {}
	uint32_t name(uint32_t kind, const std::string& text) override;
	bool record(const VPTrigRecord& r) override;
	void droppedLate(uint64_t n) override { a_.droppedLate(n); b_.droppedLate(n); }
	uint64_t mismatches() const { return mismatch_; }

private:
	TriggerSink &a_, &b_;
	uint64_t mismatch_ = 0;
};

// ---------------------------------------------------------------------------------------------- the emitter
struct EmitterParams
{
	double lateDrop = 0.1;           // 1.5 item 1 (s)
	double heartbeat = 0.07;         // fUnitLoopHeartbeatLength (exe-audio.md 4.2; the install's AudioParams.xml value)
	double combatHeartbeat = 0.1;    // fUnitLoopCombatHeartbeatLength (the same; every figure here fights)
	uint32_t loopSlots = 64;         // the EXE's unit-loop table (exe-audio.md 4.2)
	float moveEpsilon = 0.01f;       // a following effect gets EffectMove when its point moved further (world units)
	// After a fight's Natural end its units' victory clips (1800) still play: their one-shot sounds and effects are
	// sent for this long, owned by the unit (owner 0, fseq 0, player and unit: R7's second owner form); their loops
	// are not (nothing would end them). 0 = off. (The fight clock ends a fight when the choreography requests victory,
	// so the victory clip runs after the fight's OwnerEnd; unit-combat-server.md 11.2.)
	double victoryTail = 3.0;
	// true (lead-0042 #8019, matching stock): a fighting figure's Movement clips (the approach run, run stop, shuffle,
	// turns) are the server's: stock's fight sound has the run loops of the approach (exe-audio.md 9), and under route S
	// the EXE's own figure stands still (Combat off), so nobody else plays them. false: 1.6's table strictly by action
	// code (Movement the EXE's class).
	bool movementInFight = true;
	// true (lead-0042 #8089; a correction of the Architect's note 1.5 item 2, unit-combat-server.md 11.6 item 4): a loop
	// is owned by the fight (instance 0 in its record) and lives on the EXE's rule alone (renewed by any clip of its unit,
	// ended by the heartbeat or the fight's end), as stock's sounds do across clip changes. false: the note as first
	// written: the clip instance that started it owns it, the shell stops it at that instance's ClipEnded and the next
	// clip starts it again.
	bool loopsByFight = true;
	// true (4737, lead-0042 #9324; the EXE's rule, exe-audio.md 13): a Sound trigger is sent only when tierPlays(its
	// tier, its unit's living figures): a whole unit sends its tier -1 sounds and the bed of its size, not the per-figure
	// detail (tier 0, heard from a unit of 3 figures or fewer). Dropped ones are counted droppedTier. false: every
	// Sound trigger of every figure is sent (route S before 4737: 2-5 times stock's one-shots).
	bool tierGate = true;
};

struct EmitterCounters
{
	uint64_t frames = 0, fights = 0;
	uint64_t triggers = 0;                   // TriggerIn taken
	uint64_t kinds[VPTRIG_KIND_COUNT] = {};  // records written by kind
	uint64_t sinkDropped = 0;                // records the sink dropped (R3)
	uint64_t exeClass = 0;                   // triggers of a clip the EXE's figures own (1.6): not sent
	uint64_t noFight = 0;                    // triggers of a unit in no running fight: not sent
	uint64_t afterEnd = 0;                   // records of an owner that has ended (1.5 item 2): dropped
	uint64_t droppedLate = 0;                // one-shots more than lateDrop late (1.5 item 1)
	uint64_t droppedLateLasting = 0;         // effects whose interval no longer covered the frame (1.5 item 4)
	uint64_t startedLate = 0;                // lasting effects started late inside their interval
	uint64_t loopsStarted = 0, loopsRenewed = 0, loopsHeartbeat = 0, loopsOwnerEnd = 0, loopsSlotsFull = 0;
	uint64_t stopSound = 0;                  // StopSound triggers (no install file has one, exe-audio.md 6)
	uint64_t effectsEnded = 0;               // lasting effects stopped at their duration's end
	uint64_t effectsOwnerEnd = 0;            // lasting effects stopped by their owner's end
	uint64_t actions = 0, transfers = 0;     // hit moments and projectile hand-offs: no record (4775 / 4792 own them)
	uint64_t droppedTier = 0;                // 4737: Sound triggers whose tier is not the unit's (tierGate): not sent
	uint64_t unknownScript = 0;              // a sound name not in the ScriptTable: not sent
	uint64_t loopUnknown = 0;                // sounds sent as one-shots because no ScriptTable was given
	uint64_t victoryTail = 0;                // records sent in a victory tail (owned by the unit)
	uint64_t unresolved = 0;                 // a name with a variable that has no value: not sent
	uint64_t namesRefused = 0;               // names the sink refused (too long, table full): not sent
	uint64_t noPosition = 0;                 // no bone, figure or unit position: not sent
	uint64_t badScale = 0;                   // figureScale <= 0 or not finite: 1 taken
	uint64_t boneTransforms = 0;             // effect starts / moves placed by boneTransform
	uint64_t redraws = 0;                    // 4728: frames begun with an id not above the last (a redraw; accepted)
	uint64_t framesBack = 0;                 // 4728: of them, an id below the last (stamped with the last id, R1)
	uint64_t badType = 0;                    // a trigger of an unknown type
	uint64_t ownerUnknown = 0;               // fights whose owner (the EXE's event id) was not known (owner 0)
	uint64_t ownersLate = 0;                 // 4783: of them, the owner set later (fightOwner)
	uint64_t apiRefused = 0;                 // startSound / startEffect / moveEffect / stopEffect refusals
	std::map<int32_t, uint64_t> exeCodes;    // exeClass by action code
};

// A figure-less or projectile start (4792's projectile VisEffect, 4777's city strike or blast): what a TriggerIn
// cannot carry.
struct StartSpec
{
	int32_t player = -1, unit = -1, figure = -1;   // the owner's unit (its running fight is the owner)
	uint64_t instance = 0;           // the clip instance it belongs to (0 = the fight itself)
	std::string name;                // resolved (no $(...)): a 3D audio script id, or an effect name
	float pos[3] = {0, 0, 0};
	float orient[4] = {0, 0, 0, 1};  // effects
	float scale = 1;                 // effects (> 0)
	float vel[3] = {0, 0, 0};        // effects (world units per second)
	bool follows = false;            // effects: moved by moveEffect
	bool loop = false;               // sounds: the script loops (stopped by its owner's end or stopSound)
	// 4793 (additive): effects: a one-shot (a HitEffect, a ProjectileEnd child): not kept after its start, so its
	// owner's end sends no EffectStop for it (note 1.5 item 2: one-shots ring out) and move / stop refuse its handle;
	// more than lateDrop late it is not started (1.5 item 1: refused, counted droppedLate).
	bool oneShot = false;
	int32_t ec = 0, trigger = -1;    // the trigger's identity for the log (-1 none)
	float clipTime = 0;
	float late = 0;                  // seconds before frame F's time it was due (>= 0)
	std::string file;                // the trigger file for the log ("" none)
};

class TriggerEmitter
{
public:
	TriggerEmitter(TriggerSink& sink, const EmitterParams& p = EmitterParams()) : sink_(sink), p_(p) {}
	void setScripts(const ScriptTable* t) { scripts_ = t; }          // not owned; null = no table
	void setSubstitutions(Substitutions* s) { subs_ = s; }           // not owned; null = no variable resolves
	void setPositions(FigurePositions* p) { pos_ = p; }              // not owned; null = no record has a position

	// One frame: begin, the fights that started, the triggers crossed, the fight clock's OwnerEnds, then end.
	// False with why for a negative frame id, a dt that is not finite or negative, or a call out of order. 4728: an id not
	// above the last is a redraw, accepted and counted (an id below the last is stamped with the last, R1).
	bool beginFrame(int64_t frameId, double dt, std::string& why);
	// A fight starts (the fight clock's player was asked to start it; srv_units_fights.h): its units' triggers are taken
	// from now on, owned by it.
	void fightStarted(const srvunits::Fight& f);
	// 4783 (W33 #8801): the running fight `seq` learned its EXE event id late (FightClock::setOwner): its units' fight
	// reference takes it, so its later records (its OwnerEnd included) carry it. False when no running fight has that seq.
	bool fightOwner(uint32_t seq, uint32_t owner);
	void triggers(const std::vector<TriggerIn>& in);
	void ownerEnds(const std::vector<srvunits::OwnerEnd>& ends);
	// Writes the frame's records to the sink (starts, moves and stops in the frame's time order; the heartbeat's and the
	// durations' stops; the owner ends last, each after its owner's stops) and returns how many were written.
	size_t endFrame();

	// 4792 / 4777: a start that is not a clip trigger, between beginFrame and endFrame. The handle (0 = refused, why).
	// The owner is the running fight of (player, unit); a start for a unit in no running fight, or of an ended owner, is
	// refused. An effect started here is stopped by stopEffect or by its owner's end (EffectStop before the OwnerEnd:
	// "a projectile in flight at a forced end is removed with its trail", 1.5 item 4).
	uint32_t startSound(const StartSpec& s, std::string& why);
	uint32_t startEffect(const StartSpec& s, std::string& why);
	bool moveEffect(uint32_t handle, const float pos[3], const float orient[4], const float vel[3], std::string& why);
	bool stopEffect(uint32_t handle, std::string& why);
	bool stopSound(uint32_t handle, std::string& why);

	bool inFight(int32_t player, int32_t unit) const { return units_.count(std::make_pair(player, unit)) != 0; }
	// in a running fight or in its victory tail: the emitter takes this unit's triggers
	bool wants(int32_t player, int32_t unit) const
	{
		return inFight(player, unit) || tails_.count(std::make_pair(player, unit)) != 0;
	}
	size_t liveLoops() const { return loops_.size(); }
	size_t liveEffects() const { return effects_.size(); }
	const EmitterCounters& counters() const { return c_; }

private:
	struct FightRef
	{
		uint32_t owner = 0, seq = 0;
		int32_t ap = -1, au = -1, dp = -1, du = -1;
	};
	struct Pending
	{
		double t = 0;                // seconds into the frame (the order of the frame's records)
		int order = 0;               // 0 starts / moves / stops, 1 owner-end stops, 2 owner ends
		VPTrigRecord r;
	};
	struct Loop
	{
		uint32_t handle = 0;
		uint32_t fseq = 0;
		uint64_t instance = 0;
		double age = 0;
		bool renewed = false;
	};
	struct Effect
	{
		uint32_t handle = 0, fseq = 0;
		int32_t player = -1, unit = -1, figure = -1;
		uint64_t instance = 0;
		std::string bone;
		double left = -1;            // seconds of its duration left (< 0: until stopped)
		bool follows = false;
		float pos[3] = {0, 0, 0};
		bool api = false;
		bool born = true;            // started in this frame: not aged at its end
	};
	using UnitKey = std::pair<int32_t, int32_t>;
	using LoopKey = std::tuple<int32_t, int32_t, uint32_t>;          // player, unit, script name index
	using InstKey = std::tuple<int32_t, int32_t, int32_t, uint64_t>;  // player, unit, figure, instance

	const FightRef* fightOf(int32_t player, int32_t unit) const;
	bool ended(const FightRef& f, int32_t player, int32_t unit, int32_t figure, uint64_t instance) const;
	uint32_t nameIndex(uint32_t kind, const std::string& text);
	bool position(const TriggerIn& t, float out[3]);
	bool transform(const TriggerIn& t, float pos[3], float orient[4]);   // boneTransform, else position + identity
	VPTrigRecord base(uint8_t kind, const FightRef& f, int32_t player, int32_t unit, int32_t figure, uint64_t instance);
	void push(double t, int order, const VPTrigRecord& r);
	void sound(const TriggerIn& t, const FightRef& f);
	bool substituteFallback(const TriggerIn& t, std::string& out);   // 5055: the name with weaponFallback's $(WEAPON)
	void effect(const TriggerIn& t, const FightRef& f);
	void stopSoundTrigger(const TriggerIn& t, const FightRef& f);
	void ageLoops();
	void ageEffects();
	void endOwner(const srvunits::OwnerEnd& e);

	TriggerSink& sink_;
	EmitterParams p_;
	const ScriptTable* scripts_ = nullptr;
	Substitutions* subs_ = nullptr;
	FigurePositions* pos_ = nullptr;
	bool inFrame_ = false, haveFrame_ = false;
	int64_t frame_ = 0;
	double dt_ = 0;
	uint32_t nextHandle_ = 0;
	std::map<UnitKey, FightRef> units_;              // the units of the running fights
	std::set<uint32_t> endedSeq_;                    // fights that ended (their seq)
	std::set<InstKey> endedInst_;                    // clip instances that ended
	std::map<std::pair<uint32_t, std::string>, uint32_t> names_;   // (kind, text) -> index
	std::map<LoopKey, Loop> loops_;
	std::map<uint32_t, Effect> effects_;             // live lasting / API effects by handle
	std::map<uint32_t, LoopKey> apiSounds_;          // startSound loops by handle
	std::vector<Pending> pending_;
	std::vector<srvunits::OwnerEnd> ends_;           // this frame's, applied at endFrame after its triggers
	std::map<UnitKey, double> tails_;                // victory tails: unit -> until (now_ seconds)
	double now_ = 0;                                 // the sum of the frames' dt
	uint64_t lateThisFrame_ = 0;
	EmitterCounters c_;
};

}  // namespace srveffects
