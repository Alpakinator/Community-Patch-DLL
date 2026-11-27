// vp_buildid.h - the build id both processes write as their first log line (issue 2704, epic 2700).
// Shared by the x86 proxy (proxy/src/proxy_buildid.cpp, VC9 headers) and the x64 server (server/src/srv_buildid.cpp).
// Include it in ONE translation unit per binary: it defines a function with internal linkage.
//
// What the line says:
//   build: repo <commit> dirty=<0|1> dll_pin <dll.lock commit> src <mode>[ <head>] config <config> built <time>;
//   image <path> sha256 <hex>
// The first part comes from vp_build_info.h, generated at build time by proxy/gen/build_info.py into the build's
// output folder (build_proxy.py and server/build_server.py put it on the include path). Without it (another build
// that compiles these sources) every field says "unknown". The SHA-256 is of the image file this module was loaded
// from (GetModuleFileNameW), computed at run time with bcrypt.dll loaded by name (no import is added; the proxy's
// import list is checked by build_proxy.py): a hash of the binary cannot be embedded in the binary it hashes.
#pragma once
#include <windows.h>
#include <stdio.h>

#if defined(__has_include)
#if __has_include("vp_build_info.h")
#include "vp_build_info.h"
#endif
#endif
#ifndef VP_BUILD_REPO_COMMIT
#define VP_BUILD_REPO_COMMIT "unknown"
#define VP_BUILD_REPO_DIRTY "unknown"
#define VP_BUILD_DLL_PIN "unknown"
#define VP_BUILD_SRC "unknown"
#define VP_BUILD_CONFIG "unknown"
#define VP_BUILD_TIME "unknown"
#endif

namespace
{
// SHA-256 of the file as 64 lowercase hex digits; "unavailable (<why>)" otherwise.
void VpSha256File(const wchar_t* path, char* out, size_t outSize)
{
	typedef LONG(WINAPI * OpenAlgFn)(void**, const wchar_t*, const wchar_t*, ULONG);
	typedef LONG(WINAPI * CreateHashFn)(void*, void**, unsigned char*, ULONG, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * HashDataFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * FinishHashFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * DestroyHashFn)(void*);
	typedef LONG(WINAPI * CloseAlgFn)(void*, ULONG);
	_snprintf_s(out, outSize, _TRUNCATE, "unavailable (bcrypt.dll)");
	HMODULE bc = LoadLibraryW(L"bcrypt.dll");
	if (!bc) return;
	OpenAlgFn openAlg = (OpenAlgFn)GetProcAddress(bc, "BCryptOpenAlgorithmProvider");
	CreateHashFn createHash = (CreateHashFn)GetProcAddress(bc, "BCryptCreateHash");
	HashDataFn hashData = (HashDataFn)GetProcAddress(bc, "BCryptHashData");
	FinishHashFn finishHash = (FinishHashFn)GetProcAddress(bc, "BCryptFinishHash");
	DestroyHashFn destroyHash = (DestroyHashFn)GetProcAddress(bc, "BCryptDestroyHash");
	CloseAlgFn closeAlg = (CloseAlgFn)GetProcAddress(bc, "BCryptCloseAlgorithmProvider");
	void* alg = NULL;
	void* hash = NULL;
	HANDLE f = INVALID_HANDLE_VALUE;
	if (!openAlg || !createHash || !hashData || !finishHash || !destroyHash || !closeAlg)
		goto done;
	if (openAlg(&alg, L"SHA256", NULL, 0) != 0 || createHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0)
		goto done;
	f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
	                FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE)
	{
		_snprintf_s(out, outSize, _TRUNCATE, "unavailable (open error %u)", (unsigned)GetLastError());
		goto done;
	}
	{
		static unsigned char buf[1 << 16];
		DWORD got = 0;
		for (;;)
		{
			if (!ReadFile(f, buf, sizeof(buf), &got, NULL))
			{
				_snprintf_s(out, outSize, _TRUNCATE, "unavailable (read error %u)", (unsigned)GetLastError());
				goto done;
			}
			if (got == 0) break;
			if (hashData(hash, buf, got, 0) != 0) goto done;
		}
		unsigned char digest[32];
		if (finishHash(hash, digest, 32, 0) != 0) goto done;
		if (outSize < 65) goto done;
		for (int i = 0; i < 32; ++i)
			_snprintf_s(out + 2 * i, outSize - 2 * i, _TRUNCATE, "%02x", digest[i]);
	}
done:
	if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
	if (hash) destroyHash(hash);
	if (alg) closeAlg(alg, 0);
	FreeLibrary(bc);
}

// The whole build-id text (no trailing newline) for the module `self` (NULL: the process's EXE).
void VpBuildIdText(HMODULE self, char* out, size_t outSize)
{
	wchar_t path[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameW(self, path, sizeof(path) / sizeof(path[0]));
	if (n == 0 || n >= sizeof(path) / sizeof(path[0])) path[0] = 0;
	char sha[96];
	if (path[0])
		VpSha256File(path, sha, sizeof(sha));
	else
		_snprintf_s(sha, sizeof(sha), _TRUNCATE, "unavailable (GetModuleFileNameW error %u)", (unsigned)GetLastError());
	char pathA[MAX_PATH * 4];
	if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, (int)sizeof(pathA), NULL, NULL)) pathA[0] = 0;
	_snprintf_s(out, outSize, _TRUNCATE,
	            "build: repo %s dirty=%s dll_pin %s src %s config %s built %s; image %s sha256 %s", VP_BUILD_REPO_COMMIT,
	            VP_BUILD_REPO_DIRTY, VP_BUILD_DLL_PIN, VP_BUILD_SRC, VP_BUILD_CONFIG, VP_BUILD_TIME,
	            pathA[0] ? pathA : "?", sha);
}
}  // namespace
