// srv_budget.h - the turn budget of server.exe's remote mode (ticket 4652, epic 0437 W4a). Off unless the environment
// variable VP64_TURN_BUDGET is "1" (vp_budget.h); off, every hook is one test of a bool and nothing is written.
// On: the run folder gets budget.tsv (per turn, the rows that partition the turn's wall time, and per call class the
// calls and their inclusive time) and turn-times.tsv (the columns of direct mode), and the proxy's shared turn block
// gets every turn boundary. Accounting: srv_budget_core.h. Definitions: research/08-performance/turn-time-study.md
// section "The turn budget (W4a)".
#pragma once
#include <stdint.h>

namespace SrvBudget
{
extern bool g_bOn;
inline bool On() { return g_bOn; }

void Init();                         // RemoteMain after SrvRemote::Init, before the service threads start
void OpenBlock(uint32_t uProxyPid);  // kRt_Boot: the proxy's Local\VPBudget_<pid>
void ServedEnter(uint32_t method);   // SrvRemote::OnCall, first thing
void ServedLeave();                  // SrvRemote::OnCall, last thing (also on unwinding)
void OutEnter(uint32_t method);      // SrvRt::End, just before end_call
void OutLeave();                     // SrvRt::End, just after end_call
void CountUpdate();                  // SrvRt::EnterUpdate, outermost level
void AtTurn(int iTurn);              // SrvRt::LeaveUpdate: the game turn after an Update; a change is a boundary
void Final(const char* szWhy);       // kRt_Shutdown, WriteReport: the last, partial turn; files closed
}  // namespace SrvBudget
