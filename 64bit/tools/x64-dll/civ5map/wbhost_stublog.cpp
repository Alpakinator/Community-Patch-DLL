// wbhost_stublog.cpp - the stub logger for the x86 build of wbdump with this repository's reader (ticket 0343).
// closedlib-stubs/common/stublog.cpp registers its exit flush under the x64 symbol name only; the x86 test needs just
// StubLog_Hit (the writer's stub calls it): each first call is printed to stderr.
#include "stublog.h"
#include <stdio.h>
#include <windows.h>

void StubLog_Hit(StubLogSite* site)
{
	if(InterlockedIncrement(&site->calls) == 1)
		fprintf(stderr, "STUB %s %s\n", site->group, site->signature);
}

void StubLog_Flush() {}

const char* StubLog_Path() { return "(stderr)"; }
