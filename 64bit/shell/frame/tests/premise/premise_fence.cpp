// premise_fence.cpp - issue 3302, premise 1 of research/05-graphics-composition/renderer-in-server-design.md section 8:
// a shared ID3D11Fence created on an x64 device, its NT handle duplicated into a 32-bit process, opened there with
// ID3D11Device5::OpenSharedFence, and ID3D11DeviceContext4::Wait holding that process's GPU queue until the x64 side
// signals. One source, built twice: x64 = the "server" (parent), x86 = the "EXE" (child). No window is created.
//
//   premise_fence_x64.exe <path of premise_fence_x86.exe>      exit 0 = premise holds; prints one line per step
//
// Child exit codes: 0 ok, 10 section, 11 adapter, 12 device, 13 no ID3D11Device5, 14 no ID3D11DeviceContext4,
// 15 OpenSharedFence, 16 query, 17 the queued wait did not hold the GPU (query completed before the signal),
// 18 the GPU never resumed after the signal, 19 completed value wrong.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdint.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static const wchar_t* kSec = L"Local\\VPFramePremise3302.Sec";
static const wchar_t* kWaiting = L"Local\\VPFramePremise3302.Waiting";

struct PremiseSec { uint32_t handleInChild; uint32_t luidLow; int32_t luidHigh; uint32_t pad; };

static double NowMs()
{
	LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

#ifdef _WIN64
int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	if (argc < 2) { printf("usage: premise_fence_x64.exe <child exe>\n"); return 2; }
	ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
	if (FAILED(hr)) { printf("x64: D3D11CreateDevice 0x%08lx\n", hr); return 3; }
	IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr; DXGI_ADAPTER_DESC desc = {};
	dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd); xd->GetAdapter(&ad); ad->GetDesc(&desc);
	wprintf(L"x64: device on %ls (LUID %08lx:%08lx)\n", desc.Description, desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);
	ID3D11Device5* dev5 = nullptr; ID3D11DeviceContext4* ctx4 = nullptr;
	if (FAILED(dev->QueryInterface(__uuidof(ID3D11Device5), (void**)&dev5))) { printf("x64: no ID3D11Device5\n"); return 4; }
	if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&ctx4))) { printf("x64: no ID3D11DeviceContext4\n"); return 4; }
	ID3D11Fence* fence = nullptr;
	hr = dev5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)&fence);
	if (FAILED(hr)) { printf("x64: CreateFence(SHARED) 0x%08lx\n", hr); return 5; }
	HANDLE hFence = nullptr;
	hr = fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &hFence);
	if (FAILED(hr)) { printf("x64: CreateSharedHandle 0x%08lx\n", hr); return 5; }
	printf("x64: shared fence created, NT handle %p\n", hFence);

	HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, kSec);
	PremiseSec* ps = (PremiseSec*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	HANDLE waiting = CreateEventW(nullptr, TRUE, FALSE, kWaiting);
	if (!ps || !waiting) { printf("x64: section/event\n"); return 6; }

	wchar_t cmd[1024]; swprintf_s(cmd, L"\"%ls\"", argv[1]);
	STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
	fflush(stdout);
	si.dwFlags = STARTF_USESTDHANDLES;   // the child's lines go to our stdout (only the std handles are inheritable)
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	if (!CreateProcessW(argv[1], cmd, nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{ printf("x64: CreateProcess %lu\n", GetLastError()); return 7; }
	// As the server will: a handle duplicated into the other process (vp_frame.h srvFenceHandleInExe).
	HANDLE hp = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pi.dwProcessId);
	HANDLE hChild = nullptr;
	if (!hp || !DuplicateHandle(GetCurrentProcess(), hFence, hp, &hChild, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{ printf("x64: DuplicateHandle %lu\n", GetLastError()); TerminateProcess(pi.hProcess, 99); return 8; }
	ps->handleInChild = (uint32_t)(uintptr_t)hChild;
	ps->luidLow = desc.AdapterLuid.LowPart; ps->luidHigh = desc.AdapterLuid.HighPart;
	printf("x64: handle duplicated into the x86 child (pid %lu) as 0x%x\n", pi.dwProcessId, ps->handleInChild);
	ResumeThread(pi.hThread);

	HANDLE hs[2] = { waiting, pi.hProcess };
	DWORD w = WaitForMultipleObjects(2, hs, FALSE, 10000);
	if (w != WAIT_OBJECT_0)
	{
		DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
		printf("x64: child did not reach the wait (wait %lu, child exit %lu)\n", w, code);
		if (w == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 98);
		return 9;
	}
	double t0 = NowMs();
	hr = ctx4->Signal(fence, 1);
	ctx->Flush();
	printf("x64: Signal(1) queued and flushed (hr 0x%08lx), the calls took %.3f ms\n", hr, NowMs() - t0);
	fflush(stdout);
	if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0) { printf("x64: child hung\n"); TerminateProcess(pi.hProcess, 97); return 10; }
	DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
	printf("x64: child exit %lu -> premise %s\n", code, code == 0 ? "HOLDS" : "FAILS");
	CloseHandle(hFence);
	return code == 0 ? 0 : 11;
}
#else
int wmain()
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	setvbuf(stdout, nullptr, _IONBF, 0);
	HANDLE sec = OpenFileMappingW(FILE_MAP_READ, FALSE, kSec);
	const PremiseSec* ps = sec ? (const PremiseSec*)MapViewOfFile(sec, FILE_MAP_READ, 0, 0, 0) : nullptr;
	HANDLE waiting = OpenEventW(EVENT_MODIFY_STATE, FALSE, kWaiting);
	if (!ps || !waiting) { printf("x86: section/event\n"); return 10; }
	IDXGIFactory1* fac = nullptr; IDXGIAdapter1* ad = nullptr; IDXGIAdapter1* pick = nullptr;
	CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fac);
	for (UINT i = 0; fac && fac->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 d; ad->GetDesc1(&d);
		if (d.AdapterLuid.LowPart == ps->luidLow && d.AdapterLuid.HighPart == ps->luidHigh) { pick = ad; break; }
		ad->Release();
	}
	if (!pick) { printf("x86: adapter of LUID not found\n"); return 11; }
	ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	HRESULT hr = D3D11CreateDevice(pick, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
	if (FAILED(hr)) { printf("x86: D3D11CreateDevice 0x%08lx\n", hr); return 12; }
	ID3D11Device5* dev5 = nullptr; ID3D11DeviceContext4* ctx4 = nullptr;
	if (FAILED(dev->QueryInterface(__uuidof(ID3D11Device5), (void**)&dev5))) { printf("x86: no ID3D11Device5\n"); return 13; }
	if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&ctx4))) { printf("x86: no ID3D11DeviceContext4\n"); return 14; }
	HANDLE h = (HANDLE)(uintptr_t)ps->handleInChild;
	ID3D11Fence* fence = nullptr;
	hr = dev5->OpenSharedFence(h, __uuidof(ID3D11Fence), (void**)&fence);
	if (FAILED(hr)) { printf("x86: OpenSharedFence(0x%x) 0x%08lx\n", ps->handleInChild, hr); return 15; }
	CloseHandle(h);   // vp_frame.h: the composite closes the duplicated handle after opening
	printf("x86: fence opened from handle 0x%x, completed value %llu\n", ps->handleInChild, (unsigned long long)fence->GetCompletedValue());

	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	ID3D11Query* q = nullptr;
	if (FAILED(dev->CreateQuery(&qd, &q))) return 16;
	// A little GPU work behind the wait, so the query has something to complete.
	D3D11_TEXTURE2D_DESC td = {}; td.Width = td.Height = 256; td.MipLevels = td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_RENDER_TARGET;
	ID3D11Texture2D* tex = nullptr; ID3D11RenderTargetView* rtv = nullptr;
	dev->CreateTexture2D(&td, nullptr, &tex); dev->CreateRenderTargetView(tex, nullptr, &rtv);
	hr = ctx4->Wait(fence, 1);
	const float c[4] = { 1, 0, 0, 1 };
	ctx->ClearRenderTargetView(rtv, c);
	ctx->End(q);
	ctx->Flush();
	printf("x86: Wait(fence, 1) queued (hr 0x%08lx), clear + event query behind it, flushed\n", hr);
	double t0 = NowMs();
	while (NowMs() - t0 < 300.0)
	{
		BOOL done = FALSE;
		if (ctx->GetData(q, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && done)
		{ printf("x86: query completed BEFORE the signal: the wait did not hold the GPU\n"); return 17; }
		Sleep(1);
	}
	printf("x86: 300 ms without completion: the GPU queue is held by the wait\n");
	SetEvent(waiting);
	double t1 = NowMs();
	for (;;)
	{
		BOOL done = FALSE;
		if (ctx->GetData(q, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && done) break;
		if (NowMs() - t1 > 5000.0) { printf("x86: no completion 5 s after the signal\n"); return 18; }
		YieldProcessor();
	}
	double dt = NowMs() - t1;
	UINT64 v = fence->GetCompletedValue();
	printf("x86: query completed %.3f ms after the x64 side was told to signal; completed value %llu\n", dt, (unsigned long long)v);
	return v == 1 ? 0 : 19;
}
#endif
