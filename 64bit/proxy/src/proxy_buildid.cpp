// proxy_buildid.cpp - the proxy's build id (issue 2704): the first line of vpproxy.log (proxy_rt.cpp OpenLog).
// The shared code and the line's format: proxy/include/vp_buildid.h.
#include "vp_buildid.h"

// The build-id text of the module `self` (the proxy's own HMODULE).
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize)
{
	VpBuildIdText(self, out, outSize);
}
