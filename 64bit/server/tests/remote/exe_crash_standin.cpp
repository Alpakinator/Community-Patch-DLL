// exe_crash_standin.cpp - a stand-in EXE for ticket 0418 (server/tests/remote/test_exe_crash_report.py): it creates
// the vpipc client endpoint as the proxy inside the EXE does, starts the real `server.exe --remote`, waits for the
// attach, prints "SERVER pid <n>", waits until <go-file> exists (the test has opened the server's process by then), and
// ends itself with TerminateProcess(<hex code>), as an EXE that crashes (0xC0000005) or ends normally (0).
// With "hold" it does not end itself: it waits up to 60 s for the server's test_exe_kill to end it (exit 0x77 if not).
// With "incall" it sends kRt_Boot, SetDLLIFace and Init (as vp_selftest) and ends itself inside the first engine call
// the server's DLL makes back, so the server's CALL sees PeerDied (the path of srv_rt.cpp, live in 0418's run 1).
// With "record <run dir>" (ticket 0519) it first writes <run dir>\exe-crash.txt as the proxy's chained filter does
// (proxy_crash_filter.cpp FormatRecord's lines: an access violation in the EXE, "process: <its own pid>"), then ends
// itself with <hex code>: an EXE that Windows Error Reporting's box ended with 255 after the proxy recorded its crash.
//   exe_crash_standin.exe <server.exe> <server.cfg> <hex code> <go-file> [hold|incall|record <run dir>]
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vpipc/vpipc.h"
#include "vp_wire.h"
#include "vp_ids.gen.h"
#include "vpipc/stl.h"

namespace
{
unsigned long s_code = 0xC0000005ul;

// incall: the first engine call from the server ends this process inside the call.
void OnCall(void*, vpipc::Channel&, uint32_t method, vpipc::Reader&, vpipc::Writer&)
{
	printf("engine call 0x%08x %s: ending with 0x%08lX inside it\n", method,
	       vpk_method_key(method) ? vpk_method_key(method) : "(runtime)", s_code);
	TerminateProcess(GetCurrentProcess(), (UINT)s_code);
}
}  // namespace

int main(int argc, char** argv)
{
	if(argc < 5)
	{
		printf("usage: exe_crash_standin.exe <server.exe> <server.cfg> <hex code> <go-file> [hold]\n");
		return 2;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const unsigned long code = strtoul(argv[3], NULL, 16);
	const bool hold = argc >= 6 && !strcmp(argv[5], "hold");
	const bool incall = argc >= 6 && !strcmp(argv[5], "incall");
	const bool record = argc >= 7 && !strcmp(argv[5], "record");
	vpipc::EndpointConfig cfg;
	cfg.appSchemaHash = kAppSchemaHash;
	vpipc::Endpoint* ep = NULL;
	vpipc::Status st = vpipc::Endpoint::create_client(cfg, &ep);
	if(st != vpipc::Status::Ok)
	{
		printf("create_client: %s %s\n", vpipc::status_name(st), vpipc::Endpoint::last_error_text());
		return 2;
	}
	char cmd[4096];
	sprintf_s(cmd, "\"%s\" --remote --section %s --config \"%s\"", argv[1], ep->section_name(), argv[2]);
	STARTUPINFOA si = {sizeof(si)};
	PROCESS_INFORMATION pi = {};
	if(!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		printf("CreateProcess failed: %lu\n", GetLastError());
		return 2;
	}
	CloseHandle(pi.hThread);
	printf("SERVER pid %lu\n", pi.dwProcessId);
	st = ep->wait_for_server(60000);
	printf("wait_for_server: %s\n", vpipc::status_name(st));
	if(st != vpipc::Status::Ok)
		return 2;
	for(int i = 0; i < 3000 && GetFileAttributesA(argv[4]) == INVALID_FILE_ATTRIBUTES; ++i)
		Sleep(20);
	if(incall)
	{
		// The server's DLL calls the engine back during kRt_Boot / SetDLLIFace / Init; the first such call ends this
		// process with the code inside the call (OnCall), so the server's call sees PeerDied (srv_rt.cpp's path to
		// ReportAndExit(3), not a service thread's serve()).
		s_code = code;
		ep->bind_current_thread(vpipc::kChannelUi);
		vpipc::Channel* ch = &ep->current_channel();
		ch->set_handlers(OnCall, NULL, NULL);
		vpipc::Reader r;
		vpipc::Writer* w = &ch->begin_call(vpw::kRt_Boot);
		w->u32(GetCurrentProcessId());
		w->str("");
		vpipc::Status cs = ch->end_call(r);
		printf("kRt_Boot: %s\n", vpipc::status_name(cs));
		const uint32_t ok = r.u32();
		vpipc::read_string(r);
		const vpipc::Handle ctx = r.handle();
		printf("kRt_Boot ok %u\n", ok);
		vpipc::Handle eng = {vpw::MakeType(kT_ICvEngineUtility4, true), 1};
		w = &ch->begin_call(kM_ICvGameContext3_SetDLLIFace);
		w->handle(ctx);
		w->handle(eng);
		cs = ch->end_call(r);
		printf("SetDLLIFace: %s\n", vpipc::status_name(cs));
		w = &ch->begin_call(kM_ICvGameContext3_Init);
		w->handle(ctx);
		cs = ch->end_call(r);
		printf("Init: %s (no engine call reached: ending outside a call)\n", vpipc::status_name(cs));
		TerminateProcess(GetCurrentProcess(), (UINT)code);
	}
	if(record)
	{
		// the proxy's record (ticket 0419's format; lines end in CRLF; the server reads "code:" and "process:")
		const DWORD tid = GetCurrentThreadId();
		SYSTEMTIME t;
		GetLocalTime(&t);
		char rec[2048];
		const int n = sprintf_s(
		    rec,
		    "exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at CivilizationV_DX11.exe+0x136580 (VA 0x00536580), thread %lu "
		    "(the proxy's boot thread)\r\n"
		    "code: 0xC0000005 EXCEPTION_ACCESS_VIOLATION\r\n"
		    "where: CivilizationV_DX11.exe+0x136580\r\n"
		    "module base: 0x00E40000 (the EXE; its file's ImageBase 0x00400000)\r\n"
		    "rva: 0x00136580\r\n"
		    "va at the EXE's ImageBase: 0x00536580 (the knowledge base's address form)\r\n"
		    "access: read of 0x51B82000\r\n"
		    "thread: %lu (the proxy's boot thread)\r\n"
		    "process: %lu\r\n"
		    "time: %04u-%02u-%02u %02u:%02u:%02u.%03u (local)\r\n"
		    "flags: 0x0\r\n"
		    "written by: the proxy's chained unhandled-exception filter (ticket 0419); no code bytes, no stack and no "
		    "memory of the process (D83)\r\n",
		    tid, tid, GetCurrentProcessId(), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		char path[MAX_PATH * 2];
		sprintf_s(path, "%s\\exe-crash.txt", argv[6]);
		HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
		DWORD w = 0;
		const BOOL ok = f != INVALID_HANDLE_VALUE && n > 0 && WriteFile(f, rec, (DWORD)n, &w, NULL) && w == (DWORD)n;
		if(f != INVALID_HANDLE_VALUE)
			CloseHandle(f);
		printf("record: %s %s (pid %lu)\n", path, ok ? "written" : "NOT written", GetCurrentProcessId());
	}
	if(hold)
	{
		printf("hold: waiting for the server's test_exe_kill\n");
		Sleep(60000);
		TerminateProcess(GetCurrentProcess(), 0x77);
	}
	printf("ending with 0x%08lX\n", code);
	TerminateProcess(GetCurrentProcess(), (UINT)code);
	return 0;
}
