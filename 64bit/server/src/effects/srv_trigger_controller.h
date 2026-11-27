// srv_trigger_controller.h - the controller's crossed triggers (c5r::unitanim::TriggerEvent, controller.h) as the
// emitter's input (srv_trigger_emitter.h; epic 0042, ticket 4722). Header only.
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>

#include "c5r/unitanim/controller.h"
#include "srv_trigger_emitter.h"

namespace srveffects {

inline TrigType trigTypeOf(c5m::TriggerType t)
{
	switch(t)
	{
	case c5m::TriggerType::Sound: return TrigType::Sound;
	case c5m::TriggerType::StopSound: return TrigType::StopSound;
	case c5m::TriggerType::Effect: return TrigType::Effect;
	case c5m::TriggerType::Transfer: return TrigType::Transfer;
	case c5m::TriggerType::Action: return TrigType::Action;
	default: return TrigType::Unknown;
	}
}

inline TriggerIn triggerInOf(const c5r::unitanim::TriggerEvent& e)
{
	TriggerIn t;
	t.player = e.player;
	t.unit = e.id;
	t.figure = (int32_t)e.figure;
	t.instance = e.instance;
	t.type = trigTypeOf(e.type);
	t.ec = e.ec;
	t.trigger = e.trigger;
	t.clipTime = e.time;
	t.offset = e.offset;
	t.duration = e.duration;
	t.event = e.event;
	t.bone = e.bone;
	t.file = e.file;
	t.tier = e.tier;                   // 4737
	t.unitFigures = e.unit_figures;    // 4737
	return t;
}

// The frame's triggers of the units the emitter takes (a running fight or a victory tail; the others would only be
// counted noFight: this keeps the copy small).
inline void triggersOf(const std::vector<c5r::unitanim::TriggerEvent>& in, const TriggerEmitter& em, std::vector<TriggerIn>& out)
{
	out.clear();
	for(const auto& e : in)
		if(em.wants(e.player, e.id))
			out.push_back(triggerInOf(e));
}

}  // namespace srveffects
