// wheel_test.cpp - offline tests of proxy/src/proxy_wheel.cpp (ticket 0755, [Extras] FixSmoothWheel), linked with it;
// ProxyRtInt::Log is a stub here. Built and run by proxy/tests/wheel/test_wheel.py with the proxy's toolchain (clang-cl
// -m32, VC9 headers) and once under MSVC /fsanitize=address (x86). The fake image follows
// proxy/tools/rt_tests/kbhook_unit.cpp's (ticket 0720).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fake EXE image (VirtualAlloc: a PE header with build 4390913's or 26577625's time stamp and image size, an import
// directory naming USER32.dll!GetMessageW and PeekMessageW) stands in for the EXE; nothing of the EXE is read. The slot's
// previous value is a scripted queue (FakePeek); the clock and the foreground window are stand-ins (Api). Checks:
//   core      Take over the 5042 arms (research/02 camera-and-picking.md 11.3, 11.3b: N1, N2, D1, H2, H4, H8, T1 as
//             sent and as Windows merged them, T3 merged 195/114/37/14), the sign change, the 400 ms limit (400 kept,
//             401 dropped), a zero delta, merged notches (345), negative truncation, Expire;
//   install   the slot holds the thunk, one log line, a second call does nothing; both builds;
//   notched   a message of +-120 with an empty remainder passes byte for byte (the whole MSG compared);
//   summing   8 x 15 = 7 WM_NULL (message, wParam, lParam 0; hwnd, time, pt kept) and one of 120 with the key-state word
//             kept; 4 x 30 then 4 x -30: +120, -120; a remainder dropped on a sign change;
//   merged    one message of 360: three of 120 on three calls, the queue read once, then the next queued message; -240
//             as two of -120; 32767: kMaxNotches handed out, the rest counted as capped; a pending notch before WM_QUIT;
//   forward   another thread, PM_NOREMOVE, a window filter, a message range: forwarded, the message untouched, nothing
//             counted, no pending notch taken; an empty queue: FALSE;
//   time      a remainder older than 400 ms dropped by the next call (fake clock), 400 ms kept;
//   focus     a remainder formed in front, the foreground then another thread's: dropped at the next call; a remainder
//             formed behind another window (scrolling an inactive window): kept while it stays behind; pending notches
//             dropped at a foreground change;
//   report    one counters line, none again within 10 s, the next after;
//   unload    the slot back with the counters line; a hook written on top of the thunk: left alone;
//   refusals  no image, e_lfanew, signature, an unknown time stamp (quiet), no PeekMessageW import, an unbound slot, the
//             slot already the thunk: the reason, the slot unchanged, LOUD where the build is known;
//   product   Install(false, why): one "off" line, nothing written; Install(true) in this program (not the EXE): refused by
//             the time stamp, quiet.
// Exit code 0 when every check passes, 1 otherwise; "--expect-fail" adds one check that must fail.
#include "proxy_wheel.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace
{
std::string g_log;
bool g_verbose = false;
}  // namespace

namespace ProxyRtInt
{
void Log(const char* fmt, ...)
{
	char line[2048];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	g_log += line;
	g_log += "\n";
	if (g_verbose) printf("    log: %s\n", line);
}
}  // namespace ProxyRtInt

namespace
{
namespace WT = ProxyWheel::Test;
using ProxyWheel::Summer;
using ProxyWheel::Take;

int g_pass = 0, g_fail = 0;
void Check(bool ok, const char* name, int line)
{
	if (ok)
		++g_pass;
	else
	{
		++g_fail;
		printf("FAIL line %d: %s\n", line, name);
	}
}
#define CHECK(x) Check((x), #x, __LINE__)

bool LogHas(const char* s)
{
	return g_log.find(s) != std::string::npos;
}
size_t LogCount(const char* s)
{
	size_t n = 0;
	for (size_t at = g_log.find(s); at != std::string::npos; at = g_log.find(s, at + 1)) ++n;
	return n;
}

// ------------------------------------------------------------------------------------------------ the core
// The notches Take gives for each delta, all at time t (ms apart by `gap`).
std::string Run(Summer& s, const int* deltas, int n, DWORD t0 = 1000, DWORD gap = 8)
{
	std::string out;
	for (int i = 0; i < n; ++i)
	{
		char b[16];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%s%d", i ? "," : "", Take(s, deltas[i], t0 + gap * i));
		out += b;
	}
	return out;
}
std::string Fresh(const int* deltas, int n, DWORD gap = 8)
{
	Summer s;
	ProxyWheel::Reset(s);
	return Run(s, deltas, n, 1000, gap);
}

void TestCore()
{
	const int n1[] = {120};
	CHECK(Fresh(n1, 1) == "1");
	const int n2[] = {120, 120};
	CHECK(Fresh(n2, 2, 50) == "1,1");
	const int d1[] = {1};
	CHECK(Fresh(d1, 1) == "0");
	const int h2[] = {60, 60};
	CHECK(Fresh(h2, 2) == "0,1");
	const int h4[] = {30, 30, 30, 30};
	CHECK(Fresh(h4, 4) == "0,0,0,1");
	const int h8[] = {15, 15, 15, 15, 15, 15, 15, 15};
	CHECK(Fresh(h8, 8) == "0,0,0,0,0,0,0,1");
	const int h8merged[] = {45, 60, 15};   // 11.3b: H8 as Windows merged it, one message a frame
	CHECK(Fresh(h8merged, 3, 33) == "0,0,1");
	const int t1[] = {10, 12, 14, 12, 10, 14, 12, 12, 10, 14};   // 10 events adding to 120
	CHECK(Fresh(t1, 10) == "0,0,0,0,0,0,0,0,0,1");
	const int t1merged[] = {44, 61, 15};
	CHECK(Fresh(t1merged, 3, 33) == "0,0,1");
	const int t3merged[] = {195, 114, 37, 14};   // 11.3b: T3 (360) merged: 3 notches in all
	CHECK(Fresh(t3merged, 4, 33) == "1,1,0,1");
	const int neg[] = {-30, -30, -30, -30};
	CHECK(Fresh(neg, 4) == "0,0,0,-1");
	const int burst[] = {270, 255, 45};   // 11.3b's bursts at the limit: 570 = 4 notches
	CHECK(Fresh(burst, 3, 133) == "2,2,0");
	Summer s;
	ProxyWheel::Reset(s);
	CHECK(Take(s, 345, 0) == 2 && s.remainder == 105);
	CHECK(Take(s, -200, 10) == -1 && s.remainder == -80 && s.droppedBySign == 1);   // the 105 dropped, then -200
	CHECK(Take(s, 0, 20) == 0 && s.remainder == -80 && s.lastMs == 10);            // a zero delta changes nothing
	CHECK(Take(s, -40, 410) == -1 && s.remainder == 0 && s.droppedByTime == 0);    // 400 ms after: kept
	CHECK(Take(s, 60, 500) == 0 && s.remainder == 60);
	CHECK(Take(s, 60, 901) == 0 && s.remainder == 60 && s.droppedByTime == 1);     // 401 ms after: dropped first
	CHECK(!ProxyWheel::Expire(s, 1301) && s.remainder == 60);
	CHECK(ProxyWheel::Expire(s, 1302) && s.remainder == 0 && s.droppedByTime == 2);
	CHECK(!ProxyWheel::Expire(s, 9999) && s.droppedByTime == 2);                   // nothing to drop
	CHECK(Take(s, 60, 2000) == 0 && Take(s, -60, 2001) == 0 && s.remainder == -60 && s.droppedBySign == 2);
	CHECK(Take(s, 32767, 2002) == 273 && s.remainder == 7);   // after the sign drop: 32767 = 273 x 120 + 7
	// the clock wraps: GetTickCount's 49.7 days
	ProxyWheel::Reset(s);
	CHECK(Take(s, 60, 0xFFFFFF00u) == 0 && Take(s, 60, 0x00000010u) == 1 && s.droppedByTime == 0);
}

// ------------------------------------------------------------------------------------------------ the fake image
const DWORD kImportDir = 0x1000;
const DWORD kKernelOft = 0x1100, kKernelFt = 0x1140;
const DWORD kKernelName = 0x1200, kUserName = 0x1210;
const DWORD kHintSleep = 0x1300, kHintGetMsg = 0x1320, kHintPeek = 0x1340;
const DWORD kUserOft = 0x1400, kUserFt = 0x1500;
const HWND kWnd = reinterpret_cast<HWND>(0x1234);
const HWND kOther = reinterpret_cast<HWND>(0x5678);

unsigned char* g_base = NULL;
DWORD* At(DWORD rva)
{
	return reinterpret_cast<DWORD*>(g_base + rva);
}
DWORD* PeekSlot()
{
	return At(kUserFt + 4);
}
void Strcpy(DWORD rva, const char* s)
{
	strcpy(reinterpret_cast<char*>(g_base + rva), s);
}

// The slot's previous value: a scripted queue
MSG g_queue[64];
int g_qHead = 0, g_qTail = 0;
volatile LONG g_prevCalls = 0;
UINT g_lastFlags = 0;
BOOL WINAPI FakePeek(MSG* m, HWND, UINT, UINT, UINT flags)
{
	InterlockedIncrement(&g_prevCalls);
	g_lastFlags = flags;
	if (g_qHead == g_qTail) return FALSE;
	*m = g_queue[g_qHead];
	if (flags & PM_REMOVE) ++g_qHead;
	return TRUE;
}
DWORD g_mainTid = 0;
DWORD g_now = 5000;
DWORD WINAPI FakeTick()
{
	return g_now;
}
HWND g_fg = kWnd;
HWND WINAPI FakeForeground()
{
	return g_fg;
}
DWORD WINAPI FakeWindowThread(HWND h, LPDWORD pid)
{
	if (pid) *pid = GetCurrentProcessId();
	return h == kWnd ? g_mainTid : g_mainTid + 4;
}
WT::Api FakeApi()
{
	WT::Api a = {&FakeTick, &FakeForeground, &FakeWindowThread};
	return a;
}

MSG Wheel(int delta, WORD keys = 0, UINT msg = WM_MOUSEWHEEL)
{
	MSG m;
	memset(&m, 0, sizeof(m));
	m.hwnd = kWnd;
	m.message = msg;
	m.wParam = static_cast<WPARAM>((static_cast<DWORD>(static_cast<WORD>(static_cast<short>(delta))) << 16) | keys);
	m.lParam = MAKELPARAM(768, 443);
	m.time = 777;
	m.pt.x = 768;
	m.pt.y = 443;
	return m;
}
void Queue(const MSG& m)
{
	g_queue[g_qTail++] = m;
}
void QueueWheel(int delta, WORD keys = 0)
{
	Queue(Wheel(delta, keys));
}

typedef BOOL(WINAPI* PeekFnT)(MSG*, HWND, UINT, UINT, UINT);
// One call of the main loop through the slot (as the EXE's loop makes it)
BOOL Loop(MSG* m, UINT flags = PM_REMOVE, HWND w = NULL, UINT lo = 0, UINT hi = 0)
{
	memset(m, 0xCD, sizeof(*m));
	return reinterpret_cast<PeekFnT>(*PeekSlot())(m, w, lo, hi, flags);
}
int DeltaOf(const MSG& m)
{
	return GET_WHEEL_DELTA_WPARAM(m.wParam);
}
// Drains the queue through the slot; the deltas of what the "EXE" saw, "N" for WM_NULL, "Q" for WM_QUIT
std::string Drain()
{
	std::string out;
	MSG m;
	for (int guard = 0; guard < 200 && Loop(&m); ++guard)
	{
		char b[16];
		if (m.message == WM_MOUSEWHEEL)
			_snprintf_s(b, sizeof(b), _TRUNCATE, "%d", DeltaOf(m));
		else if (m.message == WM_NULL)
			strcpy(b, "N");
		else if (m.message == WM_QUIT)
			strcpy(b, "Q");
		else
			_snprintf_s(b, sizeof(b), _TRUNCATE, "m%u", m.message);
		if (!out.empty()) out += ",";
		out += b;
	}
	return out;
}

void Fresh(int build)
{
	WT::Reset();
	memset(g_base, 0, 0x3000);
	IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = 0x80;
	IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_base + 0x80);
	nt->Signature = IMAGE_NT_SIGNATURE;
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->FileHeader.TimeDateStamp = build == 0 ? 0x546CD0A8u : 0x6AF3837Du;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	nt->OptionalHeader.SizeOfImage = build == 0 ? 0x06519000u : 0x063A2000u;
	nt->OptionalHeader.NumberOfRvaAndSizes = 16;
	nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = kImportDir;
	nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 3 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
	IMAGE_IMPORT_DESCRIPTOR* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(g_base + kImportDir);
	d[0].OriginalFirstThunk = kKernelOft;
	d[0].FirstThunk = kKernelFt;
	d[0].Name = kKernelName;
	d[1].OriginalFirstThunk = kUserOft;
	d[1].FirstThunk = kUserFt;
	d[1].Name = kUserName;
	Strcpy(kKernelName, "KERNEL32.dll");
	Strcpy(kUserName, "USER32.dll");
	Strcpy(kHintSleep + 2, "Sleep");
	Strcpy(kHintGetMsg + 2, "GetMessageW");
	Strcpy(kHintPeek + 2, "PeekMessageW");
	*At(kKernelOft) = kHintSleep;
	*At(kKernelFt) = 0x11111111;
	*At(kUserOft) = kHintGetMsg;
	*At(kUserOft + 4) = kHintPeek;
	*At(kUserFt) = 0x22222222;
	*PeekSlot() = reinterpret_cast<DWORD>(&FakePeek);
	g_qHead = g_qTail = 0;
	g_prevCalls = 0;
	g_now = 5000;
	g_fg = kWnd;
	g_mainTid = GetCurrentThreadId();
	g_log.clear();
}

bool InstallFresh(int build)
{
	Fresh(build);
	return WT::InstallAt(g_base, g_mainTid, FakeApi()) == NULL;
}

// ------------------------------------------------------------------------------------------------ the checks
void TestInstall(int build)
{
	CHECK(InstallFresh(build));
	CHECK(WT::Installed() && *PeekSlot() == reinterpret_cast<DWORD>(WT::Thunk()));
	CHECK(strstr(WT::Status(), "active (slot 0x") == WT::Status());
	CHECK(LogCount("wheel: [Extras] FixSmoothWheel: the EXE's PeekMessageW import slot points at the proxy's thunk") == 1);
	CHECK(WT::InstallAt(g_base, g_mainTid, FakeApi()) == NULL && LogCount("wheel:") == 1);   // a second call: nothing
	CHECK(*At(kUserFt) == 0x22222222 && *At(kKernelFt) == 0x11111111);                       // no other slot written
}

void TestNotched()
{
	CHECK(InstallFresh(0));
	const MSG in = Wheel(120, MK_SHIFT);
	Queue(in);
	const MSG in2 = Wheel(-120);
	Queue(in2);
	MSG m;
	CHECK(Loop(&m) && memcmp(&m, &in, sizeof(m)) == 0);
	CHECK(Loop(&m) && memcmp(&m, &in2, sizeof(m)) == 0);
	CHECK(!Loop(&m));
	CHECK(WT::WheelMessages() == 2 && WT::Passed() == 2 && WT::Held() == 0 && WT::Merged() == 0 && WT::Pending() == 0);
	CHECK(WT::State().remainder == 0);
	// another message passes untouched
	MSG key;
	memset(&key, 0, sizeof(key));
	key.hwnd = kWnd;
	key.message = WM_KEYDOWN;
	key.wParam = 'A';
	Queue(key);
	CHECK(Loop(&m) && memcmp(&m, &key, sizeof(m)) == 0);
}

void TestSumming()
{
	CHECK(InstallFresh(1));
	for (int i = 0; i < 8; ++i) QueueWheel(15, MK_CONTROL);
	MSG m;
	int nulls = 0;
	for (int i = 0; i < 7; ++i)
	{
		const bool ok = Loop(&m) && m.message == WM_NULL && m.wParam == 0 && m.lParam == 0 && m.hwnd == kWnd &&
		                m.time == 777 && m.pt.x == 768;
		if (ok) ++nulls;
	}
	CHECK(nulls == 7);
	CHECK(Loop(&m) && m.message == WM_MOUSEWHEEL && DeltaOf(m) == 120 && GET_KEYSTATE_WPARAM(m.wParam) == MK_CONTROL &&
	      m.lParam == MAKELPARAM(768, 443));
	CHECK(WT::Held() == 7 && WT::Passed() == 1 && WT::WheelMessages() == 8);
	for (int i = 0; i < 4; ++i) QueueWheel(30);
	for (int i = 0; i < 4; ++i) QueueWheel(-30);
	CHECK(Drain() == "N,N,N,120,N,N,N,-120");
	QueueWheel(60);
	QueueWheel(-60);
	QueueWheel(-60);
	CHECK(Drain() == "N,N,-120" && WT::State().droppedBySign == 1);
	QueueWheel(1);
	CHECK(Drain() == "N" && WT::State().remainder == 1);
	QueueWheel(60);
	QueueWheel(30);
	QueueWheel(40);   // 131 with the 1 before: one notch, 11 left
	CHECK(Drain() == "N,N,120" && WT::State().remainder == 11);
}

void TestMerged()
{
	CHECK(InstallFresh(0));
	QueueWheel(360, MK_LBUTTON);
	MSG after;
	memset(&after, 0, sizeof(after));
	after.message = WM_MOUSEMOVE;
	Queue(after);
	MSG m[3];
	CHECK(Loop(&m[0]) && Loop(&m[1]) && Loop(&m[2]));
	CHECK(g_prevCalls == 1);   // the queue read once: the two further notches come from the pending count
	CHECK(memcmp(&m[0], &m[1], sizeof(MSG)) == 0 && memcmp(&m[1], &m[2], sizeof(MSG)) == 0);
	CHECK(DeltaOf(m[0]) == 120 && GET_KEYSTATE_WPARAM(m[0].wParam) == MK_LBUTTON && m[0].hwnd == kWnd);
	MSG n;
	CHECK(Loop(&n) && n.message == WM_MOUSEMOVE && g_prevCalls == 2);
	CHECK(WT::Merged() == 1 && WT::HandedOut() == 2 && WT::Passed() == 1 && WT::Pending() == 0);
	QueueWheel(-240);
	QueueWheel(195);
	QueueWheel(114);
	QueueWheel(37);
	QueueWheel(14);
	CHECK(Drain() == "-120,-120,120,120,N,120");   // T3 as Windows merged it (11.3b): 3 steps
	QueueWheel(32767);
	const std::string big = Drain();
	size_t steps = 0;
	for (size_t at = big.find("120"); at != std::string::npos; at = big.find("120", at + 1)) ++steps;
	CHECK(steps == (size_t)ProxyWheel::kMaxNotches && WT::Capped() == 273 - ProxyWheel::kMaxNotches);
	// a pending notch comes before WM_QUIT (the order of the queue)
	QueueWheel(240);
	MSG quit;
	memset(&quit, 0, sizeof(quit));
	quit.message = WM_QUIT;
	Queue(quit);
	CHECK(Drain() == "120,120,Q");
}

DWORD WINAPI OtherThread(LPVOID p)
{
	MSG* m = static_cast<MSG*>(p);
	return reinterpret_cast<PeekFnT>(*PeekSlot())(m, NULL, 0, 0, PM_REMOVE);
}

void TestForward()
{
	CHECK(InstallFresh(0));
	const MSG in = Wheel(15);
	Queue(in);
	MSG m;
	// PM_NOREMOVE (the loop's mouse-move peek): the message untouched, still queued
	CHECK(Loop(&m, PM_NOREMOVE) && memcmp(&m, &in, sizeof(m)) == 0 && g_qHead == 0 && g_lastFlags == PM_NOREMOVE);
	// a window filter, a message range
	CHECK(Loop(&m, PM_REMOVE, kWnd) && memcmp(&m, &in, sizeof(m)) == 0);
	Queue(in);
	CHECK(Loop(&m, PM_REMOVE, NULL, WM_MOUSEFIRST, WM_MOUSELAST) && memcmp(&m, &in, sizeof(m)) == 0);
	Queue(in);
	// another thread
	MSG tm;
	memset(&tm, 0, sizeof(tm));
	HANDLE t = CreateThread(NULL, 0, OtherThread, &tm, 0, NULL);
	CHECK(t != NULL);
	if (t)
	{
		WaitForSingleObject(t, 5000);
		DWORD rc = 0;
		GetExitCodeThread(t, &rc);
		CloseHandle(t);
		CHECK(rc == TRUE && memcmp(&tm, &in, sizeof(tm)) == 0);
	}
	CHECK(WT::Forwarded() == 4 && WT::WheelMessages() == 0 && WT::Held() == 0 && WT::State().remainder == 0);
	CHECK(!Loop(&m) && WT::Forwarded() == 4);   // an empty queue through the main loop's call: FALSE
	// pending notches are not taken by a forwarded call
	QueueWheel(240);
	CHECK(Loop(&m) && DeltaOf(m) == 120 && WT::Pending() == 1);
	QueueWheel(15);
	CHECK(Loop(&m, PM_NOREMOVE) && DeltaOf(m) == 15 && WT::Pending() == 1);
	CHECK(Loop(&m) && DeltaOf(m) == 120 && WT::Pending() == 0);
	CHECK(Loop(&m) && m.message == WM_NULL);
	// WM_MOUSEHWHEEL is not touched
	const MSG h = Wheel(15, 0, WM_MOUSEHWHEEL);
	Queue(h);
	CHECK(Loop(&m) && memcmp(&m, &h, sizeof(m)) == 0);
}

void TestTime()
{
	CHECK(InstallFresh(0));
	QueueWheel(60);
	CHECK(Drain() == "N");
	g_now += 400;
	QueueWheel(60);
	CHECK(Drain() == "120");
	QueueWheel(60);
	CHECK(Drain() == "N");
	g_now += 401;
	MSG m;
	CHECK(!Loop(&m) && WT::State().remainder == 0 && WT::State().droppedByTime == 1);   // dropped by the next call
	QueueWheel(60);
	CHECK(Drain() == "N" && WT::State().remainder == 60);
}

void TestFocus()
{
	CHECK(InstallFresh(0));
	QueueWheel(60);
	CHECK(Drain() == "N");
	g_fg = kOther;   // another program in front (the game's WM_ACTIVATE WA_INACTIVE)
	MSG m;
	CHECK(!Loop(&m) && WT::State().remainder == 0 && WT::FocusResets() == 1);
	// scrolling the game's window while another is in front: summed as usual
	QueueWheel(60);
	QueueWheel(60);
	CHECK(Drain() == "N,120" && WT::FocusResets() == 1);
	QueueWheel(60);
	CHECK(Drain() == "N");
	g_fg = kWnd;   // the game in front again (WM_ACTIVATE WA_ACTIVE): the remainder from behind is dropped
	QueueWheel(60);
	CHECK(Drain() == "N" && WT::FocusResets() == 2 && WT::State().remainder == 60);
	g_fg = NULL;   // no foreground window at all
	CHECK(!Loop(&m) && WT::FocusResets() == 3 && WT::State().remainder == 0);
	g_fg = kWnd;
	// pending notches dropped at a foreground change
	QueueWheel(480);
	CHECK(Loop(&m) && DeltaOf(m) == 120 && WT::Pending() == 3);
	g_fg = kOther;
	CHECK(!Loop(&m) && WT::Pending() == 0 && WT::FocusResets() == 4);
	g_fg = kWnd;
	// nothing held: no foreground check at all (a change then drops nothing)
	g_fg = kOther;
	CHECK(!Loop(&m) && WT::FocusResets() == 4);
}

void TestReport()
{
	CHECK(InstallFresh(0));
	QueueWheel(120);
	Drain();
	CHECK(LogCount("wheel: wheel messages 1: passed 1") == 1);
	QueueWheel(15);
	Drain();
	g_now += 9999;
	QueueWheel(15);
	Drain();
	CHECK(LogCount("wheel: wheel messages") == 1);   // within 10 s: no second line
	g_now += 1;   // 10 s after the first
	QueueWheel(15);
	Drain();
	CHECK(LogCount("wheel: wheel messages") == 2 && LogHas("wheel: wheel messages 4: passed 1 (one notch each), held 3"));
	char line[600];
	ProxyWheel::StatusLine(line, sizeof(line));
	CHECK(strstr(line, "smooth wheel: active (slot 0x") == line && strstr(line, "wheel messages 4") != NULL);
}

void TestUnload()
{
	CHECK(InstallFresh(0));
	QueueWheel(15);
	Drain();
	char line[600];
	CHECK(ProxyWheel::OnUnload(line, sizeof(line)));
	CHECK(*PeekSlot() == reinterpret_cast<DWORD>(&FakePeek));
	CHECK(strstr(line, "wheel: the PeekMessageW import slot given back; wheel messages 1: passed 0") == line);
	// a hook written on top of the thunk: left alone
	CHECK(InstallFresh(1));
	*PeekSlot() = 0x33333333;
	CHECK(ProxyWheel::OnUnload(line, sizeof(line)) && *PeekSlot() == 0x33333333);
	*PeekSlot() = reinterpret_cast<DWORD>(&FakePeek);
	// not installed: false, an empty line
	Fresh(0);
	line[0] = 'x';
	CHECK(!ProxyWheel::OnUnload(line, sizeof(line)) && line[0] == 0);
}

void Refused(const char* what, const char* want, bool loud)
{
	const DWORD before = *PeekSlot();
	const char* why = WT::InstallAt(g_base, g_mainTid, FakeApi());
	const bool ok = why != NULL && strstr(why, want) != NULL && *PeekSlot() == before && !WT::Installed() &&
	                (LogHas("LOUD: wheel:") == loud) && LogHas("wheel: every wheel message stays one step, as stock");
	if (!ok) printf("  refusal %s: %s\n", what, why ? why : "(installed)");
	Check(ok, what, __LINE__);
}

void TestRefusals()
{
	Fresh(0);
	reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_magic = 0;
	Refused("no MZ", "off: no EXE image", false);
	Fresh(0);
	reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_lfanew = 0x20000;
	Refused("e_lfanew", "off: no EXE image", false);
	Fresh(0);
	reinterpret_cast<IMAGE_NT_HEADERS*>(g_base + 0x80)->Signature = 0;
	Refused("signature", "off: no EXE image", false);
	Fresh(0);
	reinterpret_cast<IMAGE_NT_HEADERS*>(g_base + 0x80)->FileHeader.TimeDateStamp = 0x12345678;
	Refused("time stamp", "off: an EXE build the patch does not know (time stamp 0x12345678", false);
	Fresh(1);
	reinterpret_cast<IMAGE_NT_HEADERS*>(g_base + 0x80)->OptionalHeader.SizeOfImage = 0x06519000u;   // 1's stamp, 0's size
	Refused("stamp and size of two builds", "off: an EXE build the patch does not know", false);
	Fresh(0);
	Strcpy(kHintPeek + 2, "PeekMessageA");
	Refused("no PeekMessageW", "off: no USER32.dll PeekMessageW import found by name", true);
	Fresh(0);
	Strcpy(kUserName, "GDI32.dll");
	Refused("no USER32 import", "off: no USER32.dll PeekMessageW import found by name", true);
	Fresh(0);
	*PeekSlot() = 0;
	Refused("unbound slot", "off: the PeekMessageW import slot is not bound", true);
	Fresh(0);
	*PeekSlot() = reinterpret_cast<DWORD>(WT::Thunk());
	Refused("slot already the thunk", "off: the import slot already holds this module's thunk", true);
	*PeekSlot() = reinterpret_cast<DWORD>(&FakePeek);
	Refused("a second call after a refusal", "off: the import slot already holds", true);   // the first answer kept
}

void TestProduct()
{
	Fresh(0);
	ProxyWheel::Install(false, "FixSmoothWheel = 0");
	CHECK(LogCount("wheel: off (FixSmoothWheel = 0): every wheel message stays one step, as stock (ticket 0755)") == 1);
	CHECK(!WT::Installed() && *PeekSlot() == reinterpret_cast<DWORD>(&FakePeek));
	char line[600];
	ProxyWheel::StatusLine(line, sizeof(line));
	CHECK(strstr(line, "smooth wheel: off by the options file") == line);
	CHECK(!ProxyWheel::OnUnload(line, sizeof(line)));
	Fresh(0);
	ProxyWheel::Install(false, "EnableBugfixes = 0");
	CHECK(LogHas("wheel: off (EnableBugfixes = 0)"));
	// on, in this program (not the EXE): refused by the time stamp, quiet, nothing written in this image
	Fresh(0);
	ProxyWheel::Install(true, NULL);
	CHECK(!WT::Installed() && strstr(WT::Status(), "off: an EXE build the patch does not know") == WT::Status() &&
	      !LogHas("LOUD"));
	WT::Reset();
}
}  // namespace

int main(int argc, char** argv)
{
	bool expectFail = false;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--expect-fail") == 0) expectFail = true;
		if (strcmp(argv[i], "-v") == 0) g_verbose = true;
	}
	g_base = static_cast<unsigned char*>(VirtualAlloc(NULL, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!g_base)
	{
		printf("FAIL: VirtualAlloc\n");
		return 1;
	}
	TestCore();
	TestInstall(0);
	TestInstall(1);
	TestNotched();
	TestSumming();
	TestMerged();
	TestForward();
	TestTime();
	TestFocus();
	TestReport();
	TestUnload();
	TestRefusals();
	TestProduct();
	WT::Reset();
	if (expectFail) CHECK(!"--expect-fail: this check fails on purpose");
	VirtualFree(g_base, 0, MEM_RELEASE);
	printf("wheel_test: %d/%d checks passed\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
