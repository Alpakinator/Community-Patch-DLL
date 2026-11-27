// FLua (FLuaWin32.lib): real replacements for the five members in the FLua group of the census.
// x86 evidence: FLuaWin32.lib members FLuaTypes.obj and FLuaCommon.obj, disassembled 2026-11-10.
//
// FLua::Value is {lua_State* m_pkLuaState; int m_iRef} holding a reference in the Lua registry
// (LUA_REGISTRYINDEX = -10000, LUA_NOREF = -2). These functions call stock Lua 5.1 API functions
// (luaL_unref, lua_checkstack, lua_rawgeti, lua_type, lua_settop), which issue 1005's lua51.lib
// provides; luaL_unref is not among the 336 census symbols because only FLua uses it.
//
// Three more FLuaWin32.lib members sit in the census's lua51 group (issue 1005):
// Value::Value(lua_State*, int) and Value::Set(lua_State*, int) (lua_checkstack, lua_pushvalue,
// luaL_ref into the registry; Set first unrefs) and Details::CCallWithErrorHandling (lua_cpcall
// under the FLua::ms_CriticalSection lock, HandleCPCallError + lua_settop(L, -2) on failure).
#include "../FireWorks/fw_prelude.h"
extern "C"
{
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
// FLua.h itself is `#error` on upstream master (#13489, dae4c612e; issue 3511); its body is unchanged since
// Release-5.4.8, so include the headers it includes (the same declarations on both).
#include <FireWorks/FLua/include/FLuaTypes.h>
#include <FireWorks/FLua/include/FLuaGlobalData.h>
#include <FireWorks/FLua/include/FLuaStaticFunctions.h>
#include <FireWorks/FLua/include/FLuaTypeExposure.h>
#include <FireWorks/FLua/include/FLuaPointerProtection.h>
#include <stdarg.h>

namespace FLua
{
	Value::Value()
		: m_pkLuaState(NULL)
		, m_iRef(LUA_NOREF)
	{
	}

	Value::~Value()
	{
		Unref();
	}

	void Value::Unref()
	{
		if(m_pkLuaState != NULL && m_iRef != LUA_NOREF)
			luaL_unref(m_pkLuaState, LUA_REGISTRYINDEX, m_iRef);
		m_pkLuaState = NULL;
		m_iRef = LUA_NOREF;
	}

	bool Value::Push() const
	{
		if(m_pkLuaState == NULL || m_iRef == LUA_NOREF)
			return false;
		lua_checkstack(m_pkLuaState, 1);
		lua_rawgeti(m_pkLuaState, LUA_REGISTRYINDEX, m_iRef);
		return true;
	}

	bool Table::Push() const
	{
		// Pushes the referenced value; succeeds only if it is a table, otherwise pops it again.
		lua_State* L = m_kLuaVal.GetLuaState();
		if(L == NULL || m_kLuaVal.GetRef() == LUA_NOREF)
			return false;
		lua_checkstack(L, 1);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_kLuaVal.GetRef());
		if(lua_type(L, -1) == LUA_TTABLE)
			return true;
		lua_settop(L, -2);
		return false;
	}

	void ErrorHandler::operator()(const TCHAR* szError) const
	{
		if(m_pkClass != NULL)
			(m_pkClass->*m_pfnFunc)(szError);
		else
			Details::UnsafeCast<ErrorHandlerStaticFunc>(m_pfnFunc)(szError);
	}

	namespace Details
	{
		void Error(const TCHAR* szFormat, ...)
		{
			// x86: formats into a 1024-byte stack buffer with vsprintf_s, then calls the current
			// error handler (default: fprintf_s(stderr, "%s\n")).
			char szBuffer[0x400];
			va_list vaArgs;
			va_start(vaArgs, szFormat);
			vsprintf_s(szBuffer, sizeof(szBuffer), szFormat, vaArgs);
			va_end(vaArgs);
			ErrorHandlerHolder::Get()(szBuffer);
		}
	}
}
