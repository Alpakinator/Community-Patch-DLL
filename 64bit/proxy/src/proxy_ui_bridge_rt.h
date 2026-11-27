// proxy_ui_bridge_rt.h - the proxy glue of the Lua bridge (issue 0178): proxy_ui_bridge_rt.cpp. The bridge:
// proxy_ui_bridge.h.
#pragma once

struct lua_State;

namespace ProxyUiBridgeRt
{
// proxy_lua.cpp's RegisterScriptLibraries hook, after the DLL's globals were set in L (the EXE's main thread): reads the
// switches (once), learns the main thread, sets the test table (ui_bridge_test=1).
void OnStateRegistered(lua_State* L);

// proxy_lua.cpp's FuncThunk (every Lua call of a DLL function, any thread): one load of a flag when the bridge is off.
extern volatile long g_on;
void OnLuaEntrySlow(lua_State* L);
inline void OnLuaEntry(lua_State* L)
{
	if (g_on) OnLuaEntrySlow(L);
}
// proxy_update.cpp's BeforeUpdate (the game-core thread, top of every ICvGame1::Update): heartbeat, periodic stats.
void OnUpdate();
// Issue 4754, for the unit option (4759; any thread, best before the EXE's first RegisterScriptLibraries): the bridge
// installs its position listeners and the drain (UiBridge::ConfigurePositions), without ui_bridge=1's other parts.
// Called after the first registration it takes effect at once when that registration ran on the ui thread. Logged once.
// TEST ONLY equivalent: VPPROXY_TEST_UI_POSITIONS=1 in the EXE's environment.
void RequestUnitPositions();
// Ticket 0199 part 2, for the arc option ([Renderer] ServerArc; any thread, best before the EXE's first
// RegisterScriptLibraries): the bridge wraps the InGame context's Events.SpawnArrowEvent and listens to
// RemoveAllArrowsEvent (UiBridge::ConfigureArrows), without ui_bridge=1's other parts. Logged once.
void RequestArrows();
// Ticket 0300 part 2, for the path option ([Renderer] ServerPath; any thread, best before the EXE's first
// RegisterScriptLibraries): the bridge listens to the InGame context's UIPathFinderUpdate and DisplayMovementIndicator
// (UiBridge::ConfigurePath) with the EXE's DLL tests as its tests hook, without ui_bridge=1's other parts. Logged once.
void RequestPath();
}  // namespace ProxyUiBridgeRt
