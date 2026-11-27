// fake_consumer.cpp - the consumer half of a fake composite for the renderer inside server.exe (issue 3301). Test program
// only; creates no window. It takes frames from the version-3 section exactly as shell_composite.dll's AcquireAndCopy does
// (shell/composite/shell_composite.cpp, read only: texture block under texSeq, OpenSharedResourceByName, keyed mutex
// AcquireSync(1) -> frame block -> copy -> ReleaseSync(0), clientFramesConsumed), plus the depth block and its texture, and
// reads every frame back: one JSON line per frame with frameCounter, the depth block's camFrameStamp, the share of drawn
// (non-transparent) pixels and the depth range over drawn and undrawn pixels.
//
//   fake_consumer.exe --section NAME --frames N [--timeout S] [--request WxH] [--out PATH]
// Exit: 0 N frames taken, 1 timeout (fewer frames), 2 bad arguments, 3 device, 4 the section does not exist.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <share.h>
#include <string>
#include "shell_shared.h"

template <class T> static void Rel(T*& p)
{
	if(p)
	{
		p->Release();
		p = nullptr;
	}
}

template <class T> static bool Snap(const volatile T* src, T& out)
{
	const volatile uint32_t* seq = (const volatile uint32_t*)src;
	for(int i = 0; i < 64; ++i)
	{
		const uint32_t a = *seq;
		if(a == 0)
			return false;
		if(a & 1)
		{
			YieldProcessor();
			continue;
		}
		MemoryBarrier();
		memcpy(&out, (const void*)src, sizeof(T));
		MemoryBarrier();
		if(*seq == a)
			return true;
	}
	return false;
}

struct TexBlock
{
	uint32_t serverPid, generation, width, height, dxgiFormat;
	uint64_t luid;
	wchar_t name[128];
};

static bool ReadTexBlock(const VPShellHeader* h, TexBlock& out)
{
	for(int i = 0; i < 64; ++i)
	{
		const uint32_t a = h->texSeq;
		if(a & 1)
		{
			YieldProcessor();
			continue;
		}
		MemoryBarrier();
		out.serverPid = h->serverPid;
		out.generation = h->generation;
		out.width = h->width;
		out.height = h->height;
		out.dxgiFormat = h->dxgiFormat;
		out.luid = h->adapterLuid;
		memcpy(out.name, (const void*)h->textureName, sizeof(out.name));
		MemoryBarrier();
		if(h->texSeq == a)
			return a != 0;
	}
	return false;
}

int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const wchar_t* section = nullptr;
	const wchar_t* outPath = nullptr;
	int frames = 1;
	double timeout = 20;
	unsigned reqW = 0, reqH = 0;
	for(int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i];
		if(i + 1 >= argc)
		{
			fwprintf(stderr, L"%ls needs a value\n", a);
			return 2;
		}
		const wchar_t* v = argv[++i];
		if(!wcscmp(a, L"--section"))
			section = v;
		else if(!wcscmp(a, L"--frames"))
			frames = _wtoi(v);
		else if(!wcscmp(a, L"--timeout"))
			timeout = _wtof(v);
		else if(!wcscmp(a, L"--out"))
			outPath = v;
		else if(!wcscmp(a, L"--request"))
		{
			if(swscanf_s(v, L"%ux%u", &reqW, &reqH) != 2)
				return 2;
		}
		else
		{
			fwprintf(stderr, L"unknown argument %ls\n", a);
			return 2;
		}
	}
	if(!section || frames < 1)
		return 2;
	FILE* out = stdout;
	// issue 0149: shared for reading (_wfopen_s opens exclusively): the tests read the "opened" line while it runs
	if(outPath && !(out = _wfsopen(outPath, L"w", _SH_DENYWR)))
		return 2;
	HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, section);
	if(!map)
	{
		fprintf(out, "{\"error\": \"section missing\"}\n");
		return 4;
	}
	VPShellHeader* hdr = (VPShellHeader*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if(!hdr)
		return 4;
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
		return 3;
	ID3D11Device1* dev1 = nullptr;
	dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev1);
	IDXGIDevice* dd = nullptr;
	IDXGIAdapter* ad = nullptr;
	DXGI_ADAPTER_DESC adesc = {};
	if(SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd)) && SUCCEEDED(dd->GetAdapter(&ad)))
		ad->GetDesc(&adesc);
	Rel(ad);
	Rel(dd);
	hdr->clientPid = GetCurrentProcessId();
	hdr->clientLuid = ((uint64_t)(uint32_t)adesc.AdapterLuid.HighPart << 32) | adesc.AdapterLuid.LowPart;
	if(reqW && reqH)
	{
		hdr->reqWidth = reqW;
		hdr->reqHeight = reqH;
		MemoryBarrier();
		InterlockedIncrement((volatile LONG*)&hdr->reqSeq);
	}
	ID3D11Texture2D* shared = nullptr;
	IDXGIKeyedMutex* km = nullptr;
	ID3D11Texture2D* depth = nullptr;
	uint32_t gen = 0, depthGen = 0;
	int taken = 0;
	const ULONGLONG t0 = GetTickCount64();
	while(taken < frames && (double)(GetTickCount64() - t0) < timeout * 1000.0)
	{
		TexBlock tb;
		if(hdr->magic != VPSHELL_MAGIC || !ReadTexBlock(hdr, tb))
		{
			Sleep(2);
			continue;
		}
		if(tb.generation != gen || !shared)
		{
			Rel(km);
			Rel(shared);
			if(!dev1 || FAILED(dev1->OpenSharedResourceByName(tb.name, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
			                                                    __uuidof(ID3D11Texture2D), (void**)&shared)) ||
			   FAILED(shared->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km)))
			{
				Rel(shared);
				Sleep(5);
				continue;
			}
			gen = tb.generation;
			std::wstring esc;   // JSON: the name's backslashes escaped
			for(const wchar_t* q = tb.name; *q && q < tb.name + 128; ++q)
			{
				if(*q == L'\\' || *q == L'"')
					esc += L'\\';
				esc += *q;
			}
			fprintf(out, "{\"opened\": \"%ls\", \"generation\": %u, \"width\": %u, \"height\": %u}\n", esc.c_str(), gen, tb.width, tb.height);
			fflush(out);   // issue 0149: the tests wait for this line before they write cameras
		}
		const HRESULT ar = km->AcquireSync(1, 100);
		if(ar != S_OK)
			continue;
		const int64_t fc = hdr->frameCounter;
		VPShellDepth db = {};
		const bool haveDepth = Snap(VPSHELL_DEPTH(hdr), db) && db.state == 1;
		D3D11_TEXTURE2D_DESC td;
		shared->GetDesc(&td);
		td.Usage = D3D11_USAGE_STAGING;
		td.BindFlags = 0;
		td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		td.MiscFlags = 0;
		ID3D11Texture2D* st = nullptr;
		dev->CreateTexture2D(&td, nullptr, &st);
		ctx->CopyResource(st, shared);
		// the depth texture of the same keyed-mutex cycle
		ID3D11Texture2D* dst = nullptr;
		if(haveDepth && (db.generation != depthGen || !depth))
		{
			Rel(depth);
			if(dev1 && SUCCEEDED(dev1->OpenSharedResourceByName(db.textureName, DXGI_SHARED_RESOURCE_READ, __uuidof(ID3D11Texture2D),
			                                                     (void**)&depth)))
				depthGen = db.generation;
		}
		if(haveDepth && depth)
		{
			D3D11_TEXTURE2D_DESC dd2;
			depth->GetDesc(&dd2);
			dd2.Usage = D3D11_USAGE_STAGING;
			dd2.BindFlags = 0;
			dd2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			dd2.MiscFlags = 0;
			dev->CreateTexture2D(&dd2, nullptr, &dst);
			if(dst)
				ctx->CopyResource(dst, depth);
		}
		km->ReleaseSync(0);
		InterlockedIncrement64(&hdr->clientFramesConsumed);
		size_t drawn = 0, total = (size_t)td.Width * td.Height;
		std::string drawnMask;
		D3D11_MAPPED_SUBRESOURCE m;
		if(st && SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			drawnMask.assign(total, 0);
			for(UINT y = 0; y < td.Height; ++y)
			{
				const uint32_t* row = (const uint32_t*)((const char*)m.pData + (size_t)y * m.RowPitch);
				for(UINT x = 0; x < td.Width; ++x)
					if(row[x] & 0x00FFFFFFu)
					{
						++drawn;
						drawnMask[(size_t)y * td.Width + x] = 1;
					}
			}
			ctx->Unmap(st, 0);
		}
		float dMinDrawn = 2, dMaxDrawn = -1, dMinEmpty = 2, dMaxEmpty = -1;
		if(dst && SUCCEEDED(ctx->Map(dst, 0, D3D11_MAP_READ, 0, &m)) && !drawnMask.empty())
		{
			for(UINT y = 0; y < td.Height; ++y)
			{
				const float* row = (const float*)((const char*)m.pData + (size_t)y * m.RowPitch);
				for(UINT x = 0; x < td.Width; ++x)
				{
					const float d = row[x];
					if(drawnMask[(size_t)y * td.Width + x])
					{
						dMinDrawn = d < dMinDrawn ? d : dMinDrawn;
						dMaxDrawn = d > dMaxDrawn ? d : dMaxDrawn;
					}
					else
					{
						dMinEmpty = d < dMinEmpty ? d : dMinEmpty;
						dMaxEmpty = d > dMaxEmpty ? d : dMaxEmpty;
					}
				}
			}
			ctx->Unmap(dst, 0);
		}
		Rel(st);
		Rel(dst);
		++taken;
		fprintf(out,
		        "{\"frame\": %d, \"frameCounter\": %lld, \"camFrameStamp\": %lld, \"depthFrameCounter\": %lld, \"width\": %u, \"height\": %u, "
		        "\"drawn\": %.5f, \"depthDrawnMin\": %.6f, \"depthDrawnMax\": %.6f, \"depthEmptyMin\": %.6f, \"depthEmptyMax\": %.6f, "
		        "\"generation\": %u}\n",
		        taken, (long long)fc, haveDepth ? (long long)db.camFrameStamp : -1LL, haveDepth ? (long long)db.frameCounter : -1LL, td.Width,
		        td.Height, total ? (double)drawn / total : 0.0, dMinDrawn, dMaxDrawn, dMinEmpty, dMaxEmpty, gen);
		fflush(out);
	}
	Rel(depth);
	Rel(km);
	Rel(shared);
	Rel(dev1);
	Rel(ctx);
	Rel(dev);
	UnmapViewOfFile(hdr);
	CloseHandle(map);
	fprintf(out, "{\"taken\": %d}\n", taken);
	if(out != stdout)
		fclose(out);
	return taken >= frames ? 0 : 1;
}
