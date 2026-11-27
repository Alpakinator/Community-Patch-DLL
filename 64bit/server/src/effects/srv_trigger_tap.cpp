// srv_trigger_tap.cpp - the trigger emitter on the server's unit scene (ticket 4722; srv_trigger_tap.h).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_trigger_tap.h"

#include "srv_trigger_controller.h"

namespace srveffects {

void AnimTriggerTap::fightStarted(const srvunits::Fight& f)
{
	em_.fightStarted(f);
	if(routes_)
	{
		routes_->fightStarted(f);   // 4793
		FightHits h;
		h.start = routes_->now();   // the end of the last frame: the fight's time 0 is this frame's start
		hits_[f.seq] = h;
	}
}

void AnimTriggerTap::fightHits(uint32_t seq, const std::vector<c5r::unitanim::CombatHit>& hits)
{
	if(!routes_)
		return;
	auto it = hits_.find(seq);
	if(it == hits_.end() || hits.size() <= it->second.taken)
		return;
	std::vector<HitIn> in;
	for(size_t k = it->second.taken; k < hits.size(); ++k)
	{
		HitIn h;
		h.hit = hits[k];
		h.fightSeq = seq;
		h.targetIsCity = h.hit.target_id < 0;
		if(it->second.scaled)   // 0796: the fight's own times to the real clock (fightClock)
		{
			h.hit.time = it->second.real(h.hit.time);
			h.hit.launch = it->second.real(h.hit.launch);
		}
		in.push_back(h);
	}
	it->second.taken = hits.size();
	routes_->hits(in, it->second.start);
}

void AnimTriggerTap::fightLaunches(uint32_t seq, const std::vector<c5r::unitanim::CombatHit>& launches)
{
	if(!routes_)
		return;
	auto it = hits_.find(seq);
	if(it == hits_.end() || launches.size() <= it->second.launchesTaken)
		return;
	std::vector<HitIn> in;
	for(size_t k = it->second.launchesTaken; k < launches.size(); ++k)
	{
		HitIn h;
		h.hit = launches[k];
		h.fightSeq = seq;
		h.targetIsCity = h.hit.target_id < 0;
		if(it->second.scaled)   // 0796: the fight's own times to the real clock (fightClock)
		{
			h.hit.time = it->second.real(h.hit.time);
			h.hit.launch = it->second.real(h.hit.launch);
		}
		in.push_back(h);
	}
	it->second.launchesTaken = launches.size();
	routes_->launches(in, it->second.start);
}

bool AnimTriggerTap::frame(int64_t frameId, double dt, const c5r::unitanim::Controller& c,
                           const std::vector<srvunits::FightEndRecord>& ends, const std::vector<srvunits::OwnerEnd>& clipEnds,
                           std::string& why)
{
	if(!em_.beginFrame(frameId, dt, why))
		return false;
	if(routes_ && !routes_->beginFrame(dt, why))
		return false;   // cannot happen after the emitter took the same dt
	if(beforeFrame)
		beforeFrame(c);
	triggersOf(c.triggers(), em_, trig_);
	ends_.assign(clipEnds.begin(), clipEnds.end());
	for(const srvunits::FightEndRecord& e : ends)
		ends_.push_back(e.end);
	if(routes_)
	{
		// 4793: names, held projectiles and Transfers; the fights' ends (flights in the air removed); hits and flights
		routes_->intake(trig_);
		routes_->ownerEnds(ends_);
		for(const srvunits::OwnerEnd& e : ends_)
			if(e.why != srvunits::FightEnd::ClipEnded)
				hits_.erase(e.seq);
		routes_->endFrame();
	}
	em_.triggers(trig_);
	em_.ownerEnds(ends_);
	em_.endFrame();
	if(afterFrame)
		afterFrame(ends_);   // 4728
	return true;
}

}  // namespace srveffects
