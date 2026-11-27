// test_trigger_emitter.cpp - tests of the server's trigger emitter (server/src/effects/srv_trigger_emitter.*; epic 0042,
// ticket 4722). Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
// Built and run by run_trigger_emitter_test.py (with the fight clock srv_units_fights.cpp, 4721's trigger_log.cpp and
// render's status.cpp).
//
//   test_trigger_emitter.exe [LOGDIR]     every check; "test_trigger_emitter: <n> passed, <m> failed", exit 0 only when
//                                         m == 0 (LOGDIR: where the log round-trip test writes its file; default ".")
//
// The recorded AI turn is 0477's stock run (server/evidence/0477/logs/aiturn-stock-ai1.log: two AI Heavy Bombers on one
// human submarine, 20.05 and 20.07 s, then two barbarian attacks on one human unit, 3.20 and 3.67 s, all while the human
// watched the AI turn), driven through the fight clock as test_units_fights.cpp drives it; the figures' trigger stream
// is a synthetic one per fight (loops, one-shots, lasting effects, EXE-owned clips, clip changes, one long frame), so what
// is tested is the emitter's rules on the clock's real order, barrier, limits and owners. 0557's recorded fights on the
// install's art are trigger_scene.cpp's.
#include "effects/srv_trigger_emitter.h"
#include "effects/srv_trigger_logsink.h"
#include "trigger_checks.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace srveffects;
using namespace srvunits;
using SrvFeed::CombatRecord;

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

// Every record checked by 4721's contract (checkTriggerRecord) and kept; names kept.
class MemSink : public TriggerSink
{
public:
	std::vector<VPTrigRecord> recs;
	std::vector<std::string> names;
	uint64_t late = 0, bad = 0;
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
	void droppedLate(uint64_t n) override { late += n; }
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

// Roots: unit (p, u) at (p*100 + u, 0, 0), its figure f at +f in y, a bone at +0.5 in z when named "B".
class Pos : public FigurePositions
{
public:
	float shift = 0;   // moves every figure (for the following effects)
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
		out[0] = (float)(p * 100 + u) + shift;
		out[1] = 0;
		out[2] = 0;
		return true;
	}
};

class Subs : public Substitutions
{
public:
	bool terrain(int32_t, int32_t, std::string& out) override
	{
		out = "DIRT";
		return true;
	}
	bool weapon(int32_t, int32_t, int32_t, TrigType type, int32_t, std::string& out) override
	{
		out = type == TrigType::Sound ? "BLUNT" : "METAL";
		return true;
	}
};

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

static TriggerIn T(int p, int u, int fig, uint64_t inst, TrigType type, int ec, const char* ev, double offsetD, double durD = 0,
                   const char* bone = "")
{
	const float offset = (float)offsetD, dur = (float)durD;
	TriggerIn t;
	t.player = p;
	t.unit = u;
	t.figure = fig;
	t.instance = inst;
	t.type = type;
	t.ec = ec;
	t.trigger = 3;
	t.clipTime = 0.25f;
	t.offset = offset;
	t.duration = dur;
	t.event = ev;
	t.bone = bone;
	t.file = "fx_triggers_test.ftsxml";
	return t;
}

static OwnerEnd E(uint32_t seq, uint32_t owner, FightEnd why, int p = -1, int u = -1, int fig = -1, uint64_t inst = 0)
{
	OwnerEnd e;
	e.seq = seq;
	e.owner = owner;
	e.why = why;
	e.player = p;
	e.unit = u;
	e.figure = fig;
	e.instance = inst;
	return e;
}

struct Rig
{
	MemSink sink;
	Pos pos;
	Subs subs;
	ScriptTable scripts;
	std::unique_ptr<TriggerEmitter> em;
	int64_t frame = 0;
	explicit Rig(const EmitterParams& p = EmitterParams())
	{
		scripts.set("AS3D_ONE", false);
		scripts.set("AS3D_LOOP", true);
		scripts.set("AS3D_LOOP2", true);
		scripts.set("AS3D_LOOP3", true);
		scripts.set("AS3D_HIT_BLUNT", false);
		em.reset(new TriggerEmitter(sink, p));
		em->setScripts(&scripts);
		em->setSubstitutions(&subs);
		em->setPositions(&pos);
	}
	void begin(double dt = kDt)
	{
		std::string why;
		const bool ok = em->beginFrame(++frame, dt, why);
		CHECK(ok);
	}
	size_t step(const std::vector<TriggerIn>& t = std::vector<TriggerIn>(), const std::vector<OwnerEnd>& e = std::vector<OwnerEnd>(),
	            double dt = kDt)
	{
		begin(dt);
		em->triggers(t);
		em->ownerEnds(e);
		return em->endFrame();
	}
};

static void testOwnershipTable()
{
	for(int ec : {1100, 2100, 1120, 1130, 1140, 2140, 1160, 2160, 1180, 2180, 1200, 2200, 1280, 1600, 1620, 1640, 1800})
		CHECK(ownerOfCode(ec) == ClipOwner::Server);
	for(int ec : {1000, 1010, 2000, 1400, 1440, 1450, 1460, 1480, 2400, 1500, 1190, 1195, 1910, 1990, 0, -1, 5000})
		CHECK(ownerOfCode(ec) == ClipOwner::Exe);
	CHECK(classOfCode(2200) == ClipClass::Death && classOfCode(1800) == ClipClass::Victory);
	CHECK(classOfCode(1450) == ClipClass::Movement && classOfCode(1000) == ClipClass::Idle);
	CHECK(classOfCode(1500) == ClipClass::Other && classOfCode(1600) == ClipClass::CombatReady);
	CHECK(strcmp(clipClassName(ClipClass::Attack), "Attack") == 0);
}

static void testScriptTableAndSubstitution()
{
	ScriptTable t;
	size_t changed = 9;
	const std::string xml =
	    "<Script3DFile><Script3DSounds>\n"
	    "<Script3DSound><ScriptID> AS3D_A </ScriptID><SoundID>X</SoundID><bLooping>True</bLooping></Script3DSound>\n"
	    "<Script3DSound><ScriptID>as3d_b</ScriptID><bLooping>False</bLooping></Script3DSound>\n"
	    "<Script3DSound><ScriptID>AS3D_C</ScriptID></Script3DSound>\n"
	    "<Script3DSound><SoundID>no id</SoundID></Script3DSound>\n"
	    "<Script3DSound><ScriptID>AS3D_A</ScriptID><bLooping>false</bLooping></Script3DSound>\n"
	    "</Script3DSounds></Script3DFile>";
	CHECK(t.loadXml(xml, &changed) == 4);
	CHECK(changed == 1);
	bool l = true;
	CHECK(t.find("as3d_a", l) && !l);   // the later row wins
	CHECK(t.find("AS3D_B", l) && !l);
	CHECK(t.find("AS3D_C", l) && !l);   // no bLooping: not looping
	CHECK(!t.find("AS3D_D", l));
	CHECK(t.size() == 3);

	Subs s;
	std::string out, why;
	CHECK(substitute("A_$(TERRAIN)_$(WEAPON)", &s, 0, 1, 2, TrigType::Sound, 1100, out, why) && out == "A_DIRT_BLUNT");
	CHECK(substitute("B_$(WEAPON)", &s, 0, 1, 2, TrigType::Effect, 1100, out, why) && out == "B_METAL");
	CHECK(substitute("C_$(BASETERRAIN)", &s, 0, 1, 2, TrigType::Effect, 1100, out, why) && out == "C_DIRT");
	CHECK(substitute("PLAIN", nullptr, 0, 1, 2, TrigType::Sound, 1100, out, why) && out == "PLAIN");
	CHECK(!substitute("D_$(OTHER)", &s, 0, 1, 2, TrigType::Sound, 1100, out, why) && why.find("unknown variable") != std::string::npos);
	CHECK(!substitute("E_$(WEAPON", &s, 0, 1, 2, TrigType::Sound, 1100, out, why) && why.find("unterminated") != std::string::npos);
	CHECK(!substitute("F_$(WEAPON)", nullptr, 0, 1, 2, TrigType::Sound, 1100, out, why) && why.find("no value") != std::string::npos);
}

static void testOneShotAndOwnership()
{
	EmitterParams strict;
	strict.movementInFight = false;   // 1.6 strictly by action code
	Rig r(strict);
	r.begin();
	r.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	r.em->triggers({T(1, 10, 0, 55, TrigType::Sound, 1100, "AS3D_ONE", 0.02f),
	                T(1, 10, 1, 56, TrigType::Sound, 1400, "AS3D_ONE", 0.02f),          // run: the EXE's (1.6)
	                T(1, 10, 1, 56, TrigType::Sound, 1000, "AS3D_ONE", 0.02f),          // idle: the EXE's
	                T(3, 30, 0, 57, TrigType::Sound, 1100, "AS3D_ONE", 0.02f),          // no fight
	                T(2, 20, 2, 58, TrigType::Sound, 2200, "AS3D_HIT_$(WEAPON)", 0.03f),  // the fight's death: the server's
	                T(2, 20, 2, 58, TrigType::Action, 2100, "", 0.03f), T(2, 20, 2, 58, TrigType::Transfer, 1160, "", 0.03f)});
	r.em->endFrame();
	const EmitterCounters& c = r.em->counters();
	CHECK(r.sink.recs.size() == 2);
	CHECK(c.exeClass == 2 && c.exeCodes.at(1400) == 1 && c.exeCodes.at(1000) == 1);
	CHECK(c.noFight == 1 && c.actions == 1 && c.transfers == 1 && c.triggers == 7);
	if(r.sink.recs.size() == 2)
	{
		const VPTrigRecord& a = r.sink.recs[0];
		CHECK(a.kind == VPTRIG_SOUND_START && a.frameId == 1 && a.owner == 501 && a.fightSeq == 7 && a.player == 1 && a.unit == 10);
		CHECK(a.figure == 0 && a.instance == 55 && a.handle == 1 && a.flags == 0 && a.ec == 1100 && a.trigger == 3);
		CHECK(r.sink.nameOf(a.name) == "AS3D_ONE" && r.sink.nameOf(a.file) == "fx_triggers_test.ftsxml");
		CHECK(a.pos[0] == 110.0f && a.pos[1] == 0.0f && fabsf(a.late - (float)(kDt - 0.02)) < 1e-6f);
		const VPTrigRecord& b = r.sink.recs[1];
		CHECK(b.kind == VPTRIG_SOUND_START && r.sink.nameOf(b.name) == "AS3D_HIT_BLUNT" && b.handle == 2 && b.ec == 2200);
		CHECK(b.pos[0] == 220.0f && b.pos[1] == 2.0f);
	}
	CHECK(r.sink.names.size() == 3);   // AS3D_ONE, the file, AS3D_HIT_BLUNT: each once
	// the default (movementInFight, lead-0042 #8019): the run is sent, the idle not
	EmitterParams p;
	CHECK(p.movementInFight);
	Rig m(p);
	m.begin();
	m.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	m.em->triggers({T(1, 10, 1, 56, TrigType::Sound, 1400, "AS3D_ONE", 0.02f), T(1, 10, 1, 56, TrigType::Sound, 1000, "AS3D_ONE", 0.02f)});
	m.em->endFrame();
	CHECK(m.sink.recs.size() == 1 && m.em->counters().exeClass == 1);
	CHECK(r.sink.bad == 0 && m.sink.bad == 0);
}

// 4737: the EXE's tier test (research/02-exe-internals/exe-audio.md 13): a Sound trigger plays when its tier is -1 or
// equals the unit's tier (<= 3 figures 0, 4-8 1, 9+ 2); the per-figure detail is tier 0, the beds T1 / T2 pairs.
static TriggerIn TT(int p, int u, int fig, int ec, const char* ev, int tier, int figures)
{
	TriggerIn t = T(p, u, fig, 100 + (uint64_t)fig, TrigType::Sound, ec, ev, 0.02f);
	t.tier = tier;
	t.unitFigures = figures;
	return t;
}

static void testTierGate()
{
	CHECK(unitTierOf(-1) == -1 && unitTierOf(0) == 0 && unitTierOf(3) == 0 && unitTierOf(4) == 1 && unitTierOf(8) == 1);
	CHECK(unitTierOf(9) == 2 && unitTierOf(12) == 2 && unitTierOf(100) == 2);
	CHECK(tierPlays(-1, 12) && tierPlays(-1, 1) && tierPlays(0, -1) && tierPlays(2, -1));
	CHECK(!tierPlays(0, 12) && tierPlays(0, 3) && !tierPlays(0, 4) && tierPlays(1, 6) && !tierPlays(2, 6) && tierPlays(2, 12));
	CHECK(EmitterParams().tierGate);

	// a whole unit (12 figures): the detail (tier 0) is not sent, the -1 sounds and the T2 bed are, the T1 bed is not
	Rig r;
	r.begin();
	r.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	std::vector<TriggerIn> whole;
	for(int fig = 0; fig < 12; ++fig)
	{
		whole.push_back(TT(1, 10, fig, 1100, "AS3D_ONE", 0, 12));        // swing / attack vox: tier 0
		whole.push_back(TT(1, 10, fig, 1200, "AS3D_HIT_$(WEAPON)", -1, 12));   // a death's impact: tier -1
	}
	whole.push_back(TT(1, 10, 0, 1100, "AS3D_LOOP", 1, 12));   // the T1 bed
	whole.push_back(TT(1, 10, 0, 1100, "AS3D_LOOP2", 2, 12));  // the T2 bed
	r.em->triggers(whole);
	r.em->endFrame();
	const EmitterCounters& c = r.em->counters();
	CHECK(c.droppedTier == 13);   // 12 detail + the T1 bed
	size_t one = 0, hit = 0, loop1 = 0, loop2 = 0;
	for(const VPTrigRecord& x : r.sink.recs)
	{
		const std::string n = r.sink.nameOf(x.name);
		one += n == "AS3D_ONE";
		hit += n == "AS3D_HIT_BLUNT";
		loop1 += n == "AS3D_LOOP";
		loop2 += n == "AS3D_LOOP2";
	}
	CHECK(one == 0 && hit == 12 && loop1 == 0 && loop2 == 1);
	CHECK(c.loopsStarted == 1);   // a loop of the dropped tier never starts, so no SoundStop is owed for it

	// the same unit down to 3 figures: the detail plays, the beds of tiers 1 and 2 do not
	Rig s;
	s.begin();
	s.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	s.em->triggers({TT(1, 10, 0, 1100, "AS3D_ONE", 0, 3), TT(1, 10, 1, 1100, "AS3D_ONE", 0, 3), TT(1, 10, 0, 1100, "AS3D_LOOP", 1, 3),
	                TT(1, 10, 0, 1100, "AS3D_LOOP2", 2, 3)});
	s.em->endFrame();
	CHECK(s.sink.recs.size() == 2 && s.em->counters().droppedTier == 2);
	// 6 figures: only the T1 bed; figures not known (-1): everything (the EXE's unit tier -1 plays every trigger)
	Rig u;
	u.begin();
	u.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	u.em->triggers({TT(1, 10, 0, 1100, "AS3D_ONE", 0, 6), TT(1, 10, 0, 1100, "AS3D_LOOP", 1, 6), TT(1, 10, 0, 1100, "AS3D_LOOP2", 2, 6),
	                TT(1, 10, 1, 1100, "AS3D_ONE", 0, -1)});
	u.em->endFrame();
	CHECK(u.sink.recs.size() == 2 && u.em->counters().droppedTier == 2);
	if(u.sink.recs.size() == 2)
	{
		const std::string n0 = u.sink.nameOf(u.sink.recs[0].name), n1 = u.sink.nameOf(u.sink.recs[1].name);
		CHECK((n0 == "AS3D_LOOP" && n1 == "AS3D_ONE") || (n0 == "AS3D_ONE" && n1 == "AS3D_LOOP"));
		CHECK(u.sink.recs[0].figure + u.sink.recs[1].figure == 1);   // the T1 bed of figure 0, the unknown unit's figure 1
	}
	// the gate off (render_test_units_tier_gate=0): every trigger is sent, route S before 4737
	EmitterParams off;
	off.tierGate = false;
	Rig o(off);
	o.begin();
	o.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	o.em->triggers(whole);
	o.em->endFrame();
	CHECK(o.em->counters().droppedTier == 0 && o.sink.recs.size() == 26);
	// effects carry no tier test (exe-audio.md 13.1: 0x53D890 has the Sound handler as its only caller)
	Rig e;
	e.begin();
	e.em->fightStarted(F(7, 501, 1, 10, 2, 20));
	TriggerIn fx = T(1, 10, 0, 55, TrigType::Effect, 1100, "FX_A", 0.02f);
	fx.tier = 0;
	fx.unitFigures = 12;
	e.em->triggers({fx});
	e.em->endFrame();
	CHECK(e.em->counters().droppedTier == 0 && e.sink.recs.size() == 1);
	CHECK(r.sink.bad == 0 && s.sink.bad == 0 && u.sink.bad == 0 && o.sink.bad == 0 && e.sink.bad == 0);
}

static void testLateDropsAndSection()
{
	std::vector<uint8_t> buf(VPTRIG_SECTION_BYTES, 0);
	SectionSink sec;
	sec.attach(buf.data());
	CHECK(sec.generation() == 1);
	TriggerEmitter em(sec);
	Pos pos;
	ScriptTable st;
	st.set("AS3D_ONE", false);
	st.set("AS3D_LOOP", true);
	em.setPositions(&pos);
	em.setScripts(&st);
	std::string why;
	CHECK(em.beginFrame(10, 0.5, why));   // a long frame (a hitch, the window minimised)
	em.fightStarted(F(1, 900, 0, 1, 1, 2));
	em.triggers({T(0, 1, 0, 5, TrigType::Sound, 1100, "AS3D_ONE", 0.1f),    // 0.4 s late: dropped (1.5 item 1)
	             T(0, 1, 0, 5, TrigType::Sound, 1100, "AS3D_ONE", 0.45f),   // 0.05 s late: sent
	             T(0, 1, 1, 6, TrigType::Effect, 1100, "FX_ONE", 0.2f),      // a one-shot effect 0.3 s late: dropped
	             T(0, 1, 1, 6, TrigType::Effect, 1100, "FX_LAST", 0.0f, 0.6f),   // lasting, 0.5 s late, 0.6 s: starts late
	             T(0, 1, 1, 6, TrigType::Effect, 1100, "FX_GONE", 0.0f, 0.4f),   // lasting 0.4 s, 0.5 s late: dropped
	             T(0, 1, 2, 7, TrigType::Sound, 1600, "AS3D_LOOP", 0.3f)});      // a loop 0.2 s late: heartbeat lapsed
	CHECK(em.endFrame() == 2);
	const EmitterCounters& c = em.counters();
	CHECK(c.droppedLate == 3 && c.droppedLateLasting == 1 && c.startedLate == 1);
	const VPTrigServer* s = VPTRIG_SERVER(buf.data());
	CHECK(VPTrigLoad64(&s->droppedLate) == 4);
	// AS3D_ONE, the trigger file, FX_LAST, AS3D_LOOP (a loop's name is its slot key: published before its late test)
	CHECK(VPTrigLoad64(&s->writeSeq) == 2 && s->nameCount == 4);
	// the shell's read at the insertion of frame 9 sees nothing, at 10 both, in the frame's time order (R2): the effect
	// crossed at 0.0 s into the frame, the sound at 0.45 s
	VPTrigCursor cur = {0, 0, 0};
	VPTrigRecord out;
	CHECK(VPTrigReadNext(buf.data(), cur, 9, out) == VPTRIG_READ_NEWGEN);
	CHECK(VPTrigReadNext(buf.data(), cur, 9, out) == VPTRIG_READ_NONE);
	CHECK(VPTrigReadNext(buf.data(), cur, 10, out) == VPTRIG_READ_OK && out.seq == 1 && out.kind == VPTRIG_EFFECT_START &&
	      fabsf(out.late - 0.5f) < 1e-6f && out.scale == 1.0f && out.orient[3] == 1.0f && out.handle == 2);
	CHECK(strcmp(VPTrigNameAt(buf.data(), out.name)->text, "FX_LAST") == 0);
	CHECK(VPTrigReadNext(buf.data(), cur, 10, out) == VPTRIG_READ_OK && out.seq == 2 && out.kind == VPTRIG_SOUND_START &&
	      fabsf(out.late - 0.05f) < 1e-6f && out.handle == 1);
	// the lasting effect has 0.1 s left: stopped two frames of 1/30 s later... at the first frame whose time passes it
	CHECK(em.beginFrame(11, 0.05, why));
	CHECK(em.endFrame() == 0);
	CHECK(em.beginFrame(12, 0.06, why));
	CHECK(em.endFrame() == 1);
	CHECK(VPTrigReadNext(buf.data(), cur, 12, out) == VPTRIG_READ_OK && out.kind == VPTRIG_EFFECT_STOP && out.handle == 2 &&
	      out.frameId == 12);
	CHECK(em.counters().effectsEnded == 1 && em.liveEffects() == 0);
	// 4728 regression (W24's oldpc run: hundreds of "frame id N not above the last (N)" per arm): the same frame id again
	// is a redraw: accepted, counted, its records stamped with that id; a lower id takes the last id as its stamp (R1),
	// so that its records (a fight's end among them) are written, not lost
	{
		const uint64_t r0 = em.counters().redraws;
		CHECK(em.beginFrame(12, kDt, why) && why.empty());
		CHECK(em.endFrame() == 0);
		CHECK(em.counters().redraws == r0 + 1 && em.counters().framesBack == 0);
		CHECK(em.beginFrame(11, kDt, why) && why.empty());
		CHECK(em.endFrame() == 0);
		CHECK(em.counters().redraws == r0 + 2 && em.counters().framesBack == 1);
		CHECK(!em.beginFrame(-1, kDt, why) && why.find("below 0") != std::string::npos);
	}
	// beginFrame refusals
	CHECK(!em.beginFrame(13, -1, why));
	CHECK(!em.beginFrame(13, NAN, why));
	CHECK(em.beginFrame(13, kDt, why));
	CHECK(!em.beginFrame(14, kDt, why) && why.find("inside a frame") != std::string::npos);
	em.endFrame();
	// a second attach is the next generation; an orderly stop clears the magic
	SectionSink sec2;
	sec2.attach(buf.data());
	CHECK(sec2.generation() == 2 && VPTRIG_SERVER(buf.data())->generation == 2);
	CHECK(VPTrigReadNext(buf.data(), cur, 99, out) == VPTRIG_READ_NEWGEN);
	sec2.stop();
	CHECK(VPTRIG_SERVER(buf.data())->magic == 0);
}

// Ticket 4783 (R9, MC ruling #8543): the section sends records only while the shell answers ARMED for its generation.
static VPTrigRecord FightEndRec(uint32_t owner, uint32_t seq, int64_t frame)
{
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = frame;
	r.version = (uint16_t)VPTRIG_VERSION;
	r.kind = VPTRIG_OWNER_END;
	r.reason = VPTRIG_END_NATURAL;
	r.flags = VPTRIG_FLAG_PLAYED;
	r.owner = owner;
	r.fightSeq = seq;
	r.unit = 1;
	r.figure = -1;
	return r;
}

static void testRouteSGate()
{
	std::vector<uint8_t> buf(VPTRIG_SECTION_BYTES, 0);
	void* p = buf.data();
	const VPTrigServer* s = VPTRIG_SERVER(p);
	std::string line;
	// (a) no answer at the first fight record: NOT TAKEN for this generation, nothing written
	SectionSink a;
	a.attach(p);
	CHECK(s->routeSAsk == 0);   // no question until required
	a.setRequireRouteS(true);
	CHECK(s->routeSAsk == 1 && s->routeSTaken == VPTRIG_TAKEN_UNDECIDED);
	CHECK(!a.record(FightEndRec(0, 0, 5)) && a.routeSTaken() == VPTRIG_TAKEN_UNDECIDED);   // no fight: dropped, undecided
	CHECK(!a.record(FightEndRec(77, 3, 5)) && a.routeSTaken() == VPTRIG_TAKEN_NO_ANSWER);
	CHECK(s->routeSTaken == VPTRIG_TAKEN_NO_ANSWER && VPTrigLoad64(&s->writeSeq) == 0);
	CHECK(a.takeLine(line) && line.find("LOUD: units: route S (R9, generation 1): NOT TAKEN, no answer from the shell at the "
	                                    "first fight record (fight 77, seq 3)") == 0);
	CHECK(!a.takeLine(line));
	// a later ARMED answer does not change it: decided once per generation
	VPTrigPublishRouteS(p, 1, VPTRIG_ROUTES_ARMED, 0x707u, 4390913u);
	a.pollRouteS();
	CHECK(!a.record(FightEndRec(78, 4, 6)) && a.routeSTaken() == VPTRIG_TAKEN_NO_ANSWER && a.routeSDropped() == 3);
	CHECK(VPTrigLoad64(&s->writeSeq) == 0);
	// (b) the next generation (a load): ARMED -> TAKEN and records written; DISARMED -> no further record
	SectionSink b;
	b.setRequireRouteS(true);
	b.attach(p);
	CHECK(b.generation() == 2 && s->routeSAsk == 2 && s->routeSTaken == VPTRIG_TAKEN_UNDECIDED);
	b.pollRouteS();
	CHECK(b.routeSTaken() == VPTRIG_TAKEN_UNDECIDED && !b.takeLine(line));   // generation 1's answer is not generation 2's
	VPTrigPublishRouteS(p, 2, VPTRIG_ROUTES_ARMED, 0x707u, 4390913u);
	b.pollRouteS();
	CHECK(b.routeSTaken() == VPTRIG_TAKEN_YES && s->routeSTaken == VPTRIG_TAKEN_YES);
	CHECK(b.takeLine(line) && line.find("units: route S (R9, generation 2, EXE Steam build 4390913: combat hold armed, sound "
	                                    "executor armed, effect player armed): TAKEN") == 0);
	CHECK(b.record(FightEndRec(90, 5, 7)) && VPTrigLoad64(&s->writeSeq) == 1);
	VPTrigPublishRouteS(p, 2, VPTRIG_ROUTES_DISARMED, 0x707u, 4390913u);
	CHECK(!b.record(FightEndRec(91, 6, 8)) && b.routeSTaken() == VPTRIG_TAKEN_REFUSED && VPTrigLoad64(&s->writeSeq) == 1);
	CHECK(s->routeSTaken == VPTRIG_TAKEN_REFUSED);
	CHECK(b.takeLine(line) && line.find("LOUD: units: route S") == 0 && line.find("DISARMED by the shell") != std::string::npos);
	// (c) NOT ARMED: refused, the line names each part's answer
	SectionSink c;
	c.setRequireRouteS(true);
	c.attach(p);
	VPTrigPublishRouteS(p, 3, VPTRIG_ROUTES_NOT_ARMED, 0x604u, 4390913u);
	c.pollRouteS();
	CHECK(c.routeSTaken() == VPTRIG_TAKEN_REFUSED && c.takeLine(line));
	CHECK(line.find("combat hold no answer, sound executor refused, effect player armed): NOT TAKEN, the shell answered NOT "
	                "ARMED") != std::string::npos);
	CHECK(!c.record(FightEndRec(92, 7, 9)) && VPTrigLoad64(&s->writeSeq) == 0);
	// (d) off (tests, offline tools): no question, every record written as before
	SectionSink d;
	d.attach(p);
	CHECK(s->routeSAsk == 0 && d.record(FightEndRec(93, 8, 10)) && VPTrigLoad64(&s->writeSeq) == 1);
}

static void testLoopsHeartbeatAndRenew()
{
	Rig r;
	r.begin();
	r.em->fightStarted(F(3, 777, 1, 10, 2, 20));
	r.em->triggers({T(1, 10, 0, 40, TrigType::Sound, 1600, "AS3D_LOOP", kDt)});   // late 0
	r.em->endFrame();
	CHECK(r.sink.recs.size() == 1 && (r.sink.recs[0].flags & VPTRIG_FLAG_LOOP) && r.em->liveLoops() == 1);
	// re-triggered every frame for 10 frames (another figure of the same unit too): renewed, never started again
	for(int k = 0; k < 10; ++k)
		r.step({T(1, 10, k % 2, 40 + (k % 2), TrigType::Sound, 1600, "AS3D_LOOP", 0.01f)});
	CHECK(r.em->counters().loopsStarted == 1 && r.em->counters().loopsRenewed == 10 && r.sink.recs.size() == 1);
	// the last trigger was 0.0233 s before its frame's time: the heartbeat (0.1 s) lapses 3 frames later
	CHECK(r.step() == 0);
	CHECK(r.step() == 0);
	CHECK(r.step() == 1);
	CHECK(r.sink.recs.back().kind == VPTRIG_SOUND_STOP && r.sink.recs.back().handle == r.sink.recs[0].handle);
	CHECK(r.em->counters().loopsHeartbeat == 1 && r.em->liveLoops() == 0);
	// a new trigger starts a new loop with a new handle; its duration (0.5 s) keeps it alive over that interval, then
	// the heartbeat: stopped in the first frame whose time is 0.6 s or more after the trigger (18 frames)
	r.step({T(1, 10, 0, 42, TrigType::Sound, 1620, "AS3D_LOOP", kDt, 0.5)});
	CHECK(r.sink.recs.back().kind == VPTRIG_SOUND_START && r.sink.recs.back().handle == 2);
	int frames = 0;
	while(r.em->liveLoops() && frames < 100)
	{
		r.step();
		++frames;
	}
	CHECK(frames == 18 && r.sink.recs.back().kind == VPTRIG_SOUND_STOP && r.sink.recs.back().handle == 2);
	// a loop trigger whose interval and heartbeat both lapsed before its frame is dropped; inside them it starts
	r.step({T(1, 10, 0, 43, TrigType::Sound, 1600, "AS3D_LOOP", 0.0, 0.2)}, {}, 0.35);
	CHECK(r.em->liveLoops() == 0 && r.em->counters().droppedLate == 1);
	r.step({T(1, 10, 0, 43, TrigType::Sound, 1600, "AS3D_LOOP", 0.0, 0.3)}, {}, 0.35);
	CHECK(r.em->liveLoops() == 1);
	r.step();
	r.step();   // age 0.05 + 2/30 >= 0.1
	CHECK(r.em->liveLoops() == 0);
	// the slot cap
	EmitterParams p;
	p.loopSlots = 2;
	Rig s(p);
	s.begin();
	s.em->fightStarted(F(3, 777, 1, 10, 2, 20));
	s.em->triggers({T(1, 10, 0, 40, TrigType::Sound, 1600, "AS3D_LOOP", kDt), T(1, 10, 0, 40, TrigType::Sound, 1600, "AS3D_LOOP2", kDt),
	                T(2, 20, 0, 41, TrigType::Sound, 1600, "AS3D_LOOP3", kDt)});
	s.em->endFrame();
	CHECK(s.em->counters().loopsStarted == 2 && s.em->counters().loopsSlotsFull == 1);
	// a StopSound trigger stops every loop of its unit
	s.step({T(1, 10, 0, 40, TrigType::StopSound, 1100, "", 0.01f)});
	CHECK(s.em->counters().stopSound == 1 && s.em->liveLoops() == 0 && s.sink.of(VPTRIG_SOUND_STOP).size() == 2);
	// loopsByFight: the loop is the fight's (instance 0), a ClipEnded does not end it, the fight's end does
	EmitterParams fp;
	CHECK(fp.loopsByFight);   // the default (lead-0042 #8089)
	Rig q(fp);
	q.begin();
	q.em->fightStarted(F(3, 777, 1, 10, 2, 20));
	q.em->triggers({T(1, 10, 0, 40, TrigType::Sound, 1600, "AS3D_LOOP", kDt, 1.0)});
	q.em->endFrame();
	CHECK(q.sink.recs.size() == 1 && q.sink.recs[0].instance == 0 && q.sink.recs[0].figure == 0);
	q.step({}, {E(3, 777, FightEnd::ClipEnded, 1, 10, 0, 40)});
	CHECK(q.em->liveLoops() == 1 && q.em->counters().loopsOwnerEnd == 0);
	q.step({T(1, 10, 1, 41, TrigType::Sound, 1600, "AS3D_LOOP", kDt, 1.0)});   // another figure's clip renews it
	CHECK(q.em->counters().loopsRenewed == 1 && q.em->counters().loopsStarted == 1);
	q.step({}, {E(3, 777, FightEnd::Natural)});
	CHECK(q.em->liveLoops() == 0 && q.em->counters().loopsOwnerEnd == 1);
	CHECK(trigchecks::checkStream(q.sink.recs).ok());
	CHECK(r.sink.bad == 0 && s.sink.bad == 0 && q.sink.bad == 0);
}

static void testOwnerEnds()
{
	EmitterParams byInstance;
	byInstance.loopsByFight = false;   // the note as first written: a loop owned by its clip instance
	Rig r(byInstance);
	r.begin();
	r.em->fightStarted(F(4, 600, 1, 10, 2, 20));
	r.em->triggers({T(1, 10, 0, 70, TrigType::Sound, 1100, "AS3D_LOOP", kDt),                 // loop of instance 70
	                T(1, 10, 1, 71, TrigType::Effect, 1100, "FX_TRAIL", kDt, 2.0f, "B"),       // lasting, follows bone B
	                T(2, 20, 0, 80, TrigType::Sound, 1600, "AS3D_LOOP2", kDt)});              // loop of the defender
	r.em->endFrame();
	CHECK(r.sink.recs.size() == 3);
	CHECK(r.sink.recs[1].kind == VPTRIG_EFFECT_START && (r.sink.recs[1].flags & VPTRIG_FLAG_FOLLOWS_OWNER) &&
	      r.sink.recs[1].pos[2] == 0.5f);
	// the figure moves: EffectMove with the new bone point and a velocity
	r.pos.shift = 1.0f;
	r.step({T(1, 10, 0, 70, TrigType::Sound, 1100, "AS3D_LOOP", kDt), T(2, 20, 0, 80, TrigType::Sound, 1600, "AS3D_LOOP2", kDt)});
	const std::vector<VPTrigRecord> mv = r.sink.of(VPTRIG_EFFECT_MOVE);
	CHECK(mv.size() == 1 && mv[0].pos[0] == 111.0f && fabsf(mv[0].vel[0] - 30.0f) < 1e-3f && mv[0].handle == 2);
	// the clip instance 70 ends (the figure entered a new state): OwnerEnd ClipEnded after its records; its loop is the
	// shell's to stop (no SoundStop); a later trigger of instance 70 is dropped
	r.begin();
	r.em->triggers({T(1, 10, 0, 70, TrigType::Sound, 1100, "AS3D_LOOP", 0.01f)});   // crossed before the end: renewal
	r.em->ownerEnds({E(4, 600, FightEnd::ClipEnded, 1, 10, 0, 70)});
	r.em->endFrame();
	const VPTrigRecord& ce = r.sink.recs.back();
	CHECK(ce.kind == VPTRIG_OWNER_END && ce.reason == VPTRIG_END_CLIP_ENDED && (ce.flags & VPTRIG_FLAG_PLAYED) && ce.instance == 70 &&
	      ce.figure == 0 && ce.owner == 600 && ce.fightSeq == 4);
	CHECK(r.em->counters().loopsOwnerEnd == 1 && r.sink.of(VPTRIG_SOUND_STOP).empty());
	r.step({T(1, 10, 0, 70, TrigType::Sound, 1100, "AS3D_ONE", 0.03f)});
	CHECK(r.em->counters().afterEnd == 1);
	// the fight ends at its limit: the lasting effect is stopped before the OwnerEnd; the defender's loop is the shell's
	r.begin();
	r.em->triggers({T(2, 20, 0, 80, TrigType::Sound, 1600, "AS3D_LOOP2", 0.03f)});
	r.em->ownerEnds({E(4, 600, FightEnd::Limit)});
	r.em->endFrame();
	const size_t n = r.sink.recs.size();
	CHECK(n >= 2 && r.sink.recs[n - 2].kind == VPTRIG_EFFECT_STOP && r.sink.recs[n - 2].handle == 2);
	CHECK(n >= 1 && VPTrigIsFightEnd(r.sink.recs[n - 1]) && r.sink.recs[n - 1].reason == VPTRIG_END_LIMIT &&
	      (r.sink.recs[n - 1].flags & VPTRIG_FLAG_PLAYED) && r.sink.recs[n - 1].owner == 600);
	CHECK(r.em->counters().effectsOwnerEnd == 1 && r.em->liveLoops() == 0 && r.em->liveEffects() == 0);
	CHECK(!r.em->inFight(1, 10) && !r.em->inFight(2, 20));
	// no victory tail after a forced end
	r.step({T(1, 10, 0, 90, TrigType::Sound, 1800, "AS3D_ONE", 0.03f)});
	CHECK(r.em->counters().noFight == 1 && r.em->counters().victoryTail == 0);
	// played clear for Refused, Gone and Reset
	for(FightEnd why : {FightEnd::Refused, FightEnd::Gone, FightEnd::Reset})
	{
		Rig q;
		q.begin();
		q.em->fightStarted(F(9, 0, 1, 10, 2, 20));   // owner not known: fseq names it
		q.em->ownerEnds({E(9, 0, why)});
		q.em->endFrame();
		CHECK(q.sink.recs.size() == 1 && !(q.sink.recs[0].flags & VPTRIG_FLAG_PLAYED) && q.sink.recs[0].fightSeq == 9 &&
		      q.sink.recs[0].owner == 0);
		CHECK(q.em->counters().ownerUnknown == 1 && q.sink.bad == 0);
	}
	trigchecks::StreamCheck sc = trigchecks::checkStream(r.sink.recs);
	CHECK(sc.ok());
	if(!sc.ok())
		for(auto& f : sc.faults)
			printf("  %s\n", f.c_str());
	CHECK(r.sink.bad == 0);
	if(r.sink.bad)
		printf("  %s\n", r.sink.firstBad.c_str());
}

// 4783 (W33 #8801): a fight that started without its EXE event id learns it late (FightClock::setOwner ->
// AnimTriggerTap::fightOwner): the records after it carry the id, its OwnerEnd too (the shell passes that end to its
// hold); without it (the negative) every record stays owner 0.
static void testLateOwner()
{
	Rig r;
	r.begin();
	r.em->fightStarted(F(11, 0, 1, 10, 2, 20));
	r.em->triggers({T(1, 10, 0, 70, TrigType::Sound, 1100, "AS3D_ONE", kDt)});
	r.em->endFrame();
	CHECK(r.sink.recs.size() == 1 && r.sink.recs[0].owner == 0 && r.sink.recs[0].fightSeq == 11);
	CHECK(r.em->fightOwner(11, 1111) && !r.em->fightOwner(12, 5) && r.em->counters().ownersLate == 1);
	CHECK(!r.em->fightOwner(11, 2222));   // set once: a second id changes nothing
	r.step({T(2, 20, 0, 80, TrigType::Sound, 1600, "AS3D_ONE", kDt)}, {E(11, 1111, FightEnd::Natural)});
	CHECK(r.sink.recs.size() == 3 && r.sink.recs[1].kind == VPTRIG_SOUND_START && r.sink.recs[1].owner == 1111 &&
	      VPTrigIsFightEnd(r.sink.recs[2]) && r.sink.recs[2].owner == 1111 && r.sink.recs[2].fightSeq == 11);
	CHECK(r.sink.bad == 0);
	Rig n;   // the negative: no late id
	n.begin();
	n.em->fightStarted(F(13, 0, 1, 10, 2, 20));
	n.em->endFrame();
	n.step({T(1, 10, 0, 70, TrigType::Sound, 1100, "AS3D_ONE", kDt)}, {E(13, 0, FightEnd::Natural)});
	CHECK(n.sink.recs.size() == 2 && n.sink.recs[0].owner == 0 && n.sink.recs[1].owner == 0 && n.em->counters().ownersLate == 0);
}

static void testVictoryTail()
{
	Rig r;
	r.begin();
	r.em->fightStarted(F(5, 650, 1, 10, 2, 20));
	r.em->ownerEnds({E(5, 650, FightEnd::Natural)});
	r.em->endFrame();
	// after a Natural end: victory one-shots owned by the unit; loops not; other clips not
	r.step({T(1, 10, 0, 91, TrigType::Sound, 1800, "AS3D_ONE", 0.03f), T(1, 10, 0, 91, TrigType::Sound, 1800, "AS3D_LOOP", 0.03f),
	        T(1, 10, 0, 91, TrigType::Effect, 2800, "FX_ONE", 0.03f), T(1, 10, 0, 92, TrigType::Sound, 1100, "AS3D_ONE", 0.03f)});
	CHECK(r.em->counters().victoryTail == 2 && r.em->counters().afterEnd == 1 && r.em->counters().noFight == 1);
	const std::vector<VPTrigRecord> st = r.sink.of(VPTRIG_SOUND_START);
	CHECK(st.size() == 1 && st[0].owner == 0 && st[0].fightSeq == 0 && st[0].player == 1 && st[0].unit == 10);
	for(int k = 0; k < 95; ++k)
		r.step();   // 3.2 s later: the tail is over
	r.step({T(1, 10, 0, 93, TrigType::Sound, 1800, "AS3D_ONE", 0.03f)});
	CHECK(r.em->counters().victoryTail == 2 && r.em->counters().noFight == 2);
	CHECK(r.sink.bad == 0);
}

static void testUnknownScriptsAndNames()
{
	Rig r;
	r.begin();
	r.em->fightStarted(F(6, 1, 1, 10, 2, 20));
	r.em->triggers({T(1, 10, 0, 1, TrigType::Sound, 1100, "AS3D_NOT_THERE", 0.03f), T(1, 10, 0, 1, TrigType::Sound, 1100, "X_$(BAD)", 0.03f)});
	r.em->endFrame();
	CHECK(r.em->counters().unknownScript == 1 && r.em->counters().unresolved == 1 && r.sink.recs.empty());
	// no table: every sound a one-shot (counted); no substitutions: a variable does not resolve
	MemSink sink;
	Pos pos;
	TriggerEmitter em(sink);
	em.setPositions(&pos);
	std::string why;
	CHECK(em.beginFrame(1, kDt, why));
	em.fightStarted(F(6, 1, 1, 10, 2, 20));
	em.triggers({T(1, 10, 0, 1, TrigType::Sound, 1100, "AS3D_LOOP", 0.03f), T(1, 10, 0, 1, TrigType::Sound, 1100, "A_$(WEAPON)", 0.03f)});
	em.endFrame();
	CHECK(em.counters().loopUnknown == 1 && em.counters().unresolved == 1 && sink.recs.size() == 1 && sink.recs[0].flags == 0);
	// no positions: not sent
	MemSink s2;
	TriggerEmitter e2(s2);
	CHECK(e2.beginFrame(1, kDt, why));
	e2.fightStarted(F(6, 1, 1, 10, 2, 20));
	e2.triggers({T(1, 10, 0, 1, TrigType::Sound, 1100, "AS3D_ONE", 0.03f)});
	e2.endFrame();
	CHECK(e2.counters().noPosition == 1 && s2.recs.empty());
	// a name the section refuses (120 characters) is counted and not sent
	std::vector<uint8_t> buf(VPTRIG_SECTION_BYTES, 0);
	SectionSink sec;
	sec.attach(buf.data());
	TriggerEmitter e3(sec);
	e3.setPositions(&pos);
	CHECK(e3.beginFrame(1, kDt, why));
	e3.fightStarted(F(6, 1, 1, 10, 2, 20));
	e3.triggers({T(1, 10, 0, 1, TrigType::Sound, 1100, std::string(120, 'A').c_str(), 0.03f)});
	CHECK(e3.endFrame() == 0 && e3.counters().namesRefused == 1);
	CHECK(r.sink.bad == 0 && sink.bad == 0);
}

// 4793's hooks: the bone's world transform and the figure's scale on trigger effects
class XformPos : public Pos
{
public:
	float scale = 2.0f;
	bool boneTransform(int32_t p, int32_t u, int32_t f, const std::string& b, float pos[3], float orient[4]) override
	{
		if(b != "B")
			return false;
		figure(p, u, f, pos);
		pos[2] += 0.75f;
		orient[0] = 0;
		orient[1] = 0;
		orient[2] = 0.70710678f;
		orient[3] = 0.70710678f;
		return true;
	}
	float figureScale(int32_t, int32_t, int32_t) override { return scale; }
};

static void testBoneTransformAndScale()
{
	MemSink sink;
	XformPos pos;
	TriggerEmitter em(sink);
	em.setPositions(&pos);
	std::string why;
	CHECK(em.beginFrame(1, kDt, why));
	em.fightStarted(F(6, 1, 1, 10, 2, 20));
	em.triggers({T(1, 10, 0, 1, TrigType::Effect, 1100, "FX_A", kDt, 0, "B"), T(1, 10, 0, 1, TrigType::Effect, 1100, "FX_B", kDt)});
	em.endFrame();
	CHECK(sink.recs.size() == 2);
	if(sink.recs.size() == 2)
	{
		CHECK(sink.recs[0].pos[2] == 0.75f && sink.recs[0].orient[2] == 0.70710678f && sink.recs[0].scale == 2.0f);
		CHECK(sink.recs[1].pos[2] == 0.0f && sink.recs[1].orient[3] == 1.0f && sink.recs[1].orient[2] == 0.0f &&
		      sink.recs[1].scale == 2.0f);   // no bone: the root, identity
	}
	CHECK(em.counters().boneTransforms == 1);
	pos.scale = -1;
	CHECK(em.beginFrame(2, kDt, why));
	em.triggers({T(1, 10, 0, 1, TrigType::Effect, 1100, "FX_A", kDt, 0, "B")});
	em.endFrame();
	CHECK(sink.recs.size() == 3 && sink.recs.back().scale == 1.0f && em.counters().badScale == 1);
	CHECK(sink.bad == 0);
}

static void testApi()
{
	Rig r;
	std::string why;
	StartSpec s;
	s.player = 1;
	s.unit = 10;
	s.figure = 2;
	s.instance = 33;
	s.name = "ART_DEF_VEFFECT_PROJ_TEST";
	s.pos[0] = 5;
	s.vel[1] = 16.99f;
	s.follows = true;
	s.ec = 1100;
	s.trigger = 9;
	CHECK(r.em->startEffect(s, why) == 0 && why.find("outside a frame") != std::string::npos);
	r.begin();
	CHECK(r.em->startEffect(s, why) == 0 && why.find("no running fight") != std::string::npos);
	r.em->fightStarted(F(8, 808, 1, 10, 2, 20));
	StartSpec bad = s;
	bad.name = "FX_$(TERRAIN)";
	CHECK(r.em->startEffect(bad, why) == 0);
	bad = s;
	bad.scale = 0;
	CHECK(r.em->startEffect(bad, why) == 0);
	bad = s;
	bad.pos[1] = NAN;
	CHECK(r.em->startEffect(bad, why) == 0);
	const uint32_t h = r.em->startEffect(s, why);
	CHECK(h == 1);
	StartSpec snd;
	snd.player = 2;
	snd.unit = 20;
	snd.name = "AS3D_CITY_STRIKE";
	snd.loop = true;
	const uint32_t hs = r.em->startSound(snd, why);
	CHECK(hs == 2);
	r.em->endFrame();
	CHECK(r.sink.recs.size() == 2 && r.sink.recs[0].kind == VPTRIG_EFFECT_START && r.sink.recs[0].vel[1] == 16.99f &&
	      (r.sink.recs[0].flags & VPTRIG_FLAG_FOLLOWS_OWNER) && r.sink.recs[0].owner == 808);
	CHECK((r.sink.recs[1].flags & VPTRIG_FLAG_LOOP) && r.sink.recs[1].figure == -1);
	// moves, no aging of an API effect, an API loop does not lapse
	for(int k = 0; k < 10; ++k)
	{
		r.begin();
		const float p[3] = {5.0f + k, 0, 0}, q[4] = {0, 0, 0, 1}, v[3] = {30, 0, 0};
		CHECK(r.em->moveEffect(h, p, q, v, why));
		r.em->endFrame();
	}
	CHECK(r.sink.of(VPTRIG_EFFECT_MOVE).size() == 10 && r.em->liveLoops() == 1);
	r.begin();
	const float p[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, v[3] = {0, 0, 0}, nanv[3] = {NAN, 0, 0};
	CHECK(!r.em->moveEffect(h, p, q, nanv, why));
	CHECK(!r.em->moveEffect(99, p, q, v, why));
	CHECK(r.em->stopEffect(h, why));
	CHECK(!r.em->stopEffect(h, why));
	CHECK(r.em->stopSound(hs, why));
	CHECK(!r.em->stopSound(hs, why));
	r.em->endFrame();
	CHECK(r.sink.of(VPTRIG_EFFECT_STOP).size() == 1 && r.sink.of(VPTRIG_SOUND_STOP).size() == 1 && r.em->liveLoops() == 0);
	// a projectile in flight at a forced end: stopped before the OwnerEnd, its handle dead after
	r.begin();
	const uint32_t h2 = r.em->startEffect(s, why);
	r.em->endFrame();
	r.begin();
	r.em->ownerEnds({E(8, 808, FightEnd::Gone)});
	r.em->endFrame();
	const size_t n = r.sink.recs.size();
	CHECK(r.sink.recs[n - 2].kind == VPTRIG_EFFECT_STOP && r.sink.recs[n - 2].handle == h2 && r.sink.recs[n - 1].kind == VPTRIG_OWNER_END);
	r.begin();
	CHECK(!r.em->moveEffect(h2, p, q, v, why) && r.em->startEffect(s, why) == 0);
	r.em->endFrame();
	CHECK(r.em->counters().apiRefused == 11);
	CHECK(r.sink.bad == 0);
	if(r.sink.bad)
		printf("  %s\n", r.sink.firstBad.c_str());
	trigchecks::StreamCheck sc = trigchecks::checkStream(r.sink.recs);
	CHECK(sc.ok());
}

static void testLogRoundTrip(const std::string& dir)
{
	std::vector<uint8_t> buf(VPTRIG_SECTION_BYTES, 0);
	SectionSink sec;
	sec.attach(buf.data());
	LogSink log;
	std::string why;
	const std::string path = dir + "/test_trigger_emitter.log";
	CHECK(log.open(path, why));
	TeeSink tee(sec, log);
	TriggerEmitter em(tee);
	Pos pos;
	Subs subs;
	ScriptTable st;
	st.set("AS3D_LOOP_BLUNT", true);
	st.set("AS3D_ONE", false);
	em.setPositions(&pos);
	em.setSubstitutions(&subs);
	em.setScripts(&st);
	int64_t f = 100;
	for(int k = 0; k < 60; ++k)
	{
		CHECK(em.beginFrame(++f, kDt, why));
		if(k == 0)
			em.fightStarted(F(2, 4242, 0, 1, 1, 2));
		std::vector<TriggerIn> t;
		if(k % 10 == 0)
			t.push_back(T(0, 1, 0, 10 + k / 20, TrigType::Sound, 1100, "AS3D_LOOP_$(WEAPON)", 0.01f));
		if(k % 7 == 0)
			t.push_back(T(1, 2, 1, 50, TrigType::Sound, 1600, "AS3D_ONE", 0.02f));
		if(k == 5)
			t.push_back(T(1, 2, 1, 50, TrigType::Effect, 1600, "FX_$(TERRAIN)", 0.02f, 0.3f, "B"));
		em.triggers(t);
		if(k == 40)
			em.ownerEnds({E(2, 4242, FightEnd::ClipEnded, 0, 1, 0, 12)});
		if(k == 59)
			em.ownerEnds({E(2, 4242, FightEnd::Natural)});
		em.endFrame();
	}
	CHECK(log.close(why));
	CHECK(tee.mismatches() == 0 && log.refused() == 0);
	c5r::unitanim::TriggerLog back;
	c5r::Status s = c5r::unitanim::readTriggerLog(path, back);
	CHECK(s.ok());
	if(!s.ok())
		printf("  %s\n", s.str().c_str());
	// the section and the log hold the same records
	VPTrigCursor cur = {0, 0, 0};
	VPTrigRecord r;
	std::vector<VPTrigRecord> ring;
	VPTrigReadResult rr;
	while((rr = VPTrigReadNext(buf.data(), cur, f, r)) != VPTRIG_READ_NONE)
		if(rr == VPTRIG_READ_OK)
			ring.push_back(r);
	CHECK(ring.size() == back.records.size() && !ring.empty());
	bool same = ring.size() == back.records.size();
	for(size_t i = 0; same && i < ring.size(); ++i)
		same = memcmp(&ring[i], &back.records[i], sizeof(VPTrigRecord)) == 0;
	CHECK(same);
	CHECK(back.names.size() == 4 && back.names[0].text == "AS3D_LOOP_BLUNT" && back.names[1].text == "fx_triggers_test.ftsxml");
	trigchecks::StreamCheck sc = trigchecks::checkStream(back.records);
	CHECK(sc.ok() && sc.loops >= 2 && sc.kinds[VPTRIG_EFFECT_MOVE] == 0);
	printf("  log round trip: %zu records; %s\n", back.records.size(), trigchecks::summary(sc).c_str());
}

// ---------------------------------------------------------------------------------------------- 0477's recorded AI turn
struct Plan
{
	double length = 3.0;
	bool air = false;
};
class FakePlayer : public FightPlayer
{
public:
	std::map<uint32_t, Plan> plans;
	TriggerEmitter* em = nullptr;
	FightPlan start(const Fight& f) override
	{
		em->fightStarted(f);   // the hand-off: the emitter learns the fight's units at its start
		const Plan p = plans.count(f.seq) ? plans[f.seq] : Plan();
		FightPlan r;
		r.ok = true;
		r.air = p.air;
		return r;
	}
	bool advance(const Fight& f, double dt) override
	{
		(void)dt;
		const Plan p = plans.count(f.seq) ? plans[f.seq] : Plan();
		return p.length >= 0 && f.elapsed + 1e-9 >= p.length;
	}
};

static CombatRecord R(uint32_t seq, int aOwner, int aUnit, int dOwner, int dUnit, uint32_t eventId)
{
	CombatRecord r;
	memset(&r, 0, sizeof(r));
	r.seq = seq;
	r.kind = SrvFeed::kUnitCombat;
	r.flags = (uint16_t)(SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | SrvFeed::kCombatReturnedKnown);   // AI turn
	r.returned = eventId;
	r.plot = 100;
	r.attackerOwner = aOwner;
	r.attackerUnit = aUnit;
	r.attackerCity = -1;
	r.defenderOwner = dOwner;
	r.defenderUnit = dUnit;
	r.defenderCity = -1;
	return r;
}

static void testRecordedAiTurn()
{
	MemSink sink;
	Pos pos;
	Subs subs;
	ScriptTable st;
	st.set("AS3D_BAT_AMB_BLUNT", true);
	st.set("AS3D_RUN", true);
	st.set("AS3D_SWING", false);
	st.set("AS3D_DEATH", false);
	TriggerEmitter em(sink);
	em.setPositions(&pos);
	em.setSubstitutions(&subs);
	em.setScripts(&st);
	FakePlayer player;
	player.em = &em;
	player.plans[1] = Plan{-1, true};   // the bombers on the submarine: never end by themselves (0477 13.4): the 20 s limit
	player.plans[2] = Plan{-1, true};
	player.plans[3] = Plan{3.20, false};
	player.plans[4] = Plan{3.67, false};
	FightClock clock(player);
	std::string why;
	const struct { uint32_t seq; int ap, au, dp, du; uint32_t ev; } recs[] = {
	    {1, 1, 1052, 0, 1054, 501}, {2, 1, 1053, 0, 1054, 502}, {3, 63, 1045, 0, 1046, 503}, {4, 63, 1049, 0, 1046, 504}};
	for(auto& x : recs)
		CHECK(clock.add(R(x.seq, x.ap, x.au, x.dp, x.du, x.ev), nullptr, 0, why));
	// three figures per unit; each figure's clip instance changes every 0.9 s (a new clip), so ClipEnded records come
	std::map<std::tuple<int, int, int>, uint64_t> inst;
	uint64_t nextInst = 1;
	std::map<std::tuple<int, int, int>, double> since;
	uint64_t frame = 0;
	double t = 0;
	size_t lateFrames = 0;
	for(int k = 0; k < 2000; ++k)
	{
		// one long frame per fight start + 1 s (a hitch): its early triggers come late
		double dt = kDt;
		if(k == 400 || k == 1250)
		{
			dt = 0.4;
			++lateFrames;
		}
		t += dt;
		++frame;
		CHECK(em.beginFrame((int64_t)frame, dt, why));
		// the figures of the playing fights (as the controller holds them at this frame)
		std::vector<FigureInstance> figs;
		for(auto& x : recs)
			for(int side = 0; side < 2; ++side)
			{
				const int p = side ? x.dp : x.ap, u = side ? x.du : x.au;
				if(!em.inFight(p, u))
					continue;
				for(int fig = 0; fig < 3; ++fig)
				{
					auto key = std::make_tuple(p, u, fig);
					if(!inst.count(key) || t - since[key] >= 0.9)
					{
						inst[key] = nextInst++;
						since[key] = t;
					}
					FigureInstance fi;
					fi.player = p;
					fi.unit = u;
					fi.figure = fig;
					fi.instance = inst[key];
					figs.push_back(fi);
				}
			}
		std::vector<OwnerEnd> ends;
		CHECK(clock.frame(frame, dt, &figs, ends, why));
		// each fighting figure's triggers this frame: the battle loop every frame (renewed), a swing every 0.5 s, a
		// lasting effect every 1.5 s, its run (the EXE's clip) every 0.3 s, a death sound at 2.5 s
		std::vector<TriggerIn> trig;
		for(auto& f : figs)
		{
			const auto key = std::make_tuple(f.player, f.unit, f.figure);
			const double age = t - since[key];
			const int step = (int)floor(age / dt + 1e-9);
			const float off = (float)(dt * 0.25);
			trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Sound, 1100, "AS3D_BAT_AMB_$(WEAPON)", off));
			if(step % 15 == 0)
				trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Sound, 2100, "AS3D_SWING", off));
			if(step % 9 == 0)   // the approach run: the server's in a fight (movementInFight)
				trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Sound, 1400, "AS3D_RUN", off));
			if(step % 11 == 0)   // an idle fidget: the EXE's figures' (1.6), never sent
				trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Sound, 1010, "AS3D_SWING", off));
			if(step == 3)
				trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Effect, 1140, "FX_TRAIL", off, 1.5f, "B"));
			if(dt > 0.1)   // the long frame: a swing crossed 0.05 s into it is 0.35 s late (1.5 item 1)
				trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Sound, 2100, "AS3D_SWING", 0.05f));
			if(step == 2 && f.figure == 2)
				trig.push_back(T(f.player, f.unit, f.figure, f.instance, TrigType::Sound, 1200, "AS3D_DEATH", off));
		}
		em.triggers(trig);
		em.ownerEnds(ends);
		em.endFrame();
	}
	CHECK(clock.queued() == 0 && clock.playing() == 0);
	const EmitterCounters& c = em.counters();
	trigchecks::StreamCheck sc = trigchecks::checkStream(sink.recs, true);
	CHECK(sc.ok());
	for(auto& f : sc.faults)
		printf("  %s\n", f.c_str());
	bool runs = false;
	for(auto& r : sink.recs)
		runs = runs || (r.kind == VPTRIG_SOUND_START && r.ec == 1400 && (r.flags & VPTRIG_FLAG_LOOP));
	CHECK(runs);
	// four fights, each ended once, with the EXE's event id; the bombers at the limit (played), the barbarians natural
	std::vector<VPTrigRecord> fe;
	for(auto& r : sink.recs)
		if(VPTrigIsFightEnd(r))
			fe.push_back(r);
	CHECK(fe.size() == 4);
	if(fe.size() == 4)
	{
		CHECK(fe[0].owner == 501 && fe[1].owner == 502 && fe[2].owner == 503 && fe[3].owner == 504);
		CHECK(fe[0].reason == VPTRIG_END_LIMIT && fe[1].reason == VPTRIG_END_LIMIT && fe[2].reason == VPTRIG_END_NATURAL &&
		      fe[3].reason == VPTRIG_END_NATURAL);
		// barriers: each fight's records lie between its start and its end, none overlaps the next
		CHECK(fe[0].frameId < fe[1].frameId && fe[1].frameId < fe[2].frameId && fe[2].frameId < fe[3].frameId);
	}
	// every record of a fight names its owner; no record of the EXE's fidget clip (1.6); late drops in the long frames
	bool owners = true;
	for(auto& r : sink.recs)
		if(r.kind != VPTRIG_OWNER_END && r.fightSeq >= 1 && r.fightSeq <= 4)
			owners = owners && r.owner == 500 + r.fightSeq;
	CHECK(owners);
	CHECK(c.exeClass > 0 && c.exeCodes.size() == 1 && c.exeCodes.count(1010) == 1 && sc.exeRecords == 0);
	CHECK(c.droppedLate > 0 && sink.late == c.droppedLate + c.droppedLateLasting && lateFrames == 2);
	// loops owned by their fight (the default): none ended by a clip's end, every one by the heartbeat or the fight's end
	CHECK(sc.loops > 0 && sc.loopsUnstopped == 0 && sc.loopsByClipEnd == 0 && sc.loopsByFightEnd > 0 && sc.loopsByStop > 0);
	CHECK(sc.clipEnds == clock.counters().clipEnds && c.loopsHeartbeat == sc.loopsByStop);
	CHECK(em.liveLoops() == 0 && em.liveEffects() == 0);
	CHECK(sink.bad == 0);
	if(sink.bad)
		printf("  %s\n", sink.firstBad.c_str());
	printf("  0477 AI turn: %zu records, %llu frames, droppedLate %llu, exeClass %llu, loops started %llu renewed %llu "
	       "heartbeat %llu ownerEnd %llu, effects ended %llu ownerEnd %llu\n  %s\n",
	       sink.recs.size(), (unsigned long long)c.frames, (unsigned long long)c.droppedLate, (unsigned long long)c.exeClass,
	       (unsigned long long)c.loopsStarted, (unsigned long long)c.loopsRenewed, (unsigned long long)c.loopsHeartbeat,
	       (unsigned long long)c.loopsOwnerEnd, (unsigned long long)c.effectsEnded, (unsigned long long)c.effectsOwnerEnd,
	       trigchecks::summary(sc).c_str());
}

// Ticket 5052: the two rules behind the trigger stream's knobs (srv_trigger_emitter.h; unit-combat-server.md 18.25): who
// names a figure's $(WEAPON) in its running fight, and a plot's $(TERRAIN).
static void testStrikerWeaponAndPlotTags()
{
	// DefenderStrikes: the retaliate flag; in an air strike's opening the attacker's damage in this combat decides
	CHECK(DefenderStrikes(true, -1, false, false, true));
	CHECK(!DefenderStrikes(false, -1, false, false, true));
	CHECK(!DefenderStrikes(true, 0, true, false, true));    // the air opening, the attacker took no damage
	CHECK(DefenderStrikes(false, 5, true, false, true));    // the air opening, the attacker took damage
	CHECK(DefenderStrikes(true, 0, true, true, true));      // air on air: the flag
	CHECK(DefenderStrikes(true, -1, true, false, true));    // the damage not known: the flag
	CHECK(DefenderStrikes(true, 0, true, false, false));    // a defender without figures: the flag

	auto noAir = [](int32_t, int32_t) { return false; };
	FightWeaponRoles roles;
	int32_t op = -1, ou = -1;
	CHECK(roles.namer(63, 2, noAir, op, ou) == FightWeaponRoles::Namer::Own);   // no fight
	FightWeaponRoles::Fight ranged;
	ranged.seq = 1;
	ranged.attPlayer = 0;
	ranged.attUnit = 1;
	ranged.defPlayer = 63;
	ranged.defUnit = 2;
	ranged.retaliates = false;
	roles.started(ranged);
	CHECK(roles.namer(63, 2, noAir, op, ou) == FightWeaponRoles::Namer::Opponent && op == 0 && ou == 1);
	CHECK(roles.namer(0, 1, noAir, op, ou) == FightWeaponRoles::Namer::Own);    // the striker keeps its own
	CHECK(roles.namer(63, 3, noAir, op, ou) == FightWeaponRoles::Namer::Own);   // another unit
	FightWeaponRoles::Fight melee = ranged;
	melee.seq = 2;
	melee.defUnit = 4;
	melee.retaliates = true;
	roles.started(melee);
	CHECK(roles.namer(63, 4, noAir, op, ou) == FightWeaponRoles::Namer::Own);   // melee: it strikes back
	FightWeaponRoles::Fight city = ranged;
	city.seq = 3;
	city.attUnit = -1;
	city.attCity = 7;
	city.defUnit = 5;
	roles.started(city);
	CHECK(roles.namer(63, 5, noAir, op, ou) == FightWeaponRoles::Namer::OpponentCity);
	// the air opening: a bomber (air) on a land unit, the record's retaliate flag set, the attacker's damage 0
	FightWeaponRoles::Fight air = ranged;
	air.seq = 4;
	air.attUnit = 8;
	air.defUnit = 6;
	air.retaliates = true;
	air.attackerDamage = 0;
	roles.started(air);
	auto bomber = [](int32_t p, int32_t u) { return p == 0 && u == 8; };
	CHECK(roles.namer(63, 6, bomber, op, ou) == FightWeaponRoles::Namer::Opponent && ou == 8);
	CHECK(roles.namer(63, 6, noAir, op, ou) == FightWeaponRoles::Namer::Own);   // not air: the flag
	CHECK(roles.size() == 4);
	roles.ended(1);
	CHECK(roles.namer(63, 2, noAir, op, ou) == FightWeaponRoles::Namer::Own);   // after its fight's end
	roles.clear();
	CHECK(roles.size() == 0);

	// PlotTags: the feature's tag, else the terrain's, else DIRT
	PlotTags t;
	t.terrain[0] = "DIRT";
	t.terrain[2] = "SAND";
	t.terrain[5] = "WATER";
	t.terrain[3] = "";   // a terrain without a tag
	t.feature[1] = "LEAVES";
	t.feature[4] = "";   // a feature without a tag (flood plains): the terrain's
	std::string out;
	CHECK(t.tagOf(0, 1, out) == PlotTags::From::Feature && out == "LEAVES");
	CHECK(t.tagOf(2, 4, out) == PlotTags::From::Terrain && out == "SAND");
	CHECK(t.tagOf(5, -1, out) == PlotTags::From::Terrain && out == "WATER");
	CHECK(t.tagOf(3, -1, out) == PlotTags::From::Dirt && out == "DIRT");
	CHECK(t.tagOf(-1, -1, out) == PlotTags::From::Dirt && out == "DIRT");
	CHECK(t.tagOf(9, 9, out) == PlotTags::From::Dirt && out == "DIRT");   // unknown ids
}

// Ticket 5055 (unit-combat-server.md 18.30): FightWeaponRoles::namer's `struck` (every unit struck in its running fight
// is named by its striker: both sides of a melee, an attacker struck back, a city striker) and the emitter's second try
// with the unit's own weapon (Substitutions::weaponFallback) when the striker-named Sound is no script of the unit.
static void testStrikerMelee5055()
{
	auto noAir = [](int32_t, int32_t) { return false; };
	int32_t op = -1, ou = -1;
	FightWeaponRoles roles;
	// melee: Spearman 0:1 attacks Warrior 63:4, the defender strikes back
	FightWeaponRoles::Fight melee;
	melee.seq = 1;
	melee.attPlayer = 0;
	melee.attUnit = 1;
	melee.defPlayer = 63;
	melee.defUnit = 4;
	melee.retaliates = true;
	roles.started(melee);
	CHECK(roles.namer(63, 4, noAir, op, ou) == FightWeaponRoles::Namer::Own);         // 5052: melee keeps its own
	CHECK(roles.namer(0, 1, noAir, op, ou) == FightWeaponRoles::Namer::Own);
	op = ou = -1;
	CHECK(roles.namer(63, 4, noAir, op, ou, true) == FightWeaponRoles::Namer::Opponent && op == 0 && ou == 1);   // by the attacker
	op = ou = -1;
	CHECK(roles.namer(0, 1, noAir, op, ou, true) == FightWeaponRoles::Namer::Opponent && op == 63 && ou == 4);   // by the defender
	CHECK(roles.namer(63, 9, noAir, op, ou, true) == FightWeaponRoles::Namer::Own);   // a unit in no fight
	// ranged without retaliation: the defender by the attacker (as 5052), the attacker nobody strikes keeps its own
	FightWeaponRoles::Fight ranged = melee;
	ranged.seq = 2;
	ranged.attUnit = 2;
	ranged.defUnit = 5;
	ranged.retaliates = false;
	roles.started(ranged);
	op = ou = -1;
	CHECK(roles.namer(63, 5, noAir, op, ou, true) == FightWeaponRoles::Namer::Opponent && op == 0 && ou == 2);
	CHECK(roles.namer(0, 2, noAir, op, ou, true) == FightWeaponRoles::Namer::Own);
	CHECK(roles.namer(0, 2, noAir, op, ou) == FightWeaponRoles::Namer::Own);
	// a city strikes a unit: OpponentCity (the caller keeps the unit's own, counted), struck or not
	FightWeaponRoles::Fight cityAtt = melee;
	cityAtt.seq = 3;
	cityAtt.attUnit = -1;
	cityAtt.attCity = 7;
	cityAtt.defUnit = 6;
	cityAtt.retaliates = false;
	roles.started(cityAtt);
	CHECK(roles.namer(63, 6, noAir, op, ou, true) == FightWeaponRoles::Namer::OpponentCity);
	CHECK(roles.namer(63, 6, noAir, op, ou) == FightWeaponRoles::Namer::OpponentCity);
	// a unit attacks a city that strikes back: the attacker's striker is the city (OpponentCity); without the strike back
	// it keeps its own
	FightWeaponRoles::Fight cityDef = melee;
	cityDef.seq = 4;
	cityDef.attUnit = 3;
	cityDef.defUnit = -1;
	cityDef.retaliates = true;
	roles.started(cityDef);
	CHECK(roles.namer(0, 3, noAir, op, ou, true) == FightWeaponRoles::Namer::OpponentCity);
	CHECK(roles.namer(0, 3, noAir, op, ou) == FightWeaponRoles::Namer::Own);           // 5052: an attacker keeps its own
	FightWeaponRoles::Fight cityQuiet = cityDef;
	cityQuiet.seq = 5;
	cityQuiet.attUnit = 11;
	cityQuiet.retaliates = false;
	roles.started(cityQuiet);
	CHECK(roles.namer(0, 11, noAir, op, ou, true) == FightWeaponRoles::Namer::Own);
	// the air opening: a bomber 0:8 on a land unit 63:12: the defender answers only when the bomber took damage
	FightWeaponRoles::Fight air = melee;
	air.seq = 6;
	air.attUnit = 8;
	air.defUnit = 12;
	air.retaliates = true;
	air.attackerDamage = 0;
	roles.started(air);
	auto bomber = [](int32_t p, int32_t u) { return p == 0 && u == 8; };
	CHECK(roles.namer(0, 8, bomber, op, ou, true) == FightWeaponRoles::Namer::Own);   // not answered
	op = ou = -1;
	CHECK(roles.namer(63, 12, bomber, op, ou, true) == FightWeaponRoles::Namer::Opponent && ou == 8);
	roles.ended(6);
	air.seq = 7;
	air.attackerDamage = 5;
	roles.started(air);
	op = ou = -1;
	CHECK(roles.namer(0, 8, bomber, op, ou, true) == FightWeaponRoles::Namer::Opponent && op == 63 && ou == 12);   // answered
	CHECK(roles.size() == 6);

	// the emitter's second try: the striker's weapon first, the unit's own when that name is no script
	class MeleeSubs : public Substitutions
	{
	public:
		bool fallback = true;
		int fallbackCalls = 0;
		bool terrain(int32_t, int32_t, std::string& out) override
		{
			out = "DIRT";
			return true;
		}
		// the Warrior (player 63) is struck by a SPEAR, the Spearman (player 0) by a BLUNT weapon
		bool weapon(int32_t p, int32_t, int32_t, TrigType, int32_t, std::string& out) override
		{
			out = p == 63 ? "SPEAR" : "BLUNT";
			return true;
		}
		bool weaponFallback(int32_t p, int32_t, int32_t, TrigType, int32_t, std::string& out) override
		{
			++fallbackCalls;
			if(!fallback)
				return false;
			out = p == 63 ? "BLUNT" : "SPEAR";   // each unit's own
			return true;
		}
	};
	ScriptTable scripts;
	scripts.set("AS3D_W_IMPACT_SPEAR", false);   // the Warrior has an impact for the spear
	scripts.set("AS3D_W_IMPACT_BLUNT", false);
	scripts.set("AS3D_S_IMPACT_SPEAR", false);   // the Spearman has none for the club: its own is the second try
	scripts.set("AS3D_S_BAT_AMB_BLUNT", true);   // a bed by the opponent's weapon
	MeleeSubs subs;
	MemSink sink;
	Pos pos;
	TriggerEmitter em(sink);
	em.setScripts(&scripts);
	em.setSubstitutions(&subs);
	em.setPositions(&pos);
	std::string why;
	CHECK(em.beginFrame(1, kDt, why));
	em.fightStarted(F(9, 601, 0, 1, 63, 4));
	em.triggers({T(63, 4, 0, 71, TrigType::Sound, 1200, "AS3D_W_IMPACT_$(WEAPON)", 0.02f),   // by the striker: found
	             T(0, 1, 0, 72, TrigType::Sound, 1200, "AS3D_S_IMPACT_$(WEAPON)", 0.02f),    // BLUNT no script: own SPEAR
	             T(0, 1, 1, 73, TrigType::Sound, 1600, "AS3D_S_BAT_AMB_$(WEAPON)", 0.02f),   // the bed by the striker
	             T(0, 1, 0, 74, TrigType::Sound, 1200, "AS3D_S_FALL_$(WEAPON)", 0.02f),      // neither name: unknown
	             T(0, 1, 0, 75, TrigType::Sound, 1200, "AS3D_S_NOT_THERE", 0.02f)});         // no $(WEAPON): no second try
	em.endFrame();
	CHECK(sink.recs.size() == 3 && em.counters().unknownScript == 2);
	if(sink.recs.size() == 3)
	{
		CHECK(sink.nameOf(sink.recs[0].name) == "AS3D_W_IMPACT_SPEAR" && sink.recs[0].unit == 4);
		CHECK(sink.nameOf(sink.recs[1].name) == "AS3D_S_IMPACT_SPEAR" && sink.recs[1].unit == 1 && sink.recs[1].flags == 0);
		CHECK(sink.nameOf(sink.recs[2].name) == "AS3D_S_BAT_AMB_BLUNT" && sink.recs[2].flags != 0);   // a loop
	}
	CHECK(subs.fallbackCalls == 2);   // the club's impact and the fall: only names with $(WEAPON) that were no script
	// no second try (weaponFallback false, as every Substitutions before 5055): the name is not sent
	MeleeSubs quiet;
	quiet.fallback = false;
	MemSink s2;
	TriggerEmitter e2(s2);
	e2.setScripts(&scripts);
	e2.setSubstitutions(&quiet);
	e2.setPositions(&pos);
	CHECK(e2.beginFrame(1, kDt, why));
	e2.fightStarted(F(9, 601, 0, 1, 63, 4));
	e2.triggers({T(0, 1, 0, 72, TrigType::Sound, 1200, "AS3D_S_IMPACT_$(WEAPON)", 0.02f)});
	e2.endFrame();
	CHECK(s2.recs.empty() && e2.counters().unknownScript == 1 && quiet.fallbackCalls == 1);
	// the default Substitutions::weaponFallback says no
	Subs plain;
	std::string out;
	CHECK(!plain.weaponFallback(0, 1, 0, TrigType::Sound, 1200, out) && out.empty());
	CHECK(sink.bad == 0 && s2.bad == 0);
}

// Ticket 5058 (unit-combat-server.md 18.34): a striking city's weapon by its owner's era (stock's selector), the era
// carried by FightWeaponRoles for either city side, and the emitter's second try when the unit has no IMPACT_CITY_* script.
static void testCityEra5058()
{
	CHECK(std::string(CityWeaponTag(0)) == "CITY_EARLY");
	CHECK(std::string(CityWeaponTag(1)) == "CITY_MID");
	CHECK(std::string(CityWeaponTag(2)) == "CITY_LATE");
	CHECK(std::string(CityWeaponTag(3)) == "CITY_EARLY");   // renaissance: stock's first weapon again
	CHECK(std::string(CityWeaponTag(4)) == "CITY_EARLY");
	CHECK(std::string(CityWeaponTag(7)) == "CITY_EARLY");
	CHECK(std::string(CityWeaponTag(255)) == "CITY_EARLY");
	auto noAir = [](int32_t, int32_t) { return false; };
	int32_t op = -1, ou = -1, era = 9;
	FightWeaponRoles roles;
	// a city of player 0 (era 2) strikes the Warrior 63:6
	FightWeaponRoles::Fight cityAtt;
	cityAtt.seq = 1;
	cityAtt.attPlayer = 0;
	cityAtt.attCity = 7;
	cityAtt.defPlayer = 63;
	cityAtt.defUnit = 6;
	cityAtt.attCityEra = 2;
	roles.started(cityAtt);
	CHECK(roles.namer(63, 6, noAir, op, ou, false, &era) == FightWeaponRoles::Namer::OpponentCity && era == 2);
	era = 9;
	CHECK(roles.namer(63, 6, noAir, op, ou, true, &era) == FightWeaponRoles::Namer::OpponentCity && era == 2);
	CHECK(roles.namer(63, 6, noAir, op, ou) == FightWeaponRoles::Namer::OpponentCity);   // no out: as before
	// the Spearman 0:3 attacks a city of player 63 (era 4) that strikes back: the attacker's striker is the city
	FightWeaponRoles::Fight cityDef;
	cityDef.seq = 2;
	cityDef.attPlayer = 0;
	cityDef.attUnit = 3;
	cityDef.defPlayer = 63;
	cityDef.retaliates = true;
	cityDef.defCityEra = 4;
	roles.started(cityDef);
	era = 9;
	CHECK(roles.namer(0, 3, noAir, op, ou, true, &era) == FightWeaponRoles::Namer::OpponentCity && era == 4);
	era = 9;
	CHECK(roles.namer(0, 3, noAir, op, ou, false, &era) == FightWeaponRoles::Namer::Own && era == -1);   // 5052's rule
	// the era not known (a fight recorded without a player record): -1, the caller keeps the unit's own
	FightWeaponRoles::Fight unknown = cityAtt;
	unknown.seq = 3;
	unknown.defUnit = 8;
	unknown.attCityEra = -1;
	roles.started(unknown);
	era = 9;
	CHECK(roles.namer(63, 8, noAir, op, ou, true, &era) == FightWeaponRoles::Namer::OpponentCity && era == -1);
	// a unit's striker: Opponent, the era out reset to -1
	FightWeaponRoles::Fight melee;
	melee.seq = 4;
	melee.attPlayer = 0;
	melee.attUnit = 1;
	melee.defPlayer = 63;
	melee.defUnit = 4;
	melee.retaliates = true;
	melee.attCityEra = 3;   // ignored: not a city side
	roles.started(melee);
	era = 9;
	op = ou = -1;
	CHECK(roles.namer(63, 4, noAir, op, ou, true, &era) == FightWeaponRoles::Namer::Opponent && op == 0 && ou == 1 && era == -1);
	era = 9;
	CHECK(roles.namer(63, 9, noAir, op, ou, true, &era) == FightWeaponRoles::Namer::Own && era == -1);   // in no fight

	// the emitter: the Warrior has IMPACT_CITY_LATE, the Spearman no IMPACT_CITY_EARLY (its own BLUNT is the second try)
	class CitySubs : public Substitutions
	{
	public:
		int fallbackCalls = 0;
		bool terrain(int32_t, int32_t, std::string& out) override
		{
			out = "DIRT";
			return true;
		}
		bool weapon(int32_t p, int32_t, int32_t, TrigType, int32_t, std::string& out) override
		{
			out = CityWeaponTag(p == 63 ? 2 : 3);
			return true;
		}
		bool weaponFallback(int32_t p, int32_t, int32_t, TrigType, int32_t, std::string& out) override
		{
			++fallbackCalls;
			out = p == 63 ? "BLUNT" : "SPEAR";
			return true;
		}
	};
	ScriptTable scripts;
	scripts.set("AS3D_W_IMPACT_CITY_LATE", false);
	scripts.set("AS3D_S_IMPACT_SPEAR", false);
	CitySubs subs;
	MemSink sink;
	Pos pos;
	TriggerEmitter em(sink);
	em.setScripts(&scripts);
	em.setSubstitutions(&subs);
	em.setPositions(&pos);
	std::string why;
	CHECK(em.beginFrame(1, kDt, why));
	em.fightStarted(F(9, 601, 0, 3, 63, 6));
	em.triggers({T(63, 6, 0, 81, TrigType::Sound, 1200, "AS3D_W_IMPACT_$(WEAPON)", 0.02f),    // CITY_LATE: found
	             T(0, 3, 0, 82, TrigType::Sound, 1200, "AS3D_S_IMPACT_$(WEAPON)", 0.02f)});   // CITY_EARLY none: own
	em.endFrame();
	CHECK(sink.recs.size() == 2 && em.counters().unknownScript == 0 && subs.fallbackCalls == 1);
	if(sink.recs.size() == 2)
	{
		CHECK(sink.nameOf(sink.recs[0].name) == "AS3D_W_IMPACT_CITY_LATE" && sink.recs[0].unit == 6);
		CHECK(sink.nameOf(sink.recs[1].name) == "AS3D_S_IMPACT_SPEAR" && sink.recs[1].unit == 3);
	}
	CHECK(sink.bad == 0);
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	testStrikerWeaponAndPlotTags();   // 5052
	testStrikerMelee5055();           // 5055
	testCityEra5058();                // 5058
	testOwnershipTable();
	testScriptTableAndSubstitution();
	testOneShotAndOwnership();
	testTierGate();   // 4737
	testLateDropsAndSection();
	testRouteSGate();
	testLoopsHeartbeatAndRenew();
	testOwnerEnds();
	testLateOwner();   // 4783
	testVictoryTail();
	testUnknownScriptsAndNames();
	testBoneTransformAndScale();
	testApi();
	testLogRoundTrip(dir);
	testRecordedAiTurn();
#ifdef EMITTER_TEST_INJECT_FAIL
	CHECK(!"FAIL injected");
#endif
	printf("test_trigger_emitter: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
