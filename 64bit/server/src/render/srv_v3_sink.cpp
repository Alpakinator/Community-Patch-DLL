// srv_v3_sink.cpp - the version-3 frame sink (issue 3301). Origin and protocol: srv_v3_sink.h (ported from
// shell/server/shell_server.cpp at commit b4f7748; that file is unchanged).
#include "srv_v3_sink.h"
#include "ring_common.h"
#include <dxgi1_2.h>
#include <vector>

namespace srvrender {

using vpframe::SinkStatus;

namespace {
const double kAcquireLimitMs = 50.0;    // shell_server: the client holds the texture for too long -> skip this frame
const double kGpuWaitLimitMs = 2000.0;  // a GPU that does not finish a frame in 2 s is an error (as the ring sink)

bool OtherLiveServer(uint32_t pid)
{
	if(!pid || pid == GetCurrentProcessId())
		return false;
	HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
	const bool alive = hp && WaitForSingleObject(hp, 0) == WAIT_TIMEOUT;
	if(hp)
		CloseHandle(hp);
	return alive;
}
}  // namespace

bool V3Sink::deviceLost() const
{
	const HRESULT r = m_dev ? m_dev->GetDeviceRemovedReason() : S_OK;
	return r != S_OK;
}

bool V3Sink::open(ID3D11Device* dev, ID3D11DeviceContext* ctx, const vpframe::SinkConfig& cfg)
{
	if(m_hdr)
		release();
	if(!dev || !ctx || !cfg.baseName || !*cfg.baseName || wcslen(cfg.baseName) > 200)
	{
		RNote("LOUD: v3 sink: open refused: no device, context or section name (or the name is too long)");
		return false;
	}
	m_dev = dev;
	m_ctx = ctx;
	m_base = cfg.baseName;
	m_luid = cfg.adapterLuid;
	D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
	if(FAILED(dev->CreateQuery(&qd, &m_sync)))
	{
		RNote("LOUD: v3 sink: CreateQuery failed");
		release();
		return false;
	}
	// The version-3 section: create or open (the composite may already hold it).
	m_section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, m_base.c_str());
	const DWORD le = GetLastError();
	if(!m_section)
	{
		RNote("LOUD: v3 sink: CreateFileMapping('%ls') failed %lu", m_base.c_str(), le);
		release();
		return false;
	}
	const bool existed = le == ERROR_ALREADY_EXISTS;
	m_hdr = (VPShellHeader*)MapViewOfFile(m_section, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if(!m_hdr)
	{
		RNote("LOUD: v3 sink: MapViewOfFile('%ls') failed %lu", m_base.c_str(), GetLastError());
		release();
		return false;
	}
	if(m_hdr->magic == VPSHELL_MAGIC && OtherLiveServer(m_hdr->serverPid))
	{
		RNote("LOUD: v3 sink: section '%ls' is served by live pid %u (another server): refused", m_base.c_str(), m_hdr->serverPid);
		UnmapViewOfFile(m_hdr);
		m_hdr = nullptr;
		release();
		return false;
	}
	m_frame = vpframe::ring::MapSection(m_base.c_str(), m_frameMap);
	if(!m_frame)
	{
		RNote("LOUD: v3 sink: section '%ls%ls' not mapped (%lu)", m_base.c_str(), VPFRAME_SECTION_SUFFIX, GetLastError());
		release();
		return false;
	}
	VPFrameServer* srv = VPFRAME_SERVER(m_frame);
	if(srv->magic == VPFRAME_MAGIC && srv->state != VPFS_STOPPED && OtherLiveServer(srv->serverPid))
	{
		RNote("LOUD: v3 sink: section '%ls%ls' is served by live pid %u: refused", m_base.c_str(), VPFRAME_SECTION_SUFFIX,
		      srv->serverPid);
		release();
		return false;
	}
	m_cam = vpframe::ring::OpenEvent_(m_base.c_str(), VPFRAME_CAM_EVENT_SUFFIX);
	m_frm = vpframe::ring::OpenEvent_(m_base.c_str(), VPFRAME_FRM_EVENT_SUFFIX);
	if(!m_cam || !m_frm)
	{
		RNote("LOUD: v3 sink: events not created (%lu)", GetLastError());
		release();
		return false;
	}
	RLog("v3 sink: section '%ls' %s", m_base.c_str(), existed ? "opened (already existed: a client or a previous server holds it)" : "created");
	// shell_server.cpp wmain: header fields, then the client's last requested size, then the texture, magic last
	if(m_hdr->magic != VPSHELL_MAGIC)
	{
		m_hdr->texSeq = 0;
		m_hdr->reqSeq = 0;
		m_hdr->ackSeq = 0;
	}
	m_hdr->qpcFrequency = QpcFreq();
	m_hdr->frameCounter = 0;
	m_hdr->frameQpc = 0;
	m_hdr->framesOverwritten = 0;
	m_hdr->version = VPSHELL_VERSION;
	m_frameCounter = 0;
	uint32_t w = m_opt.width, h = m_opt.height;
	const uint32_t pendingSeq = m_hdr->reqSeq;
	const uint32_t rw = m_hdr->reqWidth, rh = m_hdr->reqHeight;
	if(rw >= 16 && rh >= 16 && rw <= 16384 && rh <= 16384)
	{
		w = rw;
		h = rh;
		RLog("v3 sink: starting at the client's last requested size %ux%u (seq %u)", w, h, pendingSeq);
	}
	m_hdr->ackSeq = pendingSeq;
	// A sink re-opened in the same process (device rebuilt, adapter followed) continues the generations: the composite
	// re-opens the texture only when the generation changes, and the names <base>_<pid>_<generation> must not repeat.
	if(m_hdr->serverPid == GetCurrentProcessId())
	{
		if(m_hdr->generation > m_generation)
			m_generation = m_hdr->generation;
		if(VPSHELL_DEPTH(m_hdr)->generation > m_depthGen)
			m_depthGen = VPSHELL_DEPTH(m_hdr)->generation;
	}
	// the server block of <base>.Frame (vp_frame.h); magic last
	srv->magic = 0;
	MemoryBarrier();
	srv->version = VPFRAME_VERSION;
	srv->serverPid = GetCurrentProcessId();
	srv->state = VPFS_STARTING;
	srv->failReason = VPFSF_NONE;
	srv->qpcFrequency = QpcFreq();
	srv->heartbeatQpc = Qpc();
	srv->lastDrawnFrameId = 0;
	srv->drawsTotal = srv->camerasSkipped = srv->skippedBusyTotal = srv->drawErrorsTotal = 0;
	srv->heldFrameId = 0;   // ticket 0691: no hold (the host reports its own; vp_frame.h writer order: the id first)
	srv->holdReasons = 0;
	srv->heldFramesTotal = 0;
	VPFrameRing* rg = VPFRAME_RING(m_frame);
	rg->latestFrameId = 0;
	rg->latestSlot = -1;
	MemoryBarrier();
	srv->magic = VPFRAME_MAGIC;
	if(!createTextures(w, h))
	{
		release();
		return false;
	}
	MemoryBarrier();
	m_hdr->magic = VPSHELL_MAGIC;
	return true;
}

void V3Sink::setState(uint32_t state, uint32_t failReason)
{
	if(!m_frame)
		return;
	VPFrameServer* srv = VPFRAME_SERVER(m_frame);
	srv->failReason = failReason;
	MemoryBarrier();
	srv->state = state;
}

void V3Sink::releaseTextures()
{
	SafeRelease(m_privRtv);
	SafeRelease(m_privTex);
	SafeRelease(m_km);
	SafeRelease(m_sharedTex);
	if(m_sharedHandle)
	{
		CloseHandle(m_sharedHandle);
		m_sharedHandle = nullptr;
	}
}

void V3Sink::publishTextureBlock()
{
	VPShellHeader* h = m_hdr;
	InterlockedIncrement((volatile LONG*)&h->texSeq);  // odd
	MemoryBarrier();
	h->serverPid = GetCurrentProcessId();
	h->generation = m_generation;
	h->width = m_w;
	h->height = m_h;
	h->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	h->adapterLuid = m_luid;
	wcsncpy_s(h->textureName, m_texName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&h->texSeq);  // even
}

bool V3Sink::createTextures(uint32_t w, uint32_t h)
{
	releaseTextures();
	m_w = w;
	m_h = h;
	m_generation++;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w;
	td.Height = h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	HRESULT hr = m_dev->CreateTexture2D(&td, nullptr, &m_privTex);
	if(SUCCEEDED(hr))
		hr = m_dev->CreateRenderTargetView(m_privTex, nullptr, &m_privRtv);
	if(FAILED(hr))
	{
		RNote("LOUD: v3 sink: private target %ux%u failed 0x%08lX", w, h, hr);
		return false;
	}
	td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
	hr = m_dev->CreateTexture2D(&td, nullptr, &m_sharedTex);
	if(SUCCEEDED(hr))
		hr = m_sharedTex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&m_km);
	IDXGIResource1* res1 = nullptr;
	if(SUCCEEDED(hr))
		hr = m_sharedTex->QueryInterface(__uuidof(IDXGIResource1), (void**)&res1);
	_snwprintf_s(m_texName, _countof(m_texName), _TRUNCATE, L"%ls_%u_%u", m_opt.textureBase.c_str(), GetCurrentProcessId(), m_generation);
	if(SUCCEEDED(hr))
		hr = res1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, m_texName, &m_sharedHandle);
	SafeRelease(res1);
	if(FAILED(hr))
	{
		RNote("LOUD: v3 sink: shared texture '%ls' %ux%u failed 0x%08lX", m_texName, w, h, hr);
		m_sharedHandle = nullptr;
		return false;
	}
	RNote("v3 sink: texture generation %u: %ux%u BGRA8 shared as '%ls'", m_generation, w, h, m_texName);
	publishTextureBlock();
	return true;
}

void V3Sink::releaseDepth()
{
	SafeRelease(m_dsv);
	SafeRelease(m_depthTex);
	SafeRelease(m_depthShared);
	if(m_depthHandle)
	{
		CloseHandle(m_depthHandle);
		m_depthHandle = nullptr;
	}
}

void V3Sink::publishDepthBlock(int64_t frameCounter, uint32_t state, const vpframe::FrameInfo* f)
{
	VPShellDepth* d = VPSHELL_DEPTH(m_hdr);
	InterlockedIncrement((volatile LONG*)&d->seq);  // odd
	MemoryBarrier();
	d->state = state;
	d->generation = m_depthGen;
	d->width = m_depthW;
	d->height = m_depthH;
	d->dxgiFormat = DXGI_FORMAT_R32_FLOAT;
	d->encoding = VPDEPTH_NDC_Z;
	d->frameCounter = frameCounter;
	wcsncpy_s(d->textureName, m_depthName, _TRUNCATE);
	if(f)
	{
		// issue 9039: the camera this frame was drawn with (camFrameStamp, the projection terms)
		d->camFrameStamp = f->frameId;
		d->proj22 = f->proj22;
		d->proj32 = f->proj32;
		d->nearZ = f->nearZ;
		d->farZ = f->farZ;
	}
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&d->seq);  // even
}

bool V3Sink::ensureDepth()
{
	if(m_depthTex && m_depthW == m_w && m_depthH == m_h)
		return true;
	releaseDepth();
	m_depthW = m_w;
	m_depthH = m_h;
	m_depthGen++;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = m_w;
	td.Height = m_h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.Format = DXGI_FORMAT_R32_TYPELESS;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	HRESULT hr = m_dev->CreateTexture2D(&td, nullptr, &m_depthTex);
	D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
	dd.Format = DXGI_FORMAT_D32_FLOAT;
	dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	if(SUCCEEDED(hr))
		hr = m_dev->CreateDepthStencilView(m_depthTex, &dd, &m_dsv);
	if(FAILED(hr))
	{
		RNote("LOUD: v3 sink: depth buffer %ux%u failed 0x%08lX", m_w, m_h, hr);
		return false;
	}
	if(!m_opt.depth)
		return true;   // the private depth buffer only: nothing published
	td.Format = DXGI_FORMAT_R32_FLOAT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	hr = m_dev->CreateTexture2D(&td, nullptr, &m_depthShared);
	IDXGIResource1* res1 = nullptr;
	if(SUCCEEDED(hr))
		hr = m_depthShared->QueryInterface(__uuidof(IDXGIResource1), (void**)&res1);
	_snwprintf_s(m_depthName, _countof(m_depthName), _TRUNCATE, L"%lsDepth_%u_%u", m_opt.textureBase.c_str(), GetCurrentProcessId(), m_depthGen);
	if(SUCCEEDED(hr))
		hr = res1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, m_depthName, &m_depthHandle);
	SafeRelease(res1);
	if(FAILED(hr))
	{
		RNote("LOUD: v3 sink: shared depth '%ls' failed 0x%08lX", m_depthName, hr);
		m_depthHandle = nullptr;
		return false;
	}
	RNote("v3 sink: depth generation %u: %ux%u R32_FLOAT (NDC z) shared as '%ls'", m_depthGen, m_depthW, m_depthH, m_depthName);
	publishDepthBlock(0, 1, nullptr);
	return true;
}

void V3Sink::tick(int64_t nowQpc)
{
	if(!m_hdr)
		return;
	const int64_t now = nowQpc ? nowQpc : Qpc();
	InterlockedExchange64(&m_hdr->heartbeatQpc, now);
	if(m_frame)
		VPFRAME_SERVER(m_frame)->heartbeatQpc = now;
	// resize request from the client (shell_server.cpp wmain)
	const uint32_t rs = m_hdr->reqSeq;
	if(rs != m_hdr->ackSeq)
	{
		MemoryBarrier();
		const uint32_t rw = m_hdr->reqWidth, rh = m_hdr->reqHeight;
		if(rw >= 16 && rh >= 16 && rw <= 16384 && rh <= 16384 && (rw != m_w || rh != m_h))
		{
			RNote("v3 sink: client requested %ux%u (seq %u)", rw, rh, rs);
			if(!createTextures(rw, rh))
				RNote("LOUD: v3 sink: resize to %ux%u failed: no texture until the next request", rw, rh);
			m_resized = true;   // the new texture is empty: the host draws again with its last camera
		}
		else if(!(rw >= 16 && rh >= 16 && rw <= 16384 && rh <= 16384))
			RNote("v3 sink: client request %ux%u (seq %u) outside 16..16384: ignored", rw, rh, rs);
		m_hdr->ackSeq = rs;
	}
	if(m_hdr->clientPid != m_lastClientPid)
	{
		m_lastClientPid = m_hdr->clientPid;
		const uint64_t cl = m_hdr->clientLuid;
		RNote("v3 sink: client pid %u, client adapter LUID %016llX%s", m_lastClientPid, (unsigned long long)cl,
		      (cl && cl != m_luid) ? "  ** MISMATCH: the shared texture will not open on that adapter **" : "");
	}
}

SinkStatus V3Sink::begin(const vpframe::FrameInfo& f, vpframe::FrameTargets& out)
{
	(void)f;
	out = vpframe::FrameTargets();
	if(!m_hdr || !m_sharedTex || !m_privRtv)
		return SinkStatus::NotReady;
	if(!ensureDepth())
		return deviceLost() ? SinkStatus::DeviceLost : SinkStatus::Error;
	out.colour = m_privRtv;
	out.depth = m_dsv;
	out.width = m_w;
	out.height = m_h;
	return SinkStatus::Ok;
}

SinkStatus V3Sink::end(const vpframe::FrameInfo& f, const vpframe::DrawReport& r)
{
	if(!m_hdr || !m_km)
		return SinkStatus::Error;
	VPFrameServer* srv = VPFRAME_SERVER(m_frame);
	// publish: take the texture from whichever side holds it free (shell_server.cpp wmain)
	const int64_t a0 = Qpc();
	HRESULT ar = S_OK;
	bool got = false, wasUnconsumed = false;
	for(;;)
	{
		ar = m_km->AcquireSync(0, 0);
		if(ar == S_OK)
		{
			got = true;
			break;
		}
		ar = m_km->AcquireSync(1, 0);
		if(ar == S_OK)
		{
			got = true;
			wasUnconsumed = true;
			break;
		}
		if(ar != (HRESULT)WAIT_TIMEOUT && FAILED(ar))
		{
			RLog("v3 sink: AcquireSync error 0x%08lX", ar);
			break;
		}
		if(QpcToMs(Qpc() - a0) > kAcquireLimitMs)
			break;   // the client holds it for too long: skip this frame
		Sleep(0);
	}
	const double aMs = QpcToMs(Qpc() - a0);
	if(aMs > m_acqMax)
		m_acqMax = aMs;
	if(!got)
	{
		m_skipped++;
		srv->skippedBusyTotal++;
		if(deviceLost())
			return SinkStatus::DeviceLost;
		return SinkStatus::Busy;
	}
	m_ctx->CopyResource(m_sharedTex, m_privTex);
	if(m_opt.depth && m_depthShared)
		m_ctx->CopyResource(m_depthShared, m_depthTex);
	if(m_opt.gpuSync)
	{
		// Wait until the GPU finished this frame before handing it over, so the client's GPU queue (the game's) never
		// stalls on our work after its AcquireSync.
		m_ctx->End(m_sync);
		m_ctx->Flush();
		const int64_t g0 = Qpc();
		for(unsigned spins = 0;; ++spins)
		{
			const HRESULT hr = m_ctx->GetData(m_sync, nullptr, 0, 0);
			if(hr == S_OK)
				break;
			if(FAILED(hr) || QpcToMs(Qpc() - g0) > kGpuWaitLimitMs)
			{
				RNote("LOUD: v3 sink: GPU wait for frame %lld failed (0x%08lX)", (long long)f.frameId, hr);
				m_km->ReleaseSync(0);   // nothing new: the client keeps the previous frame
				srv->drawErrorsTotal++;
				return deviceLost() ? SinkStatus::DeviceLost : SinkStatus::Error;
			}
			if(spins > 64)
				SwitchToThread();
			else
				YieldProcessor();
		}
	}
	m_frameCounter++;
	m_hdr->frameCounter = m_frameCounter;
	m_hdr->frameQpc = Qpc();
	m_lastInfo = f;
	if(m_opt.depth && m_depthShared)
		publishDepthBlock(m_frameCounter, 1, &f);
	if(wasUnconsumed)
	{
		m_overwritten++;
		m_hdr->framesOverwritten = m_overwritten;
	}
	m_km->ReleaseSync(1);
	m_ctx->Flush();
	m_published++;
	srv->lastDrawnFrameId = f.frameId;
	srv->drawsTotal++;
	if(r.result == VPDRAW_ERROR)
		srv->drawErrorsTotal++;
	VPFRAME_RING(m_frame)->latestFrameId = f.frameId;   // the v3 consumer's lockstep wait (as ring_producer --mode v3)
	SetEvent(m_frm);
	if(deviceLost())
		return SinkStatus::DeviceLost;
	return SinkStatus::Ok;
}

void V3Sink::release()
{
	if(m_hdr)
	{
		if(m_hdr->serverPid == GetCurrentProcessId())
		{
			if(m_opt.depth && m_depthGen)
				publishDepthBlock(m_frameCounter, 0, nullptr);
			m_hdr->magic = 0;   // tell clients there is no live server behind this section
		}
		UnmapViewOfFile(m_hdr);
		m_hdr = nullptr;
	}
	if(m_frame)
	{
		VPFrameServer* srv = VPFRAME_SERVER(m_frame);
		if(srv->serverPid == GetCurrentProcessId())
		{
			if(srv->state != VPFS_FAILED)
				srv->state = VPFS_STOPPED;
			MemoryBarrier();
			srv->magic = 0;
		}
		UnmapViewOfFile(m_frame);
		m_frame = nullptr;
	}
	if(m_frameMap)
	{
		CloseHandle(m_frameMap);
		m_frameMap = nullptr;
	}
	if(m_section)
	{
		CloseHandle(m_section);
		m_section = nullptr;
	}
	if(m_cam)
	{
		CloseHandle(m_cam);
		m_cam = nullptr;
	}
	if(m_frm)
	{
		CloseHandle(m_frm);
		m_frm = nullptr;
	}
	releaseTextures();
	releaseDepth();
	SafeRelease(m_sync);
	m_dev = nullptr;
	m_ctx = nullptr;
}

bool V3Sink::dump(const wchar_t* path)
{
	if(!m_privTex || !path || !*path)
		return false;
	// colour: 32-bit top-down BMP (shell_server.cpp DumpTarget)
	D3D11_TEXTURE2D_DESC td;
	m_privTex->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	td.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	if(FAILED(m_dev->CreateTexture2D(&td, nullptr, &st)))
		return false;
	m_ctx->CopyResource(st, m_privTex);
	bool ok = false;
	D3D11_MAPPED_SUBRESOURCE m;
	if(SUCCEEDED(m_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
	{
		FILE* f = nullptr;
		if(!_wfopen_s(&f, path, L"wb") && f)
		{
			BITMAPFILEHEADER fh = {};
			BITMAPINFOHEADER ih = {};
			ih.biSize = sizeof(ih);
			ih.biWidth = (LONG)td.Width;
			ih.biHeight = -(LONG)td.Height;
			ih.biPlanes = 1;
			ih.biBitCount = 32;
			ih.biCompression = BI_RGB;
			ih.biSizeImage = td.Width * td.Height * 4;
			fh.bfType = 0x4D42;
			fh.bfOffBits = sizeof(fh) + sizeof(ih);
			fh.bfSize = fh.bfOffBits + ih.biSizeImage;
			ok = fwrite(&fh, sizeof(fh), 1, f) == 1 && fwrite(&ih, sizeof(ih), 1, f) == 1;
			for(UINT y = 0; ok && y < td.Height; ++y)
				ok = fwrite((const char*)m.pData + (size_t)y * m.RowPitch, 4, td.Width, f) == td.Width;
			ok = (fclose(f) == 0) && ok;
		}
		m_ctx->Unmap(st, 0);
	}
	st->Release();
	if(!ok || !m_opt.depth || !m_depthShared)
		return ok;
	// depth: raw float32 rows of the shared copy (shell_server.cpp DumpDepth)
	m_depthShared->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	td.MiscFlags = 0;
	st = nullptr;
	if(FAILED(m_dev->CreateTexture2D(&td, nullptr, &st)))
		return false;
	m_ctx->CopyResource(st, m_depthShared);
	ok = false;
	if(SUCCEEDED(m_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
	{
		std::wstring dp = std::wstring(path) + L".depth.f32";
		FILE* f = nullptr;
		if(!_wfopen_s(&f, dp.c_str(), L"wb") && f)
		{
			ok = true;
			for(UINT y = 0; y < td.Height; ++y)
				ok = ok && fwrite((const char*)m.pData + (size_t)y * m.RowPitch, 4, td.Width, f) == td.Width;
			ok = (fclose(f) == 0) && ok;
		}
		m_ctx->Unmap(st, 0);
	}
	st->Release();
	return ok;
}

}  // namespace srvrender
