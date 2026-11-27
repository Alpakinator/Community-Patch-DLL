// render/tests/unitanim/vp_trigger_vc9_probe.cpp - ticket 4721: a compile-only probe that shell/common/vp_trigger.h
// builds in the proxy's x86 environment (clang-cl -m32 against the VC9 headers, proxy/build_proxy.py), where the shell
// sound executor of ticket 4723 may live. Not a ctest (no test_ prefix); unit-combat-server.md section 5.6 has the
// command. It uses every helper once so that each is compiled.
#include "vp_trigger.h"

extern "C" int vp_trigger_vc9_probe(void* section, long long maxFrame)
{
	VPTrigInit(section, 1, 2);
	uint32_t name = VPTrigAddName(section, VPTRIG_NAME_SOUND3D, "PROBE");
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.version = VPTRIG_VERSION;
	r.kind = VPTRIG_SOUND_START;
	r.name = name;
	r.handle = 1;
	VPTrigWrite(section, r);
	VPTrigCursor c = {0, 0, 0};
	VPTrigRecord out;
	int n = 0;
	while (VPTrigReadNext(section, c, maxFrame, out) != VPTRIG_READ_NONE) ++n;
	VPTrigPublishReader(section, c, 3);
	const VPTrigName* nm = VPTrigNameAt(section, name);
	bool played = VPTrigPlayedFor(VPTRIG_END_LIMIT) && !VPTrigIsFightEnd(out);
	VPTrigStop(section);
	return n + (nm ? 1 : 0) + (played ? 1 : 0);
}
