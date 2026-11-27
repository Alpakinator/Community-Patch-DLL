// srv_effect_routes.cpp - a fight's effect starts into the trigger stream (epic 0042, ticket 4793; srv_effect_routes.h).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/srv_effect_routes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace srveffects {

using c5r::effects::HitRoute;
using c5r::effects::NameResult;
using c5r::effects::NameValues;
using c5r::effects::ProjectileRecord;
using c5r::effects::RecordKind;

bool EffectRoutes::beginFrame(double dt, std::string& why)
{
	why.clear();
	if(!std::isfinite(dt) || dt < 0)
	{
		why = "EffectRoutes::beginFrame: dt not finite or negative";
		return false;
	}
	dt_ = dt;
	now_ += dt;
	inFrame_ = true;
	return true;
}

void EffectRoutes::fightStarted(const srvunits::Fight& f)
{
	if(f.attacker.unit >= 0)
		fightOf_[std::make_pair(f.attacker.owner, f.attacker.unit)] = f.seq;
	if(f.defender.unit >= 0)
		fightOf_[std::make_pair(f.defender.owner, f.defender.unit)] = f.seq;
}

void EffectRoutes::note(const std::string& why)
{
	if(c_.why.size() < 64 || c_.why.count(why))
		++c_.why[why];
}

NameValues EffectRoutes::valuesFor(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec)
{
	NameValues v;
	if(!subs_)
		return v;
	std::string s;
	if(subs_->terrain(player, unit, s))
		v.set_terrain(s);
	s.clear();
	if(subs_->baseTerrain(player, unit, s))
		v.set_base_terrain(s);
	s.clear();
	if(subs_->weapon(player, unit, figure, type, ec, s))
		v.set_weapon(s);
	return v;
}

bool EffectRoutes::startHeld(const TriggerIn& t)
{
	auto f = fightOf_.find(std::make_pair(t.player, t.unit));
	if(f == fightOf_.end() || ownerOfCode(t.ec) == ClipOwner::Exe || !em_.inFight(t.player, t.unit))
		return false;   // not a fight's: the emitter counts it as it would any trigger
	const double late = std::max(0.0, dt_ - (double)t.offset);
	const double d = t.duration > 0 ? (double)t.duration : 0.0;
	if(d > 0 ? late >= d : late > p_.lateDrop)
	{
		++c_.heldRefused;
		note("held: too late for its interval");
		return true;   // consumed: not started (1.5 items 1 and 4)
	}
	float pos[3] = {0, 0, 0};
	float orient[4] = {0, 0, 0, 1};
	bool havePos = false;
	if(pos_)
	{
		if(!t.bone.empty())
		{
			if(p_.projectilePose)
			{
				// 5064: the bone's world transform, as the emitter starts any trigger effect (TriggerEmitter::transform)
				havePos = pos_->boneTransform(t.player, t.unit, t.figure, t.bone, pos, orient);
				for(int k = 0; havePos && k < 4; ++k)
					havePos = std::isfinite(orient[k]) && (k == 3 || std::isfinite(pos[k]));
				++(havePos ? c_.heldBoneTransform : c_.heldNoBoneTransform);
				if(!havePos)
				{
					orient[0] = orient[1] = orient[2] = 0;
					orient[3] = 1;
				}
			}
			if(!havePos)
				havePos = pos_->bone(t.player, t.unit, t.figure, t.bone, pos);   // the bone's point (identity), as before 5064
		}
		else if(p_.projectilePose)
			++c_.heldNoBoneTransform;
		if(!havePos && t.figure >= 0)
			havePos = pos_->figure(t.player, t.unit, t.figure, pos);
		if(!havePos)
			havePos = pos_->unit(t.player, t.unit, pos);
	}
	if(!havePos)
	{
		++c_.heldNoPosition;
		return true;
	}
	StartSpec s;
	s.player = t.player;
	s.unit = t.unit;
	s.figure = t.figure;
	// owned by the fight, not by the firing clip instance: the flight outlives the clip (a ClipEnded would stop the arrow
	// in the air); a cut clip's held effect is stopped by the hold's own expiry instead
	s.instance = 0;
	s.name = t.event;
	memcpy(s.pos, pos, sizeof(pos));
	if(p_.projectilePose)
	{
		// 5064: the figure's member scale (the EXE's start carries it and its flight keeps it: effects-server.md 2.4 [L]);
		// the flight that takes this handle sends moves only, so the start's scale is the projectile's for its whole flight
		memcpy(s.orient, orient, sizeof(orient));
		const float sc = pos_ ? pos_->figureScale(t.player, t.unit, t.figure) : 1.0f;
		if(std::isfinite(sc) && sc > 0 && sc < 1e6f)
			s.scale = sc;
		++(s.scale == 1.0f ? c_.heldScaleUnknown : c_.heldScaleMember);   // FigurePositions answers 1 when no member is known
	}
	s.follows = true;      // moved by its flight once taken
	s.ec = t.ec;
	s.trigger = t.trigger;
	s.clipTime = t.clipTime;
	s.late = (float)late;
	s.file = t.file;
	std::string why;
	const uint32_t h = em_.startEffect(s, why);
	if(!h)
	{
		++c_.heldRefused;
		note(why);
		return true;
	}
	const FigKey key(t.player, t.unit, t.figure);
	auto old = heldWait_.find(key);
	if(old != heldWait_.end())
	{
		// a second held start of the same figure before a flight took the first: the first is stopped
		std::string w2;
		em_.stopEffect(old->second.handle, w2);
		++c_.heldExpired;
	}
	Held hd;
	hd.handle = h;
	hd.fseq = f->second;
	hd.trigger = t.trigger;
	hd.name = t.event;
	hd.file = t.file;
	memcpy(hd.pos, pos, sizeof(pos));
	hd.until = now_ - late + (d > 0 ? d : p_.heldGrace);
	heldWait_[key] = hd;
	++c_.held;
	return true;
}

const c5r::effects::HeldEffects* EffectRoutes::heldFor(const std::string& file)
{
	if(held_)
		return held_;
	if(!files_)
		return nullptr;
	if(filesSeen_.insert(file).second)
	{
		const c5m::TriggerSystem* ts = files_(file);
		if(ts)
			ownHeld_.add(file, *ts);
	}
	return &ownHeld_;
}

void EffectRoutes::transfer(const TriggerIn& t)
{
	++c_.transfers;
	if(!p_.heldHandOver)
		return;
	const c5r::effects::HeldEffects* hf = heldFor(t.file);
	auto hd = heldWait_.find(FigKey(t.player, t.unit, t.figure));
	if(!hf || hd == heldWait_.end() || hd->second.file != t.file || hf->refidOf(t.file, t.trigger) != hd->second.trigger)
	{
		++c_.transfersNoHeld;
		return;
	}
	if(!planner_)
		return;   // the held effect ends by its own interval
	c5r::effects::ProjectileSpec spec;
	std::string why;
	if(!planner_(t, hd->second.name, hd->second.pos, spec, why))
	{
		++c_.plannerRefused;
		note("planner: " + why);
		return;
	}
	spec.player = t.player;
	spec.unit = t.unit;
	spec.figure = (uint16_t)(t.figure >= 0 ? t.figure : 0);
	if(spec.effect.empty())
		spec.effect = hd->second.name;
	if(addFlight(spec, why))
		++c_.transfersPlanned;
	else
		note(why);
}

void EffectRoutes::intake(std::vector<TriggerIn>& in)
{
	size_t w = 0;
	for(size_t i = 0; i < in.size(); ++i)
	{
		TriggerIn& t = in[i];
		if(t.type == TrigType::Transfer)
			transfer(t);   // left in the list: the emitter counts it
		if(t.type != TrigType::Effect)
		{
			if(w != i)
				in[w] = std::move(t);
			++w;
			continue;
		}
		++c_.effectTriggers;
		std::string name, why;
		const NameResult r = names_.resolve(t.event, valuesFor(t.player, t.unit, t.figure, t.type, t.ec), name, &why);
		if(r == NameResult::NotDefined || r == NameResult::BadName)
		{
			++c_.refused;
			if(c_.refusedNames.size() < 256 || c_.refusedNames.count(t.event))
				++c_.refusedNames[t.event];
			continue;   // removed: never sent
		}
		if(r == NameResult::Fallback)
			++c_.fallback;
		else if(r == NameResult::WeaponEmpty)
			++c_.weaponEmpty;
		else
			++c_.exact;
		t.event = name;
		const c5r::effects::HeldEffects* hf = p_.heldHandOver ? heldFor(t.file) : nullptr;
		if(hf && hf->held(t.file, t.trigger) && startHeld(t))
			continue;   // started here (or consumed by the catch-up rule)
		if(w != i)
			in[w] = std::move(t);
		++w;
	}
	in.resize(w);
}

void EffectRoutes::hits(const std::vector<HitIn>& in, double fightStart)
{
	for(const HitIn& h : in)
	{
		++c_.hits;
		Pending p;
		p.in = h;
		NameValues target;
		if(h.targetIsCity)
			target.set_terrain(p_.citySurface).set_base_terrain(p_.citySurface);
		else
			target = valuesFor(h.hit.target_player, h.hit.target_id, h.hit.target_figure, TrigType::Effect, h.hit.code);
		if(!c5r::effects::planHitEffect(h.hit, names_, target, p_.cityVolley ? cityPlan() : p_.hit, p.plan))
		{
			++c_.hitNone;
			note("hit: " + p.plan.why);
			continue;
		}
		switch(p.plan.route)
		{
		case HitRoute::OneShot:
			++c_.hitOneShot;
			p.at = fightStart + p.plan.time;
			pending_.push_back(p);
			break;
		case HitRoute::CityVolley:
			++c_.hitCityVolley;
			if(p_.cityVolley && h.hit.shot >= 0)
			{
				auto f = cityFlown_.find(h.fightSeq);
				if(f != cityFlown_.end() && f->second.count(h.hit.shot))
				{
					// 0742: its flight (and the shot's sound, at the city) left at the launch: nothing more here
					++c_.hitCityVolleyFlown;
					break;
				}
			}
			p.plan.flight.t0 += fightStart;
			p.at = p.plan.flight.t0;
			pending_.push_back(p);
			break;
		case HitRoute::Flight:
			++c_.hitFlight;   // the unit's flight (addFlight) starts its HitEffect where it lands
			break;
		case HitRoute::CityDefence:
			++c_.hitCityDefence;
			break;
		case HitRoute::Refused:
			++c_.hitRefused;
			note("hit: " + p.plan.why);
			break;
		default:
			++c_.hitNone;
			break;
		}
	}
}

c5r::effects::HitPlanParams EffectRoutes::cityPlan() const
{
	c5r::effects::HitPlanParams hp = p_.hit;
	hp.city_scale = p_.cityVolleyScale;
	hp.city_child_scale = p_.cityVolleyChildScale;
	return hp;
}

// 0742: a city volley's shots at their launch. Each is planned as the landing's hit would be (planHitEffect's CityVolley:
// from the city's centre to the aimed figure, t0 = the launch, duration = the core's flight) and applied at its launch,
// so the projectile travels; the landing's hit with the same shot then adds nothing (hits()).
void EffectRoutes::launches(const std::vector<HitIn>& in, double fightStart)
{
	for(const HitIn& h : in)
	{
		++c_.cityLaunches;
		if(!p_.cityVolley || !h.hit.city || h.hit.shot < 0)
			continue;
		Pending p;
		p.in = h;
		NameValues target;
		if(h.targetIsCity)
			target.set_terrain(p_.citySurface).set_base_terrain(p_.citySurface);
		else
			target = valuesFor(h.hit.target_player, h.hit.target_id, h.hit.target_figure, TrigType::Effect, h.hit.code);
		if(!c5r::effects::planHitEffect(h.hit, names_, target, cityPlan(), p.plan) || p.plan.route != HitRoute::CityVolley)
		{
			++c_.cityLaunchNotFlown;   // no centre or no name: the landing's hit routes as before 0742
			note("city launch: " + p.plan.why);
			continue;
		}
		++c_.cityLaunchFlights;
		p.plan.flight.t0 += fightStart;
		p.at = p.plan.flight.t0;
		pending_.push_back(p);
		cityFlown_[h.fightSeq].insert(h.hit.shot);
	}
}

// An unset HitEffectScale (NULL, 0 or below): the striking figure's member scale, as the EXE's reader takes the member's
// own value (effects-server.md 1.1 item 1). 1.0 only when no figure or no positions are known; counted either way.
float EffectRoutes::hitMemberScale(const c5r::unitanim::CombatHit& h)
{
	if(pos_ && !h.city)
	{
		const float s = pos_->figureScale(h.player, h.id, h.figure);
		if(std::isfinite(s) && s > 0 && s < 1e6f)
		{
			++c_.hitScaleMember;
			return s;
		}
	}
	++c_.hitScaleUnknown;
	return 1.0f;
}

void EffectRoutes::applyHit(const Pending& p)
{
	const c5r::unitanim::CombatHit& h = p.in.hit;
	// the owner: the striking unit's fight; a city strikes for its fight's unit (the target)
	int32_t player = h.player, unit = h.id, figure = h.figure;
	if(h.city || !em_.inFight(player, unit))
	{
		player = h.target_player;
		unit = h.target_id;
		figure = -1;
	}
	if(!em_.inFight(player, unit))
	{
		++c_.hitNoFight;
		note("hit: no running fight for its units");
		return;
	}
	const double late = std::max(0.0, now_ - p.at);
	std::string why;
	// the hit's sound (4777's CombatHit::sound): a city's strike names a script (the caller's city sound); a unit's
	// weapon names its sound tag, which is a $(WEAPON) value of the clips' own Sound triggers, not a script: not sent
	if(!h.sound.empty())
	{
		bool looping = false;
		if(scripts_ && scripts_->find(h.sound, looping) && !looping)
		{
			StartSpec s;
			s.player = player;
			s.unit = unit;
			s.figure = figure;
			s.name = h.sound;
			const float* at = p.plan.route == HitRoute::CityVolley && h.has_origin ? h.origin : h.pos;
			memcpy(s.pos, at, sizeof(s.pos));
			s.ec = h.code;
			s.trigger = h.trigger;
			s.late = (float)late;
			if(em_.startSound(s, why))
				++c_.hitSounds;
			else
				note(why);
		}
		else
			++c_.hitSoundNotScript;
	}
	if(p.plan.route == HitRoute::OneShot)
	{
		if(late > p_.lateDrop)
		{
			++c_.hitLate;
			return;
		}
		StartSpec s;
		s.player = player;
		s.unit = unit;
		s.figure = figure;
		s.name = p.plan.effect;
		memcpy(s.pos, p.plan.pos, sizeof(s.pos));
		// HitEffectScale when the weapon sets it, else the striking figure's member scale as the EXE takes it
		// (effects-server.md 1.1 item 1); 1.0 only when neither is known (effects-formats.md 15.7.8)
		s.scale = p.plan.scale > 0 ? p.plan.scale : hitMemberScale(h);
		s.oneShot = true;
		s.ec = h.code;
		s.trigger = h.trigger;
		s.late = (float)late;
		if(!em_.startEffect(s, why))
		{
			++c_.hitEmitterRefused;
			note(why);
		}
		return;
	}
	// a city volley: into the flights, owned by the fight's unit
	c5r::effects::ProjectileSpec spec = p.plan.flight;
	spec.player = player;
	spec.unit = unit;
	spec.figure = 0;
	spec.owner = p.in.fightSeq;
	uint32_t sh = 0;
	if(!set_.add(spec, sh, &why))
	{
		++c_.flightsRefused;
		note("city volley: " + why);
		return;
	}
	FlightRef fr;
	fr.player = player;
	fr.unit = unit;
	fr.figure = -1;
	fr.fseq = p.in.fightSeq;
	flights_[sh] = fr;
	++c_.flightsAdded;
}

uint32_t EffectRoutes::addFlight(c5r::effects::ProjectileSpec spec, std::string& why)
{
	why.clear();
	auto f = fightOf_.find(std::make_pair(spec.player, spec.unit));
	if(f == fightOf_.end() || !em_.inFight(spec.player, spec.unit))
	{
		++c_.flightsRefused;
		why = "addFlight: unit " + std::to_string(spec.player) + "/" + std::to_string(spec.unit) + " is in no running fight";
		return 0;
	}
	FlightRef fr;
	fr.player = spec.player;
	fr.unit = spec.unit;
	fr.figure = spec.figure;
	fr.fseq = f->second;
	spec.owner = f->second;
	auto hd = heldWait_.find(FigKey(spec.player, spec.unit, (int32_t)spec.figure));
	if(hd != heldWait_.end())
	{
		fr.emitterHandle = hd->second.handle;
		spec.start_effect = false;
		heldWait_.erase(hd);
		++c_.heldTaken;
	}
	uint32_t sh = 0;
	if(!set_.add(spec, sh, &why))
	{
		++c_.flightsRefused;
		if(fr.emitterHandle)
		{
			std::string w2;
			em_.stopEffect(fr.emitterHandle, w2);
		}
		return 0;
	}
	flights_[sh] = fr;
	++c_.flightsAdded;
	return sh;
}

void EffectRoutes::ownerEnds(const std::vector<srvunits::OwnerEnd>& ends)
{
	for(const srvunits::OwnerEnd& e : ends)
	{
		if(e.why == srvunits::FightEnd::ClipEnded)
			continue;
		std::vector<ProjectileRecord> recs;
		c_.flightsForced += set_.forceEnd(now_, e.seq, recs);
		pass(recs);
		for(auto it = flights_.begin(); it != flights_.end();)
			it = it->second.fseq == e.seq ? flights_.erase(it) : std::next(it);
		pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.in.fightSeq == e.seq; }),
		               pending_.end());
		for(auto it = heldWait_.begin(); it != heldWait_.end();)
			it = it->second.fseq == e.seq ? heldWait_.erase(it) : std::next(it);   // the emitter stops them before its OwnerEnd
		for(auto it = fightOf_.begin(); it != fightOf_.end();)
			it = it->second == e.seq ? fightOf_.erase(it) : std::next(it);
		cityFlown_.erase(e.seq);   // 0742
	}
}

void EffectRoutes::pass(const std::vector<ProjectileRecord>& recs)
{
	for(const ProjectileRecord& r : recs)
	{
		auto it = flights_.find(r.handle);
		if(it == flights_.end())
		{
			++c_.recRefused;
			note("flight record of an unknown handle");
			continue;
		}
		FlightRef& f = it->second;
		const c5r::effects::V3 t = r.world.translation();
		float pos[3] = {t.x, t.y, t.z};
		float q[4] = {0, 0, 0, 1};
		c5r::effects::rotationQuaternion(r.world, q);
		float vel[3] = {r.velocity.x, r.velocity.y, r.velocity.z};
		const double late = std::max(0.0, now_ - r.time);
		std::string why;
		switch(r.kind)
		{
		case RecordKind::EffectStart:
		{
			StartSpec s;
			s.player = f.player;
			s.unit = f.unit;
			s.figure = f.figure;
			s.name = r.effect;
			memcpy(s.pos, pos, sizeof(pos));
			memcpy(s.orient, q, sizeof(q));
			s.scale = r.scale > 0 ? r.scale : 1.0f;
			memcpy(s.vel, vel, sizeof(vel));
			s.follows = true;
			s.late = (float)late;
			f.emitterHandle = em_.startEffect(s, why);
			if(f.emitterHandle)
				++c_.recStart;
			else
			{
				++c_.recRefused;
				note(why);
			}
			break;
		}
		case RecordKind::EffectMove:
			if(!f.emitterHandle)
				break;   // its start was refused: nothing to move
			if(em_.moveEffect(f.emitterHandle, pos, q, vel, why))
				++c_.recMove;
			else
			{
				++c_.recRefused;
				note(why);
			}
			break;
		case RecordKind::EffectStop:
			if(f.emitterHandle && em_.stopEffect(f.emitterHandle, why))
				++c_.recStop;
			else if(f.emitterHandle)
			{
				++c_.recRefused;
				note(why);
			}
			f.emitterHandle = 0;
			break;
		case RecordKind::ChildStart:
		case RecordKind::HitStart:
		{
			StartSpec s;
			s.player = f.player;
			s.unit = f.unit;
			s.figure = f.figure;
			s.name = r.effect;
			memcpy(s.pos, pos, sizeof(pos));
			memcpy(s.orient, q, sizeof(q));
			s.scale = r.scale > 0 ? r.scale : 1.0f;
			s.oneShot = true;
			s.late = (float)late;
			if(em_.startEffect(s, why))
				++(r.kind == RecordKind::ChildStart ? c_.recChild : c_.recHit);
			else
			{
				++c_.recRefused;
				note(why);
			}
			break;
		}
		}
	}
}

void EffectRoutes::endFrame()
{
	if(!inFrame_)
		return;
	inFrame_ = false;
	// hits whose moment has come, in time order
	std::stable_sort(pending_.begin(), pending_.end(), [](const Pending& a, const Pending& b) { return a.at < b.at; });
	size_t n = 0;
	while(n < pending_.size() && pending_[n].at <= now_ + 1e-9)
		++n;
	std::vector<Pending> due(pending_.begin(), pending_.begin() + (std::ptrdiff_t)n);
	pending_.erase(pending_.begin(), pending_.begin() + (std::ptrdiff_t)n);
	for(const Pending& p : due)
		applyHit(p);
	// the flights at now
	std::vector<ProjectileRecord> recs;
	set_.frame(now_, recs, nullptr);
	pass(recs);
	// held effects that no flight took
	for(auto it = heldWait_.begin(); it != heldWait_.end();)
	{
		if(now_ > it->second.until + 1e-9)
		{
			std::string why;
			em_.stopEffect(it->second.handle, why);
			++c_.heldExpired;
			it = heldWait_.erase(it);
		}
		else
			++it;
	}
}

}  // namespace srveffects
