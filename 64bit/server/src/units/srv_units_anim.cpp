// srv_units_anim.cpp - the controller fed from the server's unit scene (epic 0375, issue 4456). Contract: srv_units_anim.h.
// Written 2026-11-16 for this repository (GPL-3.0-or-later). No EXE code (D83): the mapping is unit-controller.md 3's and
// the controller is 0374's (c5r::unitanim), linked, not re-implemented.
#include "srv_units_anim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>

#include "c5r/units/draw.h"   // loadStringDatabases
#include "c5r/units/placement.h"   // 0685: facingYaw
#include "effects/srv_trigger_tap.h"   // issue 4722

namespace srvunits {

namespace cu = c5r::units;
namespace ca = c5r::unitanim;

// ---- Issue 4772: the walk's heading ---------------------------------------------------------------------------------
static const double kPi = 3.14159265358979323846;

int WalkDirection(float dx, float dy)
{
	if(!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0f && dy == 0.0f))
		return -1;
	// heading(d) = 60 - 60 d degrees: d = (60 - heading) / 60, rounded to the nearest step
	const double h = std::atan2((double)dy, (double)dx) * 180.0 / kPi;
	double s = (60.0 - h) / 60.0;
	s = s - 6.0 * std::floor(s / 6.0);   // [0, 6)
	int d = (int)std::floor(s + 0.5);   // a tie (s = k + 0.5) goes up; mapped to the lower value below
	if(std::fabs(s - std::floor(s) - 0.5) < 1e-9)
		d = (int)std::floor(s);
	return d % 6;
}

float WalkOffsetDeg(int facing, float dx, float dy)
{
	if(facing < 0 || facing > 5 || !std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0f && dy == 0.0f))
		return 0.0f;
	double o = std::atan2((double)dy, (double)dx) * 180.0 / kPi - (60.0 - 60.0 * facing);
	o = o - 360.0 * std::floor(o / 360.0);   // [0, 360)
	if(o > 180.0)
		o -= 360.0;                          // (-180, 180]
	return (float)o;
}

// ---- AnimFeed -------------------------------------------------------------------------------------------------------
void AnimFeed::take(const SrvFeed::Event& e, const SrvFeed::Batch& b)
{
	if(!m_started)
		return;
	Op o;
	o.kind = e.kind;
	o.owner = e.a[0];
	o.id = e.a[1];
	o.a2 = e.a[2];
	o.a3 = e.a[3];
	o.seq = e.seq;   // issue 4774
	m_lastTakenSeq = e.seq;
	if(e.kind == SrvFeed::kUnitMoved && e.a[2] >= 0 && (size_t)e.a[2] < b.paths.size())
	{
		const SrvFeed::PathEntry& pe = b.paths[(size_t)e.a[2]];
		o.pathLen = (int32_t)pe.length;
		if(pe.length && (size_t)pe.first + pe.length <= b.pathPlots.size())
			o.dest = b.pathPlots[(size_t)pe.first + pe.length - 1];   // issue 4774: the held view's plot after the move
	}
	if(e.kind == SrvFeed::kUnitCombat || e.kind == SrvFeed::kCityCombat)
	{
		// issue 4774: the fight clock's input (the core hands combats on after its own checks)
		if(e.a[2] < 0 || (size_t)e.a[2] >= b.combats.size() || b.combats[(size_t)e.a[2]].seq != e.seq)
		{
			m_c.combatsBad++;
			return;
		}
		const SrvFeed::CombatRecord& r = b.combats[(size_t)e.a[2]];
		CombatIn c;
		c.rec = r;
		if(r.nMembers && (size_t)r.firstMember + r.nMembers <= b.combatMembers.size())
			c.members.assign(b.combatMembers.begin() + r.firstMember, b.combatMembers.begin() + r.firstMember + r.nMembers);
		if(e.kind == SrvFeed::kCityCombat && r.attackerCity >= 0 && e.a[3] > 0)
			c.cityPlot = e.a[3] - 1;   // ticket 5062: the attacking city's plot (srv_feed.h kind 28)
		o.combat = (int32_t)m_combats.size();
		m_combats.push_back(c);
		m_c.combats++;
	}
	m_ops.push_back(o);
}

void AnimFeed::arrive(int32_t owner, int32_t id)
{
	if(!m_started)
		return;
	Op o;
	o.kind = kOpArrive;
	o.owner = owner;
	o.id = id;
	o.seq = m_lastTakenSeq;   // issue 4774: the order of a held arrive among the fight's ops
	m_ops.push_back(o);
}

void AnimFeed::start()
{
	m_started = true;
	m_startPending = true;
	m_ops.clear();
	m_combats.clear();   // issue 4774
	m_late.clear();      // issue 4783
	m_unformed.clear();  // issue 4739: the start's creates try every unit again
	m_afterWalkOps.clear();    // ticket 5065
	m_afterWalkBack.clear();
	m_afterWalkCombats.clear();   // ticket 5066 (a)
	m_fightTaint.clear();
	m_fightHeldMoves.clear();
}

// ---- ticket 5066 (a): the mover's combat behind its open walk ---------------------------------------------------------
void AnimFeed::setFightAfterWalk(bool on)
{
	m_fightAfterWalk = on;
	holdAfterWalk(SrvFeed::kUnitCombat, on);
	holdAfterWalk(SrvFeed::kCityCombat, on);
}

bool AnimFeed::holdCombatsAfterWalk() const
{
	return m_posExe && m_clock && (holdsAfterWalk(SrvFeed::kUnitCombat) || holdsAfterWalk(SrvFeed::kCityCombat));
}

// The units a fight names, as fightsFrame begins them: the attacker, the defender, the members that are units (no city,
// no duplicate: the defender may be the interceptor too).
void AnimFeed::fightersOf(const SrvFeed::CombatRecord& rec, const std::vector<SrvFeed::CombatMember>& members,
                          std::vector<std::pair<int32_t, int32_t>>& out) const
{
	out.clear();
	std::vector<std::pair<int32_t, int32_t>> all;
	all.push_back(std::make_pair(rec.attackerOwner, rec.attackerUnit));
	all.push_back(std::make_pair(rec.defenderOwner, rec.defenderUnit));
	for(const SrvFeed::CombatMember& m : members)
		if(!(m.flags & 1u))
			all.push_back(std::make_pair(m.player, m.id));
	for(const auto& f : all)
		if(f.second >= 0 && std::find(out.begin(), out.end(), f) == out.end())
			out.push_back(f);
}

void AnimFeed::untaint(int64_t walkKey)
{
	for(auto it = m_fightTaint.begin(); it != m_fightTaint.end();)
	{
		it->second.erase(walkKey);
		it = it->second.empty() ? m_fightTaint.erase(it) : std::next(it);
	}
}

// W125 (the two-walks edge): a unit two waiting combats name may wait behind two walks; its ops wait while any of them still
// holds a queue. walkKey = the first such walk (an op given back by it is examined again and waits behind the next).
bool AnimFeed::taintedQueue(int64_t unitKey, int64_t& walkKey) const
{
	auto t = m_fightTaint.find(unitKey);
	if(t == m_fightTaint.end())
		return false;
	for(int64_t wk : t->second)
		if(m_afterWalkOps.count(wk))
		{
			walkKey = wk;
			return true;
		}
	return false;
}

// The close's end (ticket 5065's give-back): every op given back this close goes first at the next close, in feed order;
// ticket 5066 (a): a combat that waited takes its record back into m_combats (fightsFrame cleared it this close) and every
// op loses its wait mark (holdBehindWalks decides again: a walk opened since holds it once more).
void AnimFeed::giveBackAfterWalk(std::vector<Op>& back)
{
	std::stable_sort(back.begin(), back.end(), [](const Op& x, const Op& y) { return x.seq < y.seq; });
	for(Op& o : back)
	{
		if(!o.fightWait)
			continue;
		o.fightWait = false;
		if(o.combat != -2)
			continue;
		auto c = m_afterWalkCombats.find(o.seq);
		if(c == m_afterWalkCombats.end())
		{
			o.combat = -1;   // cannot happen (the record goes with its op); passes as a kind with no event
			continue;
		}
		o.combat = (int32_t)m_combats.size();
		m_combats.push_back(c->second);
		m_combats.back().resumed = true;   // ticket 0742: a later combat may have been added meanwhile (below the clock's last seq)
		m_afterWalkCombats.erase(c);
	}
	back.insert(back.end(), m_ops.begin(), m_ops.end());
	m_ops.swap(back);
	back.clear();
}

// After the fights' ops of a close: a view taken for a waiting combat is kept while a waiting combat names its unit; then
// a fight that names it owns it (its end releases it), a walk back keeps it, else it goes (Quick Combat, a refused fight).
void AnimFeed::preFrozenFrame()
{
	if(m_preFrozen.empty())
		return;
	std::set<std::pair<int32_t, int32_t>> need;
	std::vector<std::pair<int32_t, int32_t>> f;
	for(const auto& kv : m_afterWalkOps)
		for(const Op& w : kv.second)
		{
			if(w.combat != -2)
				continue;
			auto c = m_afterWalkCombats.find(w.seq);
			if(c == m_afterWalkCombats.end())
				continue;
			fightersOf(c->second.rec, c->second.members, f);
			need.insert(f.begin(), f.end());
		}
	for(auto it = m_preFrozen.begin(); it != m_preFrozen.end();)
	{
		if(need.count(*it))
		{
			++it;
			continue;
		}
		if(!m_hold.named(it->first, it->second) && !m_returning.count(*it))
			m_view.release(it->first, it->second);
		it = m_preFrozen.erase(it);
	}
}

void AnimFeed::holdAfterWalk(uint16_t kind, bool on)
{
	if(on)
		m_afterWalkKinds.insert(kind);
	else
		m_afterWalkKinds.erase(kind);
}

void AnimFeed::setOrdersAfterWalk(bool on)
{
	holdAfterWalk(SrvFeed::kUnitWork, on);
	holdAfterWalk(SrvFeed::kUnitFortify, on);
}

bool AnimFeed::ordersAfterWalk() const
{
	return holdsAfterWalk(SrvFeed::kUnitWork) && holdsAfterWalk(SrvFeed::kUnitFortify);
}

// Ticket 5065: the one point every taken op passes before it is routed (the fights' hold, the controller's events). A walk
// is open as the op loop below will see it, op by op in feed order: open at the close's start (m_walks), opened by a kind
// 12 of a held unit, closed by an arrive (not one whose move is in this close: the loop carries that to the next close), a
// teleport or rebase (a place), a removal, a creation or a load. An op whose kind is in the table and whose unit's walk is
// open waits in m_afterWalkOps[unit] (feed order); endWalk gives the unit's ops back. A unit a fight names is the fights'
// hold's (its ops keep their place in m_ops for it).
void AnimFeed::holdBehindWalks()
{
	if(!m_posExe || m_afterWalkKinds.empty() || m_ops.empty())
		return;
	const bool fw = holdCombatsAfterWalk();   // ticket 5066 (a)
	std::set<int64_t> open;
	for(const auto& kv : m_walks)
		open.insert(kv.first);
	std::set<int64_t> movedNow;
	std::set<int64_t> namedMoves;   // ticket 5066 (a): a kind 12 of a unit a fight names, earlier in this batch
	std::vector<std::pair<int32_t, int32_t>> fighters;
	std::vector<Op> keep;
	keep.reserve(m_ops.size());
	for(const Op& o : m_ops)
	{
		const int64_t key = UnitKeyOf(o.owner, o.id);
		if(o.kind == SrvFeed::kLoadBegin)
		{
			open.clear();
			movedNow.clear();
			namedMoves.clear();
			// ticket 5066 (a): a load drops the waiting combats and the ops behind them (the fights' hold drops its own)
			if(!m_afterWalkCombats.empty() || !m_fightTaint.empty())
			{
				for(auto it = m_afterWalkOps.begin(); it != m_afterWalkOps.end();)
				{
					std::vector<Op>& q = it->second;
					const size_t before = q.size();
					q.erase(std::remove_if(q.begin(), q.end(), [](const Op& w) { return w.fightWait; }), q.end());
					m_c.fightWalkDropped += before - q.size();
					it = q.empty() ? m_afterWalkOps.erase(it) : std::next(it);
				}
				m_afterWalkCombats.clear();
				m_fightTaint.clear();
			}
			keep.push_back(o);
			continue;
		}
		// ticket 5066 (a): the mover's combat waits behind its open walk (or behind a kind 12 the fights' hold keeps for it),
		// and behind a waiting combat that names one of its units (feed order); its units' later ops wait behind it
		if(fw && o.combat >= 0 && (size_t)o.combat < m_combats.size() && m_afterWalkKinds.count(o.kind))
		{
			const CombatIn& c = m_combats[(size_t)o.combat];
			fightersOf(c.rec, c.members, fighters);
			auto walking = [&](int64_t k) {
				return open.count(k) || namedMoves.count(k) || m_fightHeldMoves.count(k);
			};
			int64_t wk = 0;
			bool wait = false;
			for(const auto& f : fighters)
				if(taintedQueue(UnitKeyOf(f.first, f.second), wk))
				{
					wait = true;
					break;
				}
			const int64_t ak = UnitKeyOf(c.rec.attackerOwner, c.rec.attackerUnit);
			if(!wait && c.rec.attackerUnit >= 0 && walking(ak))
			{
				wk = ak;
				wait = true;
			}
			if(wait)
			{
				Op h = o;
				h.fightWait = true;
				h.combat = -2;   // the record waits by seq (m_combats is cleared every close)
				m_afterWalkCombats[o.seq] = c;
				m_afterWalkOps[wk].push_back(h);
				m_c.fightWalkHeld++;
				for(const auto& f : fighters)
				{
					const int64_t fk = UnitKeyOf(f.first, f.second);
					m_fightTaint[fk].insert(wk);   // W125: added to the walks it already waits behind, not in their place
					// a unit that does not walk keeps the frame before's view while the combat waits (stock: its records wait
					// behind the combat, so the EXE still draws it as it was)
					if(!walking(fk) && m_preFrozen.insert(f).second)
						m_freezeNow.push_back(f);
				}
				continue;
			}
		}
		if(fw && !m_fightTaint.empty())
		{
			int64_t tk = 0;
			if(taintedQueue(key, tk))
			{
				// the walking attacker's own walk ends pass (they end the walk, which gives the queue back)
				const bool ends = m_fightTaint[key].count(key) && m_afterWalkOps.count(key) &&
				                  (o.kind == kOpArrive || o.kind == SrvFeed::kUnitTeleported || o.kind == SrvFeed::kUnitRebased ||
				                   o.kind == SrvFeed::kUnitDestroyed || o.kind == SrvFeed::kUnitCreated);
				if(!ends)
				{
					Op h = o;
					h.fightWait = true;
					if(h.combat >= 0 && (size_t)h.combat < m_combats.size())
					{
						m_afterWalkCombats[h.seq] = m_combats[(size_t)h.combat];
						h.combat = -2;
					}
					m_afterWalkOps[tk].push_back(h);
					m_c.fightWalkBehind++;
					continue;
				}
			}
		}
		if(m_hold.named(o.owner, o.id))
		{
			if(fw && o.kind == SrvFeed::kUnitMoved)
				namedMoves.insert(key);   // ticket 5066 (a): the fights' hold keeps it; its walk opens at the fight's end
			keep.push_back(o);
			continue;
		}
		switch(o.kind)
		{
		case SrvFeed::kUnitMoved:
			if(m_held.count(key))
			{
				open.insert(key);
				movedNow.insert(key);
			}
			break;
		case kOpArrive:
			if(!movedNow.count(key))
				open.erase(key);
			break;
		case SrvFeed::kUnitTeleported:
		case SrvFeed::kUnitRebased:
		case SrvFeed::kUnitDestroyed:
		case SrvFeed::kUnitCreated:
			open.erase(key);
			movedNow.erase(key);
			break;
		default:
			// (a combat record is ticket 5066's, above: its op names a city for kind 28, and it needs its record)
			if(m_afterWalkKinds.count(o.kind) && open.count(key) && o.kind != SrvFeed::kUnitCombat && o.kind != SrvFeed::kCityCombat)
			{
				m_afterWalkOps[key].push_back(o);
				m_c.afterWalkHeld++;
				continue;
			}
			break;
		}
		keep.push_back(o);
	}
	m_ops.swap(keep);
}

size_t AnimFeed::afterWalkWaiting() const
{
	size_t n = 0;
	for(const auto& kv : m_afterWalkOps)
		n += kv.second.size();
	return n;
}

// ---- issue 4783 (W33 #8801): late combat results -------------------------------------------------------------------------
void AnimFeed::lateResults(const std::vector<SrvFeed::LateCombatResult>& v)
{
	if(!m_started)
		return;
	m_late.insert(m_late.end(), v.begin(), v.end());
	m_c.lateResults += v.size();
}

// Before the clock's frame: a record taken since the last close (not yet added) is completed in place, as SetCombatResult
// would have done had the batch still been pending; otherwise the clock's fight of that seq takes the id.
void AnimFeed::applyLate()
{
	for(const SrvFeed::LateCombatResult& l : m_late)
	{
		bool inRecord = false;
		for(CombatIn& c : m_combats)
			if(c.rec.seq == l.seq)
			{
				c.rec.returned = l.returned;
				c.rec.flags = (uint16_t)(c.rec.flags | SrvFeed::kCombatReturnedKnown);
				inRecord = true;
				break;
			}
		// ticket 5066 (a): a combat waiting behind a walk takes its late result into its kept record
		auto wc = inRecord ? m_afterWalkCombats.end() : m_afterWalkCombats.find(l.seq);
		if(wc != m_afterWalkCombats.end())
		{
			wc->second.rec.returned = l.returned;
			wc->second.rec.flags = (uint16_t)(wc->second.rec.flags | SrvFeed::kCombatReturnedKnown);
			inRecord = true;
		}
		if(inRecord)
		{
			m_c.lateToRecord++;
			continue;
		}
		if(m_clock && m_clock->setOwner(l.seq, l.returned) == FightClock::OwnerSet::Applied)
			m_c.lateToFight++;
		else
			m_c.lateNoFight++;
	}
	m_late.clear();
}

// ---- issue 4774: the fights and the per-unit hold (header; unit-combat-server.md 6) -------------------------------------
void AnimFeed::setRunner(AnimRunner* r)
{
	m_runner = r;
	setFightPlayer(r ? &r->fightPlayer() : nullptr);
	if(r)
		r->attachFeed(this);
}

void AnimFeed::setFightPlayer(AnimFightPlayer* p)
{
	m_player = p;
	m_clock.reset(p ? new FightClock(*p) : nullptr);
	m_hold.clear();
	m_view.clear();
	m_returning.clear();   // issue 4724
	m_preFrozen.clear();   // ticket 5066 (a)
	m_freezeNow.clear();
	m_fightHeldMoves.clear();
}

HeldOp AnimFeed::heldOf(const Op& o)
{
	HeldOp h;
	h.kind = o.kind;
	h.owner = o.owner;
	h.id = o.id;
	h.a2 = o.a2;
	h.a3 = o.a3;
	h.pathLen = o.pathLen;
	h.dest = o.dest;
	h.seq = o.seq;
	return h;
}

AnimFeed::Op AnimFeed::opOf(const HeldOp& h)
{
	Op o;
	o.kind = h.kind;
	o.owner = h.owner;
	o.id = h.id;
	o.a2 = h.a2;
	o.a3 = h.a3;
	o.pathLen = h.pathLen;
	o.dest = h.dest;
	o.seq = h.seq;
	o.released = true;
	return o;
}

// The fights' ends of this frame: each gives its units' held ops back (in order), and a unit no fight names any more is
// drawn from the table again.
void AnimFeed::endFights(const std::vector<OwnerEnd>& ends, const UnitsCore& core, int64_t frameId, uint64_t frameSeq,
                         std::vector<Op>& released)
{
	for(const OwnerEnd& e : ends)
	{
		if(e.figure >= 0)
		{
			m_lastClipEnds.push_back(e);   // issue 4722: a clip instance's end goes to the trigger stream, nothing held
			continue;
		}
		std::vector<HeldOp> back;
		if(e.why != FightEnd::Reset)
			m_hold.end(e.seq, back);   // a Reset comes with a load, which cleared the hold first
		// the DLL's order across the fight's units (each unit's own order is already its seq order; an arrive carries the
		// seq of the event taken before it, so it stays behind that one)
		std::stable_sort(back.begin(), back.end(), [](const HeldOp& x, const HeldOp& y) { return x.seq < y.seq; });
		for(const HeldOp& h : back)
		{
			SrvFeed::UnitRecord r;
			const int facing = core.record(h.owner, h.id, r) ? r.facing : -1;
			if(!(m_posExe && h.kind == SrvFeed::kUnitMoved))   // mode exe: the core's walk places the unit, not the path's end
				m_view.apply(h, core.width(), facing);
			if(h.kind == SrvFeed::kUnitMoved)
				m_fightHeldMoves.erase(UnitKeyOf(h.owner, h.id));   // ticket 5066 (a): its walk opens now
			released.push_back(opOf(h));
		}
		FightEndRecord fr;
		fr.end = e;
		fr.exeFrame = frameId;
		fr.frameSeq = frameSeq;
		fr.released = back.size();
		m_lastEnds.push_back(fr);
		if(m_keepEnds)
			m_allEnds.push_back(fr);
		m_c.ownerEnds++;
		if(e.owner == 0 && e.why != FightEnd::Reset)
		{
			// issue 4783: the EXE's event id never reached this fight (no result, or one later than the end): the shell
			// cannot pass this OwnerEnd to its hold, which ends by its watchdog or outer bound instead
			m_c.ownerZeroEnds++;
			m_loud.push_back("fight seq " + std::to_string(e.seq) + " ends (OwnerEnd " + fightEndName(e.why) +
			                 ") with no EXE event id (owner 0: the forwarded combat's result has not come): the shell "
			                 "cannot pass this end to its hold");
		}
		if(e.why == FightEnd::Refused)
			m_loud.push_back("fight seq " + std::to_string(e.seq) + " (event id " + std::to_string(e.owner) +
			                 ") refused by the choreography: " + e.text +
			                 ": it ends at once (OwnerEnd Refused), its held ops come back now, its figures stay idle");
	}
}

void AnimFeed::fightsFrame(const UnitsCore& core, ca::TimelineFrame& out)
{
	m_lastEnds.clear();
	m_lastClipEnds.clear();   // issue 4722
	m_view.beginFrame();      // (m_loud is cleared at the top of close(): issue 4739, the start's refused creates are said)
	// ticket 5066 (a): the units of a combat put behind a walk in this close keep the frame before's view (as a fight's begin)
	for(const auto& f : m_freezeNow)
		if(!m_view.freeze(f.first, f.second))
			m_preFrozen.erase(f);
	m_freezeNow.clear();
	if(!m_clock)
	{
		// no player: the combat records are counted (take) and dropped; nothing is held
		std::vector<Op> keep;
		for(const Op& o : m_ops)
			if(o.combat < 0)
				keep.push_back(o);
		m_ops.swap(keep);
		m_combats.clear();
		m_late.clear();   // issue 4783: nothing is held, nothing to complete
		return;
	}
	applyLate();   // issue 4783: before the frame, so a fight starting in it starts with its event id
	// (1) the clock's frame: fights added in earlier frames start (the controller has applied their units' creates), the
	// playing ones advance; their events go into this frame before anything the hold gives back
	std::set<std::pair<int32_t, int32_t>> named;
	m_hold.namedUnits(named);
	std::vector<FigureInstance> figs;
	m_player->figures(named, figs);
	std::vector<OwnerEnd> ends;
	std::string why;
	if(!m_clock->frame(out.seq, out.dt, &figs, ends, why))
	{
		m_c.clockRefusals++;
		m_loud.push_back("the fight clock refused frame " + std::to_string(out.seq) + ": " + why);
	}
	const size_t before = out.events.size();
	m_player->takeEvents(out.events);
	m_player->takeLoud(m_loud);
	m_c.fightEvents += out.events.size() - before;
	m_c.events += out.events.size() - before;
	std::vector<Op> work;
	endFights(ends, core, m_frameId, out.seq, work);
	// issue 4724: a unit whose fight ended here and whose figures walk back keeps its view (the ops given back are in it)
	if(m_runner)
		for(const auto& k : named)
			if(!m_hold.named(k.first, k.second) && m_runner->returning(k.first, k.second))
				m_returning.insert(k);
	// ticket 5066 (b): in mode exe a kind 12 the hold gave back here (the winner's advance, a withdrawal) is walked by the EXE
	// from now on, as one that passes while the unit returns (below), but its figures keep their walk back: only the kept
	// view goes, so the placement follows the core's root (the plot left, then the EXE's positions) and the figures walk
	// from where they stand into those slots instead of gliding back to the fight's plot and snapping at the return's end
	if(m_fightAfterWalk && m_posExe)
		for(const Op& w : work)
			if(w.released && w.kind == SrvFeed::kUnitMoved && !m_hold.named(w.owner, w.id) &&
			   m_returning.erase(std::make_pair(w.owner, w.id)))
				m_c.returnsFreed++;
	// (2) this frame's ops in seq order through the hold: a combat adds its fight (its units are held from it on), a waiting
	// op of a held unit waits, an overflow gives the unit's queue back first; a load ends every fight and drops the held ops
	for(const Op& o : m_ops)
	{
		if(o.combat >= 0)
		{
			if((size_t)o.combat >= m_combats.size())
				continue;
			const CombatIn& c = m_combats[(size_t)o.combat];
			const uint64_t fightsBefore = m_clock->counters().fights;
			std::string aw;
			if(!m_clock->add(c.rec, c.members.empty() ? nullptr : c.members.data(), (uint32_t)c.members.size(), aw, c.cityPlot,
			                 c.resumed))   // ticket 0742
			{
				m_c.combatsRefused++;
				m_loud.push_back("the fight clock refused a combat record: " + aw);
				continue;
			}
			if(m_clock->counters().fights == fightsBefore)
				continue;   // Quick Combat or not visualised: no fight, nothing held (0321 4.1, 7)
			// issue 4726: the attacker, the defender, the interceptor and the damage members that are units (their damage
			// and deaths are the fight's area hits: in the EXE they wait as children of the combat, 0321 3.2's parent rule)
			std::vector<std::pair<int32_t, int32_t>> fighters;
			fighters.push_back(std::make_pair(c.rec.attackerOwner, c.rec.attackerUnit));
			fighters.push_back(std::make_pair(c.rec.defenderOwner, c.rec.defenderUnit));
			for(const SrvFeed::CombatMember& m : c.members)
				if(!(m.flags & 1u))
					fighters.push_back(std::make_pair(m.player, m.id));
			for(size_t fi = 0; fi < fighters.size(); ++fi)
			{
				const int32_t ow = fighters[fi].first;
				const int32_t id = fighters[fi].second;
				if(id < 0)
					continue;   // a city
				bool dup = false;
				for(size_t fj = 0; fj < fi; ++fj)
					dup = dup || fighters[fj] == fighters[fi];
				if(dup)
					continue;   // the defender may be the interceptor too (CvUnitCombat.cpp)
				const bool wasNamed = m_hold.named(ow, id);
				m_hold.begin(ow, id, c.rec.seq);
				if(!wasNamed)
				{
					m_view.freeze(ow, id);
					// what of this unit passed earlier in this frame (before the combat) is in the view too
					for(const Op& p : work)
						if(p.owner == ow && p.id == id && !p.released && !(m_posExe && p.kind == SrvFeed::kUnitMoved))
						{
							SrvFeed::UnitRecord r;
							m_view.apply(heldOf(p), core.width(), core.record(ow, id, r) ? r.facing : -1);
						}
				}
			}
			continue;
		}
		if(o.kind == SrvFeed::kLoadBegin)
		{
			std::vector<OwnerEnd> lends;
			m_clock->reset(lends);
			m_hold.clear();   // first: nothing comes back, the ops of the old game are dropped (counted droppedLoad)
			m_view.clear();
			m_returning.clear();   // issue 4724 (the runner drops the motions of a Reset)
			m_preFrozen.clear();   // ticket 5066 (a)
			m_fightHeldMoves.clear();
			std::vector<Op> none;
			endFights(lends, core, m_frameId, out.seq, none);
			work.push_back(o);
			continue;
		}
		std::vector<HeldOp> flushed;
		const UnitHold::Result r = m_hold.take(heldOf(o), flushed, why);
		if(r == UnitHold::Held)
		{
			if(o.kind == SrvFeed::kUnitMoved)
				m_fightHeldMoves.insert(UnitKeyOf(o.owner, o.id));   // ticket 5066 (a): a walk to come at the fight's end
			continue;
		}
		if(r == UnitHold::Overflow)
		{
			m_loud.push_back("the per-unit hold: " + why);
			for(const HeldOp& h : flushed)
			{
				if(h.kind == SrvFeed::kUnitMoved)
					m_fightHeldMoves.erase(UnitKeyOf(h.owner, h.id));
				work.push_back(opOf(h));
			}
		}
		// a removal that passes (a broken hold) while a fight names the unit: the clock ends its fights (0321 4.2: a model
		// gone ends the simulation)
		if(o.kind == SrvFeed::kUnitDestroyed && m_clock->inFight(o.owner, o.id))
		{
			std::vector<OwnerEnd> gends;
			m_clock->unitGone(o.owner, o.id, gends);
			m_c.gone += gends.size();
			std::vector<Op> back;
			endFights(gends, core, m_frameId, out.seq, back);
			for(const Op& b : back)
				work.push_back(b);
		}
		// issue 4724: an op that passes for a unit walking back goes into its kept view; a removal, or a move in mode exe
		// (the EXE walks it now), ends the walk back and the view
		const std::pair<int32_t, int32_t> ok(o.owner, o.id);
		if(m_returning.count(ok) && !m_hold.named(o.owner, o.id))
		{
			if(o.kind == SrvFeed::kUnitDestroyed || (m_posExe && o.kind == SrvFeed::kUnitMoved))
			{
				if(m_runner)
					m_runner->stopReturn(o.owner, o.id);
				m_view.release(o.owner, o.id);
				m_returning.erase(ok);
			}
			else
			{
				SrvFeed::UnitRecord ur;
				m_view.apply(heldOf(o), core.width(), core.record(o.owner, o.id, ur) ? ur.facing : -1);
			}
		}
		work.push_back(o);
	}
	m_ops.swap(work);
	m_combats.clear();
	preFrozenFrame();   // ticket 5066 (a)
	// (3) the views of units no fight names any more are dropped (the table is drawn for them again); issue 4724: a unit
	// walking back keeps its view until its figures are in their slots, or a fight names it again (a held view then);
	// ticket 5066 (a): a unit a waiting combat names keeps its view
	std::set<std::pair<int32_t, int32_t>> still;
	m_hold.namedUnits(still);
	for(const auto& k : named)
		if(!still.count(k) && !m_returning.count(k) && !m_preFrozen.count(k))
			m_view.release(k.first, k.second);
	for(auto it = m_returning.begin(); it != m_returning.end();)
	{
		if(still.count(*it))
			it = m_returning.erase(it);
		else if(!(m_runner && m_runner->returning(it->first, it->second)))
		{
			if(!m_preFrozen.count(*it))
				m_view.release(it->first, it->second);
			it = m_returning.erase(it);
		}
		else
			++it;
	}
}

void AnimFeed::endWalk(int64_t key)
{
	// ticket 5065: the ops behind the walk go back in their order (close() gives them after the op that ended the walk)
	auto aw = m_afterWalkOps.find(key);
	if(aw != m_afterWalkOps.end())
	{
		m_c.afterWalkReleased += aw->second.size();
		m_afterWalkBack.insert(m_afterWalkBack.end(), aw->second.begin(), aw->second.end());
		m_afterWalkOps.erase(aw);
		untaint(key);   // ticket 5066 (a): the units of the combats that waited here pass again
	}
	auto it = m_walks.find(key);
	if(it == m_walks.end())
		return;
	if(!it->second.headed)
		m_c.walksNoHeading++;
	m_walks.erase(it);
}

// Issue 4772: per open walk, the step from its last sample to the unit's newest position record (header: the rule).
void AnimFeed::walkHeadings(const UnitsCore& core, ca::TimelineFrame& out)
{
	for(auto& kv : m_walks)
	{
		const int32_t owner = (int32_t)(kv.first >> 32), id = (int32_t)(uint32_t)kv.first;
		Walk& w = kv.second;
		// 4774 + 4772 merge: a unit a fight names is drawn from its frozen view and its walk's arrive waits in the hold, so
		// its walk gives nothing while held (no sample, no turn); the first step after the hold, from the sample before it,
		// gives the turn to the newest heading, after the ops the hold gives back
		if(m_hold.named(owner, id))
			continue;
		float x, y, z;
		if(!core.rootOf(owner, id, x, y, z))
			continue;   // no position yet (the plot it left), or snapped by its arrival
		if(!w.haveLast)
		{
			w.haveLast = true;
			w.lastX = x;
			w.lastY = y;
			continue;
		}
		const float dx = x - w.lastX, dy = y - w.lastY;
		const float len = std::sqrt(dx * dx + dy * dy);
		if(!(len >= kWalkMinStep))
			continue;   // too short to give a heading: the step accumulates from the same sample
		w.lastX = x;
		w.lastY = y;
		if(len > kWalkMaxStep)
		{
			m_c.walkJumps++;
			continue;
		}
		m_c.walkSamples++;
		w.headed = true;
		auto fi = m_face.find(kv.first);
		// issue 4739: a facing that names no direction (7, 6, -1: a create of a unit a Lua InitUnit made) turns at the first step
		const int cur = fi == m_face.end() || fi->second < 0 || fi->second > 5 ? -1 : fi->second;
		const float off = WalkOffsetDeg(cur, dx, dy);
		if(cur >= 0 && std::fabs(off) <= kWalkTurnDeg)
			continue;
		const int d = WalkDirection(dx, dy);
		if(d < 0 || d == cur)
			continue;
		ca::TimelineEvent t;
		t.kind = ca::EventKind::Turn;
		t.player = owner;
		t.id = id;
		t.a = d;
		t.b = (cur >= 0 && off < 0) ? 1 : 0;   // right = clockwise (the heading falls); exactly 180 degrees: left
		out.events.push_back(t);
		m_c.events++;
		m_c.turns++;
		m_c.turnsRight += t.b ? 1 : 0;
		m_face[kv.first] = (int8_t)d;
	}
}

void AnimFeed::push(ca::TimelineFrame& out, ca::EventKind k, int32_t owner, int32_t id)
{
	ca::TimelineEvent ev;
	ev.kind = k;
	ev.player = owner;
	ev.id = id;
	out.events.push_back(ev);
	m_c.events++;
}

bool AnimFeed::createOf(const UnitsCore& core, int32_t owner, int32_t id, ca::TimelineEvent& ev, std::string* why)
{
	SrvFeed::UnitRecord r;
	if(!core.record(owner, id, r))
	{
		if(why)
			*why = "no table record";
		return false;
	}
	cu::UnitInput u;
	const bool formed = core.inputOf(r, u);
	// issue 4739: the facings the scene lists and the module draws (-1..7, 4464: 6, 7 and -1 at the EXE's 1 rad), not only
	// 0..5. A unit a Lua InitUnit made carries 7 (NO_DIRECTION, CvLuaPlayer's setFacingDirection) until its first move, and an
	// embark is posted before the move (CvUnit::move -> embark(pOldPlot)), so kind 21 carried 7 into the record: the create
	// of the embark's model swap was refused while the scene drew the embarked unit ('no pose program ... figure 0')
	if(!formed || u.facing < -1 || u.facing > 7)
	{
		if(why)
		{
			// issue 4739: the first of inputOf's tests that refused (its order), else the facing
			const PlayerArt pa = core.playerArt(r.owner);
			std::string row, ew;
			char b[200];
			if(formed)
			{
				snprintf(b, sizeof(b), "facing %d outside -1..7 (record flags 0x%x, plot %d, seq %u)", (int)u.facing, (unsigned)r.flags,
				         r.plot, r.seq);
				*why = b;
			}
			else if(!pa.have || !pa.colourOk || !pa.artOk)
				*why = "the owner's player art is not known";
			else if(!core.lookups().units.count(r.unitType))
				*why = "unit type " + std::to_string(r.unitType) + " has no Units row";
			else if(r.plot < 0 || !core.width())
				*why = "plot " + std::to_string(r.plot) + " outside the grid";
			else if((r.flags & cu::kFlagEmbarked) && !core.embarkedRowOf(r, row, ew))
				*why = "embarked, no embarked row: " + ew;
			else
				*why = "plot " + std::to_string(r.plot) + " outside the grid";
		}
		return false;
	}
	ev = ca::TimelineEvent();
	ev.kind = ca::EventKind::Create;
	ev.player = owner;
	ev.id = id;
	ev.unit = u;
	return true;
}

void AnimFeed::close(const UnitsCore& core, double tMs, int64_t frameId, ca::TimelineFrame& out, std::vector<cu::PlayerInput>& players)
{
	out = ca::TimelineFrame();
	players.clear();
	m_loud.clear();   // issue 4739: before the start's creates (was in fightsFrame, after them)
	// the clock (decision 6): the step between two drawn frames of the scene's frame clock; the first frame steps 0
	double dt = m_haveFrame ? (tMs - m_lastMs) / 1000.0 : 0.0;
	if(!std::isfinite(dt) || dt < 0)
		dt = 0;   // the frame clock is monotonic (QPC, frames.bin's nowMs); a step back cannot happen and takes no time
	m_haveFrame = true;
	m_lastMs = tMs;
	out.dt = dt;
	out.has_present = true;
	out.present_time = tMs / 1000.0;
	out.has_exe_frame = frameId >= 0;
	out.exe_frame = frameId >= 0 ? (uint64_t)frameId : 0;
	if(m_c.frames && frameId <= m_lastFrameId)
		m_c.redraws++;   // the scene drew this frame id again (its feed changed before the next EXE frame) or a new composite
	m_lastFrameId = frameId;
	m_c.frames++;
	out.seq = m_c.frames;   // strictly increasing whatever the frame ids do (0374's reader refuses a seq that does not)
	std::set<int32_t> owners;   // the players the frame's creates and held units need
	std::set<int64_t> touched;   // issue 4739: the units this frame's ops named (followed against the table at the end)
	auto doCreate = [&](int32_t owner, int32_t id) -> bool {
		ca::TimelineEvent ev;
		std::string why;
		const int64_t key = UnitKeyOf(owner, id);
		touched.insert(key);
		if(!createOf(core, owner, id, ev, &why))
		{
			m_c.createsNotFormed++;
			// issue 4739: the record's seq it was refused at (no record: nothing to try again); LOUD with the reason
			SrvFeed::UnitRecord r;
			if(core.record(owner, id, r))
			{
				m_unformed[key] = r.seq;
				m_loud.push_back("issue 4739: the create of unit (" + std::to_string(owner) + ", " + std::to_string(id) +
				                 ") was not formed: " + why + ": not drawn until its record changes");
			}
			else
				m_unformed.erase(key);
			return false;
		}
		out.events.push_back(ev);
		m_c.events++;
		m_c.creates++;
		m_held.insert(key);
		m_unformed.erase(key);
		m_heldEmbarked[key] = (ev.unit.flags & cu::kFlagEmbarked) != 0;
		owners.insert(owner);
		m_face[key] = ev.unit.facing;   // issue 4772
		endWalk(key);
		return true;
	};
	auto doRemove = [&](int32_t owner, int32_t id) {
		const int64_t key = UnitKeyOf(owner, id);
		touched.insert(key);
		if(m_held.erase(key))
		{
			push(out, ca::EventKind::Remove, owner, id);
			m_c.removes++;
		}
		m_heldEmbarked.erase(key);
		m_face.erase(key);   // issue 4772
		endWalk(key);
	};
	if(m_startPending)
	{
		m_startPending = false;
		for(const SrvFeed::UnitRecord& r : core.table())
			doCreate(r.owner, r.id);
	}
	m_frameId = frameId;
	holdBehindWalks();        // ticket 5065: before any routing (the fights' hold included), as the EXE's queue holds them
	fightsFrame(core, out);   // issue 4774: the fights step; m_ops becomes what passes the hold (released ops first)
	std::vector<Op> carry;          // issue 4755: arrives whose unit's move is in this frame wait for the next one
	std::set<int64_t> movedNow;     // units given a move in this frame
	for(const Op& o : m_ops)
	{
		const int64_t key = UnitKeyOf(o.owner, o.id);
		const bool held = m_held.count(key) != 0;
		if(o.kind != SrvFeed::kLoadBegin)
			touched.insert(key);   // issue 4739
		if(o.kind == kOpArrive)
		{
			if(!held)
			{
				m_c.droppedNotHeld++;
				continue;
			}
			// 4774 + 4772 merge: an arrive the hold gave back is not deferred: the EXE's arrival already happened (it waited
			// for the fight), and a deferred one would go through the hold again next frame and wait for the unit's next fight
			if(movedNow.count(key) && !o.released)
			{
				carry.push_back(o);
				m_c.arrivesDeferred++;
				continue;
			}
			SrvFeed::UnitRecord r;
			const int facing = core.record(o.owner, o.id, r) && r.facing >= 0 && r.facing <= 5 ? r.facing : -1;
			if(facing < 0)
			{
				m_c.droppedNotHeld++;
				continue;
			}
			ca::TimelineEvent ar;
			ar.kind = ca::EventKind::Arrive;
			ar.player = o.owner;
			ar.id = o.id;
			ar.a = facing;
			out.events.push_back(ar);
			m_c.events++;
			m_c.arrives++;
			m_face[key] = (int8_t)facing;   // issue 4772: the end facing rule; the walk ends
			endWalk(key);
			continue;
		}
		if(o.kind == SrvFeed::kLoadBegin)
		{
			std::vector<int64_t> all(m_held.begin(), m_held.end());
			for(int64_t k : all)
				doRemove((int32_t)(k >> 32), (int32_t)(uint32_t)k);
			continue;
		}
		if(o.kind == SrvFeed::kUnitCreated || (o.kind == SrvFeed::kUnitEmbark && !held))
		{
			if(held)
				doRemove(o.owner, o.id);   // a creation of a held key: remove + create (era change, S3)
			doCreate(o.owner, o.id);
			continue;
		}
		const bool mapped = o.kind == SrvFeed::kUnitMoved || o.kind == SrvFeed::kUnitTeleported || o.kind == SrvFeed::kUnitRebased ||
		                    o.kind == SrvFeed::kUnitDestroyed || o.kind == SrvFeed::kUnitDestroyedInCombat ||
		                    o.kind == SrvFeed::kUnitSetDamage || o.kind == SrvFeed::kUnitFortify || o.kind == SrvFeed::kUnitWork ||
		                    o.kind == SrvFeed::kUnitParadrop || o.kind == SrvFeed::kUnitActivate || o.kind == SrvFeed::kUnitEmbark ||
		                    o.kind == SrvFeed::kUnitGarrison || o.kind == SrvFeed::kUnitVisibility ||
		                    o.kind == SrvFeed::kUnitResetAnimationState;
		if(!mapped)
			continue;   // 23, 25, 28, 29, 33: no event (unit-controller.md 3)
		if(!held)
		{
			m_c.droppedNotHeld++;
			continue;
		}
		if(o.kind == SrvFeed::kUnitDestroyed)
		{
			doRemove(o.owner, o.id);
			continue;
		}
		// facing of move / arrive / place: the record's at the frame's end (the events carry none)
		SrvFeed::UnitRecord r;
		const bool haveRec = core.record(o.owner, o.id, r);
		const int facing = haveRec && r.facing >= 0 && r.facing <= 5 ? r.facing : -1;
		ca::TimelineEvent ev;
		ev.player = o.owner;
		ev.id = o.id;
		switch(o.kind)
		{
		case SrvFeed::kUnitMoved:
		case SrvFeed::kUnitTeleported:
		case SrvFeed::kUnitRebased:
		{
			if(facing < 0)
			{
				m_c.droppedNotHeld++;
				continue;
			}
			if(o.kind == SrvFeed::kUnitMoved)
			{
				ev.kind = ca::EventKind::Move;
				ev.a = facing;
				ev.b = o.pathLen > 0 ? o.pathLen : 0;
				out.events.push_back(ev);
				m_c.events++;
				movedNow.insert(key);
				if(m_posExe)
				{
					m_walks[key];   // issue 4772: a walk opens (a kind 12 of a walking unit continues it, its samples kept)
					continue;       // issue 4755: mode exe: the arrive comes with the EXE's arrival (arrive())
				}
				ca::TimelineEvent ar;   // mode own: the destination at once (header) [I]
				ar.kind = ca::EventKind::Arrive;
				ar.player = o.owner;
				ar.id = o.id;
				ar.a = facing;
				out.events.push_back(ar);
				m_c.events++;
				continue;
			}
			const int32_t plot = o.kind == SrvFeed::kUnitTeleported ? o.a2 : o.a3;
			const uint32_t w = core.width();
			if(plot < 0 || !w)
			{
				m_c.droppedNotHeld++;
				continue;
			}
			ev.kind = ca::EventKind::Place;
			ev.a = plot % (int32_t)w;
			ev.b = plot / (int32_t)w;
			ev.c = facing;
			m_face[key] = (int8_t)facing;   // issue 4772: a teleport or a rebase ends the walk
			endWalk(key);
			break;
		}
		case SrvFeed::kUnitDestroyedInCombat:
			ev.kind = ca::EventKind::Destroyed;
			ev.a = 1;
			break;
		case SrvFeed::kUnitSetDamage:
			ev.kind = ca::EventKind::Damage;
			ev.a = o.a2;
			ev.b = o.a3;
			break;
		case SrvFeed::kUnitFortify:
			ev.kind = ca::EventKind::Fortify;
			ev.a = o.a2 ? 1 : 0;
			break;
		case SrvFeed::kUnitEmbark:
			ev.kind = ca::EventKind::Embark;
			ev.a = o.a2 ? 1 : 0;
			m_heldEmbarked[key] = ev.a != 0;   // issue 4739: the controller's embark event flips its input's bit
			break;
		case SrvFeed::kUnitGarrison:
			ev.kind = ca::EventKind::Garrison;
			ev.a = o.a2 ? 1 : 0;
			break;
		case SrvFeed::kUnitVisibility:
		{
			float sec = 0;
			memcpy(&sec, &o.a3, sizeof(sec));
			ev.kind = ca::EventKind::Visibility;
			ev.a = o.a2 & 1;
			ev.blend_time = std::isfinite(sec) && sec > 0 ? sec : 0.0f;
			break;
		}
		case SrvFeed::kUnitResetAnimationState: ev.kind = ca::EventKind::Reset; break;
		case SrvFeed::kUnitParadrop:
			ev.kind = ca::EventKind::Action;
			ev.action = ca::ActionKind::Paradrop;
			break;
		case SrvFeed::kUnitActivate:
			ev.kind = ca::EventKind::Action;
			ev.action = ca::ActionKind::Activate;
			break;
		case SrvFeed::kUnitWork:
		{
			// 0439: the stop (a[2] -1) is `action work -` (the EXE posts IDLE, unit-controller.md 14.1); a build index with
			// no Builds row gets the EXE's tool for an unknown build (the hammer) through a Type no row has
			ev.kind = ca::EventKind::Action;
			ev.action = ca::ActionKind::Work;
			if(o.a2 < 0)
			{
				m_c.workStops++;
				break;
			}
			auto it = m_builds.find(o.a2);
			if(it == m_builds.end())
			{
				m_c.workUnknown++;
				ev.argument = "BUILD_UNKNOWN_INDEX_" + std::to_string(o.a2);
				break;
			}
			ev.argument = it->second;
			break;
		}
		default: continue;
		}
		out.events.push_back(ev);
		m_c.events++;
	}
	m_ops.swap(carry);   // issue 4755: the deferred arrives (none in mode own)
	if(m_resync)
	{
		m_resync = false;
		m_c.resyncs++;
		std::set<int64_t> inTable;
		for(const SrvFeed::UnitRecord& r : core.table())
			inTable.insert(UnitKeyOf(r.owner, r.id));
		std::vector<int64_t> gone;
		for(int64_t k : m_held)
			if(!inTable.count(k))
				gone.push_back(k);
		for(int64_t k : gone)
			doRemove((int32_t)(k >> 32), (int32_t)(uint32_t)k);
		for(const SrvFeed::UnitRecord& r : core.table())
			if(!m_held.count(UnitKeyOf(r.owner, r.id)))
				doCreate(r.owner, r.id);
	}
	// issue 4739 (MC #9402): the controller follows the table for the units this frame touched and for those whose create
	// was not formed: a unit the table lists and the controller does not hold is created again (a create refused at the
	// frame of an embark's model swap left the embarked unit undrawn at sea, 'no pose program ... figure 0'); a held unit
	// whose embarked bit differs from the table's is removed and created again (its art follows the table); a unit whose
	// record is gone is never created (no ghost: its removal stands). A unit a fight names or that walks back after its fight
	// is the hold's (its view is drawn, its ops wait): not followed here. A create refused at a record waits until the
	// record changes (its seq), so a refusal is not tried again every frame.
	for(const auto& kv : m_unformed)
		touched.insert(kv.first);
	for(int64_t key : touched)
	{
		const int32_t owner = (int32_t)(key >> 32), id = (int32_t)(uint32_t)key;
		if(m_hold.named(owner, id) || m_returning.count(std::make_pair(owner, id)))
			continue;
		SrvFeed::UnitRecord r;
		if(!core.record(owner, id, r))
		{
			m_unformed.erase(key);
			continue;
		}
		if(!m_held.count(key))
		{
			auto uf = m_unformed.find(key);
			if(uf != m_unformed.end() && uf->second == r.seq)
			{
				m_c.retriesWaiting++;
				continue;
			}
			if(doCreate(owner, id))
				m_c.recreated++;
			continue;
		}
		auto he = m_heldEmbarked.find(key);
		const bool tableEmbarked = (r.flags & cu::kFlagEmbarked) != 0;
		if(he != m_heldEmbarked.end() && he->second != tableEmbarked)
		{
			doRemove(owner, id);
			if(doCreate(owner, id))
				m_c.rereads++;
		}
	}
	if(m_posExe && !m_walks.empty())
		walkHeadings(core, out);   // issue 4772: after the frame's moves, arrives and places
	// ticket 5065: a unit whose ops wait while no walk of it is open (its kind 12 opened none: dropped, or no facing) and no
	// fight names it gets them back too; then every op given back this close goes first at the next close, in feed order,
	// before the arrives carried there (holdBehindWalks sees them again: a walk opened since holds them once more)
	for(auto it = m_afterWalkOps.begin(); it != m_afterWalkOps.end();)
	{
		const int32_t owner = (int32_t)(it->first >> 32), id = (int32_t)(uint32_t)it->first;
		if(m_walks.count(it->first) || m_hold.named(owner, id))
		{
			++it;
			continue;
		}
		m_c.afterWalkReleased += it->second.size();
		m_c.afterWalkNoWalk += it->second.size();
		m_afterWalkBack.insert(m_afterWalkBack.end(), it->second.begin(), it->second.end());
		untaint(it->first);   // ticket 5066 (a)
		it = m_afterWalkOps.erase(it);
	}
	if(!m_afterWalkBack.empty())
		giveBackAfterWalk(m_afterWalkBack);   // ticket 5066 (a): with the waiting combats' records
	// the players the frame's held units need, given when first seen or changed (an era change: the controller's art)
	for(int64_t k : m_held)
		owners.insert((int32_t)(k >> 32));
	for(int32_t p : owners)
	{
		const PlayerArt pa = core.playerArt(p);
		if(!pa.have || !pa.colourOk || !pa.artOk)
			continue;
		const std::string key = pa.in.colour + "|" + pa.in.art_style + "|" + pa.in.era;
		auto it = m_playerKey.find(p);
		if(it != m_playerKey.end() && it->second == key)
			continue;
		if(it != m_playerKey.end())
			m_c.playerChanges++;
		m_playerKey[p] = key;
		players.push_back(pa.in);
	}
}

// ---- AnimRunner -----------------------------------------------------------------------------------------------------
AnimRunner::AnimRunner() {}
AnimRunner::~AnimRunner() { m_fightPlayer.reset(); }   // issue 4774: its runs refer to the controller

bool AnimRunner::open(const c5r::gamefs::GameFiles& files, c5m::TableSource& db, cu::ArtResolver& art, std::string& why)
{
	m_open = false;
	m_provider.reset(new cu::GameFilesProvider(files));
	m_strings.reset(new c5m::StringDatabase());
	const cu::UnitStatus ss = cu::loadStringDatabases(files.names(), *m_provider, *m_strings);
	if(!ss.ok())
	{
		why = "the unit string databases: " + ss.str();
		return false;
	}
	const ca::AnimStatus bs = ca::loadBridgeTables(db, m_tables);
	if(!bs.ok())
	{
		why = "the controller's bridge tables: " + bs.text;
		return false;
	}
	m_builds.clear();
	{
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		if(!db.rows("Builds", {"ID", "Type"}, rows, &e))
		{
			why = "table Builds (ID, Type): " + e.what;
			return false;
		}
		for(const c5m::DbRow& r : rows)
			if(r.size() == 2 && !r[0].null && !r[1].null)
				m_builds[(int32_t)strtol(r[0].text.c_str(), nullptr, 10)] = r[1].text;
	}
	// issue 4774: the fights' tables; when they cannot be read every fight is refused with the text (never a default)
	m_combatTables.clear();
	m_fightDurs.clear();
	m_combatOk = ca::loadCombatTables(db, m_combatTables, m_combatWhy, &m_combatInfo);   // 4783: and the strike rule
	if(m_combatOk)
		m_combatWhy.clear();
	else
		m_combatWhy = "the combat tables: " + m_combatWhy;
	// 0685: the walk's turn over time (unit-server-scene.md 19.6); C5_RENDER_UNITS_WALK_TURN=0: the facing snaps (4772)
	m_walkRates.clear();
	{
		char v[8] = {};
		const DWORD n = GetEnvironmentVariableA("C5_RENDER_UNITS_WALK_TURN", v, (DWORD)sizeof(v));
		m_walkFacings.setTurnOn(!(n > 0 && n < sizeof(v) && v[0] == '0'));
	}
	{   // 0783: the walk's yaw along its curved root path; C5_RENDER_UNITS_WALK_CURVE=0: 0685's frames
		char v[8] = {};
		const DWORD n = GetEnvironmentVariableA("C5_RENDER_UNITS_WALK_CURVE", v, (DWORD)sizeof(v));
		m_walkFacings.setCurveOn(!(n > 0 && n < sizeof(v) && v[0] == '0'));
	}
	{   // 0685 (diagnostic): C5_RENDER_UNITS_WALK_LOG=1 logs the walks' facing events and turns (takeWalkLines)
		char v[8] = {};
		const DWORD n = GetEnvironmentVariableA("C5_RENDER_UNITS_WALK_LOG", v, (DWORD)sizeof(v));
		m_walkLog = n > 0 && n < sizeof(v) && v[0] == '1';
		m_walkLines.clear();
	}
	// ticket 5062: each era's city projectile (Eras.CityBombardEffectTag by ID, as CvCity::GetCityBombardEffectTag reads
	// it); a database without the table or the column leaves the list empty and a city's strike unnamed, as before 5062
	m_cityTags.clear();
	{
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		if(db.rows("Eras", {"ID", "CityBombardEffectTag"}, rows, &e))
			for(const c5m::DbRow& r : rows)
			{
				if(r.size() != 2 || r[0].null)
					continue;
				const long id = strtol(r[0].text.c_str(), nullptr, 10);
				if(id < 0 || id > 255)
					continue;
				if((size_t)id >= m_cityTags.size())
					m_cityTags.resize((size_t)id + 1);
				m_cityTags[(size_t)id] = r[1].null ? std::string() : r[1].text;
			}
	}
	m_module.reset(new cu::ModulePose(*m_provider, m_strings.get()));
	m_pose.reset(new ca::ControllerPose(*m_module));
	m_art.reset(new ca::ResolverArtSource(art));
	m_graphs.reset(new ca::ClipGraphFactory(cu::clipLoaderFromProvider(*m_provider, m_strings.get())));
	m_graphs->options.hold_from_continue = true;   // the driver's default (MC #2117)
	m_bridge.reset(new ca::ActionBridge(m_tables));
	m_bridge->remove_fallen = true;                // MC #2147 (premise change 5)
	ca::ControllerOptions co;                      // clock continuous (MC #2089), no cross-fade rules, no skips
	co.seed = 0;
	co.forced = false;
	co.bridge = m_bridge.get();
	co.revive_dead = m_bridge->remove_fallen;      // 4736: a heal replaces a fight's dead figures below the living count
	co.strings = m_strings.get();
	m_ctl.open(*m_provider, *m_art, *m_graphs, co);
	m_open = true;
	return true;
}

cu::PoseSource* AnimRunner::pose() { return m_pose.get(); }

const std::string& AnimRunner::cityBombardTag(int32_t era) const
{
	static const std::string none;
	return era >= 0 && (size_t)era < m_cityTags.size() ? m_cityTags[(size_t)era] : none;
}

bool AnimRunner::apply(const ca::TimelineFrame& f, const std::vector<cu::PlayerInput>& players, UnitsCore& core, std::string& why)
{
	return apply(f, players, core, nullptr, why);
}

bool AnimRunner::apply(const ca::TimelineFrame& f, const std::vector<cu::PlayerInput>& players, UnitsCore& core,
                       const std::set<ca::Controller::UnitKey>* drawn, std::string& why)
{
	if(!m_open)
	{
		why = "the controller is not open";
		return false;
	}
	if(!players.empty())
		m_ctl.addPlayers(players);
	m_walkFacings.follow(f);   // issue 4772: the walk facings fillFigures draws
	// 0783: each walking unit's root (the one buildList draws this frame) gives its walk's tangent
	m_walkFacings.track([&core](int64_t key, float& x, float& y) -> bool {
		float z = 0;
		return core.rootOf((int32_t)(key >> 32), (int32_t)(uint32_t)key, x, y, z);
	});
	// 0685: the drawn yaws turn by this frame's step, at each unit's first member's TurnFacingRate (read once per unit)
	m_walkFacings.advance(f.dt, [this](int64_t key) -> float {
		auto it = m_walkRates.find(key);
		if(it != m_walkRates.end())
			return it->second;
		float rate = kWalkTurnRateDefault;
		ca::CombatUnitFigures cf;
		const int32_t player = (int32_t)(key >> 32), id = (int32_t)(uint32_t)key;
		if(m_combatOk && m_ctl.unitInput(player, id) && m_ctl.combatFigures(player, id, m_combatTables, cf).ok() &&
		   !cf.members.empty())
			rate = WalkTurnRate(cf.members[0].motion.turn_facing_rate_min, cf.members[0].motion.turn_facing_rate_max);
		m_walkRates[key] = rate;
		return rate;
	});
	if(m_walkLog)   // 0685 (diagnostic): the walks' facing events of this frame, then every unit still turning
	{
		for(const ca::TimelineEvent& e : f.events)
		{
			if(e.kind != ca::EventKind::Move && e.kind != ca::EventKind::Turn && e.kind != ca::EventKind::Arrive &&
			   e.kind != ca::EventKind::Place && e.kind != ca::EventKind::Remove)
				continue;
			char line[320];
			snprintf(line, sizeof(line), "units: walk (0685): %s unit %d:%d a %d b %d c %d dt %.4f | %s", ca::eventKindName(e.kind),
			         (int)e.player, (int)e.id, (int)e.a, (int)e.b, (int)e.c, f.dt,
			         m_walkFacings.describe(UnitKeyOf(e.player, e.id)).c_str());
			m_walkLines.push_back(line);
		}
		for(const auto& kv : m_walkFacings.turnYaws())
		{
			char line[320];
			snprintf(line, sizeof(line), "units: walk (0685): turning unit %d:%d dt %.4f | %s", (int)(kv.first >> 32),
			         (int)(uint32_t)kv.first, f.dt, m_walkFacings.describe(kv.first).c_str());
			m_walkLines.push_back(line);
		}
	}
	// issue 4774: every unit a fight names is stepped, drawn or not (its fight waits on its clips), and carries the scene's
	// 'held in a fight' field; the feed's LOUD texts of this frame go to the log
	std::set<ca::Controller::UnitKey> named, withFighters;
	if(m_feed)
	{
		m_feed->hold().namedUnits(named);
		// issue 4729: a unit walking back after its fight (4724) keeps its view, and HoldView::applyTo relists it after
		// the build when the build did not list it (hidden by a Visibility the DLL sent during the fight, culled): it is
		// drawn, so it is stepped and given programs (r1's bomber: UnitRefused "no pose program" without this)
		const std::set<std::pair<int32_t, int32_t>>& back = m_feed->returningUnits();
		// ticket 5066 (a): a unit a waiting combat names keeps its view, relisted the same way (a defender killed in the
		// move-and-attack's drain stays drawn until its fight)
		const std::set<std::pair<int32_t, int32_t>>& kept = m_feed->waitingFightUnits();
		if(drawn && (!named.empty() || !back.empty() || !kept.empty()))
		{
			withFighters = *drawn;
			withFighters.insert(named.begin(), named.end());
			withFighters.insert(back.begin(), back.end());
			withFighters.insert(kept.begin(), kept.end());
			drawn = &withFighters;
		}
		std::set<int64_t> flags;
		for(const auto& k : named)
			flags.insert(UnitKeyOf(k.first, k.second));
		for(int64_t k : m_fightFlags)
			if(!flags.count(k))
				core.setHeldInFight((int32_t)(k >> 32), (int32_t)(uint32_t)k, false);
		for(int64_t k : flags)
			if(!m_fightFlags.count(k))
				core.setHeldInFight((int32_t)(k >> 32), (int32_t)(uint32_t)k, true);
		m_fightFlags.swap(flags);
		for(const std::string& l : m_feed->lastLoud())
			core.note(Status::kAnimFailed, "", "issue 4774: %s", l.c_str());   // the first 20, then every 100th
	}
	// issue 4724: the walks back that are over (as returning() told the feed in this frame's close) end; the others step
	// by the frame's dt; every motion goes to the placement for this frame's prepare
	for(const std::string& l : m_motionLoud)
		core.note(Status::kAnimFailed, "", "issue 4724: %s", l.c_str());
	m_motionLoud.clear();
	for(auto it = m_motions.begin(); it != m_motions.end();)
	{
		Motion& m = it->second;
		if(m.ended && !returningKey(it->first))
		{
			if(m.settled)
				m_mc.settled++;
			else if(m.placedSeq != m_seq)
				m_mc.notDrawn++;
			else
				m_mc.returnLimit++;
			if(m.walked)
				m_mc.residualWalks++;   // ticket 4742
			if(m_fightPlace)
				m_fightPlace->clear((int32_t)(it->first >> 32), (int32_t)(uint32_t)it->first);
			it = m_motions.erase(it);
			continue;
		}
		if(m.ended && m.tSeq != f.seq)
		{
			m.t += f.dt;
			m.tSeq = f.seq;
		}
		if(m_fightPlace)
			m_fightPlace->set((int32_t)(it->first >> 32), (int32_t)(uint32_t)it->first, m.motion, m.t);
		++it;
	}
	m_seq = f.seq;
	m_width = core.width();
	for(auto it = m_placeIn.begin(); it != m_placeIn.end();)
		it = m_fightFlags.count(it->first) ? std::next(it) : m_placeIn.erase(it);   // only fighters' inputs are kept
	if(m_quick.on())   // ticket 0796: the fighters' clip clocks at their fight's rate of this frame (the clock stepped it)
	{
		m_ctl.clearRates();
		if(m_feed && m_feed->fights())
			for(int64_t k : m_fightFlags)
			{
				const int32_t p = (int32_t)(k >> 32), id = (int32_t)(uint32_t)k;
				m_ctl.setRate(ca::Controller::UnitKey(p, id), m_feed->fights()->unitRate(p, id));
			}
	}
	const ca::AnimStatus st = m_ctl.apply(f, drawn);
	if(!st.ok())
	{
		why = std::string(ca::name(st.code)) + ": " + st.text;
		return false;
	}
	if(m_tap && m_feed)   // issue 4722: frame F's trigger records (the fighters' crossed triggers, the fights' ends)
	{
		std::string tw;
		if(!m_tap->frame(m_feed->lastFrameId(), f.dt, m_ctl, m_feed->lastFightEnds(), m_feed->lastClipEnds(), tw))
			core.note(Status::kAnimFailed, "", "issue 4722: the trigger stream refused frame %lld: %s", (long long)m_feed->lastFrameId(), tw.c_str());
	}
	m_pose->clearPrograms();
	m_blends = 0;
	for(const ca::FigureProgram& p : m_ctl.programs())
	{
		m_blends += p.program.single() ? 0 : 1;
		m_pose->setProgram(p.player, p.id, p.figure, p.program);
	}
	std::set<int64_t> failedNow;
	for(const ca::UnitAnimStatus& s : m_ctl.statuses())
	{
		if(s.status.ok())
			continue;
		const int64_t key = UnitKeyOf(s.player, s.id);
		failedNow.insert(key);
		if(m_failed.count(key))
			continue;
		const cu::UnitInput* in = m_ctl.unitInput(s.player, s.id);
		const std::string type = in ? in->unit_type : std::string("?");
		core.note(Status::kAnimFailed, type + "|" + ca::name(s.status.code), "unit (%d, %d) %s: the controller: %s: %s: not drawn (logged once per type and code)",
		          s.player, s.id, type.c_str(), ca::name(s.status.code), s.status.text.c_str());
	}
	m_failed.swap(failedNow);
	return true;
}

void AnimRunner::figuresOf(int32_t player, int32_t id, std::vector<cu::FigureInput>& out) const
{
	out.clear();
	for(const ca::FigureProgram& p : m_ctl.programs())
		if(p.player == player && p.id == id && p.program.single())
		{
			cu::FigureInput f;
			f.figure = (uint16_t)p.figure;
			f.clip = p.program.nodes[0].clip;
			f.t = p.program.nodes[0].t;
			out.push_back(f);
		}
}

// Issue 4772: AnimFeed's facing rule followed from the frame's events (the same events in the same order, so the facing
// drawn here is the one AnimFeed measured its turns from).
void WalkFacings::follow(const ca::TimelineFrame& f)
{
	for(const ca::TimelineEvent& e : f.events)
	{
		const int64_t key = UnitKeyOf(e.player, e.id);
		switch(e.kind)
		{
		case ca::EventKind::Create:
		{
			Heading h;
			h.facing = e.unit.facing;
			if(h.facing >= 0 && h.facing <= 5)   // 0685: a create sets the drawn yaw at once
			{
				h.hasYaw = true;
				h.yaw = cu::facingYaw(h.facing);
			}
			m_h[key] = h;
			break;
		}
		case ca::EventKind::Remove: m_h.erase(key); break;
		case ca::EventKind::Move:
		{
			auto it = m_h.find(key);
			if(it != m_h.end())
			{
				if(!it->second.walking)   // 0783: a new walk samples its root from scratch (a kind 12 continuing it keeps it)
				{
					it->second.hasSample = false;
					it->second.hasTan = false;
				}
				it->second.walking = true;
			}
			break;
		}
		case ca::EventKind::Turn:
		{
			auto it = m_h.find(key);
			if(it != m_h.end())
			{
				it->second.facing = (int8_t)e.a;
				it->second.side = e.b ? -1 : 1;   // 0685: right = clockwise, the yaw falls
			}
			break;
		}
		case ca::EventKind::Arrive:
		case ca::EventKind::Place:
		{
			auto it = m_h.find(key);
			if(it != m_h.end())
			{
				Heading& h = it->second;
				h.facing = (int8_t)(e.kind == ca::EventKind::Arrive ? e.a : e.c);
				h.walking = false;
				h.hasSample = false;   // 0783: the walk's tangent ends with it
				h.hasTan = false;
				h.side = 0;   // 0685: an arrival turns the short way
				if(e.kind == ca::EventKind::Place && h.facing >= 0 && h.facing <= 5)
				{
					h.hasYaw = true;   // 0685: a teleport sets the drawn yaw at once
					h.yaw = cu::facingYaw(h.facing);
					h.turning = false;
				}
			}
			break;
		}
		default: break;
		}
	}
}

bool WalkFacings::facing(int32_t player, int32_t id, int& facing) const
{
	auto it = m_h.find(UnitKeyOf(player, id));
	if(it == m_h.end() || !it->second.walking || it->second.facing < 0 || it->second.facing > 5)
		return false;
	facing = it->second.facing;
	return true;
}

size_t WalkFacings::walking() const
{
	size_t n = 0;
	for(const auto& kv : m_h)
		n += kv.second.walking ? 1 : 0;
	return n;
}

void WalkFacings::apply(cu::UnitScene& sc) const
{
	if(m_h.empty())
		return;
	for(cu::UnitInput& u : sc.units)
	{
		int fc;
		if(facing(u.player, u.id, fc))
			u.facing = (int8_t)fc;
	}
}

// ---- 0685: the walk's turn over time (header) ----------------------------------------------------------------------------
float WalkTurnRate(float facingRateMin, float facingRateMax)
{
	const float r = (facingRateMin + 0.5f * ((std::max)(facingRateMax, facingRateMin) - facingRateMin)) / 6.28318530717958647692f;
	return std::isfinite(r) && r > 0.0f ? r : kWalkTurnRateDefault;
}

void WalkFacings::setTurnOn(bool on)
{
	m_turnOn = on;
	if(!on)
	{
		m_turnYaws.clear();
		for(auto& kv : m_h)
			kv.second.turning = false;
	}
}

bool WalkFacings::target(int64_t key, int& facing) const
{
	auto it = m_h.find(key);
	if(it == m_h.end() || it->second.facing < 0 || it->second.facing > 5)
		return false;
	facing = it->second.facing;
	return true;
}

void WalkFacings::advance(double dt, const std::function<float(int64_t)>& rateOf)
{
	m_turnYaws.clear();
	if(!m_turnOn)
		return;
	const double pi = 3.14159265358979323846;
	const double step = std::isfinite(dt) ? (std::min)((std::max)(dt, 0.0), kWalkTurnMaxDt) : 0.0;
	for(auto& kv : m_h)
	{
		Heading& h = kv.second;
		if(h.facing < 0 || h.facing > 5)
			continue;
		const float facingGoal = cu::facingYaw(h.facing);
		// 0783: a walking unit with a tangent turns towards it (stock's arc), any other towards its facing (0685)
		const bool onTan = m_curveOn && h.walking && h.hasTan;
		const float goal = onTan ? h.tanYaw : facingGoal;
		if(!h.hasYaw)
		{
			h.hasYaw = true;
			h.yaw = goal;
		}
		double diff = std::remainder((double)goal - (double)h.yaw, 2.0 * pi);   // [-pi, pi]
		bool reached = std::fabs(diff) <= 1e-5;
		if(!reached)
		{
			if(std::fabs(diff) >= pi - 1e-4 && h.side != 0)   // exactly opposite: the turn event's side
				diff = h.side > 0 ? pi : -pi;
			float rate = rateOf ? rateOf(kv.first) : kWalkTurnRateDefault;
			if(!std::isfinite(rate) || rate <= 0.0f)
				rate = kWalkTurnRateDefault;
			const double move = 2.0 * pi * (double)rate * step;
			// a turn is counted from its first frame (0685); the tangent's small steps along a curve only when the rate
			// limits them (0783)
			if(!h.turning && ((!onTan && std::fabs(diff) > 0.01) || move < std::fabs(diff)))
			{
				h.turning = true;
				h.turnMs = 0;
				m_tc.turns++;
			}
			if(move >= std::fabs(diff))
				reached = true;   // within this frame's turn: on the goal (on a curve every frame, 0783)
			else
			{
				h.turnMs += step * 1000.0;
				h.yaw = (float)std::remainder((double)h.yaw + (diff > 0 ? move : -move), 2.0 * pi);
				m_tc.turnFrames++;
			}
		}
		if(reached)
		{
			h.yaw = goal;
			if(h.turning)
			{
				h.turning = false;
				h.turnMs += step * 1000.0;
				m_tc.turnMsMax = (std::max)(m_tc.turnMsMax, h.turnMs);
			}
		}
		if(onTan && reached)
			m_tc.tanFrames++;
		if(std::fabs(std::remainder((double)h.yaw - (double)facingGoal, 2.0 * pi)) > 1e-5)
			m_turnYaws[kv.first] = h.yaw;   // drawn at a yaw other than its facing's this frame
	}
}

void WalkFacings::track(const std::function<bool(int64_t, float&, float&)>& rootOf)
{
	if(!rootOf)
		return;
	const float halfPi = 1.57079632679489661923f;
	for(auto& kv : m_h)
	{
		Heading& h = kv.second;
		if(!h.walking)
			continue;
		float x = 0, y = 0;
		if(!rootOf(kv.first, x, y) || !std::isfinite(x) || !std::isfinite(y))
			continue;
		if(!h.hasSample)
		{
			h.hasSample = true;
			h.sx = x;
			h.sy = y;
			continue;
		}
		const float dx = x - h.sx, dy = y - h.sy;
		const float len = std::sqrt(dx * dx + dy * dy);
		if(len < kWalkMinStep)
			continue;   // shorter steps accumulate (4772's rule)
		h.sx = x;
		h.sy = y;
		if(len > kWalkMaxStep)
			continue;   // a jump (the wrap seam, a dropped stretch): the sample starts again, the tangent is kept
		h.tanYaw = (float)std::remainder((double)std::atan2(dy, dx) + (double)halfPi, 2.0 * 3.14159265358979323846);
		h.hasTan = true;
		m_tc.tanSamples++;
	}
}

bool WalkFacings::drawnYaw(int64_t key, int& facing, float& yaw) const
{
	auto it = m_turnYaws.find(key);
	if(it == m_turnYaws.end() || !target(key, facing))
		return false;
	yaw = it->second;
	return true;
}

std::string WalkFacings::describe(int64_t key) const
{
	auto it = m_h.find(key);
	if(it == m_h.end())
		return "not followed";
	const Heading& h = it->second;
	const double deg = 57.29577951308232;
	char s[240];
	snprintf(s, sizeof(s), "facing %d side %d walking %d yaw %.1f goal %.1f turning %d tan %.1f", (int)h.facing, (int)h.side,
	         h.walking ? 1 : 0, h.hasYaw ? h.yaw * deg : -999.0, h.facing >= 0 && h.facing <= 5 ? cu::facingYaw(h.facing) * deg : -999.0,
	         h.turning ? 1 : 0, h.hasTan ? h.tanYaw * deg : -999.0);
	std::string out = s;
	if(h.hasSample)   // 0783: the walk's last root sample (the server's root on the EXE's path), world units
	{
		snprintf(s, sizeof(s), " root %.2f,%.2f", h.sx, h.sy);
		out += s;
	}
	return out;
}

size_t MergeWalkTurns(const WalkFacings& walkFacings, BuiltFrame& b) { return MergeWalkTurns(walkFacings, b, false); }

size_t MergeWalkTurns(const WalkFacings& walkFacings, BuiltFrame& b, bool placedYaw)
{
	const std::map<int64_t, float>& yaws = walkFacings.turnYaws();
	if(yaws.empty())
		return 0;
	size_t n = 0;
	std::set<int64_t> done;
	for(const cu::UnitScene& sc : b.copies)
		for(const cu::UnitInput& u : sc.units)
		{
			const int64_t key = UnitKeyOf(u.player, u.id);
			auto it = yaws.find(key);
			if(it == yaws.end() || !done.insert(key).second)
				continue;
			int fc;
			if(!walkFacings.target(key, fc) || fc != u.facing)
				continue;   // drawn at another facing than the turn's target: as placed
			if(placedYaw)
			{
				// 0783: the placement laid the figures at the drawn yaw; an extra's heading still composes (ApplyUnitXforms
				// turns by x.yaw - placement.yaw, the placement's yaw being the drawn one)
				auto xi = b.xforms.find(key);
				if(xi == b.xforms.end() || !xi->second.heading)
					continue;
				xi->second.yaw += it->second - cu::facingYaw(fc);
				n++;
				continue;
			}
			UnitXform& x = b.xforms[key];
			if(x.heading)
				x.yaw += it->second - cu::facingYaw(fc);
			else
			{
				x.heading = true;
				x.yaw = it->second;
			}
			n++;
		}
	return n;
}

void AnimRunner::fillFigures(cu::UnitScene& sc) const
{
	m_walkFacings.apply(sc);   // issue 4772: a walking unit's facing (header)
	// programs() is in unit order then figure order (the controller's map by (player, id)); index it once
	std::map<int64_t, std::vector<cu::FigureInput>> by;
	for(const ca::FigureProgram& p : m_ctl.programs())
		if(p.program.single())
		{
			cu::FigureInput f;
			f.figure = (uint16_t)p.figure;
			f.clip = p.program.nodes[0].clip;
			f.t = p.program.nodes[0].t;
			by[UnitKeyOf(p.player, p.id)].push_back(f);
		}
	// issue 4774: a held unit is drawn from its view (the host and unitfeed call this for copies 0, 1, 2 after apply)
	const int copy = m_feed ? m_feed->view().nextCopy() : -1;
	if(m_feed)
		m_feed->view().applyTo(sc, copy);
	for(cu::UnitInput& u : sc.units)
	{
		auto it = by.find(UnitKeyOf(u.player, u.id));
		u.figures = it == by.end() ? std::vector<cu::FigureInput>() : it->second;
	}
	if(m_feed)
		m_feed->view().remember(sc, copy);   // issue 4774: the base of a view taken next frame
}

// ---- issue 4774: the fight player on the runner's controller (4782's FightPlayer, 4775's CombatRun) -----------------------
// start: the record and its members as the core's CombatInput (the flag bits are the same), each unit side's figures as the
// controller holds them (combatFigures), the clip timings from the controller's art (combatClips, durations read once per
// clip with the runner's loader), a CombatRun with the controller's feedback; a refusal (the core's or the controller's)
// names its code and text. advance: the run's events of the step are kept for AnimFeed (takeEvents). The air flag of the
// plan: a member of either side with the domain Air (the core refuses air fights until 4777, so it is the 20 s limit then).
class ControllerFightPlayer : public AnimFightPlayer
{
public:
	explicit ControllerFightPlayer(AnimRunner& r) : r_(r), fb_(r.m_ctl) {}

	void ownerKnown(const Fight& f) override
	{
		if(r_.m_tap)
			r_.m_tap->fightOwner(f);   // issue 4783: the trigger stream's records of this fight carry its event id from now on
	}

	FightPlan start(const Fight& f) override
	{
		if(r_.m_tap)
			r_.m_tap->fightStarted(f);   // issue 4722: the fight's units are the trigger stream's from now on
		FightPlan p;
		if(!r_.m_open)
		{
			p.why = "the controller is not open";
			return p;
		}
		if(!r_.m_combatOk)
		{
			p.why = r_.m_combatWhy;
			return p;
		}
		ca::CombatInput in;
		in.seq = f.rec.seq;
		in.kind = f.rec.kind;
		in.flags = f.rec.flags;
		in.plot = f.rec.plot;
		in.nuclear_level = f.rec.nuclearLevel;
		auto side = [&](ca::CombatSide& s, int32_t owner, int32_t unit, int32_t city, int32_t finalDamage) {
			s.owner = owner;
			s.unit = city >= 0 ? -1 : unit;
			s.city = city;
			s.final_damage = finalDamage < 0 ? 0 : finalDamage;
			const cu::UnitInput* u = unit >= 0 ? r_.m_ctl.unitInput(owner, unit) : nullptr;
			s.max_hp = u && u->max_hp > 0 ? u->max_hp : 100;   // a city's maximum is not in the record [I: 100]
			// issue 4726 (found live on c5h-b, unit-combat-server.md 12.7): the DLL's final damage is not clamped
			// (GenerateMeleeCombatInfo: the damage dealt plus the damage before, 152 for a frigate a destroyer sank), and a
			// VP city's damage goes above 100; the core refused such a record (BadInput) and the fight showed nothing. A
			// unit side is killed at its maximum; a city side is never killed by damage (a city is captured) [I]
			if(city >= 0)
				s.max_hp = (std::max)(s.max_hp, s.final_damage + 1);
			else if(s.final_damage > s.max_hp)
				s.final_damage = s.max_hp;
		};
		side(in.attacker, f.rec.attackerOwner, f.rec.attackerUnit, f.rec.attackerCity, f.rec.finalDamage[0]);
		side(in.defender, f.rec.defenderOwner, f.rec.defenderUnit, f.rec.defenderCity, f.rec.finalDamage[1]);
		// issue 4726: the damage each side takes in this combat, as the feed carries it (-1: not known, the retaliate byte
		// decides 0321 13.4's air opening)
		in.attacker.damage = SrvFeed::CombatDamage(f.rec, 0);
		in.defender.damage = SrvFeed::CombatDamage(f.rec, 1);
		for(const SrvFeed::CombatMember& m : f.members)
		{
			if(m.flags & SrvFeed::kCombatMemberInterceptor)
			{
				// issue 4726: participant 2; its damage is what it deals (known with kCombatDamageKnown)
				in.interceptor.owner = m.player;
				in.interceptor.unit = m.id;
				in.interceptor.max_hp = m.maxHP > 0 ? m.maxHP : 100;
				in.interceptor.final_damage = (std::min)((int32_t)(m.finalDamage < 0 ? 0 : m.finalDamage), in.interceptor.max_hp);
				in.interceptor.damage = (f.rec.flags & SrvFeed::kCombatDamageKnown) ? m.damage : -1;
				continue;
			}
			ca::CombatMemberIn mi;
			mi.player = m.player;
			mi.id = m.id;
			mi.flags = m.flags;
			mi.damage = m.damage;
			mi.final_damage = m.finalDamage;
			mi.max_hp = m.maxHP;
			in.members.push_back(mi);
		}
		std::unique_ptr<Run> run(new Run());
		if(r_.m_tap) r_.m_tap->fightArt(f, r_.m_ctl, *r_.m_art, r_.m_combatTables);   // issue 4728: art, projectile speeds
		for(int s = 0; s < 2; ++s)
		{
			const ca::CombatSide& cs = s ? in.defender : in.attacker;
			if(cs.city >= 0 || cs.unit < 0)
				continue;
			const ca::AnimStatus as = r_.m_ctl.combatFigures(cs.owner, cs.unit, r_.m_combatTables, s ? run->def : run->att);
			if(!as.ok())
			{
				p.why = std::string(s ? "the defender" : "the attacker") + " (" + std::to_string(cs.owner) + ", " +
				        std::to_string(cs.unit) + "): " + ca::name(as.code) + ": " + as.text;
				return p;
			}
			for(const ca::CombatMemberArt& m : (s ? run->def : run->att).members)
				p.air = p.air || m.domain == "Air";
		}
		// issue 4726: the other participants' figures by (player, id): the interceptor, then each damage member that is a
		// unit the controller holds; one the controller does not hold (or refuses) takes part without figures (the core
		// names it in a "Members:" note)
		std::vector<std::pair<int32_t, int32_t>> otherIds;
		if(in.interceptor.unit >= 0)
			otherIds.push_back(std::make_pair(in.interceptor.owner, in.interceptor.unit));
		for(const ca::CombatMemberIn& m : in.members)
			if(!(m.flags & 1u) && m.id >= 0)
				otherIds.push_back(std::make_pair(m.player, m.id));
		for(const auto& k : otherIds)
		{
			bool dup = (k.first == in.attacker.owner && k.second == in.attacker.unit) ||
			           (k.first == in.defender.owner && k.second == in.defender.unit);
			for(const ca::CombatUnitFigures& o : run->others)
				dup = dup || (o.player == k.first && o.id == k.second);
			if(dup)
				continue;   // the defender may also be the interceptor (CvUnitCombat.cpp): its figures are the defender's
			ca::CombatUnitFigures u;
			if(!r_.m_ctl.unitInput(k.first, k.second) || !r_.m_ctl.combatFigures(k.first, k.second, r_.m_combatTables, u).ok() ||
			   u.standing.empty())
			{
				r_.m_mc.othersWithout++;
				continue;
			}
			for(const ca::CombatMemberArt& m : u.members)
				p.air = p.air || m.domain == "Air";   // 4777 8.6: the plan's air flag looks at the interceptor too
			run->others.push_back(u);
			r_.m_mc.othersWith++;
		}
		// an interceptor that is also the defender (CvUnitCombat.cpp: "The defender may be the same unit as the
		// interceptor!") plays as the defender only: one unit's figures in one participant [I]
		if((in.interceptor.owner == in.defender.owner && in.interceptor.unit == in.defender.unit) ||
		   (in.interceptor.owner == in.attacker.owner && in.interceptor.unit == in.attacker.unit))
			in.interceptor = ca::CombatSide();
		run->positioned = positions(f, in, *run);   // issue 4724: the core then times each approach by its distance
		AnimRunner& r = r_;
		run->clips = r_.m_ctl.combatClips([&r](const std::string& clip) -> float {
			const std::string k = c5m::basename_lower(clip);
			auto it = r.m_fightDurs.find(k);
			if(it == r.m_fightDurs.end())
			{
				ca::ClipGraphFactory::readDurations(cu::clipLoaderFromProvider(*r.m_provider, r.m_strings.get()), {clip},
				                                    r.m_fightDurs);
				it = r.m_fightDurs.find(k);
			}
			return it == r.m_fightDurs.end() ? 0.0f : it->second.first;
		});
		ca::CombatParams params;   // seed 0 (the scene's), step 1/30, the 12 s / 20 s limits, defaults of 4775
		params.charge_any = r_.m_chargeAny;   // ticket 4737 (rule A): the host's or the bundle's choice
		params.turn_first = r_.m_turnFirst;   // ticket 4735 (rule A): the host's or the bundle's choice
		params.fight_drawn_figures = r_.m_fightDrawn;   // ticket 4744 (rule A): the host's or the bundle's choice
		params.reform_first = r_.m_reformFirst;         // tickets 4745 + 4747 (rule A): the host's or the bundle's choice
		params.walk_home = r_.m_walkHome;               // ticket 4742 (rule A): the host's or the bundle's choice
		params.city_in_place = r_.m_cityInPlace;        // ticket 4748 (rule A): the host's or the bundle's choice
		params.ready_after_move = r_.m_readyAfterMove;  // ticket 4746 (rule A): the host's or the bundle's choice
		params.defend_from_stance = r_.m_defendFromStance;   // ticket 4750 (rule A): the host's or the bundle's choice
		params.reform_ends_at_arrival = r_.m_reformEndsAtArrival;   // ticket 5050 (rule A): the host's or the bundle's choice
		params.attack_from_stance = r_.m_attackFromStance;   // ticket 5050 (rule A): the host's or the bundle's choice
		params.rush_ride_through = r_.m_rushRideThrough;   // ticket 5057 (rule A): the host's or the bundle's choice
		params.defender_waits_first_landing = r_.m_defenderWaitsFirstLanding;   // ticket 5057 (rule A)
		params.death_by_variant = r_.m_deathByVariant;   // ticket 5053 (rule A): the host's or the bundle's choice
		params.end_after_dying = r_.m_endAfterDying;     // ticket 5053 (rule A): the host's or the bundle's choice
		params.melee_reach = r_.m_meleeReach;   // ticket 0743 (rule A): the host's or the bundle's choice
		params.defender_step = r_.m_defenderStep;   // ticket 5061 (rule A): the host's or the bundle's choice
		params.victory_needs_word = r_.m_victoryNeedsWord;   // ticket 5059 (rule A): the host's or the bundle's choice
		params.type2_phase2 = r_.m_type2Phase2;   // ticket 5067 S1 (rule A): the host's or the bundle's choice
		// ticket 5062 (rule A): a city's own strike flies its owner era's projectile VisEffect, as stock (research
		// unit-combat-server.md 18.37); off, or the era or its tag not known: no name, the rule before 5062
		if(r_.m_cityShot && f.rec.attackerCity >= 0)
		{
			int32_t era = -1;
			if(r_.m_eraCore && f.rec.attackerOwner >= 0)
			{
				const std::vector<SrvFeed::PlayerRecord>& ps = r_.m_eraCore->players();
				const size_t o = (size_t)f.rec.attackerOwner;
				if(o < ps.size() && ps[o].player == f.rec.attackerOwner)
					era = ps[o].era;
			}
			params.city_effect = r_.cityBombardTag(era);
			if(params.city_effect.empty())
				r_.m_cityShotsUnnamed++;
			else
			{
				r_.m_cityShotsNamed++;
				r_.m_lastCityShot = params.city_effect;
			}
		}
		const ca::CombatStatus cs = run->run.start(in, run->att, run->def, run->others, *run->clips, params, &fb_);
		if(!cs.ok())
		{
			p.why = std::string(ca::combatCodeName(cs.code)) + ": " + cs.text;
			return p;
		}
		runs_[f.seq] = std::move(run);
		p.ok = true;
		return p;
	}

	// Ticket 0796: the step's playback rate by quick_combat.h's rule on the run's result so far (off: 1)
	double rate(const Fight& f) override
	{
		if(!r_.m_quick.on())
			return 1.0;
		auto it = runs_.find(f.seq);
		if(it == runs_.end())
			return 1.0;
		// a ranged fight's first strike starts at its first projectile's release: a Transfer trigger the attacker's figures
		// crossed in the last apply (the controller's triggers of that frame)
		const bool ranged = (f.rec.flags & SrvFeed::kCombatRanged) != 0;
		if(ranged && !it->second->released)
			for(const ca::TriggerEvent& t : r_.m_ctl.triggers())
				if(t.type == c5m::TriggerType::Transfer && t.player == f.attacker.owner && t.id == f.attacker.unit)
					it->second->released = true;
		return ca::QuickCombatRate(it->second->run.result(), f.elapsed, r_.m_quick, ranged, it->second->released);
	}

	bool advance(const Fight& f, double dt) override
	{
		auto it = runs_.find(f.seq);
		if(it == runs_.end())
			return true;   // nothing to play (cannot happen: start kept it)
		std::vector<ca::TimedEvent> evs;
		const bool done = it->second->run.advance(dt, evs);
		for(const ca::TimedEvent& te : evs)
			carry_[std::make_pair(te.ev.player, te.ev.id)].push_back(te.ev);   // issue 4729: delivered by takeEvents
		motion(f, *it->second, false);   // issue 4724: the figures' poses at f.elapsed
		if(r_.m_tap && r_.m_quick.on())
			r_.m_tap->fightClock(f.seq, f.wall, f.elapsed, f.rate);   // ticket 0796: the core's times to the real clock
		if(r_.m_tap) r_.m_tap->fightRun(f.seq, it->second->run.result(), it->second->motion, f.elapsed);   // issue 4793/4728
		return done;
	}

	void end(const Fight& f, FightEnd why) override
	{
		auto it = runs_.find(f.seq);
		if(it == runs_.end())
			return;
		const ca::CombatResult& res = it->second->run.result();
		const std::string head = "fight seq " + std::to_string(f.seq) + " (event id " + std::to_string(f.owner) + ")";
		if(why == FightEnd::Limit || res.at_limit)
		{
			auto side = [](const ca::SideOutcome& o) {
				return "standing " + std::to_string(o.standing) + " deaths " + std::to_string(o.died) + " of " + std::to_string(o.planned) +
				       ", owed hits paid " + std::to_string(o.paid) + " of " + std::to_string(o.owed);
			};
			// issue 4733 (diagnostic, no behaviour change): both units' types and each figure's first state and its last one
			// before the limit's forced end ("wait>ready f3,4": figures 3 and 4 began waiting and were ready at the limit)
			auto who = [this](const ca::SideOutcome& o) {
				const cu::UnitInput* u = o.id >= 0 ? r_.m_ctl.unitInput(o.player, o.id) : nullptr;
				return std::to_string(o.player) + "/" + std::to_string(o.id) + " " + (u ? u->unit_type : std::string(o.city ? "city" : "?"));
			};
			auto states = [&res](const ca::SideOutcome& o) {
				std::map<uint16_t, std::pair<std::string, std::string>> fs;   // figure -> (first, last before the limit)
				for(const ca::FigureStep& s : res.steps)
				{
					if(s.player != o.player || s.id != o.id)
						continue;
					auto& p = fs[s.figure];
					if(p.first.empty())
						p.first = p.second = ca::figStateName(s.state);
					else if(s.time < res.limit - 1e-6)
						p.second = ca::figStateName(s.state);
				}
				std::map<std::string, std::string> groups;   // "first>last" -> its figures (the line stays under the 1024 cap)
				for(const auto& kv : fs)
				{
					std::string& g = groups[kv.second.first + ">" + kv.second.second];
					g += (g.empty() ? "" : ",") + std::to_string(kv.first);
				}
				std::string t;
				for(const auto& kv : groups)
					t += " " + kv.first + " f" + kv.second;
				return t;
			};
			loud_.push_back(head + " ran to its limit (" + std::to_string((int)f.limit) +
			                " s): the choreography did not end by itself: attacker " + side(res.attacker) + "; defender " +
			                side(res.defender) + "; " + std::to_string(res.hits.size()) + " hits, " + std::to_string(res.steps.size()) +
			                " figure steps; attacker " + who(res.attacker) + ":" + states(res.attacker) + "; defender " +
			                who(res.defender) + ":" + states(res.defender));
		}
		for(const ca::CombatNote& n : res.notes)
			if(n.text.compare(0, 5, "Naval") != 0)
				loud_.push_back(head + ": the choreography's note at " + std::to_string(n.time) + " s: " + n.text);
		// issue 4729: the fight's requests still waiting for a frame go out now, in order (its end frame: every figure takes
		// its end state, as the EXE forces at the limit); a load drops them with the old game's units
		for(const ca::CombatUnitFigures* u : participants(*it->second))
		{
			auto c = carry_.find(std::make_pair(u->player, u->id));
			if(c == carry_.end())
				continue;
			if(why != FightEnd::Reset)
				pending_.insert(pending_.end(), c->second.begin(), c->second.end());
			carry_.erase(c);
		}
		if(why == FightEnd::Reset)
			carry_.clear();
		// issue 4724: the final motion from the run's result before the run is dropped: its clock runs on for the walk
		// back; a load (Reset) or a unit gone ends the motion with the fight
		if(why == FightEnd::Reset || why == FightEnd::Gone)
		{
			for(const ca::CombatUnitFigures* u : participants(*it->second))   // issue 4726: the other participants too
				if(u->id >= 0)
					r_.dropMotion(u->player, u->id);
		}
		else
			motion(f, *it->second, true);
		runs_.erase(it);
	}

	void takeLoud(std::vector<std::string>& out) override
	{
		out.insert(out.end(), loud_.begin(), loud_.end());
		loud_.clear();
	}

	// Issue 4729 (unit-combat-server.md 12.9): path 4 is a level the controller reads once per frame, so of two requests
	// to one figure in one frame only the last is seen. CombatRun steps the caller's dt in 1/30 s steps, so one long frame
	// (r1: 6.08 s) gave the Nuclear Missile run (1400), combat ready (1600) and attack (1100) at once; its idle machine
	// reads 1400 only, kept 1100 unread and never played the strike (the fight held to its 20 s limit). Here every unit's
	// request events (Action, Kill) go out at most one per figure per frame, in the core's order, each unit's later events
	// behind them (the EXE's simulation posts its figure requests from one state-machine pass per frame, 0321 4.2 [I]).
	// CombatRun's live wait (the strike's clock starts when the attack clip plays) absorbs the frames of delay.
	void takeEvents(std::vector<ca::TimelineEvent>& out) override
	{
		out.insert(out.end(), pending_.begin(), pending_.end());
		pending_.clear();
		for(auto it = carry_.begin(); it != carry_.end();)
		{
			std::deque<ca::TimelineEvent>& q = it->second;
			bool all = false;
			std::set<uint16_t> used;
			while(!q.empty())
			{
				const ca::TimelineEvent& e = q.front();
				const bool request = e.kind == ca::EventKind::Action || e.kind == ca::EventKind::Kill;
				if(request)
				{
					bool clash = all || (e.figures.all && !used.empty());
					for(size_t k = 0; !clash && !e.figures.all && k < e.figures.figures.size(); ++k)
						clash = used.count(e.figures.figures[k]) != 0;
					if(clash)
					{
						++deferred_;
						break;   // this figure's next request waits for the next frame, the unit's later events with it
					}
					if(e.figures.all)
						all = true;
					else
						used.insert(e.figures.figures.begin(), e.figures.figures.end());
				}
				out.push_back(e);
				q.pop_front();
			}
			it = q.empty() ? carry_.erase(it) : std::next(it);
		}
	}
	uint64_t eventsDeferred() const { return deferred_; }   // issue 4729: frames a request waited (counted per wait)

	void figures(const std::set<std::pair<int32_t, int32_t>>& units, std::vector<FigureInstance>& out) override
	{
		out.clear();
		for(const ca::FigureProgram& p : r_.m_ctl.programs())
			if(units.count(std::make_pair(p.player, p.id)))
			{
				FigureInstance g;
				g.player = p.player;
				g.unit = p.id;
				g.figure = (int32_t)p.figure;
				g.instance = p.instance;
				out.push_back(g);
			}
	}

private:
	struct Run
	{
		ca::CombatUnitFigures att, def;
		std::vector<ca::CombatUnitFigures> others;            // issue 4726: the interceptor's and the unit members' figures
		std::unique_ptr<ca::CombatClips> clips;
		ca::CombatRun run;
		// issue 4724
		bool positioned = false;                              // start positions for every side
		bool motionFailed = false;                            // a build was refused: no more tries (one LOUD line)
		size_t builtMoves = 0, builtSteps = 0;                // the result's size at the last build
		std::shared_ptr<const ca::FightMotion> motion;
		bool released = false;                                // ticket 0796: an attacker's Transfer trigger seen (rate)
	};

	// Issue 4724: start positions of both sides: a unit side from its last drawn placement input (the frozen view, as the
	// frame before the start placed it) through combatPositions on this frame's grid; a city defender at its plot centre.
	// All or nothing: a side without positions leaves both without (the core's fixed approach, no motion).
	bool positions(const Fight& f, const ca::CombatInput& in, Run& run)
	{
		bool ok = r_.m_grid && r_.m_motionPlace && r_.m_width;
		bool failed = false;
		std::string why;
		for(int s = 0; s < 2 && ok; ++s)
		{
			const ca::CombatSide& cs = s ? in.defender : in.attacker;
			ca::CombatUnitFigures& u = s ? run.def : run.att;
			if(cs.city >= 0 || cs.unit < 0)
			{
				// a city defends on the combat's plot; issue 4726: so does the nuke's plot target (neither unit nor city).
				// A city attacker: before 5062 no centre (the defender's figures keep their placement's heading [I] and the
				// volley has no origin, so the routes fly nothing); ticket 5062 (W107), switch on: its plot's centre from
				// kind 28's a[3] (the DLL thread's ICvCity1::GetX / GetY), not known or refused: no centre, one plain line
				if(!s)
				{
					if(r_.m_cityShot && cs.city >= 0)
					{
						float ax = 0, ay = 0;
						const int32_t cp = f.attackerCityPlot;
						const c5r::Status as = cp >= 0 ? cu::plotCentre(*r_.m_grid, cp % (int32_t)r_.m_width, cp / (int32_t)r_.m_width, ax, ay)
						                               : c5r::Status::fail(c5r::Err::NotFound, "the feed gave no plot for it");
						if(as.ok())
						{
							u.has_centre = true;
							u.centre_x = ax;
							u.centre_y = ay;
							r_.m_cityCentres++;
						}
						else
						{
							r_.m_cityNoCentre++;
							if(r_.m_cityNoCentre <= 10)   // the first ten, then counted only
								r_.m_cityLines.push_back("units: city shot (5062): fight seq " + std::to_string(f.seq) + ": city " +
								                         std::to_string(cs.owner) + "/" + std::to_string(cs.city) + " (plot " +
								                         std::to_string(cp) + ") has no centre: " + as.str() +
								                         ": its volley is not flown (the rule before 5062)");
						}
					}
					continue;
				}
				float cx = 0, cy = 0;
				const c5r::Status ps = in.plot >= 0
				                           ? cu::plotCentre(*r_.m_grid, in.plot % (int32_t)r_.m_width, in.plot / (int32_t)r_.m_width, cx, cy)
				                           : c5r::Status::fail(c5r::Err::BadLayout, "the combat has no plot");
				if(!ps.ok())
				{
					ok = false;
					failed = true;
					why = std::string(s ? "the defending city" : "the attacking city") + " (plot " + std::to_string(in.plot) + "): " + ps.str();
					break;
				}
				u.has_centre = true;
				u.centre_x = cx;
				u.centre_y = cy;
				continue;
			}
			auto pi = r_.m_placeIn.find(UnitKeyOf(cs.owner, cs.unit));
			if(pi == r_.m_placeIn.end())
			{
				ok = false;   // not placed in the frame before (culled, fogged, not drawn): no positions, not an error
				break;
			}
			const c5r::Status ps = ca::combatPositions(r_.m_formations, *r_.m_grid, pi->second, u);
			if(!ps.ok())
			{
				ok = false;
				failed = true;
				why = "unit (" + std::to_string(cs.owner) + ", " + std::to_string(cs.unit) + "): " + ps.str();
			}
		}
		// issue 4726: the other participants from their placement inputs too; one not placed the frame before (culled,
		// fogged) or refused takes part without figures (its damage shows when the hold gives it back), so that every
		// participant with figures has positions (FightMotion needs them all)
		for(size_t i = 0; ok && i < run.others.size();)
		{
			ca::CombatUnitFigures& u = run.others[i];
			auto pi = r_.m_placeIn.find(UnitKeyOf(u.player, u.id));
			c5r::Status ps = pi == r_.m_placeIn.end() ? c5r::Status::fail(c5r::Err::NotFound, "not placed")
			                                           : ca::combatPositions(r_.m_formations, *r_.m_grid, pi->second, u);
			if(!ps.ok())
			{
				if(pi != r_.m_placeIn.end())
					r_.m_motionLoud.push_back("fight seq " + std::to_string(f.seq) + ": participant (" +
					                          std::to_string(u.player) + ", " + std::to_string(u.id) +
					                          ") has no start positions: " + ps.str() + ": it takes part without figures");
				r_.m_mc.othersUnplaced++;
				run.others.erase(run.others.begin() + (std::ptrdiff_t)i);
				continue;
			}
			++i;
		}
		if(!ok)
		{
			for(ca::CombatUnitFigures* u : {&run.att, &run.def})
			{
				u->x.clear();
				u->y.clear();
				u->has_centre = false;
			}
			for(ca::CombatUnitFigures& u : run.others)
			{
				u.x.clear();
				u.y.clear();
				u.has_centre = false;
			}
			r_.m_mc.noPositions++;
			if(failed)
			{
				r_.m_mc.positionFails++;
				r_.m_motionLoud.push_back("fight seq " + std::to_string(f.seq) + ": no start positions: " + why +
				                          ": the fight runs with the core's fixed approach, its figures stay in their slots");
			}
			return false;
		}
		r_.m_mc.positioned++;
		return true;
	}

	// Issue 4724: the run's FightMotion: while it plays, rebuilt when its result grew (moves and steps so far, its end
	// taken as the fight's limit so that every pose up to now is the fight's); at its end the final one (the walk back
	// after CombatResult::end). Set on every unit side with the fight's clock (f.elapsed).
	void motion(const Fight& f, Run& run, bool final)
	{
		if(!run.positioned || run.motionFailed)
			return;
		const ca::CombatResult& res = run.run.result();
		if(final || !run.motion || res.moves.size() != run.builtMoves || res.steps.size() != run.builtSteps)
		{
			std::shared_ptr<ca::FightMotion> m(new ca::FightMotion());
			c5r::Status st;
			if(final)
				st = m->build(res, run.att, run.def, run.others);   // issue 4726: with the other participants
			else
			{
				ca::CombatResult part = res;
				part.end = (std::max)(f.limit, f.elapsed);   // (std::max): windows.h's macro
				st = m->build(part, run.att, run.def, run.others);
			}
			r_.m_mc.builds++;
			if(!st.ok())
			{
				run.motionFailed = true;
				r_.m_mc.buildFails++;
				r_.m_motionLoud.push_back("fight seq " + std::to_string(f.seq) + ": its figure motion refused: " + st.str() +
				                          ": its figures stay in their slots");
				for(const ca::CombatUnitFigures* u : participants(run))
					if(u->id >= 0)
						r_.dropMotion(u->player, u->id);
				return;
			}
			run.motion = m;
			run.builtMoves = res.moves.size();
			run.builtSteps = res.steps.size();
		}
		for(const ca::CombatUnitFigures* u : participants(run))
			if(u->id >= 0)
			{
				r_.setMotion(u->player, u->id, run.motion, f.elapsed, final);
				r_.m_mc.returns += final ? 1 : 0;
			}
	}
	// Issue 4726: every participant with figures (the two sides, then the interceptor and the unit members)
	static std::vector<const ca::CombatUnitFigures*> participants(const Run& run)
	{
		std::vector<const ca::CombatUnitFigures*> v = {&run.att, &run.def};
		for(const ca::CombatUnitFigures& o : run.others)
			v.push_back(&o);
		return v;
	}
	AnimRunner& r_;
	ca::ControllerFeedback fb_;
	std::map<uint32_t, std::unique_ptr<Run>> runs_;
	std::vector<ca::TimelineEvent> pending_;
	std::map<std::pair<int32_t, int32_t>, std::deque<ca::TimelineEvent>> carry_;   // issue 4729: per unit, not yet given
	uint64_t deferred_ = 0;                                                          // issue 4729
	std::vector<std::string> loud_;
};

AnimFightPlayer& AnimRunner::fightPlayer()
{
	if(!m_fightPlayer)
		m_fightPlayer.reset(new ControllerFightPlayer(*this));
	return *m_fightPlayer;
}

// ---- issue 4724: the fights' figure motion on the drawing renderer's placement ------------------------------------------
// The renderer's placement source: it keeps the placement input of every unit a fight names (the drawn input after the
// held view, with its art's formation and ZOffsets: the start positions of its next fight), places through 4776's
// FightPlacement over the module's own placement, and marks a walking-back unit placed (and settled when FightPlacement
// found every figure in its slot).
class MotionPlacement : public cu::PlacementSource
{
public:
	explicit MotionPlacement(AnimRunner& r) : r_(r) {}
	cu::UnitStatus place(const cu::PlacementRequest& q, cu::UnitPlacement& out) override
	{
		if(!q.unit)
			return r_.m_fightPlace->place(q, out);
		const int64_t key = UnitKeyOf(q.unit->player, q.unit->id);
		if(q.art && r_.m_fightFlags.count(key))
		{
			// as the module's placement forms its input (render/src/units/draw_steps.cpp)
			cu::PlacementInput in;
			in.formation = q.art->formation;
			for(const cu::MemberArt& m : q.art->members)
				for(int k = 0; k < m.count; ++k)
					in.zOffset.push_back(m.zoffset);
			in.plotX = q.unit->plot_x;
			in.plotY = q.unit->plot_y;
			in.hasRoot = q.unit->has_root;
			in.rootX = q.unit->root_x;
			in.rootY = q.unit->root_y;
			in.facing = q.unit->facing;
			in.seed = q.unit->formation_seed;
			in.damage = q.unit->damage;
			in.maxHp = q.unit->max_hp;
			in.alive = q.unit->alive;
			in.waterLevel = q.water_level;
			r_.m_placeIn[key] = in;
		}
		const uint64_t settledBefore = r_.m_fightPlace->settled();
		const uint64_t movedBefore = r_.m_fightPlace->moved();   // ticket 4742
		const cu::UnitStatus st = r_.m_fightPlace->place(q, out);
		auto mit = r_.m_motions.find(key);
		if(mit != r_.m_motions.end() && st.ok())
		{
			mit->second.placedSeq = r_.m_seq;
			if(mit->second.ended && r_.m_fightPlace->settled() != settledBefore)
				mit->second.settled = true;
			if(mit->second.ended && r_.m_fightPlace->moved() != movedBefore)
				mit->second.walked = true;   // ticket 4742: a figure still off its slot after the fight's end
		}
		if(st.ok())
			walkYaw(q, key, out);
		return st;
	}

	// 0783 (unit-server-scene.md 19.13): a unit the walk facings draw at a yaw other than its facing's (0685's turn, the
	// walk's tangent) has its placed figures turned about the root by the difference, each figure's height taken again
	// at its new spot (applyFigureOffsets, as a fight's offsets), so the formation follows the curve with its feet on the
	// ground. A unit a fight names or moves is left as placed, as is one drawn at another facing than the yaw's.
	void walkYaw(const cu::PlacementRequest& q, int64_t key, cu::UnitPlacement& out)
	{
		int fc = -1;
		float yaw = 0;
		if(!q.grid || out.placed.empty() || !r_.m_walkFacings.drawnYaw(key, fc, yaw) || fc != q.unit->facing ||
		   r_.m_fightFlags.count(key) || r_.m_motions.count(key))
			return;
		const float d = (float)std::remainder((double)yaw - (double)out.yaw, 2.0 * 3.14159265358979323846);
		if(!std::isfinite(d) || std::fabs(d) <= 1e-6f)
			return;
		const float c = std::cos(d), s = std::sin(d);
		const float cu0 = std::cos(-out.yaw), su0 = std::sin(-out.yaw);   // the world delta into the unit's frame
		std::vector<cu::FigureOffset> offs;
		offs.reserve(out.placed.size());
		for(const cu::FigurePlacement& f : out.placed)
		{
			const float rx = f.x - out.rootX, ry = f.y - out.rootY;
			const float wx = c * rx - s * ry - rx, wy = s * rx + c * ry - ry;   // R(d) r - r
			cu::FigureOffset o;
			o.figure = f.figure;
			o.dx = cu0 * wx - su0 * wy;
			o.dy = su0 * wx + cu0 * wy;
			o.hasYaw = true;
			o.yaw = (float)std::remainder((double)f.yaw + (double)d, 2.0 * 3.14159265358979323846);
			offs.push_back(o);
		}
		if(cu::applyFigureOffsets(*q.grid, q.water_level, offs, out).ok())
		{
			out.yaw = yaw;
			r_.m_walkYawPlaced++;
		}
	}

private:
	AnimRunner& r_;
};

cu::PlacementSource* AnimRunner::fightPlacement(const c5r::gamefs::GameFiles& files, const std::wstring& installRoot)
{
	if(m_motionTried)
		return m_motionPlace.get();
	m_motionTried = true;
	c5r::Status st;
	m_modulePlace = cu::makeModulePlacement(files, installRoot, &st);
	if(!m_modulePlace || !st.ok())
	{
		m_motionWhy = "the module's placement (UnitFormations.xml): " + st.str();
		m_modulePlace.reset();
		return nullptr;
	}
	const c5r::Status fs = m_formations.load(files, installRoot);
	if(!fs.ok())
	{
		m_motionWhy = "the formation table: " + fs.str();
		m_modulePlace.reset();
		return nullptr;
	}
	m_fightPlace.reset(new ca::FightPlacement(m_modulePlace.get()));
	m_motionPlace.reset(new MotionPlacement(*this));
	return m_motionPlace.get();
}

void AnimRunner::setMotion(int32_t player, int32_t id, std::shared_ptr<const ca::FightMotion> m, double t, bool ended)
{
	Motion& mo = m_motions[UnitKeyOf(player, id)];
	if(mo.ended && !ended)
		m_mc.stopped++;   // the unit's next fight begins while it walks back
	mo.motion = std::move(m);
	mo.t = t;
	mo.ended = ended;
	mo.settled = false;
	mo.tSeq = m_seq + 1;   // set in a close: the frame being closed has this step already
}

void AnimRunner::dropMotion(int32_t player, int32_t id)
{
	m_motions.erase(UnitKeyOf(player, id));
	if(m_fightPlace)
		m_fightPlace->clear(player, id);
}

bool AnimRunner::returningKey(int64_t key) const
{
	auto it = m_motions.find(key);
	if(it == m_motions.end() || !it->second.ended || it->second.settled)
		return false;
	const Motion& m = it->second;
	return m.placedSeq != 0 && m.placedSeq == m_seq && m.motion && m.t <= m.motion->end() + kReturnLimit;
}

bool AnimRunner::returning(int32_t player, int32_t id) const { return returningKey(UnitKeyOf(player, id)); }

void AnimRunner::stopReturn(int32_t player, int32_t id)
{
	auto it = m_motions.find(UnitKeyOf(player, id));
	if(it == m_motions.end() || !it->second.ended)
		return;
	m_mc.stopped++;
	if(it->second.walked)
		m_mc.residualWalks++;   // ticket 4742
	dropMotion(player, id);
}

bool AnimRunner::motionOf(int32_t player, int32_t id, double& t, bool& ended) const
{
	auto it = m_motions.find(UnitKeyOf(player, id));
	if(it == m_motions.end())
		return false;
	t = it->second.t;
	ended = it->second.ended;
	return true;
}

// ---- AnimRecord -----------------------------------------------------------------------------------------------------
void AnimRecord::add(const ca::TimelineFrame& f, const std::vector<cu::PlayerInput>& players)
{
	for(const cu::PlayerInput& p : players)
		if(m_players.insert(p.player).second)
			m_tl.players.push_back(p);   // the file form has one record per player: later changes are counted by AnimFeed
	m_tl.frames.push_back(f);
	m_dirty = true;
}

bool AnimRecord::write(const std::wstring& path, std::string& why)
{
	const std::wstring tmp = path + L".tmp";
	const ca::TimelineStatus s = ca::writeTimelineFile(tmp, m_tl, nullptr);
	if(!s.ok())
	{
		why = s.str();
		DeleteFileW(tmp.c_str());
		return false;
	}
	if(!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		why = "cannot replace the file";
		return false;
	}
	m_dirty = false;
	return true;
}

}  // namespace srvunits
