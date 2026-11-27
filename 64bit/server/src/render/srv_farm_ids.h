// srv_farm_ids.h - the improvement id of the farm and the resource id of wheat for the server's farm fields (epic 0528,
// ticket 4852). Written 2026-11-18 for this repository (GPL-3.0-or-later). The pattern of server/src/forest/srv_forest_ids.h.
//
// The render thread never reads the DLL (bug 0103) and the feed's PlotRecord carries type indices only, so the farm
// producer (srv_farmfields) learns which improvement index draws as a farm and which resource index is wheat from this
// small process-wide table. Its writer is the feed's DLL side (server/src/remote/srv_remote_feed.cpp): at every plot
// snapshot it reads every improvement's and every resource's art define (ICvImprovementInfo1 / ICvResourceInfo1::
// GetArtDefineTag, pure getters of the D30 list), calls FarmImprovementTagSeen / WheatResourceTagSeen with each, and
// FarmIdsResolved() just before SrvFeed::EndSnapshot. Its reader is FarmFieldsProducer on the render thread. The ids are
// found by name, never fixed: the EXE's farm builder acts on the art id of ART_DEF_IMPROVEMENT_FARM and the farm manager
// sets its wheat flag for the resource art ART_DEF_RESOURCE_WHEAT (farm-fields-layout.md 1.2, 4.2 step 5, 9).
//
// Header-only (inline functions with one shared state, C++14 ODR rules) so that the DLL side includes it without the
// renderer's headers. Thread-safe (one SRW lock).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <string.h>

namespace srvrender {

const char* const kFarmArtTag = "ART_DEF_IMPROVEMENT_FARM";
const char* const kWheatArtTag = "ART_DEF_RESOURCE_WHEAT";

struct FarmIds
{
	int farm = -1, wheat = -1;   // -1: no improvement / resource with that art define seen
	bool resolved = false;       // a snapshot has been scanned (FarmIdsResolved)
	uint32_t generation = 0;     // +1 on every change of the three fields above
};

namespace farmids_detail {
struct State
{
	SRWLOCK lock = SRWLOCK_INIT;
	FarmIds ids;
};
inline State& Get()
{
	static State s;
	return s;
}
// Keeps the lowest id seen with the tag (several types with one art define: a deterministic choice).
inline void SetLowest(int FarmIds::*slot, int id)
{
	State& s = Get();
	AcquireSRWLockExclusive(&s.lock);
	if(s.ids.*slot < 0 || id < s.ids.*slot)
	{
		s.ids.*slot = id;
		s.ids.generation++;
	}
	ReleaseSRWLockExclusive(&s.lock);
}
}  // namespace farmids_detail

// An improvement type's art define, as read (any DLL thread): the farm's when the tag is ART_DEF_IMPROVEMENT_FARM.
inline void FarmImprovementTagSeen(int improvementId, const char* artDefineTag)
{
	if(improvementId >= 0 && artDefineTag && strcmp(artDefineTag, kFarmArtTag) == 0)
		farmids_detail::SetLowest(&FarmIds::farm, improvementId);
}

// A resource type's art define: wheat's when the tag is ART_DEF_RESOURCE_WHEAT.
inline void WheatResourceTagSeen(int resourceId, const char* artDefineTag)
{
	if(resourceId >= 0 && artDefineTag && strcmp(artDefineTag, kWheatArtTag) == 0)
		farmids_detail::SetLowest(&FarmIds::wheat, resourceId);
}

// Every improvement and resource type has been read (call before SrvFeed::EndSnapshot).
inline void FarmIdsResolved()
{
	farmids_detail::State& s = farmids_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	if(!s.ids.resolved)
	{
		s.ids.resolved = true;
		s.ids.generation++;
	}
	ReleaseSRWLockExclusive(&s.lock);
}

// Back to "nothing known" (a new game; the tests). The generation keeps rising.
inline void FarmIdsReset()
{
	farmids_detail::State& s = farmids_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	const uint32_t gen = s.ids.generation + 1;
	s.ids = FarmIds();
	s.ids.generation = gen;
	ReleaseSRWLockExclusive(&s.lock);
}

inline FarmIds FarmIdsNow()
{
	farmids_detail::State& s = farmids_detail::Get();
	AcquireSRWLockShared(&s.lock);
	const FarmIds r = s.ids;
	ReleaseSRWLockShared(&s.lock);
	return r;
}

}  // namespace srvrender
