// seed_double.cpp - a test double of srvunits::SeedAllocator (server/src/units/srv_units_seed.h) for the unit-scene tests
// of issue 4454 while issue 4453's implementation is not merged. Linked ONLY by server/tests/units/build_units_tests.py,
// and only when server/src/units/srv_units_seed.cpp does not exist (the real one replaces it at once).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). The rule is the note's section 4 (unit-formations.md 12.7):
// one free list, a freed index at the head when below the head else right behind it, a load clear in reverse creation
// order, embark / era change as remove + create. No combat hold (decision 14 is 4453's): Advance does nothing.
#include "units/srv_units_seed.h"

#include <algorithm>
#include <deque>
#include <map>

namespace srvunits {

struct SeedAllocator::Impl
{
	ArtChangesFn art;
	LogFn log;
	std::map<int64_t, uint32_t> seed;
	std::vector<int64_t> live;     // creation order
	std::deque<uint32_t> freeList;
	uint32_t size = 0;
	bool inLoad = false, outOfStep = false;
	SeedCounters c = {};
	static int64_t K(int32_t o, int32_t id) { return ((int64_t)o << 32) | (uint32_t)id; }
	void alloc(int64_t k)
	{
		uint32_t i;
		if(freeList.empty())
			i = size++;
		else
		{
			i = freeList.front();
			freeList.pop_front();
		}
		seed[k] = i;
		live.push_back(k);
		c.created++;
	}
	bool release(int64_t k)
	{
		auto it = seed.find(k);
		if(it == seed.end())
			return false;
		const uint32_t i = it->second;
		seed.erase(it);
		live.erase(std::find(live.begin(), live.end(), k));
		if(freeList.empty() || i < freeList.front())
			freeList.push_front(i);
		else
			freeList.insert(freeList.begin() + 1, i);
		c.freed++;
		return true;
	}
};

SeedAllocator::SeedAllocator(ArtChangesFn artChanges, LogFn log) : m_impl(new Impl())
{
	m_impl->art = std::move(artChanges);
	m_impl->log = std::move(log);
}

SeedAllocator::~SeedAllocator() { delete m_impl; }

void SeedAllocator::Apply(const SrvFeed::Event& e, double, const std::vector<SrvFeed::UnitRecord>& table)
{
	Impl& m = *m_impl;
	const int64_t k = Impl::K(e.a[0], e.a[1]);
	switch(e.kind)
	{
	case SrvFeed::kLoadBegin:
		m.inLoad = true;
	{
		const std::vector<int64_t> l = m.live;   // a key created twice (KnownCreation) appears twice
		for(auto it = l.rbegin(); it != l.rend(); ++it)
			m.release(*it);   // last created first (12.7.3 S5); the size is kept
		m.live.clear();
		break;
	}
	case SrvFeed::kUnitCreated:
		m.inLoad = false;
		if(m.seed.count(k))
		{
			m.c.knownCreation++;
			if(m.log)
				m.log(SeedStatus::kKnownCreation, e, "a second record for a held key");
		}
		m.alloc(k);
		break;
	case SrvFeed::kUnitEmbark:
		m.release(k);
		m.alloc(k);
		break;
	case SrvFeed::kUnitDestroyed:
		if(!m.release(k) && !m.inLoad)
		{
			m.c.unknownRemoval++;
			if(m.log)
				m.log(SeedStatus::kUnknownRemoval, e, "no record held");
		}
		break;
	case SrvFeed::kEraChanged:
		for(const SrvFeed::UnitRecord& u : table)
			if(u.owner == e.a[0] && m.seed.count(Impl::K(u.owner, u.id)) && m.art && m.art(u, u.era, e.a[1]))
			{
				m.release(Impl::K(u.owner, u.id));
				m.alloc(Impl::K(u.owner, u.id));
			}
		break;
	default: break;
	}
}

void SeedAllocator::Advance(double) {}

void SeedAllocator::NoteDropped(uint64_t count)
{
	Impl& m = *m_impl;
	if(!m.outOfStep)
	{
		m.c.outOfStepEpisodes++;
		SrvFeed::Event e = {};
		e.kind = SrvFeed::kOverflow;
		e.a[0] = (int32_t)count;
		if(m.log)
			m.log(SeedStatus::kOutOfStep, e, "events dropped by the feed");
	}
	m.outOfStep = true;
}

bool SeedAllocator::SeedOf(UnitKey key, uint32_t& s) const
{
	auto it = m_impl->seed.find(Impl::K(key.owner, key.id));
	if(it == m_impl->seed.end())
		return false;
	s = it->second;
	return true;
}

bool SeedAllocator::OutOfStep() const { return m_impl->outOfStep; }
SeedCounters SeedAllocator::Counters() const { return m_impl->c; }

}  // namespace srvunits
