// FAutoArchive, FAutoVariableBase and FSerialization::toString (basic types): real replacements.
// x86 evidence: FireWorksWin32.obj members FAutoArchive.obj, FAutoVariableBase.obj, disassembled
// 2026-11-10. FAutoArchive is the out-of-sync (OOS) checking archive of CvCity/CvPlayer/CvPlot/CvUnit;
// nothing here writes save-game data.
#include "fw_prelude.h"
#include "CvString.h"
#include <FireWorks/FAutoArchive.h>
#include <FireWorks/FAutoVariableBase.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------
// FAutoArchive

namespace
{
	bool ms_CallStacksEnabled = false;
}

FAutoArchive::FAutoArchive()
	: m_contents()
	, m_deltas()
{
}

FAutoArchive::~FAutoArchive()
{
}

void FAutoArchive::EnableCallStacks(bool bState)
{
	ms_CallStacksEnabled = bState;
}

bool FAutoArchive::AreCallStacksEnabled()
{
	return ms_CallStacksEnabled;
}

void FAutoArchive::add(FAutoVariableBase& kVariable)
{
	m_contents.push_back(&kVariable);
}

void FAutoArchive::clearDelta()
{
	for(std::set<FAutoVariableBase*>::const_iterator it = m_deltas.begin(); it != m_deltas.end(); ++it)
		(*it)->clearDelta();
	m_deltas.clear();
}

void FAutoArchive::reset()
{
	for(std::vector<FAutoVariableBase*>::const_iterator it = m_contents.begin(); it != m_contents.end(); ++it)
		(*it)->reset();
}

bool FAutoArchive::hasDeltas() const
{
	return !m_deltas.empty();
}

void FAutoArchive::saveDelta(FDataStream& saveTo, std::vector<std::pair<std::string, std::string> >& /*callStacks*/) const
{
	// x86 format: 4-byte count of changed variables; per variable (in m_deltas order, i.e. by
	// pointer value) its 4-byte index in m_contents (omitted if not found), then its own saveDelta.
	saveTo << static_cast<unsigned int>(m_deltas.size());
	for(std::set<FAutoVariableBase*>::const_iterator it = m_deltas.begin(); it != m_deltas.end(); ++it)
	{
		std::vector<FAutoVariableBase*>::const_iterator found = std::find(m_contents.begin(), m_contents.end(), *it);
		if(found != m_contents.end())
			saveTo << static_cast<unsigned int>(found - m_contents.begin());
		(*it)->saveDelta(saveTo);
	}
	m_deltas.clear();
}

// ---------------------------------------------------------------------------------------------
// FAutoVariableBase

FAutoVariableBase::FAutoVariableBase(const std::string& name, FAutoArchive& owner)
#if !defined(FINAL_RELEASE) || defined(VPDEBUG)
	: m_lastCallStackToChangeThisVariable()
	, m_callStackRemark()
	, m_callStackTracking(false)
#endif
{
	owner.add(*this);
	owner.setVariableName(*this, name);
}

FAutoVariableBase::~FAutoVariableBase()
{
}

std::string FAutoVariableBase::debugDump(const std::vector<std::pair<std::string, std::string> >&) const
{
	return std::string("Variable Out Of Sync : ") + name();
}

// ---------------------------------------------------------------------------------------------
// FSerialization::toString, debug text for OOS dumps. The x86 format strings are reproduced,
// including their slips: "%ud" for unsigned int (number followed by a literal 'd'), "%uhh" for
// unsigned char (number followed by "hh") and "%hh" for char (an unterminated conversion; VC9's
// sprintf stops there, so the x86 result is an empty string [I]). The modern CRT would reject the
// last two as invalid formats, so they are built without sprintf.

namespace FSerialization
{
	std::string toString(const bool& source)
	{
		static const std::string trueString("true");
		static const std::string falseString("false");
		return source ? trueString : falseString;
	}

	std::string toString(const char& /*source*/)
	{
		return std::string();
	}

	std::string toString(const unsigned char& source)
	{
		char szBuffer[16];
		sprintf_s(szBuffer, "%u", static_cast<unsigned int>(source));
		return std::string(szBuffer) + "hh";
	}

	std::string toString(const int& source)
	{
		char szBuffer[16];
		sprintf_s(szBuffer, "%d", source);
		return std::string(szBuffer);
	}

	std::string toString(const unsigned int& source)
	{
		char szBuffer[16];
		sprintf_s(szBuffer, "%ud", source);
		return std::string(szBuffer);
	}
}
