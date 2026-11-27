// srv_remote_savevariant.cpp - ticket 0354: a save of the other DLL variant (22 / 43 major civilizations) is refused
// before the DLL reads a byte of its game section. Knowledge base: research/03-dll-engine-boundary/proxy-design.md
// section 30, player-limits.md 5.3 and 8.3.
//
// Why: the two variants' saves are incompatible both ways (191 fixed-length arrays of MAX_MAJOR_CIVS elements written
// without a count, player-limits.md 5.1), and the DLL's CvGame::Read only logs "Potential savefile format mismatch".
// Through the proxy a cross load ended in the EXE's CRT abort box (0334's 4254) or, since 0359, in D118's report after
// the misread had already run; a silent wrong load is what D118/D125 forbid.
//
// Where: ICvGame1::Read is the first engine -> DLL read of the save body (0359's live load: one pull session per Read,
// ICvGame1 first); the server dispatches it as ICvGame2_Read (the proxy hands out ICvGame2 objects). The body begins
// with what CvGame::Read reads first (CvGame.cpp:10986-11020 at 04b1211b8): u32 save version, the 16-byte game data
// hash, CURRENT_GAMECORE_VERSION (u32 length + bytes, at offset 0x14; tools/d10/savecmp.py VERSION_OFF).
//
// How the save's major count is found, in this order:
// 1. The mark. Our build scripts stamp CURRENT_GAMECORE_VERSION with "+m<N>" after the describe for another count than
//    22 (tools/x64-dll/build.py version_string; "Release-5.4.8-51-g04b1211b8+m43 Copy", "...+ia<n>+m43 Copy"). A
//    "+m<N>" token of the describe part (the text before the first space, tokens separated by '+') gives N; it costs
//    no read beyond the string. A marked save of another count is always refused (MC #1943).
// 2. The layout, for a save without the mark (every save of upstream's builds, its 43 build included, whose string
//    says "Dirty", and our 22 builds'). CvGame::Serialize writes no member sized by the major count until its last
//    classes (player-limits.md 5.1, 5.3), so the count is read from the game section's tail, which the game section's
//    own end anchors (CvTradeClasses.cpp:2401-2408, CvCorporationClasses.cpp:1950, CvContractClasses.cpp:837,
//    CvGame.cpp:10980-10983, :11110-11114):
//      CvGameTrade:  m_routesPerPlayer   u32 64 (MAX_PLAYERS), 64 x (u32 k, k x i32)
//                    m_aaiTechDifference, m_aaiPolicyDifference   2 x N x N x i32, no count   <- N
//                    m_iNextID           i32
//      CvGameCorporations: u32 c, c x 20 bytes;  CvGameContracts: u32 a, a x 20 bytes, u32 i, i x 20 bytes
//      m_strGameId:  u32 L, L bytes;  then the embedded save database: u32 size, the SQLite file ("SQLite format 3\0").
//    The hook reads the body up to the database's magic (bytes CvGame::Read reads anyway: the database is the last
//    thing it reads), then looks for every position p and count N (2..62) for which that chain ends exactly at the
//    database's size field. Exactly one N: the save's count. None or several: "unknown", no refusal (left to 0359's
//    line). Checked on 20 saves of 10 builds, 22 and 43 (proxy-design.md 30): exactly one fit each, the right one.
//    The read goes in kScanChunk pieces and stops at the magic, so it never runs past the game section of a save
//    whose database holds at least kScanChunk + 15 bytes (every save examined: 3,072 B or more; one SQLite page is
//    1,024 B here); a save with an empty database (never seen) would be read on until the engine's stream ends.
//
// Then: a count other than SrvMajorCivs() (the running DLL's Defines.MAX_MAJOR_CIVS, issue 4253) is refused: a server
// report line names the save's version string, its count and how it was found, then SrvRemote::ReportAndExit(
// kExitBase | save << 8 | running, why). The proxy's failed call reads that exit code (proxy_rt.cpp CallFailed) and
// writes D118's report with the stage "save-variant" and a summary naming both variants; vp64helper's popup gives the
// stage its own first line. Otherwise the stream is put back at its start and the DLL reads it as before (a pull
// session keeps every pulled byte, so nothing is pulled twice and the engine stream ends where the DLL's reads end).
//
// A body too short for the version string ends as it would without the check (the engine's stream throws in
// kRtE_StreamRead, exe_exception, 0359's path); a string longer than kMaxVersion or with a byte outside printable ASCII
// is "no mark readable": no refusal. If SrvMajorCivs() is unknown (0: srv_remote_db.cpp's boot guard has not run)
// nothing is refused (a LOUD line); VP_SERVER_TEST_MAJOR_CIVS=<n> stands in for it then, for the offline tests only
// (hutest's fake DLL has no database), never over a count that was read.
#include "srv_remote.h"
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include "srv_exe_crash.h"
#include "srv_remote_savemods.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>

// BEGIN 0354 SAVE VARIANT LOGIC (research/03-dll-engine-boundary/evidence/0354/variant_test.py compiles this block on
// its own against real save bodies: keep it self-contained, standard C++ only)
namespace savevariant
{
const unsigned int kVersionOff = 0x14;   // u32 save version + 16-byte game data hash
const unsigned int kMaxVersion = 512;    // longer: not a version string (ours are about 40 bytes)
const unsigned int kScanChunk = 1024;    // read size while looking for the database's magic
const int kMinMajors = 2, kMaxMajors = 62;
const char kDbMagic[16] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};
const unsigned int kRoutesPlayers = 64;  // m_routesPerPlayer's size (MAX_PLAYERS)
const unsigned int kMaxRoutes = 4096;    // per player, per list: a larger count is not this structure
const unsigned int kMaxGameId = 256;

inline unsigned int U32(const unsigned char* b) { return (unsigned)b[0] | (unsigned)b[1] << 8 | (unsigned)b[2] << 16 | (unsigned)b[3] << 24; }

// N of a "+m<N>" token (1-2 digits, 2..62) of the describe part of a CURRENT_GAMECORE_VERSION; 0 without one.
inline int MarkMajors(const std::string& v)
{
	const std::string describe = v.substr(0, v.find(' '));
	size_t pos = describe.find('+');
	while(pos != std::string::npos)
	{
		const size_t next = describe.find('+', pos + 1);
		const std::string tok = describe.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1);
		if((tok.size() == 2 || tok.size() == 3) && tok[0] == 'm' && tok[1] >= '0' && tok[1] <= '9' &&
		   (tok.size() == 2 || (tok[2] >= '0' && tok[2] <= '9')))
		{
			const int n = atoi(tok.c_str() + 1);
			if(n >= kMinMajors && n <= kMaxMajors)
				return n;
		}
		pos = next;
	}
	return 0;
}

// The position of the database's magic in b[from, n), or -1.
inline long FindMagic(const unsigned char* b, size_t n, size_t from)
{
	for(size_t p = from; p + sizeof(kDbMagic) <= n; ++p)
		if(b[p] == 'S' && memcmp(b + p, kDbMagic, sizeof(kDbMagic)) == 0)
			return (long)p;
	return -1;
}

// The game section's major count from its tail (see the file comment): b holds the body from its start; magic is the
// position of the database's magic. Returns N when exactly one count fits, 0 when none does, -1 when several do.
// *pAnchor: the position of m_routesPerPlayer of the (first) fit.
inline int LayoutMajors(const unsigned char* b, size_t magic, size_t from, size_t* pAnchor)
{
	if(magic < from + 8)
		return 0;
	const size_t sizePos = magic - 4;  // u32 database size
	int found = 0;
	bool several = false;
	for(size_t p = from; p + 4 + 4 * kRoutesPlayers <= sizePos; ++p)
	{
		if(U32(b + p) != kRoutesPlayers)
			continue;
		size_t q = p + 4;
		bool ok = true;
		for(unsigned int i = 0; i < kRoutesPlayers && ok; ++i)
		{
			if(q + 4 > sizePos)
			{
				ok = false;
				break;
			}
			const unsigned int k = U32(b + q);
			ok = k <= kMaxRoutes && q + 4 + 4ull * k <= sizePos;
			q += 4 + 4 * (size_t)k;
		}
		if(!ok)
			continue;
		for(int n = kMinMajors; n <= kMaxMajors; ++n)
		{
			size_t r = q + 8 * (size_t)n * n + 4;  // the two arrays, m_iNextID
			bool fit = true;
			for(int list = 0; list < 3 && fit; ++list)  // corporations, active and inactive contracts
			{
				if(r + 4 > sizePos)
				{
					fit = false;
					break;
				}
				const unsigned int c = U32(b + r);
				fit = c <= kMaxRoutes;
				r += 4 + 20 * (size_t)c;
			}
			if(!fit || r + 4 > sizePos)
				continue;
			const unsigned int l = U32(b + r);
			if(l > kMaxGameId || r + 4 + l != sizePos)
				continue;
			if(found && found != n)
				several = true;
			if(!found)
			{
				found = n;
				if(pAnchor)
					*pAnchor = p;
			}
		}
	}
	return several ? -1 : found;
}

// The save's major count from the body read through more(buf, n), which appends the next n bytes of the body to buf
// and returns false when it cannot (fewer bytes, or the stream raised). 0: unknown (why says why). *pHow: "its
// version mark" or "its layout"; why also carries the layout's details. Reads the header and the version string, and
// for an unmarked save the body on in kScanChunk pieces up to the database's magic, never further.
template<class More>
int ScanSave(More more, std::string& version, const char** pHow, std::string& why)
{
	std::string buf;
	*pHow = "";
	char sz[220];
	if(!more(buf, kVersionOff + 4))
	{
		why = "the body is shorter than its header";
		return 0;
	}
	const unsigned int len = U32(reinterpret_cast<const unsigned char*>(buf.data()) + kVersionOff);
	if(len == 0 || len > kMaxVersion)
	{
		snprintf(sz, sizeof(sz), "no version string at body offset 0x%x (length %u)", kVersionOff, len);
		why = sz;
		return 0;
	}
	if(!more(buf, len))
	{
		why = "the body ends inside the version string";
		return 0;
	}
	version.assign(buf, kVersionOff + 4, len);
	for(size_t i = 0; i < version.size(); ++i)
		if(version[i] < 0x20 || version[i] >= 0x7F)
		{
			version.clear();
			why = "the version string holds a byte outside printable ASCII";
			return 0;
		}
	const int mark = MarkMajors(version);
	if(mark > 0)
	{
		*pHow = "its version mark";
		return mark;
	}
	const size_t verEnd = buf.size();
	long magic = -1;
	while(magic < 0)
	{
		const size_t from = buf.size() >= verEnd + sizeof(kDbMagic) ? buf.size() - sizeof(kDbMagic) + 1 : verEnd;
		if(!more(buf, kScanChunk))
		{
			snprintf(sz, sizeof(sz), "no mark, and the body ended before the embedded save database (%zu bytes read)",
			         buf.size());
			why = sz;
			return 0;
		}
		magic = FindMagic(reinterpret_cast<const unsigned char*>(buf.data()), buf.size(), from);
	}
	size_t anchor = 0;
	const int m = LayoutMajors(reinterpret_cast<const unsigned char*>(buf.data()), (size_t)magic, verEnd, &anchor);
	if(m > 0)
	{
		snprintf(sz, sizeof(sz), "trade arrays of %d x %d after m_routesPerPlayer at body offset %zu, database at %ld",
		         m, m, anchor, magic);
		why = sz;
		*pHow = "its layout";
		return m;
	}
	snprintf(sz, sizeof(sz), "no mark, and the game section's tail fits %s count (database at %ld, %zu bytes read)",
	         m == 0 ? "no" : "more than one", magic, buf.size());
	why = sz;
	return 0;
}
}  // namespace savevariant
// END 0354 SAVE VARIANT LOGIC

namespace
{
using namespace savevariant;
const unsigned int kExitBase = 0x20560000u;  // application bit 29, 'V'; | save majors << 8 | running majors

int RunningMajors(bool* pbTest)
{
	*pbTest = false;
	const int n = SrvMajorCivs();
	if(n > 0)
		return n;
	char v[16];
	const DWORD k = GetEnvironmentVariableA("VP_SERVER_TEST_MAJOR_CIVS", v, sizeof(v));
	if(k > 0 && k < sizeof(v))
	{
		const int t = atoi(v);
		if(t >= kMinMajors && t <= kMaxMajors)
		{
			*pbTest = true;
			return t;
		}
	}
	return 0;
}

// The reader of ScanSave over the session stream: false when the stream gives fewer bytes or raises (the server's
// memory stream raises at its end; a pull past the engine stream's end ends the session in SrvRt::End, 0359's path,
// and does not return).
struct StreamMore
{
	FDataStream* s;
	bool operator()(std::string& buf, unsigned int n) const
	{
		const size_t at = buf.size();
		buf.resize(at + n);
		unsigned int got = 0;
		try
		{
			got = s->ReadIt(n, &buf[at]);
		}
		catch(...)
		{
			got = 0;
		}
		buf.resize(at + (got <= n ? got : 0));
		return got == n;
	}
};
}  // namespace

// ICvGame1::Read(FDataStream &): CALL self, stream in; RETURN u32 consumed (as the generated body).
bool ServerHook_ICvGame2_Read(ICvGame2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	FDataStream& s = SrvRt::StreamIn(args);
	if(!args.ok())
	{
		vpipc::Writer scratch;
		SrvRt::StreamInRet(scratch, s);  // drop the stream record
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvGame1::Read(FDataStream &): bad arguments");
	}
	SrvExeCrash::NoteLoad();  // ticket 0418: the EXE-crash report's "time since the save load began" (and test_exe_kill=load)
	bool bTest = false;
	const int running = RunningMajors(&bTest);
	std::string version, why;
	const char* how = "";
	const unsigned int start = s.GetPosition();
	// Ticket 0681: the bytes ScanSave reads anyway are kept (its first 4 KiB at most) for the mod-mismatch check; no
	// byte is read for it beyond ScanSave's reads (a read past a short body's end ends the session: hutest streamread)
	std::string head;
	StreamMore more = {&s};
	const auto keep = [&more, &head](std::string& buf, unsigned int n) -> bool {
		const size_t at = buf.size();
		const bool ok = more(buf, n);
		if(head.size() < 4096 && buf.size() > at)
			head.append(buf, at, std::min<size_t>(buf.size() - at, 4096 - head.size()));
		return ok;
	};
	const int saved = ScanSave(keep, version, &how, why);
	s.SetPosition(start);
	{
		// Ticket 0681: the body's game data hash (bytes 4..19) and its first bytes, and the save's file name for the
		// mod-mismatch check (srv_remote_savemods.h); the DLL compares the hash itself in CvGame::Read.
		const bool bHash = head.size() >= 20;
		std::string file;
		ICvGameContext1* pContext = SrvRemote::Context();
		ICvPreGame1* pPre = pContext ? pContext->GetPreGame() : NULL;
		if(pPre)
		{
			file = pPre->loadFileName().c_str();
			pPre->Destroy();
		}
		SrvSaveMods::BeginLoad(file.c_str(), bHash ? reinterpret_cast<const unsigned char*>(head.data()) + 4 : NULL, version,
		                       head);
	}
	if(saved <= 0)
		SrvRemote::Report("save variant: \"%s\": count unknown (%s); the DLL reads the save unchecked (ticket 0354)",
		                  version.c_str(), why.c_str());
	else if(running <= 0)
	{
		SrvLog("remote: LOUD: save variant: the running DLL's major count is unknown (the player-limits guard did not "
		       "run); save \"%s\" (%d majors by %s) not checked (ticket 0354)", version.c_str(), saved, how);
		SrvRemote::Report("save variant: save \"%s\" not checked: the running DLL's major count is unknown",
		                  version.c_str());
	}
	else
	{
		SrvRemote::Report("save variant: save \"%s\" has %d major civilizations (by %s%s%s), the running DLL %d%s: %s "
		                  "(ticket 0354)",
		                  version.c_str(), saved, how, why.empty() ? "" : ": ", why.c_str(), running,
		                  bTest ? " (VP_SERVER_TEST_MAJOR_CIVS)" : "", saved == running ? "loads" : "REFUSED");
		if(saved != running)
		{
			char sz[700];
			sprintf_s(sz,
			          "save-variant: the save was made with a %d-civilization build of Vox Populi (by %s; gamecore "
			          "version \"%s\"), the running DLL has %d major civilizations; refused before the DLL read the game "
			          "section (ticket 0354)",
			          saved, how, version.c_str(), running);
			SrvRemote::ReportAndExit((int)(kExitBase | ((unsigned)saved << 8) | (unsigned)running), sz);
		}
	}
	static_cast<ICvGame1*>(self)->Read(s);
	SrvRt::StreamInRet(ret, s);
	return true;
}
