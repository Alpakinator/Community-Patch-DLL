// ring_producer.cpp - stand-in producer of the frame hand-over (issue 3302), x64: plays the server's render host. Waits on
// <base>.Cam, reads the publish block (vp_frame.h), draws the picture of frame F (standin_util.h encoding: a clear plus
// --passes full-screen passes of a shader with --iters loop steps, all writing the same colour; depth cleared to F's
// value) and publishes it through RingSink (--mode ring) or through a keyed-mutex shared texture with copy and GPU wait,
// the version-3 hand-over of shell_server.exe (--mode v3). Creates no window. Test program only.
//
//   ring_producer.exe --base NAME [--mode ring|v3] [--sync fence|cpu] [--slots N] [--seconds S] [--idle-exit S]
//                     [--passes N] [--iters N] [--width W --height H (v3)] [--log PATH] [--test-skip-cpu-wait]
//                     [--unbaked N | --unbaked frame]
// --unbaked (issue 0139): the slot record's count of land tiles drawn without their bake: N for every frame, or
// `frame` = F % 1000 (each frame its own count); without it the record says "not counted".
// Exit: 0 done, 2 arguments, 3 device, 4 sink/section refused (another live server), 5 v3 setup failed.
#include "standin_util.h"
#include "../ring_sink.h"
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace standin;
using vpframe::ring::SafeRelease;

int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const wchar_t* base = nullptr; const wchar_t* logPath = nullptr;
	bool v3 = false, skipCpuWait = false, clearColour = true, busyRetry = true;
	int64_t busyRetried = 0;
	uint32_t sync = VPSYNC_FENCE, slots = VPFRAME_DEFAULT_SLOTS, iters = 0, w3 = 0, h3 = 0;
	int passes = 0;
	int unbaked = -1; bool unbakedFrame = false;
	double seconds = 60, idleExit = 0;
	for (int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i]; const wchar_t* v = i + 1 < argc ? argv[i + 1] : nullptr;
		if (ArgIs(a, L"--test-skip-cpu-wait")) { skipCpuWait = true; continue; }
		if (!v) { fwprintf(stderr, L"ring_producer: %ls needs a value\n", a); return 2; }
		++i;
		if (ArgIs(a, L"--base")) base = v;
		else if (ArgIs(a, L"--log")) logPath = v;
		else if (ArgIs(a, L"--mode")) { if (!wcscmp(v, L"v3")) v3 = true; else if (wcscmp(v, L"ring")) { fwprintf(stderr, L"ring_producer: --mode ring|v3\n"); return 2; } }
		else if (ArgIs(a, L"--sync")) { if (!wcscmp(v, L"cpu")) sync = VPSYNC_CPU; else if (wcscmp(v, L"fence")) { fwprintf(stderr, L"ring_producer: --sync fence|cpu\n"); return 2; } }
		else if (ArgIs(a, L"--slots")) slots = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--seconds")) seconds = _wtof(v);
		else if (ArgIs(a, L"--idle-exit")) idleExit = _wtof(v);
		else if (ArgIs(a, L"--passes")) passes = _wtoi(v);
		else if (ArgIs(a, L"--busy-retry")) busyRetry = _wtoi(v) != 0;
		else if (ArgIs(a, L"--iters")) iters = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--clear")) clearColour = _wtoi(v) != 0;
		else if (ArgIs(a, L"--width")) w3 = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--height")) h3 = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--unbaked"))
		{
			if (!wcscmp(v, L"frame")) unbakedFrame = true;
			else { wchar_t* e = nullptr; long n = wcstol(v, &e, 10); if (!*v || *e || n < 0 || n > 100000) { fwprintf(stderr, L"ring_producer: --unbaked N (0..100000) | frame\n"); return 2; } unbaked = (int)n; }
		}
		else { fwprintf(stderr, L"ring_producer: unknown argument %ls\n", a); return 2; }
	}
	if (!base || !*base) { fwprintf(stderr, L"ring_producer: --base is required\n"); return 2; }
	if (v3 && (!w3 || !h3)) { fwprintf(stderr, L"ring_producer: --mode v3 needs --width and --height\n"); return 2; }
	Log log; log.open(logPath);

	ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; uint64_t luid = 0;
	if (!CreateDevice(dev, ctx, luid)) { log.line("device creation failed"); return 3; }
	Gpu gpu;
	if (!gpu.init(dev)) { log.line("shaders failed"); return 3; }
	log.line("ring_producer pid %u base %ls mode %s sync %s slots %u passes %d iters %u luid %016llX", GetCurrentProcessId(), base,
		v3 ? "v3" : "ring", sync == VPSYNC_FENCE ? "fence" : "cpu", slots, passes, iters, luid);

	vpframe::RingSink sink;
	sink.setLog(&Log::Cb, &log);
	sink.testSkipCpuWait(skipCpuWait);
	vpframe::SinkConfig cfg; cfg.baseName = base; cfg.slots = slots; cfg.sync = sync; cfg.adapterLuid = luid;
	HANDLE cam = nullptr;
	void* sec = nullptr; HANDLE secMap = nullptr;
	// v3 arm objects
	HANDLE frm = nullptr, v3Map = nullptr; V3Block* v3b = nullptr;
	ID3D11Texture2D* priv = nullptr; ID3D11RenderTargetView* privRtv = nullptr; ID3D11Texture2D* shared = nullptr;
	IDXGIKeyedMutex* km = nullptr; HANDLE sharedH = nullptr; ID3D11Query* doneQ = nullptr;
	if (!v3)
	{
		if (!sink.open(dev, ctx, cfg)) { log.line("sink open refused"); return 4; }
		cam = sink.camEvent();
		sec = const_cast<void*>(sink.section());
	}
	else
	{
		sec = vpframe::ring::MapSection(base, secMap);
		cam = vpframe::ring::OpenEvent_(base, VPFRAME_CAM_EVENT_SUFFIX);
		frm = vpframe::ring::OpenEvent_(base, VPFRAME_FRM_EVENT_SUFFIX);
		wchar_t n[256]; vpframe::ring::Name(n, 256, base, L".V3");
		v3Map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, n);
		v3b = v3Map ? (V3Block*)MapViewOfFile(v3Map, FILE_MAP_ALL_ACCESS, 0, 0, 0) : nullptr;
		D3D11_TEXTURE2D_DESC td = {}; td.Width = w3; td.Height = h3; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		bool ok = sec && cam && frm && v3b && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &priv)) &&
			SUCCEEDED(dev->CreateRenderTargetView(priv, nullptr, &privRtv));
		td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
		ok = ok && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &shared)) && SUCCEEDED(shared->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km));
		IDXGIResource1* r1 = nullptr;
		if (ok)
		{
			_snwprintf_s(v3b->texName, _countof(v3b->texName), _TRUNCATE, L"%ls.V3Tex.%u", base, GetCurrentProcessId());
			ok = SUCCEEDED(shared->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1)) &&
				SUCCEEDED(r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, v3b->texName, &sharedH));
		}
		SafeRelease(r1);
		D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
		ok = ok && SUCCEEDED(dev->CreateQuery(&qd, &doneQ));
		if (!ok) { log.line("v3 setup failed"); return 5; }
		v3b->width = w3; v3b->height = h3; v3b->generation++; v3b->frameId = 0; v3b->published = 0;
		VPFRAME_SERVER(sec)->qpcFrequency = QpcFreq();
		log.line("v3: keyed-mutex texture %ux%u '%ls'", w3, h3, v3b->texName);
	}

	VPFramePublish* pub = VPFRAME_PUBLISH(sec);
	int64_t lastF = 0, draws = 0, busy = 0, notReady = 0, errors = 0, skippedCams = 0, v3Overwritten = 0, v3Timeouts = 0;
	double sumCpu = 0, sumPublish = 0;
	const int64_t t0 = Qpc(), freq = QpcFreq();
	int64_t lastCam = t0, lastSec = t0;
	bool sawCam = false;
	uint32_t lastExePid = 0;
	for (;;)
	{
		int64_t now = Qpc();
		if ((double)(now - t0) / freq > seconds) { log.line("seconds reached"); break; }
		if (idleExit > 0 && sawCam && (double)(now - lastCam) / freq > idleExit) { log.line("idle %.1f s: exit", idleExit); break; }
		if (!v3) sink.tick(now);
		if ((double)(now - lastSec) / freq >= 1.0)
		{
			log.line("1s: draws %lld busy %lld notready %lld errors %lld camskipped %lld gen %u fence signalled %llu completed %llu", draws, busy, notReady, errors, skippedCams, sink.generation(), sink.fenceSignalled(), sink.fenceCompleted());
			lastSec = now;
		}
		if (WaitForSingleObject(cam, 100) != WAIT_OBJECT_0) continue;
		int64_t wake = Qpc();
		// the newest published camera (publish block seqlock)
		int64_t F = 0, pubQpc = 0;
		for (int tries = 0; tries < 8; ++tries)
		{
			uint32_t a = pub->seq; MemoryBarrier();
			if (a & 1) { YieldProcessor(); continue; }
			F = pub->frameId; pubQpc = pub->publishQpc; MemoryBarrier();
			if (pub->seq == a) break;
			F = 0;
		}
		// A new composite (another EXE pid) numbers its frames from 1 again: forget the old one's newest frame. The render
		// host (3301) needs the same rule.
		uint32_t exePid = pub->exePid;
		if (exePid != lastExePid) { if (lastExePid) log.line("consumer pid %u -> %u: frame ids restart", lastExePid, exePid); lastExePid = exePid; lastF = 0; }
		if (F <= lastF) continue;
		sawCam = true; lastCam = wake;
		if (lastF && F > lastF + 1) skippedCams += F - lastF - 1;
		lastF = F;
		vpframe::FrameInfo info; info.frameId = F; info.publishQpc = pubQpc; info.wakeQpc = wake;
		float col[4]; FrameColour(F, col);
		if (!v3)
		{
			vpframe::FrameTargets t;
			vpframe::SinkStatus st = sink.begin(info, t);
			if (st == vpframe::SinkStatus::Busy && busyRetry)
			{
				// No free slot: the composite's GPU still reads the others. Retry until the camera's deadline (a slot is
				// given back when the composite polls its event query) rather than skip the camera at once.
				int64_t dl = pub->deadlineQpc;
				while (st == vpframe::SinkStatus::Busy && Qpc() < dl && pub->frameId == F)
				{
					YieldProcessor();
					if (sink.freeSlots()) st = sink.begin(info, t);
				}
				if (st == vpframe::SinkStatus::Ok) busyRetried++;
			}
			if (st == vpframe::SinkStatus::Busy) { busy++; continue; }
			if (st != vpframe::SinkStatus::Ok) { notReady++; continue; }
			int64_t c0 = Qpc();
			if (clearColour) ctx->ClearRenderTargetView(t.colour, col);
			ctx->ClearDepthStencilView(t.depth, D3D11_CLEAR_DEPTH, FrameDepth(F), 0);
			gpu.load(ctx, t.colour, t.width, t.height, col, iters, passes);
			vpframe::DrawReport rep; rep.cpuMs = (float)Ms(Qpc() - c0);
			rep.tilesUnbaked = unbakedFrame ? (int32_t)(F % 1000) : unbaked;
			int64_t e0 = Qpc();
			st = sink.end(info, rep);
			sumPublish += Ms(Qpc() - e0); sumCpu += rep.cpuMs;
			if (st == vpframe::SinkStatus::Ok) draws++; else errors++;
		}
		else
		{
			int64_t c0 = Qpc();
			ctx->ClearRenderTargetView(privRtv, col);
			gpu.load(ctx, privRtv, w3, h3, col, iters, passes);
			sumCpu += Ms(Qpc() - c0);
			// version 3: take the texture from whichever side holds it free, copy, wait for the GPU, release to the client
			int64_t e0 = Qpc();
			HRESULT ar = S_OK; bool got = false, over = false;
			for (;;)
			{
				ar = km->AcquireSync(0, 0);
				if (ar == S_OK) { got = true; break; }
				ar = km->AcquireSync(1, 0);
				if (ar == S_OK) { got = true; over = true; break; }
				if (ar != (HRESULT)WAIT_TIMEOUT && FAILED(ar)) break;
				if (Ms(Qpc() - e0) > 50.0) break;
				Sleep(0);
			}
			if (!got) { v3Timeouts++; continue; }
			ctx->CopyResource(shared, priv);
			ctx->End(doneQ);
			ctx->Flush();
			while (ctx->GetData(doneQ, nullptr, 0, 0) == S_FALSE) YieldProcessor();
			if (over) v3Overwritten++;
			v3b->frameId = F;
			v3b->readyQpc = Qpc();
			v3b->published++;
			km->ReleaseSync(1);
			ctx->Flush();
			// the ring block's latestFrameId doubles as "newest frame" for the v3 consumer's lockstep wait
			VPFRAME_RING(sec)->latestFrameId = F;
			SetEvent(frm);
			sumPublish += Ms(Qpc() - e0);
			draws++;
		}
	}
	log.line("done: busy_retried %lld draws %lld busy %lld notready %lld errors %lld camskipped %lld lastF %lld v3_overwritten %lld v3_timeouts %lld "
		"cpu_ms_mean %.4f publish_ms_mean %.4f skippedBusyTotal %lld",
		busyRetried, draws, busy, notReady, errors, skippedCams, lastF, v3Overwritten, v3Timeouts, draws ? sumCpu / draws : 0.0,
		draws ? sumPublish / draws : 0.0, sec ? (long long)VPFRAME_SERVER(sec)->skippedBusyTotal : 0LL);
	sink.release();
	SafeRelease(doneQ); SafeRelease(km); SafeRelease(shared); SafeRelease(privRtv); SafeRelease(priv);
	if (sharedH) CloseHandle(sharedH);
	return 0;
}
