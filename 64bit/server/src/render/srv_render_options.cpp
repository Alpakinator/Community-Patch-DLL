// srv_render_options.cpp - the renderer's view of the patch's options file (issue 3308): [Renderer]
// BakedTileCameraRadius from SrvOptions() (server/src/srv_options.h, one-off 9042), which the server read once at its
// start from server.cfg options_file= (the proxy writes that line at every mode=on boot).
//
// Issue 3312: options/include is on the render units' include path (build_server.py RENDER_INCLUDES,
// build_render_tests.py INCLUDES), so the server's own header is included by name.
// No DLL header: srv_options.h and options/ include nothing of the fork.
//
// Ticket 0674: the render_config key render_fow_mask_window (srv_render_host.h), parsed here for ParseHostConfig.
#include "srv_residency_source.h"
#include "srv_render_host.h"
#include "srv_options.h"

namespace srvrender {

int RenderOptionBakedTileCameraRadius() { return SrvOptions().BakedTileCameraRadius(); }

namespace {
int g_fowMaskWindow = kFowMaskWindowCamera;   // ticket 0674: render_fow_mask_window as ParseHostConfig read it last
}

bool ParseRenderFowMaskWindow(const std::string& value, std::string& why)
{
	if(value.empty() || value == "camera")
	{
		g_fowMaskWindow = kFowMaskWindowCamera;
		return true;
	}
	if(value == "pair")
	{
		g_fowMaskWindow = kFowMaskWindowPair;
		return true;
	}
	why = "render_fow_mask_window=" + value + ": camera or pair";
	return false;
}

int RenderFowMaskWindowOption() { return g_fowMaskWindow; }

const char* RenderFowMaskWindowName(int option) { return option == kFowMaskWindowPair ? "pair" : "camera"; }

// Ticket 0793: render_fow_overlay_history (declared in srv_render_scene.h, repeated here so that this unit does not take
// the scene's header and its include paths into every builder that compiles it)
bool ParseRenderFowOverlayHistory(const std::string& value, std::string& why);
bool RenderFowOverlayHistoryOption();
const char* RenderFowOverlayHistoryName(bool on);
namespace {
bool g_fowOverlayHistory = true;
}

bool ParseRenderFowOverlayHistory(const std::string& value, std::string& why)
{
	if(value.empty() || value == "1")
	{
		g_fowOverlayHistory = true;
		return true;
	}
	if(value == "0")
	{
		g_fowOverlayHistory = false;
		return true;
	}
	why = "render_fow_overlay_history=" + value + ": 1 or 0";
	return false;
}

bool RenderFowOverlayHistoryOption() { return g_fowOverlayHistory; }

const char* RenderFowOverlayHistoryName(bool on) { return on ? "1" : "0"; }

}  // namespace srvrender
