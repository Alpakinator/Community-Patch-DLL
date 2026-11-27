// render/tests/unitanim/test_trigger_record.cpp - ticket 4721: shell/common/vp_trigger.h on its own: the layout (every
// offset again at run time, so an x86 build of this file checks the x86 shell's view as well as the static_asserts),
// the ring's rules R1-R6 single-threaded, and a writer and a reader thread on one section (no torn record, every
// record read or counted lost). Self-contained (no c5r, no test kit) on purpose: the same file compiles as an x86
// program with the shell's compiler (unit-combat-server.md section 7.6 has the command).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "vp_trigger.h"

namespace {

int g_failures = 0;
int g_checks = 0;
#define TCHECK(c) do { ++g_checks; if (!(c)) { ++g_failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Section {
	std::vector<uint64_t> mem;
	Section() : mem(VPTRIG_SECTION_BYTES / 8 + 1, 0) {}
	void* p() { return mem.data(); }
};

VPTrigRecord rec(uint8_t kind, int64_t frame, uint32_t handle)
{
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.version = VPTRIG_VERSION;
	r.kind = kind;
	r.frameId = frame;
	r.handle = handle;
	r.owner = 1000;
	r.player = 0;
	r.unit = 1;
	r.figure = -1;
	r.trigger = -1;
	if (kind == VPTRIG_OWNER_END)
	{
		r.handle = 0;
		r.flags = VPTRIG_FLAG_PLAYED;
	}
	return r;
}

// the offset of a field measured on an object at run time (not offsetof: a constant would only repeat the asserts)
size_t g_sink = 0;
template <class T>
size_t at(const T& o, const volatile void* field)
{
	size_t d = (size_t)((const volatile char*)field - (const volatile char*)&o);
	g_sink += d;
	return d;
}
size_t value(size_t v)
{
	g_sink += v;
	return v;
}

void layout()
{
	VPTrigRecord r = {};
	VPTrigServer s = {};
	VPTrigReader rd = {};
	VPTrigName n = {};
	TCHECK(value(sizeof(r)) == 128);
	TCHECK(value(sizeof(n)) == 128);
	TCHECK(value(sizeof(s)) == 128);
	TCHECK(value(sizeof(rd)) == 128);
	TCHECK(at(r, &r.seq) == 0);
	TCHECK(at(r, &r.frameId) == 8);
	TCHECK(at(r, &r.version) == 16);
	TCHECK(at(r, &r.kind) == 18);
	TCHECK(at(r, &r.reason) == 19);
	TCHECK(at(r, &r.flags) == 20);
	TCHECK(at(r, &r.owner) == 24);
	TCHECK(at(r, &r.fightSeq) == 28);
	TCHECK(at(r, &r.player) == 32);
	TCHECK(at(r, &r.unit) == 36);
	TCHECK(at(r, &r.figure) == 40);
	TCHECK(at(r, &r.handle) == 44);
	TCHECK(at(r, &r.instance) == 48);
	TCHECK(at(r, &r.name) == 56);
	TCHECK(at(r, &r.file) == 60);
	TCHECK(at(r, &r.pos) == 64);
	TCHECK(at(r, &r.scale) == 76);
	TCHECK(at(r, &r.orient) == 80);
	TCHECK(at(r, &r.vel) == 96);
	TCHECK(at(r, &r.ec) == 108);
	TCHECK(at(r, &r.trigger) == 112);
	TCHECK(at(r, &r.clipTime) == 116);
	TCHECK(at(r, &r.late) == 120);
	TCHECK(at(r, &r.reserved0) == 124);
	TCHECK(at(n, &n.text) == 8);
	TCHECK(at(s, &s.generation) == 24);
	TCHECK(at(s, &s.writeSeq) == 32);
	TCHECK(at(s, &s.nameCount) == 40);
	TCHECK(at(s, &s.droppedFull) == 48);
	TCHECK(at(s, &s.droppedLate) == 56);
	TCHECK(at(s, &s.namesRefused) == 64);
	TCHECK(at(rd, &rd.lostTotal) == 8);
	TCHECK(at(rd, &rd.readerPid) == 32);
	TCHECK(at(rd, &rd.generationSeen) == 36);
	TCHECK(value(VPTRIG_RECORDS_OFFSET) == 524544u);
	TCHECK(value(VPTRIG_SECTION_BYTES) == 1048832u);
	printf("layout (%u-bit): record %u B, name %u B, server %u B, reader %u B, records at %u, section %u B\n",
	       (unsigned)(sizeof(void*) * 8), (unsigned)sizeof(VPTrigRecord), (unsigned)sizeof(VPTrigName),
	       (unsigned)sizeof(VPTrigServer), (unsigned)sizeof(VPTrigReader), (unsigned)VPTRIG_RECORDS_OFFSET,
	       (unsigned)VPTRIG_SECTION_BYTES);
}

void namesAndGenerations()
{
	Section s;
	VPTrigCursor c = {0, 0, 0};
	VPTrigRecord out;
	// no server yet: nothing
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NONE);
	VPTrigInit(s.p(), 1, 4242);
	TCHECK(VPTRIG_SERVER(s.p())->magic == VPTRIG_MAGIC);
	TCHECK(VPTRIG_SERVER(s.p())->capacity == VPTRIG_CAPACITY);
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NEWGEN);
	TCHECK(c.generation == 1 && c.next == 1);
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NONE);
	// R5
	TCHECK(VPTrigNameAt(s.p(), 1) == NULL);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, "TEST_A") == 1);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_EFFECT, "TEST_B") == 2);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_NONE, "TEST_C") == 0);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_KIND_COUNT, "TEST_C") == 0);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, "") == 0);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, NULL) == 0);
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, "TAB\tNAME") == 0);
	char longName[VPTRIG_NAME_TEXT + 1];
	memset(longName, 'A', sizeof(longName));
	longName[VPTRIG_NAME_TEXT] = 0;
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, longName) == 0);           // 120 characters
	longName[VPTRIG_NAME_TEXT - 1] = 0;
	TCHECK(VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, longName) == 3);           // 119 characters
	TCHECK(VPTRIG_SERVER(s.p())->namesRefused == 6);
	const VPTrigName* n = VPTrigNameAt(s.p(), 2);
	TCHECK(n && n->kind == VPTRIG_NAME_EFFECT && n->length == 6 && strcmp(n->text, "TEST_B") == 0);
	n = VPTrigNameAt(s.p(), 3);
	TCHECK(n && n->length == 119 && n->text[119] == 0);
	TCHECK(VPTrigNameAt(s.p(), 0) == NULL);
	TCHECK(VPTrigNameAt(s.p(), 4) == NULL);
	// the table fills at VPTRIG_NAME_CAPACITY - 1 names
	uint32_t last = 0;
	for (uint32_t i = 4; i < VPTRIG_NAME_CAPACITY + 3; ++i)
	{
		char b[32];
		snprintf(b, sizeof(b), "N%u", i);
		uint32_t got = VPTrigAddName(s.p(), VPTRIG_NAME_SOUND3D, b);
		if (got) last = got;
	}
	TCHECK(last == VPTRIG_NAME_CAPACITY - 1);
	TCHECK(VPTRIG_SERVER(s.p())->nameCount == VPTRIG_NAME_CAPACITY - 1);

	// records, then a new generation (R6): the cursor restarts, old records are not read
	TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, 5, 1)));
	TCHECK(VPTrigReadNext(s.p(), c, 5, out) == VPTRIG_READ_OK);
	TCHECK(out.seq == 1 && out.frameId == 5);
	TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, 6, 2)));
	VPTrigInit(s.p(), 2, 4243);
	TCHECK(VPTRIG_SERVER(s.p())->nameCount == 0 && VPTRIG_SERVER(s.p())->writeSeq == 0);
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NEWGEN);
	TCHECK(c.generation == 2 && c.next == 1);
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NONE);
	// an orderly stop reads as a new generation 0 (end every owner), then nothing
	VPTrigStop(s.p());
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NEWGEN);
	TCHECK(c.generation == 0);
	TCHECK(VPTrigReadNext(s.p(), c, 1000, out) == VPTRIG_READ_NONE);
}

void frameRule()
{
	Section s;
	VPTrigInit(s.p(), 1, 1);
	VPTrigCursor c = {0, 0, 0};
	VPTrigRecord out;
	TCHECK(VPTrigReadNext(s.p(), c, 0, out) == VPTRIG_READ_NEWGEN);
	// frames 10, 10, 11, 13 (R1)
	TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, 10, 1)));
	TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_EFFECT_START, 10, 2)));
	TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_SOUND_STOP, 11, 1)));
	TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_OWNER_END, 13, 0)));
	// R2: the insertion of frame 9 executes nothing; of 10 the first two; a miss that shows 12 executes 11's
	TCHECK(VPTrigReadNext(s.p(), c, 9, out) == VPTRIG_READ_NONE);
	TCHECK(VPTrigReadNext(s.p(), c, 10, out) == VPTRIG_READ_OK && out.seq == 1 && out.kind == VPTRIG_SOUND_START);
	TCHECK(VPTrigReadNext(s.p(), c, 10, out) == VPTRIG_READ_OK && out.seq == 2 && out.kind == VPTRIG_EFFECT_START);
	TCHECK(VPTrigReadNext(s.p(), c, 10, out) == VPTRIG_READ_NONE);
	TCHECK(VPTrigReadNext(s.p(), c, 12, out) == VPTRIG_READ_OK && out.seq == 3 && out.frameId == 11);
	TCHECK(VPTrigReadNext(s.p(), c, 12, out) == VPTRIG_READ_NONE);
	TCHECK(VPTrigReadNext(s.p(), c, 13, out) == VPTRIG_READ_OK && out.seq == 4 && VPTrigIsFightEnd(out));
	TCHECK((out.flags & VPTRIG_FLAG_PLAYED) != 0);
	TCHECK(VPTrigReadNext(s.p(), c, 100, out) == VPTRIG_READ_NONE);
	VPTrigPublishReader(s.p(), c, 77);
	TCHECK(VPTRIG_READER(s.p())->readSeq == 4 && VPTRIG_READER(s.p())->readerPid == 77);
	TCHECK(VPTRIG_READER(s.p())->generationSeen == 1);
}

void overrunAndReserve()
{
	// no reader attached: the server overwrites; the reader finds the loss and counts it exactly (R3)
	{
		Section s;
		VPTrigInit(s.p(), 1, 1);
		VPTrigCursor c = {0, 0, 0};
		VPTrigRecord out;
		TCHECK(VPTrigReadNext(s.p(), c, 0, out) == VPTRIG_READ_NEWGEN);
		const uint32_t total = VPTRIG_CAPACITY + 100;
		for (uint32_t i = 1; i <= total; ++i) TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, i, i)));
		TCHECK(VPTrigReadNext(s.p(), c, 1u << 30, out) == VPTRIG_READ_LOST);
		TCHECK(c.lost == 100 && c.next == 101);
		uint32_t got = 0;
		int64_t expect = 101;
		for (;;)
		{
			VPTrigReadResult r = VPTrigReadNext(s.p(), c, 1u << 30, out);
			if (r != VPTRIG_READ_OK) { TCHECK(r == VPTRIG_READ_NONE); break; }
			TCHECK((int64_t)out.seq == expect && out.frameId == expect && out.handle == (uint32_t)expect);
			++expect;
			++got;
		}
		TCHECK(got == VPTRIG_CAPACITY);
	}
	// a reader attached and behind: everything but OwnerEnd is dropped once fewer than VPTRIG_RESERVE are free
	{
		Section s;
		VPTrigInit(s.p(), 1, 1);
		VPTrigCursor c = {0, 0, 0};
		VPTrigRecord out;
		TCHECK(VPTrigReadNext(s.p(), c, 0, out) == VPTRIG_READ_NEWGEN);
		VPTrigPublishReader(s.p(), c, 99);   // attached, read nothing
		uint32_t written = 0, dropped = 0;
		for (uint32_t i = 1; i <= VPTRIG_CAPACITY; ++i)
		{
			if (VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, 1, i))) ++written;
			else ++dropped;
		}
		TCHECK(written == VPTRIG_CAPACITY - VPTRIG_RESERVE + 1);
		TCHECK(dropped == VPTRIG_RESERVE - 1);
		TCHECK(VPTRIG_SERVER(s.p())->droppedFull == (int64_t)(VPTRIG_RESERVE - 1));
		TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_OWNER_END, 1, 0)));   // OwnerEnd always written
		// the reader catches up: no loss, the OwnerEnd last
		int64_t n = 0;
		while (VPTrigReadNext(s.p(), c, 1, out) == VPTRIG_READ_OK) ++n;
		TCHECK(n == (int64_t)written + 1 && out.kind == VPTRIG_OWNER_END && c.lost == 0);
		VPTrigPublishReader(s.p(), c, 99);
		TCHECK(VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, 2, 1)));   // room again
		// a reader block of another generation does not hold the server back
		VPTRIG_READER(s.p())->generationSeen = 7;
		VPTRIG_READER(s.p())->readSeq = 0;
		uint32_t refused = 0;
		for (uint32_t i = 0; i < VPTRIG_CAPACITY; ++i)
			if (!VPTrigWrite(s.p(), rec(VPTRIG_SOUND_START, 3, 1))) ++refused;
		TCHECK(refused == 0);
	}
}

// One writer thread, one reader thread on one section, records whose fields are derived from their seq: a torn copy
// would show as a mismatch. The reader is slowed now and then so that the ring overruns and R3's loss path runs.
void threads()
{
	Section s;
	VPTrigInit(s.p(), 1, 1);
	const int64_t total = 300000;
	std::atomic<bool> done(false);
	int64_t okCount = 0, mismatches = 0, lostCount = 0, lostEvents = 0;
	std::thread reader([&]() {
		VPTrigCursor c = {0, 0, 0};
		VPTrigRecord out;
		int64_t last = 0;
		unsigned spin = 0;
		for (;;)
		{
			bool finished = done.load();
			VPTrigReadResult r = VPTrigReadNext(s.p(), c, INT64_MAX, out);
			if (r == VPTRIG_READ_OK)
			{
				int64_t q = (int64_t)out.seq;
				bool good = q > last && out.frameId == q / 3 && out.handle == (uint32_t)(q * 7 + 1) &&
				            out.instance == (uint64_t)q * 0x9E3779B97F4A7C15ull && out.pos[0] == (float)(q % 1000) &&
				            out.reserved0 == (uint32_t)(q ^ 0x5A5A5A5A) && out.version == VPTRIG_VERSION;
				if (!good) ++mismatches;
				last = q;
				++okCount;
				if ((++spin & 0xFFFF) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(30));   // overrun
			}
			else if (r == VPTRIG_READ_LOST)
				++lostEvents;
			else if (r == VPTRIG_READ_NONE && finished)
				break;
		}
		lostCount = c.lost;
	});
	for (int64_t q = 1; q <= total; ++q)
	{
		VPTrigRecord r = rec(VPTRIG_SOUND_START, q / 3, (uint32_t)(q * 7 + 1));
		r.instance = (uint64_t)q * 0x9E3779B97F4A7C15ull;
		r.pos[0] = (float)(q % 1000);
		r.reserved0 = (uint32_t)(q ^ 0x5A5A5A5A);
		VPTrigWrite(s.p(), r);
		if ((q & 0xFFF) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // let the reader keep up mostly
	}
	done.store(true);
	reader.join();
	printf("threads: %lld written, %lld read, %lld lost (%lld loss events), %lld mismatches\n", (long long)total,
	       (long long)okCount, (long long)lostCount, (long long)lostEvents, (long long)mismatches);
	TCHECK(mismatches == 0);
	TCHECK(okCount + lostCount == total);
	TCHECK(okCount > 0);
	TCHECK(lostEvents > 0);   // the ring overran at least once (the reader's 30 ms pause), so R3's loss path ran
}

}  // namespace

int main()
{
	layout();
	namesAndGenerations();
	frameRule();
	overrunAndReserve();
	threads();
	printf("test_trigger_record: %d checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
