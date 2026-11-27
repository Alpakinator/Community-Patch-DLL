// srv_trigger_emitter.cpp - the server's trigger emitter (epic 0042, ticket 4722; srv_trigger_emitter.h).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_trigger_emitter.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace srveffects {

// ---------------------------------------------------------------------------------------------- the ownership table
const char* clipClassName(ClipClass c)
{
	switch(c)
	{
	case ClipClass::Attack: return "Attack";
	case ClipClass::Charge: return "Charge";
	case ClipClass::CombatReady: return "CombatReady";
	case ClipClass::BombardDefend: return "BombardDefend";
	case ClipClass::Death: return "Death";
	case ClipClass::Victory: return "Victory";
	case ClipClass::Movement: return "Movement";
	case ClipClass::Idle: return "Idle";
	case ClipClass::Other: return "Other";
	}
	return "?";
}

// The path-4 codes of unit-controller.md 6.5 (combat-choreography.md 5.2): attacks 1100 (melee), 1130 (anti-air),
// 1140 (charge attack), 1160 (attack city), 1180 (bombard / ranged), 1192 (an attack code with an Action trigger in
// the files); 1120 run charge; 1600 combat ready (1620, 1640 with it); 1280 bombard defend; 1200 death; 1800 victory.
// Movement 1400-1499
// (run 1400, run stop 1440, shuffle 1450, turns 1460 / 1480); idles and their fidgets 1000-1099. Everything else
// (fortify 1500, captured 1190 / 1195, work 1910-1960, activate 1990, unknown codes) is Other.
ClipClass classOfCode(int32_t ec)
{
	int32_t c = ec;
	if(c >= 2000 && c < 3000)
		c -= 1000;   // the B variant the machine draws (unit-controller.md 6.5)
	switch(c)
	{
	case 1100: case 1130: case 1140: case 1160: case 1180: case 1192: return ClipClass::Attack;
	case 1120: return ClipClass::Charge;
	case 1600: case 1620: case 1640: return ClipClass::CombatReady;   // 1620 / 1640: crossed by fighting figures in
	                                                                  // 0557's fights (unit-combat-server.md 11.6) [I: the
	                                                                  // combat-ready loop and its exit, by number]
	case 1280: return ClipClass::BombardDefend;
	case 1200: return ClipClass::Death;
	case 1800: return ClipClass::Victory;
	default: break;
	}
	if(c >= 1400 && c < 1500)
		return ClipClass::Movement;
	if(c >= 1000 && c < 1100)
		return ClipClass::Idle;
	return ClipClass::Other;
}

ClipOwner ownerOfCode(int32_t ec)
{
	switch(classOfCode(ec))
	{
	case ClipClass::Attack:
	case ClipClass::Charge:
	case ClipClass::CombatReady:
	case ClipClass::BombardDefend:
	case ClipClass::Death:
	case ClipClass::Victory: return ClipOwner::Server;
	default: return ClipOwner::Exe;
	}
}

// ---------------------------------------------------------------------------------------------- ScriptTable
std::string ScriptTable::upper(const std::string& s)
{
	std::string o(s);
	for(char& ch : o)
		ch = (char)std::toupper((unsigned char)ch);
	return o;
}

bool ScriptTable::find(const std::string& scriptId, bool& looping) const
{
	auto it = m_.find(upper(scriptId));
	if(it == m_.end())
		return false;
	looping = it->second;
	return true;
}

static std::string trimmed(const std::string& s)
{
	size_t a = 0, b = s.size();
	while(a < b && std::isspace((unsigned char)s[a]))
		++a;
	while(b > a && std::isspace((unsigned char)s[b - 1]))
		--b;
	return s.substr(a, b - a);
}

// The text of <tag>...</tag> inside [from, to); false when absent.
static bool element(const std::string& t, size_t from, size_t to, const char* tag, std::string& out)
{
	const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
	size_t a = t.find(open, from);
	if(a == std::string::npos || a >= to)
		return false;
	a += open.size();
	size_t b = t.find(close, a);
	if(b == std::string::npos || b > to)
		return false;
	out = trimmed(t.substr(a, b - a));
	return true;
}

size_t ScriptTable::loadXml(const std::string& text, size_t* changed)
{
	size_t rows = 0, ch = 0, at = 0;
	const std::string open = "<Script3DSound>", close = "</Script3DSound>";
	for(;;)
	{
		size_t a = text.find(open, at);
		if(a == std::string::npos)
			break;
		size_t b = text.find(close, a);
		if(b == std::string::npos)
			break;
		std::string id, loop;
		if(element(text, a, b, "ScriptID", id) && !id.empty())
		{
			bool l = false;
			if(element(text, a, b, "bLooping", loop))
				l = upper(loop) == "TRUE" || loop == "1";
			const std::string key = upper(id);
			auto it = m_.find(key);
			if(it != m_.end() && it->second != l)
				++ch;
			m_[key] = l;
			++rows;
		}
		at = b + close.size();
	}
	if(changed)
		*changed = ch;
	return rows;
}

// ---------------------------------------------------------------------------------------------- substitution
bool substitute(const std::string& name, Substitutions* subs, int32_t player, int32_t unit, int32_t figure, TrigType type,
                int32_t ec, std::string& out, std::string& why)
{
	out.clear();
	why.clear();
	size_t at = 0;
	for(;;)
	{
		size_t a = name.find("$(", at);
		if(a == std::string::npos)
		{
			out += name.substr(at);
			return true;
		}
		out += name.substr(at, a - at);
		size_t b = name.find(')', a + 2);
		if(b == std::string::npos)
		{
			why = "an unterminated $( in '" + name + "'";
			return false;
		}
		const std::string var = name.substr(a + 2, b - a - 2);
		std::string v;
		bool ok = false;
		if(!subs)
			ok = false;
		else if(var == "TERRAIN")
			ok = subs->terrain(player, unit, v);
		else if(var == "BASETERRAIN")
			ok = subs->baseTerrain(player, unit, v);
		else if(var == "WEAPON")
			ok = subs->weapon(player, unit, figure, type, ec, v);
		else
		{
			why = "unknown variable $(" + var + ") in '" + name + "'";
			return false;
		}
		if(!ok)
		{
			why = "no value for $(" + var + ") in '" + name + "'";
			return false;
		}
		out += v;
		at = b + 1;
	}
}

namespace {
// 5055: the caller's substitutions with $(WEAPON) from weaponFallback (the emitter's second try of a Sound name)
class FallbackSubs : public Substitutions
{
public:
	explicit FallbackSubs(Substitutions& s) : s_(s) {}
	bool terrain(int32_t player, int32_t unit, std::string& out) override { return s_.terrain(player, unit, out); }
	bool baseTerrain(int32_t player, int32_t unit, std::string& out) override { return s_.baseTerrain(player, unit, out); }
	bool weapon(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec, std::string& out) override
	{
		return s_.weaponFallback(player, unit, figure, type, ec, out);
	}

private:
	Substitutions& s_;
};
}  // namespace

bool TriggerEmitter::substituteFallback(const TriggerIn& t, std::string& out)
{
	if(!subs_)
		return false;
	FallbackSubs f(*subs_);
	std::string why;
	return substitute(t.event, &f, t.player, t.unit, t.figure, t.type, t.ec, out, why) && !out.empty();
}

// ---------------------------------------------------------------------------------------------- sinks
SectionSink::~SectionSink()
{
	if(m_section && m_mapping)
		UnmapViewOfFile(m_section);
	if(m_mapping)
		CloseHandle((HANDLE)m_mapping);
}

void SectionSink::init()
{
	VPTrigServer* s = VPTRIG_SERVER(m_section);
	uint32_t gen = 1;
	if(s->magic == VPTRIG_MAGIC || s->generation != 0)
		gen = s->generation + 1;   // a reader of the old generation sees the change (R6); 0 never
	if(gen == 0)
		gen = 1;
	m_gen = gen;
	VPTrigInit(m_section, gen, (uint32_t)GetCurrentProcessId());
	m_taken = VPTRIG_TAKEN_UNDECIDED;   // R9: every generation is decided anew (the shell answers as it decided once)
	m_seenState = VPTRIG_ROUTES_NONE;
	if(m_requireRouteS)
		VPTrigAskRouteS(m_section);
}

void SectionSink::setRequireRouteS(bool on)
{
	m_requireRouteS = on;
	if(on && m_section && m_taken == VPTRIG_TAKEN_UNDECIDED)
		VPTrigAskRouteS(m_section);
}

void SectionSink::decide(uint32_t taken, const std::string& line)
{
	m_taken = taken;
	VPTRIG_SERVER(m_section)->routeSTaken = taken;
	m_lines.push_back(line);
}

static std::string RouteSPartsText(uint32_t parts)
{
	static const char* const kNames[3] = {"combat hold", "sound executor", "effect player"};
	std::string s;
	for(int p = 0; p < 3; ++p)
	{
		s += p ? ", " : "";
		s += kNames[p];
		s += !(parts & (1u << (p + VPTRIG_PART_ANSWERED_SHIFT))) ? " no answer" : (parts & (1u << p)) ? " armed" : " refused";
	}
	return s;
}

void SectionSink::pollRouteS()
{
	if(!m_section || !m_requireRouteS)
		return;
	uint32_t parts = 0, build = 0;
	const uint32_t st = VPTrigRouteSAnswer(m_section, &parts, &build);
	if(st == m_seenState)
		return;
	m_seenState = st;
	const std::string head = "route S (R9, generation " + std::to_string(m_gen) + ", EXE Steam build " +
	                         (build ? std::to_string(build) : std::string("unknown")) + ": " + RouteSPartsText(parts) + ")";
	if(m_taken == VPTRIG_TAKEN_UNDECIDED)
	{
		if(st == VPTRIG_ROUTES_ARMED)
			decide(VPTRIG_TAKEN_YES, "units: " + head + ": TAKEN, the shell answered ARMED: the trigger stream sends its records");
		else if(st == VPTRIG_ROUTES_NOT_ARMED || st == VPTRIG_ROUTES_DISARMED)
			decide(VPTRIG_TAKEN_REFUSED, "LOUD: units: " + head + ": NOT TAKEN, the shell answered " +
			                                 (st == VPTRIG_ROUTES_NOT_ARMED ? "NOT ARMED" : "DISARMED") +
			                                 ": no trigger record is sent; the EXE plays the fights its own way");
		return;
	}
	if(m_taken == VPTRIG_TAKEN_YES && (st == VPTRIG_ROUTES_DISARMED || st == VPTRIG_ROUTES_NOT_ARMED))
		decide(VPTRIG_TAKEN_REFUSED, "LOUD: units: " + head + ": DISARMED by the shell (its watchdog, a part's fault or its "
		                                                       "unload): no further trigger record is sent");
}

bool SectionSink::takeLine(std::string& line)
{
	if(m_lines.empty())
		return false;
	line = m_lines.front();
	m_lines.erase(m_lines.begin());
	return true;
}

bool SectionSink::open(const std::wstring& base, std::string& why)
{
	why.clear();
	if(m_section)
	{
		why = "the trigger section is already open";
		return false;
	}
	const std::wstring name = base + VPTRIG_SECTION_SUFFIX;
	HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPTRIG_SECTION_BYTES, name.c_str());
	if(!h)
	{
		why = "CreateFileMappingW(<base>.Trig) failed " + std::to_string(GetLastError());
		return false;
	}
	void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, VPTRIG_SECTION_BYTES);
	if(!p)
	{
		why = "MapViewOfFile(<base>.Trig) failed " + std::to_string(GetLastError());
		CloseHandle(h);
		return false;
	}
	m_mapping = h;
	m_section = p;
	init();
	return true;
}

void SectionSink::attach(void* section)
{
	m_section = section;
	m_mapping = nullptr;
	init();
}

void SectionSink::stop()
{
	if(m_section)
		VPTrigStop(m_section);
}

uint32_t SectionSink::name(uint32_t kind, const std::string& text)
{
	return m_section ? VPTrigAddName(m_section, kind, text.c_str()) : 0;
}

bool SectionSink::record(const VPTrigRecord& r)
{
	if(!m_section)
		return false;
	if(m_requireRouteS)
	{
		pollRouteS();
		if(m_taken == VPTRIG_TAKEN_UNDECIDED && (r.owner != 0 || r.fightSeq != 0))
			decide(VPTRIG_TAKEN_NO_ANSWER,
			       "LOUD: units: route S (R9, generation " + std::to_string(m_gen) + "): NOT TAKEN, no answer from the shell "
			       "at the first fight record (fight " + std::to_string(r.owner) + ", seq " + std::to_string(r.fightSeq) +
			       "): no trigger record is sent for this generation; the EXE plays the fights its own way");
		if(m_taken != VPTRIG_TAKEN_YES)
		{
			++m_routeSDropped;
			return false;
		}
	}
	return VPTrigWrite(m_section, r);
}

void SectionSink::droppedLate(uint64_t n)
{
	if(!m_section || !n)
		return;
	VPTrigServer* s = VPTRIG_SERVER(m_section);
	VPTrigStore64(&s->droppedLate, VPTrigLoad64(&s->droppedLate) + (int64_t)n);
}

uint32_t TeeSink::name(uint32_t kind, const std::string& text)
{
	const uint32_t x = a_.name(kind, text);
	if(x == 0)
		return 0;   // refused by the first: never offered to the second, so the indexes stay in step
	const uint32_t y = b_.name(kind, text);
	if(x != y)
	{
		++mismatch_;
		return 0;
	}
	return x;
}

bool TeeSink::record(const VPTrigRecord& r)
{
	const bool x = a_.record(r);
	const bool y = b_.record(r);
	return x && y;
}

// ---------------------------------------------------------------------------------------------- the emitter
static uint8_t reasonOf(srvunits::FightEnd e)
{
	return (uint8_t)e;   // vp_trigger.h's VPTrigReason equals FightEnd number for number
}

static_assert((int)srvunits::FightEnd::Natural == VPTRIG_END_NATURAL && (int)srvunits::FightEnd::Limit == VPTRIG_END_LIMIT &&
                  (int)srvunits::FightEnd::Refused == VPTRIG_END_REFUSED && (int)srvunits::FightEnd::Gone == VPTRIG_END_GONE &&
                  (int)srvunits::FightEnd::Reset == VPTRIG_END_RESET &&
                  (int)srvunits::FightEnd::ClipEnded == VPTRIG_END_CLIP_ENDED,
              "FightEnd and VPTrigReason number for number");

bool TriggerEmitter::beginFrame(int64_t frameId, double dt, std::string& why)
{
	why.clear();
	if(inFrame_)
	{
		why = "beginFrame inside a frame (endFrame missing)";
		return false;
	}
	if(!std::isfinite(dt) || dt < 0)
	{
		why = "dt not finite or negative";
		return false;
	}
	// 4728: the same frame id again is a redraw: UnitsHost steps the controller once per frame the server draws, and the
	// server draws a frame id again when its feed changed before the next EXE frame (AnimFeed counts it `redraws`).
	// Refusing it lost that step's crossed triggers and its fights' OwnerEnds (W24's oldpc run, hundreds per arm). It is
	// accepted, its records stamped F (R1 allows equal ids; the shell reads them at its next insertion, R2). An id below
	// the last (AnimFeed's "new composite") keeps the last id as its stamp (R1: never decreasing), counted: dropping the
	// frame would drop its ends and leave loops sounding.
	if(frameId < 0)
	{
		why = "frame id " + std::to_string(frameId) + " below 0";
		return false;
	}
	if(haveFrame_ && frameId <= frame_)
	{
		++c_.redraws;
		if(frameId < frame_)
			++c_.framesBack;
		frameId = frame_;
	}
	inFrame_ = true;
	haveFrame_ = true;
	frame_ = frameId;
	dt_ = dt;
	now_ += dt;
	lateThisFrame_ = 0;
	return true;
}

void TriggerEmitter::fightStarted(const srvunits::Fight& f)
{
	FightRef r;
	r.owner = f.ownerKnown ? f.owner : 0;
	r.seq = f.seq;
	r.ap = f.attacker.owner;
	r.au = f.attacker.unit;
	r.dp = f.defender.owner;
	r.du = f.defender.unit;
	++c_.fights;
	if(r.owner == 0)
		++c_.ownerUnknown;
	if(r.au >= 0)
	{
		units_[UnitKey(r.ap, r.au)] = r;
		tails_.erase(UnitKey(r.ap, r.au));
	}
	if(r.du >= 0)
	{
		units_[UnitKey(r.dp, r.du)] = r;
		tails_.erase(UnitKey(r.dp, r.du));
	}
}

bool TriggerEmitter::fightOwner(uint32_t seq, uint32_t owner)
{
	bool any = false;
	for(auto& kv : units_)
		if(kv.second.seq == seq && kv.second.owner == 0)
		{
			kv.second.owner = owner;
			any = true;
		}
	if(any)
		++c_.ownersLate;
	return any;
}

const TriggerEmitter::FightRef* TriggerEmitter::fightOf(int32_t player, int32_t unit) const
{
	auto it = units_.find(UnitKey(player, unit));
	return it == units_.end() ? nullptr : &it->second;
}

bool TriggerEmitter::ended(const FightRef& f, int32_t player, int32_t unit, int32_t figure, uint64_t instance) const
{
	if(f.seq != 0 && endedSeq_.count(f.seq))
		return true;
	return instance != 0 && endedInst_.count(InstKey(player, unit, figure, instance)) != 0;
}

uint32_t TriggerEmitter::nameIndex(uint32_t kind, const std::string& text)
{
	const auto key = std::make_pair(kind, text);
	auto it = names_.find(key);
	if(it != names_.end())
		return it->second;
	const uint32_t i = sink_.name(kind, text);
	if(i == 0)
	{
		++c_.namesRefused;
		return 0;
	}
	names_[key] = i;
	return i;
}

bool TriggerEmitter::position(const TriggerIn& t, float out[3])
{
	if(!pos_)
		return false;
	if(!t.bone.empty() && pos_->bone(t.player, t.unit, t.figure, t.bone, out))
		return true;
	if(t.figure >= 0 && pos_->figure(t.player, t.unit, t.figure, out))
		return true;
	return pos_->unit(t.player, t.unit, out);
}

bool TriggerEmitter::transform(const TriggerIn& t, float pos[3], float orient[4])
{
	if(pos_ && !t.bone.empty() && pos_->boneTransform(t.player, t.unit, t.figure, t.bone, pos, orient))
	{
		bool finite = true;
		for(int k = 0; k < 3; ++k)
			finite = finite && std::isfinite(pos[k]);
		for(int k = 0; k < 4; ++k)
			finite = finite && std::isfinite(orient[k]);
		if(finite)
		{
			++c_.boneTransforms;
			return true;
		}
	}
	orient[0] = orient[1] = orient[2] = 0;
	orient[3] = 1;
	return position(t, pos);
}

VPTrigRecord TriggerEmitter::base(uint8_t kind, const FightRef& f, int32_t player, int32_t unit, int32_t figure,
                                  uint64_t instance)
{
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = frame_;
	r.version = (uint16_t)VPTRIG_VERSION;
	r.kind = kind;
	r.owner = f.owner;
	r.fightSeq = f.seq;
	r.player = player;
	r.unit = unit;
	r.figure = figure;
	r.instance = instance;
	r.trigger = -1;
	return r;
}

void TriggerEmitter::push(double t, int order, const VPTrigRecord& r)
{
	Pending p;
	p.t = t;
	p.order = order;
	p.r = r;
	pending_.push_back(p);
}

void TriggerEmitter::triggers(const std::vector<TriggerIn>& in)
{
	for(const TriggerIn& t : in)
	{
		++c_.triggers;
		if(t.type == TrigType::Action)
		{
			++c_.actions;
			continue;
		}
		if(t.type == TrigType::Transfer)
		{
			++c_.transfers;
			continue;
		}
		if(t.type == TrigType::Unknown)
		{
			++c_.badType;
			continue;
		}
		const FightRef* f = fightOf(t.player, t.unit);
		if(!f)
		{
			auto tail = tails_.find(UnitKey(t.player, t.unit));
			if(tail != tails_.end() && now_ <= tail->second + 1e-9 && classOfCode(t.ec) == ClipClass::Victory &&
			   t.type != TrigType::StopSound)
			{
				FightRef unitOwned;   // R7's second owner form: player and unit, no fight
				if(t.type == TrigType::Sound)
					sound(t, unitOwned);
				else
					effect(t, unitOwned);
				continue;
			}
			++c_.noFight;
			continue;
		}
		if(ownerOfCode(t.ec) == ClipOwner::Exe && !(p_.movementInFight && classOfCode(t.ec) == ClipClass::Movement))
		{
			++c_.exeClass;
			++c_.exeCodes[t.ec];
			continue;
		}
		if(ended(*f, t.player, t.unit, t.figure, t.instance))
		{
			++c_.afterEnd;
			continue;
		}
		if(t.type == TrigType::Sound)
			sound(t, *f);
		else if(t.type == TrigType::Effect)
			effect(t, *f);
		else
			stopSoundTrigger(t, *f);
	}
}

static double lateOf(double dt, float offset)
{
	double l = dt - (double)offset;
	return l > 0 ? l : 0;
}

void TriggerEmitter::sound(const TriggerIn& t, const FightRef& f)
{
	const bool tail = f.seq == 0 && f.owner == 0;
	if(p_.tierGate && !tierPlays(t.tier, t.unitFigures))
	{
		++c_.droppedTier;   // 4737: the EXE's handler would not play it (exe-audio.md 13); a loop is not renewed either
		return;
	}
	std::string name, why;
	if(!substitute(t.event, subs_, t.player, t.unit, t.figure, t.type, t.ec, name, why) || name.empty())
	{
		++c_.unresolved;
		return;
	}
	bool looping = false;
	if(scripts_ && !scripts_->empty())
	{
		if(!scripts_->find(name, looping))
		{
			// 5055: a name made with the striker's weapon that is no script of the unit gets a second try with the unit's
			// own weapon (Substitutions::weaponFallback; unit-combat-server.md 18.30)
			std::string alt;
			if(!subs_ || t.event.find("$(WEAPON)") == std::string::npos || !substituteFallback(t, alt) || alt == name ||
			   !scripts_->find(alt, looping))
			{
				++c_.unknownScript;
				return;
			}
			name = alt;
		}
	}
	else
		++c_.loopUnknown;
	const double late = lateOf(dt_, t.offset);
	if(looping && tail)
	{
		++c_.afterEnd;   // a loop after the fight: nothing would end it
		return;
	}
	if(looping)
	{
		const uint32_t ni = nameIndex(VPTRIG_NAME_SOUND3D, name);
		if(!ni)
			return;
		const LoopKey key(t.player, t.unit, ni);
		auto it = loops_.find(key);
		const double hb = p_.combatHeartbeat;
		// A Sound trigger with a duration keeps its loop alive over that interval (renewed each frame while the clip is
		// inside it), so its age counts from the interval's end: age = late - duration (unit-combat-server.md 11.3, [I]:
		// 0557's Sound triggers carry durations up to 1.17 s; 4797's live loops lived 0.19-2.66 s)
		const double d = t.duration > 0 ? (double)t.duration : 0.0;
		const double age = late - d;
		if(it != loops_.end())
		{
			// exe-audio.md 4.2: a live loop of this unit and script is renewed, not started again
			if(!it->second.renewed || age < it->second.age)
				it->second.age = age;
			it->second.renewed = true;
			++c_.loopsRenewed;
			return;
		}
		if(age >= hb)
		{
			++c_.droppedLate;   // its heartbeat had already lapsed when the frame shows it
			++lateThisFrame_;
			return;
		}
		if(loops_.size() >= p_.loopSlots)
		{
			++c_.loopsSlotsFull;   // the EXE stops a loop beyond its 64 slots at once
			return;
		}
		float pos[3];
		if(!position(t, pos))
		{
			++c_.noPosition;
			return;
		}
		const uint64_t inst = p_.loopsByFight ? 0 : t.instance;
		VPTrigRecord r = base(VPTRIG_SOUND_START, f, t.player, t.unit, t.figure, inst);
		r.flags = VPTRIG_FLAG_LOOP;
		r.handle = ++nextHandle_;
		r.name = ni;
		r.file = t.file.empty() ? 0 : nameIndex(VPTRIG_NAME_TRIGGERFILE, t.file);
		memcpy(r.pos, pos, sizeof(pos));
		r.ec = t.ec;
		r.trigger = t.trigger;
		r.clipTime = t.clipTime;
		r.late = (float)late;
		push(t.offset, 0, r);
		Loop l;
		l.handle = r.handle;
		l.fseq = f.seq;
		l.instance = inst;
		l.age = age;
		l.renewed = true;
		loops_[key] = l;
		++c_.loopsStarted;
		return;
	}
	if(late > p_.lateDrop)
	{
		++c_.droppedLate;   // 1.5 item 1
		++lateThisFrame_;
		return;
	}
	const uint32_t ni = nameIndex(VPTRIG_NAME_SOUND3D, name);
	if(!ni)
		return;
	float pos[3];
	if(!position(t, pos))
	{
		++c_.noPosition;
		return;
	}
	VPTrigRecord r = base(VPTRIG_SOUND_START, f, t.player, t.unit, t.figure, t.instance);
	r.handle = ++nextHandle_;
	r.name = ni;
	r.file = t.file.empty() ? 0 : nameIndex(VPTRIG_NAME_TRIGGERFILE, t.file);
	memcpy(r.pos, pos, sizeof(pos));
	r.ec = t.ec;
	r.trigger = t.trigger;
	r.clipTime = t.clipTime;
	r.late = (float)late;
	push(t.offset, 0, r);
	if(tail)
		++c_.victoryTail;
}

void TriggerEmitter::effect(const TriggerIn& t, const FightRef& f)
{
	const bool tail = f.seq == 0 && f.owner == 0;
	std::string name, why;
	if(!substitute(t.event, subs_, t.player, t.unit, t.figure, t.type, t.ec, name, why) || name.empty())
	{
		++c_.unresolved;
		return;
	}
	const double late = lateOf(dt_, t.offset);
	const double d = t.duration > 0 ? (double)t.duration : 0.0;
	if(d <= 0)
	{
		if(late > p_.lateDrop)
		{
			++c_.droppedLate;
			++lateThisFrame_;
			return;
		}
	}
	else if(late >= d)
	{
		++c_.droppedLateLasting;   // 1.5 item 4: its interval no longer covers the frame
		++lateThisFrame_;
		return;
	}
	const uint32_t ni = nameIndex(VPTRIG_NAME_EFFECT, name);
	if(!ni)
		return;
	float pos[3], orient[4];
	if(!transform(t, pos, orient))   // the bone's world transform (4793), else its point / the root with identity
	{
		++c_.noPosition;
		return;
	}
	VPTrigRecord r = base(VPTRIG_EFFECT_START, f, t.player, t.unit, t.figure, t.instance);
	r.handle = ++nextHandle_;
	r.name = ni;
	r.file = t.file.empty() ? 0 : nameIndex(VPTRIG_NAME_TRIGGERFILE, t.file);
	memcpy(r.pos, pos, sizeof(pos));
	memcpy(r.orient, orient, sizeof(orient));
	float scale = pos_ ? pos_->figureScale(t.player, t.unit, t.figure) : 1.0f;
	if(!(scale > 0) || !std::isfinite(scale))
	{
		++c_.badScale;
		scale = 1.0f;
	}
	r.scale = scale;
	const bool follows = d > 0 && !t.bone.empty();
	if(follows)
		r.flags = VPTRIG_FLAG_FOLLOWS_OWNER;
	r.ec = t.ec;
	r.trigger = t.trigger;
	r.clipTime = t.clipTime;
	r.late = (float)late;
	push(t.offset, 0, r);
	if(tail)
		++c_.victoryTail;
	if(d > 0)
	{
		if(late > p_.lateDrop)
			++c_.startedLate;
		Effect e;
		e.handle = r.handle;
		e.fseq = f.seq;
		e.player = t.player;
		e.unit = t.unit;
		e.figure = t.figure;
		e.instance = t.instance;
		e.bone = t.bone;
		e.left = d - late;
		e.follows = follows;
		memcpy(e.pos, pos, sizeof(pos));
		effects_[e.handle] = e;
	}
}

void TriggerEmitter::stopSoundTrigger(const TriggerIn& t, const FightRef& f)
{
	// The EXE's StopSound handler stops every tracked sound of the unit (exe-audio.md 6); no install file has one.
	++c_.stopSound;
	for(auto it = loops_.begin(); it != loops_.end();)
	{
		if(std::get<0>(it->first) == t.player && std::get<1>(it->first) == t.unit)
		{
			VPTrigRecord r = base(VPTRIG_SOUND_STOP, f, t.player, t.unit, t.figure, it->second.instance);
			r.handle = it->second.handle;
			r.ec = t.ec;
			r.trigger = t.trigger;
			r.clipTime = t.clipTime;
			push(t.offset, 0, r);
			it = loops_.erase(it);
		}
		else
			++it;
	}
}

void TriggerEmitter::ownerEnds(const std::vector<srvunits::OwnerEnd>& ends)
{
	for(const srvunits::OwnerEnd& e : ends)
		ends_.push_back(e);
}

void TriggerEmitter::endOwner(const srvunits::OwnerEnd& e)
{
	const bool clip = e.why == srvunits::FightEnd::ClipEnded;
	FightRef f;
	f.owner = e.owner;
	f.seq = e.seq;
	// the owner's lasting effects stop before its end (1.5 items 2 and 4); its loops are the shell's (R7)
	for(auto it = effects_.begin(); it != effects_.end();)
	{
		const Effect& x = it->second;
		const bool mine = clip ? (x.player == e.player && x.unit == e.unit && x.figure == e.figure && x.instance == e.instance)
		                       : (x.fseq != 0 && x.fseq == e.seq);
		if(mine)
		{
			VPTrigRecord r = base(VPTRIG_EFFECT_STOP, f, x.player, x.unit, x.figure, x.instance);
			r.handle = x.handle;
			push(dt_, 1, r);
			++c_.effectsOwnerEnd;
			it = effects_.erase(it);
		}
		else
			++it;
	}
	for(auto it = loops_.begin(); it != loops_.end();)
	{
		const bool mine = clip ? (std::get<0>(it->first) == e.player && std::get<1>(it->first) == e.unit &&
		                          it->second.instance == e.instance)
		                       : it->second.fseq == e.seq;
		if(mine)
		{
			++c_.loopsOwnerEnd;
			for(auto a = apiSounds_.begin(); a != apiSounds_.end(); ++a)
				if(a->second == it->first)
				{
					apiSounds_.erase(a);
					break;
				}
			it = loops_.erase(it);
		}
		else
			++it;
	}
	VPTrigRecord r = base(VPTRIG_OWNER_END, f, clip ? e.player : -1, clip ? e.unit : -1, clip ? e.figure : -1,
	                      clip ? e.instance : 0);
	if(!clip)
	{
		// the fight's units, for the log (R7 keys a fight's end on figure -1 and instance 0)
		r.player = e.player;
		r.unit = e.unit;
	}
	r.reason = reasonOf(e.why);
	if(VPTrigPlayedFor(r.reason))
		r.flags = VPTRIG_FLAG_PLAYED;
	push(dt_, 2, r);
	if(clip)
	{
		endedInst_.insert(InstKey(e.player, e.unit, e.figure, e.instance));
		return;
	}
	endedSeq_.insert(e.seq);
	for(auto it = units_.begin(); it != units_.end();)
	{
		if(it->second.seq == e.seq)
		{
			if(e.why == srvunits::FightEnd::Natural && p_.victoryTail > 0)
				tails_[it->first] = now_ + p_.victoryTail;
			for(auto k = endedInst_.begin(); k != endedInst_.end();)
				k = (std::get<0>(*k) == it->first.first && std::get<1>(*k) == it->first.second) ? endedInst_.erase(k) : std::next(k);
			it = units_.erase(it);
		}
		else
			++it;
	}
}

void TriggerEmitter::ageLoops()
{
	for(auto it = loops_.begin(); it != loops_.end();)
	{
		Loop& l = it->second;
		if(!l.renewed)
			l.age += dt_;
		l.renewed = false;
		if(l.age >= p_.combatHeartbeat - 1e-9)
		{
			// exe-audio.md 4.2: not triggered again for the heartbeat: faded out (an immediate end for 3D sounds)
			FightRef f;
			int32_t player = std::get<0>(it->first), unit = std::get<1>(it->first);
			const FightRef* fr = fightOf(player, unit);
			if(fr)
				f = *fr;
			else
				f.seq = l.fseq;
			VPTrigRecord r = base(VPTRIG_SOUND_STOP, f, player, unit, -1, l.instance);
			r.handle = l.handle;
			push(dt_, 0, r);
			++c_.loopsHeartbeat;
			for(auto a = apiSounds_.begin(); a != apiSounds_.end(); ++a)
				if(a->second == it->first)
				{
					apiSounds_.erase(a);
					break;
				}
			it = loops_.erase(it);
		}
		else
			++it;
	}
}

void TriggerEmitter::ageEffects()
{
	for(auto it = effects_.begin(); it != effects_.end();)
	{
		Effect& e = it->second;
		if(e.born)
		{
			e.born = false;
			++it;
			continue;
		}
		FightRef f;
		f.seq = e.fseq;
		const FightRef* fr = fightOf(e.player, e.unit);
		if(fr && fr->seq == e.fseq)
			f = *fr;
		if(e.left >= 0)
		{
			e.left -= dt_;
			if(e.left <= 1e-9)
			{
				VPTrigRecord r = base(VPTRIG_EFFECT_STOP, f, e.player, e.unit, e.figure, e.instance);
				r.handle = e.handle;
				push(dt_, 0, r);
				++c_.effectsEnded;
				it = effects_.erase(it);
				continue;
			}
		}
		if(e.follows && !e.api && pos_)
		{
			TriggerIn t;
			t.player = e.player;
			t.unit = e.unit;
			t.figure = e.figure;
			t.bone = e.bone;
			float p[3], q[4];
			if(transform(t, p, q))
			{
				const float dx = p[0] - e.pos[0], dy = p[1] - e.pos[1], dz = p[2] - e.pos[2];
				if(dx * dx + dy * dy + dz * dz > p_.moveEpsilon * p_.moveEpsilon)
				{
					VPTrigRecord r = base(VPTRIG_EFFECT_MOVE, f, e.player, e.unit, e.figure, e.instance);
					r.handle = e.handle;
					memcpy(r.pos, p, sizeof(p));
					memcpy(r.orient, q, sizeof(q));
					if(dt_ > 0)
					{
						r.vel[0] = (float)(dx / dt_);
						r.vel[1] = (float)(dy / dt_);
						r.vel[2] = (float)(dz / dt_);
					}
					push(dt_, 0, r);
					memcpy(e.pos, p, sizeof(p));
				}
			}
		}
		++it;
	}
}

size_t TriggerEmitter::endFrame()
{
	if(!inFrame_)
		return 0;
	for(const srvunits::OwnerEnd& e : ends_)
		endOwner(e);
	ends_.clear();
	ageLoops();
	ageEffects();
	for(auto it = tails_.begin(); it != tails_.end();)
		it = now_ > it->second + 1e-9 ? tails_.erase(it) : std::next(it);
	std::stable_sort(pending_.begin(), pending_.end(), [](const Pending& a, const Pending& b) {
		return a.order != b.order ? a.order < b.order : a.t < b.t;
	});
	size_t n = 0;
	for(const Pending& p : pending_)
	{
		if(sink_.record(p.r))
		{
			++n;
			++c_.kinds[p.r.kind];
		}
		else
			++c_.sinkDropped;
	}
	pending_.clear();
	sink_.droppedLate(lateThisFrame_);
	lateThisFrame_ = 0;
	inFrame_ = false;
	++c_.frames;
	return n;
}

// ---------------------------------------------------------------------------------------------- the API (4792, 4777)
uint32_t TriggerEmitter::startSound(const StartSpec& s, std::string& why)
{
	why.clear();
	const FightRef* f = fightOf(s.player, s.unit);
	if(!inFrame_ || !f || s.name.empty() || s.name.find("$(") != std::string::npos || !(s.late >= 0) ||
	   ended(*f, s.player, s.unit, s.figure, s.instance))
	{
		++c_.apiRefused;
		why = !inFrame_ ? "startSound outside a frame"
		      : !f      ? "startSound: unit " + std::to_string(s.player) + "/" + std::to_string(s.unit) + " is in no running fight"
		      : s.name.empty() || s.name.find("$(") != std::string::npos ? "startSound: no resolved name"
		      : !(s.late >= 0) ? "startSound: late below 0"
		                       : "startSound: its owner has ended";
		return 0;
	}
	if(!s.loop && s.late > p_.lateDrop)
	{
		++c_.droppedLate;
		++lateThisFrame_;
		why = "startSound: more than lateDrop late (1.5 item 1)";
		return 0;
	}
	const uint32_t ni = nameIndex(VPTRIG_NAME_SOUND3D, s.name);
	if(!ni)
	{
		why = "startSound: the name table refused the name";
		return 0;
	}
	VPTrigRecord r = base(VPTRIG_SOUND_START, *f, s.player, s.unit, s.figure, s.instance);
	r.flags = s.loop ? VPTRIG_FLAG_LOOP : 0;
	r.handle = ++nextHandle_;
	r.name = ni;
	r.file = s.file.empty() ? 0 : nameIndex(VPTRIG_NAME_TRIGGERFILE, s.file);
	memcpy(r.pos, s.pos, sizeof(r.pos));
	r.ec = s.ec;
	r.trigger = s.trigger;
	r.clipTime = s.clipTime;
	r.late = s.late;
	push(dt_ - s.late, 0, r);
	if(s.loop)
	{
		// an API loop lives until stopSound or its owner's end (no heartbeat: the caller owns it)
		const LoopKey key(s.player, s.unit, 0x80000000u | r.handle);
		Loop l;
		l.handle = r.handle;
		l.fseq = f->seq;
		l.instance = s.instance;
		l.age = -1e30;   // never lapses
		loops_[key] = l;
		apiSounds_[r.handle] = key;
		++c_.loopsStarted;
	}
	return r.handle;
}

uint32_t TriggerEmitter::startEffect(const StartSpec& s, std::string& why)
{
	why.clear();
	const FightRef* f = fightOf(s.player, s.unit);
	bool finite = true;
	for(float v : {s.pos[0], s.pos[1], s.pos[2], s.orient[0], s.orient[1], s.orient[2], s.orient[3], s.vel[0], s.vel[1], s.vel[2],
	               s.scale, s.late, s.clipTime})
		finite = finite && std::isfinite(v);
	if(!inFrame_ || !f || s.name.empty() || s.name.find("$(") != std::string::npos || !finite || !(s.scale > 0) ||
	   !(s.late >= 0) || ended(*f, s.player, s.unit, s.figure, s.instance))
	{
		++c_.apiRefused;
		why = !inFrame_ ? "startEffect outside a frame"
		      : !f      ? "startEffect: unit " + std::to_string(s.player) + "/" + std::to_string(s.unit) + " is in no running fight"
		      : s.name.empty() || s.name.find("$(") != std::string::npos ? "startEffect: no resolved name"
		      : !finite || !(s.scale > 0) || !(s.late >= 0) ? "startEffect: a value not finite, scale not above 0 or late below 0"
		                                                    : "startEffect: its owner has ended";
		return 0;
	}
	if(s.oneShot && s.late > p_.lateDrop)
	{
		// 4793: a one-shot effect follows 1.5 item 1 as a one-shot sound does
		++c_.droppedLate;
		++lateThisFrame_;
		why = "startEffect: a one-shot more than lateDrop late (1.5 item 1)";
		return 0;
	}
	const uint32_t ni = nameIndex(VPTRIG_NAME_EFFECT, s.name);
	if(!ni)
	{
		why = "startEffect: the name table refused the name";
		return 0;
	}
	VPTrigRecord r = base(VPTRIG_EFFECT_START, *f, s.player, s.unit, s.figure, s.instance);
	r.flags = s.follows ? VPTRIG_FLAG_FOLLOWS_OWNER : 0;
	r.handle = ++nextHandle_;
	r.name = ni;
	r.file = s.file.empty() ? 0 : nameIndex(VPTRIG_NAME_TRIGGERFILE, s.file);
	memcpy(r.pos, s.pos, sizeof(r.pos));
	memcpy(r.orient, s.orient, sizeof(r.orient));
	r.scale = s.scale;
	memcpy(r.vel, s.vel, sizeof(r.vel));
	r.ec = s.ec;
	r.trigger = s.trigger;
	r.clipTime = s.clipTime;
	r.late = s.late;
	push(dt_ - s.late, 0, r);
	if(s.oneShot)
		return r.handle;   // 4793: not kept (no move, no stop, none at its owner's end)
	Effect e;
	e.handle = r.handle;
	e.fseq = f->seq;
	e.player = s.player;
	e.unit = s.unit;
	e.figure = s.figure;
	e.instance = s.instance;
	e.follows = s.follows;
	e.api = true;
	memcpy(e.pos, s.pos, sizeof(e.pos));
	effects_[e.handle] = e;
	return r.handle;
}

bool TriggerEmitter::moveEffect(uint32_t handle, const float pos[3], const float orient[4], const float vel[3],
                                std::string& why)
{
	why.clear();
	auto it = effects_.find(handle);
	bool finite = pos && orient && vel;
	for(int k = 0; finite && k < 3; ++k)
		finite = std::isfinite(pos[k]) && std::isfinite(vel[k]);
	for(int k = 0; finite && k < 4; ++k)
		finite = std::isfinite(orient[k]);
	if(!inFrame_ || it == effects_.end() || !it->second.api || !finite)
	{
		++c_.apiRefused;
		why = !inFrame_ ? "moveEffect outside a frame"
		      : it == effects_.end() || !it->second.api ? "moveEffect: handle " + std::to_string(handle) + " is not live"
		                                                 : "moveEffect: a value not finite";
		return false;
	}
	const Effect& e = it->second;
	FightRef f;
	f.seq = e.fseq;
	const FightRef* fr = fightOf(e.player, e.unit);
	if(fr && fr->seq == e.fseq)
		f = *fr;
	VPTrigRecord r = base(VPTRIG_EFFECT_MOVE, f, e.player, e.unit, e.figure, e.instance);
	r.handle = handle;
	memcpy(r.pos, pos, sizeof(r.pos));
	memcpy(r.orient, orient, sizeof(r.orient));
	memcpy(r.vel, vel, sizeof(r.vel));
	push(dt_, 0, r);
	memcpy(it->second.pos, pos, sizeof(it->second.pos));
	return true;
}

bool TriggerEmitter::stopEffect(uint32_t handle, std::string& why)
{
	why.clear();
	auto it = effects_.find(handle);
	if(!inFrame_ || it == effects_.end() || !it->second.api)
	{
		++c_.apiRefused;
		why = !inFrame_ ? "stopEffect outside a frame" : "stopEffect: handle " + std::to_string(handle) + " is not live";
		return false;
	}
	const Effect& e = it->second;
	FightRef f;
	f.seq = e.fseq;
	const FightRef* fr = fightOf(e.player, e.unit);
	if(fr && fr->seq == e.fseq)
		f = *fr;
	VPTrigRecord r = base(VPTRIG_EFFECT_STOP, f, e.player, e.unit, e.figure, e.instance);
	r.handle = handle;
	push(dt_, 0, r);
	effects_.erase(it);
	return true;
}

bool TriggerEmitter::stopSound(uint32_t handle, std::string& why)
{
	why.clear();
	auto a = apiSounds_.find(handle);
	if(!inFrame_ || a == apiSounds_.end())
	{
		++c_.apiRefused;
		why = !inFrame_ ? "stopSound outside a frame" : "stopSound: handle " + std::to_string(handle) + " is no live loop";
		return false;
	}
	auto it = loops_.find(a->second);
	if(it != loops_.end())
	{
		FightRef f;
		f.seq = it->second.fseq;
		const FightRef* fr = fightOf(std::get<0>(it->first), std::get<1>(it->first));
		if(fr && fr->seq == f.seq)
			f = *fr;
		VPTrigRecord r = base(VPTRIG_SOUND_STOP, f, std::get<0>(it->first), std::get<1>(it->first), -1, it->second.instance);
		r.handle = handle;
		push(dt_, 0, r);
		loops_.erase(it);
	}
	apiSounds_.erase(a);
	return true;
}

}  // namespace srveffects
