// proxy_queue_guard.cpp - the guard of the EXE's 4 MB UI message queue on the product route (ticket 0492). See
// proxy_queue_guard.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Origin (COMMON rule 13, decision D22): the thunk (the range test, ReserveOrDivert's compare-and-swap and the scratch
// offset), the counters, the import-slot walk by name and the once-per-burst report are ported from repository
// Community-Patch-DLL (the project's fork, the repository deps/dll.lock names), branch engine-queue-guard, commit
// ac7afabc2 (GPL-3.0, like this repository):
//   CvGameCoreDLL_Expansion2/EngineQueueGuard.cpp, EngineQueueGuard.h
// Changed for the proxy: the EXE check is pattern-free (MC decision 2026-11-17, D83: the origin's four code byte patterns
// and their operand cross-checks are not carried; the time stamp, the slot found by name at its RVA and data checks of
// the queue take their place) and covers the DX11 build only (the product's EXE); the log goes to vpproxy.log; the
// slot is given back at an unload, or the image pinned when another hook sits on top; no BIN_HOOKS option and no
// VP_QUEUEGUARD switch (the proxy's boot is the switch; with the patch switched off, VP64Options.ini [Proxy]
// GuardsWhenOff, ticket 0635). The layout numbers are those of
// research/02-exe-internals/message-queue-and-fixed-buffers.md section 1 and ui-queue-guard-product.md section 4.
// Toolchain: clang-cl -m32, VC9 headers. x86 only (pointers are DWORDs).
#include "proxy_queue_guard.h"
#include "proxy_exe.h"  // ticket 4963: the build table (qg_iat_interlockedexchangeadd, qg_channel_index, qg_channels)
#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace ProxyRtInt  // proxy_rt_internal.h (declared here so that the offline unit test needs no generated header)
{
void Log(const char* fmt, ...);
}  // namespace ProxyRtInt

namespace
{
// ================================================================================================ the layout (DX11)
// The addresses come from the build table (ticket 4963; exebuild/symbols/proxy.tsv): qg_channel_index (int: which
// channel the writers use, 0 in every run seen), qg_channels (channel 0; channel c at + c * CHANNEL_STRIDE),
// qg_iat_interlockedexchangeadd (the EXE's import slot of KERNEL32!InterlockedExchangeAdd). The structure below is the
// same in both builds (0582 B.2).
const DWORD NUM_CHANNELS = 2;
const DWORD CHANNEL_STRIDE = 0x800180;       // two buffers, then the flip counter and the high-water mark
const DWORD BUFFER_STRIDE = 0x400080;        // size field and header, then the records
const DWORD HEADER_BYTES = 0x80;             // record = buffer + HEADER_BYTES + reserved offset
const DWORD BUFFER_CAPACITY = BUFFER_STRIDE - HEADER_BYTES;  // 4,194,304
const DWORD FLIP_OFFSET = 0x800100;          // the flip counter in a channel
// Records are 128 to 2,304 bytes (the origin checked 235 reserve sites). Anything larger passes through unguarded.
const LONG SCRATCH_BYTES = 64 * 1024;
const DWORD QUIET_MS = 2000;                 // one drop line per burst: after two seconds without a drop

typedef LONG(WINAPI* ExchangeAddFn)(LONG volatile* pAddend, LONG lValue);

// ================================================================================================ state
// Written by Install before the slot is written; read by the thunk on any thread.
ExchangeAddFn g_pfnOriginal = NULL;
DWORD g_dwChannels = 0;
DWORD g_dwIndexAt = 0;    // the channel index's address in the running build (ticket 4963)
char g_szReason[200] = {0};  // a refusal that names the build or a symbol
char* g_pScratch = NULL;  // never freed (a late writer may hold a pointer into it); reused by a later Install
DWORD* g_pSlot = NULL;
bool g_bInstalled = false;
bool g_bTried = false;
bool g_bPinned = false;
bool g_bForeignSeen = false;
const char* g_szStatus = "not installed (no boot yet)";
char g_szStatusBuf[260] = {0};
bool g_bWhenOff = false;  // ticket 0635: installed by InstallWhenOff (the patch switched off)

volatile LONG g_lDroppedRecords = 0;
volatile LONG g_lDroppedBytes = 0;
volatile LONG g_lLargestDropped = 0;
volatile LONG g_lTooLargeForScratch = 0;
volatile LONG g_lLastDropTick = 0;
// The report's own state (game-core thread only).
LONG g_lReportedRecords = 0;
LONG g_lReportedBytes = 0;
LONG g_lReportedTooLarge = 0;
LONG g_lBursts = 0;

// ================================================================================================ the thunk
void RaiseLargest(LONG lValue)
{
	LONG lSeen = g_lLargestDropped;
	while (lValue > lSeen)
	{
		const LONG lPrevious = InterlockedCompareExchange(&g_lLargestDropped, lValue, lSeen);
		if (lPrevious == lSeen) break;
		lSeen = lPrevious;
	}
}

// Reserves lValue bytes in the buffer whose size field is pSize, or diverts the record if it would not fit. Returns
// what the EXE's own reserve returns: the offset of the record from pSize + HEADER_BYTES.
LONG ReserveOrDivert(LONG volatile* pSize, LONG lValue)
{
	LONG lOld = *pSize;
	for (;;)
	{
		const DWORD dwOld = static_cast<DWORD>(lOld);
		if (dwOld > BUFFER_CAPACITY || static_cast<DWORD>(lValue) > BUFFER_CAPACITY - dwOld) break;
		const LONG lSeen = InterlockedCompareExchange(pSize, lOld + lValue, lOld);
		if (lSeen == lOld) return lOld;
		lOld = lSeen;
	}
	if (lValue > SCRATCH_BYTES)
	{
		// Never seen; the EXE's own behaviour is kept rather than writing past the scratch area (LOUD in OnUpdate).
		InterlockedIncrement(&g_lTooLargeForScratch);
		return g_pfnOriginal(pSize, lValue);
	}
	InterlockedIncrement(&g_lDroppedRecords);
	InterlockedExchangeAdd(&g_lDroppedBytes, lValue);
	RaiseLargest(lValue);
	InterlockedExchange(&g_lLastDropTick, static_cast<LONG>(GetTickCount()));
	// The size is left alone, so the dispatcher never reaches this record. Concurrent writers may share the scratch
	// area; nothing reads it.
	return static_cast<LONG>(reinterpret_cast<DWORD>(g_pScratch) - (reinterpret_cast<DWORD>(pSize) + HEADER_BYTES));
}

LONG WINAPI GuardedExchangeAdd(LONG volatile* pAddend, LONG lValue)
{
	// Unsigned, so an address below the channels wraps and fails the range test too.
	const DWORD dwOffset = reinterpret_cast<DWORD>(pAddend) - g_dwChannels;
	if (dwOffset < NUM_CHANNELS * CHANNEL_STRIDE && lValue > 0)
	{
		const DWORD dwInChannel = dwOffset % CHANNEL_STRIDE;
		if (dwInChannel == 0 || dwInChannel == BUFFER_STRIDE) return ReserveOrDivert(pAddend, lValue);
	}
	return g_pfnOriginal(pAddend, lValue);
}

// ================================================================================================ the checks
// True if [dwAddr, dwAddr + dwBytes) lies in committed memory that is readable (as proxy_shell.cpp's IsAccessible).
bool IsReadable(DWORD dwAddr, DWORD dwBytes)
{
	if (dwAddr < 0x10000 || dwAddr + dwBytes < dwAddr) return false;
	DWORD dwAt = dwAddr;
	const DWORD dwEnd = dwAddr + dwBytes;
	while (dwAt < dwEnd)
	{
		MEMORY_BASIC_INFORMATION kInfo;
		if (VirtualQuery(reinterpret_cast<LPCVOID>(dwAt), &kInfo, sizeof(kInfo)) != sizeof(kInfo)) return false;
		if (kInfo.State != MEM_COMMIT || (kInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		const DWORD dwReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((kInfo.Protect & dwReadable) == 0) return false;
		const DWORD dwRegionEnd = reinterpret_cast<DWORD>(kInfo.BaseAddress) + static_cast<DWORD>(kInfo.RegionSize);
		if (dwRegionEnd <= dwAt) return false;
		dwAt = dwRegionEnd;
	}
	return true;
}

// The image's import slot of KERNEL32.dll!InterlockedExchangeAdd, found by name, or NULL. Every RVA is checked to lie in
// the image and every structure to be readable before it is read.
DWORD* FindImportSlot(DWORD dwBase, const IMAGE_NT_HEADERS* pNt)
{
	const DWORD dwImageSize = pNt->OptionalHeader.SizeOfImage;
	const IMAGE_DATA_DIRECTORY& kDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (kDir.VirtualAddress == 0 || kDir.Size == 0 || kDir.VirtualAddress >= dwImageSize) return NULL;
	for (DWORD dwDesc = dwBase + kDir.VirtualAddress;; dwDesc += sizeof(IMAGE_IMPORT_DESCRIPTOR))
	{
		if (dwDesc + sizeof(IMAGE_IMPORT_DESCRIPTOR) > dwBase + dwImageSize ||
		    !IsReadable(dwDesc, sizeof(IMAGE_IMPORT_DESCRIPTOR)))
			return NULL;
		const IMAGE_IMPORT_DESCRIPTOR* pImport = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(dwDesc);
		if (pImport->Name == 0) return NULL;
		if (pImport->Name >= dwImageSize || pImport->OriginalFirstThunk == 0 || pImport->FirstThunk == 0 ||
		    pImport->OriginalFirstThunk >= dwImageSize || pImport->FirstThunk >= dwImageSize ||
		    !IsReadable(dwBase + pImport->Name, 13) ||
		    _strnicmp(reinterpret_cast<const char*>(dwBase + pImport->Name), "KERNEL32.dll", 13) != 0)
			continue;
		DWORD dwName = dwBase + pImport->OriginalFirstThunk;
		DWORD dwIat = dwBase + pImport->FirstThunk;
		for (;; dwName += 4, dwIat += 4)
		{
			if (dwName + 4 > dwBase + dwImageSize || dwIat + 4 > dwBase + dwImageSize || !IsReadable(dwName, 4) ||
			    !IsReadable(dwIat, 4))
				return NULL;
			const DWORD dwEntry = *reinterpret_cast<const DWORD*>(dwName);
			if (dwEntry == 0) break;
			if (IMAGE_SNAP_BY_ORDINAL32(dwEntry) || dwEntry + 2 + 23 > dwImageSize || !IsReadable(dwBase + dwEntry + 2, 23))
				continue;
			if (memcmp(reinterpret_cast<const char*>(dwBase + dwEntry + 2), "InterlockedExchangeAdd", 23) == 0)
				return reinterpret_cast<DWORD*>(dwIat);
		}
	}
}

// NULL if the guard may be installed on the image at dwBase, else the reason not. *pbKnown: the build is one the patch
// knows (a refusal is LOUD then). *ppSlot: the slot; *pdwChannels, *pdwIndex: the channels and the channel index.
const char* VerifyExe(DWORD dwBase, ExchangeAddFn pfnSelf, bool* pbKnown, DWORD** ppSlot, DWORD* pdwChannels,
                      DWORD* pdwIndex)
{
	*pbKnown = false;
	if (!IsReadable(dwBase, sizeof(IMAGE_DOS_HEADER))) return "off: no EXE image";
	const IMAGE_DOS_HEADER* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dwBase);
	if (pDos->e_magic != IMAGE_DOS_SIGNATURE || pDos->e_lfanew <= 0 || pDos->e_lfanew > 0x10000 ||
	    !IsReadable(dwBase + pDos->e_lfanew, sizeof(IMAGE_NT_HEADERS)))
		return "off: no EXE image";
	const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(dwBase + pDos->e_lfanew);
	if (pNt->Signature != IMAGE_NT_SIGNATURE) return "off: no EXE image";
	// ticket 4963: the build from the registry (stamp and SizeOfImage), the addresses from its table
	const int build = VpExeBuildFromHeader(pNt->FileHeader.TimeDateStamp, pNt->OptionalHeader.SizeOfImage);
	if (build == kVpExeBuildUnknown)
		return ProxyExe::UnknownBuild(g_szReason, sizeof(g_szReason), "off: ", pNt->FileHeader.TimeDateStamp,
		                              pNt->OptionalHeader.SizeOfImage);
	*pbKnown = true;
	const int syms[3] = {VPX_qg_iat_interlockedexchangeadd, VPX_qg_channel_index, VPX_qg_channels};
	for (int i = 0; i < 3; ++i)
		if (!ProxyExe::Va(build, syms[i]))
			return ProxyExe::NotCarried(g_szReason, sizeof(g_szReason), "off: ", build, syms[i]);
	const DWORD dwSlotAt = static_cast<DWORD>(ProxyExe::Addr(build, VPX_qg_iat_interlockedexchangeadd, dwBase));
	const DWORD dwIndexAt = static_cast<DWORD>(ProxyExe::Addr(build, VPX_qg_channel_index, dwBase));
	const DWORD dwChannels = static_cast<DWORD>(ProxyExe::Addr(build, VPX_qg_channels, dwBase));

	DWORD* pSlot = FindImportSlot(dwBase, pNt);
	if (pSlot == NULL) return "off: no KERNEL32.dll InterlockedExchangeAdd import found by name";
	if (reinterpret_cast<DWORD>(pSlot) != dwSlotAt)
		return "off: the InterlockedExchangeAdd import slot is not at the expected RVA";
	if (*pSlot == 0) return "off: the InterlockedExchangeAdd import slot is not bound";
	if (*pSlot == reinterpret_cast<DWORD>(pfnSelf)) return "off: the import slot already holds this guard";

	if (!IsReadable(dwIndexAt, 4)) return "off: the channel index is not readable";
	const LONG lIndex = *reinterpret_cast<const LONG*>(dwIndexAt);
	if (lIndex < 0 || lIndex >= static_cast<LONG>(NUM_CHANNELS)) return "off: unexpected channel index (not 0 or 1)";
	for (DWORD c = 0; c < NUM_CHANNELS; ++c)
	{
		const DWORD dwChannel = dwChannels + c * CHANNEL_STRIDE;
		for (DWORD b = 0; b < 2; ++b)
		{
			const DWORD dwSize = dwChannel + b * BUFFER_STRIDE;
			if (!IsReadable(dwSize, 4)) return "off: a buffer's size field is not readable";
			if (*reinterpret_cast<const DWORD*>(dwSize) > BUFFER_CAPACITY)
				return "off: a buffer's size field is above the capacity (the queue is not as expected)";
		}
		if (!IsReadable(dwChannel + FLIP_OFFSET, 8)) return "off: a flip counter is not readable";
	}
	*ppSlot = pSlot;
	*pdwChannels = dwChannels;
	*pdwIndex = dwIndexAt;
	return NULL;
}

// The installed status text: "active (DX11 build; slot 0x..., channels 0x..., index N)".
void SetActiveStatus()
{
	_snprintf_s(g_szStatusBuf, sizeof(g_szStatusBuf), _TRUNCATE,
	            "active (DX11 build; slot 0x%08lX, channels 0x%08lX, index %ld; scratch %ld KB%s)",
	            static_cast<unsigned long>(reinterpret_cast<DWORD>(g_pSlot)), static_cast<unsigned long>(g_dwChannels),
	            static_cast<long>(*reinterpret_cast<const LONG*>(g_dwIndexAt)), static_cast<long>(SCRATCH_BYTES / 1024),
	            g_bWhenOff ? "; the patch is switched off: the 32-bit DLL answers" : "");
	g_szStatus = g_szStatusBuf;
}

// Ticket 0635: the file name of the module that holds dwAddr ("an unknown module" when none), for the refusal line.
void ModuleOf(DWORD dwAddr, char* out, size_t cap)
{
	HMODULE hOwner = NULL;
	char path[MAX_PATH] = {0};
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCSTR>(dwAddr), &hOwner) &&
	    GetModuleFileNameA(hOwner, path, MAX_PATH) != 0)
	{
		const char* name = strrchr(path, '\\');
		_snprintf_s(out, cap, _TRUNCATE, "%s", name ? name + 1 : path);
	}
	else
		_snprintf_s(out, cap, _TRUNCATE, "an unknown module");
}

// bWhenOff (ticket 0635): the patch is switched off; refused, without LOUD, when the slot does not hold pfnBound (the
// proxy never hooks on top of another hook there).
const char* InstallOn(DWORD dwBase, ExchangeAddFn pfnBound, bool bWhenOff)
{
	if (g_bTried) return g_bInstalled ? NULL : g_szStatus;
	g_bTried = true;
	bool bKnown = false;
	DWORD* pSlot = NULL;
	DWORD dwChannels = 0, dwIndexAt = 0;
	const char* szProblem = VerifyExe(dwBase, &GuardedExchangeAdd, &bKnown, &pSlot, &dwChannels, &dwIndexAt);
	if (szProblem == NULL && bWhenOff && (pfnBound == NULL || *pSlot != reinterpret_cast<DWORD>(pfnBound)))
	{
		char owner[MAX_PATH];
		ModuleOf(*pSlot, owner, sizeof(owner));
		_snprintf_s(g_szReason, sizeof(g_szReason), _TRUNCATE,
		            "off: the import slot is already hooked (it holds 0x%08lX in %s, not KERNEL32's own function): with the "
		            "patch switched off the proxy does not hook on top (the 32-bit DLL may bring its own guard)",
		            static_cast<unsigned long>(*pSlot), owner);
		szProblem = g_szReason;  // LOUD (MC #9004: say which of the two happened, loudly)
	}
	g_bWhenOff = bWhenOff;
	if (szProblem == NULL && g_pScratch == NULL)
	{
		g_pScratch = static_cast<char*>(VirtualAlloc(NULL, SCRATCH_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (g_pScratch == NULL) szProblem = "off: the scratch area could not be allocated";
	}
	DWORD dwOldProtect = 0;
	if (szProblem == NULL && !VirtualProtect(pSlot, sizeof(DWORD), PAGE_READWRITE, &dwOldProtect))
		szProblem = "off: the import slot could not be made writable";
	if (szProblem != NULL)
	{
		g_szStatus = szProblem;
		ProxyRtInt::Log("%squeue guard: UI message queue guard %s; the EXE's queue is not protected%s", bKnown ? "LOUD: " : "",
		                szProblem, bKnown ? (bWhenOff ? " (tickets 0492, 0635)" : " (ticket 0492)") : "");
		return szProblem;
	}
	// Everything the thunk reads is in place before the first call can reach it; the exchange is a full barrier, and a
	// caller already past the slot still holds the previous value, which works.
	const DWORD dwBound = *pSlot;
	g_dwChannels = dwChannels;
	g_dwIndexAt = dwIndexAt;
	g_pfnOriginal = reinterpret_cast<ExchangeAddFn>(dwBound);
	g_pSlot = pSlot;
	InterlockedExchange(reinterpret_cast<LONG volatile*>(pSlot), reinterpret_cast<LONG>(&GuardedExchangeAdd));
	DWORD dwIgnored = 0;
	VirtualProtect(pSlot, sizeof(DWORD), dwOldProtect, &dwIgnored);
	g_bInstalled = true;
	SetActiveStatus();
	ProxyRtInt::Log("%squeue guard: UI message queue guard %s; records that would not fit a 4 MB buffer are dropped and "
	                "counted (%s)",
	                bWhenOff ? "LOUD: " : "", g_szStatus,
	                bWhenOff ? "tickets 0492, 0635: installed by the proxy with the patch switched off" : "ticket 0492");
	if (pfnBound != NULL && dwBound != reinterpret_cast<DWORD>(pfnBound))
		ProxyRtInt::Log("LOUD: queue guard: the import slot held 0x%08lX, not KERNEL32!InterlockedExchangeAdd 0x%08lX "
		                "(another module hooked it first): the guard forwards to it",
		                static_cast<unsigned long>(dwBound), static_cast<unsigned long>(reinterpret_cast<DWORD>(pfnBound)));
	return NULL;
}

// The drop report (game-core thread): one line per burst after QUIET_MS without a drop; a LOUD line for records too
// large to divert; the pin when another hook sits on top of the thunk.
void UpdateOn(DWORD dwNow)
{
	if (!g_bInstalled) return;
	if (!g_bForeignSeen && *g_pSlot != reinterpret_cast<DWORD>(&GuardedExchangeAdd))
	{
		// Another module wrote the slot after us and may forward to the thunk: the image must never be unmapped.
		g_bForeignSeen = true;  // said once
		HMODULE hSelf = NULL;
		g_bPinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
		                               reinterpret_cast<LPCWSTR>(&GuardedExchangeAdd), &hSelf) != FALSE;
		ProxyRtInt::Log("LOUD: queue guard: the import slot now holds 0x%08lX, not the guard: another module hooked it on "
		                "top; the proxy's image is %s (it may be called through that hook)",
		                static_cast<unsigned long>(*g_pSlot),
		                g_bPinned ? "pinned for the life of the process" : "NOT pinned (GetModuleHandleEx failed)");
	}
	const LONG lTooLarge = g_lTooLargeForScratch;
	if (lTooLarge != g_lReportedTooLarge)
	{
		ProxyRtInt::Log("LOUD: queue guard: %ld record(s) larger than the %ld KB scratch area did not fit and were passed "
		                "through unguarded (the EXE's own overrun; never seen on stock)",
		                static_cast<long>(lTooLarge - g_lReportedTooLarge), static_cast<long>(SCRATCH_BYTES / 1024));
		g_lReportedTooLarge = lTooLarge;
	}
	const LONG lRecords = g_lDroppedRecords;
	if (lRecords == g_lReportedRecords) return;
	if (dwNow - static_cast<DWORD>(g_lLastDropTick) < QUIET_MS) return;
	const LONG lBytes = g_lDroppedBytes;
	++g_lBursts;
	ProxyRtInt::Log("queue guard: dropped %ld records (%ld KB) that did not fit the EXE's UI message queue; since the boot "
	                "%ld records (%ld KB) in %ld burst(s), largest %ld bytes, %ld too large to divert",
	                static_cast<long>(lRecords - g_lReportedRecords), static_cast<long>((lBytes - g_lReportedBytes) >> 10),
	                static_cast<long>(lRecords), static_cast<long>(lBytes >> 10), static_cast<long>(g_lBursts),
	                static_cast<long>(g_lLargestDropped), static_cast<long>(g_lTooLargeForScratch));
	g_lReportedRecords = lRecords;
	g_lReportedBytes = lBytes;
}

// Gives the slot its previous value back if it still holds the thunk. Logs nothing.
void Restore()
{
	if (!g_bInstalled || g_pSlot == NULL) return;
	DWORD dwOldProtect = 0;
	if (!VirtualProtect(g_pSlot, sizeof(DWORD), PAGE_READWRITE, &dwOldProtect)) return;
	InterlockedCompareExchange(reinterpret_cast<LONG volatile*>(g_pSlot), reinterpret_cast<LONG>(g_pfnOriginal),
	                           reinterpret_cast<LONG>(&GuardedExchangeAdd));
	DWORD dwIgnored = 0;
	VirtualProtect(g_pSlot, sizeof(DWORD), dwOldProtect, &dwIgnored);
}
}  // namespace

namespace ProxyQueueGuard
{
void Install()
{
	HMODULE hKernel = GetModuleHandleA("kernel32.dll");
	const ExchangeAddFn pfnBound =
	    hKernel ? reinterpret_cast<ExchangeAddFn>(GetProcAddress(hKernel, "InterlockedExchangeAdd")) : NULL;
	InstallOn(reinterpret_cast<DWORD>(GetModuleHandleA(NULL)), pfnBound, false);
}

void InstallWhenOff()
{
	HMODULE hKernel = GetModuleHandleA("kernel32.dll");
	const ExchangeAddFn pfnBound =
	    hKernel ? reinterpret_cast<ExchangeAddFn>(GetProcAddress(hKernel, "InterlockedExchangeAdd")) : NULL;
	InstallOn(reinterpret_cast<DWORD>(GetModuleHandleA(NULL)), pfnBound, true);
}

void OnUpdate()
{
	UpdateOn(GetTickCount());
}

void OnUnload()
{
	Restore();  // a compare-and-swap: a hook on top of the thunk is left alone (the image is pinned then)
}

void StatusLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "UI queue guard: %s; dropped %ld records (%ld KB) in %ld reported burst(s), largest %ld bytes, %ld too large "
	            "to divert%s",
	            g_szStatus, static_cast<long>(g_lDroppedRecords), static_cast<long>(g_lDroppedBytes >> 10),
	            static_cast<long>(g_lBursts), static_cast<long>(g_lLargestDropped),
	            static_cast<long>(g_lTooLargeForScratch),
	            g_bInstalled && g_pSlot && *g_pSlot != reinterpret_cast<DWORD>(&GuardedExchangeAdd)
	                ? "; another hook sits on top of the guard (the proxy is pinned)"
	                : "");
}

namespace Test
{
const char* InstallAt(void* base, ExchangeAddFn bound)
{
	return InstallOn(reinterpret_cast<DWORD>(base), bound, false);  // the same function type as the file's ExchangeAddFn
}
const char* InstallWhenOffAt(void* base, ExchangeAddFn bound)
{
	return InstallOn(reinterpret_cast<DWORD>(base), bound, true);
}
void UpdateAt(unsigned long tick)
{
	UpdateOn(tick);
}
void Reset()
{
	Restore();
	g_pfnOriginal = NULL;
	g_dwChannels = 0;
	g_pSlot = NULL;
	g_bInstalled = g_bTried = g_bPinned = g_bForeignSeen = g_bWhenOff = false;
	g_szStatus = "not installed (no boot yet)";
	g_lDroppedRecords = g_lDroppedBytes = g_lLargestDropped = g_lTooLargeForScratch = g_lLastDropTick = 0;
	g_lReportedRecords = g_lReportedBytes = g_lReportedTooLarge = g_lBursts = 0;
}
bool Installed()
{
	return g_bInstalled;
}
bool Pinned()
{
	return g_bPinned;
}
const char* Status()
{
	return g_szStatus;
}
long Dropped()
{
	return g_lDroppedRecords;
}
long DroppedBytes()
{
	return g_lDroppedBytes;
}
long Largest()
{
	return g_lLargestDropped;
}
long TooLarge()
{
	return g_lTooLargeForScratch;
}
long Bursts()
{
	return g_lBursts;
}
const void* Thunk()
{
	return reinterpret_cast<const void*>(&GuardedExchangeAdd);
}
const void* Scratch()
{
	return g_pScratch;
}
unsigned long RvaChannelIndex(int build)
{
	return ProxyExe::Rva(build, VPX_qg_channel_index);
}
unsigned long RvaChannels(int build)
{
	return ProxyExe::Rva(build, VPX_qg_channels);
}
unsigned long RvaSlot(int build)
{
	return ProxyExe::Rva(build, VPX_qg_iat_interlockedexchangeadd);
}
void DropSymbol(const char* name)
{
	ProxyExe::Test::DropSymbol(name ? VpExeSymByName(name, -1) : -1);
}
unsigned long ChannelStride()
{
	return CHANNEL_STRIDE;
}
unsigned long BufferStride()
{
	return BUFFER_STRIDE;
}
unsigned long Capacity()
{
	return BUFFER_CAPACITY;
}
long ScratchBytes()
{
	return SCRATCH_BYTES;
}
}  // namespace Test
}  // namespace ProxyQueueGuard
