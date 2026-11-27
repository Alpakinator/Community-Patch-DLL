// bakefake.cpp - a stand-in producer of the bake-tap block for the render host tests (issue 3301). Test code only.
// Origin (COMMON 13, D22): FakeBakeMain of shell/server/shell_server.cpp (--scene bakefake) at this repository's commit
// b4f7748, ported: creates the manifest section and one solid-colour shared texture pair per slot (tiles alternate red /
// blue by parity), writes VPShellBakeTap ACTIVE, waits `seconds`. Negative variants: a missing slot's textures, a manifest
// version, typeless textures (as the live tap's BC1_TYPELESS).
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
#include <vector>
#include "shell_shared.h"

int BakeFakeMain(const wchar_t* sectionName, int tilesX, int tilesY, int tilePlots, int missing, int manifestVersion, bool typeless,
                 double seconds)
{
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
	{
		printf("bakefake: device failed\n");
		return 3;
	}
	HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, sectionName);
	VPShellHeader* hdr = sec ? (VPShellHeader*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES) : nullptr;
	if(!hdr)
	{
		printf("bakefake: section failed %lu\n", GetLastError());
		return 1;
	}
	const uint32_t pid = GetCurrentProcessId();
	const uint32_t n = (uint32_t)(tilesX * tilesY);
	const size_t bytes = sizeof(VPShellBakeManifest) + (size_t)n * sizeof(VPShellBakeSlot);
	wchar_t manName[64];
	_snwprintf_s(manName, _countof(manName), _TRUNCATE, VPSHELL_BAKE_SECTION_FMT, pid);
	HANDLE man = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)bytes, manName);
	uint8_t* mv = man ? (uint8_t*)MapViewOfFile(man, FILE_MAP_ALL_ACCESS, 0, 0, bytes) : nullptr;
	if(!mv)
	{
		printf("bakefake: manifest section failed %lu\n", GetLastError());
		return 1;
	}
	std::vector<ID3D11Texture2D*> texs;
	std::vector<HANDLE> handles;
	VPShellBakeManifest* mh = (VPShellBakeManifest*)mv;
	mh->magic = VPSHELL_BAKE_MAGIC;
	mh->version = (uint32_t)manifestVersion;
	mh->slotCount = n;
	mh->slotBytes = sizeof(VPShellBakeSlot);
	const DXGI_FORMAT fmt = typeless ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
	for(uint32_t i = 0; i < n; ++i)
	{
		VPShellBakeSlot* sl = (VPShellBakeSlot*)(mv + sizeof(VPShellBakeManifest) + (size_t)i * sizeof(VPShellBakeSlot));
		const uint32_t tx = i % (uint32_t)tilesX, ty = i / (uint32_t)tilesX;
		const bool red = ((tx + ty) & 1) == 0;
		if((int)i != missing)
		{
			uint32_t px[16 * 16];
			for(auto& p : px)
				p = red ? 0xFF0000FFu : 0xFFFF0000u;   // R8G8B8A8: red or blue
			for(int k = 0; k < 2; ++k)
			{
				D3D11_TEXTURE2D_DESC td = {};
				td.Width = 16;
				td.Height = 16;
				td.MipLevels = 1;
				td.ArraySize = 1;
				td.SampleDesc.Count = 1;
				td.Format = fmt;
				td.Usage = D3D11_USAGE_DEFAULT;
				td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
				D3D11_SUBRESOURCE_DATA sd = {px, 64, 0};
				ID3D11Texture2D* tex = nullptr;
				HRESULT hr = dev->CreateTexture2D(&td, &sd, &tex);
				IDXGIResource1* r1 = nullptr;
				if(SUCCEEDED(hr))
					hr = tex->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
				wchar_t name[96];
				_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_BAKE_TEXTURE_FMT, pid, i, (unsigned)k);
				HANDLE h = nullptr;
				if(SUCCEEDED(hr))
					hr = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, name, &h);
				if(r1)
					r1->Release();
				if(FAILED(hr))
				{
					printf("bakefake: texture failed 0x%08lX\n", hr);
					return 1;
				}
				texs.push_back(tex);
				handles.push_back(h);
			}
		}
		sl->seq = 1;
		sl->state = 1;
		sl->tileIndex = i;
		sl->tileX = tx;
		sl->tileY = ty;
		sl->generation = 1;
		sl->width = 16;
		sl->height = 16;
		sl->mips = 1;
		sl->dxgiFormat[0] = sl->dxgiFormat[1] = fmt;
		MemoryBarrier();
		sl->seq = 2;
	}
	ctx->Flush();
	VPShellBakeTap* bt = VPSHELL_BAKETAP(hdr);
	InterlockedIncrement((volatile LONG*)&bt->seq);
	MemoryBarrier();
	bt->state = VPBAKE_ACTIVE;
	bt->failReason = 0;
	bt->exePid = pid;
	bt->loadSeq = 1;
	bt->slotCount = n;
	bt->tilesX = (uint32_t)tilesX;
	bt->tilesY = (uint32_t)tilesY;
	bt->tilePlotsX = bt->tilePlotsY = (uint32_t)tilePlots;
	bt->manifestBytes = bytes;
	bt->copiesTotal = (int64_t)n * 2;
	wcsncpy_s(bt->manifestName, manName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&bt->seq);
	InterlockedIncrement((volatile LONG*)&bt->manifestSeq);
	printf("bakefake: %u slots (%dx%d tiles of %d plots, missing slot %d, manifest version %d) ACTIVE\n", n, tilesX, tilesY, tilePlots,
	       missing, manifestVersion);
	fflush(stdout);
	Sleep((DWORD)(seconds * 1000.0));
	printf("bakefake: done (server ack manifest seq %u)\n", bt->ackManifestSeq);
	for(auto* t : texs)
		t->Release();
	for(auto h : handles)
		CloseHandle(h);
	UnmapViewOfFile(mv);
	CloseHandle(man);
	UnmapViewOfFile(hdr);
	CloseHandle(sec);
	ctx->Release();
	dev->Release();
	return 0;
}
