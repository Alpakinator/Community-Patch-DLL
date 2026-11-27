// proxy_uidraw.cpp - the growth of the EXE's UI draw-list buffers on the product route (ticket 0487). See
// proxy_uidraw.h. Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Origin (COMMON rule 13, decision D22): the code patterns, the cross-checks, the allocator arguments and the DX11
// two-step swap are those of upstream pull request LoneGazebo/Community-Patch-DLL#13406 "Grow the engine's UI draw
// buffers (Prevent crashes)" by vaderkos (GPL-3.0, like this repository), head commit 8cfa46a83 (read 2026-11-17):
//   CvGameCoreDLL_Expansion2/Exe/CvExeAddresses.h  -> kPatterns, kChecks (the DX11 column only)
//   CvGameCoreDLL_Expansion2/Exe/CvExeApi.cpp      -> CallAlloc (heap 9, tags 0xC / 0xE, "as CCommandStream calls them")
//   CvGameCoreDLL_Expansion2/Exe/CvExe.cpp         -> StartGrowth / Step (re-written here; same behaviour)
// The check semantics (abs32, rel32, val8, val32) are those of upstream's resolver, CvExeSymbols.cpp at 6c80fa023
// (pull request #13379). Nothing here was taken from this project's own dumps of the EXE (D83). Only data is written
// (the stream objects' fields on the EXE's heap); no EXE code byte is read beyond the patterns or written (D4).
// Toolchain: clang-cl -m32, VC9 headers. x86 only (pointers are DWORDs).
#include "proxy_uidraw.h"
#include "proxy_exe.h"  // ticket 4963: the build table
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
using namespace ProxyUiDraw;
using ProxyUiDraw::Test::Pattern;
using ProxyUiDraw::Test::CrossCheck;

// ================================================================================================ the EXE layout
// Ticket 4963 (epic 0590): the build from the image's PE stamp and SizeOfImage, every address from the build table
// (exebuild/symbols/proxy.tsv; #13406's CvExeAddresses.h DX11 column gave the anchors): uid_create_streams
// (UIManager_StartupRenderer_CreateCommandStreams), uid_read_uimanager (UIManager_StartupRenderer_ReadUIManager),
// uid_uimanager (g_pUIManager, data), uid_cstream_ctor / uid_cstream_dtor (CCommandStream, DX11; the destructor frees
// min(current, other) of each pair), uid_cstream_clear (ClearStream), uid_new_array (operator new[]),
// uid_malloc_aligned (FireMallocAlignedNoTracking), uid_delete_array and uid_free_aligned (no pattern: the destructor's
// calls vouch for them).
const DWORD kImageBase = VPEXE_IMAGE_BASE;

// The UI manager's stream pointers and the stream's fields.
const DWORD kOffStream = 0x6f0, kOffStreamLeft = 0x6f4, kOffStreamRight = 0x6f8;
const DWORD kOffSetCount = 0x04, kOffCommandCap = 0x08, kOffCommandCount = 0x0c, kOffCommands = 0x14, kOffData = 0x24,
            kOffDataSize = 0x34, kOffDataUsed = 0x40, kOffDataOther = 0x2068, kOffCommandsOther = 0x2070,
            kOffDataUsedOther = 0x2074;
const DWORD kStreamBytes = kOffDataUsedOther + 4;

// The code patterns: one X row per pattern (symbol, name, bytes; exebuild/patterns.txt lists this file, so tools/exebuild/
// gen_table.py checks every row on every build it generates; "??" = any byte, no R() operand: the cross-checks below
// hold the operands). The bytes are #13406's.
#define VP_UIDRAW_PATTERNS(X) \
	X(uid_create_streams, "UIManager_StartupRenderer_CreateCommandStreams", "83 3D ?? ?? ?? ?? 00 8B 0D ?? ?? ?? ?? 6A 00 68 00 00 10 00 68 00 04 00 00 68 00 10 00 00 74 29 E8 ?? ?? ?? ?? 6A 00 68 00 00 10 00 68 00 04 00 00 89 86 ?? ?? 00 00 8B 0D ?? ?? ?? ?? 68 00 10 00 00 E8 ?? ?? ?? ?? EB 0B E8 ?? ?? ?? ?? 89 86 ?? ?? 00 00 8D 8C 24 38 03 00 00 68 ?? ?? ?? ?? 51 89 86 ?? ?? 00 00 89 86 ?? ?? 00 00") \
	X(uid_read_uimanager, "UIManager_StartupRenderer_ReadUIManager", "8B 15 ?? ?? ?? ?? 8B 8A ?? ?? 00 00 51 8B 0D ?? ?? ?? ?? 50 E8") \
	X(uid_cstream_ctor, "CCommandStream_ctor_DX11", "8B 44 24 04 53 56 57 8B 7C 24 18 ?? ?? 8B 4C 24 14 89 4E 10 89 46 08 ?? ?? 83 C7 0F ?? ?? 83 E7 F0 ?? ?? BA 20 00 00 00 F7 E2 6A 0C 0F 90 C1 6A 09 6A 18 68 ?? ?? ?? ?? 6A 01 89 5E 40 89 7E 34 F7 D9 ?? ?? 51 E8 ?? ?? ?? ?? 89 46 14") \
	X(uid_cstream_dtor, "CCommandStream_dtor_DX11", "56 ?? ?? 8B 86 70 20 00 00 8B 4E 14 ?? ?? 72 02 ?? ?? 50 E8 ?? ?? ?? ?? 8B 06 8B 8E 6C 20 00 00 83 C4 04 ?? ?? 72 02 ?? ?? 50 E8 ?? ?? ?? ?? 8B 86 68 20 00 00 8B 4E 24 83 C4 04 ?? ?? 72 02 ?? ?? 50 E8 ?? ?? ?? ??") \
	X(uid_cstream_clear, "CCommandStream_ClearStream", "?? ?? 89 41 0C 89 41 04 89 41 40 89 41 44 89 41 48 89 41 30 89 41 54") \
	X(uid_new_array, "operator_new_array", "56 8B 74 24 08 57 85 F6 75 05 BE 01 00 00 00 6A 01 E8 ?? ?? ?? ??") \
	X(uid_malloc_aligned, "FireMallocAlignedNoTracking", "8B 44 24 08 85 C0 74 0F 50 8B 44 24 08 50 E8 ?? ?? ?? ?? 83 C4 08 C3 8B 4C 24 04 6A 00 51 FF 15 ?? ?? ?? ?? 83 C4 08 C3")
struct PatternRow
{
	const char* name;
	int sym;
	const char* bytes;
};
#define VP_UIDRAW_ROW(sym, name, bytes) {name, VPX_##sym, bytes},
const PatternRow kPatternRows[] = {VP_UIDRAW_PATTERNS(VP_UIDRAW_ROW)};
#undef VP_UIDRAW_ROW
const size_t kPatternCount = sizeof(kPatternRows) / sizeof(kPatternRows[0]);

enum { kAbs32 = 0, kRel32 = 1, kVal8 = 2, kVal32 = 3 };
// #13406's EXE_CHECK rows for DX11, as (symbol + offset, target): the target is a symbol (abs32, rel32) or a value.
struct CheckRow
{
	int kind;
	int sym;
	DWORD off;
	int targetSym;   // abs32 / rel32; -1 for a value
	DWORD value;     // val8 / val32
};
const CheckRow kCheckRows[] = {
	{kAbs32, VPX_uid_read_uimanager, 0x02, VPX_uid_uimanager, 0},
	{kVal32, VPX_uid_create_streams, 0x50, -1, kOffStreamLeft},
	{kVal32, VPX_uid_create_streams, 0x63, -1, kOffStreamRight},
	{kVal32, VPX_uid_create_streams, 0x69, -1, kOffStream},
	{kVal8, VPX_uid_cstream_ctor, 0x16, -1, kOffCommandCap},
	{kVal8, VPX_uid_cstream_ctor, 0x3F, -1, kOffDataSize},
	{kRel32, VPX_uid_cstream_ctor, 0x45, VPX_uid_new_array, 0},
	{kVal8, VPX_uid_cstream_ctor, 0x4C, -1, kOffCommands},
	{kRel32, VPX_uid_cstream_ctor, 0x90, VPX_uid_malloc_aligned, 0},
	{kVal8, VPX_uid_cstream_ctor, 0x9A, -1, kOffData},
	{kVal32, VPX_uid_cstream_ctor, 0x9F, -1, kOffDataOther},
	{kVal32, VPX_uid_cstream_ctor, 0xDF, -1, kOffDataUsedOther},
	{kVal32, VPX_uid_cstream_dtor, 0x05, -1, kOffCommandsOther},
	{kVal8, VPX_uid_cstream_dtor, 0x0B, -1, kOffCommands},
	{kRel32, VPX_uid_cstream_dtor, 0x13, VPX_uid_delete_array, 0},
	{kRel32, VPX_uid_cstream_dtor, 0x2A, VPX_uid_delete_array, 0},
	{kVal32, VPX_uid_cstream_dtor, 0x31, -1, kOffDataOther},
	{kVal8, VPX_uid_cstream_dtor, 0x37, -1, kOffData},
	{kRel32, VPX_uid_cstream_dtor, 0x42, VPX_uid_free_aligned, 0},
	{kVal8, VPX_uid_cstream_clear, 0x04, -1, kOffCommandCount},
	{kVal8, VPX_uid_cstream_clear, 0x07, -1, kOffSetCount},
	{kVal8, VPX_uid_cstream_clear, 0x0A, -1, kOffDataUsed},
};
const size_t kCheckCount = sizeof(kCheckRows) / sizeof(kCheckRows[0]);

// The layout resolved for one build (Resolve): the patterns and cross-checks at that build's VAs.
Pattern g_pat[kPatternCount];
CrossCheck g_chk[kCheckCount];
DWORD g_vaUiManager = 0, g_vaNewArray = 0, g_vaMallocAligned = 0, g_vaDeleteArray = 0, g_vaFreeAligned = 0;
int g_layoutBuild = -2;

// Fills the layout of the build from its table; -1 = done, else the symbol it does not carry.
int Resolve(int build)
{
	const int syms[] = {VPX_uid_create_streams, VPX_uid_read_uimanager, VPX_uid_uimanager, VPX_uid_cstream_ctor,
	                    VPX_uid_cstream_dtor, VPX_uid_cstream_clear, VPX_uid_new_array, VPX_uid_malloc_aligned,
	                    VPX_uid_delete_array, VPX_uid_free_aligned};
	for (size_t i = 0; i < sizeof(syms) / sizeof(syms[0]); ++i)
		if (!ProxyExe::Va(build, syms[i])) return syms[i];
	for (size_t i = 0; i < kPatternCount; ++i)
	{
		g_pat[i].name = kPatternRows[i].name;
		g_pat[i].va = ProxyExe::Va(build, kPatternRows[i].sym);
		g_pat[i].bytes = kPatternRows[i].bytes;
	}
	for (size_t i = 0; i < kCheckCount; ++i)
	{
		const CheckRow& r = kCheckRows[i];
		g_chk[i].kind = r.kind;
		g_chk[i].va = ProxyExe::Va(build, r.sym) + r.off;
		g_chk[i].target = r.targetSym >= 0 ? ProxyExe::Va(build, r.targetSym) : r.value;
	}
	g_vaUiManager = ProxyExe::Va(build, VPX_uid_uimanager);
	g_vaNewArray = ProxyExe::Va(build, VPX_uid_new_array);
	g_vaMallocAligned = ProxyExe::Va(build, VPX_uid_malloc_aligned);
	g_vaDeleteArray = ProxyExe::Va(build, VPX_uid_delete_array);
	g_vaFreeAligned = ProxyExe::Va(build, VPX_uid_free_aligned);
	g_layoutBuild = build;
	return -1;
}

// The allocators' arguments (#13406's CvExeApi.cpp: "as CCommandStream calls them").
const int kFireHeap = 9, kTagCommands = 0xc, kTagData = 0xe;
const DWORD kDataAlign = 0x10;
const DWORD kNewDataSize = kGrowth * kStockDataSize;                 // 4 MB per half
const DWORD kNewCommandCap = kGrowth * kStockCommandCap;             // 16,384 per half
const DWORD kNewCommandsSize = kNewCommandCap * kCommandSize;        // 512 KB per half

// ================================================================================================ state
Hooks g_hooks = {NULL, NULL, NULL};
volatile LONG g_enabled = 0;
volatile LONG g_state = Test::kOff;
char g_why[300] = "";
DWORD g_base = 0;                       // the EXE image (0 = not checked yet)
bool g_checked = false;                 // main thread only from here down
Test::Allocators g_alloc = {NULL, NULL, NULL, NULL};

struct Growth                           // a stream between its two steps
{
	BYTE* stream;
	DWORD data, commands;               // the new blocks (two halves each)
	DWORD oldData, oldCommands;         // the lower pointer of each old pair (what the EXE allocated)
	DWORD frames;                       // frames since it started
};
Growth g_growth[3];
BYTE* g_grownStreams[8];                // streams this process grew (to tell a re-created stream from a first one)
size_t g_grownCount = 0;
volatile LONG g_grown = 0;
volatile LONG g_regrowths = 0;
volatile LONG g_reports = 0;
DWORD g_pendingFrames = 0;              // consecutive frames with a stream not grown
volatile LONG g_dataHigh = 0;           // the last frame's data use, high-water (bytes)
volatile LONG g_dataCap = 0;            // the data capacity at the high-water's last read
int g_levelLogged = 0;                  // 0, 10, ..., 100: the last usage level logged
DWORD g_lastUsageMs = 0;
volatile LONG g_frames = 0;
volatile LONG g_testFail = 0;           // FailForTest (TEST ONLY)
char g_testWhy[200] = "";

// ================================================================================================ helpers
void Logf(const char* fmt, ...)
{
	if (!g_hooks.log) return;
	char body[700];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
	va_end(ap);
	char line[720];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "uidraw: %s", body);
	g_hooks.log(line);
}

DWORD NowMs()
{
	return g_hooks.now ? (DWORD)g_hooks.now() : GetTickCount();
}

int HexNibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// True if [addr, addr + n) lies in committed memory that is readable (and writable if asked); proxy_shell.cpp's rule.
bool IsAccessible(DWORD addr, DWORD n, bool write)
{
	if (addr < 0x10000 || addr + n < addr) return false;
	DWORD at = addr;
	const DWORD end = addr + n;
	while (at < end)
	{
		MEMORY_BASIC_INFORMATION mi;
		if (VirtualQuery(reinterpret_cast<LPCVOID>(at), &mi, sizeof(mi)) != sizeof(mi)) return false;
		if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
		                       PAGE_EXECUTE_WRITECOPY;
		const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((mi.Protect & (write ? writable : readable)) == 0) return false;
		const DWORD regionEnd = reinterpret_cast<DWORD>(mi.BaseAddress) + static_cast<DWORD>(mi.RegionSize);
		if (regionEnd <= at) return false;
		at = regionEnd;
	}
	return true;
}

// The pattern's length in bytes, or 0 when it is malformed.
DWORD PatternLength(const char* p)
{
	DWORD n = 0;
	while (*p)
	{
		if (*p == ' ') { ++p; continue; }
		if (!p[1]) return 0;
		if (!(p[0] == '?' && p[1] == '?') && (HexNibble(p[0]) < 0 || HexNibble(p[1]) < 0)) return 0;
		++n;
		p += 2;
	}
	return n;
}

// The first byte that differs (its offset into *at), or -1 when the bytes match the pattern.
int Mismatch(const unsigned char* code, const char* p)
{
	int off = 0;
	while (*p)
	{
		if (*p == ' ') { ++p; continue; }
		if (!(p[0] == '?' && p[1] == '?') && code[off] != (unsigned char)(HexNibble(p[0]) * 16 + HexNibble(p[1])))
			return off;
		++off;
		p += 2;
	}
	return -1;
}

DWORD Rt(DWORD va) { return g_base + (va - kImageBase); }   // a VA at the image's place in this process

void SetWhy(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(g_why, sizeof(g_why), _TRUNCATE, fmt, ap);
	va_end(ap);
}

// The checks of the image at base. NULL = passed; else the reason (in g_why). *loud = false only for another build.
const char* Verify(DWORD base, bool* loud)
{
	*loud = true;
	if (!IsAccessible(base, sizeof(IMAGE_DOS_HEADER), false)) { *loud = false; SetWhy("off: no EXE image"); return g_why; }
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000000 ||
	    !IsAccessible(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS), false))
	{
		*loud = false;
		SetWhy("off: no EXE image");
		return g_why;
	}
	const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) { *loud = false; SetWhy("off: no EXE image"); return g_why; }
	// ticket 4963: the build from the registry (stamp and SizeOfImage), the layout from its table
	const int build = VpExeBuildFromHeader(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
	if (build == kVpExeBuildUnknown)
	{
		*loud = false;
		SetWhy("off: not an EXE build the patch knows (PE timestamp 0x%08lX, SizeOfImage 0x%08lX)",
		       (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.SizeOfImage);
		return g_why;
	}
	const int missing = Resolve(build);
	if (missing >= 0)
	{
		char line[200];
		SetWhy("EXE check: %s", ProxyExe::NotCarried(line, sizeof(line), "", build, missing));
		return g_why;
	}
	const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
	g_base = base;
	if (g_vaUiManager - kImageBase + 4 > imageSize || !IsAccessible(Rt(g_vaUiManager), 4, false))
	{
		SetWhy("EXE check: g_pUIManager (VA 0x%08lX) is not readable in the image", (unsigned long)g_vaUiManager);
		return g_why;
	}
	for (size_t i = 0; i < kPatternCount; ++i)
	{
		const Pattern& p = g_pat[i];
		const DWORD len = PatternLength(p.bytes);
		if (!len || p.va - kImageBase + len > imageSize || !IsAccessible(Rt(p.va), len, false))
		{
			SetWhy("EXE check: %s (VA 0x%08lX) is not readable", p.name, p.va);
			return g_why;
		}
		const int at = Mismatch(reinterpret_cast<const unsigned char*>(Rt(p.va)), p.bytes);
		if (at >= 0)
		{
			SetWhy("EXE check: the code of %s (VA 0x%08lX) differs from the known layout at +0x%X", p.name, p.va, at);
			return g_why;
		}
	}
	for (size_t i = 0; i < kCheckCount; ++i)
	{
		const CrossCheck& c = g_chk[i];
		const DWORD n = c.kind == kRel32 ? 5 : (c.kind == kVal8 ? 1 : 4);
		if (c.va - kImageBase + n > imageSize || !IsAccessible(Rt(c.va), n, false))
		{
			SetWhy("EXE check: cross-check %u at VA 0x%08lX is not readable", (unsigned)i, c.va);
			return g_why;
		}
		const unsigned char* b = reinterpret_cast<const unsigned char*>(Rt(c.va));
		DWORD v = 0;
		bool ok = false;
		switch (c.kind)
		{
		case kAbs32:   // the operand is an address in this process
			memcpy(&v, b, 4);
			ok = v == Rt(c.target);
			break;
		case kRel32:   // a call or jmp whose target is c.target (rel32 is position independent: compare VAs)
		{
			LONG rel = 0;
			memcpy(&rel, b + 1, 4);
			v = c.va + 5 + (DWORD)rel;
			ok = (b[0] == 0xE8 || b[0] == 0xE9) && v == c.target;
			break;
		}
		case kVal8:
			v = b[0];
			ok = v == c.target;
			break;
		default:
			memcpy(&v, b, 4);
			ok = v == c.target;
			break;
		}
		if (!ok)
		{
			SetWhy("EXE check: cross-check %u at VA 0x%08lX reads 0x%lX, expected 0x%lX", (unsigned)i, c.va,
			       (unsigned long)v, c.kind == kAbs32 ? (unsigned long)Rt(c.target) : (unsigned long)c.target);
			return g_why;
		}
	}
	return NULL;
}

DWORD& Field(BYTE* s, DWORD off) { return *reinterpret_cast<DWORD*>(s + off); }

bool IsGrown(BYTE* s)
{
	return Field(s, kOffDataSize) >= kNewDataSize && Field(s, kOffCommandCap) >= kNewCommandCap;
}

// Between frames the current half is empty and nothing reads it (#13406).
bool CurrentHalfFree(BYTE* s)
{
	return Field(s, kOffDataUsed) == 0 && Field(s, kOffCommandCount) == 0 && Field(s, kOffSetCount) == 0;
}

bool IsOneBlock(DWORD a, DWORD b, DWORD half)
{
	if (!a || !b) return false;
	return a + half == b || b + half == a;
}

DWORD Lower(DWORD a, DWORD b) { return a < b ? a : b; }

// At a failure: a growth in flight whose stream uses neither new block yet gives its new blocks back; one that has
// already swapped a half keeps them (the stream reads from them; the EXE's destructor frees what it frees at exit).
void AbandonGrowths()
{
	for (size_t i = 0; i < sizeof(g_growth) / sizeof(g_growth[0]); ++i)
	{
		Growth& g = g_growth[i];
		if (!g.stream) continue;
		bool inUse = true;
		if (IsAccessible((DWORD)g.stream, kStreamBytes, false))
		{
			const DWORD d = Field(g.stream, kOffData), o = Field(g.stream, kOffDataOther);
			inUse = (d >= g.data && d < g.data + 2 * kNewDataSize) || (o >= g.data && o < g.data + 2 * kNewDataSize);
		}
		if (!inUse && g_alloc.freeAligned && g_alloc.deleteArray)
		{
			g_alloc.freeAligned(reinterpret_cast<void*>(g.data));
			g_alloc.deleteArray(reinterpret_cast<void*>(g.commands));
		}
		Logf("stream 0x%08lX: the growth in flight is abandoned; its new blocks are %s", (DWORD)g.stream,
		     inUse ? "kept (one half already uses them)" : "freed");
		memset(&g, 0, sizeof(g));
	}
}

// The first failure: off for the process, one FAILED line, the report hook once.
void Fail(const char* code, const char* fmt, ...)
{
	char detail[400];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(detail, sizeof(detail), _TRUNCATE, fmt, ap);
	va_end(ap);
	if (g_state == Test::kFailed) return;
	_snprintf_s(g_why, sizeof(g_why), _TRUNCATE, "%s: %s", code, detail);
	InterlockedExchange(&g_state, Test::kFailed);
	AbandonGrowths();
	Logf("FAILED: %s; the EXE keeps its stock 1 MB / 4,096-command UI draw buffers (more than about 450 unit flags in "
	     "view can crash the game)", g_why);
	if (g_hooks.report && InterlockedIncrement(&g_reports) == 1) g_hooks.report(code, g_why);
}

Growth* FindGrowth(BYTE* s)
{
	for (size_t i = 0; i < sizeof(g_growth) / sizeof(g_growth[0]); ++i)
		if (g_growth[i].stream == s) return &g_growth[i];
	return NULL;
}

bool WasGrown(BYTE* s)
{
	for (size_t i = 0; i < g_grownCount; ++i)
		if (g_grownStreams[i] == s) return true;
	return false;
}

void NoteGrown(BYTE* s)
{
	if (WasGrown(s)) return;
	if (g_grownCount < sizeof(g_grownStreams) / sizeof(g_grownStreams[0])) g_grownStreams[g_grownCount++] = s;
}

void* CallAlloc(Test::AllocFn fn, DWORD size, DWORD align, int tag)
{
	return fn(size, align, "proxy_uidraw.cpp", __LINE__, kFireHeap, tag);
}

// Starts a stream's growth: the layout check and both allocations. NULL after a Fail.
Growth* StartGrowth(BYTE* s)
{
	const DWORD dataSize = Field(s, kOffDataSize), cap = Field(s, kOffCommandCap);
	const DWORD data = Field(s, kOffData), dataOther = Field(s, kOffDataOther);
	const DWORD cmds = Field(s, kOffCommands), cmdsOther = Field(s, kOffCommandsOther);
	if (!IsOneBlock(data, dataOther, dataSize) || !IsOneBlock(cmds, cmdsOther, cap * kCommandSize))
	{
		Fail("layout", "stream 0x%08lX: the halves are not one block each (data 0x%08lX / 0x%08lX of %lu B, commands "
		     "0x%08lX / 0x%08lX of %lu x 32 B)", (DWORD)s, data, dataOther, dataSize, cmds, cmdsOther, cap);
		return NULL;
	}
	Growth* g = FindGrowth(NULL);
	if (!g)
	{
		Fail("layout", "stream 0x%08lX: more than three streams growing at once", (DWORD)s);
		return NULL;
	}
	void* newData = CallAlloc(g_alloc.mallocAligned, 2 * kNewDataSize, kDataAlign, kTagData);
	void* newCmds = CallAlloc(g_alloc.newArray, 2 * kNewCommandsSize, 1, kTagCommands);
	if (!newData || !newCmds || ((DWORD)newData & (kDataAlign - 1)) != 0)
	{
		const DWORD d = (DWORD)newData, c = (DWORD)newCmds;
		if (newData) g_alloc.freeAligned(newData);
		if (newCmds) g_alloc.deleteArray(newCmds);
		Fail("allocation", "stream 0x%08lX: the EXE's allocators returned data 0x%08lX (%lu B, 16-aligned) and commands "
		     "0x%08lX (%lu B)", (DWORD)s, d, (DWORD)(2 * kNewDataSize), c, (DWORD)(2 * kNewCommandsSize));
		return NULL;
	}
	g->stream = s;
	g->data = (DWORD)newData;
	g->commands = (DWORD)newCmds;
	g->oldData = Lower(data, dataOther);
	g->oldCommands = Lower(cmds, cmdsOther);
	g->frames = 0;
	Logf("stream 0x%08lX: growth started: data 0x%08lX (2 x %lu B), commands 0x%08lX (2 x %lu x 32 B); old data 0x%08lX "
	     "(2 x %lu B), old commands 0x%08lX (2 x %lu x 32 B)", (DWORD)s, g->data, kNewDataSize, g->commands,
	     kNewCommandCap, g->oldData, dataSize, g->oldCommands, cap);
	return g;
}

// One frame of a stream: true when it is grown (now or before).
bool Step(BYTE* s)
{
	if (IsGrown(s)) return true;
	Growth* g = FindGrowth(s);
	if (!g)
	{
		if (WasGrown(s) || (g_grown > 0 && g_state == Test::kGrown))
		{
			const LONG n = InterlockedIncrement(&g_regrowths);
			Logf("stream 0x%08lX is at %lu B / %lu commands again (the renderer made its streams again); growing it again "
			     "(%ld of at most %d)", (DWORD)s, Field(s, kOffDataSize), Field(s, kOffCommandCap), n, (int)kMaxRegrowths);
			if (n > kMaxRegrowths)
			{
				Fail("regrowth", "the streams went back to the stock sizes %ld times", n);
				return false;
			}
		}
		g = StartGrowth(s);
		if (!g) return false;
	}
	++g->frames;
	if (!CurrentHalfFree(s)) return false;
	const DWORD newData[2] = {g->data, g->data + kNewDataSize};
	const DWORD newCmds[2] = {g->commands, g->commands + kNewCommandsSize};
	DWORD& data = Field(s, kOffData);
	DWORD& cmds = Field(s, kOffCommands);
	const DWORD dataOther = Field(s, kOffDataOther);
	if (data != newData[0] && data != newData[1])
	{
		const int half = dataOther == newData[0] ? 1 : 0;
		data = newData[half];
		cmds = newCmds[half];
	}
	if (dataOther != newData[0] && dataOther != newData[1]) return false;   // the other half follows after the swap
	// Both halves are new: nothing refers to the old blocks any more.
	Field(s, kOffDataSize) = kNewDataSize;
	Field(s, kOffCommandCap) = kNewCommandCap;
	g_alloc.freeAligned(reinterpret_cast<void*>(g->oldData));
	g_alloc.deleteArray(reinterpret_cast<void*>(g->oldCommands));
	const LONG n = InterlockedIncrement(&g_grown);
	Logf("grown: stream 0x%08lX, %lu B, %lu commands, after %lu frames (growth %ld this process)", (DWORD)s, kNewDataSize,
	     kNewCommandCap, g->frames, n);
	NoteGrown(s);
	memset(g, 0, sizeof(*g));
	return true;
}

// The usage of the stream in use: the last frame's data, high-water, levels.
void Usage(BYTE* s)
{
	const DWORD used = Field(s, kOffDataUsedOther), cap = Field(s, kOffDataSize);
	if ((LONG)used > g_dataHigh) InterlockedExchange(&g_dataHigh, (LONG)used);
	InterlockedExchange(&g_dataCap, (LONG)cap);
	const DWORD t = NowMs();
	if (t - g_lastUsageMs < 1000) return;
	g_lastUsageMs = t;
	if (!cap) return;
	const DWORD pct = (DWORD)(((unsigned __int64)(DWORD)g_dataHigh * 100) / cap);
	const int level = pct >= 100 ? 100 : (int)(pct / 10) * 10;   // every 10 % step from 10 % on
	if (level > g_levelLogged)
	{
		g_levelLogged = level;
		Logf("usage: the UI draw data reached %lu %% of its %lu B (high-water %lu B, last frame %lu B, command count now "
		     "%lu of %lu)%s", pct, cap, (DWORD)g_dataHigh, used, Field(s, kOffCommandCount), Field(s, kOffCommandCap),
		     level >= 90 ? "; LOUD: close to the limit" : "");
	}
}

void FrameBody()
{
	if (!g_checked)
	{
		g_checked = true;
		if (!g_alloc.newArray)
		{
			const DWORD base = (DWORD)GetModuleHandleW(NULL);
			bool loud = true;
			if (Verify(base, &loud))
			{
				if (loud)
					Fail("exe-check", "%s", g_why);
				else
				{
					InterlockedExchange(&g_state, Test::kOff);
					Logf("%s; nothing is grown", g_why);
				}
				return;
			}
			g_alloc.newArray = reinterpret_cast<Test::AllocFn>(Rt(g_vaNewArray));
			g_alloc.mallocAligned = reinterpret_cast<Test::AllocFn>(Rt(g_vaMallocAligned));
			g_alloc.deleteArray = reinterpret_cast<Test::FreeFn>(Rt(g_vaDeleteArray));
			g_alloc.freeAligned = reinterpret_cast<Test::FreeFn>(Rt(g_vaFreeAligned));
			Logf("EXE checks passed (%s, %u code patterns, %u cross-checks; #13406's DX11 layout); growing the UI "
			     "draw buffers to %lu B and %lu commands per half at the next frames", VpExeBuildLabel(g_layoutBuild), (unsigned)kPatternCount,
			     (unsigned)kCheckCount, kNewDataSize, kNewCommandCap);
		}
		InterlockedExchange(&g_state, Test::kWaiting);
	}
	if (g_state != Test::kWaiting && g_state != Test::kGrowing && g_state != Test::kGrown) return;
	InterlockedIncrement(&g_frames);
	if (g_testFail)
	{
		Fail("test", "TEST ONLY (VP64_UIDRAW_TEST_FAIL): %s", g_testWhy);
		return;
	}
	const DWORD mgr = *reinterpret_cast<const DWORD*>(Rt(g_vaUiManager));
	BYTE* streams[3];
	size_t count = 0;
	bool ready = mgr != 0 && IsAccessible(mgr + kOffStream, 12, false);
	if (ready)
	{
		const DWORD offs[3] = {kOffStreamLeft, kOffStreamRight, kOffStream};
		for (int i = 0; i < 3; ++i)
		{
			BYTE* s = *reinterpret_cast<BYTE* const*>(mgr + offs[i]);
			if (!s) continue;
			bool seen = false;
			for (size_t k = 0; k < count; ++k) seen = seen || streams[k] == s;
			if (seen) continue;
			if (!IsAccessible((DWORD)s, kStreamBytes, true)) { ready = false; break; }
			streams[count++] = s;
		}
		if (!count) ready = false;
	}
	bool allGrown = ready;
	if (ready)
		for (size_t k = 0; k < count && g_state != Test::kFailed; ++k)
			allGrown = Step(streams[k]) && allGrown;
	if (g_state == Test::kFailed) return;
	// A growth in flight whose stream is gone (the renderer made its streams again between the two steps): its new
	// blocks are left alone (half of them may be in the old stream's hands until its destructor).
	for (size_t i = 0; ready && i < sizeof(g_growth) / sizeof(g_growth[0]); ++i)
	{
		if (!g_growth[i].stream) continue;
		bool live = false;
		for (size_t k = 0; k < count; ++k) live = live || streams[k] == g_growth[i].stream;
		if (!live)
		{
			Logf("stream 0x%08lX went away during its growth (after %lu frames); its new blocks are left as they are",
			     (DWORD)g_growth[i].stream, g_growth[i].frames);
			memset(&g_growth[i], 0, sizeof(g_growth[i]));
		}
	}
	if (allGrown)
	{
		g_pendingFrames = 0;
		InterlockedExchange(&g_state, Test::kGrown);
		BYTE* inUse = *reinterpret_cast<BYTE* const*>(mgr + kOffStream);
		if (inUse) Usage(inUse);
		return;
	}
	InterlockedExchange(&g_state, ready ? Test::kGrowing : Test::kWaiting);
	if (++g_pendingFrames >= (DWORD)kPendingFrames)
		Fail("pending", "%s after %lu frames (%s)", ready ? "the growth is still pending" : "no command stream was found",
		     g_pendingFrames, ready ? "the current half was not seen empty" : "g_pUIManager or its streams not readable");
}

const char* StateName(LONG s)
{
	switch (s)
	{
	case Test::kOff: return "off";
	case Test::kWaiting: return "waiting";
	case Test::kGrowing: return "growing";
	case Test::kGrown: return "grown";
	case Test::kFailed: return "FAILED";
	default: return "?";
	}
}
}  // namespace

namespace ProxyUiDraw
{
void SetHooks(const Hooks& h) { g_hooks = h; }

void Configure(int option)
{
	InterlockedExchange(&g_enabled, option == 1 ? 1 : 0);
	if (option == 1)
		Logf("on (always since one-off 9081): the EXE's UI draw buffers are grown to %lu B and %lu commands per half at "
		     "the first in-game frames (ticket 0487)", kNewDataSize, kNewCommandCap);
	else   // tests only: the proxy configures 1 (the owner, one-off 9081, 2026-11-22: no longer an option)
		Logf("configured %d: off; the EXE keeps its stock 1 MB / 4,096-command UI draw buffers", option);
	if (option != 1) _snprintf_s(g_why, sizeof(g_why), _TRUNCATE, "off: configured %d", option);
}

bool Enabled() { return g_enabled != 0; }

void OnFrame()
{
	if (!g_enabled) return;
	const LONG s = g_state;
	if (s == Test::kFailed || (s == Test::kOff && g_checked)) return;
	__try
	{
		FrameBody();
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		Fail("fault", "an access violation while reading or writing the UI manager's streams (frame %ld)", g_frames);
	}
}

void StatusLine(char* out, size_t cap)
{
	const LONG high = g_dataHigh, dcap = g_dataCap;
	_snprintf_s(out, cap, _TRUNCATE,
	            "uidraw: %s%s%s; grown %ld (regrown %ld); frames %ld; data high-water %ld B of %ld B (%ld %%)",
	            !g_enabled ? "off" : StateName(g_state), g_why[0] ? "; " : "", g_why, g_grown, g_regrowths, g_frames,
	            high, dcap, dcap ? (LONG)(((unsigned __int64)(DWORD)high * 100) / (DWORD)dcap) : 0L);
}

// ---- ticket 0765: the answers of the proxy's Exe table (proxy_uidraw.h) ---------------------------------------------
const char* LuaCanGrow()
{
	if (!g_enabled) return "bin_hooks_off";
	const LONG s = g_state;
	if (s == Test::kFailed) return strncmp(g_why, "exe-check", 9) == 0 ? "unsupported_exe" : "unavailable";
	if (s == Test::kOff && g_checked) return "unsupported_exe";   // another EXE build: nothing is grown (not loud)
	return NULL;   // not checked yet (the first frame hook checks), waiting, growing or grown
}

const char* LuaTryGrow()
{
	const char* why = LuaCanGrow();
	if (why) return why;
	return g_state == Test::kGrown ? NULL : "pending";
}

const char* LuaUsage(unsigned long* used, unsigned long* capacity)
{
	const char* why = LuaCanGrow();
	if (why) return why;
	// the EXE checks must have passed (the frame hook's first run) before anything of the EXE is read here
	if (!g_checked || !g_alloc.newArray || !g_vaUiManager) return "unavailable";
	if (!IsAccessible(Rt(g_vaUiManager), 4, false)) return "unavailable";
	const DWORD mgr = *reinterpret_cast<const DWORD*>(Rt(g_vaUiManager));
	if (!mgr || !IsAccessible(mgr + kOffStream, 4, false)) return "unavailable";
	BYTE* s = *reinterpret_cast<BYTE* const*>(mgr + kOffStream);
	if (!s || !IsAccessible((DWORD)s, kStreamBytes, false)) return "unavailable";
	if (used) *used = Field(s, kOffDataUsedOther);
	if (capacity) *capacity = Field(s, kOffDataSize);
	return NULL;
}

void FailForTest(const char* why)
{
	_snprintf_s(g_testWhy, sizeof(g_testWhy), _TRUNCATE, "%s", why ? why : "");
	InterlockedExchange(&g_testFail, 1);
	Logf("TEST ONLY: the next frame after the EXE checks fails (%s)", g_testWhy);
}

namespace Test
{
const char* InstallAt(const void* base, const Allocators& alloc)
{
	Reset();
	bool loud = true;
	if (Verify((DWORD)base, &loud))
	{
		g_checked = true;
		if (loud)
			Fail("exe-check", "%s", g_why);
		else
		{
			InterlockedExchange(&g_state, kOff);
			Logf("%s; nothing is grown", g_why);
		}
		return g_why;
	}
	g_alloc = alloc;
	g_checked = true;
	InterlockedExchange(&g_state, kWaiting);
	return NULL;
}

void Reset()
{
	g_enabled = 1;
	g_state = kOff;
	g_why[0] = 0;
	g_base = 0;
	g_checked = false;
	memset(&g_alloc, 0, sizeof(g_alloc));
	memset(g_growth, 0, sizeof(g_growth));
	memset(g_grownStreams, 0, sizeof(g_grownStreams));
	g_grownCount = 0;
	g_grown = g_regrowths = g_reports = 0;
	g_pendingFrames = 0;
	g_dataHigh = g_dataCap = 0;
	g_levelLogged = 0;
	g_lastUsageMs = 0;
	g_frames = 0;
	g_testFail = 0;
	g_testWhy[0] = 0;
}

size_t PatternCount() { return kPatternCount; }
const Pattern& PatternAt(size_t i) { return g_pat[i]; }
size_t CrossCheckCount() { return kCheckCount; }
const CrossCheck& CrossCheckAt(size_t i) { return g_chk[i]; }
bool UseBuild(int build) { return Resolve(build) < 0; }
unsigned long Timestamp() { return VpExeBuildInfoOf(g_layoutBuild) ? VpExeBuildInfoOf(g_layoutBuild)->timeDateStamp : 0; }
unsigned long SizeOfImage() { return VpExeBuildInfoOf(g_layoutBuild) ? VpExeBuildInfoOf(g_layoutBuild)->sizeOfImage : 0; }
unsigned long ImageBase() { return kImageBase; }
unsigned long VaUiManager() { return g_vaUiManager; }
void DropSymbol(const char* name) { ProxyExe::Test::DropSymbol(name ? VpExeSymByName(name, -1) : -1); }
unsigned long OffStream(int which) { return which == 1 ? kOffStreamLeft : which == 2 ? kOffStreamRight : kOffStream; }
unsigned long Off(Field f)
{
	static const DWORD k[kFieldCount] = {kOffSetCount, kOffCommandCap, kOffCommandCount, kOffCommands, kOffData,
	                                     kOffDataSize, kOffDataUsed, kOffDataOther, kOffCommandsOther, kOffDataUsedOther};
	return f < kFieldCount ? k[f] : 0;
}
unsigned long StreamBytes() { return kStreamBytes; }
State GetState() { return (State)g_state; }
const char* Why() { return g_why; }
unsigned long Grown() { return (unsigned long)g_grown; }
unsigned long Regrowths() { return (unsigned long)g_regrowths; }
unsigned long PendingFrames() { return g_pendingFrames; }
unsigned long Reports() { return (unsigned long)g_reports; }
unsigned long DataHighWater() { return (unsigned long)g_dataHigh; }
}  // namespace Test
}  // namespace ProxyUiDraw
