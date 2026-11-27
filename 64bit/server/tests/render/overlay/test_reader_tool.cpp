// test_reader_tool.cpp - the overlay tap's reader (server/src/render/srv_overlay_tap.cpp) as a command, for the tests of
// issue 3703 (server/tests/render/overlay/test_reader_overlay.py and shell/tests/test_overlay_tap.py): it maps a main
// section by name, makes a D3D11 device on the default adapter (as server.exe's render device), runs
// srvrender::OverlayTapReader::acquire() in a loop and reports what the reader decided.
// Written 2026-11-14 for this repository (GPL-3.0-or-later).
//
//   test_reader_tool.exe --section NAME [--seconds S] [--pairs N] [--json OUT] [--readback]
//                        [--craft W,H,MIPS,FORMAT --craft-pid PID --craft-slot S]
//
// --pairs N: stop after N new pairs were bound (default 1; 0 = run the whole time). --readback: after each new pair,
// read back texel (0,0) of mips 0 and 5 and texel (100,37) of mip 0 of the bound texture (staging copy on this device)
// and check the pair: in the DLL's test mode texel (0,0) holds frame k (r = k, g = k >> 8) and OverlayProj view 0
// element 12 holds k; a mismatch is a pairing error. --craft: create the NT-shared texture of slot S of EXE pid PID
// here (tests of the reader without a DLL; the test writes the block and the overlay section itself).
// Output: the reader's notes (RNote) on stdout, then "result: state S accepted A refused R stale T pairs P mismatches M
// why: ...". JSON: {"state", "failReason", "why", "accepted", "refused", "stale", "pairs": [{"generation", "slot",
// "presentCount", "numViews", "proj0": [16], "texel0", "texel5", "texelKnown", "k"}], "mismatches"}.
// Exit: 0 at least one pair bound (N pairs with --pairs N) and no mismatch; 1 the reader is Failed; 2 usage; 3 the main
// section does not open; 4 no pair bound within the time; 5 a pairing mismatch; 6 no D3D11 device.
#include "srv_overlay_tap.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

using namespace srvrender;

static void HostLog(const char* line) { printf("%s\n", line); fflush(stdout); }

static std::string JsonEsc(const std::string& s)
{
	std::string o;
	for(char c : s)
	{
		if(c == '"' || c == '\\')
			o += '\\';
		if((unsigned char)c < 0x20)
			continue;
		o += c;
	}
	return o;
}

static const char* StateName(OverlayTapReader::State s)
{
	switch(s)
	{
	case OverlayTapReader::State::Absent: return "Absent";
	case OverlayTapReader::State::Off: return "Off";
	case OverlayTapReader::State::Waiting: return "Waiting";
	case OverlayTapReader::State::Active: return "Active";
	case OverlayTapReader::State::Failed: return "Failed";
	}
	return "?";
}

struct Pair
{
	uint32_t generation, slot, numViews;
	uint64_t presentCount;
	float proj0[16];
	uint32_t texel0 = 0, texel5 = 0, texelKnown = 0;
	uint32_t k = 0;
};

// texels (x, y) of `mip` of the SRV's texture through a 1x1 staging copy
static bool ReadTexel(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, uint32_t mip, uint32_t x,
                      uint32_t y, uint32_t& out)
{
	ID3D11Resource* r = nullptr;
	srv->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t);
	r->Release();
	if(!t)
		return false;
	D3D11_TEXTURE2D_DESC d;
	t->GetDesc(&d);
	D3D11_TEXTURE2D_DESC sd = {};
	sd.Width = sd.Height = 1;
	sd.MipLevels = 1;
	sd.ArraySize = 1;
	sd.Format = d.Format;
	sd.SampleDesc.Count = 1;
	sd.Usage = D3D11_USAGE_STAGING;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* st = nullptr;
	bool ok = false;
	if(SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &st)))
	{
		D3D11_BOX b = {x, y, 0, x + 1, y + 1, 1};
		ctx->CopySubresourceRegion(st, 0, 0, 0, 0, t, D3D11CalcSubresource(mip, 0, d.MipLevels), &b);
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			memcpy(&out, m.pData, 4);
			ctx->Unmap(st, 0);
			ok = true;
		}
		st->Release();
	}
	t->Release();
	return ok;
}

int wmain(int argc, wchar_t** argv)
{
	const wchar_t* section = nullptr;
	const wchar_t* json = nullptr;
	double seconds = 10;
	int pairsWanted = 1;
	bool readback = false;
	unsigned cw = 0, ch = 0, cm = 0, cf = 0, cpid = 0, cslot = 0;
	bool craft = false;
	for(int i = 1; i < argc; ++i)
	{
		if(!wcscmp(argv[i], L"--section") && i + 1 < argc)
			section = argv[++i];
		else if(!wcscmp(argv[i], L"--json") && i + 1 < argc)
			json = argv[++i];
		else if(!wcscmp(argv[i], L"--seconds") && i + 1 < argc)
			seconds = _wtof(argv[++i]);
		else if(!wcscmp(argv[i], L"--pairs") && i + 1 < argc)
			pairsWanted = _wtoi(argv[++i]);
		else if(!wcscmp(argv[i], L"--readback"))
			readback = true;
		else if(!wcscmp(argv[i], L"--craft") && i + 1 < argc)
		{
			craft = swscanf_s(argv[++i], L"%u,%u,%u,%u", &cw, &ch, &cm, &cf) == 4;
			if(!craft)
			{
				printf("usage: --craft W,H,MIPS,FORMAT\n");
				return 2;
			}
		}
		else if(!wcscmp(argv[i], L"--craft-pid") && i + 1 < argc)
			cpid = (unsigned)_wtoi(argv[++i]);
		else if(!wcscmp(argv[i], L"--craft-slot") && i + 1 < argc)
			cslot = (unsigned)_wtoi(argv[++i]);
		else
		{
			printf("usage: unknown argument '%ls'\n", argv[i]);
			return 2;
		}
	}
	if(!section)
	{
		printf("usage: test_reader_tool.exe --section NAME [--seconds S] [--pairs N] [--json OUT] [--readback] [--craft W,H,MIPS,FORMAT "
		       "--craft-pid PID --craft-slot S] (--section required)\n");
		return 2;
	}
	LogOpen(nullptr, HostLog);
	HANDLE mh = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, section);
	VPShellHeader* hdr = mh ? (VPShellHeader*)MapViewOfFile(mh, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0) : nullptr;
	if(!hdr)
	{
		printf("result: the main section '%ls' does not open (error %lu)\n", section, GetLastError());
		return 3;
	}
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
	{
		printf("result: no D3D11 device\n");
		return 6;
	}
	ID3D11Texture2D* crafted = nullptr;
	HANDLE craftedNt = nullptr;
	if(craft)
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = cw;
		d.Height = ch;
		d.MipLevels = cm;
		d.ArraySize = 1;
		d.Format = (DXGI_FORMAT)cf;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		HRESULT hr = dev->CreateTexture2D(&d, nullptr, &crafted);
		IDXGIResource1* r = nullptr;
		wchar_t name[80];
		_snwprintf_s(name, _TRUNCATE, VPSHELL_OVERLAY_TEXTURE_FMT, cpid, cslot);
		if(SUCCEEDED(hr))
			hr = crafted->QueryInterface(__uuidof(IDXGIResource1), (void**)&r);
		if(SUCCEEDED(hr))
		{
			hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, name, &craftedNt);
			r->Release();
		}
		if(FAILED(hr))
		{
			printf("result: crafting '%ls' (%ux%u, %u mips, format %u) failed 0x%08X\n", name, cw, ch, cm, cf, (unsigned)hr);
			return 2;
		}
		printf("crafted '%ls' %ux%u, %u mips, format %u\n", name, cw, ch, cm, cf);
	}
	std::vector<Pair> pairs;
	int mismatches = 0;
	int code = 4;
	{
		OverlayTapReader rd(hdr, dev);
		const int64_t t0 = Qpc();
		uint32_t lastGen = 0;
		while(QpcToMs(Qpc() - t0) < seconds * 1000.0)
		{
			OverlayFrame f;
			if(rd.acquire(f) && f.generation != lastGen)
			{
				lastGen = f.generation;
				Pair p;
				p.generation = f.generation;
				p.slot = f.slot;
				p.numViews = f.numViews;
				p.presentCount = f.presentCount;
				memcpy(p.proj0, f.overlayProj[0], sizeof(p.proj0));
				if(readback && f.srv)
				{
					ReadTexel(dev, ctx, f.srv, 0, 0, 0, p.texel0);
					ReadTexel(dev, ctx, f.srv, 5, 0, 0, p.texel5);
					ReadTexel(dev, ctx, f.srv, 0, 100, 37, p.texelKnown);
					p.k = (uint32_t)p.proj0[12];
					const uint32_t tk = p.texel0 & 0xFFFF;
					const uint32_t t5 = p.texel5 & 0xFFFF;
					if(tk != (p.k & 0xFFFF) || t5 != (p.k & 0xFFFF) || ((p.texel5 >> 16) & 0xFF) != 5)
					{
						mismatches++;
						printf("pairing: MISMATCH generation %u slot %u: OverlayProj k %u, texel (0,0) mip 0 %08X, mip 5 %08X\n",
						       p.generation, p.slot, p.k, p.texel0, p.texel5);
					}
				}
				pairs.push_back(p);
				if(pairsWanted > 0 && (int)pairs.size() >= pairsWanted)
					break;
			}
			if(rd.state() == OverlayTapReader::State::Failed)
				break;
			Sleep(4);
		}
		const bool failed = rd.state() == OverlayTapReader::State::Failed;
		code = failed ? 1 : mismatches ? 5 : pairs.empty() || (pairsWanted > 0 && (int)pairs.size() < pairsWanted) ? 4 : 0;
		printf("result: state %s failReason %u accepted %lld refused %lld stale %lld pairs %u mismatches %d why: %s\n",
		       StateName(rd.state()), rd.failReason(), (long long)rd.accepted, (long long)rd.refused, (long long)rd.stale,
		       (unsigned)pairs.size(), mismatches, rd.lastWhy().c_str());
		if(json)
		{
			FILE* fj = _wfopen(json, L"w");
			if(fj)
			{
				fprintf(fj, "{\"state\": \"%s\", \"failReason\": %u, \"why\": \"%s\", \"accepted\": %lld, \"refused\": %lld, "
				            "\"stale\": %lld, \"mismatches\": %d, \"inUseSlot\": %u, \"ackGeneration\": %u, \"pairs\": [",
				        StateName(rd.state()), rd.failReason(), JsonEsc(rd.lastWhy()).c_str(), (long long)rd.accepted,
				        (long long)rd.refused, (long long)rd.stale, mismatches, VPSHELL_OVERLAYTAP(hdr)->inUseSlot,
				        VPSHELL_OVERLAYTAP(hdr)->ackGeneration);
				for(size_t i = 0; i < pairs.size(); ++i)
				{
					const Pair& p = pairs[i];
					fprintf(fj, "%s{\"generation\": %u, \"slot\": %u, \"presentCount\": %llu, \"numViews\": %u, \"texel0\": %u, "
					            "\"texel5\": %u, \"texelKnown\": %u, \"k\": %u, \"proj0\": [",
					        i ? ", " : "", p.generation, p.slot, (unsigned long long)p.presentCount, p.numViews, p.texel0,
					        p.texel5, p.texelKnown, p.k);
					for(int e = 0; e < 16; ++e)
						fprintf(fj, "%s%.9g", e ? ", " : "", p.proj0[e]);
					fprintf(fj, "]}");
				}
				fprintf(fj, "]}\n");
				fclose(fj);
			}
		}
	}   // the reader's destructor names no slot in inUseSlot
	if(crafted)
		crafted->Release();
	if(craftedNt)
		CloseHandle(craftedNt);
	ctx->Release();
	dev->Release();
	UnmapViewOfFile(hdr);
	CloseHandle(mh);
	return code;
}
