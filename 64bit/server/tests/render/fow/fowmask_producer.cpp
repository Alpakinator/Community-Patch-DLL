// fowmask_producer.cpp - a test producer of the server's fog mask hand-over (ticket 0339): srv_fow_mask_pub.cpp alone,
// on a hardware device (else WARP), publishing masks in a window that pans by the clock (shell_fowmask.h FowTestPan,
// ticket 0394) whose mip 0 holds the pair generation's low byte in R in a 5 x 5 block at the world origin's texel, for
// shell/tests/test_fow_models.py (the consumer is shell_composite.dll in test_host.exe, fow_models_test=<mode>).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Test program only.
//
//   fowmask_producer.exe --exe-pid N [--section NAME] [--seconds S] [--size 1024|512] [--hz H] [--stop-at S] [--no-mask] [--json PATH]
// --no-mask: the section is made and kept alive, but no mask is ever handed over (the consumer's timeout).
// --stop-at S: after S seconds the publisher stops (STOPPED: the consumer puts the white view back) and starts again 2 s
// later. Prints one JSON object (and writes it to --json): the counters and the consumer's half of the section. Exit 0,
// 2 on a bad argument or no device.
#include "srv_fow_mask_pub.h"
#include <d3d11_1.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#pragma comment(lib, "d3d11.lib")

namespace {
void Log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "producer: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}
}  // namespace

int wmain(int argc, wchar_t** argv)
{
	uint32_t pid = 0, size = 1024, hz = 120;
	double seconds = 10, stopAt = -1;
	std::wstring section, json;
	bool noMask = false;
	for(int i = 1; i < argc; ++i)
	{
		const std::wstring a = argv[i];
		const wchar_t* v = i + 1 < argc ? argv[i + 1] : nullptr;
		if(a == L"--exe-pid" && v) pid = (uint32_t)wcstoul(argv[++i], nullptr, 10);
		else if(a == L"--section" && v) section = argv[++i];
		else if(a == L"--seconds" && v) seconds = _wtof(argv[++i]);
		else if(a == L"--size" && v) size = (uint32_t)wcstoul(argv[++i], nullptr, 10);
		else if(a == L"--hz" && v) hz = (uint32_t)wcstoul(argv[++i], nullptr, 10);
		else if(a == L"--stop-at" && v) stopAt = _wtof(argv[++i]);
		else if(a == L"--json" && v) json = argv[++i];
		else if(a == L"--no-mask") noMask = true;
		else
		{
			fprintf(stderr, "fowmask_producer: unknown argument %ls\n", a.c_str());
			return 2;
		}
	}
	if(!pid || (size != 1024 && size != 512) || !hz)
	{
		fprintf(stderr, "fowmask_producer: --exe-pid N is required; --size 1024 or 512\n");
		return 2;
	}
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)) &&
	   FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
	{
		fprintf(stderr, "fowmask_producer: no device\n");
		return 2;
	}
	// the mask as c5r::fow::MaskRenderer makes it: typeless, the full chain, generate-mips
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = size;
	d.MipLevels = size == 1024 ? 11 : 10;
	d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	d.SampleDesc.Count = 1;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	ID3D11Texture2D* mask = nullptr;
	if(FAILED(dev->CreateTexture2D(&d, nullptr, &mask)))
	{
		fprintf(stderr, "fowmask_producer: the mask texture\n");
		return 2;
	}
	srvrender::FowMaskPublisher pub(&Log);
	if(!section.empty())
		pub.setSectionName(section);
	LARGE_INTEGER f, t0, now;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t0);
	uint32_t gen = 0;
	bool stopped = false, restarted = false;
	for(;;)
	{
		QueryPerformanceCounter(&now);
		const double s = (double)(now.QuadPart - t0.QuadPart) / (double)f.QuadPart;
		if(s >= seconds)
			break;
		if(stopAt >= 0 && s >= stopAt && !stopped)
		{
			pub.stop("test: --stop-at");
			stopped = true;
		}
		if(stopped && !restarted && s < stopAt + 2.0)
		{
			Sleep(1000 / hz);
			continue;
		}
		restarted = stopped;
		++gen;
		// the window of this mask by the clock (shell_fowmask.h FowTestPan, the stand-in's synthetic EXE computes the same),
		// and a 5 x 5 block of the pair generation's low byte at the world origin's texel in it (ticket 0394: the consumer
		// finds it where the window it moved the mask to puts the origin)
		srvrender::FowMaskPubInput in;
		in.numViews = 1;
		float p[16] = {FOW_TEST_SX, 0, 0, 0, 0, FOW_TEST_SY, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0.5f, 1};
		LARGE_INTEGER q;
		QueryPerformanceCounter(&q);
		FowTestPan((double)q.QuadPart * 1000.0 / (double)f.QuadPart, p[0], p[12], p[13]);
		memcpy(in.overlayProj[0], p, sizeof(p));
		{
			const int u = (int)floor(((double)p[12] + 1.0) * 0.5 * size), v = (int)floor((1.0 - (double)p[13]) * 0.5 * size);
			uint32_t block[25];
			for(uint32_t& t : block)
				t = (gen & 0xFFu) | 0xFF000000u;
			D3D11_BOX box = {(UINT)(u - 2), (UINT)(v - 2), 0, (UINT)(u + 3), (UINT)(v + 3), 1};
			ctx->UpdateSubresource(mask, 0, &box, block, 20, 100);
		}
		in.pairGeneration = gen;
		in.pairPresentCount = gen;
		in.fogGeneration = 1;
		in.fowAnim[0] = in.fowAnim[1] = 0.00195367f;
		in.fowAnim[2] = -2.0f * (float)s;
		pub.frame(ctx, pid, noMask ? nullptr : mask, true, in);
		ctx->Flush();
		std::string why;
		if(pub.consumerFailed(why))
			Log("%s", why.c_str());
		Sleep(1000 / hz);
	}
	std::string why;
	const bool failed = pub.consumerFailed(why);
	// the consumer's half, read back through a second mapping of the section
	wchar_t name[96];
	if(!section.empty())
		_snwprintf_s(name, _TRUNCATE, L"%s", section.c_str());
	else
		_snwprintf_s(name, _TRUNCATE, VPSHELL_FOWMASK_SECTION_FMT, pid);
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
	const VPShellFowMaskSection* sec = h ? (const VPShellFowMaskSection*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, VPSHELL_FOWMASK_SECTION_BYTES) : nullptr;
	char out[2048];
	std::string esc;
	for(char c : why)
		esc += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
	_snprintf_s(out, _TRUNCATE,
	            "{\"generations\": %u, \"copies\": %lld, \"published\": %lld, \"skipped\": %lld, \"faults\": %lld, \"consumerState\": \"%s\", "
	            "\"failed\": %s, \"why\": \"%s\", \"wanted\": %u, \"consumerPid\": %u, \"ackGeneration\": %u, \"consumerCopies\": %u, "
	            "\"lagFrames\": %u, \"failReason\": \"%s\"}",
	            gen, (long long)pub.copies, (long long)pub.published, (long long)pub.skipped, (long long)pub.faults,
	            srvrender::FowMaskPublisher::ConsumerStateName(pub.consumerState()), failed ? "true" : "false", esc.c_str(),
	            sec ? sec->wanted : 0u, sec ? sec->consumerPid : 0u, sec ? sec->ackGeneration : 0u, sec ? sec->copiesTotal : 0u,
	            sec ? sec->lagFrames : 0u, srvrender::FowMaskPublisher::FailName(sec ? sec->failReason : 0u));
	if(sec)
		UnmapViewOfFile(sec);
	if(h)
		CloseHandle(h);
	printf("%s\n", out);
	if(!json.empty())
	{
		FILE* fp = nullptr;
		if(!_wfopen_s(&fp, json.c_str(), L"wb") && fp)
		{
			fputs(out, fp);
			fclose(fp);
		}
	}
	pub.release("the producer ends");
	mask->Release();
	ctx->Release();
	dev->Release();
	return 0;
}
