// srv_buildid.cpp - the server's build id (issue 2704): the first line of server.log (srv_main.cpp, after the log opens).
// The shared code and the line's format: proxy/include/vp_buildid.h (included by relative path; the server's own
// sources do not have proxy/include on their include path).
#include "../../proxy/include/vp_buildid.h"

// The build-id text of server.exe (its own image).
void SrvBuildIdText(char* out, size_t outSize)
{
	VpBuildIdText(NULL, out, outSize);
}
