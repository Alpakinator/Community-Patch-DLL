// trigger_checks.h - the acceptance checks of ticket 4722 over a written trigger stream (a record list in write order:
// the offline log read back, or a test sink's list). Shared by test_trigger_emitter.cpp and trigger_scene.cpp.
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "vp_trigger.h"
#include "effects/srv_trigger_emitter.h"

namespace trigchecks {

struct StreamCheck
{
	uint64_t kinds[VPTRIG_KIND_COUNT] = {};
	uint64_t loops = 0, loopsByStop = 0, loopsByClipEnd = 0, loopsByFightEnd = 0, loopsUnstopped = 0;
	uint64_t effectsStopped = 0;          // EffectStart records later stopped (EffectStop)
	uint64_t recordsAfterEnd = 0;         // a record of an owner after its OwnerEnd
	uint64_t exeRecords = 0;              // a record of a clip the EXE's figures own (1.6)
	uint64_t unknownHandle = 0;           // a stop or move of a handle never started or already stopped
	uint64_t order = 0;                   // seq not increasing or frame ids decreasing
	uint64_t fightEnds = 0, clipEnds = 0, played = 0;
	std::vector<std::string> faults;      // the first few, named
	bool ok() const { return loopsUnstopped == 0 && recordsAfterEnd == 0 && exeRecords == 0 && unknownHandle == 0 && order == 0; }
};

inline void fault(StreamCheck& c, const std::string& s)
{
	if(c.faults.size() < 8)
		c.faults.push_back(s);
}

// movementSent: the emitter ran with movementInFight (Movement clips are then the server's too).
inline StreamCheck checkStream(const std::vector<VPTrigRecord>& recs, bool movementSent = false)
{
	StreamCheck c;
	typedef std::tuple<int32_t, int32_t, int32_t, uint64_t> Inst;
	std::set<uint32_t> endedFights;        // fseq
	std::set<uint32_t> endedOwners;        // owner ids (fights with a known owner)
	std::set<Inst> endedInst;
	std::map<uint32_t, size_t> liveLoops;  // handle -> index of its start
	std::set<uint32_t> liveHandles;
	uint64_t lastSeq = 0;
	int64_t lastFrame = -1;
	for(size_t i = 0; i < recs.size(); ++i)
	{
		const VPTrigRecord& r = recs[i];
		if(r.kind < VPTRIG_KIND_COUNT)
			++c.kinds[r.kind];
		if((lastSeq && r.seq <= lastSeq) || r.frameId < lastFrame)
		{
			++c.order;
			fault(c, "order at record " + std::to_string(r.seq));
		}
		lastSeq = r.seq;
		lastFrame = r.frameId;
		if(r.kind == VPTRIG_OWNER_END)
		{
			if(VPTrigPlayedFor(r.reason) != ((r.flags & VPTRIG_FLAG_PLAYED) != 0))
				fault(c, "played flag at " + std::to_string(r.seq));
			if(r.flags & VPTRIG_FLAG_PLAYED)
				++c.played;
			if(VPTrigIsFightEnd(r))
			{
				++c.fightEnds;
				if(r.fightSeq)
					endedFights.insert(r.fightSeq);
				if(r.owner)
					endedOwners.insert(r.owner);
				for(auto it = liveLoops.begin(); it != liveLoops.end();)
				{
					const VPTrigRecord& s = recs[it->second];
					if((s.fightSeq && s.fightSeq == r.fightSeq) || (s.owner && s.owner == r.owner))
					{
						++c.loopsByFightEnd;
						it = liveLoops.erase(it);
					}
					else
						++it;
				}
			}
			else
			{
				++c.clipEnds;
				const Inst k(r.player, r.unit, r.figure, r.instance);
				endedInst.insert(k);
				for(auto it = liveLoops.begin(); it != liveLoops.end();)
				{
					const VPTrigRecord& s = recs[it->second];
					if(Inst(s.player, s.unit, s.figure, s.instance) == k)
					{
						++c.loopsByClipEnd;
						it = liveLoops.erase(it);
					}
					else
						++it;
				}
			}
			continue;
		}
		// a record of an ended owner
		const bool afterFight = (r.fightSeq && endedFights.count(r.fightSeq)) || (r.owner && endedOwners.count(r.owner));
		const bool afterInst = r.instance && endedInst.count(Inst(r.player, r.unit, r.figure, r.instance));
		if((afterFight || afterInst) && (r.kind == VPTRIG_SOUND_START || r.kind == VPTRIG_EFFECT_START || r.kind == VPTRIG_EFFECT_MOVE))
		{
			++c.recordsAfterEnd;
			fault(c, "record " + std::to_string(r.seq) + " after its owner's end");
		}
		if(r.ec != 0)
		{
			const srveffects::ClipClass k = srveffects::classOfCode(r.ec);
			if(srveffects::ownerOfCode(r.ec) == srveffects::ClipOwner::Exe &&
			   !(movementSent && k == srveffects::ClipClass::Movement) && (r.kind == VPTRIG_SOUND_START || r.kind == VPTRIG_EFFECT_START))
			{
				++c.exeRecords;
				fault(c, "record " + std::to_string(r.seq) + " of an EXE-owned clip ec " + std::to_string(r.ec));
			}
		}
		if(r.kind == VPTRIG_SOUND_START || r.kind == VPTRIG_EFFECT_START)
		{
			liveHandles.insert(r.handle);
			if(r.kind == VPTRIG_SOUND_START && (r.flags & VPTRIG_FLAG_LOOP))
			{
				++c.loops;
				liveLoops[r.handle] = i;
			}
		}
		else if(r.kind == VPTRIG_SOUND_STOP || r.kind == VPTRIG_EFFECT_STOP || r.kind == VPTRIG_EFFECT_MOVE)
		{
			if(!liveHandles.count(r.handle))
			{
				++c.unknownHandle;
				fault(c, "record " + std::to_string(r.seq) + " names handle " + std::to_string(r.handle) + " not live");
				continue;
			}
			if(r.kind == VPTRIG_SOUND_STOP)
			{
				if(liveLoops.erase(r.handle))
					++c.loopsByStop;
				liveHandles.erase(r.handle);
			}
			if(r.kind == VPTRIG_EFFECT_STOP)
			{
				++c.effectsStopped;
				liveHandles.erase(r.handle);
			}
		}
	}
	c.loopsUnstopped = liveLoops.size();
	for(auto& l : liveLoops)
		fault(c, "loop handle " + std::to_string(l.first) + " (record " + std::to_string(recs[l.second].seq) + ") never stopped");
	return c;
}

inline std::string summary(const StreamCheck& c)
{
	char b[512];
	snprintf(b, sizeof b,
	         "SoundStart=%llu SoundStop=%llu EffectStart=%llu EffectMove=%llu EffectStop=%llu OwnerEnd=%llu (fight %llu, clip %llu) "
	         "loops=%llu (stop %llu, clip end %llu, fight end %llu, unstopped %llu) afterEnd=%llu exeRecords=%llu unknownHandle=%llu "
	         "order=%llu",
	         (unsigned long long)c.kinds[VPTRIG_SOUND_START], (unsigned long long)c.kinds[VPTRIG_SOUND_STOP],
	         (unsigned long long)c.kinds[VPTRIG_EFFECT_START], (unsigned long long)c.kinds[VPTRIG_EFFECT_MOVE],
	         (unsigned long long)c.kinds[VPTRIG_EFFECT_STOP], (unsigned long long)c.kinds[VPTRIG_OWNER_END],
	         (unsigned long long)c.fightEnds, (unsigned long long)c.clipEnds, (unsigned long long)c.loops,
	         (unsigned long long)c.loopsByStop, (unsigned long long)c.loopsByClipEnd, (unsigned long long)c.loopsByFightEnd,
	         (unsigned long long)c.loopsUnstopped, (unsigned long long)c.recordsAfterEnd, (unsigned long long)c.exeRecords,
	         (unsigned long long)c.unknownHandle, (unsigned long long)c.order);
	return b;
}

}  // namespace trigchecks
