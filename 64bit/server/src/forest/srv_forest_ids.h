// srv_forest_ids.h - the feature ids of FEATURE_FOREST and FEATURE_JUNGLE for the renderer's trees (epic 0431, ticket 4554).
// Written 2026-11-16 for this repository (GPL-3.0-or-later). research/05-graphics-composition/trees.md 13.3.
//
// The render thread never reads the DLL (bug 0103) and the feed's PlotRecord carries feature type indices only, so the
// trees learn which index is the forest and which the jungle from this small process-wide table. Its writer is the feed's
// DLL side (server/src/remote/srv_remote_feed.cpp, the wiring of trees.md 13.6): ForestFeatureTagSeen(id,
// ICvFeatureInfo1::GetArtDefineTag()) for every feature type it reads in a snapshot or refresh pass (a pure getter of the
// D30 list), then ForestFeatureIdsResolved() just before SrvFeed::EndSnapshot. Its reader is ForestScene::poll() on the
// render thread. The ids are found by name, never fixed (design 9.3); 4550 measured 5 / 1 on its saves (trees.md 10.1).
//
// Header-only (inline functions with one shared state, C++14 ODR rules) so that the DLL side can include it without the
// renderer's sources or headers, and a program that has no forest units still links. Thread-safe (one SRW lock).
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

// The art defines of the two features in Civ5Features.xml [I: the tag text; the ids found with it are what 4550 measured].
const char* const kForestArtTag = "ART_DEF_FEATURE_FOREST";
const char* const kJungleArtTag = "ART_DEF_FEATURE_JUNGLE";

struct ForestFeatureIds
{
	int forest = -1, jungle = -1;   // -1: no feature type with that art define seen
	bool resolved = false;          // a snapshot has been scanned (ForestFeatureIdsResolved), or the host set them itself
	uint32_t generation = 0;        // +1 on every change of the three fields above
};

namespace forestids_detail {
struct State
{
	SRWLOCK lock = SRWLOCK_INIT;
	ForestFeatureIds ids;
};
inline State& Get()
{
	static State s;
	return s;
}
}  // namespace forestids_detail

// A feature type's art define, as read (any DLL thread). A tag other than the two is ignored; another id for the same tag
// replaces the first (the generation moves).
inline void ForestFeatureTagSeen(int featureId, const char* artDefineTag)
{
	if(featureId < 0 || !artDefineTag)
		return;
	const bool f = strcmp(artDefineTag, kForestArtTag) == 0, j = strcmp(artDefineTag, kJungleArtTag) == 0;
	if(!f && !j)
		return;
	forestids_detail::State& s = forestids_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	int& slot = f ? s.ids.forest : s.ids.jungle;
	if(slot != featureId)
	{
		slot = featureId;
		s.ids.generation++;
	}
	ReleaseSRWLockExclusive(&s.lock);
}

// Every feature type present on the map has been read (call before SrvFeed::EndSnapshot).
inline void ForestFeatureIdsResolved()
{
	forestids_detail::State& s = forestids_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	if(!s.ids.resolved)
	{
		s.ids.resolved = true;
		s.ids.generation++;
	}
	ReleaseSRWLockExclusive(&s.lock);
}

// Back to "nothing known" (a new game; the tests). The generation keeps rising.
inline void ForestFeatureIdsReset()
{
	forestids_detail::State& s = forestids_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	const uint32_t gen = s.ids.generation + 1;
	s.ids = ForestFeatureIds();
	s.ids.generation = gen;
	ReleaseSRWLockExclusive(&s.lock);
}

inline ForestFeatureIds ForestFeatureIdsNow()
{
	forestids_detail::State& s = forestids_detail::Get();
	AcquireSRWLockShared(&s.lock);
	const ForestFeatureIds r = s.ids;
	ReleaseSRWLockShared(&s.lock);
	return r;
}

}  // namespace srvrender
