// proxy_pacing.cpp - D165 option 3: a shorter update tick in AI turns (ticket 4662). See proxy_pacing.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Written from the knowledge base's description of the EXE's behaviour (research/02-exe-internals/game-core-thread.md
// section 6, research/08-performance/d165-pacing-fix-design.md sections 3-7; decision D83): no EXE code byte is read,
// compared or written. Read: the PE header, the vftable's entries (.rdata), the app global, the AI-turn byte and the
// hold (.data), the engine utility object's vptr (heap). Written: that vptr (one aligned interlocked compare-exchange).
// Toolchain: clang-cl -m32, VC9 headers (the proxy's); offline test proxy/tools/rt_tests/pacing_unit.cpp, the same and
// MSVC /fsanitize=address, with VP_PACING_OFFLINE defined (no engine glue). x86 only (the vftable holds 32-bit pointers
// and the replacement is a 32-bit __thiscall stand-in).
#ifndef VP_PACING_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way
#include "proxy_ifaces.gen.h"   // ICvGame1, ICvPlayer1 (option 2's scan, ticket 4665)
#include "proxy_rt_logic.h"     // ProxyLogic::Settings
#include "proxy_local.h"        // ProxyIniSettings (proxy_update.cpp)
#include "proxy_report.h"
#include "proxy_session.h"
#include "vp64_options.h"
#include <string>
#include <vector>
#include <tlhelp32.h>
#endif
#include "proxy_pacing.h"
#include "proxy_exe.h"  // ticket 4963: the build table
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__clang__)
#define VP_PACING_RETADDR() __builtin_return_address(0)
#else
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define VP_PACING_RETADDR() _ReturnAddress()
#endif

namespace ProxyPacing
{
// ---- the layout of the running build (ticket 4963) -----------------------------------------------------------------
namespace
{
ExeRvas g_rvas = {0, 0, 0, 0, 0, 0, 0, 0, 0};
int g_layoutBuild = kVpExeBuildUnknown;
const char* g_layoutMissing = "";
}  // namespace

const ExeRvas& Rvas()
{
	return g_rvas;
}

int SelectLayout(int build, const char** missing)
{
	// The layout is replaced only by a complete one: a refused image leaves the last good layout (every path refuses
	// on that image anyway; nothing reads Rvas() through a refused check).
	ExeRvas l = {0, 0, 0, 0, 0, 0, 0, 0, 0};
	g_layoutBuild = build;
	if (missing) *missing = "";
	if (VpExeBuildInfoOf(build) == NULL) return 2;
	const struct
	{
		DWORD* field;
		int sym;
	} m[] = {{&l.appGlobal, VPX_cam_app},           {&l.utilVftable, VPX_pac_util_vftable},
	         {&l.secsPerTurn, VPX_pac_secs_per_turn}, {&l.frameSite, VPX_pac_frame_site},
	         {&l.aiTurnByte, VPX_pac_ai_turn_byte},   {&l.hold, VPX_pac_hold},
	         {&l.gvsVftable, VPX_ses_gvs_vtbl},       {&l.qbIndex, VPX_pac_qb_index},
	         {&l.qbBuf, VPX_pac_qb_buf}};
	for (size_t i = 0; i < sizeof(m) / sizeof(m[0]); ++i)
	{
		*m[i].field = ProxyExe::Rva(build, m[i].sym);
		if (*m[i].field == 0)
		{
			g_layoutMissing = VpExeSymName(m[i].sym);
			if (missing) *missing = g_layoutMissing;
			return 7;
		}
	}
	g_rvas = l;
	return 0;
}

namespace
{
// getSecsPerTurn is a __thiscall method without arguments returning a float (x87 ST0). __fastcall with two register
// arguments (ECX = this, EDX unused) and no stack arguments has the same calling sequence and cleanup.
typedef float(__fastcall* SecsFn)(void* self, void* unusedEdx);

// The copy: a header that names it (so a later proxy load can recognise a copy an earlier load left behind) and the
// table: table[0] is the entry before slot 0 (the RTTI slot), table[1 + i] is slot i. The vptr written is &table[1].
const char kMagic[8] = {'V', 'P', '6', '4', 'T', 'I', 'C', 'K'};
struct CopyPage
{
	char magic[8];
	DWORD origVptr;  // the EXE's vftable (rebased)
	DWORD origSlot;  // the EXE's slot 102 (rebased)
	DWORD table[1 + kEntries];
};
const DWORD kHeaderBytes = 16;  // magic, origVptr, origSlot: the bytes before table[0]

LogFn g_log = NULL;
ReportFn g_report = NULL;
State g_state = kPending;
bool g_decided = false;
int g_tickMs = -1;
ULONG_PTR g_object = 0;  // the object whose vptr we wrote
ULONG_PTR g_base = 0;
DWORD g_origVptr = 0;
CopyPage* g_page = NULL;  // this proxy load's copy (left mapped at unload, never freed)
volatile LONG g_installs = 0, g_checks = 0;

// ---- read by the replacement (written before the vptr swap; the interlocked swap is a full barrier) -------------
ULONG_PTR g_site = 0;
DWORD g_mainTid = 0;
const volatile BYTE* g_aiByte = NULL;
const volatile LONG* g_hold = NULL;
SecsFn g_orig = NULL;
float g_tickSecs = 0.0f;
volatile LONG g_siteCalls = 0, g_shortened = 0, g_siteStock = 0, g_siteOther = 0, g_otherCallers = 0;
volatile ULONG_PTR g_lastRa = 0;
DWORD g_imageSize = 0;  // the EXE's SizeOfImage (for naming return addresses in the counters line)
// The other callers by return address (the first kRaSlots distinct ones; later ones only in g_otherCallers).
const int kRaSlots = 6;
volatile LONG g_raAddr[kRaSlots];
volatile LONG g_raCount[kRaSlots];

void NoteOtherCaller(ULONG_PTR ra)
{
	for (int i = 0; i < kRaSlots; ++i)
	{
		const LONG a = g_raAddr[i];
		if (a == static_cast<LONG>(ra) ||
		    (a == 0 && (InterlockedCompareExchange(&g_raAddr[i], static_cast<LONG>(ra), 0) == 0 ||
		                g_raAddr[i] == static_cast<LONG>(ra))))
		{
			InterlockedIncrement(&g_raCount[i]);
			return;
		}
	}
}

// The replacement of slot 102. Runs on whatever thread calls the method; no lock, no allocation, no logging.
float __fastcall SecsPerTurn(void* self, void* unusedEdx)
{
	const ULONG_PTR ra = reinterpret_cast<ULONG_PTR>(VP_PACING_RETADDR());
	g_lastRa = ra;
	if (ra == g_site)
	{
		if (GetCurrentThreadId() == g_mainTid)
		{
			++g_siteCalls;  // only the main thread counts these: no interlocked operation per frame
			if (*g_aiByte == 1 && *g_hold == 0)
			{
				++g_shortened;
				return g_tickSecs;
			}
			++g_siteStock;
		}
		else
			InterlockedIncrement(&g_siteOther);
	}
	else
	{
		InterlockedIncrement(&g_otherCallers);
		NoteOtherCaller(ra);
	}
	return g_orig(self, unusedEdx);
}

void Log(bool loud, const char* fmt, ...)
{
	if (!g_log) return;
	char line[900];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	g_log(line, loud);
}

// ---- guarded access ------------------------------------------------------------------------------------------------
// The access itself is in a function of its own, called inside __try: clang-cl's x86 SEH catches a fault only at a
// call inside the __try body, not in an instruction of the body itself (found by pacing_unit's decommitted-vftable case,
// 2026-11-17: the plain build crashed where MSVC's caught it; proxy_leaderscene.cpp's WalkRaw is called the same way).
__declspec(noinline) void CopyDwordsRaw(ULONG_PTR at, DWORD* out, int n)
{
	for (int i = 0; i < n; ++i) out[i] = reinterpret_cast<const volatile DWORD*>(at)[i];
}

__declspec(noinline) void CopyBytesRaw(ULONG_PTR at, BYTE* out, DWORD n)
{
	for (DWORD i = 0; i < n; ++i) out[i] = reinterpret_cast<const volatile BYTE*>(at)[i];
}

__declspec(noinline) DWORD SwapRaw(ULONG_PTR object, DWORD want, DWORD expected)
{
	return static_cast<DWORD>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object), static_cast<LONG>(want),
	                                                     static_cast<LONG>(expected)));
}

bool ReadDwords(ULONG_PTR at, DWORD* out, int n)
{
	__try
	{
		CopyDwordsRaw(at, out, n);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

bool ReadBytes(ULONG_PTR at, void* out, DWORD n)
{
	__try
	{
		CopyBytesRaw(at, static_cast<BYTE*>(out), n);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// The value found before the exchange in *seen; false on an access fault.
bool SwapVptr(ULONG_PTR object, DWORD want, DWORD expected, DWORD* seen)
{
	__try
	{
		*seen = SwapRaw(object, want, expected);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// ---- the image ---------------------------------------------------------------------------------------------------
struct Image
{
	DWORD size;
	DWORD textLo, textHi;  // RVA range of the executable section holding getSecsPerTurn
};

enum
{
	kSecExec = 1,
	kSecWrite = 2,
	kSecRead = 4
};

// 0 = passed; else a code for VerifyExe's reason. Not inlined: VerifyGuarded calls it inside __try (see above).
__declspec(noinline) int VerifyRaw(ULONG_PTR base, Image* img)
{
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return 1;
	const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return 1;
	// ticket 4963: the build from the registry (stamp and SizeOfImage), its layout from the table (2 or 7 refuse)
	const int lay = SelectLayout(VpExeBuildFromHeader(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage), NULL);
	if (lay != 0) return lay;
	img->size = nt->OptionalHeader.SizeOfImage;
	if (img->size < Rvas().hold + 4 || img->size < Rvas().utilVftable + 4 * (kEntries + 1)) return 3;
	const unsigned n = nt->FileHeader.NumberOfSections;
	if (n == 0 || n > 96) return 1;
	const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	// For each address: the section holding [rva, rva + len) and its access.
	const struct
	{
		DWORD rva, len;
		int need, refuse, code;
	} want[] = {
		{Rvas().secsPerTurn, 1, kSecExec, 0, 4},
		{Rvas().frameSite, 1, kSecExec, 0, 4},
		{Rvas().utilVftable - 4, 4 * (kEntries + 2), kSecRead, kSecExec | kSecWrite, 5},
		{Rvas().appGlobal, 4, kSecRead | kSecWrite, kSecExec, 6},
		{Rvas().aiTurnByte, 1, kSecRead | kSecWrite, kSecExec, 6},
		{Rvas().hold, 4, kSecRead | kSecWrite, kSecExec, 6},
	};
	for (size_t w = 0; w < sizeof(want) / sizeof(want[0]); ++w)
	{
		bool found = false;
		for (unsigned i = 0; i < n && !found; ++i)
		{
			const DWORD va = sec[i].VirtualAddress;
			const DWORD len = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
			if (want[w].rva < va || want[w].rva + want[w].len > va + len) continue;
			found = true;
			const DWORD c = sec[i].Characteristics;
			const int acc = ((c & IMAGE_SCN_MEM_EXECUTE) ? kSecExec : 0) | ((c & IMAGE_SCN_MEM_WRITE) ? kSecWrite : 0) |
			                ((c & IMAGE_SCN_MEM_READ) ? kSecRead : 0);
			if ((acc & want[w].need) != want[w].need || (acc & want[w].refuse) != 0) return want[w].code;
			if (w == 0)
			{
				img->textLo = va;
				img->textHi = va + len;
			}
		}
		if (!found) return want[w].code;
	}
	if (Rvas().frameSite < img->textLo || Rvas().frameSite >= img->textHi) return 4;
	return 0;
}

int VerifyGuarded(ULONG_PTR base, Image* img)
{
	if (base < 0x10000) return 1;
	__try
	{
		return VerifyRaw(base, img);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return 1;
	}
}

// Ticket 4963: the header only: the build and its layout (0, 1 no image, 2 unknown build, 7 a symbol not carried).
__declspec(noinline) int LayoutRaw(ULONG_PTR base)
{
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return 1;
	const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return 1;
	return SelectLayout(VpExeBuildFromHeader(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage), NULL);
}

int LayoutGuarded(ULONG_PTR base)
{
	if (base < 0x10000) return 1;
	__try
	{
		return LayoutRaw(base);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return 1;
	}
}

const char* VerifyReason(int rc)
{
	static char s_why[200];
	switch (rc)
	{
	case 0: return NULL;
	case 2: return "not an EXE build the patch knows (PE TimeDateStamp and SizeOfImage)";
	case 7: return ProxyExe::NotCarried(s_why, sizeof(s_why), "", g_layoutBuild, VpExeSymByName(g_layoutMissing, -1));
	case 3: return "the EXE image (SizeOfImage) is smaller than the layout needs";
	case 4: return "getSecsPerTurn or the frame's call site is not in the EXE's executable section";
	case 5: return "the engine utility vftable is not in a read-only data section of the EXE";
	case 6: return "g_Civ5App, the AI-turn byte or the hold is not in a writable data section of the EXE";
	default: return "no readable i386 EXE image at the module base";
	}
}

bool InText(const Image& img, DWORD value)
{
	return value >= g_base + img.textLo && value < g_base + img.textHi;
}

// ---- the copy's slot 102 ----------------------------------------------------------------------------------------
void SetCopySlot(CopyPage* page, DWORD value)
{
	if (!page) return;
	DWORD old = 0;
	if (!VirtualProtect(page, sizeof(CopyPage), PAGE_READWRITE, &old)) return;
	page->table[1 + kSlotSecsPerTurn] = value;
	VirtualProtect(page, sizeof(CopyPage), PAGE_READONLY, &old);
}

DWORD CopyVptrOf(const CopyPage* page)
{
	return page ? static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(&page->table[1])) : 0;
}

// The object's vptr back to the EXE's vftable when it is still this load's copy. True when written.
bool PutBack(ULONG_PTR object)
{
	if (!object || !g_page || !g_origVptr) return false;
	DWORD seen = 0;
	return SwapVptr(object, g_origVptr, CopyVptrOf(g_page), &seen) && seen == CopyVptrOf(g_page);
}

// ---- the install --------------------------------------------------------------------------------------------------
char g_check[40];
char g_why[400];

bool Refuse(const char* check, const char* fmt, ...)
{
	_snprintf_s(g_check, sizeof(g_check), _TRUNCATE, "%s", check);
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(g_why, sizeof(g_why), _TRUNCATE, fmt, ap);
	va_end(ap);
	return false;
}

// A copy that an earlier proxy load left on the object (issue 3308: a second boot in one process when the earlier
// unload did not restore): its header names the same EXE vftable. Restored here; true when the vptr is the EXE's now.
bool TakeBackOldCopy(ULONG_PTR object, DWORD vptr, DWORD orig)
{
	BYTE head[kHeaderBytes + 4];  // magic, origVptr, origSlot, table[0]
	if (vptr < kHeaderBytes + 4 + 0x10000 || !ReadBytes(vptr - 4 - kHeaderBytes, head, sizeof(head))) return false;
	DWORD hv = 0, hs = 0;
	memcpy(&hv, head + 8, 4);
	memcpy(&hs, head + 12, 4);
	if (memcmp(head, kMagic, sizeof(kMagic)) != 0 || hv != orig) return false;
	DWORD seen = 0;
	if (!SwapVptr(object, orig, vptr, &seen) || seen != vptr) return false;
	// Its slot 102 back to the EXE's function too (it may point into an image that is gone).
	CopyPage* old = reinterpret_cast<CopyPage*>(vptr - 4 - kHeaderBytes);
	DWORD prot = 0;
	if (VirtualProtect(old, sizeof(CopyPage), PAGE_READWRITE, &prot))
	{
		old->table[1 + kSlotSecsPerTurn] = hs;
		VirtualProtect(old, sizeof(CopyPage), PAGE_READONLY, &prot);
	}
	Log(false, "pacing: the engine utility at 0x%08lx carried a copy left by an earlier proxy load (0x%08lx): put back "
	           "to the EXE's vftable before this install (issue 3308)", (unsigned long)object, (unsigned long)vptr);
	return true;
}

bool Install(const Config& c)
{
	g_base = c.base;
	Image img = {0, 0, 0};
	const char* bad = VerifyReason(VerifyGuarded(c.base, &img));
	if (bad) return Refuse("exe", "%s", bad);
	if (!c.mainThread) return Refuse("main-thread", "the EXE's main thread could not be found");
	const ULONG_PTR object = reinterpret_cast<ULONG_PTR>(c.object);
	DWORD app = 0, util = 0;
	if (!ReadDwords(c.base + Rvas().appGlobal, &app, 1) || app < 0x10000)
		return Refuse("app-global", "g_Civ5App (VA 0x%08lx) is not set", (unsigned long)(c.base + Rvas().appGlobal));
	if (!ReadDwords(app + kAppOffEngineUtility, &util, 1))
		return Refuse("app-global", "the app object 0x%08lx cannot be read at +0x%lx", (unsigned long)app,
		              (unsigned long)kAppOffEngineUtility);
	if (util != object)
		return Refuse("engine-utility", "the app's engine utility 0x%08lx is not the one the EXE gave the DLL (0x%08lx)",
		              (unsigned long)util, (unsigned long)object);
	const DWORD orig = static_cast<DWORD>(c.base + Rvas().utilVftable);
	DWORD vptr = 0;
	if (!ReadDwords(object, &vptr, 1)) return Refuse("vptr", "the engine utility 0x%08lx cannot be read", (unsigned long)object);
	const DWORD mine = CopyVptrOf(g_page);  // 0 before this load's first install
	if (mine && vptr == mine)
	{
		// This load's own copy, from an install on this object before (the object came back): take it back first.
		DWORD seen = 0;
		if (!SwapVptr(object, orig, mine, &seen) || seen != mine)
			return Refuse("vptr", "this load's own copy is on the object and could not be taken back");
	}
	else if (vptr != orig && !TakeBackOldCopy(object, vptr, orig))
		return Refuse("vptr", "the engine utility's vptr is 0x%08lx, not the EXE's vftable 0x%08lx", (unsigned long)vptr,
		              (unsigned long)orig);
	DWORD t[kEntries + 2];  // t[0] = the entry before slot 0, t[1 + i] = slot i, t[1 + kEntries] = the entry after
	if (!ReadDwords(orig - 4, t, kEntries + 2))
		return Refuse("vftable", "the vftable at 0x%08lx cannot be read", (unsigned long)orig);
	if (t[0] != 0 && (t[0] < c.base || t[0] >= c.base + img.size || InText(img, t[0])))
		return Refuse("vftable", "the entry before slot 0 is 0x%08lx: neither 0 nor a data pointer of the EXE",
		              (unsigned long)t[0]);
	for (int i = 0; i < kEntries; ++i)
		if (!InText(img, t[1 + i]))
			return Refuse("vftable", "slot %d is 0x%08lx, outside the EXE's .text", i, (unsigned long)t[1 + i]);
	if (t[1 + kSlotSecsPerTurn] != c.base + Rvas().secsPerTurn)
		return Refuse("vftable", "slot %d is 0x%08lx, not getSecsPerTurn 0x%08lx", kSlotSecsPerTurn,
		              (unsigned long)t[1 + kSlotSecsPerTurn], (unsigned long)(c.base + Rvas().secsPerTurn));
	if (InText(img, t[1 + kEntries]))
		return Refuse("vftable", "the entry after slot %d is a code pointer (0x%08lx): the table is longer than the %d "
		              "entries known", kEntries - 1, (unsigned long)t[1 + kEntries], kEntries);
	if (!g_page)
	{
		g_page = static_cast<CopyPage*>(VirtualAlloc(NULL, sizeof(CopyPage), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (!g_page) return Refuse("copy", "VirtualAlloc of the copy failed (error %lu)", (unsigned long)GetLastError());
	}
	else
	{
		DWORD prot = 0;
		if (!VirtualProtect(g_page, sizeof(CopyPage), PAGE_READWRITE, &prot))
			return Refuse("copy", "the copy cannot be made writable (error %lu)", (unsigned long)GetLastError());
	}
	memcpy(g_page->magic, kMagic, sizeof(kMagic));
	g_page->origVptr = orig;
	g_page->origSlot = t[1 + kSlotSecsPerTurn];
	memcpy(g_page->table, t, sizeof(DWORD) * (1 + kEntries));
	g_page->table[1 + kSlotSecsPerTurn] = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(&SecsPerTurn));
	DWORD prot = 0;
	VirtualProtect(g_page, sizeof(CopyPage), PAGE_READONLY, &prot);
	// What the replacement reads, before the swap.
	g_orig = reinterpret_cast<SecsFn>(static_cast<ULONG_PTR>(t[1 + kSlotSecsPerTurn]));
	g_site = c.base + Rvas().frameSite;
	g_mainTid = c.mainThread;
	g_aiByte = reinterpret_cast<const volatile BYTE*>(c.base + Rvas().aiTurnByte);
	g_hold = reinterpret_cast<const volatile LONG*>(c.base + Rvas().hold);
	g_tickSecs = (float)c.tickMs / 1000.0f;
	g_imageSize = img.size;
	g_origVptr = orig;
	DWORD seen = 0;
	if (!SwapVptr(object, CopyVptrOf(g_page), orig, &seen))
	{
		SetCopySlot(g_page, g_page->origSlot);
		return Refuse("swap", "an access fault writing the vptr of 0x%08lx", (unsigned long)object);
	}
	if (seen != orig)
	{
		SetCopySlot(g_page, g_page->origSlot);
		return Refuse("swap", "the vptr changed to 0x%08lx during the install", (unsigned long)seen);
	}
	g_object = object;
	InterlockedIncrement(&g_installs);
	Log(false, "pacing: on: [Performance] AiUpdateTickMs = %d: the engine utility 0x%08lx now uses a copy of the EXE's "
	           "vftable 0x%08lx (%d entries and the RTTI slot, checked) at 0x%08lx; slot %d (getSecsPerTurn 0x%08lx) "
	           "answers %.3f s at the frame's call site 0x%08lx on the main thread %lu while the AI-turn byte 0x%08lx is "
	           "1 and the hold 0x%08lx is 0, else the EXE's own (install %ld)",
	    c.tickMs, (unsigned long)object, (unsigned long)orig, kEntries, (unsigned long)CopyVptrOf(g_page),
	    kSlotSecsPerTurn, (unsigned long)g_page->origSlot, (double)g_tickSecs, (unsigned long)g_site,
	    (unsigned long)g_mainTid, (unsigned long)(c.base + Rvas().aiTurnByte), (unsigned long)(c.base + Rvas().hold),
	    (long)g_installs);
	return true;
}

const char* StateName(State s)
{
	switch (s)
	{
	case kOff: return "off";
	case kPending: return "pending";
	case kActive: return "active";
	case kStopped: return "stopped";
	}
	return "?";
}

// A failed check: the EXE's own pacing from here on (the vptr back when it is ours, the copy's slot 102 the EXE's),
// one loud line and the report.
void Fail(bool boot)
{
	const bool back = PutBack(g_object);
	if (g_page) SetCopySlot(g_page, g_page->origSlot);
	g_state = kStopped;
	char counters[300];
	CountersLine(counters, sizeof(counters));
	char line[900];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "pacing: FAILED check %s %s: %s; [Performance] AiUpdateTickMs = %d is not used: the EXE's own update "
	            "tick from here on%s (D118: %s); %s",
	            g_check, boot ? "at the install" : "during play", g_why, g_tickMs,
	            back ? ", the engine utility's vptr put back" : "",
	            boot ? "the error report and the popup" : "the error report, no popup (MC #3503)", counters);
	if (g_log) g_log(line, true);
	if (g_report) g_report(boot, g_check, line);
}
}  // namespace

void SetSinks(LogFn log, ReportFn report)
{
	g_log = log;
	g_report = report;
}

State CurrentState() { return g_state; }

const char* VerifyExe(ULONG_PTR base)
{
	Image img = {0, 0, 0};
	return VerifyReason(VerifyGuarded(base, &img));
}

void CountersLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "pacing counters: state %s, frame-site calls %ld (shortened %ld, stock %ld), frame-site calls on another "
	            "thread %ld, other callers %ld, checks %ld, installs %ld",
	            StateName(g_state), (long)g_siteCalls, (long)g_shortened, (long)g_siteStock, (long)g_siteOther,
	            (long)g_otherCallers, (long)g_checks, (long)g_installs);
	// The other callers by return address: "EXE+0x<rva>" inside the EXE's image, else the address.
	for (int i = 0; i < kRaSlots; ++i)
	{
		const ULONG_PTR a = static_cast<ULONG_PTR>(static_cast<DWORD>(g_raAddr[i]));
		if (!a) break;
		const size_t used = strlen(out);
		if (used + 48 >= cap) break;
		if (g_base && a >= g_base && a < g_base + g_imageSize)
			_snprintf_s(out + used, cap - used, _TRUNCATE, "%s EXE+0x%lx x%ld", i ? "," : "; other callers by return address:",
			            (unsigned long)(a - g_base), (long)g_raCount[i]);
		else
			_snprintf_s(out + used, cap - used, _TRUNCATE, "%s 0x%08lx x%ld", i ? "," : "; other callers by return address:",
			            (unsigned long)a, (long)g_raCount[i]);
	}
}

void Update(const Config& c)
{
	if (!g_decided)
	{
		g_decided = true;
		g_tickMs = c.tickMs;
		if (c.tickMs < 0)
		{
			g_state = kOff;
			Log(false, "pacing: off ([Performance] AiUpdateTickMs = %d): nothing installed, the EXE's own update tick "
			           "(D165 option 3, ticket 4662)", c.tickMs);
			return;
		}
		g_state = kPending;
	}
	switch (g_state)
	{
	case kOff:
	case kStopped:
		return;
	case kPending:
		if (!c.object) return;
		if (Install(c))
			g_state = kActive;
		else
			Fail(true);
		return;
	case kActive:
		break;
	}
	InterlockedIncrement(&g_checks);
	if (!c.object) return;  // between two game sessions: the object stays the EXE's, our copy on it
	const ULONG_PTR object = reinterpret_cast<ULONG_PTR>(c.object);
	if (object != g_object)
	{
		const bool back = PutBack(g_object);
		Log(false, "pacing: the EXE gave the DLL another engine utility (0x%08lx, was 0x%08lx; the old one's vptr %s): "
		           "installing on the new one", (unsigned long)object, (unsigned long)g_object,
		    back ? "put back" : "was no longer ours");
		g_object = 0;
		if (!Install(c)) Fail(false);
		return;
	}
	DWORD app = 0, util = 0, vptr = 0;
	if (!ReadDwords(c.base + Rvas().appGlobal, &app, 1) || app < 0x10000 || !ReadDwords(app + kAppOffEngineUtility, &util, 1))
	{
		Refuse("app-global", "g_Civ5App or its engine utility cannot be read any more");
		Fail(false);
		return;
	}
	if (util != object)
	{
		Refuse("engine-utility", "the app's engine utility is now 0x%08lx, not 0x%08lx", (unsigned long)util,
		       (unsigned long)object);
		Fail(false);
		return;
	}
	if (!ReadDwords(object, &vptr, 1))
	{
		Refuse("vptr", "an access fault reading the vptr of the engine utility 0x%08lx", (unsigned long)object);
		Fail(false);
		return;
	}
	if (vptr == CopyVptrOf(g_page)) return;
	if (vptr == g_origVptr)
	{
		Log(false, "pacing: the engine utility 0x%08lx has the EXE's own vftable again (re-made by the EXE?): installing "
		           "again", (unsigned long)object);
		g_object = 0;
		if (!Install(c)) Fail(false);
		return;
	}
	Refuse("vptr", "the engine utility's vptr is 0x%08lx, neither this copy (0x%08lx) nor the EXE's vftable (0x%08lx): "
	               "something else replaced it", (unsigned long)vptr, (unsigned long)CopyVptrOf(g_page),
	       (unsigned long)g_origVptr);
	Fail(false);
}

bool Restore(const char* why, char* line, size_t cap)
{
	if (cap) line[0] = 0;
	if (!g_page) return false;
	const bool back = PutBack(g_object);
	SetCopySlot(g_page, g_page->origSlot);
	if (g_state == kActive) g_state = kStopped;
	char counters[300];
	CountersLine(counters, sizeof(counters));
	_snprintf_s(line, cap, _TRUNCATE, "pacing: %s: %s; the copy's slot %d is the EXE's getSecsPerTurn again; %s", why,
	            back ? "the engine utility's vptr put back to the EXE's vftable"
	                 : "the engine utility's vptr was not this copy (nothing written)",
	            kSlotSecsPerTurn, counters);
	return true;
}

// ================================================================================================ option 2 (ticket 4665)
namespace
{
State g_skState = kPending;
bool g_skDecided = false;
bool g_skByteAtStart = false;  // this Update's sample (game-core thread only)
int g_skOn = 0;
ULONG_PTR g_skBase = 0;
volatile LONG g_skUpdates = 0, g_skAiUpdates = 0, g_skScans = 0, g_skCalls = 0, g_skWrites = 0, g_skAlready = 0,
              g_skNotAi = 0, g_skHeld = 0, g_skNone = 0, g_skHuman = 0, g_skScanFail = 0, g_skChecks = 0;
// D246 (ticket 0718): not written because the quick modes were not both on (and why), the reads, the logged changes.
volatile LONG g_skAnim = 0, g_skAnimCombat = 0, g_skAnimMovement = 0, g_skAnimUnknown = 0, g_qReads = 0, g_qChanges = 0;
int g_qLast = -1;  // the last read's state: -1 none yet in this load, 0 animations on, 1 both on, 2 not answered
LONGLONG g_qUs = 0, g_qUsMax = 0;  // the reads' time (the cost D246 adds per AI Update; game-core thread only)
LONGLONG g_skScanUs = 0, g_skScanMaxUs = 0;
char g_skCheck[40];
char g_skWhy[400];

bool SkRefuse(const char* check, const char* fmt, ...)
{
	_snprintf_s(g_skCheck, sizeof(g_skCheck), _TRUNCATE, "%s", check);
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(g_skWhy, sizeof(g_skWhy), _TRUNCATE, fmt, ap);
	va_end(ap);
	return false;
}

__declspec(noinline) LONG ExchangeRaw(ULONG_PTR at, LONG value)
{
	return InterlockedExchange(reinterpret_cast<volatile LONG*>(at), value);
}

// One aligned interlocked write; the value found before it in *old; false on an access fault.
bool ExchangeGuarded(ULONG_PTR at, LONG value, LONG* old)
{
	__try
	{
		*old = ExchangeRaw(at, value);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// The GameViewState the write goes to: g_Civ5App readable, [app+0xC0] a readable object with GameViewState's vptr and
// +0x8CC 4-byte aligned. 0 when a check fails (its name and reason in g_skCheck / g_skWhy).
ULONG_PTR SkGvs(ULONG_PTR base)
{
	DWORD app = 0, gvs = 0, vptr = 0;
	if (!ReadDwords(base + Rvas().appGlobal, &app, 1) || app < 0x10000)
	{
		SkRefuse("skip-app-global", "g_Civ5App (VA 0x%08lx) is not set or cannot be read", (unsigned long)(base + Rvas().appGlobal));
		return 0;
	}
	if (!ReadDwords(app + kAppOffGvs, &gvs, 1))
	{
		SkRefuse("skip-app-global", "the app object 0x%08lx cannot be read at +0x%lx", (unsigned long)app,
		         (unsigned long)kAppOffGvs);
		return 0;
	}
	if (gvs < 0x10000 || ((gvs + kGvsOffSeen) & 3) != 0)
	{
		SkRefuse("skip-gvs", "the app's game view state pointer is 0x%08lx (not an object, or +0x%lx not 4-byte aligned)",
		         (unsigned long)gvs, (unsigned long)kGvsOffSeen);
		return 0;
	}
	if (!ReadDwords(gvs, &vptr, 1))
	{
		SkRefuse("skip-gvs", "the game view state 0x%08lx cannot be read", (unsigned long)gvs);
		return 0;
	}
	if (vptr != base + Rvas().gvsVftable)
	{
		SkRefuse("skip-gvs", "the app's state 0x%08lx has the vptr 0x%08lx, not GameViewState's 0x%08lx", (unsigned long)gvs,
		         (unsigned long)vptr, (unsigned long)(base + Rvas().gvsVftable));
		return 0;
	}
	return gvs;
}

bool SkInstall(const SkipConfig& c)
{
	g_skBase = c.base;
	const char* bad = VerifyExe(c.base);
	if (bad) return SkRefuse("skip-exe", "%s", bad);
	const ULONG_PTR gvs = SkGvs(c.base);
	if (!gvs) return false;
	Log(false, "pacing: option 2 on: [Performance] AiSkipVisualDrain = %d: after each ICvGame1::Update that starts with the "
	           "AI-turn byte 0x%08lx at 1, with the hold 0x%08lx at 0, the proxy writes the first alive turn-active player "
	           "of slots 0-%d (unless it is the active human) into GameViewState+0x%lx (the game view state 0x%08lx, vptr "
	           "0x%08lx checked; re-checked before every write), only while Quick Combat and Quick Movement are on "
	           "(D246, read after each AI Update, ticket 0718)",
	    c.on, (unsigned long)(c.base + Rvas().aiTurnByte), (unsigned long)(c.base + Rvas().hold), kSlots - 1,
	    (unsigned long)kGvsOffSeen, (unsigned long)gvs, (unsigned long)(c.base + Rvas().gvsVftable));
	if (c.on == 2)
	{
		// Ticket 0571 (D191, 0568's variant F1).
		Log(false, "pacing: option 2 value 2: at an AI gap with more than %ld undispatched records in queue B (index at "
		           "0x%08lx) GameViewState+0x%lx is not written, so the frame's own gate drains queue B before the next "
		           "player; %s",
		    (long)kHoldFloorRecords, (unsigned long)(c.base + Rvas().qbIndex), (unsigned long)kGvsOffSeen,
		    c.drain == 1 ? "with option 4 the ordering byte is also cleared at such a gap when its guards pass"
		                 : "without option 4 (AiDrainUnordered = 1) such a gap drains in the EXE's order");
		if (c.drain != 1)
			Log(false, "pacing: warning: [Performance] AiSkipVisualDrain = 2 without AiDrainUnordered = 1 gains little: the "
			           "gaps with more than %ld records drain one keyed record per unit per frame, as the EXE does",
			    (long)kHoldFloorRecords);
	}
	return true;
}

void DrStopWithSkip();  // option 4 (ticket 4675), below

// A failed check: no more writes in this proxy load (the frame waits for queue B as the EXE does), one loud line and
// the report (boot: with the popup). Option 3 is not touched; option 4 stops with option 2 (its byte set back).
void SkFail(bool boot)
{
	g_skState = kStopped;
	DrStopWithSkip();
	char counters[700];  // ticket 0718: the line grew by the D246 counters
	SkipCountersLine(counters, sizeof(counters));
	char line[1400];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "pacing: FAILED check %s %s: %s; [Performance] AiSkipVisualDrain = %d is not used: the frame waits for "
	            "queue B as the EXE does from here on (D118: %s); %s",
	            g_skCheck, boot ? "at the install" : "during play", g_skWhy, g_skOn,
	            boot ? "the error report and the popup" : "the error report, no popup (MC #3503)", counters);
	if (g_log) g_log(line, true);
	if (g_report) g_report(boot, g_skCheck, line);
}

// ================================================================================================ option 4 (ticket 4675)
State g_drState = kOff;
bool g_drAnyMovement = false;  // TEST only: the Quick Movement guard not applied (options 2 and 4, D246)

// ================================================================================================ D246 (ticket 0718)
// One read of Quick Combat and Quick Movement per AI Update that reaches option 2's decision: after the Update, while it
// is still pending (the main thread pumps queue B only while no Update is pending, game-core-thread.md 6.3), so the
// read is at the next AI player's turn start (or the end-of-phase drain's start), never mid-drain. It decides option
// 2's write and option 4's clear for that Update.
struct QuickRead
{
	bool known;  // the DLL answered
	bool qc, qm;
	bool On() const { return known && qc && (qm || g_drAnyMovement); }
};

LONGLONG QpcFreq();

QuickRead ReadQuick(QuickFn quick)
{
	QuickRead r;
	r.known = r.qc = r.qm = false;
	InterlockedIncrement(&g_qReads);
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	if (quick && quick(&r.qc, &r.qm))
		r.known = true;
	else
		r.qc = r.qm = false;
	QueryPerformanceCounter(&t1);
	const LONGLONG us = (t1.QuadPart - t0.QuadPart) * 1000000 / QpcFreq();
	g_qUs += us;
	if (us > g_qUsMax) g_qUsMax = us;
	const int now = !r.known ? 2 : r.On() ? 1 : 0;
	if (now == g_qLast) return r;
	g_qLast = now;
	InterlockedIncrement(&g_qChanges);
	const char* which = g_drState == kActive ? "AiSkipVisualDrain and AiDrainUnordered" : "AiSkipVisualDrain";
	if (now == 1)
		Log(false, "pacing: D246: Quick Combat and Quick Movement on%s: [Performance] %s act%s from this AI player on "
		           "(ticket 0718)",
		    r.qm ? "" : " (TEST: Quick Movement is off, waived by VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1)", which,
		    g_drState == kActive ? "" : "s");
	else if (now == 0)
		Log(false, "pacing: D246: animations on (Quick Combat %s, Quick Movement %s): [Performance] %s rest%s; the frame "
		           "waits for queue B in the EXE's order from this AI player on (ticket 0718)",
		    r.qc ? "on" : "off", r.qm ? "on" : "off", which, g_drState == kActive ? "" : "s");
	else
		Log(false, "pacing: D246: Quick Combat and Quick Movement could not be asked of the DLL: [Performance] %s rest%s "
		           "until they can (ticket 0718)",
		    which, g_drState == kActive ? "" : "s");
	return r;
}

ULONG_PTR g_drBase = 0;
ULONG_PTR g_drByte = 0;   // the ordering byte the proxy holds at 0 (0 = none)
ULONG_PTR g_drLastByte = 0;
LONG g_drIndex = -1;      // queue B's index at the clear
volatile LONG g_drEnds = 0, g_drCleared = 0, g_drSetBack = 0, g_drNoCombat = 0, g_drNoMovement = 0, g_drUnknown = 0,
              g_drHeld = 0, g_drBit1 = 0, g_drChanging = 0, g_drRetries = 0, g_drIndexMoved = 0;
LONGLONG g_drRecs = 0, g_drRecsMax = 0, g_drUs = 0, g_drUsMax = 0;
char g_drCheck[40];
char g_drWhy[400];

bool DrRefuse(const char* check, const char* fmt, ...)
{
	_snprintf_s(g_drCheck, sizeof(g_drCheck), _TRUNCATE, "%s", check);
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(g_drWhy, sizeof(g_drWhy), _TRUNCATE, fmt, ap);
	va_end(ap);
	return false;
}

__declspec(noinline) void WriteByteRaw(ULONG_PTR at, BYTE v)
{
	*reinterpret_cast<volatile BYTE*>(at) = v;
}

bool WriteByteGuarded(ULONG_PTR at, BYTE v)
{
	__try
	{
		WriteByteRaw(at, v);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// The access of the EXE section holding [rva, rva + len) (kSec* bits), -1 when none does or the header cannot be read.
__declspec(noinline) int SectionAccessRaw(ULONG_PTR base, DWORD rva, DWORD len)
{
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return -1;
	const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
	const unsigned n = nt->FileHeader.NumberOfSections;
	if (n == 0 || n > 96) return -1;
	const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	for (unsigned i = 0; i < n; ++i)
	{
		const DWORD va = sec[i].VirtualAddress;
		const DWORD sz = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
		if (rva < va || rva + len > va + sz) continue;
		const DWORD c = sec[i].Characteristics;
		return ((c & IMAGE_SCN_MEM_EXECUTE) ? kSecExec : 0) | ((c & IMAGE_SCN_MEM_WRITE) ? kSecWrite : 0) |
		       ((c & IMAGE_SCN_MEM_READ) ? kSecRead : 0);
	}
	return -1;
}

int SectionAccess(ULONG_PTR base, DWORD rva, DWORD len)
{
	if (base < 0x10000) return -1;
	__try
	{
		return SectionAccessRaw(base, rva, len);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return -1;
	}
}

// Queue B's address from its index; 0 when the index cannot be read or is out of range (g_drCheck / g_drWhy set).
ULONG_PTR DrQueue(ULONG_PTR base, LONG* index)
{
	DWORD idx = 0;
	if (!ReadDwords(base + Rvas().qbIndex, &idx, 1))
	{
		DrRefuse("drain-queue", "an access fault reading queue B's index (VA 0x%08lx)", (unsigned long)(base + Rvas().qbIndex));
		return 0;
	}
	if (idx >= (DWORD)kQbIndexMax)
	{
		DrRefuse("drain-queue", "queue B's index (VA 0x%08lx) is %lu, not 0..%d", (unsigned long)(base + Rvas().qbIndex),
		         (unsigned long)idx, kQbIndexMax - 1);
		return 0;
	}
	*index = static_cast<LONG>(idx);
	return base + Rvas().qbBuf + kQbStride * idx;
}

// What a walk over queue B's two halves found.
struct DrWalk
{
	LONGLONG records;
	LONGLONG undispatched;  // ticket 0571: the write half's records and the dispatch half's from q+0x2C on
	long held;  // records with a hold counter (either half)
	long bit1;  // records of the write half with flag bit 1
	bool torn;  // a size chain that does not end exactly at the half's end
};

// One half, size-chained records from begin to end; records starting at or after countFrom count as undispatched. Not
// inlined: called inside __try (see ReadDwords).
__declspec(noinline) void WalkHalfRaw(ULONG_PTR begin, ULONG_PTR end, bool writeHalf, ULONG_PTR countFrom, DrWalk* w)
{
	ULONG_PTR at = begin;
	while (at < end)
	{
		if (end - at < kRecMin)
		{
			w->torn = true;
			return;
		}
		const DWORD size = *reinterpret_cast<const volatile DWORD*>(at);
		if (size < kRecMin || size > kRecMax || size > end - at)
		{
			w->torn = true;
			return;
		}
		const DWORD hold = *reinterpret_cast<const volatile DWORD*>(at + kRecOffHold);
		const DWORD flags = *reinterpret_cast<const volatile DWORD*>(at + kRecOffFlags);
		++w->records;
		if (at >= countFrom) ++w->undispatched;
		if (hold != 0) ++w->held;
		if (writeHalf && (flags & 2) != 0) ++w->bit1;
		at += size;
	}
}

// 0 = walked, 1 = an access fault, 2 = a half that is not a byte vector.
int WalkGuarded(const BYTE* ctl, DrWalk* w)
{
	DWORD p = 0;
	memcpy(&p, ctl + kQbOffFlip, 4);
	const DWORD hw = (p & 1) * 0x10, hd = ((p - 1) & 1) * 0x10;
	DWORD wb = 0, we = 0, db = 0, de = 0;
	memcpy(&wb, ctl + hw + 4, 4);
	memcpy(&we, ctl + hw + 8, 4);
	memcpy(&db, ctl + hd + 4, 4);
	memcpy(&de, ctl + hd + 8, 4);
	if (we < wb || de < db || we - wb > kHalfMax || de - db > kHalfMax || (wb < 0x10000 && we != wb) ||
	    (db < 0x10000 && de != db))
		return 2;
	DWORD dispatched = 0;
	memcpy(&dispatched, ctl + kQbOffDispatched, 4);
	// A dispatch offset past the half's end counts nothing of the dispatch half as undispatched.
	const ULONG_PTR from = dispatched <= de - db ? (ULONG_PTR)db + dispatched : (ULONG_PTR)de;
	__try
	{
		WalkHalfRaw(db, de, false, from, w);
		WalkHalfRaw(wb, we, true, wb, w);
		return 0;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return 1;
	}
}

bool DrInstall(ULONG_PTR base)
{
	g_drBase = base;
	{
		// ticket 4963: the layout of the running build (the header only, as before this ticket no more is checked here)
		const char* bad = VerifyReason(LayoutGuarded(base));
		if (bad) return DrRefuse("drain-exe", "%s", bad);
	}
	const int a1 = SectionAccess(base, Rvas().qbIndex, 4), a2 = SectionAccess(base, Rvas().qbBuf, kQbStride * kQbIndexMax);
	if (a1 < 0 || a2 < 0 || (a1 & (kSecRead | kSecWrite)) != (kSecRead | kSecWrite) ||
	    (a2 & (kSecRead | kSecWrite)) != (kSecRead | kSecWrite) || (a1 & kSecExec) || (a2 & kSecExec))
		return DrRefuse("drain-exe", "queue B's index or buffers (VA 0x%08lx, 0x%08lx) are not in a writable data section of "
		                             "the EXE", (unsigned long)(base + Rvas().qbIndex), (unsigned long)(base + Rvas().qbBuf));
	LONG idx = -1;
	const ULONG_PTR q = DrQueue(base, &idx);
	if (!q) return false;
	BYTE ctl[kQbCtl];
	if (!ReadBytes(q, ctl, kQbCtl))
		return DrRefuse("drain-queue", "queue B's control block at 0x%08lx cannot be read", (unsigned long)q);
	if (ctl[kQbOffOrder] != 1)
		return DrRefuse("drain-byte", "queue B's ordering byte at 0x%08lx is %u, not 1", (unsigned long)(q + kQbOffOrder),
		                (unsigned)ctl[kQbOffOrder]);
	Log(false, "pacing: option 4 on: [Performance] AiDrainUnordered = 1: at the AI turn's end-of-phase drain (option 2's scan "
	           "finds no turn-active player) the proxy clears queue B's ordering byte (index %ld, the byte at 0x%08lx, now 1) "
	           "when Quick Combat%s on, no record of queue B is held and none has flag bit 1, and sets it back to 1 at the "
	           "next Update%s",
	    (long)idx, (unsigned long)(q + kQbOffOrder), g_drAnyMovement ? " is" : " and Quick Movement are",
	    g_drAnyMovement ? "; TEST: the Quick Movement guard is NOT applied (VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1)" : "");
	return true;
}

// The byte back to 1 when the proxy holds it. 0 = nothing held, 1 = set back, -1 = a check failed (g_drCheck set; the
// byte is then forgotten, never written).
int DrSetBack()
{
	if (!g_drByte) return 0;
	const ULONG_PTR at = g_drByte;
	g_drByte = 0;
	BYTE b = 0;
	if (!ReadBytes(at, &b, 1))
	{
		DrRefuse("drain-read", "an access fault reading queue B's ordering byte at 0x%08lx to set it back", (unsigned long)at);
		return -1;
	}
	if (b != 0)
	{
		DrRefuse("drain-byte", "queue B's ordering byte at 0x%08lx is %u when it is to be set back, not the proxy's 0 (written by "
		                       "the EXE?)", (unsigned long)at, (unsigned)b);
		return -1;
	}
	if (!WriteByteGuarded(at, 1))
	{
		DrRefuse("drain-write", "an access fault setting queue B's ordering byte at 0x%08lx back to 1", (unsigned long)at);
		return -1;
	}
	InterlockedIncrement(&g_drSetBack);
	DWORD now = 0;
	if (ReadDwords(g_drBase + Rvas().qbIndex, &now, 1) && (LONG)now != g_drIndex)
	{
		InterlockedIncrement(&g_drIndexMoved);
		Log(false, "pacing: option 4: queue B's index moved from %ld to %lu while the ordering byte was 0; the byte of index "
		           "%ld set back", (long)g_drIndex, (unsigned long)now, (long)g_drIndex);
	}
	return 1;
}

// A failed check of option 4: no more clears in this proxy load, the byte back to 1 when it is still the proxy's 0, one
// loud line and the report (boot: with the popup). Options 2 and 3 are not touched.
void DrFail(bool boot)
{
	g_drState = kStopped;
	bool back = false;
	if (g_drByte)
	{
		BYTE b = 0;
		const ULONG_PTR at = g_drByte;
		g_drByte = 0;
		back = ReadBytes(at, &b, 1) && b == 0 && WriteByteGuarded(at, 1);
	}
	char counters[500];
	DrainCountersLine(counters, sizeof(counters));
	char line[1100];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "pacing: FAILED check %s %s: %s; [Performance] AiDrainUnordered = 1 is not used: queue B drains in the "
	            "EXE's order from here on%s (D118: %s); %s",
	            g_drCheck, boot ? "at the install" : "during play", g_drWhy,
	            back ? ", the ordering byte set back to 1" : "",
	            boot ? "the error report and the popup" : "the error report, no popup (MC #3503)", counters);
	if (g_log) g_log(line, true);
	if (g_report) g_report(boot, g_drCheck, line);
}

// Option 2 has failed: option 4 cannot act without it (the byte back when held; option 2's report covers it).
void DrStopWithSkip()
{
	if (g_drState != kActive && g_drState != kPending) return;
	g_drState = kStopped;
	bool back = false;
	if (g_drByte)
	{
		BYTE b = 0;
		const ULONG_PTR at = g_drByte;
		g_drByte = 0;
		back = ReadBytes(at, &b, 1) && b == 0 && WriteByteGuarded(at, 1);
	}
	Log(true, "pacing: option 4 stops with option 2 ([Performance] AiDrainUnordered = 1 needs AiSkipVisualDrain)%s",
	    back ? "; the ordering byte set back to 1" : "");
}

LONGLONG QpcFreq();

// Queue B walked until two reads of its control block around the walk agree, at most 3 attempts (the main thread does
// not pump while an Update is pending, but the post functions move the end pointer before they write a record). 0 = a
// stable walk (ctl and w filled), 1 = the queue changed or a size chain did not hold in every attempt, -1 = a failure
// (g_drCheck / g_drWhy set; the caller decides whose failure it is).
int QbStableWalk(ULONG_PTR q, BYTE* ctl, DrWalk* w)
{
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		if (attempt)
		{
			InterlockedIncrement(&g_drRetries);
			Sleep(1);
		}
		BYTE again[kQbCtl];
		if (!ReadBytes(q, ctl, kQbCtl))
		{
			DrRefuse("drain-read", "an access fault reading queue B's control block at 0x%08lx", (unsigned long)q);
			return -1;
		}
		memset(w, 0, sizeof(*w));
		const int rc = WalkGuarded(ctl, w);
		if (!ReadBytes(q, again, kQbCtl))
		{
			DrRefuse("drain-read", "an access fault reading queue B's control block at 0x%08lx", (unsigned long)q);
			return -1;
		}
		const bool same = memcmp(ctl, again, kQbCtl) == 0;
		if (rc == 0 && same && !w->torn) return 0;
		if (same && rc != 0)
		{
			// The queue did not move and still could not be walked: the layout is not what the proxy was written for.
			DrRefuse(rc == 1 ? "drain-read" : "drain-queue",
			         rc == 1 ? "an access fault walking queue B's records (control block 0x%08lx unchanged)"
			                 : "queue B's halves at 0x%08lx are not byte vectors",
			         (unsigned long)q);
			return -1;
		}
	}
	return 1;
}

// The end-of-phase drain (option 2's scan found no turn-active player after an AI Update; the Update is still pending,
// so the main thread does not pump queue B): the guards (the quick modes from this Update's D246 read), then the clear.
void DrEndOfPhase(const QuickRead& qr)
{
	InterlockedIncrement(&g_drEnds);
	if (g_drByte) return;  // held already (an After without a Before does not reach here; kept for safety)
	if (!qr.known)
	{
		InterlockedIncrement(&g_drUnknown);
		return;
	}
	if (!qr.qc)
	{
		InterlockedIncrement(&g_drNoCombat);
		return;
	}
	if (!qr.qm && !g_drAnyMovement)
	{
		InterlockedIncrement(&g_drNoMovement);
		return;
	}
	LONG idx = -1;
	const ULONG_PTR q = DrQueue(g_drBase, &idx);
	if (!q)
	{
		DrFail(false);
		return;
	}
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	DrWalk w;
	BYTE ctl[kQbCtl];
	const int walked = QbStableWalk(q, ctl, &w);
	if (walked < 0)
	{
		DrFail(false);
		return;
	}
	const bool stable = walked == 0;
	QueryPerformanceCounter(&t1);
	const LONGLONG us = (t1.QuadPart - t0.QuadPart) * 1000000 / QpcFreq();
	g_drUs += us;
	if (us > g_drUsMax) g_drUsMax = us;
	if (!stable)
	{
		const LONG n = InterlockedIncrement(&g_drChanging);
		if (n <= 3)
			Log(false, "pacing: option 4: queue B changed or was being written during 3 walks; not cleared this time (%ld)",
			    (long)n);
		return;
	}
	g_drRecs += w.records;
	if (w.records > g_drRecsMax) g_drRecsMax = w.records;
	if (w.held)
	{
		InterlockedIncrement(&g_drHeld);
		return;
	}
	if (w.bit1)
	{
		InterlockedIncrement(&g_drBit1);
		return;
	}
	if (ctl[kQbOffOrder] != 1)
	{
		DrRefuse("drain-byte", "queue B's ordering byte at 0x%08lx is %u before the clear, not 1", (unsigned long)(q + kQbOffOrder),
		         (unsigned)ctl[kQbOffOrder]);
		DrFail(false);
		return;
	}
	if (!WriteByteGuarded(q + kQbOffOrder, 0))
	{
		DrRefuse("drain-write", "an access fault clearing queue B's ordering byte at 0x%08lx", (unsigned long)(q + kQbOffOrder));
		DrFail(false);
		return;
	}
	g_drByte = g_drLastByte = q + kQbOffOrder;
	g_drIndex = idx;
	InterlockedIncrement(&g_drCleared);
}

LONGLONG QpcFreq()
{
	static LONGLONG f = 0;
	if (!f)
	{
		LARGE_INTEGER q;
		QueryPerformanceFrequency(&q);
		f = q.QuadPart ? q.QuadPart : 1;
	}
	return f;
}

// ================================================================================================ option 2 value 2 (ticket 0571)
// At an AI gap with more than kHoldFloorRecords undispatched records, option 2 leaves +0x8CC alone, so the frame's own
// gate waits for queue B (the AI-turn byte drops, the pump runs), and option 4 clears the ordering byte there when its
// guards pass. Why not 0568's hold of the next request: the pump moves no record while the AI-turn byte is 1 (ticket 0571,
// poller [L]); see proxy_pacing.h.
volatile LONG g_hdGaps = 0, g_hdGated = 0, g_hdSmall = 0, g_hdChanging = 0;
LONGLONG g_hdRecsMax = 0, g_hdRecsLast = 0, g_hdRecsAll = 0, g_hdUs = 0, g_hdUsMax = 0;
// Option 4 at gated gaps (its end-of-phase counters stay as they were).
volatile LONG g_drGapCleared = 0, g_drGapNotQuick = 0, g_drGapHeld = 0, g_drGapBit1 = 0;

// Option 4 at a gated gap: its guards on the walk HdGap made, then the clear (set back at the next Update's top). Since
// D246 (ticket 0718) a gap reaches here only with the quick modes on (this Update's read); the check is kept for safety.
void DrAtGap(const QuickRead& qr, ULONG_PTR q, LONG idx, const BYTE* ctl, const DrWalk& w)
{
	if (g_drByte) return;  // held already (the set-back runs at every Update's top; kept for safety)
	if (!qr.On())
	{
		InterlockedIncrement(&g_drGapNotQuick);
		return;
	}
	if (w.held)
	{
		InterlockedIncrement(&g_drGapHeld);
		return;
	}
	if (w.bit1)
	{
		InterlockedIncrement(&g_drGapBit1);
		return;
	}
	if (ctl[kQbOffOrder] != 1)
	{
		DrRefuse("drain-byte", "queue B's ordering byte at 0x%08lx is %u before the clear at an AI gap, not 1",
		         (unsigned long)(q + kQbOffOrder), (unsigned)ctl[kQbOffOrder]);
		DrFail(false);
		return;
	}
	if (!WriteByteGuarded(q + kQbOffOrder, 0))
	{
		DrRefuse("drain-write", "an access fault clearing queue B's ordering byte at 0x%08lx at an AI gap",
		         (unsigned long)(q + kQbOffOrder));
		DrFail(false);
		return;
	}
	g_drByte = g_drLastByte = q + kQbOffOrder;
	g_drIndex = idx;
	InterlockedIncrement(&g_drGapCleared);
}

enum HdVerdict
{
	kHdWrite,  // write +0x8CC as value 1 does
	kHdGate,   // leave +0x8CC: the frame's own gate drains queue B first
	kHdStop    // option 2 failed (reported); nothing more
};

// At an AI gap (option 2's scan found the next AI player; the Update is still pending, so the main thread does not
// pump queue B): count queue B's undispatched records; above the floor the gap goes to the EXE's gate, with option 4's
// clear when its guards pass.
HdVerdict HdGap(const QuickRead& qr)
{
	InterlockedIncrement(&g_hdGaps);
	LONG idx = -1;
	const ULONG_PTR q = DrQueue(g_skBase, &idx);
	if (!q)
	{
		SkRefuse("skip-gap-queue", "%s", g_drWhy);
		SkFail(false);
		return kHdStop;
	}
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	BYTE ctl[kQbCtl];
	DrWalk w;
	const int walked = QbStableWalk(q, ctl, &w);
	QueryPerformanceCounter(&t1);
	const LONGLONG us = (t1.QuadPart - t0.QuadPart) * 1000000 / QpcFreq();
	g_hdUs += us;
	if (us > g_hdUsMax) g_hdUsMax = us;
	if (walked < 0)
	{
		SkRefuse(strcmp(g_drCheck, "drain-read") == 0 ? "skip-gap-read" : "skip-gap-queue", "%s", g_drWhy);
		SkFail(false);
		return kHdStop;
	}
	if (walked > 0)
	{
		const LONG n = InterlockedIncrement(&g_hdChanging);
		if (n <= 3)
			Log(false, "pacing: option 2 value 2: queue B changed or was being written during 3 walks at an AI gap; written "
			           "as with value 1 this time (%ld)", (long)n);
		return kHdWrite;
	}
	g_hdRecsLast = w.undispatched;
	g_hdRecsAll += w.undispatched;
	if (w.undispatched > g_hdRecsMax) g_hdRecsMax = w.undispatched;
	if (w.undispatched <= kHoldFloorRecords)
	{
		InterlockedIncrement(&g_hdSmall);
		return kHdWrite;
	}
	if (g_drState == kActive) DrAtGap(qr, q, idx, ctl, w);
	InterlockedIncrement(&g_hdGated);
	return kHdGate;
}
}  // namespace

State SkipState() { return g_skState; }

void SkipCountersLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "pacing: option 2 counters: state %s, Updates %ld (starting in an AI turn %ld), scans %ld (DLL calls %ld, "
	            "%I64d us, max %I64d us), writes %ld (already that player %ld), not written: human turn %ld, hold %ld, no "
	            "turn-active player %ld, the active human %ld, scan failed %ld, animations on %ld (Quick Combat off %ld, "
	            "Quick Movement off %ld, settings unknown %ld); checks %ld; quick reads %ld (changes %ld, %I64d us, max %I64d "
	            "us)",
	            StateName(g_skState), (long)g_skUpdates, (long)g_skAiUpdates, (long)g_skScans, (long)g_skCalls, g_skScanUs,
	            g_skScanMaxUs, (long)g_skWrites, (long)g_skAlready, (long)g_skNotAi, (long)g_skHeld, (long)g_skNone,
	            (long)g_skHuman, (long)g_skScanFail, (long)g_skAnim, (long)g_skAnimCombat, (long)g_skAnimMovement,
	            (long)g_skAnimUnknown, (long)g_skChecks, (long)g_qReads, (long)g_qChanges, g_qUs, g_qUsMax);
}

void SkipBefore(const SkipConfig& c)
{
	if (!g_skDecided)
	{
		g_skDecided = true;
		g_skOn = c.on;
		g_drState = kOff;
		g_drAnyMovement = c.drainAnyMovement;
		if (c.on != 1 && c.on != 2)
		{
			g_skState = kOff;
			Log(false, "pacing: option 2 off ([Performance] AiSkipVisualDrain = %d): nothing read or written, the frame waits "
			           "for queue B as the EXE does (D165 option 2, ticket 4665)", c.on);
			if (c.drain == 1)
				Log(false, "pacing: warning: option 4 not used: [Performance] AiDrainUnordered = 1 needs AiSkipVisualDrain = 1 "
				           "or 2 (D165 option 4, ticket 4675); nothing read or written");
			return;
		}
		if (c.headless)
		{
			g_skState = kOff;
			Log(false, "pacing: option 2 not used: [Performance] AiSkipVisualDrain = %d but host=headless (no EXE frame to "
			           "pace); nothing read or written", c.on);
			if (c.drain == 1)
				Log(false, "pacing: warning: option 4 not used: [Performance] AiDrainUnordered = 1 but option 2 is not used "
				           "(host=headless); nothing read or written");
			return;
		}
		g_skState = kPending;
		if (c.drain == 1)
			g_drState = kPending;
		else
			Log(false, "pacing: option 4 off ([Performance] AiDrainUnordered = %d): queue B's ordering byte is not read or "
			           "written (D165 option 4, ticket 4675)", c.drain);
	}
	// Option 4: the ordering byte back to 1 at the top of the Update after the end-of-phase clear (the turn-change
	// Update: its request set the pending count, so no pump has run since).
	if (g_drByte && DrSetBack() < 0) DrFail(false);
	g_skByteAtStart = false;
	switch (g_skState)
	{
	case kOff:
	case kStopped:
		return;
	case kPending:
		if (!SkInstall(c))
		{
			SkFail(true);
			return;
		}
		g_skState = kActive;
		if (g_drState == kPending)
		{
			if (DrInstall(c.base))
				g_drState = kActive;
			else
				DrFail(true);
		}
		break;
	case kActive:
		break;
	}
	InterlockedIncrement(&g_skUpdates);
	BYTE b = 0;
	if (!ReadBytes(g_skBase + Rvas().aiTurnByte, &b, 1))
	{
		SkRefuse("skip-ai-byte", "an access fault reading the AI-turn byte 0x%08lx", (unsigned long)(g_skBase + Rvas().aiTurnByte));
		SkFail(false);
		return;
	}
	g_skByteAtStart = b == 1;
}

void SkipAfter(ScanFn scan, QuickFn quick)
{
	if (g_skState != kActive) return;
	if (!g_skByteAtStart)
	{
		InterlockedIncrement(&g_skNotAi);
		return;
	}
	g_skByteAtStart = false;  // one write per Update
	InterlockedIncrement(&g_skAiUpdates);
	DWORD hold = 0;
	if (!ReadDwords(g_skBase + Rvas().hold, &hold, 1))
	{
		SkRefuse("skip-hold", "an access fault reading the hold 0x%08lx", (unsigned long)(g_skBase + Rvas().hold));
		SkFail(false);
		return;
	}
	if (hold != 0)
	{
		InterlockedIncrement(&g_skHeld);
		return;
	}
	InterlockedIncrement(&g_skChecks);
	const ULONG_PTR gvs = SkGvs(g_skBase);
	if (!gvs)
	{
		SkFail(false);
		return;
	}
	ScanResult r;
	r.first = kSlots;
	r.active = -1;
	r.activeHuman = false;
	r.calls = 0;
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	const bool ok = scan && scan(&r);
	QueryPerformanceCounter(&t1);
	const LONGLONG us = (t1.QuadPart - t0.QuadPart) * 1000000 / QpcFreq();
	g_skScanUs += us;
	if (us > g_skScanMaxUs) g_skScanMaxUs = us;
	InterlockedIncrement(&g_skScans);
	InterlockedExchangeAdd(&g_skCalls, r.calls);
	if (!ok)
	{
		InterlockedIncrement(&g_skScanFail);
		return;
	}
	if (r.first < 0 || r.first >= kSlots)
	{
		InterlockedIncrement(&g_skNone);
		// The AI turn's end-of-phase drain follows (nothing written: the frame waits for queue B before the turn-change
		// request). Option 4 (ticket 4675): its guards (the D246 read, ticket 0718) and the clear.
		if (g_drState == kActive) DrEndOfPhase(ReadQuick(quick));
		return;
	}
	if (r.first == r.active && r.activeHuman)
	{
		InterlockedIncrement(&g_skHuman);
		return;
	}
	// D246 (ticket 0718): the next AI player's turn starts here; with animations on (Quick Combat or Quick Movement off,
	// or not answered) nothing is written, so the frame's own gate drains queue B in the EXE's order, as stock.
	const QuickRead qr = ReadQuick(quick);
	if (!qr.On())
	{
		InterlockedIncrement(&g_skAnim);
		InterlockedIncrement(!qr.known ? &g_skAnimUnknown : !qr.qc ? &g_skAnimCombat : &g_skAnimMovement);
		return;
	}
	if (g_skOn == 2)
	{
		// Ticket 0571: a gap with many undispatched records goes to the frame's own gate (+0x8CC not written).
		const HdVerdict v = HdGap(qr);
		if (v != kHdWrite) return;
	}
	LONG old = 0;
	if (!ExchangeGuarded(gvs + kGvsOffSeen, r.first, &old))
	{
		SkRefuse("skip-write", "an access fault writing GameViewState+0x%lx at 0x%08lx", (unsigned long)kGvsOffSeen,
		         (unsigned long)(gvs + kGvsOffSeen));
		SkFail(false);
		return;
	}
	InterlockedIncrement(&g_skWrites);
	if (old == r.first) InterlockedIncrement(&g_skAlready);
}

State DrainState() { return g_drState; }

bool DrainArmed() { return g_drByte != 0; }

bool DrainRestore(const char* why, char* line, size_t cap)
{
	if (cap) line[0] = 0;
	if (!g_drByte) return false;
	const ULONG_PTR at = g_drByte;
	const int rc = DrSetBack();
	if (rc < 0)
	{
		_snprintf_s(line, cap, _TRUNCATE, "pacing: option 4: %s: queue B's ordering byte at 0x%08lx NOT set back: %s", why,
		            (unsigned long)at, g_drWhy);
		return false;
	}
	char counters[500];
	DrainCountersLine(counters, sizeof(counters));
	_snprintf_s(line, cap, _TRUNCATE, "pacing: option 4: %s: queue B's ordering byte at 0x%08lx set back to 1; %s", why,
	            (unsigned long)at, counters);
	return true;
}

void DrainCountersLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "pacing: option 4 counters: state %s, end-of-phase drains %ld, cleared %ld, set back %ld, not cleared: Quick "
	            "Combat off %ld, Quick Movement off %ld, settings unknown %ld, held %ld, flag bit 1 %ld, queue changing %ld "
	            "(retries %ld); records walked %I64d (max %I64d), %I64d us (max %I64d us); index moved %ld",
	            StateName(g_drState), (long)g_drEnds, (long)g_drCleared, (long)g_drSetBack, (long)g_drNoCombat,
	            (long)g_drNoMovement, (long)g_drUnknown, (long)g_drHeld, (long)g_drBit1, (long)g_drChanging,
	            (long)g_drRetries, g_drRecs, g_drRecsMax, g_drUs, g_drUsMax, (long)g_drIndexMoved);
}

void HoldCountersLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "pacing: option 2 value 2 counters: gaps %ld (to the EXE's gate %ld; written: at most %ld records %ld, queue "
	            "changing %ld); undispatched records at a gap max %I64d (all gaps %I64d); walks %I64d us (max %I64d us); "
	            "option 4 at gated gaps: cleared %ld, not cleared: settings %ld, held %ld, flag bit 1 %ld",
	            (long)g_hdGaps, (long)g_hdGated, (long)kHoldFloorRecords, (long)g_hdSmall, (long)g_hdChanging, g_hdRecsMax,
	            g_hdRecsAll, g_hdUs, g_hdUsMax, (long)g_drGapCleared, (long)g_drGapNotQuick, (long)g_drGapHeld,
	            (long)g_drGapBit1);
}

namespace Test
{
void Reset()
{
	g_state = kPending;
	g_decided = false;
	g_tickMs = -1;
	g_object = 0;
	g_base = 0;
	g_origVptr = 0;
	g_page = NULL;
	g_installs = g_checks = 0;
	g_site = 0;
	g_mainTid = 0;
	g_aiByte = NULL;
	g_hold = NULL;
	g_orig = NULL;
	g_tickSecs = 0.0f;
	g_siteCalls = g_shortened = g_siteStock = g_siteOther = g_otherCallers = 0;
	g_lastRa = 0;
	g_imageSize = 0;
	for (int i = 0; i < kRaSlots; ++i) g_raAddr[i] = g_raCount[i] = 0;
	g_skState = kPending;
	g_skDecided = false;
	g_skByteAtStart = false;
	g_skOn = 0;
	g_skBase = 0;
	g_skUpdates = g_skAiUpdates = g_skScans = g_skCalls = g_skWrites = g_skAlready = 0;
	g_skNotAi = g_skHeld = g_skNone = g_skHuman = g_skScanFail = g_skChecks = 0;
	g_skScanUs = g_skScanMaxUs = 0;
	g_skAnim = g_skAnimCombat = g_skAnimMovement = g_skAnimUnknown = g_qReads = g_qChanges = 0;  // ticket 0718
	g_qLast = -1;
	g_qUs = g_qUsMax = 0;
	g_drState = kOff;
	g_drAnyMovement = false;
	g_drBase = 0;
	g_drByte = g_drLastByte = 0;
	g_drIndex = -1;
	g_drEnds = g_drCleared = g_drSetBack = g_drNoCombat = g_drNoMovement = g_drUnknown = 0;
	g_drHeld = g_drBit1 = g_drChanging = g_drRetries = g_drIndexMoved = 0;
	g_drRecs = g_drRecsMax = g_drUs = g_drUsMax = 0;
	// ticket 0571
	g_hdGaps = g_hdGated = g_hdSmall = g_hdChanging = 0;
	g_hdRecsMax = g_hdRecsLast = g_hdRecsAll = g_hdUs = g_hdUsMax = 0;
	g_drGapCleared = g_drGapNotQuick = g_drGapHeld = g_drGapBit1 = 0;
}
void SetSite(ULONG_PTR site) { g_site = site; }
ULONG_PTR LastReturnAddress() { return g_lastRa; }
ULONG_PTR CopyVptr() { return CopyVptrOf(g_page); }
long SiteCalls() { return g_siteCalls; }
long Shortened() { return g_shortened; }
long SiteStock() { return g_siteStock; }
long SiteOtherThread() { return g_siteOther; }
long OtherCallers() { return g_otherCallers; }
long Checks() { return g_checks; }
long Installs() { return g_installs; }
long SkipWrites() { return g_skWrites; }
long SkipScans() { return g_skScans; }
long SkipChecks() { return g_skChecks; }
long SkipNotAiTurn() { return g_skNotAi; }
long SkipHeld() { return g_skHeld; }
long SkipNoPlayer() { return g_skNone; }
long SkipHuman() { return g_skHuman; }
long SkipAnimations() { return g_skAnim; }
long QuickReads() { return g_qReads; }
long QuickChanges() { return g_qChanges; }
long DrainEnds() { return g_drEnds; }
long DrainCleared() { return g_drCleared; }
long DrainSetBack() { return g_drSetBack; }
long DrainNotQuick() { return g_drNoCombat + g_drNoMovement + g_drUnknown; }
long DrainHeld() { return g_drHeld; }
long DrainBit1() { return g_drBit1; }
long DrainChanging() { return g_drChanging; }
ULONG_PTR DrainByteAddress() { return g_drLastByte; }
long HoldGaps() { return g_hdGaps; }
long HoldGated() { return g_hdGated; }
long HoldSmall() { return g_hdSmall; }
long HoldChanging() { return g_hdChanging; }
LONGLONG HoldLastRecords() { return g_hdRecsLast; }
long DrainGapCleared() { return g_drGapCleared; }
long DrainGapRefused() { return g_drGapNotQuick + g_drGapHeld + g_drGapBit1; }
}  // namespace Test
}  // namespace ProxyPacing

#ifndef VP_PACING_OFFLINE
// ================================================================================================ the engine glue
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp (issue 2704)
extern "C" ICvGameContext1* DllGetGameContext();                  // proxy_rt.cpp: after the boot, the context wrapper

#ifndef THREAD_QUERY_LIMITED_INFORMATION
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#endif

namespace
{
void GlueLog(const char* line, bool loud)
{
	if (loud)
		ProxyRtInt::LogErrPublic("%s", line);
	else
		ProxyRtInt::Log("%s", line);
}

// The EXE's main thread: the process's first thread (the one with the earliest creation time).
DWORD FindMainThread()
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	const DWORD pid = GetCurrentProcessId();
	DWORD best = 0;
	ULONGLONG bestTime = ~0ULL;
	THREADENTRY32 te;
	te.dwSize = sizeof(te);
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
	{
		if (te.th32OwnerProcessID != pid) continue;
		HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
		if (!h) continue;
		FILETIME created, exited, kernel, user;
		if (GetThreadTimes(h, &created, &exited, &kernel, &user))
		{
			const ULONGLONG t = ((ULONGLONG)created.dwHighDateTime << 32) | created.dwLowDateTime;
			if (t < bestTime)
			{
				bestTime = t;
				best = te.th32ThreadID;
			}
		}
		CloseHandle(h);
		te.dwSize = sizeof(te);
	}
	CloseHandle(snap);
	return best;
}

std::string Narrow(const std::wstring& w)
{
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

// The newest vpproxy.log among the places the proxy writes it (log=, the run folders under Logs\VP64\run and under
// run_root, beside the proxy): the one this load has just written to.
std::string FindLogFile()
{
	const ProxyLogic::Settings& s = ProxyIniSettings();
	if (!s.logPath.empty()) return s.logPath;
	std::vector<std::string> dirs;
	std::wstring cfg;
	if (ProxySession::ConfigIniPath(&cfg))
	{
		const size_t slash = cfg.find_last_of(L"\\/");
		if (slash != std::wstring::npos) dirs.push_back(Narrow(cfg.substr(0, slash)) + "\\Logs\\VP64\\run");
	}
	dirs.push_back(s.runRoot);
	std::string best;
	ULONGLONG bestTime = 0;
	for (size_t i = 0; i < dirs.size(); ++i)
	{
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dirs[i] + "\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
			const std::string f = dirs[i] + "\\" + fd.cFileName + "\\vpproxy.log";
			WIN32_FILE_ATTRIBUTE_DATA a;
			if (!GetFileAttributesExA(f.c_str(), GetFileExInfoStandard, &a)) continue;
			const ULONGLONG t = ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
			if (t > bestTime)
			{
				bestTime = t;
				best = f;
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	if (best.empty()) best = s.proxyDir + "\\vpproxy.log";
	return best;
}

volatile LONG g_reported = 0;

// The D118 report of a failed check (proxy_report.h), once per proxy load; boot = also the helper's popup. The game
// goes on in both cases (the EXE's own pacing is the stock game). This is not a failure of the 64-bit path, so the
// helper gets the stock session's verb (popup), never "failed" (which would switch the patch off for the next start).
void GlueReport(bool boot, const char* check, const char* line)
{
	if (InterlockedCompareExchange(&g_reported, 1, 0) != 0)
	{
		ProxyRtInt::Log("pacing: a report was written already in this proxy load; this failure is in the log only");
		return;
	}
	ProxyRtInt::LogFlush();
	const ProxyLogic::Settings& s = ProxyIniSettings();
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCSTR>(&GlueReport), &self);
	ProxyReport::Input in;
	in.stage = "pacing";
	in.code = std::string(boot ? "install-" : "play-") + check;
	in.details.push_back(line);
	char counters[700];  // ticket 0718: option 2's line grew by the D246 counters
	ProxyPacing::CountersLine(counters, sizeof(counters));
	in.details.push_back(counters);
	ProxyPacing::SkipCountersLine(counters, sizeof(counters));
	in.details.push_back(counters);
	{
		char dc[500];
		ProxyPacing::DrainCountersLine(dc, sizeof(dc));
		in.details.push_back(dc);
		char hc[700];
		ProxyPacing::HoldCountersLine(hc, sizeof(hc));  // ticket 0571
		in.details.push_back(hc);
	}
	const bool option2 = strncmp(check, "skip-", 5) == 0;   // ticket 4665: option 2's checks are named skip-*
	const bool option4 = strncmp(check, "drain-", 6) == 0;  // ticket 4675: option 4's checks are named drain-*
	in.summary = std::string("The patch's AI-turn pacing option ([Performance] ") +
	             (option4 ? "AiDrainUnordered" : option2 ? "AiSkipVisualDrain" : "AiUpdateTickMs") + ") failed a check (" +
	             check + "); the game goes on with the game's own pacing.";
	{
		char id[1024];
		ProxyBuildIdText(self, id, sizeof(id));
		in.patch = id;
	}
	{
		ProxyReport::Hashed h;
		h.label = "proxy";
		char p[MAX_PATH * 2] = {};
		GetModuleFileNameA(self, p, sizeof(p));
		h.path = p;
		in.hashes.push_back(h);
	}
	if (s.iniFound) in.copies.push_back(s.iniPath);
	{
		wchar_t opt[1024];
		if (Vp64Opt::DefaultPath(opt, sizeof(opt) / sizeof(opt[0])) != Vp64Opt::kPathNone) in.copiesW.push_back(opt);
	}
	in.tails.push_back(FindLogFile());
	in.session.push_back(option4 ? "the pacing option 4 of D165 (ticket 4675) failed a check; the 64-bit path itself is not "
	                               "affected and the game goes on with the EXE's own order for its visual events"
	                     : option2 ? "the pacing option 2 of D165 (ticket 4665) failed a check; the 64-bit path itself is not "
	                                 "affected and the game goes on with the EXE's own wait for its visual events"
	                               : "the pacing option of D165 (ticket 4662) failed a check; the 64-bit path itself is not "
	                                 "affected and the game goes on with the EXE's own update tick");
	std::wstring root, dir;
	std::vector<std::string> lines;
	if (!ProxyReport::Root(&root))
		lines.push_back("report: FAILED: the Documents folder could not be found; no report");
	else
		ProxyReport::Write(in, root, &dir, &lines);
	for (size_t i = 0; i < lines.size(); ++i) ProxyRtInt::Log("pacing: %s", lines[i].c_str());
	if (!boot)
	{
		ProxyRtInt::Log("pacing: no popup for a failure during play (MC #3503)");
		return;
	}
	if (dir.empty()) dir = root;
	std::string helperLine;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(s.proxyDir),
	                                               ProxySession::VerbFor(false, GetCurrentProcessId()), dir,
	                                               s.iniFound ? ProxySession::Widen(s.iniPath) : std::wstring(),
	                                               &helperLine);
	ProxyRtInt::Log("pacing: %s%s", started ? "" : "the popup FAILED to start: ", ProxyReport::Scrub(helperLine).c_str());
	ProxyRtInt::LogFlush();
}

ProxyPacing::Config g_config;
ProxyPacing::SkipConfig g_skConfig;
bool g_configured = false;
double g_lastCounters = 0.0;

// Option 2's scan (ticket 4665): what the frame's gate computes, through the same DLL interface the EXE calls (the
// proxy's wrappers: canonical handles and the epoch mirror answer what they can; right after an Update the mirror is
// stale, so the getters cross once and fill it for the frame's own scan). The first player in slots 0-63 with
// IsEverAlive and IsTurnActive (ICvPlayer1 slots 21 and 22, the frame's two calls), the active player, and IsHuman
// only when they are the same player.
bool GlueScanInner(ProxyPacing::ScanResult* out)
{
	ICvGameContext1* ctx = DllGetGameContext();
	if (!ctx) return false;
	ICvGame1* g = ctx->GetGame();
	++out->calls;
	if (!g) return false;
	out->active = (int)g->GetActivePlayer();
	++out->calls;
	for (int i = 0; i < ProxyPacing::kSlots; ++i)
	{
		ICvPlayer1* pl = g->GetPlayer((PlayerTypes)i);
		++out->calls;
		if (!pl) continue;
		bool hit = pl->IsEverAlive();
		++out->calls;
		if (hit)
		{
			hit = pl->IsTurnActive();
			++out->calls;
		}
		if (hit && i == out->active)
		{
			out->activeHuman = pl->IsHuman();
			++out->calls;
		}
		pl->Destroy();
		if (hit)
		{
			out->first = i;
			break;
		}
	}
	g->Destroy();
	return true;
}

bool GlueScan(ProxyPacing::ScanResult* out)
{
	try
	{
		return GlueScanInner(out);
	}
	catch (...)
	{
		return false;
	}
}

// Option 4's game settings (ticket 4675): the DLL's own CvPreGame::quickCombat() and quickMovement() (ICvPreGame1 slots
// 84 and 85; what decides whether the DLL posts combats and animated moves or teleports), asked at an end-of-phase drain;
// since D246 (ticket 0718) once after every AI Update that reaches option 2's decision (options 2 and 4 act only with both on).
bool GlueQuickInner(bool* quickCombat, bool* quickMovement)
{
	ICvGameContext1* ctx = DllGetGameContext();
	if (!ctx) return false;
	ICvPreGame1* pg = ctx->GetPreGame();
	if (!pg) return false;
	*quickCombat = pg->quickCombat();
	*quickMovement = pg->quickMovement();
	pg->Destroy();
	return true;
}

bool GlueQuick(bool* quickCombat, bool* quickMovement)
{
	try
	{
		return GlueQuickInner(quickCombat, quickMovement);
	}
	catch (...)
	{
		return false;
	}
}

// TEST only (MC #4203 (a)): VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1 lets option 4 act with Quick Movement off, for the
// DIAGNOSTIC pictures that decide whether the Quick Movement guard stays. Logged in option 4's install line.
bool DrainAnyMovementTest()
{
	char v[8];
	const DWORD n = GetEnvironmentVariableA("VP64_PACING_TEST_DRAIN_ANY_MOVEMENT", v, sizeof(v));
	return n == 1 && v[0] == '1';
}
}  // namespace

void ProxyPacing::OnUpdate()
{
	if (!g_configured)
	{
		g_configured = true;
		SetSinks(GlueLog, GlueReport);
		g_config.base = reinterpret_cast<ULONG_PTR>(GetModuleHandleA(NULL));
		g_skConfig.base = g_config.base;
		g_skConfig.on = ProxyRtInt::ProxyOptions().AiSkipVisualDrain();
		g_skConfig.headless = ProxyIniSettings().singleThreadHost;
		g_skConfig.drain = ProxyRtInt::ProxyOptions().AiDrainUnordered();  // ticket 4675 (option 4)
		g_skConfig.drainAnyMovement = g_skConfig.drain == 1 && DrainAnyMovementTest();
		g_config.tickMs = ProxyRtInt::ProxyOptions().AiUpdateTickMs();
		g_config.mainThread = 0;
		if (g_config.tickMs >= 0)
		{
			g_config.mainThread = FindMainThread();
			ProxyRtInt::Log("pacing: [Performance] AiUpdateTickMs = %d; the EXE's main thread (the process's first) is %lu; "
			                "this Update's thread is %lu", g_config.tickMs, (unsigned long)g_config.mainThread,
			                (unsigned long)GetCurrentThreadId());
		}
	}
	g_config.object = ProxyRtInt::EngineUtility();
	Update(g_config);
	SkipBefore(g_skConfig);  // ticket 4665 (option 2): off = nothing (decided once per load)
	if (CurrentState() == kActive || SkipState() == kActive)
	{
		const double now = ProxyRtInt::Now();
		if (now - g_lastCounters >= 60.0)
		{
			g_lastCounters = now;
			char line[700];  // ticket 0718
			if (CurrentState() == kActive)
			{
				CountersLine(line, sizeof(line));
				ProxyRtInt::Log("%s", line);
			}
			if (SkipState() == kActive)
			{
				SkipCountersLine(line, sizeof(line));
				ProxyRtInt::Log("%s", line);
			}
			if (DrainState() == kActive)
			{
				char dl[500];
				DrainCountersLine(dl, sizeof(dl));
				ProxyRtInt::Log("%s", dl);
			}
			if (SkipState() == kActive && g_skConfig.on == 2)
			{
				char hl[700];
				HoldCountersLine(hl, sizeof(hl));
				ProxyRtInt::Log("%s", hl);
			}
		}
	}
}

void ProxyPacing::OnAfterUpdate()
{
	if (SkipState() != kActive) return;  // off, not installed or stopped: nothing read
	SkipAfter(GlueScan, GlueQuick);
}

bool ProxyPacing::OnUnload(char* line, size_t cap)
{
	if (g_configured && (g_skConfig.on == 1 || g_skConfig.on == 2))
	{
		char sk[700];  // ticket 0718
		SkipCountersLine(sk, sizeof(sk));
		ProxyRtInt::Log("detach: %s", sk);
	}
	if (g_configured && g_skConfig.on == 2)
	{
		char hl[700];
		HoldCountersLine(hl, sizeof(hl));
		ProxyRtInt::Log("detach: %s", hl);
	}
	if (g_configured && g_skConfig.drain == 1)
	{
		// Ticket 4675: queue B's ordering byte back to 1 if an unload comes during an end-of-phase drain.
		char dr[700];
		if (DrainArmed())
			DrainRestore("unload", dr, sizeof(dr));
		else
			DrainCountersLine(dr, sizeof(dr));
		ProxyRtInt::Log("detach: %s", dr);
	}
	return Restore("unload", line, cap);
}
#endif  // VP_PACING_OFFLINE
