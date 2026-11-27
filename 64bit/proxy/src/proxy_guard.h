// proxy_guard.h - the proxy's check of the server's window guard (issue 2703). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace ProxyGuard
{
// Called by the boot once the server has attached. Only when CIVBG_GUARD=1 is in this process' environment (the server
// inherits it and must then have installed its guard, server/src/srv_guard.cpp): reads Local\CivBgGuard_<serverPid>
// and returns false, with the reason in vpproxy.log, when the server has exited (its guard exit codes 0xC5F00030,
// 0xC5F00032, 0xC5F00033 are named), the section is missing or not complete (magic), guard_state (1276) != 1 or a
// thread's veto has failed (win_veto_failed, 1272). The boot then fails like every other boot failure
// (DllGetGameContext returns NULL). Returns true when the guard is complete or not requested.
bool CheckServerGuard(HANDLE hServer, unsigned long serverPid);
}
