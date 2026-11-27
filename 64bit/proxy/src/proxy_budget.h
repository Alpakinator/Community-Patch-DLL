// proxy_budget.h - the proxy's side of the turn budget (ticket 4652, epic 0437 W4a). Off unless VP64_TURN_BUDGET is
// "1" in the EXE's environment at the proxy's boot (vp_budget.h); off, every hook is one test of a bool and nothing is
// created or written. On: the section Local\VPBudget_<EXE pid> (server.exe publishes its turn boundaries there) and
// budget-proxy.tsv beside vpproxy.log: per turn the EXE's time with no call to the server in flight (exe_alone), and per
// call class the calls each way with the time the proxy saw (proxy_budget_core.h).
#pragma once
#include <stdint.h>
#include <string>

namespace ProxyBudget
{
extern bool g_bOn;
inline bool On() { return g_bOn; }

// Boot, before the server starts: reads the switch; on, creates the section and opens budget-proxy.tsv in `dir`.
// Returns a line for vpproxy.log ("" when off).
std::string Init(uint32_t uPid, const std::string& dir);
int64_t OutBegin();                            // ProxyRt::End: before the CALL is sent; returns its clock value
void OutEnd(uint32_t method, int64_t t0);      // ... after the RETURN (Finish) on every path
void Served(uint32_t method, int64_t t0);      // OnCall: the end of a call the DLL made into the EXE (t0: its start)
std::string Final();                           // WriteStats: every remaining turn written, file closed; a log line
}  // namespace ProxyBudget
