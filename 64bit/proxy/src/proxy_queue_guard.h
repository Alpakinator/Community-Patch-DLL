// proxy_queue_guard.h - the guard of the EXE's 4 MB UI message queue on the product route (ticket 0492). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The EXE passes engine-to-UI records (decals, redraw rectangles, ...) through a double-buffered queue in its .data
// section: two channels of two 4 MB buffers, a flip counter and a high-water mark at fixed RVAs. A writer reserves room
// with KERNEL32!InterlockedExchangeAdd(&buffer.size, recordSize) through the EXE's import slot and writes the record at
// buffer + 0x80 + old size, with no capacity check. During a long load the main thread does not swap for minutes and a
// buffer overruns: the record past the end rewrites the flip counter (research/02-exe-internals/
// message-queue-and-fixed-buffers.md section 1). The product route overruns it from the same saves as stock
// (research/03-dll-engine-boundary/ui-queue-guard-product.md section 3, ticket 0459).
//
// The guard points that import slot at a thunk. For the four size fields it reserves with compare-and-swap and, when
// the record would not fit, returns an offset that puts the record into a 64 KB scratch area and leaves the size alone,
// so the dispatcher never reaches it (the record is dropped: a stale farm field or route curve, D66, bounded). Every
// other caller is forwarded to the slot's previous value. Only data is written (the import slot); no EXE code byte is
// read or written (D4, D83: the checks are pattern-free, MC decision 2026-11-17).
//
// Checks before the write (each refusal is one line in vpproxy.log, LOUD on the known EXE, and the guard stays off):
// a build the patch knows (PE time stamp and SizeOfImage, exebuild/include/vp_exe_build.h) whose table carries the three
// symbols (ticket 4963), the import slot of KERNEL32.dll!InterlockedExchangeAdd found by name in the import directory and
// equal to the table's qg_iat_interlockedexchangeadd, the channel index
// 0 or 1, both channels' size fields readable and at most the capacity, the flip counters readable, the slot bound and
// not already this guard's thunk.
//
// Lifetime: installed from ProxyShell::OnBoot (the mode=on boot, before the front end loads any save). Ticket 0635:
// also with the patch switched off (BootOff in proxy_rt.cpp: mode=off, failed=1 and the pack fallback, where the
// player's own 32-bit Vox Populi DLL answers), by InstallWhenOff after that DLL's first DllGetGameContext, unless
// VP64Options.ini says [Proxy] GuardsWhenOff = 0. There the proxy never hooks on top of another hook: when the
// slot no longer holds KERNEL32's own function (the -ia test DLL brings its own guard, research
// 03-dll-engine-boundary/ui-queue-guard-product.md section 8), the guard stays off and says which module holds it. At an unload (FreeLibrary: the EXE unloads the game DLL at a mod-set change, and the proxy may boot again,
// issue 3308) the slot is given its previous value back if it still holds the thunk. If another module has hooked the
// slot on top of the thunk (seen by OnUpdate), the proxy pins its own image so that the thunk can never be unmapped
// (LOUD). The scratch area is never freed (a late writer may still hold a pointer into it).
//
// Drops: counted by the thunk (no logging there: it runs on any thread, inside the EXE's writers); OnUpdate logs one
// line per burst once two seconds have passed without a drop, and a LOUD line when a record larger than the scratch
// area had to be passed through unguarded. The counters are in vpproxy.log's end lines and the error report
// (StatusLine). Drops raise no popup (MC decision 2026-11-17: log and report only, D118 not raised for a bounded loss).
#pragma once
#include <stddef.h>

namespace ProxyQueueGuard
{
// From ProxyShell::OnBoot (the boot thread). Checks the EXE (GetModuleHandle(NULL)) and writes the slot; one log line.
// A second call in one image does nothing.
void Install();
// Ticket 0635: from BootOff (the patch switched off or the pack fallback), after the 32-bit DLL's first
// DllGetGameContext. As Install, but refused, without LOUD, when the slot no longer holds KERNEL32's own
// InterlockedExchangeAdd: another module hooked it first (the -ia test DLL's own guard), and the proxy does not hook
// on top of it. A second call in one image does nothing (also after Install).
void InstallWhenOff();
// From ProxyShell::OnUpdate (the game-core thread, top of each ICvGame1::Update), and in mode=off from the forwarded
// DllGetGameContext calls on the boot thread (ticket 0635; there is no Update hook then). Cheap when nothing happened.
void OnUpdate();
// From ProxyShell::OnUnload (DLL_PROCESS_DETACH by FreeLibrary, under the loader lock). Logs nothing.
void OnUnload();
// One line for vpproxy.log's end lines and the error report: "UI queue guard: <status>; dropped ...".
void StatusLine(char* out, size_t cap);

// ---- for the offline tests (proxy/tools/rt_tests/queue_guard_unit.cpp); the product calls none of these ------------
namespace Test
{
typedef long(__stdcall* ExchangeAddFn)(long volatile* addend, long value);
// Install against the image at `base` (a fake image in the tests); `bound` is what the slot is expected to hold when
// nobody hooked it (the product: GetProcAddress(kernel32, "InterlockedExchangeAdd")). NULL = installed, else the
// reason it was refused (the same text as the log line).
const char* InstallAt(void* base, ExchangeAddFn bound);
// Ticket 0635: InstallWhenOff against the image at `base`: refused when the slot does not hold `bound`.
const char* InstallWhenOffAt(void* base, ExchangeAddFn bound);
// One OnUpdate step with `tick` as the time (GetTickCount's clock).
void UpdateAt(unsigned long tick);
// Gives the slot back as at an unload and forgets everything (counters zeroed; the scratch area is kept).
void Reset();
bool Installed();
bool Pinned();
const char* Status();
long Dropped();
long DroppedBytes();
long Largest();
long TooLarge();
long Bursts();
// The thunk's own address (what the slot holds once installed) and the scratch area.
const void* Thunk();
const void* Scratch();
// The layout of a build (kVpExeBuild*; ticket 4963: from the build table), so the tests build their fake image from the
// same numbers; 0 when the build does not carry the symbol.
unsigned long RvaChannelIndex(int build);
unsigned long RvaChannels(int build);
unsigned long RvaSlot(int build);
// Makes the table report the catalogue symbol `name` as not carried (NULL: none); proxy_exe.h's test hook.
void DropSymbol(const char* name);
unsigned long ChannelStride();
unsigned long BufferStride();
unsigned long Capacity();
long ScratchBytes();
}  // namespace Test
}  // namespace ProxyQueueGuard
