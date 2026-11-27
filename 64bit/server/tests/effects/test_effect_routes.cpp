// test_effect_routes.cpp - tests of a fight's effect routes into the trigger stream (server/src/effects/
// srv_effect_routes.*; epic 0042, ticket 4793). Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
// Built and run by run_effect_routes_test.py (with 4722's emitter, the fight clock's header, c5render.lib and
// c5r_models.lib of a render build).
//
//   test_effect_routes.exe [LOGDIR]     every check; "test_effect_routes: <n> passed, <m> failed", exit 0 only when m == 0
//
// Synthetic effect definitions in memory (no game file); every record of every case goes through 4721's contract check
// and 4722's stream check (trigger_checks.h): owners, handles, order.
#include "effects/srv_effect_routes.h"
#include "effects/srv_trigger_logsink.h"
#include "trigger_checks.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5models/effect_xml.h"
#include "c5models/logic.h"

using namespace srveffects;
using namespace srvunits;
using c5r::effects::EffectNames;
using c5r::effects::HeldEffects;
using c5r::effects::ProjectileSpec;
using c5r::unitanim::CombatHit;

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                                                                         \
	do                                                                                                                   \
	{                                                                                                                    \
		if(c)                                                                                                            \
			++g_pass;                                                                                                    \
		else                                                                                                             \
		{                                                                                                                \
			++g_fail;                                                                                                    \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                          \
		}                                                                                                                \
	} while(0)

static const double kDt = 1.0 / 30.0;

class MemSink : public TriggerSink
{
public:
	std::vector<VPTrigRecord> recs;
	std::vector<std::string> names;
	uint64_t bad = 0;
	std::string firstBad;
	uint32_t name(uint32_t kind, const std::string& text) override
	{
		(void)kind;
		names.push_back(text);
		return (uint32_t)names.size();
	}
	bool record(const VPTrigRecord& r) override
	{
		VPTrigRecord c = r;
		c.seq = recs.size() + 1;
		c5r::Status s = c5r::unitanim::checkTriggerRecord(c);
		if(!s.ok())
		{
			++bad;
			if(firstBad.empty())
				firstBad = s.str() + " (" + c5r::unitanim::formatTriggerRecord(c) + ")";
		}
		recs.push_back(c);
		return true;
	}
	std::vector<VPTrigRecord> of(uint8_t kind) const
	{
		std::vector<VPTrigRecord> o;
		for(auto& r : recs)
			if(r.kind == kind)
				o.push_back(r);
		return o;
	}
	const std::string& nameOf(uint32_t i) const
	{
		static const std::string none;
		return i >= 1 && i <= names.size() ? names[i - 1] : none;
	}
};

// Unit (p, u) at (p*100 + u, 0, 0); figure f at +f in y; bone "B" at +0.5 in z.
class Pos : public FigurePositions
{
public:
	bool bone(int32_t p, int32_t u, int32_t f, const std::string& b, float out[3]) override
	{
		if(b != "B")
			return false;
		figure(p, u, f, out);
		out[2] += 0.5f;
		return true;
	}
	bool figure(int32_t p, int32_t u, int32_t f, float out[3]) override
	{
		if(f < 0)
			return false;
		unit(p, u, out);
		out[1] += (float)f;
		return true;
	}
	bool unit(int32_t p, int32_t u, float out[3]) override
	{
		out[0] = (float)(p * 100 + u);
		out[1] = 0;
		out[2] = 0;
		return true;
	}
	float figureScale(int32_t, int32_t, int32_t f) override { return f == unknownFigure ? 0.0f : memberScale; }
	float memberScale = 1.0f;   // the figures' member scale (testHitScale sets it)
	int32_t unknownFigure = -1; // a figure whose member scale is not known (0); -1: none
};

// Unit 2's plot is snow (no variant of FIRE_IMPACT: DIRT), every other DIRT; weapon METAL; player 9 has no value.
class Subs : public Substitutions
{
public:
	bool terrain(int32_t p, int32_t u, std::string& out) override
	{
		if(p == 9)
			return false;
		out = u == 2 ? "SNOW" : "DIRT";
		return true;
	}
	bool weapon(int32_t p, int32_t, int32_t, TrigType, int32_t, std::string& out) override
	{
		if(p == 9)
			return false;
		out = "METAL";
		return true;
	}
};

class Mem : public c5m::FileProvider
{
public:
	std::map<std::string, std::string> files;
	bool read(const std::string& n, c5m::Bytes& out, std::string* origin) override
	{
		auto it = files.find(n);
		if(it == files.end())
			return false;
		out.assign(it->second.begin(), it->second.end());
		if(origin)
			*origin = "mem:" + n;
		return true;
	}
};

static std::string vis(const std::string& name, const std::string& body = "")
{
	return "<VisEffectArtInfo><Name>" + name + "</Name><Transform><Translation x=\"0\" y=\"0\" z=\"0\"/>"
	       "<Rotation x=\"0\" y=\"0\" z=\"0\"/><Scale>1.0</Scale></Transform><fBaseDuration>0.5</fBaseDuration>"
	       "<fBaseDurationScale>1.0</fBaseDurationScale>" + body + "</VisEffectArtInfo>";
}
static std::string child(const std::string& n)
{
	return "<ChildEffects><ChildEffect trigger=\"ProjectileEnd\"><Name>" + n + "</Name></ChildEffect></ChildEffects>";
}
static bool loadLib(c5m::EffectLibrary& lib)
{
	Mem fp;
	fp.files["civ5artdefines_viseffects.xml"] =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<VisEffectArtInfos>" + vis("FX_MUZZLE") + vis("FX_HIT_DIRT") +
		vis("FX_HIT_SNOW") + vis("FX_FIRE_IMPACT_DIRT") + vis("FX_FIRE_IMPACT_CITY") + vis("FX_DEATH_METAL") +
		vis("FX_ARROW", child("FX_HIT_$(TERRAIN)")) + vis("FX_TORCH", child("FX_FIRE_IMPACT_$(TERRAIN)")) +
		vis("CITY_BOMBARD_EARLY", child("FX_HIT_$(TERRAIN)")) + vis("FX_MG_HIT_DIRT") + "</VisEffectArtInfos>";
	c5m::Error e;
	return lib.load(fp, c5m::fx_load_list({}), &e);
}

static Fight F(uint32_t seq, uint32_t owner, int ap, int au, int dp, int du)
{
	Fight f;
	f.seq = seq;
	f.owner = owner;
	f.ownerKnown = owner != 0;
	f.attacker.owner = ap;
	f.attacker.unit = au;
	f.defender.owner = dp;
	f.defender.unit = du;
	return f;
}

static TriggerIn T(int p, int u, int fig, uint64_t inst, TrigType type, int ec, const char* ev, int trig, double dur = 0,
                   const char* bone = "")
{
	TriggerIn t;
	t.player = p;
	t.unit = u;
	t.figure = fig;
	t.instance = inst;
	t.type = type;
	t.ec = ec;
	t.trigger = trig;
	t.clipTime = 0.3f;
	t.offset = (float)kDt;   // crossed at the end of the frame: not late
	t.duration = (float)dur;
	t.event = ev;
	t.bone = bone;
	t.file = "fx_triggers_test.ftsxml";
	return t;
}

static OwnerEnd E(uint32_t seq, uint32_t owner, FightEnd why)
{
	OwnerEnd e;
	e.seq = seq;
	e.owner = owner;
	e.why = why;
	return e;
}

// The emitter and the routes of one test, stepped together in the documented order.
struct Rig
{
	MemSink sink;
	Pos pos;
	Subs subs;
	c5m::EffectLibrary lib;
	std::unique_ptr<EffectNames> names;
	HeldEffects held;
	std::unique_ptr<TriggerEmitter> em;
	std::unique_ptr<EffectRoutes> routes;
	int64_t frame = 0;
	explicit Rig(const RouteParams& p = RouteParams())
	{
		const bool ok = loadLib(lib);
		CHECK(ok);
		names.reset(new EffectNames(lib));
		em.reset(new TriggerEmitter(sink));
		em->setSubstitutions(&subs);
		em->setPositions(&pos);
		routes.reset(new EffectRoutes(*em, *names, p));
		routes->setSubstitutions(&subs);
		routes->setPositions(&pos);
		routes->setHeld(&held);
		// the test file: Effect 5 (the arrow in the hand) is handed over by Transfer 6; Effect 7 is a muzzle flash
		const char* doc = "<trigger_system type=\"FGrannyTimedTriggerSystem\"><triggers>"
		                  "<trigger type=\"FTimedTriggerEffect\" id=\"5\" ec=\"1100\" time=\"0.3\" duration=\"0.2\" event=\"FX_ARROW\" bone=\"B\"/>"
		                  "<trigger type=\"FTimedTriggerTransfer\" id=\"6\" ec=\"1100\" time=\"0.45\" refid=\"5\"/>"
		                  "<trigger type=\"FTimedTriggerEffect\" id=\"7\" ec=\"1100\" time=\"0.1\" duration=\"0\" event=\"FX_MUZZLE\" bone=\"B\"/>"
		                  "</triggers></trigger_system>";
		c5m::TriggerSystem ts;
		c5m::Error e;
		const bool parsed = c5m::parse_ftsxml(doc, strlen(doc), ts, &e);
		CHECK(parsed);
		held.add("fx_triggers_test.ftsxml", ts);
	}
	void begin(double dt = kDt)
	{
		std::string why;
		const bool a = em->beginFrame(++frame, dt, why);
		const bool b = routes->beginFrame(dt, why);
		CHECK(a && b);
	}
	void start(const Fight& f)
	{
		em->fightStarted(f);
		routes->fightStarted(f);
	}
	// one frame: triggers, hits (with the fight's start on the routes' clock), flights, ends
	size_t step(std::vector<TriggerIn> t = std::vector<TriggerIn>(), const std::vector<OwnerEnd>& e = std::vector<OwnerEnd>(),
	            double dt = kDt)
	{
		begin(dt);
		routes->intake(t);
		em->triggers(t);
		routes->ownerEnds(e);
		em->ownerEnds(e);
		routes->endFrame();
		return em->endFrame();
	}
	trigchecks::StreamCheck check() const { return trigchecks::checkStream(sink.recs); }
};

static CombatHit H(int p, int u, int tp, int tu, double launch, double t, const char* effect)
{
	CombatHit h;
	h.player = p;
	h.id = u;
	h.figure = 1;
	h.target_player = tp;
	h.target_id = tu;
	h.target_figure = 0;
	h.launch = launch;
	h.time = t;
	h.effect = effect;
	h.has_pos = true;
	h.pos[0] = 50;
	h.pos[1] = 60;
	h.pos[2] = 0;
	h.result = c5r::unitanim::HitResult::Death;
	return h;
}

// ---------------------------------------------------------------------------------------------- 1. clip Effect triggers
static void testIntakeNames()
{
	Rig r;
	r.start(F(1, 1001, 0, 1, 1, 2));
	r.start(F(2, 1002, 9, 3, 1, 4));
	std::vector<TriggerIn> t;
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_MUZZLE", 7, 0, "B"));                 // exact
	t.push_back(T(1, 2, 0, 12, TrigType::Effect, 1200, "FX_FIRE_IMPACT_$(TERRAIN)", 8));          // snow -> DIRT
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_DEATH_$(WEAPON)", 9));                 // METAL
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "ART_DEF_VEFFECT_SEAGULLS_01", 10));       // dangling
	t.push_back(T(9, 3, 0, 13, TrigType::Effect, 1100, "FX_HIT_$(TERRAIN)", 11));                 // no value -> DIRT
	t.push_back(T(0, 1, 0, 11, TrigType::Sound, 1100, "AS3D_X", 12));                             // untouched
	std::vector<TriggerIn> copy = t;
	r.routes->intake(copy);
	CHECK(copy.size() == 5);
	CHECK(copy[1].event == "FX_FIRE_IMPACT_DIRT" && copy[2].event == "FX_DEATH_METAL" && copy[3].event == "FX_HIT_DIRT");
	CHECK(copy[4].type == TrigType::Sound && copy[4].event == "AS3D_X");
	const RouteCounters& c = r.routes->counters();
	CHECK(c.effectTriggers == 5 && c.exact == 2 && c.fallback == 2 && c.refused == 1);
	CHECK(c.refusedNames.count("ART_DEF_VEFFECT_SEAGULLS_01") == 1);
	// through the emitter: four EffectStarts with the resolved names
	t.pop_back();
	r.step(t);
	std::vector<VPTrigRecord> s = r.sink.of(VPTRIG_EFFECT_START);
	CHECK(s.size() == 4);
	std::vector<std::string> got;
	for(auto& x : s)
		got.push_back(r.sink.nameOf(x.name));
	CHECK(got.size() == 4 && got[0] == "FX_MUZZLE" && got[1] == "FX_FIRE_IMPACT_DIRT" && got[2] == "FX_DEATH_METAL" &&
	      got[3] == "FX_HIT_DIRT");
	CHECK(r.sink.bad == 0);
	if(r.sink.bad)
		printf("  %s\n", r.sink.firstBad.c_str());
}

// ---------------------------------------------------------------------------------------------- 2. held projectiles
static void testHeldHandOver()
{
	Rig r;
	r.start(F(1, 1001, 0, 1, 1, 2));
	// frame 1: the arrow in the hand (held: started by the routes, following), and the muzzle (an ordinary trigger)
	std::vector<TriggerIn> t;
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_ARROW", 5, 0.2, "B"));
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_MUZZLE", 7, 0, "B"));
	r.step(t);
	CHECK(r.routes->counters().held == 1 && r.routes->heldWaiting() == 1);
	std::vector<VPTrigRecord> s = r.sink.of(VPTRIG_EFFECT_START);
	CHECK(s.size() == 2);
	uint32_t armHandle = 0;
	for(auto& x : s)
		if(r.sink.nameOf(x.name) == "FX_ARROW")
		{
			armHandle = x.handle;
			CHECK((x.flags & VPTRIG_FLAG_FOLLOWS_OWNER) != 0);
			CHECK(x.pos[0] == 1.0f && x.pos[2] == 0.5f);   // the bone of figure 0 of unit 0/1
			CHECK(x.trigger == 5);
		}
	CHECK(armHandle != 0);
	// frame 2: the Transfer: the flight takes the held handle (no second start)
	r.begin();
	ProjectileSpec spec;
	spec.player = 0;
	spec.unit = 1;
	spec.figure = 0;
	spec.effect = "FX_ARROW";
	spec.children.push_back("FX_HIT_DIRT");
	spec.path.kind = c5r::effects::PathKind::Straight;
	spec.path.p[0] = c5r::effects::V3{1, 0, 0.5f};
	spec.path.p[3] = c5r::effects::V3{101, 0, 0};
	spec.path.duration = 0.2f;
	spec.t0 = r.routes->now();
	std::string why;
	const uint32_t h = r.routes->addFlight(spec, why);
	CHECK(h != 0);
	CHECK(r.routes->counters().heldTaken == 1 && r.routes->heldWaiting() == 0);
	r.routes->endFrame();
	r.em->endFrame();
	// the firing clip ends while the arrow flies: the arrow (owned by the fight) flies on
	std::vector<OwnerEnd> ce;
	OwnerEnd clipEnd = E(1, 1001, FightEnd::ClipEnded);
	clipEnd.player = 0;
	clipEnd.unit = 1;
	clipEnd.figure = 0;
	clipEnd.instance = 11;
	ce.push_back(clipEnd);
	r.step(std::vector<TriggerIn>(), ce);
	CHECK(r.sink.of(VPTRIG_EFFECT_STOP).empty());
	for(int i = 0; i < 9; ++i)
		r.step();
	// one instance from the hand to the target: one start, moves on the same handle, the child at the end, no stop
	size_t starts = 0, moves = 0, stops = 0, children = 0;
	for(auto& x : r.sink.recs)
	{
		if(x.kind == VPTRIG_EFFECT_START && r.sink.nameOf(x.name) == "FX_ARROW")
			++starts;
		if(x.kind == VPTRIG_EFFECT_MOVE)
		{
			CHECK(x.handle == armHandle);
			++moves;
		}
		if(x.kind == VPTRIG_EFFECT_STOP)
			++stops;
		if(x.kind == VPTRIG_EFFECT_START && r.sink.nameOf(x.name) == "FX_HIT_DIRT")
		{
			++children;
			CHECK(fabsf(x.pos[0] - 101.0f) < 0.01f);
			CHECK((x.flags & VPTRIG_FLAG_FOLLOWS_OWNER) == 0);
		}
	}
	CHECK(starts == 1 && moves >= 5 && stops == 0 && children == 1);
	CHECK(r.routes->counters().recChild == 1 && r.routes->counters().recMove == moves);
	// the fight ends: the arrow's (tracked) effect is stopped before the OwnerEnd, the child (a one-shot) is not
	std::vector<OwnerEnd> e;
	e.push_back(E(1, 1001, FightEnd::Natural));
	r.step(std::vector<TriggerIn>(), e);
	std::vector<VPTrigRecord> st = r.sink.of(VPTRIG_EFFECT_STOP);
	CHECK(st.size() == 1 && st[0].handle == armHandle);
	trigchecks::StreamCheck c = r.check();
	CHECK(c.ok() && r.sink.bad == 0);
	if(!c.ok())
		printf("  %s\n", trigchecks::summary(c).c_str());
}

// 5064 (RouteParams::projectilePose): the held arrow starts at the bone's world transform and the member's scale, and
// the flight that takes its handle keeps both (moves carry no scale); off: the rule before 5064 (the bone's point, the
// identity orientation, scale 1)
class PosedPos : public Pos
{
public:
	bool boneTransform(int32_t p, int32_t u, int32_t f, const std::string& b, float pos[3], float orient[4]) override
	{
		++asks;
		if(refuse || !bone(p, u, f, b, pos))
			return false;
		orient[0] = 0;
		orient[1] = 0;
		orient[2] = 0.70710678f;   // 90 degrees about +z
		orient[3] = 0.70710678f;
		if(nanOrient)
			orient[0] = NAN;
		return true;
	}
	int asks = 0;
	bool refuse = false, nanOrient = false;
};

static void heldPoseCase(bool on, bool refuse, bool nanOrient, float memberScale, float wantScale, bool wantPosed)
{
	RouteParams p;
	p.projectilePose = on;
	PosedPos pp;   // outlives the rig, which holds it
	Rig r(p);
	pp.memberScale = memberScale;
	pp.refuse = refuse;
	pp.nanOrient = nanOrient;
	r.em->setPositions(&pp);
	r.routes->setPositions(&pp);
	r.start(F(1, 1001, 0, 1, 1, 2));
	std::vector<TriggerIn> t;
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_ARROW", 5, 0.2, "B"));
	r.step(t);
	CHECK(r.routes->counters().held == 1);
	std::vector<VPTrigRecord> s = r.sink.of(VPTRIG_EFFECT_START);
	CHECK(s.size() == 1);
	if(s.size() != 1)
		return;
	const VPTrigRecord& a = s[0];
	CHECK(fabsf(a.scale - wantScale) < 1e-6f);
	CHECK(a.pos[0] == 1.0f && a.pos[2] == 0.5f);   // the bone's point either way
	if(wantPosed)
		CHECK(fabsf(a.orient[2] - 0.70710678f) < 1e-6f && fabsf(a.orient[3] - 0.70710678f) < 1e-6f);
	else
		CHECK(a.orient[0] == 0 && a.orient[1] == 0 && a.orient[2] == 0 && a.orient[3] == 1);
	CHECK(pp.asks == (on ? 1 : 0));   // off: boneTransform is never asked (the rule before 5064)
	const RouteCounters& c = r.routes->counters();
	CHECK(c.heldBoneTransform == (on && wantPosed ? 1u : 0u) && c.heldNoBoneTransform == (on && !wantPosed ? 1u : 0u));
	CHECK(c.heldScaleMember + c.heldScaleUnknown == (on ? 1u : 0u));
	// the flight takes the handle: moves on it, and no second start (so no other scale reaches the EXE)
	r.begin();
	ProjectileSpec spec;
	spec.player = 0;
	spec.unit = 1;
	spec.figure = 0;
	spec.effect = "FX_ARROW";
	spec.scale = 0.5f;   // the planner's scale is not sent: the held start's is the flight's
	spec.path.kind = c5r::effects::PathKind::Straight;
	spec.path.p[0] = c5r::effects::V3{1, 0, 0.5f};
	spec.path.p[3] = c5r::effects::V3{101, 0, 0};
	spec.path.duration = 0.2f;
	spec.t0 = r.routes->now();
	std::string why;
	CHECK(r.routes->addFlight(spec, why) != 0);
	r.routes->endFrame();
	r.em->endFrame();
	for(int i = 0; i < 8; ++i)
		r.step();
	size_t starts = 0, moves = 0;
	for(auto& x : r.sink.recs)
	{
		if(x.kind == VPTRIG_EFFECT_START && r.sink.nameOf(x.name) == "FX_ARROW")
			++starts;
		if(x.kind == VPTRIG_EFFECT_MOVE && x.handle == a.handle)
			++moves;
	}
	CHECK(starts == 1 && moves >= 5);
	CHECK(r.sink.bad == 0);
}

static void testHeldPose5064()
{
	heldPoseCase(true, false, false, 0.14f, 0.14f, true);    // the fix: the bone's transform, the member's scale
	heldPoseCase(false, false, false, 0.14f, 1.0f, false);   // off: the rule before 5064 (scale 1, identity)
	heldPoseCase(true, true, false, 0.14f, 0.14f, false);    // no transform: the bone's point with identity, still scaled
	heldPoseCase(true, false, true, 0.12f, 0.12f, false);    // a non-finite orientation is refused: identity
	heldPoseCase(true, false, false, 0.0f, 1.0f, true);      // no member scale (0): 1, counted unknown
	{
		RouteParams p;
		PosedPos pp;
		Rig r(p);
		pp.memberScale = 1.0f;
		r.routes->setPositions(&pp);
		r.em->setPositions(&pp);
		r.start(F(1, 1001, 0, 1, 1, 2));
		std::vector<TriggerIn> t;
		t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_ARROW", 5, 0.2, "B"));
		r.step(t);
		CHECK(r.routes->counters().heldScaleUnknown == 1 && r.routes->counters().heldScaleMember == 0);
	}
}

static void testHeldExpires()
{
	Rig r;
	r.start(F(1, 1001, 0, 1, 1, 2));
	std::vector<TriggerIn> t;
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_ARROW", 5, 0.2, "B"));
	r.step(t);
	for(int i = 0; i < 8; ++i)
		r.step();   // 0.27 s: no flight took it within its 0.2 s
	CHECK(r.routes->counters().heldExpired == 1 && r.routes->heldWaiting() == 0);
	CHECK(r.sink.of(VPTRIG_EFFECT_STOP).size() == 1);
	// with the hand-over off the arrow is an ordinary lasting trigger effect of the emitter
	RouteParams p;
	p.heldHandOver = false;
	Rig r2(p);
	r2.start(F(1, 1001, 0, 1, 1, 2));
	r2.step(t);
	CHECK(r2.routes->counters().held == 0 && r2.sink.of(VPTRIG_EFFECT_START).size() == 1);
	// a flight with no held effect waiting starts its own
	r2.begin();
	ProjectileSpec spec;
	spec.player = 0;
	spec.unit = 1;
	spec.effect = "FX_ARROW";
	spec.path.kind = c5r::effects::PathKind::Straight;
	spec.path.p[3] = c5r::effects::V3{10, 0, 0};
	spec.path.duration = 0.1f;
	spec.t0 = r2.routes->now();
	std::string why;
	CHECK(r2.routes->addFlight(spec, why) != 0);
	r2.routes->endFrame();
	r2.em->endFrame();
	CHECK(r2.sink.of(VPTRIG_EFFECT_START).size() == 2);
	// a flight of a unit in no fight is refused
	r2.begin();
	spec.unit = 77;
	CHECK(r2.routes->addFlight(spec, why) == 0 && why.find("no running fight") != std::string::npos);
	r2.routes->endFrame();
	r2.em->endFrame();
}

// the Transfer trigger launches the flight through the planner; the trigger files come by name (lazy HeldEffects)
static void testTransferPlanner()
{
	Rig r;
	r.routes->setHeld(nullptr);
	const char* doc = "<trigger_system type=\"FGrannyTimedTriggerSystem\"><triggers>"
	                  "<trigger type=\"FTimedTriggerEffect\" id=\"5\" ec=\"1100\" time=\"0.3\" duration=\"0.2\" event=\"FX_ARROW\" bone=\"B\"/>"
	                  "<trigger type=\"FTimedTriggerTransfer\" id=\"6\" ec=\"1100\" time=\"0.45\" refid=\"5\"/>"
	                  "</triggers></trigger_system>";
	c5m::TriggerSystem ts;
	c5m::Error e;
	CHECK(c5m::parse_ftsxml(doc, strlen(doc), ts, &e));
	int asked = 0;
	r.routes->setTriggerFiles([&](const std::string& f) -> const c5m::TriggerSystem* {
		++asked;
		return f == "fx_triggers_test.ftsxml" ? &ts : nullptr;
	});
	int planned = 0;
	std::string seenEffect;
	r.routes->setFlightPlanner([&](const TriggerIn& t, const std::string& effect, const float from[3], ProjectileSpec& out,
	                               std::string& why) {
		(void)why;
		++planned;
		seenEffect = effect;
		CHECK(t.trigger == 6 && from[0] == 1.0f && from[2] == 0.5f);
		out.path.kind = c5r::effects::PathKind::Straight;
		out.path.p[0] = c5r::effects::V3{from[0], from[1], from[2]};
		out.path.p[3] = c5r::effects::V3{101, 0, 0};
		out.path.duration = 0.15f;
		out.t0 = r.routes->now();
		out.children.push_back("FX_HIT_DIRT");
		return true;
	});
	r.start(F(1, 1001, 0, 1, 1, 2));
	std::vector<TriggerIn> t;
	t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_ARROW", 5, 0.2, "B"));
	r.step(t);
	CHECK(r.routes->counters().held == 1);
	t.clear();
	t.push_back(T(0, 1, 0, 11, TrigType::Transfer, 1100, "", 6));
	t.push_back(T(0, 1, 1, 11, TrigType::Transfer, 1100, "", 6));   // figure 1 holds nothing
	r.step(t);
	CHECK(planned == 1 && seenEffect == "FX_ARROW");
	const RouteCounters& c = r.routes->counters();
	CHECK(c.transfers == 2 && c.transfersPlanned == 1 && c.transfersNoHeld == 1 && c.heldTaken == 1);
	CHECK(asked == 1);   // the file read once
	for(int i = 0; i < 8; ++i)
		r.step();
	CHECK(c.recChild == 1 && r.sink.of(VPTRIG_EFFECT_STOP).empty());
	CHECK(r.em->counters().transfers == 2);   // the Transfers still reach the emitter's count
	trigchecks::StreamCheck ck = r.check();
	CHECK(ck.ok() && r.sink.bad == 0);
}

// a city volley's sound when it names a known script; a unit weapon's tag is not a script
static void testHitSounds()
{
	Rig r;
	ScriptTable scripts;
	scripts.set("AS3D_CITY_VOLLEY", false);
	r.em->setScripts(&scripts);
	r.routes->setScripts(&scripts);
	r.start(F(1, 1001, 0, 1, 1, 2));
	r.begin();
	std::vector<HitIn> hs;
	HitIn c;
	c.fightSeq = 1;
	c.hit = H(0, -1, 1, 2, 0.0, 0.3, "CITY_BOMBARD_EARLY");
	c.hit.city = true;
	c.hit.projectile = true;
	c.hit.has_origin = true;
	c.hit.origin[0] = 5;
	c.hit.sound = "AS3D_CITY_VOLLEY";
	hs.push_back(c);
	HitIn a;
	a.fightSeq = 1;
	a.hit = H(0, 1, 1, 2, 0.0, 0.0, "FX_MG_HIT_DIRT");
	a.hit.sound = "BULLET";
	hs.push_back(a);
	r.routes->hits(hs, r.routes->now() - kDt);
	r.routes->endFrame();
	r.em->endFrame();
	CHECK(r.routes->counters().hitSounds == 1 && r.routes->counters().hitSoundNotScript == 1);
	std::vector<VPTrigRecord> s = r.sink.of(VPTRIG_SOUND_START);
	CHECK(s.size() == 1 && r.sink.nameOf(s[0].name) == "AS3D_CITY_VOLLEY" && s[0].pos[0] == 5.0f);
}

// A weapon without a HitEffectScale (NULL, 0 or below) starts its HitEffect at the striking member's scale, as the EXE
// takes the member's own value (effects-server.md 1.1 item 1); 1.0 drew the hit about ten times stock's size live
// (effects-formats.md 15.7.8, ticket 4727 r5/r6). Fails with the old "unset -> 1.0".
static void testHitScale()
{
	Rig r;
	r.pos.memberScale = 0.12f;
	r.pos.unknownFigure = 2;
	r.start(F(1, 1001, 0, 1, 1, 2));
	r.begin();
	const double t0 = r.routes->now();
	std::vector<HitIn> hs;
	HitIn a;
	a.fightSeq = 1;
	a.hit = H(0, 1, 1, 2, 0.1, 0.1, "FX_MG_HIT_DIRT");   // HitEffectScale unset (CombatHit's 0)
	hs.push_back(a);
	HitIn b = a;
	b.hit.figure = 2;                                     // its member scale not known: 1.0, counted
	hs.push_back(b);
	HitIn c = a;
	c.hit.scale = 0.5f;                                   // set: the weapon's
	hs.push_back(c);
	r.routes->hits(hs, t0 - kDt);
	r.routes->endFrame();
	r.em->endFrame();
	for(int i = 0; i < 10; ++i)
		r.step();
	size_t at012 = 0, at1 = 0, at05 = 0;
	for(auto& x : r.sink.of(VPTRIG_EFFECT_START))
	{
		if(r.sink.nameOf(x.name) != "FX_MG_HIT_DIRT")
			continue;
		at012 += x.scale == 0.12f ? 1 : 0;
		at1 += x.scale == 1.0f ? 1 : 0;
		at05 += x.scale == 0.5f ? 1 : 0;
	}
	CHECK(at012 == 1 && at1 == 1 && at05 == 1);
	CHECK(r.routes->counters().hitScaleMember == 1 && r.routes->counters().hitScaleUnknown == 1);
	if(!(at012 == 1 && at1 == 1 && at05 == 1))
		printf("  hit scales: member %zu, 1.0 %zu, set %zu\n", at012, at1, at05);
}

// ---------------------------------------------------------------------------------------------- 3. hits
static void testHits()
{
	Rig r;
	r.start(F(1, 1001, 0, 1, 1, 2));
	r.begin();
	const double t0 = r.routes->now();   // the fight's time 0
	std::vector<HitIn> hs;
	HitIn a;
	a.fightSeq = 1;
	a.hit = H(0, 1, 1, 2, 0.1, 0.1, "FX_MG_HIT_$(TERRAIN)");   // no flight: a one-shot at 0.1 s (target on snow -> DIRT)
	a.hit.scale = 0.75f;
	hs.push_back(a);
	HitIn b = a;
	b.hit = H(0, 1, 1, 2, 0.2, 0.5, "FX_MG_HIT_DIRT");
	b.hit.projectile = true;                                    // a unit's projectile: the flight's
	hs.push_back(b);
	HitIn c = a;
	c.hit = H(0, -1, 1, 2, 0.2, 0.8, "CITY_BOMBARD_EARLY");    // a city's volley at the defender
	c.hit.city = true;
	c.hit.projectile = true;
	c.hit.figure = 0;
	c.hit.has_origin = true;
	c.hit.origin[0] = 0;
	c.hit.origin[1] = 0;
	c.hit.origin[2] = 0;
	hs.push_back(c);
	HitIn d = c;
	d.hit.projectile = false;                                   // a city's answer in a melee: nothing
	hs.push_back(d);
	HitIn e = a;
	e.hit.effect = "FX_NOPE_$(TERRAIN)";                        // refused by name
	hs.push_back(e);
	HitIn g = a;
	g.hit.area = true;                                          // an area share: nothing
	hs.push_back(g);
	r.routes->hits(hs, t0 - kDt);
	r.routes->endFrame();
	r.em->endFrame();
	const RouteCounters& k = r.routes->counters();
	CHECK(k.hits == 6 && k.hitOneShot == 1 && k.hitFlight == 1 && k.hitCityVolley == 1 && k.hitCityDefence == 1 &&
	      k.hitRefused == 1 && k.hitNone == 1);
	CHECK(r.routes->pendingHits() == 2);
	for(int i = 0; i < 40; ++i)
		r.step();
	std::vector<VPTrigRecord> s = r.sink.of(VPTRIG_EFFECT_START);
	size_t mg = 0, volley = 0, volleyChild = 0;
	for(auto& x : s)
	{
		const std::string n = r.sink.nameOf(x.name);
		if(n == "FX_MG_HIT_DIRT")
		{
			++mg;
			CHECK(x.pos[0] == 50 && x.pos[1] == 60 && x.scale == 0.75f && x.unit == 1);
		}
		if(n == "CITY_BOMBARD_EARLY")
		{
			++volley;
			CHECK(x.player == 1 && x.unit == 2 && (x.flags & VPTRIG_FLAG_FOLLOWS_OWNER));   // owned by the fight's unit
		}
		if(n == "FX_HIT_SNOW")
			++volleyChild;   // the volley's child with the target's surface (unit 2 stands on snow)
	}
	CHECK(mg == 1 && volley == 1 && volleyChild == 1);
	CHECK(r.sink.of(VPTRIG_EFFECT_MOVE).size() >= 10);
	CHECK(k.recChild == 1 && r.routes->pendingHits() == 0);
	trigchecks::StreamCheck ck = r.check();
	CHECK(ck.ok() && r.sink.bad == 0);
	if(!ck.ok())
		printf("  %s\n", trigchecks::summary(ck).c_str());
}

static void testLateHitAndForcedEnd()
{
	Rig r;
	r.start(F(1, 1001, 0, 1, 1, 2));
	r.begin();
	const double t0 = r.routes->now();
	std::vector<HitIn> hs;
	HitIn a;
	a.fightSeq = 1;
	a.hit = H(0, 1, 1, 2, 0.0, 0.0, "FX_MG_HIT_DIRT");
	hs.push_back(a);
	HitIn c = a;
	c.hit = H(0, -1, 1, 2, 0.1, 2.0, "CITY_BOMBARD_EARLY");   // a long volley still in the air at the end
	c.hit.city = true;
	c.hit.projectile = true;
	c.hit.has_origin = true;
	hs.push_back(c);
	r.routes->hits(hs, t0 - 0.5);   // the fight started 0.5 s ago: the one-shot is 0.5 s late
	r.routes->endFrame();
	r.em->endFrame();
	CHECK(r.routes->counters().hitLate == 1);
	for(int i = 0; i < 10; ++i)
		r.step();
	CHECK(r.routes->flights().live() == 1);
	// the 12 s limit ends the fight: the volley is removed with its trail, its child dropped
	std::vector<OwnerEnd> e;
	e.push_back(E(1, 1001, FightEnd::Limit));
	r.step(std::vector<TriggerIn>(), e);
	CHECK(r.routes->flights().live() == 0 && r.routes->counters().flightsForced == 1 && r.routes->counters().recStop == 1);
	for(int i = 0; i < 70; ++i)
		r.step();
	size_t child = 0;
	for(auto& x : r.sink.of(VPTRIG_EFFECT_START))
		if(r.sink.nameOf(x.name) == "FX_HIT_DIRT")
			++child;
	CHECK(child == 0);
	// the stop comes before the OwnerEnd, nothing of the owner after it
	trigchecks::StreamCheck ck = r.check();
	CHECK(ck.ok() && ck.recordsAfterEnd == 0 && r.sink.bad == 0);
	if(!ck.ok())
		printf("  %s\n", trigchecks::summary(ck).c_str());
	const std::vector<VPTrigRecord>& all = r.sink.recs;
	size_t stopAt = 0, endAt = 0;
	for(size_t i = 0; i < all.size(); ++i)
	{
		if(all[i].kind == VPTRIG_EFFECT_STOP)
			stopAt = i;
		if(all[i].kind == VPTRIG_OWNER_END)
			endAt = i;
	}
	CHECK(stopAt < endAt);
}

// ---------------------------------------------------------------------------------------------- the emitter hunk
static void testOneShotApi()
{
	Rig r;
	r.start(F(1, 1001, 0, 1, 1, 2));
	r.begin();
	StartSpec s;
	s.player = 0;
	s.unit = 1;
	s.name = "FX_HIT_DIRT";
	s.oneShot = true;
	std::string why;
	const uint32_t h = r.em->startEffect(s, why);
	CHECK(h != 0 && r.em->liveEffects() == 0);
	float p[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, v[3] = {0, 0, 0};
	CHECK(!r.em->moveEffect(h, p, q, v, why) && !r.em->stopEffect(h, why));
	s.late = 0.2f;   // more than lateDrop: refused, counted
	CHECK(r.em->startEffect(s, why) == 0 && why.find("lateDrop") != std::string::npos);
	CHECK(r.em->counters().droppedLate == 1);
	s.oneShot = false;   // a lasting start keeps its late start (1.5 item 4 is the caller's then)
	CHECK(r.em->startEffect(s, why) != 0 && r.em->liveEffects() == 1);
	r.routes->endFrame();
	r.em->endFrame();
	std::vector<OwnerEnd> e;
	e.push_back(E(1, 1001, FightEnd::Natural));
	r.step(std::vector<TriggerIn>(), e);
	CHECK(r.sink.of(VPTRIG_EFFECT_STOP).size() == 1);   // only the lasting one
}

static void testLogRoundTrip(const std::string& dir)
{
	// the routes' records through the offline log sink (D137) and back through 4721's reader
	Rig r;
	LogSink log;
	std::string why;
	const std::string path = dir + "/test_effect_routes.trig.log";
	CHECK(log.open(path, why));
	TeeSink tee(r.sink, log);
	TriggerEmitter em(tee);
	em.setSubstitutions(&r.subs);
	em.setPositions(&r.pos);
	EffectRoutes routes(em, *r.names);
	routes.setSubstitutions(&r.subs);
	const Fight f = F(1, 1001, 0, 1, 1, 2);
	std::vector<HitIn> hs;
	HitIn a;
	a.fightSeq = 1;
	a.hit = H(0, 1, 1, 2, 0.05, 0.05, "FX_MG_HIT_$(TERRAIN)");
	hs.push_back(a);
	for(int i = 1; i <= 6; ++i)
	{
		CHECK(em.beginFrame(i, kDt, why) && routes.beginFrame(kDt, why));
		if(i == 1)
		{
			em.fightStarted(f);
			routes.fightStarted(f);
			routes.hits(hs, routes.now() - kDt);
		}
		std::vector<TriggerIn> t;
		if(i == 2)
			t.push_back(T(0, 1, 0, 11, TrigType::Effect, 1100, "FX_MUZZLE", 7, 0, "B"));
		routes.intake(t);
		em.triggers(t);
		std::vector<OwnerEnd> e;
		if(i == 6)
			e.push_back(E(1, 1001, FightEnd::Natural));
		routes.ownerEnds(e);
		em.ownerEnds(e);
		routes.endFrame();
		em.endFrame();
	}
	CHECK(log.close(why));
	CHECK(tee.mismatches() == 0 && log.refused() == 0);
	c5r::unitanim::TriggerLog back;
	c5r::Status st = c5r::unitanim::readTriggerLog(path, back);
	CHECK(st.ok());
	CHECK(back.records.size() == r.sink.recs.size() && back.records.size() == 3);   // MG hit, muzzle, OwnerEnd
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	testIntakeNames();
	testHeldHandOver();
	testHeldExpires();
	testHeldPose5064();   // 5064
	testTransferPlanner();
	testHitSounds();
	testHits();
	testHitScale();
	testLateHitAndForcedEnd();
	testOneShotApi();
	testLogRoundTrip(dir);
#ifdef ROUTES_TEST_INJECT_FAIL
	CHECK(!"FAIL injected");
#endif
	printf("test_effect_routes: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
