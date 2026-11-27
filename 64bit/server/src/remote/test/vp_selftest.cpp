// vp_selftest.cpp - a self-test of the server's remote mode without the proxy (epic 2300, task 2303).
// An x64 client built from vpipc and the generated ids plays the proxy's part: it creates the client endpoint, starts
// `server.exe --remote`, and sends kRt_Boot, SetDLLIFace(an engine handle), SetGameDatabase, optionally Init,
// GetPreGame + Destroy, GetGame + GetGameTurn + Destroy, kRt_LuaRegister, kRt_LuaCall(Game.GetGameTurn), kRt_Stats,
// kRt_Shutdown. Every engine call the server makes back (DLL -> engine) is answered "unimplemented" and listed.
//
// Usage: vp_selftest.exe <server.exe> <remote server.cfg> [<game database file> [init] [die] [unexpected]]
// die: the client exits after the Lua calls without kRt_Shutdown (the server must report and exit with code 3).
// Build: server/src/remote/test/build_selftest.py (compiles this file against a build_server.py --out tree).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include "vpipc/vpipc.h"
#include "vpipc/stl.h"
#include "vp_wire.h"
#include "vp_ids.gen.h"

namespace
{
	std::map<uint32_t, long> s_engineCalls;

	void OnCall(void*, vpipc::Channel& ch, uint32_t method, vpipc::Reader&, vpipc::Writer& ret)
	{
		if(++s_engineCalls[method] == 1)
			printf("  engine call from the server: 0x%08x %s -> answered unimplemented\n", method,
			       vpk_method_key(method) ? vpk_method_key(method) : "(runtime)");
		ch.set_return_status(vpw::kStatusUnimplemented);
		ret.str("vp_selftest: not served");
	}

	// Walks one Lua value; prints it at depth <= maxDepth; remembers the kLCFunc id of path "Game.GetGameTurn".
	uint32_t s_gameTurnFunc = 0;
	void Walk(vpipc::Reader& r, const std::string& path, int depth, bool print)
	{
		const uint32_t tag = r.u32();
		switch(tag)
		{
		case vpw::kLNil: if(print) printf("%*s%s = nil\n", depth * 2, "", path.c_str()); break;
		case vpw::kLFalse: case vpw::kLTrue: if(print) printf("%*s%s = %s\n", depth * 2, "", path.c_str(), tag == vpw::kLTrue ? "true" : "false"); break;
		case vpw::kLNumber: { const double d = r.f64(); if(print) printf("%*s%s = %g\n", depth * 2, "", path.c_str(), d); break; }
		case vpw::kLString: { const std::string s = vpipc::read_string(r); if(print) printf("%*s%s = \"%s\"\n", depth * 2, "", path.c_str(), s.c_str()); break; }
		case vpw::kLTable:
		{
			const uint32_t n = r.u32();
			if(print)
				printf("%*s%s = table, %u pairs\n", depth * 2, "", path.c_str(), n);
			for(uint32_t i = 0; i < n && r.ok(); ++i)
			{
				// keys: strings or numbers in practice
				const uint32_t kt = r.u32();
				std::string key;
				if(kt == vpw::kLString)
					key = vpipc::read_string(r);
				else if(kt == vpw::kLNumber)
				{
					char sz[32];
					sprintf_s(sz, "[%g]", r.f64());
					key = sz;
				}
				else
				{
					printf("    (key tag %u)\n", kt);
					return;
				}
				Walk(r, path + "." + key, depth + 1, false);
			}
			break;
		}
		case vpw::kLCFunc:
		{
			const uint32_t id = r.u32();
			if(path == "Game.GetGameTurn")
				s_gameTurnFunc = id;
			if(print) printf("%*s%s = C function %u\n", depth * 2, "", path.c_str(), id);
			break;
		}
		case vpw::kLSRef: case vpw::kLERef: { const uint32_t id = r.u32(), t = r.u32(); if(print) printf("%*s%s = ref %u type %u\n", depth * 2, "", path.c_str(), id, t); break; }
		case vpw::kLInstance: { const std::string n = vpipc::read_string(r); const uint64_t p = r.u64(); if(print) printf("%*s%s = instance %s %llx\n", depth * 2, "", path.c_str(), n.c_str(), (unsigned long long)p); break; }
		case vpw::kLUnsupported: { const std::string n = vpipc::read_string(r); if(print) printf("%*s%s = unsupported (%s)\n", depth * 2, "", path.c_str(), n.c_str()); break; }
		default: printf("bad tag %u at %s\n", tag, path.c_str()); break;
		}
	}

	vpipc::Channel* s_ch = NULL;
	bool Call(const char* what, vpipc::Reader& r)
	{
		const vpipc::Status st = s_ch->end_call(r);
		if(st != vpipc::Status::Ok)
		{
			vpipc::Reader m = r;
			const char* sz = st == vpipc::Status::RemoteError ? m.cstr() : NULL;
			printf("%s: %s (remote status %u) %s\n", what, vpipc::status_name(st), s_ch->last_remote_status(), sz ? sz : "");
			return false;
		}
		return true;
	}
}

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		printf("usage: vp_selftest.exe <server.exe> <remote server.cfg> [<game database> [init] [die]]\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	vpipc::EndpointConfig cfg;
	cfg.appSchemaHash = kAppSchemaHash;
	vpipc::Endpoint* ep = NULL;
	vpipc::Status st = vpipc::Endpoint::create_client(cfg, &ep);
	if(st != vpipc::Status::Ok)
	{
		printf("create_client: %s %s\n", vpipc::status_name(st), vpipc::Endpoint::last_error_text());
		return 1;
	}
	printf("client endpoint %s, schema 0x%016llx\n", ep->section_name(), (unsigned long long)kAppSchemaHash);
	char cmd[4096];
	sprintf_s(cmd, "\"%s\" --remote --section %s --config \"%s\"", argv[1], ep->section_name(), argv[2]);
	STARTUPINFOA si = {sizeof(si)};
	PROCESS_INFORMATION pi = {};
	const DWORD t0 = GetTickCount();
	if(!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		printf("CreateProcess failed: %lu\n", GetLastError());
		return 1;
	}
	printf("server pid %lu: %s\n", pi.dwProcessId, cmd);
	st = ep->wait_for_server(60000);
	printf("wait_for_server: %s after %lu ms\n", vpipc::status_name(st), GetTickCount() - t0);
	if(st != vpipc::Status::Ok)
		return 1;
	ep->bind_current_thread(vpipc::kChannelUi);
	s_ch = &ep->current_channel();
	s_ch->set_handlers(OnCall, NULL, NULL);
	vpipc::Reader r;

	// kRt_Boot
	vpipc::Writer* w = &s_ch->begin_call(vpw::kRt_Boot);
	w->u32(GetCurrentProcessId());
	w->str("");
	if(!Call("kRt_Boot", r))
		return 1;
	const uint32_t ok = r.u32();
	const std::string msg = vpipc::read_string(r);
	const vpipc::Handle ctx = r.handle();
	const uint32_t spid = r.u32();
	printf("kRt_Boot: ok %u, \"%s\", context handle type 0x%08x id %d (kind %s), server pid %u, reader ok %d left %u\n", ok,
	       msg.c_str(), ctx.type, ctx.id, vpk_iface_name(vpw::Kind(ctx.type)), spid, r.ok(), r.remaining());

	// SetDLLIFace(engine handle 1 as ICvEngineUtility4)
	vpipc::Handle eng = {vpw::MakeType(kT_ICvEngineUtility4, true), 1};
	w = &s_ch->begin_call(kM_ICvGameContext3_SetDLLIFace);
	w->handle(ctx);
	w->handle(eng);
	if(Call("SetDLLIFace", r))
		printf("SetDLLIFace: %d\n", (int)r.b());

	if(argc >= 4)
	{
		w = &s_ch->begin_call(kM_ICvGameContext3_SetGameDatabase);
		w->handle(ctx);
		w->str(argv[3]);
		if(Call("SetGameDatabase", r))
			printf("SetGameDatabase: ok %u\n", r.u32());
	}
	bool bInit = false, bDie = false, bUnexpected = false;
	for(int i = 4; i < argc; ++i)
	{
		bInit |= !strcmp(argv[i], "init");
		bDie |= !strcmp(argv[i], "die");
		bUnexpected |= !strcmp(argv[i], "unexpected");
	}
	if(bInit)
	{
		w = &s_ch->begin_call(kM_ICvGameContext3_Init);
		w->handle(ctx);
		if(Call("Init", r))
			printf("Init: returned\n");
	}

	// GetPreGame / Destroy
	w = &s_ch->begin_call(kM_ICvGameContext3_GetPreGame);
	w->handle(ctx);
	if(Call("GetPreGame", r))
	{
		const vpipc::Handle pg = r.handle();
		printf("GetPreGame: handle type 0x%08x id %d (%s)\n", pg.type, pg.id, vpk_iface_name(vpw::Kind(pg.type)));
		w = &s_ch->begin_call(kM_ICvPreGame2_Destroy);
		w->handle(pg);
		if(Call("PreGame Destroy", r))
			printf("PreGame Destroy: ok\n");
		w = &s_ch->begin_call(kM_ICvPreGame2_Destroy);
		w->handle(pg);
		Call("PreGame Destroy again (expected: bad handle)", r);
	}
	// GetGame / GetGameTurn / Destroy
	w = &s_ch->begin_call(kM_ICvGameContext3_GetGame);
	w->handle(ctx);
	if(Call("GetGame", r))
	{
		const vpipc::Handle g = r.handle();
		printf("GetGame: handle type 0x%08x id %d (%s)\n", g.type, g.id, vpk_iface_name(vpw::Kind(g.type)));
		w = &s_ch->begin_call(kM_ICvGame2_GetGameTurn);
		w->handle(g);
		if(Call("GetGameTurn", r))
			printf("GetGameTurn: %d\n", r.i32());
		w = &s_ch->begin_call(kM_ICvGame2_Destroy);
		w->handle(g);
		Call("Game Destroy", r);
	}

	// kRt_LuaRegister
	w = &s_ch->begin_call(vpw::kRt_LuaRegister);
	if(Call("kRt_LuaRegister", r))
	{
		const uint32_t n = r.u32();
		printf("kRt_LuaRegister: %u globals (%u bytes)\n", n, r.remaining());
		for(uint32_t i = 0; i < n && r.ok(); ++i)
		{
			const std::string name = vpipc::read_string(r);
			const bool bPrint = name == "Game" || name == "LuaTypes" || name == "Players" || i < 6;
			Walk(r, name, 1, bPrint);
		}
		printf("kRt_LuaRegister: reader ok %d, left %u; Game.GetGameTurn = C function %u\n", r.ok(), r.remaining(),
		       s_gameTurnFunc);
	}
	if(s_gameTurnFunc)
	{
		w = &s_ch->begin_call(vpw::kRt_LuaCall);
		w->u32(vpw::kLCFunc);
		w->u32(s_gameTurnFunc);
		w->u32(0);
		if(Call("kRt_LuaCall", r))
		{
			const uint32_t status = r.u32();
			if(status == 0)
			{
				const uint32_t nres = r.u32();
				printf("kRt_LuaCall Game.GetGameTurn(): ok, %u results\n", nres);
				for(uint32_t i = 0; i < nres; ++i)
					Walk(r, "result", 1, true);
			}
			else
				printf("kRt_LuaCall Game.GetGameTurn(): Lua error: %s\n", vpipc::read_string(r).c_str());
		}
	}
	// kRt_LuaDescribeType("Player"): member count
	w = &s_ch->begin_call(vpw::kRt_LuaDescribeType);
	w->str("Player");
	if(Call("kRt_LuaDescribeType", r))
		printf("kRt_LuaDescribeType(Player): %u members (%u bytes)\n", r.u32(), r.remaining() + 4);

	if(bUnexpected)
	{
		// A method left to a generated loud default on the server (GetHexDebugLayerNames has no hook): with
		// fail_on_unexpected=1 the server reports and exits with code 4.
		w = &s_ch->begin_call(kM_ICvGameContext3_GetHexDebugLayerNames);
		w->handle(ctx);
		Call("GetHexDebugLayerNames (no remote treatment)", r);
	}
	if(bDie)
	{
		// The engine "dies" without kRt_Shutdown: the server must write its report and exit (code 3).
		puts("die: exiting without kRt_Shutdown");
		TerminateProcess(GetCurrentProcess(), 0);
	}
	w = &s_ch->begin_call(vpw::kRt_Stats);
	if(Call("kRt_Stats", r))
	{
		const std::string t = vpipc::read_string(r);
		printf("kRt_Stats: %zu bytes; first lines:\n%.600s\n", t.size(), t.c_str());
	}
	w = &s_ch->begin_call(vpw::kRt_Shutdown);
	w->u32(0);
	if(Call("kRt_Shutdown", r))
		printf("kRt_Shutdown: report %s\n", vpipc::read_string(r).c_str());
	WaitForSingleObject(pi.hProcess, 30000);
	DWORD code = 0;
	GetExitCodeProcess(pi.hProcess, &code);
	printf("server exit code %lu; %zu distinct engine methods called back; total %lu ms\n", code, s_engineCalls.size(),
	       GetTickCount() - t0);
	ep->close();
	return 0;
}
