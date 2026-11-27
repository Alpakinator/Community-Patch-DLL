// srv_trigger_logsink.cpp - the offline trigger log as a TriggerSink (ticket 4722; srv_trigger_logsink.h).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_trigger_logsink.h"

namespace srveffects {

bool LogSink::open(const std::string& path, std::string& why)
{
	c5r::Status s = w_.open(path);
	if(!s.ok())
	{
		why = s.str();
		return false;
	}
	names_ = 0;
	seq_ = 0;
	return true;
}

bool LogSink::close(std::string& why)
{
	c5r::Status s = w_.close();
	if(!s.ok())
	{
		why = s.str();
		return false;
	}
	return true;
}

uint32_t LogSink::name(uint32_t kind, const std::string& text)
{
	c5r::unitanim::TriggerName n;
	n.index = names_ + 1;
	n.kind = kind;
	n.text = text;
	c5r::Status s = w_.name(n);
	if(!s.ok())
	{
		if(first_.empty())
			first_ = s.str();
		++refused_;
		return 0;
	}
	return ++names_;
}

bool LogSink::record(const VPTrigRecord& r)
{
	VPTrigRecord c = r;
	c.seq = seq_ + 1;
	c5r::Status s = w_.record(c);
	if(!s.ok())
	{
		if(first_.empty())
			first_ = s.str() + " (record " + std::to_string(c.seq) + ")";
		++refused_;
		return false;
	}
	++seq_;
	return true;
}

}  // namespace srveffects
