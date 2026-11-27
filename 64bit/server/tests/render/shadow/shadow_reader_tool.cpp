// shadow_reader_tool.cpp - the shadow tap's reader (server/src/render/srv_shadow_tap.cpp) as a command, for the tests of
// issue 4304 (server/tests/render/shadow/test_reader_shadow.py and shell/tests/test_shadow_tap.py) and the live checks: it
// makes a D3D11 device on the default adapter (as server.exe's render device), runs srvrender::ShadowTapReader::acquire()
// in a loop and reports what the reader decided. Written 2026-11-15 for this repository (GPL-3.0-or-later). Model:
// server/tests/render/overlay/test_reader_tool.cpp.
//
//   shadow_reader_tool.exe --pid PID [--section NAME] [--main NAME] [--seconds S] [--pairs N] [--json OUT] [--readback]
//                          [--craft W,H,FORMAT --craft-slot S --craft-epoch E]
//
// --pid: the EXE pid of the section Local\VPShellShadowTap.<pid> (--section names another). --main: the main section
// (presentCount for the stale rule). --pairs N: stop after N new triples (default 1; 0 = run the whole time). --readback:
// after each new triple read back word 0 of texel (0,0) and of texel (100,37) of the bound texture and check the triple:
// in the DLL's test mode texel (0,0) holds frame k in every 16-bit word, ShadowMatrix0 view 0 element 12 holds k and the
// strength is ((k & 1023) + 1) / 1025 (quality 0: no texture, strength 0); a mismatch is a pairing error. --craft: create
// the NT-shared texture of slot S, epoch E of EXE pid PID here (tests of the reader without a DLL).
// Output: the reader's notes (RNote), then "result: state S accepted A refused R stale T pairs P mismatches M verify OK/BAD
// why: ...". JSON: {"state", "failReason", "why", "installFailure", "accepted", "refused", "stale", "reopened", "verifyOk",
// "verifyBad", "mismatches", "inUseSlot", "ackGeneration", "pairs": [{"generation", "slot", "presentCount", "numViews",
// "quality", "width", "height", "format", "strength", "srv", "m0": [16], "texel0", "texelKnown", "k"}]}.
// Exit: 0 at least one triple bound (N with --pairs N), no mismatch, no verify difference; 1 the reader is Failed; 2 usage;
// 3 the main section does not open; 4 no triple bound within the time; 5 a pairing mismatch; 6 no D3D11 device; 7 a verify
// read differed.
#include "srv_shadow_tap.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <math.h>
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

static const char* StateName(ShadowTapReader::State s)
{
	switch(s)
	{
	case ShadowTapReader::State::Absent: return "Absent";
	case ShadowTapReader::State::Off: return "Off";
	case ShadowTapReader::State::Waiting: return "Waiting";
	case ShadowTapReader::State::Active: return "Active";
	case ShadowTapReader::State::Failed: return "Failed";
	}
	return "?";
}

struct Pair
{
	uint32_t generation, slot, numViews, quality, width, height, format;
	uint64_t presentCount;
	float strength;
	float m0[16];
	bool srv;
	uint32_t texel0 = 0, texelKnown = 0, k = 0;
	bool unitMaskDropped = false;   // ticket 4785
	uint32_t rawWidth = 0, rawHeight = 0;
};

// the first 16-bit word of texel (x, y) of the SRV's texture through a 1x1 staging copy
static bool ReadWord(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, uint32_t x, uint32_t y,
                     uint32_t& out)
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
	if(x < d.Width && y < d.Height && SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &st)))
	{
		D3D11_BOX b = {x, y, 0, x + 1, y + 1, 1};
		ctx->CopySubresourceRegion(st, 0, 0, 0, 0, t, 0, &b);
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			uint16_t w;
			memcpy(&w, m.pData, 2);
			out = w;
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
	const wchar_t* mainName = nullptr;
	const wchar_t* json = nullptr;
	double seconds = 10;
	int pairsWanted = 1;
	bool readback = false;
	unsigned pid = 0, cw = 0, ch = 0, cf = 0, cslot = 0, cepoch = 1;
	bool craft = false;
	for(int i = 1; i < argc; ++i)
	{
		if(!wcscmp(argv[i], L"--pid") && i + 1 < argc)
			pid = (unsigned)_wtoi(argv[++i]);
		else if(!wcscmp(argv[i], L"--section") && i + 1 < argc)
			section = argv[++i];
		else if(!wcscmp(argv[i], L"--main") && i + 1 < argc)
			mainName = argv[++i];
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
			craft = swscanf_s(argv[++i], L"%u,%u,%u", &cw, &ch, &cf) == 3;
			if(!craft)
			{
				printf("usage: --craft W,H,FORMAT\n");
				return 2;
			}
		}
		else if(!wcscmp(argv[i], L"--craft-slot") && i + 1 < argc)
			cslot = (unsigned)_wtoi(argv[++i]);
		else if(!wcscmp(argv[i], L"--craft-epoch") && i + 1 < argc)
			cepoch = (unsigned)_wtoi(argv[++i]);
		else
		{
			printf("usage: unknown argument '%ls'\n", argv[i]);
			return 2;
		}
	}
	if(!pid)
	{
		printf("usage: shadow_reader_tool.exe --pid PID [--section NAME] [--main NAME] [--seconds S] [--pairs N] [--json OUT] "
		       "[--readback] [--craft W,H,FORMAT --craft-slot S --craft-epoch E] (--pid required)\n");
		return 2;
	}
	LogOpen(nullptr, HostLog);
	HANDLE mh = nullptr;
	VPShellHeader* hdr = nullptr;
	if(mainName)
	{
		mh = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, mainName);
		hdr = mh ? (VPShellHeader*)MapViewOfFile(mh, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0) : nullptr;
		if(!hdr)
		{
			printf("result: the main section '%ls' does not open (error %lu)\n", mainName, GetLastError());
			return 3;
		}
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
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = (DXGI_FORMAT)cf;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		HRESULT hr = dev->CreateTexture2D(&d, nullptr, &crafted);
		IDXGIResource1* r = nullptr;
		wchar_t name[96];
		_snwprintf_s(name, _TRUNCATE, VPSHELL_SHADOW_TEXTURE_FMT, pid, cslot, cepoch);
		if(SUCCEEDED(hr))
			hr = crafted->QueryInterface(__uuidof(IDXGIResource1), (void**)&r);
		if(SUCCEEDED(hr))
		{
			hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, name, &craftedNt);
			r->Release();
		}
		if(FAILED(hr))
		{
			printf("result: crafting '%ls' (%ux%u, format %u) failed 0x%08X\n", name, cw, ch, cf, (unsigned)hr);
			return 2;
		}
		printf("crafted '%ls' %ux%u, format %u\n", name, cw, ch, cf);
	}
	std::vector<Pair> pairs;
	int mismatches = 0;
	int code = 4;
	uint32_t inUse = 0, ack = 0;
	{
		ShadowTapReader* rdp = section ? new ShadowTapReader(std::wstring(section), pid, hdr, dev) : new ShadowTapReader(pid, hdr, dev);
		ShadowTapReader& rd = *rdp;
		const int64_t t0 = Qpc();
		uint32_t lastGen = 0;
		while(QpcToMs(Qpc() - t0) < seconds * 1000.0)
		{
			ShadowFrame f;
			if(rd.acquire(f) && f.generation != lastGen)
			{
				lastGen = f.generation;
				Pair p;
				p.generation = f.generation;
				p.slot = f.slot;
				p.numViews = f.numViews;
				p.quality = f.quality;
				p.width = f.width;
				p.height = f.height;
				p.format = f.format;
				p.presentCount = f.presentCount;
				p.strength = f.strength;
				p.srv = f.srv != nullptr;
				p.unitMaskDropped = f.unitMaskDropped;   // ticket 4785
				p.rawWidth = f.rawWidth;
				p.rawHeight = f.rawHeight;
				memcpy(p.m0, f.matrices[0], sizeof(p.m0));
				if(readback)
				{
					p.k = (uint32_t)p.m0[12];
					const float want = f.quality ? (float)((p.k & 1023) + 1) / 1025.0f : 0.0f;
					bool bad = fabsf(f.strength - want) > 1e-6f;
					if(f.srv)
					{
						ReadWord(dev, ctx, f.srv, 0, 0, p.texel0);
						ReadWord(dev, ctx, f.srv, 100, 37, p.texelKnown);
						bad = bad || p.texel0 != (p.k & 0xFFFF);
					}
					else if(f.quality != 0)
						bad = true;
					if(bad)
					{
						mismatches++;
						printf("pairing: MISMATCH generation %u slot %u quality %u: ShadowMatrix0 k %u, texel (0,0) %04X, strength %.9g "
						       "(want %.9g)\n",
						       p.generation, p.slot, p.quality, p.k, p.texel0, f.strength, want);
					}
				}
				pairs.push_back(p);
				if(pairsWanted > 0 && (int)pairs.size() >= pairsWanted)
					break;
			}
			if(rd.state() == ShadowTapReader::State::Failed)
				break;
			Sleep(2);
		}
		const bool failed = rd.state() == ShadowTapReader::State::Failed;
		code = failed ? 1 : mismatches ? 5 : rd.verifyBad ? 7 : pairs.empty() || (pairsWanted > 0 && (int)pairs.size() < pairsWanted) ? 4 : 0;
		printf("result: state %s failReason %u accepted %lld refused %lld stale %lld pairs %u mismatches %d verify %lld/%lld "
		       "reopened %lld why: %s\n",
		       StateName(rd.state()), rd.failReason(), (long long)rd.accepted, (long long)rd.refused, (long long)rd.stale,
		       (unsigned)pairs.size(), mismatches, (long long)rd.verifyOk, (long long)rd.verifyBad, (long long)rd.reopened,
		       rd.lastWhy().c_str());
		std::string why = rd.lastWhy();
		ShadowTapReader::State st = rd.state();
		uint32_t fr = rd.failReason();
		long long acc = rd.accepted, ref = rd.refused, stl = rd.stale, reo = rd.reopened, vok = rd.verifyOk, vbad = rd.verifyBad;
		const bool inst = rd.installFailure();
		delete rdp;   // names no slot in inUseSlot
		// the block's consumer fields after the reader let go (the section may be gone: read through a fresh mapping)
		wchar_t nm[96];
		if(section)
			wcsncpy_s(nm, section, _TRUNCATE);
		else
			_snwprintf_s(nm, _TRUNCATE, VPSHELL_SHADOWTAP_SECTION_FMT, pid);
		HANDLE sh = OpenFileMappingW(FILE_MAP_READ, FALSE, nm);
		const VPShellShadowTap* sb = sh ? (const VPShellShadowTap*)MapViewOfFile(sh, FILE_MAP_READ, 0, 0, 0) : nullptr;
		if(sb)
		{
			inUse = sb->inUseSlot;
			ack = sb->ackGeneration;
			UnmapViewOfFile(sb);
		}
		if(sh)
			CloseHandle(sh);
		if(json)
		{
			FILE* fj = _wfopen(json, L"w");
			if(fj)
			{
				fprintf(fj, "{\"state\": \"%s\", \"failReason\": %u, \"why\": \"%s\", \"installFailure\": %s, \"accepted\": %lld, "
				            "\"refused\": %lld, \"stale\": %lld, \"reopened\": %lld, \"verifyOk\": %lld, \"verifyBad\": %lld, "
				            "\"mismatches\": %d, \"inUseSlot\": %u, \"ackGeneration\": %u, \"pairs\": [",
				        StateName(st), fr, JsonEsc(why).c_str(), inst ? "true" : "false", acc, ref, stl, reo, vok, vbad, mismatches,
				        inUse, ack);
				for(size_t i = 0; i < pairs.size(); ++i)
				{
					const Pair& p = pairs[i];
					fprintf(fj, "%s{\"generation\": %u, \"slot\": %u, \"presentCount\": %llu, \"numViews\": %u, \"quality\": %u, "
					            "\"width\": %u, \"height\": %u, \"format\": %u, \"strength\": %.9g, \"srv\": %s, \"texel0\": %u, "
					            "\"texelKnown\": %u, \"k\": %u, \"unitMaskDropped\": %s, \"rawWidth\": %u, \"rawHeight\": %u, \"m0\": [",
					        i ? ", " : "", p.generation, p.slot, (unsigned long long)p.presentCount, p.numViews, p.quality, p.width,
					        p.height, p.format, p.strength, p.srv ? "true" : "false", p.texel0, p.texelKnown, p.k,
					        p.unitMaskDropped ? "true" : "false", p.rawWidth, p.rawHeight);
					for(int e = 0; e < 16; ++e)
						fprintf(fj, "%s%.9g", e ? ", " : "", p.m0[e]);
					fprintf(fj, "]}");
				}
				fprintf(fj, "]}\n");
				fclose(fj);
			}
		}
	}
	if(crafted)
		crafted->Release();
	if(craftedNt)
		CloseHandle(craftedNt);
	ctx->Release();
	dev->Release();
	if(hdr)
		UnmapViewOfFile(hdr);
	if(mh)
		CloseHandle(mh);
	return code;
}
