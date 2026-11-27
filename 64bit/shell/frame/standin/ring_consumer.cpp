// ring_consumer.cpp - stand-in consumer of the frame hand-over (issue 3302), x86: plays the composite DLL inside the EXE.
// Per frame: publish camera F (publish block seqlock + <base>.Cam, vp_frame.h step 1), the "EXE's" own GPU work
// (--passes x --iters of a load shader into its back buffer), wait for F up to the bound (step 3), take the frame
// through RingSource (--mode ring) or the version-3 keyed mutex + copy (--mode v3), draw it over the back buffer
// (straight alpha, no copy for the ring), release, poll, then "present" (Flush; at most 2 frames in flight). Creates
// no window. Test program only.
//
//   ring_consumer.exe --base NAME [--mode ring|v3] [--sync fence|cpu] [--slots N] [--width W --height H] [--frames N]
//                     [--seconds S] [--bound-us B] [--passes N] [--iters N] [--verify] [--hold-every N --hold-frames M]
//                     [--resize-at F:WxH] [--warmup N] [--csv PATH] [--log PATH] [--publish-only]
// Exit: 0 done (the checks are in the last line, "result: ..."), 2 arguments, 3 device, 4 source open refused,
//       6 a verification failed (wrong picture, frame order), 7 v3 setup failed.
#include "standin_util.h"
#include "../ring_source.h"
#include <dxgi1_2.h>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace standin;
using vpframe::ring::SafeRelease;

struct Target
{
	ID3D11Texture2D* tex = nullptr; ID3D11RenderTargetView* rtv = nullptr; uint32_t w = 0, h = 0;
	bool make(ID3D11Device* dev, uint32_t W, uint32_t H)
	{
		SafeRelease(rtv); SafeRelease(tex); w = W; h = H;
		D3D11_TEXTURE2D_DESC td = {}; td.Width = W; td.Height = H; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.BindFlags = D3D11_BIND_RENDER_TARGET;
		return SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex)) && SUCCEEDED(dev->CreateRenderTargetView(tex, nullptr, &rtv));
	}
};

// Reads texel (x, y) of the texture behind srv (one texel copied to a staging texture; Map waits for the GPU, which
// includes the queued fence wait: exactly what the composite's draw would have seen).
static bool ReadTexel(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, uint32_t x, uint32_t y, uint32_t& out)
{
	ID3D11Resource* r = nullptr; srv->GetResource(&r);
	ID3D11Texture2D* t = nullptr; r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t); r->Release();
	D3D11_TEXTURE2D_DESC td; t->GetDesc(&td);
	D3D11_TEXTURE2D_DESC sd = td; sd.Width = sd.Height = 1; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	bool ok = SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &st));
	if (ok)
	{
		D3D11_BOX b = { x, y, 0, x + 1, y + 1, 1 };
		ctx->CopySubresourceRegion(st, 0, 0, 0, 0, t, 0, &b);
		D3D11_MAPPED_SUBRESOURCE m;
		ok = SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m));
		if (ok) { out = *(const uint32_t*)m.pData; ctx->Unmap(st, 0); }
	}
	SafeRelease(st); t->Release();
	return ok;
}

static bool VerifyFrame(ID3D11Device* dev, ID3D11DeviceContext* ctx, const vpframe::SourceFrame& f, uint32_t w, uint32_t h, Log& log, const char* when)
{
	uint32_t c = 0, d = 0;
	if (!ReadTexel(dev, ctx, f.colour, w / 2, h / 2, c) || !ReadTexel(dev, ctx, f.depth, w - 1, h - 1, d))
	{ log.line("verify %s: readback failed (frame %lld)", when, f.frameId); return false; }
	float df; memcpy(&df, &d, 4);
	if (c != FrameBgra(f.frameId) || df != FrameDepth(f.frameId))
	{
		log.line("verify %s: frame %lld slot %d shows colour %08X depth %.9g, expected %08X %.9g", when, f.frameId, f.slot, c, df,
			FrameBgra(f.frameId), FrameDepth(f.frameId));
		return false;
	}
	return true;
}

int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const wchar_t* base = nullptr; const wchar_t* logPath = nullptr; const wchar_t* csvPath = nullptr;
	bool v3 = false, verify = false, publishOnly = false;
	uint32_t sync = VPSYNC_FENCE, slots = VPFRAME_DEFAULT_SLOTS, w = 640, h = 360, iters = 0, boundUs = 50000;
	int passes = 0, holdEvery = 0, holdFrames = 0, warmup = 0;
	int64_t frames = 0, resizeAt = 0, reopenAt = 0, cpuUs = 0; uint32_t rw = 0, rh = 0;
	double seconds = 0;
	for (int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i]; const wchar_t* v = i + 1 < argc ? argv[i + 1] : nullptr;
		if (ArgIs(a, L"--verify")) { verify = true; continue; }
		if (ArgIs(a, L"--publish-only")) { publishOnly = true; continue; }
		if (!v) { fwprintf(stderr, L"ring_consumer: %ls needs a value\n", a); return 2; }
		++i;
		if (ArgIs(a, L"--base")) base = v;
		else if (ArgIs(a, L"--log")) logPath = v;
		else if (ArgIs(a, L"--csv")) csvPath = v;
		else if (ArgIs(a, L"--mode")) { if (!wcscmp(v, L"v3")) v3 = true; else if (wcscmp(v, L"ring")) { fwprintf(stderr, L"ring_consumer: --mode ring|v3\n"); return 2; } }
		else if (ArgIs(a, L"--sync")) { if (!wcscmp(v, L"cpu")) sync = VPSYNC_CPU; else if (wcscmp(v, L"fence")) { fwprintf(stderr, L"ring_consumer: --sync fence|cpu\n"); return 2; } }
		else if (ArgIs(a, L"--slots")) slots = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--width")) w = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--height")) h = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--frames")) frames = _wtoi64(v);
		else if (ArgIs(a, L"--seconds")) seconds = _wtof(v);
		else if (ArgIs(a, L"--bound-us")) boundUs = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--passes")) passes = _wtoi(v);
		else if (ArgIs(a, L"--iters")) iters = (uint32_t)_wtoi(v);
		else if (ArgIs(a, L"--hold-every")) holdEvery = _wtoi(v);
		else if (ArgIs(a, L"--hold-frames")) holdFrames = _wtoi(v);
		else if (ArgIs(a, L"--warmup")) warmup = _wtoi(v);
		else if (ArgIs(a, L"--reopen-at")) reopenAt = _wtoi64(v);
		else if (ArgIs(a, L"--cpu-us")) cpuUs = _wtoi64(v);
		else if (ArgIs(a, L"--resize-at"))
		{
			if (swscanf_s(v, L"%lld:%ux%u", &resizeAt, &rw, &rh) != 3 || resizeAt <= 0 || !rw || !rh) { fwprintf(stderr, L"ring_consumer: --resize-at F:WxH\n"); return 2; }
		}
		else { fwprintf(stderr, L"ring_consumer: unknown argument %ls\n", a); return 2; }
	}
	if (!base || !*base) { fwprintf(stderr, L"ring_consumer: --base is required\n"); return 2; }
	if (!frames && seconds <= 0) { fwprintf(stderr, L"ring_consumer: --frames or --seconds is required\n"); return 2; }
	if (!w || !h || w > 16384 || h > 16384) { fwprintf(stderr, L"ring_consumer: size out of range\n"); return 2; }
	Log log; log.open(logPath);
	FILE* csv = nullptr;
	if (csvPath) { _wfopen_s(&csv, csvPath, L"w"); if (csv) fprintf(csv, "frame,take,got,age,wait_us,ho_cpu_us,frame_us_prev,throttle_us,exe_us\n"); }

	ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; uint64_t luid = 0;
	if (!CreateDevice(dev, ctx, luid)) { log.line("device creation failed"); return 3; }
	Gpu gpu;
	if (!gpu.init(dev)) { log.line("shaders failed"); return 3; }
	log.line("ring_consumer pid %u base %ls mode %s sync %s slots %u %ux%u bound %u us passes %d iters %u", GetCurrentProcessId(), base,
		v3 ? "v3" : "ring", sync == VPSYNC_FENCE ? "fence" : "cpu", slots, w, h, boundUs, passes, iters);

	vpframe::RingSource src(slots, sync);
	src.setLog(&Log::Cb, &log);
	HANDLE secMap = nullptr; void* sec = nullptr; HANDLE camEv = nullptr;
	// v3 arm
	HANDLE frmEv = nullptr; V3Block* v3b = nullptr; ID3D11Device1* dev1 = nullptr;
	ID3D11Texture2D* v3Shared = nullptr; IDXGIKeyedMutex* km = nullptr; ID3D11Texture2D* local = nullptr; ID3D11ShaderResourceView* localSrv = nullptr;
	uint32_t v3Gen = 0;
	if (!v3)
	{
		if (!src.open(dev, ctx, base)) { log.line("source open refused"); return 4; }
		sec = const_cast<void*>(src.section());
		src.request(w, h);
	}
	else
	{
		sec = vpframe::ring::MapSection(base, secMap);
		frmEv = vpframe::ring::OpenEvent_(base, VPFRAME_FRM_EVENT_SUFFIX);
		wchar_t n[256]; vpframe::ring::Name(n, 256, base, L".V3");
		HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, n);
		v3b = m ? (V3Block*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0) : nullptr;
		if (!sec || !frmEv || !v3b || FAILED(dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev1))) { log.line("v3 setup failed"); return 7; }
	}
	camEv = vpframe::ring::OpenEvent_(base, VPFRAME_CAM_EVENT_SUFFIX);
	VPFramePublish* pub = VPFRAME_PUBLISH(sec);
	if (!v3)
	{
		// As the composite would: the request goes out at start-up, cameras afterwards. Wait (at most 2 s) until a live
		// server answered it, so frame 1 is not lost to the ring's creation; without a server, go on at once.
		int64_t r0 = Qpc();
		while (Ms(Qpc() - r0) < 2000.0 && VPFRAME_SERVER(sec)->magic == VPFRAME_MAGIC &&
			!(VPFRAME_RING(sec)->slotCount && VPFRAME_RING(sec)->ackReqSeq == pub->reqSeq)) Sleep(1);
	}
	Target bb; if (!bb.make(dev, w, h)) { log.line("back buffer failed"); return 3; }

	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	ID3D11Query* frameQ[3] = {};
	for (auto& q : frameQ) dev->CreateQuery(&qd, &q);
	// GPU time of the hand-over part of a frame (v3: copy + draw; ring: draw; the fence wait is queued before the first stamp)
	const int kTs = 6;
	ID3D11Query* tsDisjoint[kTs] = {}; ID3D11Query* tsA[kTs] = {}; ID3D11Query* tsB[kTs] = {}; int64_t tsFrame[kTs] = {};
	D3D11_QUERY_DESC qdj = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }, qts = { D3D11_QUERY_TIMESTAMP, 0 };
	for (int i = 0; i < kTs; ++i) { dev->CreateQuery(&qdj, &tsDisjoint[i]); dev->CreateQuery(&qts, &tsA[i]); dev->CreateQuery(&qts, &tsB[i]); }
	std::vector<double> gpuUs; std::vector<int64_t> gpuFrame;

	const int64_t freq = QpcFreq(), t0 = Qpc();
	int64_t F = 0, lastTaken = 0, exact = 0, older = 0, none = 0, verifyFail = 0, orderFail = 0, dup = 0, waitsOk = 0, waitsMissed = 0;
	int64_t maxFrameUs = 0, prevStart = 0, lateWaitEnds = 0, maxLate = 0;
	double sumWait = 0, sumHo = 0, sumFrame = 0; int64_t nMeasured = 0;
	struct Held { vpframe::SourceFrame f; int64_t until; };
	std::vector<Held> held;
	int64_t heldVerified = 0;
	vpframe::SourceFrame f3;   // v3: the local copy's frame
	for (;;)
	{
		int64_t start = Qpc();
		if (frames && F >= frames) break;
		if (seconds > 0 && (double)(start - t0) / freq >= seconds) break;
		if (resizeAt && F + 1 == resizeAt)
		{
			log.line("resize to %ux%u at frame %lld", rw, rh, resizeAt);
			w = rw; h = rh; bb.make(dev, w, h);
			if (!v3) src.request(w, h);
		}
		if (reopenAt && F + 1 == reopenAt && !v3)
		{
			// the composite's close() + open() (ResizeBuffers, mode off and on): claims reset, a fresh generation
			for (auto& hh : held) src.release(ctx, hh.f);
			held.clear();
			src.close();
			bool ok = src.open(dev, ctx, base);
			src.request(w, h);
			log.line("reopen at frame %lld: open %d", reopenAt, (int)ok);
			sec = const_cast<void*>(src.section()); pub = VPFRAME_PUBLISH(sec);
		}
		// 1. publish F (the composite's step 1, reduced: no camera block)
		++F;
		int64_t pq = Qpc();
		pub->seq = pub->seq | 1u; MemoryBarrier();
		pub->source = VPPUB_UPDATEVIEWS; pub->frameId = F; pub->publishQpc = pq;
		pub->deadlineQpc = pq + (int64_t)boundUs * freq / 1000000; pub->boundUs = boundUs; pub->lockstep = boundUs ? 1 : 0;
		MemoryBarrier(); pub->seq = pub->seq + 1;
		SetEvent(camEv);
		if (publishOnly) { Sleep(5); continue; }
		// 2. the EXE's own work
		float bg[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
		ctx->ClearRenderTargetView(bb.rtv, bg);
		gpu.load(ctx, bb.rtv, w, h, bg, iters, passes);
		ctx->Flush();
		// the EXE's CPU work between UpdateViews and the insertion (lockstep-probe.md: 1.0-1.8 ms sparse)
		if (cpuUs) { int64_t c0 = Qpc(); while ((Qpc() - c0) * 1000000 / freq < cpuUs) YieldProcessor(); }
		// 3. wait for F up to the bound, then take it
		int64_t deadline = pq + (int64_t)boundUs * freq / 1000000;
		int64_t w0 = Qpc();
		bool ready;
		if (!v3) ready = src.waitFor(F, deadline);
		else
		{
			for (;;)
			{
				if (VPFRAME_RING(sec)->latestFrameId >= F) { ready = true; break; }
				int64_t now = Qpc();
				if (now >= deadline) { ready = false; break; }
				int64_t leftMs = (deadline - now) * 1000 / freq;
				if (leftMs > 1) WaitForSingleObject(frmEv, (DWORD)(leftMs - 1)); else YieldProcessor();
			}
		}
		int64_t w1 = Qpc();
		if (ready) waitsOk++; else waitsMissed++;
		{
			// a wait that ended more than 0.5 ms after its deadline (or after its start, when it began past the deadline)
			int64_t late = w1 - (deadline > w0 ? deadline : w0);
			if (!ready && late > freq / 2000) lateWaitEnds++;
			if (!ready && late > maxLate) maxLate = late;
		}
		int ts = (int)(F % kTs);
		bool tsFree = tsFrame[ts] == 0, tsBegun = false;
		if (!tsFree)
		{
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 a = 0, b = 0;
			if (ctx->GetData(tsDisjoint[ts], &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
				ctx->GetData(tsA[ts], &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
				ctx->GetData(tsB[ts], &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
			{
				if (!dj.Disjoint && tsFrame[ts] > warmup) { gpuUs.push_back((double)(b - a) * 1e6 / (double)dj.Frequency); gpuFrame.push_back(tsFrame[ts]); }
				tsFrame[ts] = 0;
				tsFree = true;
			}
		}
		vpframe::SourceFrame sf; vpframe::Take take = vpframe::Take::None;
		int64_t h0 = Qpc();
		if (!v3)
		{
			take = src.acquire(ctx, F, sf);
		}
		else if (ready)
		{
			if (!v3Shared || v3b->generation != v3Gen)
			{
				SafeRelease(localSrv); SafeRelease(local); SafeRelease(km); SafeRelease(v3Shared);
				if (SUCCEEDED(dev1->OpenSharedResourceByName(v3b->texName, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					__uuidof(ID3D11Texture2D), (void**)&v3Shared)) && SUCCEEDED(v3Shared->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km)))
				{
					D3D11_TEXTURE2D_DESC td; v3Shared->GetDesc(&td); td.MiscFlags = 0; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					dev->CreateTexture2D(&td, nullptr, &local); dev->CreateShaderResourceView(local, nullptr, &localSrv);
					v3Gen = v3b->generation;
				}
				else { SafeRelease(v3Shared); log.line("v3: texture '%ls' not opened", v3b->texName); }
			}
			if (km && km->AcquireSync(1, 0) == S_OK)
			{
				int64_t got = v3b->frameId;
				if (tsFree) { ctx->Begin(tsDisjoint[ts]); ctx->End(tsA[ts]); tsBegun = true; }
				ctx->CopyResource(local, v3Shared);
				km->ReleaseSync(0);
				f3.frameId = got; f3.colour = localSrv;
				take = got == F ? vpframe::Take::Exact : vpframe::Take::Older;
			}
			if (take != vpframe::Take::None) sf = f3;
		}
		else if (f3.frameId) { sf = f3; take = vpframe::Take::Older; }
		int64_t h1 = Qpc();
		if (take != vpframe::Take::None)
		{
			if (!v3 && tsFree) { ctx->Begin(tsDisjoint[ts]); ctx->End(tsA[ts]); tsBegun = true; }
			gpu.blit(ctx, bb.rtv, w, h, sf.colour);
			if (tsBegun) { ctx->End(tsB[ts]); ctx->End(tsDisjoint[ts]); tsFrame[ts] = F; }
			if (sf.frameId < lastTaken) { orderFail++; log.line("order: frame %lld taken after %lld", sf.frameId, lastTaken); }
			if (take == vpframe::Take::Exact && sf.frameId == lastTaken) dup++;
			lastTaken = sf.frameId;
			if (take == vpframe::Take::Exact) exact++; else older++;
			if (verify && !v3 && !VerifyFrame(dev, ctx, sf, w, h, log, "take")) verifyFail++;
		}
		else none++;
		int64_t r0 = Qpc();
		if (!v3 && take != vpframe::Take::None)
		{
			if (holdEvery && holdFrames && F % holdEvery == 0 && (int)held.size() < (int)slots) held.push_back({ sf, F + holdFrames });
			else src.release(ctx, sf);
		}
		for (size_t i = 0; i < held.size();)
		{
			if (F >= held[i].until)
			{
				// the server must not have drawn into a slot we held: its picture is still the held frame's
				if (verify && !VerifyFrame(dev, ctx, held[i].f, w, h, log, "held")) verifyFail++; else heldVerified++;
				src.release(ctx, held[i].f);
				held.erase(held.begin() + i);
			}
			else ++i;
		}
		if (!v3) src.poll(Qpc());
		int64_t r1 = Qpc();
		// 4. "present": flush, at most 2 frames in flight
		ctx->End(frameQ[F % 3]);
		ctx->Flush();
		ID3D11Query* old = frameQ[(F + 1) % 3];
		int64_t th0 = Qpc();
		if (F >= 3) while (ctx->GetData(old, nullptr, 0, 0) == S_FALSE) YieldProcessor();
		double throttleUs = (double)(Qpc() - th0) * 1e6 / freq;
		int64_t frameUs = prevStart ? (start - prevStart) * 1000000 / freq : 0;
		prevStart = start;
		double waitUs = (double)(w1 - w0) * 1e6 / freq, hoUs = (double)((h1 - h0) + (r1 - r0)) * 1e6 / freq;
		if (F > warmup && frameUs)
		{
			sumWait += waitUs; sumHo += hoUs; sumFrame += (double)frameUs; nMeasured++;
			if (frameUs > maxFrameUs) maxFrameUs = frameUs;
		}
		if (csv) fprintf(csv, "%lld,%d,%lld,%lld,%.1f,%.1f,%lld,%.1f,%.1f\n", F, (int)take, sf.frameId, sf.frameId ? F - sf.frameId : -1, waitUs, hoUs,
			frameUs, throttleUs, (double)(w0 - start) * 1e6 / freq);
	}
	for (auto& hh : held) src.release(ctx, hh.f);
	held.clear();
	// drain: let the last releases complete so the section shows them
	ctx->Flush();
	const float bg0[4] = { 0, 0, 0, 1 };
	int64_t d0 = Qpc();
	int drainPolls = 0;
	while (src.pending() && Ms(Qpc() - d0) < 500.0)
	{
		ctx->ClearRenderTargetView(bb.rtv, bg0);   // a little work and a flush, as the EXE's next frames would bring
		ctx->Flush();
		src.poll(Qpc()); ++drainPolls; Sleep(1);
	}
	log.line("drain: pending %d after %d polls, %.1f ms", src.pending(), drainPolls, Ms(Qpc() - d0));
	if (csv)
	{
		fprintf(csv, "# gpu\n");
		for (size_t i = 0; i < gpuUs.size(); ++i) fprintf(csv, "#gpu,%lld,%.2f\n", gpuFrame[i], gpuUs[i]);
		fclose(csv);
	}
	double gpuMean = 0; for (double g : gpuUs) gpuMean += g; if (!gpuUs.empty()) gpuMean /= gpuUs.size();
	const vpframe::RingSourceStats& st = src.stats();
	log.line("result: frames %lld exact %lld older %lld none %lld order_fail %lld dup %lld verify_fail %lld held_verified %lld "
		"waits_ok %lld waits_missed %lld late_wait_ends %lld max_late_us %lld max_frame_us %lld mean_frame_us %.1f mean_wait_us %.1f mean_handover_cpu_us %.2f "
		"mean_gpu_us %.2f gpu_n %zu claim_retries %lld released %lld generations %lld server_changes %lld open_failures %lld refused %lld "
		"fence_waits %lld pending_max %lld fence_done_at_acquire %lld fence_completed_max %lld fence_poisoned %lld sync %u gen %u",
		F, exact, older, none, orderFail, dup, verifyFail, heldVerified, waitsOk, waitsMissed, lateWaitEnds, maxLate * 1000000 / freq, maxFrameUs,
		nMeasured ? sumFrame / nMeasured : 0.0, nMeasured ? sumWait / nMeasured : 0.0, nMeasured ? sumHo / nMeasured : 0.0, gpuMean, gpuUs.size(),
		st.claimRetries, st.released, st.generations, st.serverChanges, st.openFailures, st.refusedSection, st.fenceWaits, st.pendingMax, st.fenceDoneAtAcquire, st.fenceCompletedMax, st.fencePoisoned,
		src.syncInForce(), src.generation());
	src.close();
	SafeRelease(localSrv); SafeRelease(local); SafeRelease(km); SafeRelease(v3Shared); SafeRelease(dev1);
	return (verifyFail || orderFail) ? 6 : 0;
}
