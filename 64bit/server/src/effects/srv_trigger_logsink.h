// srv_trigger_logsink.h - the offline trigger log as a TriggerSink (epic 0042, ticket 4722; D137): every name and
// record the emitter writes, through 4721's writer (render/include/c5r/unitanim/trigger_log.h). Logs carry names from
// the player's install: they stay outside git (D83).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

#include "c5r/unitanim/trigger_log.h"
#include "srv_trigger_emitter.h"

namespace srveffects {

class LogSink : public TriggerSink
{
public:
	// False with why when the file cannot be opened.
	bool open(const std::string& path, std::string& why);
	bool close(std::string& why);
	bool isOpen() const { return w_.isOpen(); }
	uint32_t name(uint32_t kind, const std::string& text) override;
	// The record with the log's own seq (1, 2, ... in write order); false when the writer refused it (counted).
	bool record(const VPTrigRecord& r) override;
	uint64_t refused() const { return refused_; }
	const std::string& firstRefusal() const { return first_; }
	uint64_t records() const { return w_.records(); }

private:
	c5r::unitanim::TriggerLogWriter w_;
	uint32_t names_ = 0;
	uint64_t seq_ = 0;
	uint64_t refused_ = 0;
	std::string first_;
};

}  // namespace srveffects
