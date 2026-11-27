// lockstep_server.cpp - stand-in version-3 server for the lockstep tests of shell_composite.dll (issue 3304, epic 3300).
// Test program only (built by shell\build.cmd into bin\lockstep_server.exe); never in the product.
//
// Speaks the version-3 section of shell/common/shell_shared.h exactly as shell_server.exe does (texture block, keyed
// mutex + copy, frame block, depth block with camFrameStamp written before ReleaseSync(1), the client's resize request),
// but draws a picture whose every pixel says which camera it was drawn with, so a test can check the composite's take:
//   colour: transparent (0,0,0,0) outside --region; inside it the frame id F of the camera block it drew
//           (R = (F >> 16) & 255, G = (F >> 8) & 255, B = F & 255, A = 255; the encoding of shell/frame/standin_util.h);
//   depth:  R32_FLOAT, 1.0 outside --region, --depth-value inside it (NDC z, VPDEPTH_NDC_Z).
// It draws exactly the newest camera block (frameStamp changed), woken by <base>.Cam (vp_frame.h) or by polling.
// Test switches make it slow, frozen, stalled for a while, or killed after N frames.
//
//   lockstep_server.exe --section NAME --name TEXBASE [--seconds S] [--log PATH] [--size WxH] [--region x0,y0,x1,y1]
//                       [--depth-value D] [--delay-us N] [--stall-at S --stall-for S] [--exit-after N] [--no-depth-block]
//                       [--wake event|poll] [--stall-on PATH] [--stall-heartbeat-stop] [--hold-for-exe N,M]
// --hold-for-exe N,M (ticket 0691, MC #11169): as server.exe's fog at a load: the first camera with a frame id >= N is
// HELD (not drawn) until the EXE has presented M further frames (the camera block's presentCount); every camera meanwhile
// is held too. The hold is reported as server.exe reports it (vp_frame.h VPFrameServer::heldFrameId / holdReasons =
// VPHOLD_TEST | VPHOLD_FOG_NO_TABLE, in <section>.Frame) unless --hold-unreported (a server before the report). M < 0:
// the hold never clears (a stuck hold). --hold-on PATH: the hold begins at the first camera >= N after PATH exists (looked
// for every 5 ms, as --stall-on), so it falls on the composite's lockstep frames however long its hook commit took.
// --stall-heartbeat-stop (ticket 0691): no heartbeat during the stall either (a hung render thread); without it the
// heartbeat ticks through the stall (a server alive that delivers no frame).
// --stall-on PATH (ticket 0494): the stall of --stall-for starts when PATH exists (looked for every 5 ms), once, not at
// a time on this process's clock: under load the host's hook commit and the UpdateViews hook's enable took seconds and
// put the host's first lockstep frames after a stall at 3.0 s (or after 600 drawn frames). The test makes the file.
// Exit: 0 done, 2 arguments, 3 device, 4 section, 9 --exit-after (TerminateProcess, as a killed server).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <share.h>
#include <algorithm>
#include "shell_shared.h"
#include "vp_frame.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
static int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static int64_t g_freq;
static double Ms(int64_t d) { return (double)d * 1000.0 / (double)g_freq; }
static FILE* g_log;
static void Log(const char* fmt, ...)
{
	char b[1024];
	va_list a; va_start(a, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a); va_end(a);
	SYSTEMTIME t; GetLocalTime(&t);
	FILE* f = g_log ? g_log : stdout;
	fprintf(f, "%02u:%02u:%02u.%03u %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, b);
	fflush(f);
}

struct Opts
{
	const wchar_t* section = nullptr;
	const wchar_t* texBase = nullptr;
	const wchar_t* log = nullptr;
	double seconds = 30;
	UINT w = 640, h = 360;
	int rx0 = 0, ry0 = 0, rx1 = 1 << 20, ry1 = 1 << 20;
	float depthValue = 0.5f;
	double delayUs = 0;
	double stallAt = 0, stallFor = 0;
	const wchar_t* stallOn = nullptr;
	int64_t exitAfter = 0;
	bool depthBlock = true;
	bool wakeEvent = true;
	bool stallHbStop = false;   // ticket 0691
	int64_t holdAt = 0, holdPresents = 0;   // ticket 0691 (MC #11169): --hold-for-exe N,M
	bool holdReport = true;
	const wchar_t* holdOn = nullptr;
};

struct Server
{
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	uint64_t luid = 0;
	VPShellHeader* hdr = nullptr;
	HANDLE map = nullptr;
	UINT w = 0, h = 0;
	uint32_t gen = 0;
	ID3D11Texture2D* privCol = nullptr; ID3D11RenderTargetView* privColRtv = nullptr;
	ID3D11Texture2D* privDep = nullptr; ID3D11RenderTargetView* privDepRtv = nullptr;
	ID3D11Texture2D* regCol = nullptr; ID3D11RenderTargetView* regColRtv = nullptr;   // region-sized, copied in at (x0, y0)
	ID3D11Texture2D* regDep = nullptr; ID3D11RenderTargetView* regDepRtv = nullptr;
	ID3D11Texture2D* shCol = nullptr; IDXGIKeyedMutex* km = nullptr; HANDLE shColH = nullptr;
	ID3D11Texture2D* shDep = nullptr; HANDLE shDepH = nullptr;
	ID3D11Query* sync = nullptr;
	wchar_t colName[160] = L"", depName[160] = L"";
	int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;   // the region clipped to the texture
};

static void ReleaseTextures(Server& s)
{
	SafeRelease(s.privColRtv); SafeRelease(s.privCol); SafeRelease(s.privDepRtv); SafeRelease(s.privDep);
	SafeRelease(s.regColRtv); SafeRelease(s.regCol); SafeRelease(s.regDepRtv); SafeRelease(s.regDep);
	SafeRelease(s.km); SafeRelease(s.shCol); SafeRelease(s.shDep);
	if (s.shColH) { CloseHandle(s.shColH); s.shColH = nullptr; }
	if (s.shDepH) { CloseHandle(s.shDepH); s.shDepH = nullptr; }
}

static bool MakeTex(Server& s, UINT w, UINT h, DXGI_FORMAT f, UINT bind, UINT misc, ID3D11Texture2D** t, ID3D11RenderTargetView** rtv)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = f; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = bind; d.MiscFlags = misc;
	if (FAILED(s.dev->CreateTexture2D(&d, nullptr, t))) return false;
	return !rtv || SUCCEEDED(s.dev->CreateRenderTargetView(*t, nullptr, rtv));
}

static void PublishDepthBlock(Server& s, const Opts& o, int64_t frameCounter, int64_t stamp, const VPShellCamera& cam, uint32_t state)
{
	if (!o.depthBlock) return;
	VPShellDepth* d = VPSHELL_DEPTH(s.hdr);
	InterlockedIncrement((volatile LONG*)&d->seq);
	MemoryBarrier();
	d->state = state; d->generation = s.gen; d->width = s.w; d->height = s.h; d->dxgiFormat = DXGI_FORMAT_R32_FLOAT;
	d->encoding = VPDEPTH_NDC_Z; d->frameCounter = frameCounter;
	wcsncpy_s(d->textureName, s.depName, _TRUNCATE);
	d->camFrameStamp = stamp;
	d->proj22 = cam.proj[10]; d->proj32 = cam.proj[14]; d->nearZ = cam.nearZ; d->farZ = cam.farZ;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&d->seq);
}

static bool CreateTextures(Server& s, const Opts& o, UINT w, UINT h)
{
	ReleaseTextures(s);
	s.w = w; s.h = h; s.gen++;
	s.rx0 = std::max(0, std::min((int)w, o.rx0)); s.ry0 = std::max(0, std::min((int)h, o.ry0));
	s.rx1 = std::max(s.rx0, std::min((int)w, o.rx1)); s.ry1 = std::max(s.ry0, std::min((int)h, o.ry1));
	const UINT rt = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (!MakeTex(s, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rt, 0, &s.privCol, &s.privColRtv) ||
		!MakeTex(s, w, h, DXGI_FORMAT_R32_FLOAT, rt, 0, &s.privDep, &s.privDepRtv)) return false;
	if (s.rx1 > s.rx0 && s.ry1 > s.ry0 &&
		(!MakeTex(s, s.rx1 - s.rx0, s.ry1 - s.ry0, DXGI_FORMAT_B8G8R8A8_UNORM, rt, 0, &s.regCol, &s.regColRtv) ||
		 !MakeTex(s, s.rx1 - s.rx0, s.ry1 - s.ry0, DXGI_FORMAT_R32_FLOAT, rt, 0, &s.regDep, &s.regDepRtv))) return false;
	if (!MakeTex(s, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rt, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE, &s.shCol, nullptr) ||
		!MakeTex(s, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE, &s.shDep, nullptr))
		return false;
	_snwprintf_s(s.colName, _TRUNCATE, L"%ls_%u_%u", o.texBase, GetCurrentProcessId(), s.gen);
	_snwprintf_s(s.depName, _TRUNCATE, L"%lsDepth_%u_%u", o.texBase, GetCurrentProcessId(), s.gen);
	IDXGIResource1* r = nullptr;
	if (FAILED(s.shCol->QueryInterface(__uuidof(IDXGIResource1), (void**)&r)) ||
		FAILED(r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, s.colName, &s.shColH))) { SafeRelease(r); return false; }
	SafeRelease(r);
	if (FAILED(s.shDep->QueryInterface(__uuidof(IDXGIResource1), (void**)&r)) ||
		FAILED(r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, s.depName, &s.shDepH))) { SafeRelease(r); return false; }
	SafeRelease(r);
	if (FAILED(s.shCol->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&s.km))) return false;
	// texture block (seqlock)
	VPShellHeader* hd = s.hdr;
	InterlockedIncrement((volatile LONG*)&hd->texSeq);
	MemoryBarrier();
	hd->serverPid = GetCurrentProcessId(); hd->generation = s.gen; hd->width = w; hd->height = h;
	hd->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM; hd->adapterLuid = s.luid;
	wcsncpy_s(hd->textureName, s.colName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&hd->texSeq);
	VPShellCamera none = {};
	PublishDepthBlock(s, o, 0, 0, none, 1);
	Log("texture generation %u: %ux%u '%ls', depth '%ls', region %d,%d..%d,%d", s.gen, w, h, s.colName, s.depName, s.rx0, s.ry0, s.rx1, s.ry1);
	return true;
}

static bool SnapCamera(VPShellHeader* hdr, VPShellCamera& out)
{
	const VPShellCamera* c = VPSHELL_CAMERA(hdr);
	for (int i = 0; i < 8; ++i)
	{
		uint32_t s1 = c->seq;
		if (!s1 || (s1 & 1)) { YieldProcessor(); continue; }
		MemoryBarrier();
		memcpy(&out, (const void*)c, sizeof(out));
		MemoryBarrier();
		if (c->seq == s1) return true;
	}
	return false;
}

int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	Opts o;
	for (int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i]; const wchar_t* v = i + 1 < argc ? argv[i + 1] : nullptr;
		if (!wcscmp(a, L"--no-depth-block")) { o.depthBlock = false; continue; }
		if (!wcscmp(a, L"--stall-heartbeat-stop")) { o.stallHbStop = true; continue; }
		if (!wcscmp(a, L"--hold-unreported")) { o.holdReport = false; continue; }
		if (!v) { fwprintf(stderr, L"lockstep_server: %ls needs a value\n", a); return 2; }
		++i;
		if (!wcscmp(a, L"--section")) o.section = v;
		else if (!wcscmp(a, L"--name")) o.texBase = v;
		else if (!wcscmp(a, L"--log")) o.log = v;
		else if (!wcscmp(a, L"--seconds")) o.seconds = _wtof(v);
		else if (!wcscmp(a, L"--size")) { if (swscanf_s(v, L"%ux%u", &o.w, &o.h) != 2 || !o.w || !o.h) { fwprintf(stderr, L"bad --size\n"); return 2; } }
		else if (!wcscmp(a, L"--region")) { if (swscanf_s(v, L"%d,%d,%d,%d", &o.rx0, &o.ry0, &o.rx1, &o.ry1) != 4) { fwprintf(stderr, L"bad --region\n"); return 2; } }
		else if (!wcscmp(a, L"--depth-value")) o.depthValue = (float)_wtof(v);
		else if (!wcscmp(a, L"--delay-us")) o.delayUs = _wtof(v);
		else if (!wcscmp(a, L"--stall-at")) o.stallAt = _wtof(v);
		else if (!wcscmp(a, L"--stall-for")) o.stallFor = _wtof(v);
		else if (!wcscmp(a, L"--stall-on")) o.stallOn = v;
		else if (!wcscmp(a, L"--hold-on")) o.holdOn = v;
		else if (!wcscmp(a, L"--exit-after")) o.exitAfter = _wtoi64(v);
		else if (!wcscmp(a, L"--hold-for-exe")) { if (swscanf_s(v, L"%lld,%lld", &o.holdAt, &o.holdPresents) != 2 || o.holdAt <= 0) { fwprintf(stderr, L"bad --hold-for-exe N,M\n"); return 2; } }
		else if (!wcscmp(a, L"--wake")) { if (!wcscmp(v, L"poll")) o.wakeEvent = false; else if (wcscmp(v, L"event")) { fwprintf(stderr, L"--wake event|poll\n"); return 2; } }
		else { fwprintf(stderr, L"lockstep_server: unknown argument %ls\n", a); return 2; }
	}
	if (!o.section || !o.texBase) { fwprintf(stderr, L"lockstep_server: --section and --name are required\n"); return 2; }
	if (o.log) g_log = _wfsopen(o.log, L"a", _SH_DENYNO);
	Log("lockstep_server start pid %u section %ls region %d,%d..%d,%d depth %.4f delay %.0f us stall %.1f+%.1f s on '%ls' exit-after %lld depth-block %d wake %s heartbeat-stop %d hold-for-exe %lld,%lld reported %d",
		GetCurrentProcessId(), o.section, o.rx0, o.ry0, o.rx1, o.ry1, o.depthValue, o.delayUs, o.stallAt, o.stallFor,
		o.stallOn ? o.stallOn : L"-", o.exitAfter, (int)o.depthBlock, o.wakeEvent ? "event" : "poll", (int)o.stallHbStop, o.holdAt, o.holdPresents, (int)o.holdReport);

	Server s;
	D3D_FEATURE_LEVEL fl;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &s.dev, &fl, &s.ctx)))
	{ Log("device creation failed"); return 3; }
	{
		IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr;
		if (SUCCEEDED(s.dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && SUCCEEDED(xd->GetAdapter(&ad)))
		{ DXGI_ADAPTER_DESC d; ad->GetDesc(&d); s.luid = ((uint64_t)(uint32_t)d.AdapterLuid.HighPart << 32) | d.AdapterLuid.LowPart; }
		SafeRelease(ad); SafeRelease(xd);
	}
	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	s.dev->CreateQuery(&qd, &s.sync);
	s.map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, o.section);
	if (!s.map) { Log("section '%ls' failed %u", o.section, GetLastError()); return 4; }
	s.hdr = (VPShellHeader*)MapViewOfFile(s.map, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if (!s.hdr) { Log("map failed %u", GetLastError()); return 4; }
	s.hdr->version = VPSHELL_VERSION;
	s.hdr->qpcFrequency = g_freq;
	if (!CreateTextures(s, o, o.w, o.h)) { Log("textures failed"); return 3; }
	MemoryBarrier();
	s.hdr->magic = VPSHELL_MAGIC;
	wchar_t en[256]; _snwprintf_s(en, _TRUNCATE, L"%ls%ls", o.section, VPFRAME_CAM_EVENT_SUFFIX);
	HANDLE cam = CreateEventW(nullptr, FALSE, FALSE, en);
	if (!cam) { Log("event '%ls' failed %u", en, GetLastError()); return 4; }
	// ticket 0691: the hold report lives in <section>.Frame (created-or-opened, as both sides do; only the hold fields written)
	VPFrameServer* fsrv = nullptr;
	if (o.holdAt)
	{
		wchar_t fn[256]; _snwprintf_s(fn, _TRUNCATE, L"%ls%ls", o.section, VPFRAME_SECTION_SUFFIX);
		HANDLE fm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPFRAME_SECTION_BYTES, fn);
		void* fv = fm ? MapViewOfFile(fm, FILE_MAP_ALL_ACCESS, 0, 0, VPFRAME_SECTION_BYTES) : nullptr;
		if (!fv) { Log("frame section '%ls' failed %u", fn, GetLastError()); return 4; }
		fsrv = VPFRAME_SERVER(fv);   // the mapping stays open until the process ends
		InterlockedExchange64(&fsrv->heldFrameId, 0);
		InterlockedExchange((volatile LONG*)&fsrv->holdReasons, 0);
	}
	int64_t holdPc0 = -1, heldNow = 0, heldN = 0;   // presentCount at the hold's start (-1: not begun); the frame held now
	bool holdDone = false, holdArmed = false;
	double nextHoldLook = 0;

	const int64_t start = Qpc();
	int64_t last = 0, frame = 0, drawn = 0, secStart = start, secDrawn = 0, stalled = 0;
	uint32_t ack = s.hdr->ackSeq;
	double maxDrawMs = 0;
	bool stallLogged = false, resumeLogged = false, stallArmed = false, hbStopped = false;
	double nextLook = 0;
	while (Ms(Qpc() - start) < o.seconds * 1000.0)
	{
		int64_t now = Qpc();
		if (!hbStopped) InterlockedExchange64(&s.hdr->heartbeatQpc, now);
		uint32_t req = s.hdr->reqSeq;
		if (req != ack)
		{
			ack = req;
			UINT rw = s.hdr->reqWidth, rh = s.hdr->reqHeight;
			if (rw && rh && rw <= 8192 && rh <= 8192 && (rw != s.w || rh != s.h)) { if (!CreateTextures(s, o, rw, rh)) { Log("textures failed"); return 3; } }
			s.hdr->ackSeq = ack;
		}
		VPShellCamera c;
		const double el = Ms(now - start) / 1000.0;
		if (o.stallOn && !stallArmed && el >= nextLook)   // --stall-on: the stall starts when the file appears
		{
			nextLook = el + 0.005;
			if (GetFileAttributesW(o.stallOn) != INVALID_FILE_ATTRIBUTES) { stallArmed = true; o.stallAt = el; }
		}
		const bool armed = !o.stallOn || stallArmed;
		const bool inStall = armed && o.stallFor > 0 && el >= o.stallAt && el < o.stallAt + o.stallFor;
		hbStopped = inStall && o.stallHbStop;   // ticket 0691: from the next iteration on
		if (inStall && !stallLogged) { stallLogged = true; Log("stall: no frames from %.2f s for %.2f s", el, o.stallFor); }
		if (!inStall && stallLogged && !resumeLogged) { resumeLogged = true; Log("stall over at %.2f s", el); }
		bool hold = false;
		if (o.holdOn && !holdArmed && el >= nextHoldLook)
		{
			nextHoldLook = el + 0.005;
			holdArmed = GetFileAttributesW(o.holdOn) != INVALID_FILE_ATTRIBUTES;
		}
		if (!inStall && o.holdAt && !holdDone && (!o.holdOn || holdArmed) && SnapCamera(s.hdr, c) && c.frameStamp >= o.holdAt)   // ticket 0691: --hold-for-exe
		{
			const int64_t pc = InterlockedCompareExchange64(&VPSHELL_CAMERA(s.hdr)->presentCount, 0, 0);
			if (holdPc0 < 0) { holdPc0 = pc; Log("hold: frame %lld held from present %lld until %lld more presents (%s)", c.frameStamp, pc, o.holdPresents, o.holdReport ? "reported" : "NOT reported"); }
			if (o.holdPresents >= 0 && pc - holdPc0 >= o.holdPresents)
			{
				holdDone = true;
				Log("hold: over at present %lld (%lld frame ids held, the last %lld); drawing frame %lld", pc, heldN, heldNow, c.frameStamp);
			}
			else
			{
				hold = true;
				if (c.frameStamp != heldNow)
				{
					heldNow = c.frameStamp; heldN++;
					if (fsrv && o.holdReport)   // vp_frame.h's writer order: the reasons, then the frame id
					{
						InterlockedExchange64(&fsrv->heldFrameId, 0);
						InterlockedExchange((volatile LONG*)&fsrv->holdReasons, (LONG)(VPHOLD_TEST | VPHOLD_FOG_NO_TABLE));
						InterlockedExchange64(&fsrv->heldFrameId, heldNow);
						InterlockedIncrement64(&fsrv->heldFramesTotal);
					}
				}
			}
		}
		if (!inStall && !hold && SnapCamera(s.hdr, c) && c.frameStamp != last)
		{
			int64_t t0 = Qpc();
			const int64_t F = c.frameStamp;
			last = F;
			const float clear[4] = { 0, 0, 0, 0 }, one[4] = { 1, 1, 1, 1 };
			const float col[4] = { (float)((F >> 16) & 255) / 255.0f, (float)((F >> 8) & 255) / 255.0f, (float)(F & 255) / 255.0f, 1.0f };
			const float dv[4] = { o.depthValue, 0, 0, 0 };
			s.ctx->ClearRenderTargetView(s.privColRtv, clear);
			s.ctx->ClearRenderTargetView(s.privDepRtv, one);
			if (s.regColRtv)
			{
				s.ctx->ClearRenderTargetView(s.regColRtv, col);
				s.ctx->ClearRenderTargetView(s.regDepRtv, dv);
				s.ctx->CopySubresourceRegion(s.privCol, 0, s.rx0, s.ry0, 0, s.regCol, 0, nullptr);
				s.ctx->CopySubresourceRegion(s.privDep, 0, s.rx0, s.ry0, 0, s.regDep, 0, nullptr);
			}
			if (o.delayUs > 0) { int64_t end = Qpc() + (int64_t)(o.delayUs * (double)g_freq / 1e6); while (Qpc() < end) YieldProcessor(); }
			// publish as shell_server.exe does: take the texture from whichever side holds it free
			bool got = false;
			int64_t a0 = Qpc();
			for (;;)
			{
				HRESULT ar = s.km->AcquireSync(0, 0);
				if (ar == S_OK) { got = true; break; }
				ar = s.km->AcquireSync(1, 0);
				if (ar == S_OK) { got = true; break; }
				if (ar != (HRESULT)WAIT_TIMEOUT && FAILED(ar)) { Log("AcquireSync error 0x%08X", ar); break; }
				if (Ms(Qpc() - a0) > 50.0) break;
				Sleep(0);
			}
			if (got)
			{
				s.ctx->CopyResource(s.shCol, s.privCol);
				s.ctx->CopyResource(s.shDep, s.privDep);
				s.ctx->End(s.sync);
				s.ctx->Flush();
				while (s.ctx->GetData(s.sync, nullptr, 0, 0) == S_FALSE) YieldProcessor();
				frame++;
				s.hdr->frameCounter = frame;
				s.hdr->frameQpc = Qpc();
				PublishDepthBlock(s, o, frame, F, c, 1);
				s.km->ReleaseSync(1);
				s.ctx->Flush();
				drawn++; secDrawn++;
				if (fsrv && heldNow) { InterlockedExchange64(&fsrv->heldFrameId, 0); InterlockedExchange((volatile LONG*)&fsrv->holdReasons, 0); heldNow = 0; }
				maxDrawMs = std::max(maxDrawMs, Ms(Qpc() - t0));
				if (o.exitAfter && frame >= o.exitAfter)
				{
					Log("exit-after: terminating after frame %lld (camera %lld)", frame, F);
					TerminateProcess(GetCurrentProcess(), 9);
				}
			}
		}
		else if (inStall) stalled++;
		now = Qpc();
		if (Ms(now - secStart) >= 1000.0)
		{
			Log("1s: drawn %lld last camera %lld frame %lld max draw %.2f ms gen %u %ux%u", secDrawn, last, frame, maxDrawMs, s.gen, s.w, s.h);
			secStart = now; secDrawn = 0; maxDrawMs = 0;
		}
		if (o.wakeEvent) WaitForSingleObject(cam, 1);
		else { int64_t end = Qpc() + g_freq / 4000; while (Qpc() < end) YieldProcessor(); }
	}
	Log("lockstep_server totals: drawn %lld frames %lld last camera %lld", drawn, frame, last);
	s.hdr->magic = 0;
	ReleaseTextures(s);
	SafeRelease(s.sync);
	CloseHandle(cam);
	UnmapViewOfFile(s.hdr);
	CloseHandle(s.map);
	SafeRelease(s.ctx); SafeRelease(s.dev);
	if (g_log) fclose(g_log);
	return 0;
}
