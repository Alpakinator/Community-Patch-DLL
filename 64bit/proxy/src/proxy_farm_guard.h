// proxy_farm_guard.h - the farm builder guard on every 64-bit start (ticket 0535; D185 follow-up of 0527 and 0528). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The guard of ticket 0527 with 4855's rule 2 (shell/composite/farm_guard.{h,cpp}, compiled into the proxy as well;
// research/02-exe-internals/farm-builder-guard.md) is owned by the proxy, which is loaded on every 64-bit start, so a game
// without the composite (VP64Options.ini [Renderer] ServerRenderer = 0, or the composite refused by its SHA-256 check) is
// guarded too. One owner per process: the proxy attaches the guard's shared page before the composite's LoadLibrary, and the
// composite's copy then installs nothing (it hands its Presents to the proxy's guard; farm_guard.h, ticket 0535).
//
// Keys, names unchanged (0527, 4853): [composite] farm_guard (1 default, 0 = the builder's own listener word) and
// farm_builder (1 default) of shell_composite.ini in the proxy's folder, each overridden by VPSHELL_FARM_GUARD /
// VPSHELL_FARM_BUILDER in the EXE's environment, re-read every second. farm_builder = 0 (VPSHELL_FARM_BUILDER=0, set by
// proxy_shell.cpp with [Renderer] ServerFarmFields = 1) acts only while the composite is loaded, as before: without the
// composite the server draws no fields, and the builder is called through the guard.
// Ticket 0655: farm_end_post (1 default; VPSHELL_FARM_END_POST first, then [composite] of the same ini), the test switch of
// the farm-end post on the skipped-server path (farm_guard.h): 0 = no post, no farm building. It works on a shipped layout too.
// Ticket 0657: farm_edge (1 default; VPSHELL_FARM_EDGE first, then [composite]), the test switch of the edge placement
// (farm_guard.h): 0 = no pass-through on the farm manager's listeners, every farm building at its plot centre (as 0655).
// Ticket 0784 (D268): tread_filter (0 default; VPSHELL_TREAD_FILTER first, then [composite]); proxy_shell.cpp sets
// VPSHELL_TREAD_FILTER=1 from VP64Options.ini [Renderer] HideTreadEffect = 1 with ServerUnits = 1. It acts only while the
// composite is loaded (the server draws the units): the main Particle object's type-0x28 node gets the tread filter, which
// drops the EXE's two landship tread effects (farm_guard.h).
//
// Threads: Install and SetComposite on the proxy's boot thread (DllGetGameContext, the EXE's main thread at the front end);
// a watcher thread every 2 ms (high-resolution timer) runs FarmGuard_OnTick (the EXE check once, the swap under the EXE's
// registration lock, the burst lines); the guard's hook runs where the EXE calls it (the main thread's queue-B pump).
// OnUnload (DLL_PROCESS_DETACH, under the loader lock) stops the watcher outside its work and detaches the guard: the hook
// stub returns at once until the next proxy boot attaches again. Every line is in vpproxy.log ("farm_guard: ...").
#pragma once

namespace ProxyFarmGuard
{
// From ProxyShell::OnBoot (mode=on, before the composite's LoadLibrary): attaches the guard (the shared page), reads the
// keys, starts the watcher. `outDir`: the folder whose VP64\run\<..>-<pid>-<n> holds this process's run folder (the record
// farm-guard.log and farm-builder-calls.log go there; <user>\Logs on the shipped layout; NULL or "" = none known).
// `iniPath`: shell_composite.ini beside the proxy (it may be missing: the built-in defaults). A second call does nothing.
void Install(const wchar_t* outDir, const wchar_t* iniPath);
// From ProxyShell::OnBoot after the composite's load (or without one): farm_builder = 0 acts only with the composite.
void SetComposite(bool loaded);
// From ProxyShell::OnUnload (under the loader lock). Logs nothing.
void OnUnload();

// ---- for the offline tests (proxy/tests/farm_guard_proxy_test.cpp); the product calls none of these -----------------------
namespace Test
{
// Install against a fake world (farm_guard.h FgWorld; no EXE check): the same attach, keys and watcher as Install.
void InstallWorld(const void* fgWorld, const wchar_t* recordDir, const wchar_t* iniPath);
// The keys in force: farm_guard, farm_builder as read, and whether the builder is skipped (farm_builder = 0 and composite).
void Keys(int* farmGuard, int* farmBuilder, int* skip);
// Ticket 0655: farm_end_post as read (1 = the farm-end post is made on the skipped-server path).
int FarmEndPost();
// Ticket 0657: farm_edge as read (1 = the edge pass-throughs are made while the builder is skipped).
int FarmEdge();
// Ticket 0784: the tread filter in force (tread_filter = 1 and the composite loaded).
int TreadFilter();
// Watcher ticks so far.
unsigned long Ticks();
// Stops the watcher and detaches, as at an unload; the next Install starts again (as a new boot of the proxy).
void Unload();
// The owner's EXE check on an image at `image` (a fake header in the tests): 1 = a known build (the guard may look at
// it; its own symbols and patterns decide the rest), -1 = not a known build (nothing is read or written).
int ValidateImage(const void* image);
}  // namespace Test
}  // namespace ProxyFarmGuard
