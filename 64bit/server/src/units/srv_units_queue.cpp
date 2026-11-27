// srv_units_queue.cpp - the per-unit hold of a fighter's DLL-side events (epic 0042, issue 4774; srv_units_queue.h).
// Written 2026-11-20 for this repository (GPL-3.0-or-later).
#include "srv_units_queue.h"

#include <algorithm>

namespace srvunits {

namespace cu = c5r::units;

// Feed kinds (srv_feed.h SrvFeed::Kind), named here so that the hold depends on nothing but its own header.
static const uint16_t kMoved = 12, kTeleported = 13, kDestroyed = 14, kDiedInCombat = 15, kDamage = 16, kRebased = 30;

// ---- UnitHold ---------------------------------------------------------------------------------------------------------
bool UnitHold::waits(uint16_t kind)
{
	return kind == kMoved || kind == kTeleported || kind == kDestroyed || kind == kDiedInCombat || kind == kDamage ||
	       kind == kRebased || kind == kHoldArrive;
}

void UnitHold::begin(int32_t owner, int32_t id, uint32_t fightSeq)
{
	if(id < 0)
		return;   // a city side: no unit
	Unit& u = units_[key(owner, id)];
	if(std::find(u.fights.begin(), u.fights.end(), fightSeq) != u.fights.end())
		return;   // both sides of one fight are one unit (cannot happen; taken once)
	u.fights.push_back(fightSeq);
	c_.begun++;
}

void UnitHold::flush(Unit& u, std::vector<HeldOp>& out)
{
	for(const Entry& e : u.ops)
		out.push_back(e.op);
	total_ -= u.ops.size();
	u.ops.clear();
}

UnitHold::Result UnitHold::take(const HeldOp& op, std::vector<HeldOp>& flushed, std::string& why)
{
	why.clear();
	auto it = units_.find(key(op.owner, op.id));
	if(it == units_.end() || it->second.fights.empty())
		return Pass;
	Unit& u = it->second;
	if(u.broken)
	{
		c_.passedBroken++;
		return Pass;
	}
	if(!waits(op.kind))
	{
		c_.passedHeldUnit++;
		return Pass;
	}
	const bool unitFull = u.ops.size() >= kHoldUnitMax, allFull = total_ >= kHoldTotalMax;
	if(unitFull || allFull)
	{
		const size_t n = u.ops.size();
		(unitFull ? c_.overflowUnit : c_.overflowTotal)++;
		c_.flushed += n;
		flush(u, flushed);
		u.broken = true;
		why = "unit (" + std::to_string(op.owner) + ", " + std::to_string(op.id) + "): " +
		      (unitFull ? "its held ops reached " + std::to_string(kHoldUnitMax)
		                : "all held ops reached " + std::to_string(kHoldTotalMax)) +
		      ": its " + std::to_string(n) + " ops given back now, in order, and it passes everything until its fights end";
		return Overflow;
	}
	Entry e;
	e.fight = u.fights.back();
	e.op = op;
	u.ops.push_back(e);
	total_++;
	c_.held++;
	c_.maxUnit = std::max(c_.maxUnit, u.ops.size());
	c_.maxTotal = std::max(c_.maxTotal, total_);
	return Held;
}

bool UnitHold::end(uint32_t fightSeq, std::vector<HeldOp>& out)
{
	c_.ends++;
	bool any = false;
	for(auto it = units_.begin(); it != units_.end();)
	{
		Unit& u = it->second;
		auto f = std::find(u.fights.begin(), u.fights.end(), fightSeq);
		if(f == u.fights.end())
		{
			++it;
			continue;
		}
		any = true;
		u.fights.erase(f);
		// the front run whose owners have all ended comes back (owners are in order: a later fight owns later ops)
		while(!u.ops.empty() &&
		      std::find(u.fights.begin(), u.fights.end(), u.ops.front().fight) == u.fights.end())
		{
			out.push_back(u.ops.front().op);
			u.ops.pop_front();
			total_--;
			c_.released++;
		}
		if(u.fights.empty())
		{
			// nothing can be left (every owner ended); kept defensive: given back in order
			c_.released += u.ops.size();
			flush(u, out);
			it = units_.erase(it);
			continue;
		}
		++it;
	}
	if(!any)
		c_.endsUnknown++;
	return any;
}

void UnitHold::clear()
{
	c_.droppedLoad += total_;
	c_.clears++;
	units_.clear();
	total_ = 0;
}

bool UnitHold::held(int32_t owner, int32_t id) const
{
	auto it = units_.find(key(owner, id));
	return it != units_.end() && !it->second.fights.empty() && !it->second.broken;
}

bool UnitHold::named(int32_t owner, int32_t id) const
{
	auto it = units_.find(key(owner, id));
	return it != units_.end() && !it->second.fights.empty();
}

size_t UnitHold::queued(int32_t owner, int32_t id) const
{
	auto it = units_.find(key(owner, id));
	return it == units_.end() ? 0 : it->second.ops.size();
}

void UnitHold::namedUnits(std::set<std::pair<int32_t, int32_t>>& out) const
{
	for(const auto& kv : units_)
		if(!kv.second.fights.empty())
			out.insert(std::make_pair((int32_t)(kv.first >> 32), (int32_t)(uint32_t)kv.first));
}

// ---- HoldView ---------------------------------------------------------------------------------------------------------
void HoldView::beginFrame()
{
	if(copy_ > 0 || !cur_.empty())
		prev_.swap(cur_);   // a frame with no copy applied keeps the last frame's list
	cur_.clear();
	copy_ = 0;
}

void HoldView::remember(const cu::UnitScene& sc, int copy)
{
	if(copy < 0 || copy > 2)
		return;
	std::map<int32_t, const cu::PlayerInput*> players;
	for(const cu::PlayerInput& p : sc.players)
		players[p.player] = &p;
	for(const cu::UnitInput& u : sc.units)
	{
		Last& l = cur_[key(u.player, u.id)];
		if(!l.copies || copy == 1)
		{
			l.in = u;   // copy 1 (the wrap copy 0) is the one whose fields are kept when a unit is in several
			l.in.figures.clear();
			auto p = players.find(u.player);
			if(p != players.end())
			{
				l.player = *p->second;
				l.havePlayer = true;
			}
		}
		l.copies |= (uint8_t)(1u << copy);
	}
}

bool HoldView::freeze(int32_t owner, int32_t id)
{
	const int64_t k = key(owner, id);
	if(views_.count(k))
		return true;
	auto it = prev_.find(k);
	if(it == prev_.end())
	{
		c_.missing++;
		return false;
	}
	View v;
	v.last = it->second;
	views_[k] = v;
	c_.frozen++;
	return true;
}

void HoldView::apply(const HeldOp& op, uint32_t width, int facing)
{
	auto it = views_.find(key(op.owner, op.id));
	if(it == views_.end())
		return;
	View& v = it->second;
	cu::UnitInput& in = v.last.in;
	auto place = [&](int32_t plot) {
		if(plot < 0 || !width)
			return;
		in.plot_x = plot % (int32_t)width;
		in.plot_y = plot / (int32_t)width;
		in.has_root = false;
	};
	switch(op.kind)
	{
	case kDamage: in.damage = (int16_t)op.a2; break;
	case kTeleported: place(op.a2); break;
	case kRebased: place(op.a3); break;
	case kMoved: place(op.dest); break;
	case kDiedInCombat: in.flags = (uint8_t)(in.flags | cu::kFlagDead); break;
	case kDestroyed: v.removed = true; break;
	default: return;
	}
	if(facing >= 0 && facing <= 5 && (op.kind == kMoved || op.kind == kTeleported || op.kind == kRebased))
		in.facing = (int8_t)facing;
	c_.applied++;
}

void HoldView::release(int32_t owner, int32_t id)
{
	if(views_.erase(key(owner, id)))
		c_.released++;
}

void HoldView::clear()
{
	views_.clear();
	prev_.clear();
	cur_.clear();
	copy_ = 0;
}

bool HoldView::frozen(int32_t owner, int32_t id) const { return views_.count(key(owner, id)) != 0; }

void HoldView::applyTo(cu::UnitScene& sc, int copy)
{
	if(views_.empty() || copy < 0 || copy > 2)
		return;
	std::set<int64_t> listed;
	for(cu::UnitInput& u : sc.units)
	{
		const int64_t k = key(u.player, u.id);
		auto it = views_.find(k);
		if(it == views_.end())
			continue;
		listed.insert(k);
		if(it->second.removed)
			continue;   // given back with its kind 14 in this frame: the table's last word (it is not listed next frame)
		const cu::UnitInput& v = it->second.last.in;
		u.plot_x = v.plot_x;
		u.plot_y = v.plot_y;
		u.has_root = v.has_root;
		u.root_x = v.root_x;
		u.root_y = v.root_y;
		u.root_z = v.root_z;
		u.facing = v.facing;
		u.damage = v.damage;
		u.max_hp = v.max_hp;
		u.flags = v.flags;
		u.formation_seed = v.formation_seed;
		u.unit_type = v.unit_type;
		u.art_row = v.art_row;
		c_.substituted++;
	}
	for(auto& kv : views_)
	{
		const View& v = kv.second;
		if(listed.count(kv.first) || v.removed || !(v.last.copies & (1u << copy)))
			continue;
		cu::UnitInput in = v.last.in;
		in.fade_from = 1;
		in.fade_to = 0;
		in.fade = 0;
		sc.units.push_back(in);
		c_.relisted++;
		bool havePlayer = false;
		for(const cu::PlayerInput& p : sc.players)
			havePlayer = havePlayer || p.player == in.player;
		if(!havePlayer && v.last.havePlayer)
			sc.players.push_back(v.last.player);
	}
}

}  // namespace srvunits
