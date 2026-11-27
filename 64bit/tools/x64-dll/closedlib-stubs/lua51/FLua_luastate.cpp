// The three FLua members the probe's grouping put under lua51 (issue 1005).
//
// tools/x64-dll/evidence/2026-11-10-probe/probe-group-unresolved.py tests the lua51 rule (\blua_) before the
// FLua rule, so these FLua symbols, whose signatures mention `struct lua_State`, were counted as lua51:
//   FLua::Details::CCallWithErrorHandling(lua_State*, lua_CFunction, void*)
//   FLua::Value::Value(lua_State*, int)
//   FLua::Value::Set(lua_State*, int)
// With a real Lua 5.1.4 underneath they are implemented for real, from the header's contract
// (FireWorks/FLua/include/FLuaTypes.h, FLuaCommon.h): a Value holds a registry reference (luaL_ref in
// LUA_REGISTRYINDEX) to a stack value. They touch only Value's two data members and do not call the private
// Value::Unref(), so the FLua group (issue 1006) can define Unref, Value(), ~Value() and Push() in its own
// library without a clash. They are logged like stubs so the smoke run sees them.
//
// [I] The closed FLua code is not available; the reference scheme is inferred from Valid()
// (m_pkLuaState && m_iRef != LUA_NOREF) and GetRef() ("LuaValue owns this lua ref"). Whether the real Set()
// stores nil as LUA_REFNIL or as LUA_NOREF is unknown: here nil (and an invalid stack index) gives LUA_NOREF,
// so Valid() is false for nil, which is what FLua::Table(L, "missing") callers test with Valid().

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tchar.h>
#include <assert.h>
#include <FireWorks/FFireTypes.h>
#include <FireWorks/FAssert.h>
// FAssert expands to ASSERT_DIALOGUE/BUILTIN_TRAP, which only the DLL's PCH defines. FLua.h uses it in inline
// helpers this file does not call.
#undef FAssert
#define FAssert(expr) ((void)0)
extern "C" {	// as CvGameCoreDLL_Expansion2/Lua/CvLuaSupport.h does: Lua is C
#include <lua.h>
#include <lauxlib.h>
}
// FLua.h itself is `#error` on upstream master (#13489, dae4c612e; issue 3511); its body is unchanged since
// Release-5.4.8, so include the headers it includes (the same declarations on both).
#include <FireWorks/FLua/include/FLuaTypes.h>
#include <FireWorks/FLua/include/FLuaGlobalData.h>
#include <FireWorks/FLua/include/FLuaStaticFunctions.h>
#include <FireWorks/FLua/include/FLuaTypeExposure.h>
#include <FireWorks/FLua/include/FLuaPointerProtection.h>
#include "stublog.h"

namespace FLua
{
	namespace
	{
		void ReleaseRef(lua_State* L, int& iRef)
		{
			if (L != NULL && iRef != LUA_NOREF && iRef != LUA_REFNIL)
				luaL_unref(L, LUA_REGISTRYINDEX, iRef);
			iRef = LUA_NOREF;
		}

		int TakeRef(lua_State* L, int iStackIndex)
		{
			if (L == NULL || lua_type(L, iStackIndex) == LUA_TNONE || lua_isnil(L, iStackIndex))
				return LUA_NOREF;
			lua_pushvalue(L, iStackIndex);
			return luaL_ref(L, LUA_REGISTRYINDEX);	// pops the copy
		}
	}

	Value::Value(lua_State* L, int iStackIndex) : m_pkLuaState(NULL), m_iRef(LUA_NOREF)
	{
		STUB_CALL("lua51", "public: __cdecl FLua::Value::Value(struct lua_State *, int)");
		int iRef = TakeRef(L, iStackIndex);
		if (iRef != LUA_NOREF)
		{
			m_pkLuaState = L;
			m_iRef = iRef;
		}
	}

	void Value::Set(lua_State* L, int iStackIndex)
	{
		STUB_CALL("lua51", "public: void __cdecl FLua::Value::Set(struct lua_State *, int)");
		// Take the new reference before dropping the old one: the stack slot may be this value's own push.
		int iRef = TakeRef(L, iStackIndex);
		ReleaseRef(m_pkLuaState, m_iRef);
		m_pkLuaState = (iRef != LUA_NOREF) ? L : NULL;
		m_iRef = iRef;
	}

	namespace Details
	{
		bool CCallWithErrorHandling(lua_State* L, lua_CFunction pfn, void* pvUserData)
		{
			STUB_CALL("lua51", "bool __cdecl FLua::Details::CCallWithErrorHandling(struct lua_State *, int (__cdecl *)(struct lua_State *), void *)");
			int iResult = lua_cpcall(L, pfn, pvUserData);
			if (iResult == 0)
				return true;
			// FLuaCommon.h: "If the call fails then an error is reported with FLua::Details::Error and nothing
			// is left on the stack." lua_cpcall leaves exactly the error object.
			const char* szMsg = lua_tostring(L, -1);
			Error("%s", szMsg != NULL ? szMsg : "(error object is not a string)");
			lua_pop(L, 1);
			return false;
		}
	}
}
