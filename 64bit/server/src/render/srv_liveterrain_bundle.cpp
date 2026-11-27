// srv_liveterrain_bundle.cpp - live terrain in the scene bundle (epic 0465, ticket 4907; rule A inside format 9, the
// pattern of ticket 4852's farm producer: render_config keys, no new member). Written 2026-11-19 by w5-4907 for this
// repository (GPL-3.0-or-later).
//
//   * SceneRecorder::noteLiveTerrainFrame (called from beforeDraw for the span's frames): the field generation the scene
//     draws, one entry per change (the span's first frame always);
//   * SceneRecorder::takeLiveTerrain (called from doFinish before render_config is copied into the manifest):
//     render_live_terrain "0" / "1" (live terrain on at the span's first frame) and with "1" the journal of the span's load
//     (liveterrain::EncodeSpanText: the producer's inputs, every job as a table diff, every generation with its box and
//     SHA-256, the frame list, the feature names), or a version note kLiveTerrainNote when it cannot be taken;
//   * CheckLiveTerrainConfig: the checker's rules for those keys (srv_scene_bundle.cpp calls it beside the farm's).
// The player (server/tools/sceneshot) calls Scene::liveTerrainReplayInit / liveTerrainReplayTo (srv_liveterrain_scene.cpp).
#include "srv_scene_recorder.h"
#include "srv_liveterrain.h"
#include "srv_liveterrain_features.h"

namespace srvrender {

void SceneRecorder::noteLiveTerrainFrame(int64_t frameId)
{
	const liveterrain::LiveTerrain* lt = m_liveGet ? m_liveGet() : nullptr;
	if(!m_liveSpanNoted)
	{
		m_liveSpanNoted = true;
		m_liveOnAtSpan = lt != nullptr;
		m_liveLoadAtSpan = lt ? lt->loadSeq() : 0;
	}
	if(!lt || !m_liveOnAtSpan)
		return;
	const uint32_t g = lt->counters().fieldGeneration;
	if(m_liveFrames.empty() || m_liveFrames.back().gen != g)
	{
		liveterrain::FrameGen f;
		f.frameId = frameId;
		f.gen = g;
		m_liveFrames.push_back(f);
	}
}

void SceneRecorder::takeLiveTerrain()
{
	if(!m_liveSpanNoted)
		return;   // the span never started: no frame, nothing to say
	setRenderConfig("render_live_terrain", m_liveOnAtSpan ? "1" : "0");
	if(!m_liveOnAtSpan)
		return;
	const liveterrain::LiveTerrain* lt = m_liveGet ? m_liveGet() : nullptr;
	std::string why;
	if(!lt)
		why = "the live terrain hook is gone";
	else if(!lt->journal().open)
		why = "the span's load has no producer (its inputs could not be had: render.log)";
	else if(lt->loadSeq() != m_liveLoadAtSpan || lt->journal().loadSeq != m_liveLoadAtSpan)
		why = "a new load inside the span";
	if(!why.empty())
	{
		m_notes.push_back(std::string(liveterrain::kLiveTerrainNote) + ": " + why + ": the player draws the load's field only");
		RNote("recorder: %s", m_notes.back().c_str());
		return;
	}
	const std::string text = liveterrain::EncodeSpanText(lt->journal(), m_liveFrames, LiveTerrainFeatureType);
	setRenderConfig("render_live_terrain_journal", text);
	RNote("recorder: live terrain journal: load seq %u, %zu jobs, %zu generations, %zu frame entries, %zu bytes of text",
	      lt->journal().loadSeq, lt->journal().jobs.size(), lt->journal().generations.size(), m_liveFrames.size(), text.size());
}

}  // namespace srvrender
