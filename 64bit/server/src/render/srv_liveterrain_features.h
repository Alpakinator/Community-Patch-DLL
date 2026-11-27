// srv_liveterrain_features.h - the feature types' names by id, for the live-terrain producer (epic 0465, ticket 4907).
// Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later).
//
// The feed's PlotRecord carries feature ids only; the producer takes a natural wonder by its Type ("FEATURE_ULURU",
// asset-pipeline.md 4.2 point 1: the pattern flags of each wonder) and the marsh by "FEATURE_MARSH" (8.1's Marsh reason;
// id 2 when the name is empty). Model: server/src/forest/srv_forest_ids.h (a small process-wide table written by the
// feed's DLL side, read on the render thread).
//
// For 0.1b the name is made from the feature's art define, which the feed already reads for the trees (GetArtDefineTag,
// on the D30 purity list): "ART_DEF_FEATURE_X" -> "FEATURE_X" (lead-0465 #6640 (f), MC #6639: no new DLL-boundary
// call). [I] that every feature's art define is its Type with "ART_DEF_" in front: true for the stock wonders' rows the
// producer names (asset-pipeline.md 4.2 point 1) as far as Civ5Features.xml's convention goes; checked live on 4906's
// natural-wonder transitions (live-terrain.md 9.5). A tag of another form gives no name (the producer's rule for an
// unnamed wonder applies). GetType (not on D30) only after the release if this proves wrong.
// Writer: server/src/remote/srv_remote_feed.cpp, FeatureCache::IsNW (once per feature type per snapshot or refresh).
//
// Header-only (inline functions with one shared state), thread-safe (one SRW lock).
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
#include <string>
#include <vector>

namespace srvrender {

namespace ltfeatures_detail {
struct State
{
	SRWLOCK lock = SRWLOCK_INIT;
	std::vector<std::string> names;   // by feature id; empty: not known
	uint32_t generation = 0;
};
inline State& Get()
{
	static State s;
	return s;
}
}  // namespace ltfeatures_detail

// "ART_DEF_FEATURE_X" -> "FEATURE_X"; "" for null or any other form.
inline std::string LiveTerrainTypeFromArtTag(const char* artDefineTag)
{
	static const char kPrefix[] = "ART_DEF_FEATURE_";
	if(!artDefineTag || strncmp(artDefineTag, kPrefix, sizeof(kPrefix) - 1) != 0 || !artDefineTag[sizeof(kPrefix) - 1])
		return std::string();
	return std::string(artDefineTag + 8);   // drops "ART_DEF_"
}

// A feature type's id and art define, as read (any DLL thread). Ids above 4096 are ignored; a tag that gives no name
// clears the id's name.
inline void LiveTerrainFeatureArtTagSeen(int featureId, const char* artDefineTag)
{
	if(featureId < 0 || featureId > 4096)
		return;
	const std::string type = LiveTerrainTypeFromArtTag(artDefineTag);
	ltfeatures_detail::State& s = ltfeatures_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	if((size_t)featureId >= s.names.size())
		s.names.resize((size_t)featureId + 1);
	if(s.names[featureId] != type)
	{
		s.names[featureId] = type;
		s.generation++;
	}
	ReleaseSRWLockExclusive(&s.lock);
}

// The Type of a feature id ("" when not known).
inline std::string LiveTerrainFeatureType(int featureId)
{
	ltfeatures_detail::State& s = ltfeatures_detail::Get();
	AcquireSRWLockShared(&s.lock);
	std::string r = featureId >= 0 && (size_t)featureId < s.names.size() ? s.names[featureId] : std::string();
	ReleaseSRWLockShared(&s.lock);
	return r;
}

inline void LiveTerrainFeatureTypesReset()
{
	ltfeatures_detail::State& s = ltfeatures_detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	s.names.clear();
	s.generation++;
	ReleaseSRWLockExclusive(&s.lock);
}

}  // namespace srvrender
