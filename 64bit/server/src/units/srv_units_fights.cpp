// srv_units_fights.cpp - the server's fight clock (epic 0042, issue 4782; srv_units_fights.h).
#include "srv_units_fights.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace srvunits {

const char* fightEndName(FightEnd e)
{
	switch(e)
	{
	case FightEnd::Natural: return "Natural";
	case FightEnd::Limit: return "Limit";
	case FightEnd::Refused: return "Refused";
	case FightEnd::Gone: return "Gone";
	case FightEnd::Reset: return "Reset";
	case FightEnd::ClipEnded: return "ClipEnded";
	}
	return "?";
}

namespace {
// Issue 4726: the fight's own units: the attacker, the defender and the interceptor (0321 3.2's three key pairs [I]); the
// damage members are held by the hold (srv_units_anim) but are not keys of the clock.
bool names(const Fight& f, int32_t owner, int32_t unit)
{
	if(unit < 0)
		return false;
	for(const FightSide* s : {&f.attacker, &f.defender, &f.interceptor})
		if(s->owner == owner && s->unit == unit)
			return true;
	return false;
}
}  // namespace

bool FightClock::add(const SrvFeed::CombatRecord& r, const SrvFeed::CombatMember* members, uint32_t nMembers,
                     std::string& why, int32_t attackerCityPlot, bool resumed)
{
	why.clear();
	if(r.kind != SrvFeed::kUnitCombat && r.kind != SrvFeed::kCityCombat)
	{
		++c_.refusedRecords;
		why = "combat seq " + std::to_string(r.seq) + ": kind " + std::to_string(r.kind) + " is not 25 or 28";
		return false;
	}
	bool below = false;
	if(haveSeq_ && r.seq <= lastSeq_)
	{
		// ticket 0742: a record back from its wait behind a walk (5066 (a)) is taken below the last seq, once
		bool held = r.seq == lastSeq_;
		for(const Fight& f : fights_)
			held = held || f.seq == r.seq;
		if(!resumed || held)
		{
			++c_.refusedRecords;
			why = "combat seq " + std::to_string(r.seq) + " not above the last one added (" + std::to_string(lastSeq_) +
			      (resumed ? "), and a fight of its seq is held" : "): fights start in the DLL's call order");
			return false;
		}
		below = true;
	}
	if(nMembers && !members)
	{
		++c_.refusedRecords;
		why = "combat seq " + std::to_string(r.seq) + ": " + std::to_string(nMembers) + " members and no member array";
		return false;
	}
	if(below)
		++c_.resumedBelow;
	else
		lastSeq_ = r.seq;
	haveSeq_ = true;
	++c_.records;
	// 0321 4.1 and 7: no simulation with Quick Combat on or the visualise flag clear
	const bool quick = (r.flags & SrvFeed::kCombatGameRead) && (r.flags & SrvFeed::kCombatQuickOption);
	if(quick || !(r.flags & SrvFeed::kCombatVisualize))
	{
		++c_.noFight;
		return true;
	}
	Fight f;
	f.seq = r.seq;
	f.rec = r;
	f.members.assign(members, members + nMembers);
	f.attackerCityPlot = r.attackerCity >= 0 && attackerCityPlot >= 0 ? attackerCityPlot : -1;   // ticket 5062
	f.ownerKnown = (r.flags & SrvFeed::kCombatReturnedKnown) != 0;
	f.owner = f.ownerKnown ? r.returned : 0;
	if(!f.ownerKnown)
		++c_.ownerUnknown;
	// 0321 3.3: a barrier while the human watches the AI turns (the active player's turn not active) or for a nuclear
	// attack. The human-and-alive part of the EXE's test is not in the record [I: taken as true].
	f.barrier = r.nuclearLevel > 0 ||
	            ((r.flags & SrvFeed::kCombatGameRead) && !(r.flags & SrvFeed::kCombatActiveTurn));
	f.attacker.owner = r.attackerOwner;
	f.attacker.unit = r.attackerUnit;
	f.attacker.city = r.attackerCity;
	f.defender.owner = r.defenderOwner;
	f.defender.unit = r.defenderUnit;
	f.defender.city = r.defenderCity;
	for(const SrvFeed::CombatMember& m : f.members)   // issue 4726: the interceptor's flagged member
		if((r.flags & SrvFeed::kCombatInterceptorMember) && (m.flags & SrvFeed::kCombatMemberInterceptor))
		{
			f.interceptor.owner = m.player;
			f.interceptor.unit = m.id;
		}
	fights_.push_back(f);
	++c_.fights;
	c_.maxQueued = std::max<uint32_t>(c_.maxQueued, static_cast<uint32_t>(queued()));
	return true;
}

// Two fights wait for each other when they name one unit or one city (0321 3.2: the key pairs cross-compared) [C 0321];
// the plot words are not in the record and are not compared [I].
bool FightClock::shares(const Fight& a, const Fight& b) const
{
	const FightSide* as[3] = {&a.attacker, &a.defender, &a.interceptor};   // issue 4726: the interceptor's pair too
	const FightSide* bs[3] = {&b.attacker, &b.defender, &b.interceptor};
	for(const FightSide* x : as)
		for(const FightSide* y : bs)
		{
			if(x->owner != y->owner)
				continue;
			if(x->unit >= 0 && x->unit == y->unit)
				return true;
			if(x->city >= 0 && x->city == y->city)
				return true;
		}
	return false;
}

void FightClock::finish(size_t index, FightEnd why, uint64_t frame, std::vector<OwnerEnd>& out)
{
	Fight f = fights_[index];
	fights_.erase(fights_.begin() + static_cast<std::ptrdiff_t>(index));
	if(f.started)
		player_.end(f, why);
	OwnerEnd e;
	e.frame = frame;
	e.owner = f.owner;
	e.seq = f.seq;
	e.why = why;
	e.elapsed = f.elapsed;
	out.push_back(e);
	if(!f.ownerKnown)
		++c_.ownerZeroEnds;   // issue 4783: the caller says it LOUD (AnimFeed::endFights)
	switch(why)
	{
	case FightEnd::Natural: ++c_.natural; break;
	case FightEnd::Limit: ++c_.limit; break;
	case FightEnd::Refused: ++c_.refused; break;
	case FightEnd::Gone: ++c_.gone; break;
	case FightEnd::Reset: ++c_.reset; break;
	case FightEnd::ClipEnded: break;
	}
	// the fight's figures' open clip instances are covered by its own OwnerEnd
	for(auto it = clips_.begin(); it != clips_.end();)
		if(it->second.first == f.seq)
			it = clips_.erase(it);
		else
			++it;
}

bool FightClock::frame(uint64_t fr, double dt, const std::vector<FigureInstance>* figures, std::vector<OwnerEnd>& out,
                       std::string& why)
{
	why.clear();
	if(!std::isfinite(dt) || dt < 0)
	{
		++c_.badFrames;
		why = "frame " + std::to_string(fr) + ": dt not finite or negative";
		return false;
	}
	if(haveFrame_ && fr <= lastFrame_)
	{
		++c_.badFrames;
		why = "frame " + std::to_string(fr) + " not above the last (" + std::to_string(lastFrame_) + ")";
		return false;
	}
	haveFrame_ = true;
	lastFrame_ = fr;

	// (1) starts, in the DLL's order: a fight waits for every earlier fight that names one of its units or city, a barrier
	// for every earlier fight, and every fight behind a barrier waits for it
	bool barrierAhead = false;
	for(size_t i = 0; i < fights_.size(); ++i)
	{
		Fight& f = fights_[i];
		if(f.started)
		{
			if(f.barrier)
				barrierAhead = true;
			continue;
		}
		bool wait = barrierAhead || (f.barrier && i > 0);
		for(size_t k = 0; k < i && !wait; ++k)
			if(shares(fights_[k], f))
				wait = true;
		if(f.barrier)
			barrierAhead = true;
		if(wait)
			continue;
		f.started = true;
		f.startFrame = fr;
		f.elapsed = 0;
		++c_.started;
		if(f.barrier)
			++c_.barriers;
		const FightPlan plan = player_.start(f);
		f.limit = plan.air ? kFightLimitAir : kFightLimit;
		if(!plan.ok)
		{
			f.refused = true;
			f.refusal = plan.why.empty() ? std::string("refused without a text") : plan.why;
		}
	}
	size_t now = 0;
	for(auto& f : fights_)
		now += f.started ? 1 : 0;
	c_.maxPlaying = std::max<uint32_t>(c_.maxPlaying, static_cast<uint32_t>(now));

	// (2) the fights that started in an earlier frame advance; refusals end in their own frame
	rated_.clear();   // ticket 0796
	std::vector<std::pair<uint32_t, FightEnd>> ends;
	for(auto& f : fights_)
	{
		if(!f.started)
			continue;
		if(f.refused)
		{
			ends.push_back(std::make_pair(f.seq, FightEnd::Refused));
			continue;
		}
		if(f.startFrame == fr)
			continue;
		// ticket 0796: the step at the player's playback rate (1 = real time: dt x 1 is dt exactly, the rule before 0796)
		const double r = player_.rate(f);
		f.rate = (std::isfinite(r) && r > 0) ? r : 1.0;
		f.wall += dt;
		f.elapsed += dt * f.rate;
		if(f.rate != 1.0)
			for(const FightSide* s : {&f.attacker, &f.defender, &f.interceptor})
				if(s->unit >= 0)
					rated_.push_back(std::make_pair(std::make_pair(s->owner, s->unit), f.rate));   // unitRate, ends too
		const bool done = player_.advance(f, dt * f.rate);
		if(done)
			ends.push_back(std::make_pair(f.seq, FightEnd::Natural));
		else if(f.elapsed + 1e-9 >= f.limit)
			ends.push_back(std::make_pair(f.seq, FightEnd::Limit));
	}

	// (3) clip instances of the fighting figures: an instance that changed or vanished since the last frame ended
	if(figures)
	{
		std::map<std::tuple<int32_t, int32_t, int32_t>, uint64_t> cur;
		for(const FigureInstance& g : *figures)
			cur[std::make_tuple(g.player, g.unit, g.figure)] = g.instance;
		for(auto it = clips_.begin(); it != clips_.end();)
		{
			auto n = cur.find(it->first);
			if(n != cur.end() && n->second == it->second.second)
			{
				++it;
				continue;
			}
			uint32_t owner = 0;
			for(const Fight& f : fights_)
				if(f.seq == it->second.first)
					owner = f.owner;
			OwnerEnd e;
			e.frame = fr;
			e.owner = owner;
			e.seq = it->second.first;
			e.player = std::get<0>(it->first);
			e.unit = std::get<1>(it->first);
			e.figure = std::get<2>(it->first);
			e.instance = it->second.second;
			e.why = FightEnd::ClipEnded;
			out.push_back(e);
			++c_.clipEnds;
			it = clips_.erase(it);
		}
		// open the instances of every figure of a playing fight's units (a new or changed instance is open from now)
		for(const FigureInstance& g : *figures)
		{
			const auto key = std::make_tuple(g.player, g.unit, g.figure);
			if(clips_.count(key))
				continue;
			for(const Fight& f : fights_)
				if(f.started && !f.refused && names(f, g.player, g.unit))
				{
					clips_[key] = std::make_pair(f.seq, g.instance);
					break;
				}
		}
	}

	// the fights' own ends, in the DLL's order
	for(auto& e : ends)
		for(size_t i = 0; i < fights_.size(); ++i)
			if(fights_[i].seq == e.first)
			{
				const std::string text = fights_[i].refusal;
				finish(i, e.second, fr, out);
				out.back().text = text;
				break;
			}
	return true;
}

void FightClock::unitGone(int32_t owner, int32_t unit, std::vector<OwnerEnd>& out)
{
	for(size_t i = 0; i < fights_.size();)
	{
		const Fight& f = fights_[i];
		if(names(f, owner, unit))
			finish(i, FightEnd::Gone, lastFrame_, out);
		else
			++i;
	}
}

void FightClock::reset(std::vector<OwnerEnd>& out)
{
	while(!fights_.empty())
		finish(0, FightEnd::Reset, lastFrame_, out);
	clips_.clear();
}

FightClock::OwnerSet FightClock::setOwner(uint32_t seq, uint32_t owner)
{
	for(Fight& f : fights_)
	{
		if(f.seq != seq)
			continue;
		if(f.ownerKnown)
			return OwnerSet::Known;
		f.owner = owner;
		f.ownerKnown = true;
		++c_.ownerLate;
		if(f.started)
			player_.ownerKnown(f);
		return OwnerSet::Applied;
	}
	++c_.ownerLateNoFight;
	return OwnerSet::NoFight;
}

size_t FightClock::queued() const
{
	size_t n = 0;
	for(auto& f : fights_)
		n += f.started ? 0 : 1;
	return n;
}

size_t FightClock::playing() const
{
	return fights_.size() - queued();
}

bool FightClock::inFight(int32_t owner, int32_t unit) const
{
	for(auto& f : fights_)
		if(names(f, owner, unit))
			return true;
	return false;
}

double FightClock::unitRate(int32_t owner, int32_t unit) const
{
	double r = 1.0;   // ticket 0796: the fights stepped in the last frame at a rate (those that ended in it too)
	for(const auto& u : rated_)
		if(u.first.first == owner && u.first.second == unit && u.second > r)
			r = u.second;
	return r;
}

}  // namespace srvunits
