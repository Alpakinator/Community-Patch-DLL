// srv_units_core.cpp - the CPU part of the server's unit scene (epic 0375, issue 4454). Contract: srv_units_core.h.
// Written 2026-11-15 for this repository (GPL-3.0-or-later). No EXE code (D83): the rules are the knowledge base's
// (unit-server-scene.md 3, 5, 10; unit-renderer.md 3, 10.3; unit-draws.md 3).
#include "srv_units_core.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "srv_units_anim.h"
#include "c5r/terrain/terrain.h"
#include "c5r/units/placement.h"

namespace srvunits {

namespace cu = c5r::units;

const char* statusName(Status s)
{
	static const char* const k[] = {"PrecheckFailed", "UnitRefused",   "ColourUnknown",    "FeedOutOfOrder", "UnknownRemoval",
	                                "KnownCreation",  "CombatHoldAmbiguous", "AllocatorOutOfStep", "PlayerUnknown",
	                                "UnitTypeUnknown", "PlayerArtUnknown", "SeedMissing", "PlotUnknown", "FrameFailed",
	                                "ReflectionMatrixMissing", "AnimFailed", "AnimUnavailable"};
	static_assert(sizeof(k) / sizeof(k[0]) == (size_t)Status::kCount, "status names");
	return (size_t)s < (size_t)Status::kCount ? k[(size_t)s] : "?";
}

Status statusOfSeed(SeedStatus s)
{
	switch(s)
	{
	case SeedStatus::kUnknownRemoval: return Status::kUnknownRemoval;
	case SeedStatus::kKnownCreation: return Status::kKnownCreation;
	case SeedStatus::kCombatHoldAmbiguous: return Status::kCombatHoldAmbiguous;
	case SeedStatus::kOutOfStep: return Status::kAllocatorOutOfStep;
	default: return Status::kCount;
	}
}

bool IsUnitKind(uint16_t kind)
{
	return (kind >= SrvFeed::kUnitCreated && kind <= SrvFeed::kUnitCombat) ||
	       (kind >= SrvFeed::kUnitMissionEnd && kind <= SrvFeed::kEraChanged);
}

bool LoadSetupGoesOn(LoadSetup& s, const SrvFeed::Event& e, double tSec)
{
	if(e.kind == SrvFeed::kLoadBegin)
	{
		s.active = true;
		s.maxOwner = -1;
		s.lastSec = tSec;
		s.events = 0;
		return true;
	}
	if(!s.active)
		return false;
	// CvUnit::setupGraphical's calls (unit-server-scene.md 18.3): nothing else belongs to a setup
	bool setupCall = false;
	switch(e.kind)
	{
	case SrvFeed::kUnitCreated:
	case SrvFeed::kUnitEmbark:
	case SrvFeed::kUnitShouldDimFlag:
	case SrvFeed::kUnitFortify:
	case SrvFeed::kUnitSetDamage:
	case SrvFeed::kUnitWork:
	case SrvFeed::kUnitVisibility:
	case SrvFeed::kUnitGarrison:
		setupCall = true;
		break;
	default:
		break;
	}
	const int32_t owner = e.a[0];
	if(!setupCall || owner < s.maxOwner || tSec - s.lastSec > kLoadSetupGapSec || tSec < s.lastSec)
	{
		s.active = false;
		return false;
	}
	s.maxOwner = owner;
	s.lastSec = tSec;
	s.events++;
	return true;
}

// ---- the live feed ------------------------------------------------------------------------------------------------
namespace {
struct LivePort : FeedPort
{
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& gen) override { SrvFeed::CopyUnits(out, gen); }
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override { SrvFeed::CopyPlayers(out); }
	bool unitDatabase(std::wstring& path, uint32_t& gen) override { return SrvFeed::GetUnitDatabase(path, gen); }
};
}  // namespace
FeedPort& LiveFeedPort()
{
	static LivePort p;
	return p;
}

// ---- lookups --------------------------------------------------------------------------------------------------------
bool Lookups::load(c5m::TableSource& db, std::string& why)
{
	struct T { const char* table; std::map<int32_t, std::string>* out; };
	const T ts[] = {{"Units", &units}, {"PlayerColors", &colours}, {"Civilizations", &civs}, {"Eras", &eras},
	                {"ArtStyleTypes", &artStyles}, {"MinorCivilizations", &minorCivs}};   // MinorCivilizations: issue 4460 (A1)
	for(const T& t : ts)
	{
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		t.out->clear();
		if(!db.rows(t.table, {"ID", "Type"}, rows, &e))
		{
			why = std::string("table ") + t.table + " (ID, Type): " + e.what;
			return false;
		}
		for(const c5m::DbRow& r : rows)
		{
			if(r.size() != 2 || r[0].null || r[1].null)
				continue;
			char* end = nullptr;
			const long v = strtol(r[0].text.c_str(), &end, 10);
			if(!end || *end)
			{
				why = std::string("table ") + t.table + ": ID '" + r[0].text + "' is not an integer";
				return false;
			}
			(*t.out)[(int32_t)v] = r[1].text;
		}
	}
	if(units.empty())
	{
		why = "table Units has no rows";
		return false;
	}
	std::string ew;
	if(!loadEmbark(db, ew))
		embarkWhy = ew.empty() ? std::string("embarked art tables not read") : ew;
	// Issue 4986 (hunk H1, agreed by lead-0042 #8163): Units.AirInterceptRange for the unit extras (AirInterceptPatrol);
	// optional: a failure leaves airInterceptWhy set and the map empty, load succeeds.
	airInterceptRange.clear();
	airInterceptWhy.clear();
	{
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		if(!db.rows("Units", {"ID", "AirInterceptRange"}, rows, &e))
			airInterceptWhy = std::string("table Units (ID, AirInterceptRange): ") + e.what;
		else
			for(const c5m::DbRow& r : rows)
			{
				if(r.size() != 2 || r[0].null)
					continue;
				char* e0 = nullptr;
				char* e1 = nullptr;
				const long id = strtol(r[0].text.c_str(), &e0, 10);
				const long v = r[1].null ? 0 : strtol(r[1].text.c_str(), &e1, 10);
				if(!e0 || *e0 || (!r[1].null && (!e1 || *e1)))
					continue;
				airInterceptRange[(int32_t)id] = (int32_t)v;
			}
	}
	// Issue 4986 (MC #11389): the DLL's GetAirInterceptRange is the unit row plus promotions' AirInterceptRangeChange; VP has
	// no such promotion, so the row is exact there. A mod's promotion that changes it is listed here (the extra says so in
	// one line: its ring shows the base range only). Optional: a missing column leaves airInterceptPromotionsWhy set.
	airInterceptPromotions.clear();
	airInterceptPromotionsWhy.clear();
	{
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		if(!db.rows("UnitPromotions", {"Type", "AirInterceptRangeChange"}, rows, &e))
			airInterceptPromotionsWhy = std::string("table UnitPromotions (Type, AirInterceptRangeChange): ") + e.what;
		else
			for(const c5m::DbRow& r : rows)
			{
				if(r.size() != 2 || r[1].null)
					continue;
				char* e1 = nullptr;
				const long v = strtol(r[1].text.c_str(), &e1, 10);
				if(e1 && !*e1 && v != 0)
					airInterceptPromotions.push_back(r[0].null ? std::string("?") : r[0].text);
			}
	}
	return true;
}

// Issue 4469: the embarked art's tables (Lookups::embarkOverride, lastEmbarkArtEra, allWaterEra).
bool Lookups::loadEmbark(c5m::TableSource& db, std::string& why)
{
	embarkOverride.clear();
	lastEmbarkArtEra = allWaterEra = -1;
	embarkWhy.clear();
	auto read = [&](const char* table, std::initializer_list<std::string> cols, std::vector<c5m::DbRow>& rows) {
		c5m::Error e;
		rows.clear();
		if(!db.rows(table, std::vector<std::string>(cols), rows, &e))
		{
			why = std::string("table ") + table + ": " + e.what;
			return false;
		}
		return true;
	};
	auto num = [](const c5m::DbRow& r, size_t i, long& v) {
		if(r.size() <= i || r[i].null)
			return false;
		char* end = nullptr;
		v = strtol(r[i].text.c_str(), &end, 10);
		return end && !*end && !r[i].text.empty();
	};
	std::vector<c5m::DbRow> rows;
	if(!read("Defines", {"Name", "Value"}, rows))
		return false;
	for(const c5m::DbRow& r : rows)
	{
		long v = 0;
		if(r.size() == 2 && !r[0].null && r[0].text == "LAST_EMBARK_ART_ERA" && num(r, 1, v))
			lastEmbarkArtEra = (int32_t)v;
	}
	if(lastEmbarkArtEra < 0)
	{
		why = "Defines has no integer LAST_EMBARK_ART_ERA";
		return false;
	}
	std::map<std::string, int32_t> eraId;
	for(const auto& kv : eras)
		eraId[kv.second] = kv.first;
	if(!read("Technologies", {"Era", "EmbarkedAllWaterPassage"}, rows))
		return false;
	for(const c5m::DbRow& r : rows)
	{
		long v = 0;
		if(r.size() != 2 || r[0].null || !num(r, 1, v) || v == 0)
			continue;
		auto it = eraId.find(r[0].text);
		if(it == eraId.end())
		{
			why = "Technologies: era " + r[0].text + " of an all-water technology has no Eras row";
			return false;
		}
		if(allWaterEra < 0 || it->second < allWaterEra)
			allWaterEra = it->second;
	}
	// the DLL's CvPlayer::init: the first trait test wins (EmbarkedAllWater before EmbarkedToLandFlatCost)
	std::map<std::string, int> traitKind;   // 1 all water, 2 to-land flat cost
	if(!read("Traits", {"Type", "EmbarkedAllWater", "EmbarkedToLandFlatCost"}, rows))
		return false;
	for(const c5m::DbRow& r : rows)
	{
		long a = 0, b = 0;
		if(r.size() != 3 || r[0].null)
			continue;
		const bool hasA = num(r, 1, a) && a != 0, hasB = num(r, 2, b) && b != 0;
		if(hasA || hasB)
			traitKind[r[0].text] = hasA ? 1 : 2;
	}
	std::map<std::string, int> leaderKind;
	if(!read("Leader_Traits", {"LeaderType", "TraitType"}, rows))
		return false;
	for(const c5m::DbRow& r : rows)
	{
		if(r.size() != 2 || r[0].null || r[1].null)
			continue;
		auto k = traitKind.find(r[1].text);
		if(k == traitKind.end())
			continue;
		int& lk = leaderKind[r[0].text];
		if(lk == 0 || k->second < lk)
			lk = k->second;
	}
	std::map<std::string, int32_t> civId;
	for(const auto& kv : civs)
		civId[kv.second] = kv.first;
	if(!read("Civilization_Leaders", {"CivilizationType", "LeaderheadType"}, rows))
		return false;
	for(const c5m::DbRow& r : rows)
	{
		if(r.size() != 2 || r[0].null || r[1].null)
			continue;
		auto lk = leaderKind.find(r[1].text);
		auto c = civId.find(r[0].text);
		if(lk == leaderKind.end() || c == civId.end())
			continue;
		embarkOverride[c->second] = lk->second == 1 ? "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE" : "ART_DEF_UNIT_U_DANISH_LONGBOAT";
	}
	// issue 0439: the override the feed carries as a hash, matched against every art define Type
	artByHash24.clear();
	artHashWhy.clear();
	{
		c5m::Error e;
		rows.clear();
		if(!db.rows("ArtDefine_UnitInfos", std::vector<std::string>{"Type"}, rows, &e))
			artHashWhy = "table ArtDefine_UnitInfos: " + e.what;   // only an override from the feed needs it
	}
	for(const c5m::DbRow& r : rows)
	{
		if(r.size() != 1 || r[0].null || r[0].text.empty())
			continue;
		const uint32_t h = SrvFeed::EmbarkArtHash24(r[0].text.c_str());
		auto it = artByHash24.find(h);
		if(it == artByHash24.end())
			artByHash24[h] = r[0].text;
		else if(it->second != r[0].text)
			it->second.clear();   // two Types share the hash: ambiguous
	}
	return true;
}

bool EmbarkedRow(const Lookups& l, int32_t civilization, int32_t era, std::string& row, std::string& why)
{
	row.clear();
	if(!l.embarkWhy.empty())
	{
		why = l.embarkWhy;
		return false;
	}
	if(l.lastEmbarkArtEra < 0)
	{
		why = "the embarked art tables were not loaded";
		return false;
	}
	if(civilization >= 0)
	{
		auto o = l.embarkOverride.find(civilization);
		if(o != l.embarkOverride.end())
		{
			row = o->second;
			return true;
		}
	}
	if(era >= l.lastEmbarkArtEra)
		row = "ART_DEF_UNIT_TRANSPORT";
	else if(l.allWaterEra < 0 || era < l.allWaterEra)
		row = "ART_DEF_UNIT_GALLEY";
	else
	{
		why = "era " + std::to_string(era) + ": GALLEON or GALLEY by the team's all-water passage, which the feed does not carry";
		return false;
	}
	return true;
}

bool EmbarkedRow(const Lookups& l, int32_t civilization, int32_t era, uint32_t bits, std::string& row, std::string& why)
{
	if(!(bits & SrvFeed::kEmbarkArtRead))
		return EmbarkedRow(l, civilization, era, row, why);
	row.clear();
	if(!l.embarkWhy.empty())
	{
		why = l.embarkWhy;
		return false;
	}
	if(l.lastEmbarkArtEra < 0)
	{
		why = "the embarked art tables were not loaded";
		return false;
	}
	if(bits & SrvFeed::kEmbarkArtOverride)
	{
		if(!l.artHashWhy.empty())
		{
			why = "the embarked override: " + l.artHashWhy;
			return false;
		}
		const uint32_t h = bits >> SrvFeed::kEmbarkArtHashShift;
		auto it = l.artByHash24.find(h);
		if(it != l.artByHash24.end())
		{
			if(it->second.empty())
			{
				char b[96];
				snprintf(b, sizeof(b), "the embarked override's hash %06x names two art defines", (unsigned)h);
				why = b;
				return false;
			}
			row = it->second;
			return true;
		}
		// no art define has it: the EXE ignores such an override and takes the era's row [C, 3.2]
	}
	if(era >= l.lastEmbarkArtEra)
		row = "ART_DEF_UNIT_TRANSPORT";
	else
		row = (bits & SrvFeed::kEmbarkArtAllWater) ? "ART_DEF_UNIT_GALLEON" : "ART_DEF_UNIT_GALLEY";
	return true;
}

// ---- fades ----------------------------------------------------------------------------------------------------------
bool fadeState(const Fade& f, bool flagVisible, double tMs, float& from, float& to, float& p, bool& running)
{
	running = false;
	from = 1;
	to = 0;
	p = 0;
	if(!f.have)
		return flagVisible;
	if(f.blendMs <= 0 || !std::isfinite(f.blendMs))
		return f.visible;   // at once: (1, 0, 0) when visible, not listed when hidden
	double q = (tMs - f.t0Ms) / (double)f.blendMs;
	q = q < 0 ? 0 : q > 1 ? 1 : q;
	p = (float)q;
	if(f.visible)
	{
		from = 0;
		to = 1;
		running = q < 1;
		return true;
	}
	from = 1;
	to = 0;
	running = q < 1;
	return q < 1;
}

// ---- cull -----------------------------------------------------------------------------------------------------------
static void mul44(const float* a, const float* b, float* o)
{
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
			o[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

static bool inverse44(const float* m, float* out)
{
	double a[4][8];
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 8; ++c)
			a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	for(int c = 0; c < 4; ++c)
	{
		int piv = c;
		for(int r = c + 1; r < 4; ++r)
			if(std::fabs(a[r][c]) > std::fabs(a[piv][c]))
				piv = r;
		if(std::fabs(a[piv][c]) < 1e-12)
			return false;
		if(piv != c)
			for(int k = 0; k < 8; ++k)
				std::swap(a[c][k], a[piv][k]);
		const double d = a[c][c];
		for(int k = 0; k < 8; ++k)
			a[c][k] /= d;
		for(int r = 0; r < 4; ++r)
			if(r != c)
			{
				const double f = a[r][c];
				for(int k = 0; k < 8; ++k)
					a[r][k] -= f * a[c][k];
			}
	}
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
			out[r * 4 + c] = (float)a[r][c + 4];
	return true;
}

static bool allFinite(const float* v, int n);

CullCamera CullOf(const UnitsCamera& cam)
{
	CullCamera cc;
	mul44(cam.view, cam.proj, cc.vp);
	cc.wrap = cam.wrapPeriod;
	cc.valid = allFinite(cc.vp, 16);
	return cc;
}

bool boxInView(const float vp[16], float x0, float y0, float z0, float x1, float y1, float z1)
{
	// clip = (x, y, z, 1) x vp; outside a plane when every corner is: x < -w, x > w, y < -w, y > w, z < 0, z > w
	int out[6] = {0, 0, 0, 0, 0, 0};
	for(int i = 0; i < 8; ++i)
	{
		const float x = (i & 1) ? x1 : x0, y = (i & 2) ? y1 : y0, z = (i & 4) ? z1 : z0;
		float c[4];
		for(int k = 0; k < 4; ++k)
			c[k] = x * vp[0 * 4 + k] + y * vp[1 * 4 + k] + z * vp[2 * 4 + k] + vp[3 * 4 + k];
		out[0] += c[0] < -c[3];
		out[1] += c[0] > c[3];
		out[2] += c[1] < -c[3];
		out[3] += c[1] > c[3];
		out[4] += c[2] < 0;
		out[5] += c[2] > c[3];
	}
	for(int k = 0; k < 6; ++k)
		if(out[k] == 8)
			return false;
	return true;
}

static bool allFinite(const float* v, int n)
{
	for(int i = 0; i < n; ++i)
		if(!std::isfinite(v[i]))
			return false;
	return true;
}

bool passCamera(const UnitsCamera& cam, int k, float waterLevel, cu::PassCamera& out)
{
	if(!allFinite(cam.view, 16) || !allFinite(cam.proj, 16))
		return false;
	float vp[16];
	mul44(cam.view, cam.proj, vp);
	if(k != 0)
	{
		// Translation(k W, 0, 0) x VP: row 3 += k W row 0 (row vectors)
		const float s = (float)k * cam.wrapPeriod;
		for(int c = 0; c < 4; ++c)
			vp[12 + c] += s * vp[c];
	}
	float eye[3] = {cam.camWorld[12], cam.camWorld[13], cam.camWorld[14]};
	const bool cwOk = allFinite(cam.camWorld, 16) && (eye[0] != 0 || eye[1] != 0 || eye[2] != 0);
	if(!cwOk)
	{
		float inv[16];
		if(!inverse44(cam.view, inv))
			return false;
		eye[0] = inv[12];
		eye[1] = inv[13];
		eye[2] = inv[14];
	}
	out = cu::PassCamera();
	for(int r = 0; r < 3; ++r)
		out.rotation[r] = {cwOk ? cam.camWorld[r * 4 + 0] : 0.0f, cwOk ? cam.camWorld[r * 4 + 1] : 0.0f,
		                   cwOk ? cam.camWorld[r * 4 + 2] : 0.0f, 0.0f};
	memcpy(out.eye, eye, sizeof(eye));
	for(int r = 0; r < 4; ++r)
		out.viewProj[r] = {vp[r * 4 + 0], vp[r * 4 + 1], vp[r * 4 + 2], vp[r * 4 + 3]};
	out.threshold = waterLevel;
	float fog[8];
	c5r::terrain::distanceFogBlock(c5r::terrain::DistanceFog(), eye, fog);
	out.fogColour = {fog[0], fog[1], fog[2], fog[3]};
	out.fog = {fog[4], fog[5], fog[6], fog[7]};
	return true;
}

// ---- the core -------------------------------------------------------------------------------------------------------
UnitsCore::UnitsCore(FeedPort& port, LogFn log) : m_port(port), m_log(std::move(log))
{
	m_players.resize(64);
	for(SrvFeed::PlayerRecord& p : m_players)
	{
		memset(&p, 0, sizeof(p));
		p.player = -1;
	}
	m_alloc.reset(new SeedAllocator(
	    [this](const SrvFeed::UnitRecord& u, int o, int n) { return artChanges(u, o, n); },
	    [this](SeedStatus s, const SrvFeed::Event& e, const char* detail) {
		    const Status st = statusOfSeed(s);
		    if(st == Status::kCount)
			    return;
		    note(st, "", "allocator: %s: kind %u seq %u key (%d, %d): %s", statusName(st), (unsigned)e.kind, e.seq, e.a[0], e.a[1],
		         detail ? detail : "");
	    }));
	// issue 4460: decision 14 (a), the combat-held removal. Issue 4462: the table that covers the stock path (4453,
	// unit-formations.md 12.7.6 (5)) and the product path (4458, 12.7.7 (1)): melee 4.3 s band 0.6 s, ranged 3.9 s
	// band 0.35 s from the kind 25 event; a creation inside a band is counted CombatHoldAmbiguous, never guessed
	HoldParams hp;
	hp.lengthSec[(int)HoldClass::kMelee] = kHoldMeleeSec;
	hp.lengthSec[(int)HoldClass::kRanged] = kHoldRangedSec;
	hp.bandSec[(int)HoldClass::kMelee] = kHoldMeleeBandSec;
	hp.bandSec[(int)HoldClass::kRanged] = kHoldRangedBandSec;
	m_alloc->SetHoldParams(hp);
}

UnitsCore::~UnitsCore() {}

void UnitsCore::note(Status s, const std::string& onceKey, const char* fmt, ...)
{
	m_c.byStatus[(int)s]++;
	bool say;
	if(!onceKey.empty())
		say = m_once.insert(std::string(statusName(s)) + "|" + onceKey).second;
	else
	{
		const uint64_t n = ++m_statusLogged[(int)s];
		say = n <= 20 || n % 100 == 0;
	}
	if(!say || !m_log)
		return;
	char body[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(body, sizeof(body), fmt, ap);
	va_end(ap);
	char line[1200];
	snprintf(line, sizeof(line), "LOUD: units: %s: %s (%llu so far)", statusName(s), body, (unsigned long long)m_c.byStatus[(int)s]);
	m_log(line);
}

void UnitsCore::setDatabase(const Lookups& l, const cu::ArtResolver* art)
{
	m_look = l;
	m_art = art;
}

void UnitsCore::takeTable(const char* why)
{
	(void)why;
	uint32_t gen = 0;
	m_port.copyUnits(m_units, gen);
	std::sort(m_units.begin(), m_units.end(), [](const SrvFeed::UnitRecord& a, const SrvFeed::UnitRecord& b) {
		return a.owner != b.owner ? a.owner < b.owner : a.id < b.id;
	});
	m_tableGen = gen;
	m_haveGen = true;
	m_tableTaken = true;
	m_c.tableCopies++;
}

bool UnitsCore::onBatch(const SrvFeed::Batch& b, double tMs)
{
	m_c.batches++;
	if(b.width && b.height)
	{
		m_width = b.width;
		m_height = b.height;
	}
	bool anyUnit = false, overflow = false;
	uint32_t maxSeq = m_lastSeq;
	std::map<int64_t, int32_t> oldPlots;   // issue 4755: the plot each unit with a kind 12 stood on before this batch
	for(const SrvFeed::Event& e : b.events)
	{
		if(e.kind == SrvFeed::kOverflow)
			overflow = true;
		if(IsUnitKind(e.kind) || e.kind == SrvFeed::kCityCombat)   // kind 28 upserts its units' records (4452)
			anyUnit = true;
		maxSeq = (std::max)(maxSeq, e.seq);
		if(m_posExe && e.kind == SrvFeed::kUnitMoved && m_tableTaken)
		{
			SrvFeed::UnitRecord r;
			const int64_t key = UnitKeyOf(e.a[0], e.a[1]);
			if(!oldPlots.count(key) && record(e.a[0], e.a[1], r))
				oldPlots[key] = r.plot;
		}
	}
	for(const SrvFeed::PlayerRecord& p : b.players)
		if(p.player >= 0 && p.player < 64)
			m_players[(size_t)p.player] = p;
	if(m_anim && !b.lateResults.empty())
		m_anim->lateResults(b.lateResults);   // issue 4783: event ids of earlier batches' combats (applied at the next close)
	bool changed = !b.players.empty();
	if(!m_haveGen || b.unitTableGen != m_tableGen || overflow || anyUnit)
	{
		if(!m_haveGen)
		{
			std::vector<SrvFeed::PlayerRecord> all;
			m_port.copyPlayers(all);
			for(const SrvFeed::PlayerRecord& p : all)
				if(p.player >= 0 && p.player < 64)
					m_players[(size_t)p.player] = p;
		}
		// issue 4463: the copy is the one SrvFeed::Drain took in the lock acquisition of this batch's events (srv_feed.h
		// CopyUnits), so a PushUnit between that Drain and this call is not in it (it comes with the next batch)
		takeTable(overflow ? "overflow" : anyUnit ? "unit events" : "start");
		changed = true;
		// the feed's contract (srv_feed.h CopyUnits): the table is the state after exactly the events drained so far; a
		// record newer than the batch is a broken contract (LOUD), no longer the benign race of note 3.4
		for(const SrvFeed::UnitRecord& u : m_units)
			if(u.seq > maxSeq)
				note(Status::kFeedOutOfOrder, "", "table record (%d, %d) has seq %u above the last drained %u (the table copy is newer than the batch)",
				     u.owner, u.id, u.seq, maxSeq);
	}
	for(const SrvFeed::Event& e : b.events)
	{
		if(e.kind == SrvFeed::kOverflow)
		{
			m_alloc->NoteDropped((uint64_t)(uint32_t)e.a[0]);
			if(m_anim)
				m_anim->resync();   // issue 4456: events were dropped: the controller follows the re-copied table
			continue;
		}
		if(e.kind == SrvFeed::kUiMirror)
		{
			if(e.flags == kUiKindUnitPosition || e.flags == kUiKindFrameMark)
				posRecord(e);   // issue 4755
			continue;
		}
		if(!IsUnitKind(e.kind) && e.kind != SrvFeed::kCityCombat)
			continue;
		m_c.unitEvents++;
		if(e.seq <= m_lastSeq)
		{
			note(Status::kFeedOutOfOrder, "", "kind %u seq %u not above the last taken %u: skipped", (unsigned)e.kind, e.seq, m_lastSeq);
			continue;
		}
		// ticket 0502: a load's setup keeps feed order (frame grouping off) until its first event that is not a setup call;
		// the grouping is back on before that event is applied, and off only after kLoadBegin's own Apply (its clear)
		if(e.kind != SrvFeed::kLoadBegin && m_loadSetup.active)
		{
			if(LoadSetupGoesOn(m_loadSetup, e, tMs / 1000.0))
				m_c.loadSetupEvents++;
			else
			{
				m_alloc->SetFrameGrouping(true);
				char line[200];
				snprintf(line, sizeof(line), "units: load setup %llu ended at kind %u of player %d (seq %u): %llu unit events in "
				         "feed order, frame grouping on again (ticket 0502)", (unsigned long long)m_c.loadSetups,
				         (unsigned)e.kind, e.a[0], e.seq, (unsigned long long)m_loadSetup.events);
				m_log(line);
			}
		}
		if(e.kind == SrvFeed::kUnitMoved && (e.a[2] < 0 || (size_t)e.a[2] >= b.paths.size()))
		{
			note(Status::kFeedOutOfOrder, "", "kUnitMoved seq %u path index %d outside the batch's %zu paths: skipped", e.seq, e.a[2],
			     b.paths.size());
			m_lastSeq = e.seq;
			continue;
		}
		if(e.kind == SrvFeed::kUnitCombat || e.kind == SrvFeed::kCityCombat)
		{
			// issue 4460: the allocator's combat input (SeedAllocator::Apply ignores 25 / 28; NoteCombat takes them)
			if(e.a[2] < 0 || (size_t)e.a[2] >= b.combats.size() || b.combats[(size_t)e.a[2]].seq != e.seq)
			{
				note(Status::kFeedOutOfOrder, "", "kind %u seq %u combat index %d outside the batch's %zu combats (or another seq): skipped",
				     (unsigned)e.kind, e.seq, e.a[2], b.combats.size());
				m_lastSeq = e.seq;
				continue;
			}
			m_lastSeq = e.seq;
			m_alloc->NoteCombat(e, tMs / 1000.0, combatNoteOf(b.combats[(size_t)e.a[2]]));
			m_c.combatNotes++;
			if(m_anim) m_anim->take(e, b);   // issue 4774: the fight clock's input, in the DLL's order
			continue;
		}
		m_lastSeq = e.seq;
		m_alloc->Apply(e, tMs / 1000.0, m_units);
		if(e.kind == SrvFeed::kLoadBegin)
		{
			LoadSetupGoesOn(m_loadSetup, e, tMs / 1000.0);   // ticket 0502: the setup begins after the load's clear
			m_alloc->SetFrameGrouping(false);
			m_c.loadSetups++;
		}
		if(m_posExe)
			posEvent(e, oldPlots);   // issue 4755: walks, snaps, the load's clear (before the controller takes the event)
		if(m_anim)
		{
			// issue 4469: an Embark of a unit the controller holds is the EXE's model swap (a removal and a creation,
			// 12.7.3 S3): handed on as a creation of a held key, which the controller applies as remove + create
			// (srv_units_anim.cpp), so the unit binds its new art (the embarked row, or the land row again) through
			// inputOf; its figures restart in the default state, as the controller's own embark event does.
			if(e.kind == SrvFeed::kUnitEmbark && m_anim->holds(e.a[0], e.a[1]))
			{
				SrvFeed::Event c = e;
				c.kind = SrvFeed::kUnitCreated;
				m_anim->take(c, b);
			}
			else
				m_anim->take(e, b);   // issue 4456: the controller's timeline (unit-controller.md 3)
		}
		const int64_t key = UnitKeyOf(e.a[0], e.a[1]);
		switch(e.kind)
		{
		case SrvFeed::kUnitCreated:
		case SrvFeed::kUnitDestroyed:
			m_fades.erase(key);
			m_embarkArt.erase(key);
			break;
		case SrvFeed::kUnitEmbark:   // issue 0439: the embarked art's inputs at the call (srv_feed.h EmbarkArtBits)
			if(e.a[2])
				m_embarkArt[key] = (uint32_t)e.a[3];
			else
				m_embarkArt.erase(key);
			break;
		case SrvFeed::kUnitVisibility:
		{
			// issue 4463: a Visibility for a key the feed's table did not hold (before the creation) is ignored, as the EXE
			// ignores it (note 12.1 [L]); else a unit created embarked (kind 21, which erases no fade) would keep it
			if(e.flags & SrvFeed::kEventNoRecord)
				break;
			Fade f;
			f.have = true;
			f.visible = (e.a[2] & 1) != 0;
			f.t0Ms = tMs;
			float sec = 0;
			memcpy(&sec, &e.a[3], sizeof(sec));
			f.blendMs = std::isfinite(sec) && sec > 0 ? sec * 1000.0f : 0.0f;
			m_fades[key] = f;
			break;
		}
		case SrvFeed::kLoadBegin:
			m_fades.clear();
			m_embarkArt.clear();
			break;
		default: break;
		}
	}
	if(m_extras && m_extras->anyOn())
		m_extras->onBatch(b);   // issue 4981: after the scene took the batch
	return changed;
}

// ---- issue 4755: the root position source exe (unit-server-scene.md 17) ---------------------------------------------
void UnitsCore::setAnimFeed(AnimFeed* a)
{
	m_anim = a;
	if(m_anim)
		m_anim->setPositionsExe(m_posExe);
}

void UnitsCore::setPositionsExe(bool on)
{
	m_posExe = on;
	if(!on)
		m_pos.clear();
	if(m_anim)
		m_anim->setPositionsExe(on);
}

size_t UnitsCore::walksOpen() const
{
	size_t n = 0;
	for(const auto& kv : m_pos)
		n += kv.second.walking ? 1 : 0;
	return n;
}

bool UnitsCore::rootOf(int32_t owner, int32_t id, float& x, float& y, float& z) const
{
	auto it = m_pos.find(UnitKeyOf(owner, id));
	if(!m_posExe || it == m_pos.end() || !it->second.have)
		return false;
	x = it->second.x;
	y = it->second.y;
	z = it->second.z;
	return true;
}

bool UnitsCore::walking(int32_t owner, int32_t id) const
{
	auto it = m_pos.find(UnitKeyOf(owner, id));
	return it != m_pos.end() && it->second.walking;
}

void UnitsCore::posLine(bool loud, const char* fmt, ...)
{
	// the first 20 lines, then every 100th (as note()'s unkeyed lines)
	const uint64_t n = ++m_posLines;
	if(!m_log || !(n <= 20 || n % 100 == 0))
		return;
	char body[700];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(body, sizeof(body), fmt, ap);
	va_end(ap);
	char line[800];
	snprintf(line, sizeof(line), "%sunits: positions: %s", loud ? "LOUD: " : "", body);
	m_log(line);
}

void UnitsCore::openLedger(int64_t key, PosState& s, uint16_t kind, uint32_t seq)
{
	if(!m_ledgerOn)
		return;
	WalkRecord w;
	w.owner = (int32_t)(key >> 32);
	w.id = (int32_t)(uint32_t)key;
	w.kind = kind;
	w.seq = seq;
	w.frame = m_frameId;
	w.lastPosBeforeSeq = s.seq;
	w.lastPosBeforeFrame = s.seq ? s.posFrame : -1;
	s.ledger = (int)m_ledger.size();
	m_ledger.push_back(w);
}

void UnitsCore::closeLedger(PosState& s, const char* end, uint32_t seq)
{
	if(s.ledger < 0 || (size_t)s.ledger >= m_ledger.size())
	{
		s.ledger = -1;
		return;
	}
	WalkRecord& w = m_ledger[(size_t)s.ledger];
	w.end = end;
	w.endSeq = seq;
	w.endFrame = m_frameId;
	s.ledger = -1;
}

void UnitsCore::endWalk(int64_t key, PosState& s, const char* why, uint32_t seq, bool giveArrive)
{
	if(!s.walking)
		return;
	s.walking = false;
	s.walkHasPos = false;
	s.fromPlot = -1;
	closeLedger(s, why, seq);
	if(giveArrive && m_anim)
		m_anim->arrive((int32_t)(key >> 32), (int32_t)(uint32_t)key);
}

// A kind-6 (position, arrival) or kind-7 (frame mark) record of the proxy's bridge (SrvFeed::kUiMirror).
void UnitsCore::posRecord(const SrvFeed::Event& e)
{
	if(e.seq <= m_lastUiSeq)
	{
		m_pc.outOfOrder++;
		posLine(true, "a bridge record (kind %u) with seq %u not above the last taken %u: skipped (%llu so far)", (unsigned)e.flags, e.seq,
		        m_lastUiSeq, (unsigned long long)m_pc.outOfOrder);
		return;
	}
	m_lastUiSeq = e.seq;
	if(e.flags == kUiKindFrameMark)
	{
		m_pc.frameMarks++;
		m_pc.lastExeFrame = (uint32_t)e.a[0];
		return;
	}
	if(!m_posExe)
	{
		m_pc.ignoredOwn++;
		return;
	}
	const bool arrival = e.turn == 1;
	float x, y, z;
	memcpy(&x, &e.a[1], 4);
	memcpy(&y, &e.a[2], 4);
	memcpy(&z, &e.a[3], 4);
	if(e.plot < 0 || e.plot >= 64 || (e.turn != 0 && e.turn != 1) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
	{
		m_pc.bad++;
		posLine(true, "a %s record seq %u of player %d unit %d refused: %s (%llu so far)", arrival ? "arrival" : "position", e.seq, e.plot, e.a[0],
		        e.plot < 0 || e.plot >= 64 ? "the player is not 0..63" : (e.turn != 0 && e.turn != 1) ? "the arrival field is not 0 or 1"
		                                                                                              : "x, y or z is not finite",
		        (unsigned long long)m_pc.bad);
		return;
	}
	const int32_t owner = e.plot, id = e.a[0];
	SrvFeed::UnitRecord r;
	if(!record(owner, id, r))
	{
		m_pc.unknownUnit++;
		posLine(false, "a %s record seq %u of unit (%d, %d), which the table does not hold: not applied (%llu so far)",
		        arrival ? "arrival" : "position", e.seq, owner, id, (unsigned long long)m_pc.unknownUnit);
		return;
	}
	const int64_t key = UnitKeyOf(owner, id);
	PosState& s = m_pos[key];
	if(arrival)
	{
		m_pc.arrivals++;
		s.have = false;   // the arrival snaps the unit to its plot centre
		if(s.walking)
		{
			m_pc.arrived++;
			endWalk(key, s, "arrival", e.seq, true);
		}
		else
			m_pc.arrivalsIdle++;
		return;
	}
	if(s.walking && e.seq < s.moveSeq)
	{
		// F7 (contract 6): a position caused by a move reaches the server after the move's DLL call
		m_pc.beforeMove++;
		posLine(true, "F7: position seq %u of unit (%d, %d) is below its kind 12 seq %u: not applied (%llu so far)", e.seq, owner, id, s.moveSeq,
		        (unsigned long long)m_pc.beforeMove);
		return;
	}
	m_pc.positions++;
	if(s.walking)
	{
		m_pc.inWalk++;
		if(!s.walkHasPos && s.ledger >= 0 && (size_t)s.ledger < m_ledger.size())
		{
			m_ledger[(size_t)s.ledger].firstPosSeq = e.seq;
			m_ledger[(size_t)s.ledger].firstPosFrame = m_frameId;
		}
		if(!s.walkHasPos)
		{
			const int64_t d = m_frameId - s.moveFrame;   // drawn frames from the kind 12's stamp to this position's
			m_pc.firstPosFramesMax = (std::max)(m_pc.firstPosFramesMax, d);
			if(d > 2)
				m_pc.firstPosOver2++;
		}
		s.walkHasPos = true;
	}
	else
	{
		m_pc.noWalk++;
		if(s.ledger >= 0 && (size_t)s.ledger < m_ledger.size() && m_ledger[(size_t)s.ledger].firstPosSeq == 0)
		{
			m_ledger[(size_t)s.ledger].firstPosSeq = e.seq;
			m_ledger[(size_t)s.ledger].firstPosFrame = m_frameId;
		}
	}
	if(s.ledger >= 0 && (size_t)s.ledger < m_ledger.size())
		m_ledger[(size_t)s.ledger].positions++;
	s.have = true;
	s.x = x;
	s.y = y;
	s.z = z;
	s.seq = e.seq;
	s.posFrame = m_frameId;
	s.fresh = true;   // F9's clock starts again at the next noted frame (a batch drained after a long draw is not old)
}

// A unit event taken in mode exe (after the allocator): kind 12 opens or continues a walk; 13 / 30 snap the unit to its
// plot and end a walk (the controller's place); 11 a fresh unit at its plot; 14 forgets it; 32 forgets every unit.
void UnitsCore::posEvent(const SrvFeed::Event& e, const std::map<int64_t, int32_t>& oldPlots)
{
	const int64_t key = UnitKeyOf(e.a[0], e.a[1]);
	switch(e.kind)
	{
	case SrvFeed::kUnitMoved:
	{
		m_pc.moves++;
		PosState& s = m_pos[key];
		if(!s.walking)
		{
			m_pc.walks++;
			closeLedger(s, "next", e.seq);
			openLedger(key, s, SrvFeed::kUnitMoved, e.seq);
			s.walking = true;
			s.walkHasPos = false;
			auto op = oldPlots.find(key);
			s.fromPlot = op != oldPlots.end() ? op->second : -1;
			s.posFrame = s.moveFrame = m_frameId;   // the quiet rule counts from the kind 12 until the first position
			s.fresh = true;
		}
		else if(s.ledger >= 0 && (size_t)s.ledger < m_ledger.size())
			m_ledger[(size_t)s.ledger].moves++;
		s.moveSeq = e.seq;
		break;
	}
	case SrvFeed::kUnitTeleported:
	case SrvFeed::kUnitRebased:
	{
		PosState& s = m_pos[key];
		s.have = false;
		if(s.walking)
		{
			m_pc.placeEnds++;
			endWalk(key, s, "place", e.seq, false);
		}
		closeLedger(s, "next", e.seq);
		openLedger(key, s, e.kind, e.seq);
		break;
	}
	case SrvFeed::kUnitCreated:
	{
		auto it = m_pos.find(key);
		if(it != m_pos.end())
		{
			if(it->second.walking)
				m_pc.removeEnds++;
			endWalk(key, it->second, "created", e.seq, false);
			closeLedger(it->second, "created", e.seq);
			m_pos.erase(it);
		}
		break;
	}
	case SrvFeed::kUnitDestroyed:
	{
		auto it = m_pos.find(key);
		if(it != m_pos.end())
		{
			if(it->second.walking)
				m_pc.removeEnds++;
			endWalk(key, it->second, "removed", e.seq, false);
			closeLedger(it->second, "removed", e.seq);
			m_pos.erase(it);
		}
		break;
	}
	case SrvFeed::kLoadBegin:
		for(auto& kv : m_pos)
		{
			if(kv.second.walking)
				m_pc.loadEnds++;
			endWalk(kv.first, kv.second, "load", e.seq, false);
			closeLedger(kv.second, "load", e.seq);
		}
		m_pos.clear();
		m_pc.loadsCleared++;
		break;
	default: break;
	}
}

CombatNote UnitsCore::combatNoteOf(const SrvFeed::CombatRecord& r)
{
	CombatNote n;
	n.attacker.owner = r.attackerUnit >= 0 ? (int8_t)r.attackerOwner : (int8_t)-1;
	n.attacker.id = r.attackerUnit >= 0 ? r.attackerUnit : -1;
	n.defender.owner = r.defenderUnit >= 0 ? (int8_t)r.defenderOwner : (int8_t)-1;
	n.defender.id = r.defenderUnit >= 0 ? r.defenderUnit : -1;
	n.visualize = (r.flags & SrvFeed::kCombatVisualize) != 0;
	const bool read = (r.flags & SrvFeed::kCombatGameRead) != 0;
	if(!read)
		m_c.combatGameUnread++;
	// the human's turn: the active player's IsTurnActive at the combat (the active player is the human in a single-player
	// game; 4452's kCombatActiveTurn); unread flags hold nothing (feed order)
	n.humanTurn = read && (r.flags & SrvFeed::kCombatActiveTurn) != 0;
	n.quickCombat = (r.flags & SrvFeed::kCombatQuickOption) != 0;
	n.cls = (r.flags & (SrvFeed::kCombatRanged | SrvFeed::kCombatBombing | SrvFeed::kCombatAirSweep)) ? HoldClass::kRanged
	                                                                                                   : HoldClass::kMelee;
	return n;
}

PlayerArt UnitsCore::playerArt(int32_t player) const
{
	PlayerArt a;
	if(player < 0 || player >= 64 || m_players[(size_t)player].player != player)
	{
		a.why = "no player record";
		return a;
	}
	const SrvFeed::PlayerRecord& p = m_players[(size_t)player];
	a.have = true;
	a.in.player = player;
	auto c = m_look.colours.find(p.colour);
	if(c != m_look.colours.end())
	{
		a.colourOk = true;
		a.in.colour = c->second;
	}
	else
		a.why = "colour index " + std::to_string(p.colour) + " has no PlayerColors row";
	// art style (premise A1): a major civilization's or the barbarians' Civilizations.Type; a city-state (flags bit 0):
	// its MinorCivilizations.Type, which the feed puts in `civilization` for a minor civilization (srv_feed.h
	// PlayerRecord, issue 4460; 0041's resolver takes a minor civilization Type, its ArtStyleSuffix: an ArtStyleType alone
	// does not fix the suffix, unit-server-scene.md 3.2)
	std::string style;
	bool styleOk;
	if(p.flags & 1)
	{
		auto s = m_look.minorCivs.find(p.civilization);
		styleOk = s != m_look.minorCivs.end();
		if(styleOk)
			style = s->second;
		else if(a.why.empty())
			a.why = "minor civilization index " + std::to_string(p.civilization) + " has no MinorCivilizations row";
	}
	else
	{
		auto s = m_look.civs.find(p.civilization);
		styleOk = s != m_look.civs.end();
		if(styleOk)
			style = s->second;
		else if(a.why.empty())
			a.why = "civilization index " + std::to_string(p.civilization) + " has no Civilizations row";
	}
	auto e = m_look.eras.find(p.era);
	const bool eraOk = e != m_look.eras.end();
	if(!eraOk && a.why.empty())
		a.why = "era index " + std::to_string(p.era) + " has no Eras row";
	a.artOk = styleOk && eraOk;
	if(a.artOk)
	{
		a.in.art_style = style;
		a.in.era = e->second;
	}
	return a;
}

bool UnitsCore::artChanges(const SrvFeed::UnitRecord& u, int oldEra, int newEra)
{
	const PlayerArt pa = playerArt(u.owner);
	auto t = m_look.units.find(u.unitType);
	auto eo = m_look.eras.find(oldEra), en = m_look.eras.find(newEra);
	if(!m_art || !pa.artOk || t == m_look.units.end() || eo == m_look.eras.end() || en == m_look.eras.end())
	{
		note(Status::kPlayerArtUnknown, "", "era change %d -> %d of unit (%d, %d): art rows cannot be compared (%s): taken as unchanged", oldEra,
		     newEra, u.owner, u.id, !m_art ? "no database yet" : pa.artOk ? "type or era index unknown" : pa.why.c_str());
		return false;
	}
	std::string ro, rn;
	const cu::UnitStatus so = m_art->art_row(t->second, pa.in.art_style, eo->second, ro);
	const cu::UnitStatus sn = m_art->art_row(t->second, pa.in.art_style, en->second, rn);
	if(!so.ok() || !sn.ok())
	{
		note(Status::kPlayerArtUnknown, "", "era change of unit (%d, %d) %s: %s / %s: taken as unchanged", u.owner, u.id, t->second.c_str(),
		     so.str().c_str(), sn.str().c_str());
		return false;
	}
	return ro != rn;
}

bool UnitsCore::seedOf(int8_t owner, int32_t id, uint32_t& seed) const
{
	UnitKey k;
	k.owner = owner;
	k.id = id;
	return m_alloc->SeedOf(k, seed);
}

bool UnitsCore::record(int32_t owner, int32_t id, SrvFeed::UnitRecord& out) const
{
	auto it = std::lower_bound(m_units.begin(), m_units.end(), std::make_pair(owner, id),
	                           [](const SrvFeed::UnitRecord& r, const std::pair<int32_t, int32_t>& k) {
		                           return r.owner != k.first ? r.owner < k.first : r.id < k.second;
	                           });
	if(it == m_units.end() || it->owner != owner || it->id != id)
		return false;
	out = *it;
	return true;
}

bool UnitsCore::embarkedRowOf(const SrvFeed::UnitRecord& u, std::string& row, std::string& why) const
{
	if(u.owner < 0 || u.owner >= 64 || m_players[(size_t)u.owner].player != u.owner)
	{
		why = "no player record";
		return false;
	}
	const SrvFeed::PlayerRecord& p = m_players[(size_t)u.owner];
	// the override is a major civilization's (CvPlayer::init's traits); a city-state's record carries its minor
	// civilization index there (flags bit 0) and has none
	const int32_t civ = (p.flags & 1) ? -1 : p.civilization;
	// issue 0439: the inputs the feed read at the unit's last embark, when it carries them
	auto eb = m_embarkArt.find(UnitKeyOf(u.owner, u.id));
	const uint32_t bits = eb != m_embarkArt.end() ? eb->second : 0u;
	if(!EmbarkedRow(m_look, civ, p.era, bits, row, why))
		return false;
	if(m_art && !m_art->has_row(row))
	{
		// the EXE falls back to the era's row when the override names no art define [C]; the era rows must exist
		const bool overridden = (bits & SrvFeed::kEmbarkArtRead) ? (bits & SrvFeed::kEmbarkArtOverride) != 0
		                                                           : civ >= 0 && m_look.embarkOverride.count(civ) != 0;
		if(overridden && EmbarkedRow(m_look, -1, p.era, bits & ~(uint32_t)SrvFeed::kEmbarkArtOverride, row, why) &&
		   m_art->has_row(row))
			return true;
		why = "embarked art row " + row + " has no ArtDefine_UnitInfos row";
		return false;
	}
	return true;
}

bool UnitsCore::inputOf(const SrvFeed::UnitRecord& u, cu::UnitInput& ui) const
{
	const PlayerArt pa = playerArt(u.owner);
	auto t = m_look.units.find(u.unitType);
	if(!pa.have || !pa.colourOk || !pa.artOk || t == m_look.units.end() || !m_width || !m_height || u.plot < 0 ||
	   (uint32_t)u.plot >= m_width * m_height)
		return false;
	std::string row, why;
	if((u.flags & cu::kFlagEmbarked) && !embarkedRowOf(u, row, why))
		return false;   // issue 4469: an embarked unit without its embarked row is not given to the controller either
	ui = cu::UnitInput();
	ui.player = u.owner;
	ui.id = u.id;
	ui.unit_type = t->second;
	ui.art_row = row;   // issue 4469: the embarked row for an embarked unit, else "" (the module resolves the land row)
	ui.plot_x = u.plot % (int32_t)m_width;
	ui.plot_y = u.plot / (int32_t)m_width;
	ui.has_root = false;
	ui.facing = u.facing;
	uint32_t seed = 0;
	ui.formation_seed = seedOf(u.owner, u.id, seed) ? seed : 0;
	ui.damage = u.damage;
	ui.max_hp = u.maxHP;
	ui.alive = -1;
	ui.flags = u.flags;
	return true;
}

void UnitsCore::build(const CullCamera* cam, const c5r::heightfield::HeightGrid* grid, double tMs, BuiltFrame& out, int64_t frameId)
{
	if(!m_frameNoted)
		noteFrame(frameId);   // issue 4755
	m_frameNoted = false;
	buildList(cam, grid, tMs, out);
}

void UnitsCore::noteFrame(int64_t frameId)
{
	// issue 4755: the frame's id (the stamp of what the next batches take) and F9's rule on the open walks
	if(frameId < 0)
		frameId = m_haveFrame ? m_frameId + 1 : 0;
	m_frameId = frameId;
	m_haveFrame = true;
	m_frameNoted = true;
	if(m_posExe)
		for(auto& kv : m_pos)
		{
			PosState& s = kv.second;
			if(s.fresh)
			{
				s.posFrame = frameId;   // taken since the last noted frame: drawn first in this one
				s.fresh = false;
			}
			if(!s.walking)
				continue;
			if(frameId < s.posFrame)
				s.posFrame = frameId;   // the frame ids went back (a new composite): count again from here
			const int64_t quiet = frameId - s.posFrame, bound = s.walkHasPos ? kWalkQuietFrames : kWalkStartQuietFrames;
			if(quiet <= bound)
				continue;
			m_pc.quietEnds++;
			posLine(false, "F9: the walk of unit (%d, %d) (kind 12 seq %u) ended by the rule: %s for %lld drawn frames at frame %lld: arrive, the "
			        "unit at its plot (%llu so far)",
			        (int32_t)(kv.first >> 32), (int32_t)(uint32_t)kv.first, s.moveSeq, s.walkHasPos ? "no position" : "no first position",
			        (long long)quiet, (long long)frameId, (unsigned long long)m_pc.quietEnds);
			s.have = false;
			endWalk(kv.first, s, "quiet", 0, true);
		}
}

void UnitsCore::buildList(const CullCamera* cam, const c5r::heightfield::HeightGrid* grid, double tMs, BuiltFrame& out)
{
	m_alloc->Advance(tMs / 1000.0);
	for(cu::UnitScene& s : out.copies)
	{
		s = cu::UnitScene();
		s.water_level = grid ? grid->waterZ : 0.0f;
		if(m_posExe)
			s.position_source = "exe";
	}
	out.listed = out.culled = out.notDrawn = 0;
	out.fading = false;
	out.xforms.clear();
	m_c.frames++;
	std::set<int32_t> used[3];
	const bool cull = cam && cam->valid && grid;
	const bool wrap = cull && std::isfinite(cam->wrap) && cam->wrap > 0;
	const bool xOn = m_extras && m_extras->anyOn();   // issue 4981: the unit extras
	// Ticket 5056 (unit-server-scene.md 20): each owner's PlayerArt once per call, on its first use. m_players and m_look
	// do not change inside buildList, so the value is the one playerArt gave for every listed unit before (two strings
	// copied and three lookups per unit: 35 % of this function on the huge save, 733 listed units a frame).
	PlayerArt artCache[64];
	bool artCached[64] = {};
	PlayerArt artOutside;   // an owner outside 0..63: not cached (playerArt answers "no player record")
	auto artOf = [&](int32_t player) -> const PlayerArt& {
		if(player < 0 || player >= 64)
		{
			artOutside = playerArt(player);
			return artOutside;
		}
		if(!artCached[player])
		{
			artCache[player] = playerArt(player);
			artCached[player] = true;
		}
		return artCache[player];
	};
	if(xOn)
	{
		UnitExtrasFrame xf;
		xf.grid = grid;
		xf.units = &m_units;
		xf.players = &m_players;
		xf.width = m_width;
		xf.height = m_height;
		xf.tMs = tMs;
		xf.frameId = m_frameId;
		xf.airInterceptRange = &m_look.airInterceptRange;   // issue 4986 (H1)
		xf.airInterceptPromotions = m_look.airInterceptPromotionsWhy.empty() ? &m_look.airInterceptPromotions : nullptr;   // MC #11389
		m_extras->beginFrame(xf);
	}
	for(const SrvFeed::UnitRecord& u : m_units)
	{
		const int64_t key = UnitKeyOf(u.owner, u.id);
		auto fi = m_fades.find(key);
		Fade none;
		float from, to, p;
		bool running;
		const bool stockListed =
		    fadeState(fi == m_fades.end() ? none : fi->second, (u.flags & cu::kFlagVisible) != 0, tMs, from, to, p, running);
		UnitOverride ov;
		if(xOn && !extraOverride(u, key, stockListed, ov))
			ov = UnitOverride();
		if(!stockListed && !ov.show)
			continue;
		if(ov.show)
		{
			from = 1;   // opaque at rest, as a visible unit with no fade (fadeState's (1, 0, 0))
			to = 0;
			p = 0;
			running = false;
		}
		out.fading |= running;
		out.listed++;
		// ticket 5056: the note key built only for a note (was a string built for every listed unit)
		auto ks = [&u]() { return std::to_string(u.owner) + "," + std::to_string(u.id); };
		const PlayerArt& pa = artOf(u.owner);
		if(!pa.have)
		{
			note(Status::kPlayerUnknown, std::to_string(u.owner), "player %d (owner of unit %d) has no player record: its units not drawn", u.owner,
			     u.id);
			out.notDrawn++;
			continue;
		}
		if(!pa.colourOk)
		{
			note(Status::kColourUnknown, std::to_string(u.owner), "player %d: %s: its units not drawn", u.owner, pa.why.c_str());
			out.notDrawn++;
			continue;
		}
		if(!pa.artOk)
		{
			note(Status::kPlayerArtUnknown, std::to_string(u.owner), "player %d: %s: its units not drawn", u.owner, pa.why.c_str());
			out.notDrawn++;
			continue;
		}
		auto t = m_look.units.find(u.unitType);
		if(t == m_look.units.end())
		{
			note(Status::kUnitTypeUnknown, std::to_string(u.unitType), "unit type index %d (unit %d of player %d) has no Units row: not drawn",
			     u.unitType, u.id, u.owner);
			out.notDrawn++;
			continue;
		}
		if(!m_width || !m_height || u.plot < 0 || (uint32_t)u.plot >= m_width * m_height)
		{
			note(Status::kPlotUnknown, ks(), "unit (%d, %d) plot %d outside the feed's grid %ux%u: not drawn", u.owner, u.id, u.plot, m_width,
			     m_height);
			out.notDrawn++;
			continue;
		}
		std::string embRow;
		if(u.flags & cu::kFlagEmbarked)
		{
			// issue 4469: an embarked unit is drawn with the embarked art (EmbarkedRow), never its land art
			std::string why;
			if(!embarkedRowOf(u, embRow, why))
			{
				note(Status::kPlayerArtUnknown, "embark " + std::to_string(u.owner) + " " + why,
				     "embarked unit (%d, %d) %s: %s: not drawn", u.owner, u.id, t->second.c_str(), why.c_str());
				out.notDrawn++;
				continue;
			}
		}
		uint32_t seed = 0;
		if(!seedOf(u.owner, u.id, seed))
		{
			note(Status::kSeedMissing, ks(), "unit (%d, %d) %s: the allocator holds no seed: not drawn", u.owner, u.id, t->second.c_str());
			out.notDrawn++;
			continue;
		}
		int32_t px, py;
		bool hasRoot = false;
		float rx = 0, ry = 0, rz = 0;
		if(sceneRoot(u, key, px, py, hasRoot, rx, ry, rz))   // issue 4755 (mode exe); a function since issue 4981
			m_pc.holdFrames++;
		// issue 4981: the extras' root, after the scene's own (the hook's input); epic 0042's per-figure combat offsets (4776)
		// come after the hook, in the unit's own frame (extras-units.md 4981.1)
		if(ov.root)
		{
			hasRoot = true;
			rx = ov.rootX;
			ry = ov.rootY;
			rz = 0;   // not read by the placement (heights sampled at the slots)
		}
		const float grow = ov.scale > 1 ? ov.scale : 1.0f;   // issue 4981: a scaled unit's cull box grows with it
		bool inCopy[3] = {false, !cull, false};
		if(cull)
		{
			float cx = rx, cy = ry;
			const c5r::Status pc = hasRoot ? c5r::Status::Ok() : cu::plotCentre(*grid, px, py, cx, cy);
			if(!pc.ok())
			{
				note(Status::kPlotUnknown, ks(), "unit (%d, %d) plot (%d, %d): %s: not drawn", u.owner, u.id, px, py, pc.str().c_str());
				out.notDrawn++;
				continue;
			}
			for(int k = wrap ? -1 : 0; k <= (wrap ? 1 : 0); ++k)
			{
				const float x = cx + (float)k * cam->wrap, r = kCullRadius * grow;
				inCopy[k + 1] = boxInView(cam->vp, x - r, cy - r, 0.0f, x + r, cy + r, kCullTop * grow);
			}
		}
		if(!inCopy[0] && !inCopy[1] && !inCopy[2])
		{
			out.culled++;
			continue;
		}
		cu::UnitInput ui;
		ui.player = u.owner;
		ui.id = u.id;
		ui.unit_type = t->second;
		ui.art_row = embRow;   // "": the module resolves it from the owner's era and style (note 3.2; premise A2);
		                       // issue 4469: an embarked unit's embarked row
		ui.plot_x = px;
		ui.plot_y = py;
		ui.has_root = hasRoot;   // position source own (decision 5): false; exe (issue 4755): the newest position record
		ui.root_x = rx;
		ui.root_y = ry;
		ui.root_z = rz;
		ui.foot_radius = ov.footRadius;   // issue 4982 (FixUnitPlacement); 0 without an extra
		ui.facing = u.facing;
		ui.formation_seed = seed;
		ui.damage = u.damage;
		ui.max_hp = u.maxHP;
		ui.alive = -1;
		ui.flags = u.flags;
		ui.fade_from = from;
		ui.fade_to = to;
		ui.fade = p;
		for(int c = 0; c < 3; ++c)
			if(inCopy[c])
			{
				out.copies[c].units.push_back(ui);
				used[c].insert(u.owner);
			}
		if(xOn)   // issue 4981: the drawn unit's extras, counted once per unit-frame; turn and scale after prepare
		{
			m_c.xShown += ov.show ? 1 : 0;
			m_c.xRoot += ov.root ? 1 : 0;
			m_c.xHeading += ov.heading ? 1 : 0;
			m_c.xScaled += ov.scale != 1.0f ? 1 : 0;
			m_c.xFoot += ov.footRadius != 0.0f ? 1 : 0;
			if(ov.heading || ov.scale != 1.0f || ov.lift != 0.0f)
			{
				UnitXform& x = out.xforms[key];
				x.heading = ov.heading;
				x.yaw = ov.yaw;
				x.scale = ov.scale;
				x.lift = ov.lift;   // issue 4986 (H2)
			}
		}
	}
	for(int c = 0; c < 3; ++c)
		for(int32_t pl : used[c])
			out.copies[c].players.push_back(artOf(pl).in);   // ticket 5056: the cached art
	m_c.listed += out.listed;
	m_c.culled += out.culled;
}

bool UnitsCore::sceneRoot(const SrvFeed::UnitRecord& u, int64_t key, int32_t& px, int32_t& py, bool& hasRoot, float& rx, float& ry,
                          float& rz) const
{
	const bool plotOk = m_width && m_height && u.plot >= 0 && (uint32_t)u.plot < m_width * m_height;
	px = plotOk ? u.plot % (int32_t)m_width : -1;
	py = plotOk ? u.plot / (int32_t)m_width : -1;
	hasRoot = false;
	rx = ry = rz = 0;
	if(!m_posExe)
		return false;
	auto ps = m_pos.find(key);
	if(ps == m_pos.end())
		return false;
	const PosState& s = ps->second;
	if(s.have)
	{
		hasRoot = true;
		rx = s.x;
		ry = s.y;
		rz = s.z;
		return false;
	}
	if(s.walking && !s.walkHasPos && s.fromPlot >= 0 && m_width && (uint32_t)s.fromPlot < m_width * m_height)
	{
		px = s.fromPlot % (int32_t)m_width;
		py = s.fromPlot / (int32_t)m_width;
		return true;
	}
	return false;
}

// ---- issue 4981: the units seam (extras-units.md section 4981) --------------------------------------------------------
void UnitsCore::setHeldInFight(int32_t owner, int32_t id, bool on)
{
	if(on)
		m_fightHeld.insert(UnitKeyOf(owner, id));
	else
		m_fightHeld.erase(UnitKeyOf(owner, id));
}

bool UnitsCore::heldInFight(int32_t owner, int32_t id) const { return m_fightHeld.count(UnitKeyOf(owner, id)) != 0; }

static bool finiteF(float v) { return std::isfinite(v) != 0; }

bool UnitsCore::extraOverride(const SrvFeed::UnitRecord& u, int64_t key, bool stockListed, UnitOverride& ov)
{
	UnitExtraUnit xu;
	xu.rec = &u;
	xu.listed = stockListed;
	xu.hiding = (u.flags & cu::kFlagVisible) == 0;
	float rz = 0;
	sceneRoot(u, key, xu.plotX, xu.plotY, xu.hasRoot, xu.rootX, xu.rootY, rz);
	xu.facing = u.facing;
	xu.heldInFight = m_fightHeld.count(key) != 0;
	m_extras->unit(xu, ov);
	const bool any = ov.show || ov.root || ov.heading || ov.scale != 1.0f || ov.lift != 0.0f || ov.footRadius != 0.0f;
	if(!any)
		return true;
	if(xu.heldInFight)
	{
		m_c.xHeld++;   // epic 0042's contract: an extra never moves a fighter
		return false;
	}
	if((ov.root && (!finiteF(ov.rootX) || !finiteF(ov.rootY))) || (ov.heading && !finiteF(ov.yaw)) || !finiteF(ov.scale) ||
	   ov.scale <= 0 || ov.scale > kMaxUnitExtraScale || !finiteF(ov.lift) || ov.lift < 0 || ov.lift > kMaxUnitExtraLift ||
	   !finiteF(ov.footRadius) || ov.footRadius < 0 || ov.footRadius > kMaxUnitExtraFootRadius)
	{
		if(m_c.xBad++ == 0 && m_log)
			m_log("units: LOUD: a unit extra's override is outside its ranges: ignored for that unit (counted x_bad; the extras module "
			      "should have switched that extra off)");
		return false;
	}
	if(ov.show && !xu.hiding)
		ov.show = false;   // stock lists it already: nothing to show
	if(ov.show)
	{
		// the seam's own refusals: a dead unit; an invisible unit of another team than the active one (never revealed)
		const bool dead = (u.flags & cu::kFlagDead) != 0;
		bool otherInvisible = false;
		if(u.flags & cu::kFlagInvisible)
		{
			const bool row = u.owner >= 0 && u.owner < 64 && m_players[(size_t)u.owner].player == u.owner;
			otherInvisible = !row || m_players[(size_t)u.owner].team != m_players[(size_t)u.owner].activeTeam;
		}
		if(dead || otherInvisible)
		{
			m_c.xShowRefused++;
			ov.show = false;
		}
	}
	return true;
}

size_t ApplyUnitXforms(cu::FramePlan& plan, const std::map<int64_t, UnitXform>& xforms)
{
	if(xforms.empty())
		return 0;
	size_t changed = 0;
	for(cu::UnitPlan& up : plan.units)
	{
		if(!up.status.ok() || up.placement.placed.empty())
			continue;
		auto it = xforms.find(UnitKeyOf(up.player, up.id));
		if(it == xforms.end())
			continue;
		const UnitXform& x = it->second;
		const float s = x.scale;
		const float d = x.heading ? x.yaw - up.placement.yaw : 0.0f;
		const float lift = x.lift;   // issue 4986 (H2): a translation in z after the turn and the scale
		if(s == 1.0f && d == 0.0f && lift == 0.0f)
			continue;
		// the pivot: the placement's root, z the mean of its living figures' z (figures on flat ground keep their feet there)
		double zs = 0;
		for(const cu::FigurePlacement& f : up.placement.placed)
			zs += f.z;
		const float P[3] = {up.placement.rootX, up.placement.rootY, (float)(zs / (double)up.placement.placed.size())};
		const float c = std::cos(d), sn = std::sin(d);
		// L = s Rz(d): world point w -> P + L (w - P); a 3x4 row triple [A | b] (w = A v + b) -> [L A | P + L (b - P)]
		const float L[3][3] = {{s * c, -s * sn, 0}, {s * sn, s * c, 0}, {0, 0, s}};
		auto apply = [&](cu::Float4* r) {
			cu::Float4 o[3];
			for(int i = 0; i < 3; ++i)
			{
				const float a0 = L[i][0] * r[0].x + L[i][1] * r[1].x + L[i][2] * r[2].x;
				const float a1 = L[i][0] * r[0].y + L[i][1] * r[1].y + L[i][2] * r[2].y;
				const float a2 = L[i][0] * r[0].z + L[i][1] * r[1].z + L[i][2] * r[2].z;
				const float b = P[i] + L[i][0] * (r[0].w - P[0]) + L[i][1] * (r[1].w - P[1]) + L[i][2] * (r[2].w - P[2]) +
				                (i == 2 ? lift : 0.0f);
				o[i] = {a0, a1, a2, b};
			}
			r[0] = o[0];
			r[1] = o[1];
			r[2] = o[2];
		};
		for(uint32_t b = up.first_block; b < up.first_block + up.blocks; ++b)
		{
			const size_t base = (size_t)b * cu::kPaletteRows;
			if(base + cu::kPaletteRows > plan.palette.size())
				break;
			for(uint32_t k = 0; k < cu::kPaletteBones; ++k)
				apply(&plan.palette[base + 2 + 3 * k]);   // rows 0 colour, 1 fade, then 32 bones x 3
		}
		for(cu::MeshDraw& md : up.draws)
			for(size_t i = 0; i + 16 <= md.cb0.size(); i += 16)
				apply(&md.cb0[i]);   // material 87: rows 0-2 the world rows (draw.cpp staticCb0)
		// epic 0042 (4776): its per-figure combat offsets, in the unit's own frame, come after this (extras-units.md 4981.1)
		changed++;
	}
	return changed;
}

void UnitsCore::afterPrepare(const cu::UnitScene& sc, const cu::FramePlan& plan)
{
	for(size_t i = 0; i < plan.units.size(); ++i)
	{
		const cu::UnitPlan& u = plan.units[i];
		const std::string type = i < sc.units.size() ? sc.units[i].unit_type : std::string("?");
		if(u.status.ok())
			m_c.drawnUnits++;
		else
			note(Status::kUnitRefused, type + "|" + cu::unitCodeName(u.status.code), "unit (%d, %d) %s: %s: not drawn (logged once per type and code)",
			     u.player, u.id, type.c_str(), u.status.str().c_str());
		for(const std::string& w : u.status.warnings)
			if(m_once.insert("warning|" + w).second && m_log)
				m_log(("units: warning (unit drawn): " + type + ": " + w).c_str());
	}
}

void UnitsCore::frameFailed(const std::string& why)
{
	note(Status::kFrameFailed, "", "frame: %s: no units drawn this frame", why.c_str());
}

}  // namespace srvunits
