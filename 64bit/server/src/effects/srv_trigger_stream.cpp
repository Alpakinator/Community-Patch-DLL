// srv_trigger_stream.cpp - the trigger stream as one object for a host (ticket 4722; srv_trigger_stream.h).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_trigger_stream.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cwctype>

#include "c5r/effects/projectile_model.h"   // 4728: loadInstallEffects
#include "c5r/units/placement.h"

namespace srveffects {

namespace {

void findScripts(const std::wstring& dir, std::vector<std::wstring>& out)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		const std::wstring n = fd.cFileName;
		if(n == L"." || n == L"..")
			continue;
		const std::wstring p = dir + L"\\" + n;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			findScripts(p, out);
		else if(n.size() > 13)
		{
			std::wstring tail = n.substr(n.size() - 13);
			for(wchar_t& c : tail)
				c = (wchar_t)std::towlower(c);
			if(tail == L"3dscripts.xml")
				out.push_back(p);
		}
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}

bool readFile(const std::wstring& p, std::string& out)
{
	FILE* f = _wfopen(p.c_str(), L"rb");
	if(!f)
		return false;
	char buf[65536];
	size_t n;
	out.clear();
	while((n = fread(buf, 1, sizeof buf, f)) > 0)
		out.append(buf, n);
	fclose(f);
	return true;
}

}  // namespace

size_t loadScriptTable(const std::wstring& install, c5m::TableSource* db, ScriptTable& out)
{
	std::vector<std::wstring> files;
	if(!install.empty())
		findScripts(install + L"\\Assets", files);
	std::stable_sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
		const bool sa = a.find(L"\\Assets\\Sounds\\") != std::wstring::npos, sb = b.find(L"\\Assets\\Sounds\\") != std::wstring::npos;
		return sa != sb ? sa : a < b;
	});
	size_t rows = 0;
	for(const std::wstring& p : files)
	{
		std::string text;
		if(readFile(p, text))
			rows += out.loadXml(text);
	}
	if(db)
	{
		std::vector<c5m::DbRow> r;
		c5m::Error e;
		if(db->rows("Audio_3DSounds", {"ScriptID", "Looping"}, r, &e))
			for(auto& row : r)
				if(!row[0].null && !row[0].text.empty())
				{
					std::string l = row[1].null ? std::string() : row[1].text;
					for(char& ch : l)
						ch = (char)std::tolower((unsigned char)ch);
					out.set(row[0].text, l == "1" || l == "true");
					++rows;
				}
	}
	return rows;
}

bool loadPlotTags(c5m::TableSource& db, PlotTags& out, std::string& why)
{
	why.clear();
	for(int k = 0; k < 2; ++k)
	{
		const char* table = k ? "Features" : "Terrains";
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		if(!db.rows(table, {"ID", "EffectTypeTag"}, rows, &e))
		{
			why = std::string(table) + ": " + e.what;
			return false;
		}
		std::map<int32_t, std::string>& m = k ? out.feature : out.terrain;
		for(auto& r : rows)
			if(!r[0].null)
				m[(int32_t)std::atoi(r[0].text.c_str())] = r[1].null ? std::string() : r[1].text;
	}
	return true;
}

// $(WEAPON) from the member's Index-0 weapon row (unit-combat-server.md 11.3), the figure's member from the controller;
// 5052 (W, strikerWeapon): a unit that does not strike in its running fight takes its opponent's figure 0 instead;
// 5055 (strikerMelee): for a Sound every unit struck in its running fight does, and its own is the emitter's second try.
// 5058 (cityEra): a Sound whose striker is a city takes the city's weapon by its owner's era (CityWeaponTag), its own the
// second try; an unknown era keeps its own as before.
// $(TERRAIN) from the host, else DIRT (counted).
class TriggerStream::Subs : public Substitutions
{
public:
	struct Weapon
	{
		std::string tag, sound;
	};
	std::map<std::string, Weapon> weapons;
	c5r::unitanim::CombatArtTables tables;
	const c5r::unitanim::Controller* ctl = nullptr;
	std::map<std::pair<int32_t, int32_t>, c5r::unitanim::CombatUnitFigures> cache;   // this frame's
	UnitTerrainFn* terrainFn = nullptr;
	uint64_t standIn = 0;
	bool strikerWeapon = false;   // 5052 (W)
	bool strikerMelee = false;    // 5055 (with strikerWeapon)
	bool cityEra = false;         // 5058 (with strikerWeapon)
	bool plotTerrain = false;     // 5052 (T): only for the counters (the host's function does the lookup)
	FightWeaponRoles roles;       // 5052 (W): the running fights (fightArt adds, the fights' OwnerEnds remove)
	NameCounters n;
	bool terrain(int32_t player, int32_t unit, std::string& out) override
	{
		if(terrainFn && *terrainFn && (*terrainFn)(player, unit, out) && !out.empty())
			return true;
		++standIn;
		if(plotTerrain)
			++n.plotNoTable;
		out = "DIRT";
		return true;
	}
	const c5r::unitanim::CombatUnitFigures* figuresOf(int32_t player, int32_t unit)
	{
		if(!ctl)
			return nullptr;
		const auto key = std::make_pair(player, unit);
		auto it = cache.find(key);
		if(it == cache.end())
		{
			c5r::unitanim::CombatUnitFigures f;
			if(!ctl->combatFigures(player, unit, tables, f).ok())
				return nullptr;
			it = cache.emplace(key, std::move(f)).first;
		}
		return &it->second;
	}
	bool isAir(int32_t p, int32_t u)
	{
		const c5r::unitanim::CombatUnitFigures* f = figuresOf(p, u);
		if(f)
			for(const c5r::unitanim::CombatMemberArt& m : f->members)
				if(m.domain == "Air")
					return true;
		return false;
	}
	// The figure's member's weapon row: the sound tag for a Sound (else the weapon tag), the weapon tag for an Effect.
	// 0: no such figure; -1: its member has no weapon row; 1: out set.
	int memberWeapon(const c5r::unitanim::CombatUnitFigures& src, int32_t figure, TrigType type, std::string& out) const
	{
		int32_t at = 0;
		for(const c5r::unitanim::CombatMemberArt& m : src.members)
		{
			if(figure >= at && figure < at + m.count)
			{
				auto w = weapons.find(m.member);
				if(w == weapons.end())
					return -1;
				out = type == TrigType::Sound && !w->second.sound.empty() ? w->second.sound : w->second.tag;
				return 1;
			}
			at += m.count;
		}
		return 0;
	}
	bool weapon(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec, std::string& out) override
	{
		(void)ec;
		const c5r::unitanim::CombatUnitFigures* src = figuresOf(player, unit);
		if(!src)
			return false;
		bool fromOpponent = false;
		if(strikerWeapon && roles.size())   // 5052 (W)
		{
			auto air = [this](int32_t p, int32_t u) { return isAir(p, u); };
			int32_t op = -1, ou = -1, era = -1;
			switch(roles.namer(player, unit, air, op, ou, strikerMelee && type == TrigType::Sound, &era))   // 5055: struck
			{
			case FightWeaponRoles::Namer::Opponent:
				if(const c5r::unitanim::CombatUnitFigures* of = figuresOf(op, ou))
				{
					src = of;
					figure = 0;   // stock: the other participant's figure 0
					fromOpponent = true;
				}
				break;
			case FightWeaponRoles::Namer::OpponentCity:
				if(cityEra && type == TrigType::Sound && era >= 0)   // 5058: the striking city's weapon by its owner's era
				{
					out = CityWeaponTag(era);
					++n.strikerCityEra;
					return true;
				}
				++n.strikerCity;   // a city has no figure: the unit's own, as before
				break;
			default:
				break;
			}
		}
		const int r = memberWeapon(*src, figure, type, out);
		if(r < 0 && fromOpponent)
			++n.strikerNoWeapon;
		if(r > 0 && fromOpponent)
			++n.strikerNamed;
		return r > 0;
	}
	// 5055: the figure's own weapon when weapon() gave its striker's for this Sound (strikerMelee only: 5052's rule has no
	// second try)
	bool weaponFallback(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec, std::string& out) override
	{
		(void)ec;
		if(!strikerWeapon || (!strikerMelee && !cityEra) || type != TrigType::Sound || !roles.size())
			return false;
		auto air = [this](int32_t p, int32_t u) { return isAir(p, u); };
		int32_t op = -1, ou = -1, era = -1;
		const FightWeaponRoles::Namer nm = roles.namer(player, unit, air, op, ou, strikerMelee, &era);
		const bool striker = strikerMelee && nm == FightWeaponRoles::Namer::Opponent && figuresOf(op, ou);
		const bool city = cityEra && nm == FightWeaponRoles::Namer::OpponentCity && era >= 0;   // 5058
		if(!striker && !city)
			return false;
		const c5r::unitanim::CombatUnitFigures* src = figuresOf(player, unit);
		if(!src || memberWeapon(*src, figure, type, out) <= 0)
			return false;
		++n.strikerFallback;
		return true;
	}
};

TriggerStream::TriggerStream() {}
TriggerStream::~TriggerStream()
{
	std::string why;
	close(why);
}

bool TriggerStream::open(const TriggerStreamConfig& cfg, c5m::TableSource& db, std::string& why)
{
	why.clear();
	if(em_)
	{
		why = "the trigger stream is already open";
		return false;
	}
	loadScriptTable(cfg.install, &db, scripts_);
	if(scripts_.empty())
	{
		why = "no 3D script under the install's Assets nor in Audio_3DSounds: no loop flag known";
		return false;
	}
	subs_.reset(new Subs());
	cfg_ = cfg;   // 5052
	subs_->strikerWeapon = cfg.strikerWeapon;
	subs_->strikerMelee = cfg.strikerWeapon && cfg.strikerMelee;   // 5055
	subs_->cityEra = cfg.strikerWeapon && cfg.cityEra;             // 5058
	subs_->plotTerrain = cfg.plotTerrain;
	plotTags_ = PlotTags();
	plotTagsWhy_.clear();
	if(cfg.plotTerrain && !loadPlotTags(db, plotTags_, plotTagsWhy_))
		plotTags_ = PlotTags();   // every plot DIRT (counted plotDirt); the host says plotTagsWhy() LOUD
	{
		std::vector<c5m::DbRow> rows;
		c5m::Error e;
		if(!db.rows("ArtDefine_UnitMemberCombatWeapons", {"UnitMemberType", "Index", "WeaponTypeTag", "WeaponTypeSoundOverrideTag"},
		            rows, &e))
		{
			why = "ArtDefine_UnitMemberCombatWeapons: " + e.what;
			return false;
		}
		for(auto& r : rows)
			if(!r[0].null && !r[1].null && r[1].text == "0")
				subs_->weapons[r[0].text] = Subs::Weapon{r[2].null ? "" : r[2].text, r[3].null ? "" : r[3].text};
	}
	if(!c5r::unitanim::loadCombatTables(db, subs_->tables, why))
		return false;
	subs_->terrainFn = &terrain_;
	// 4728: the positions are the fight geometry's (a fighting figure's pose and bones; else the unit's root as before)
	geo_.reset(new FightGeometry());
	effectsWhy_.clear();
	bool effects = cfg.effects;
	{
		std::string gw;
		if(!geo_->open(db, gw))
		{
			effectsWhy_ = "the fight geometry's tables: " + gw;   // positions still served; no routes (no flight planner)
			effects = false;
		}
	}
	geo_->setUnitRoot([this](int32_t player, int32_t unit, float out[3]) { return root_ && root_(player, unit, out); });
	geo_->setSubstitutions(subs_.get());
	geo_->setGrid(grid_);
	geo_->setProjectilePose(cfg.routes.projectilePose);   // 5064
	TriggerSink* sink = nullptr;
	if(!cfg.section.empty())
	{
		sec_.reset(new SectionSink());
		if(!sec_->open(cfg.section, why))
		{
			sec_.reset();
			return false;
		}
		sec_->setRequireRouteS(cfg.requireRouteS);   // ticket 4783: R9, the question asked now
		sink = sec_.get();
	}
	if(!cfg.logPath.empty())
	{
		log_.reset(new LogSink());
		if(!log_->open(cfg.logPath, why))
		{
			log_.reset();
			return false;
		}
		if(sink)
		{
			tee_.reset(new TeeSink(*sec_, *log_));
			sink = tee_.get();
		}
		else
			sink = log_.get();
	}
	if(!sink)
	{
		why = "neither a section nor a log";
		return false;
	}
	em_.reset(new TriggerEmitter(*sink, cfg.params));
	em_->setScripts(&scripts_);
	em_->setSubstitutions(subs_.get());
	em_->setPositions(geo_.get());
	tap_.reset(new AnimTriggerTap(*em_));
	Subs* subs = subs_.get();
	FightGeometry* geo = geo_.get();
	tap_->beforeFrame = [subs, geo](const c5r::unitanim::Controller& c) {
		subs->ctl = &c;
		subs->cache.clear();
		geo->beginFrame(c);   // 4728: this frame's pose programs
	};
	// 4728: the fights' art, speeds, targets and poses; the ends drop them
	tap_->onFightArt = [this](const srvunits::Fight& f, c5r::unitanim::Controller& c, c5r::unitanim::ArtSource& art,
	                          c5r::unitanim::CombatArtTables& tables) { fightArt(f, c, art, tables); };
	tap_->onFightRun = [geo](uint32_t seq, const c5r::unitanim::CombatResult& r,
	                         const std::shared_ptr<const c5r::unitanim::FightMotion>& m, double elapsed) { geo->fightRun(seq, r, m, elapsed); };
	tap_->afterFrame = [geo, subs](const std::vector<srvunits::OwnerEnd>& ends) {
		geo->ownerEnds(ends);
		for(const srvunits::OwnerEnd& e : ends)   // 5052 (W): the fight's own end drops its roles (after its records)
			if(e.why != srvunits::FightEnd::ClipEnded)
				subs->roles.ended(e.seq);
	};
	if(effects && (!cfg.files || !cfg.provider))
	{
		effectsWhy_ = "no install files given to the trigger stream";
		effects = false;
	}
	if(effects)
	{
		std::string ew;
		if(!c5r::effects::loadInstallEffects(*cfg.files, *cfg.provider, fxlib_, ew))
			effectsWhy_ = "the install's effect definitions: " + ew;
		else
		{
			names_.reset(new c5r::effects::EffectNames(fxlib_));
			geo_->setNames(names_.get());
			if(cfg.bones)
				geo_->setBoneSource(cfg.provider, cfg.strings);
			routes_.reset(new EffectRoutes(*em_, *names_, cfg.routes));
			routes_->setSubstitutions(subs_.get());
			routes_->setPositions(geo_.get());
			routes_->setScripts(&scripts_);
			auto files = [this](const std::string& file) -> const c5m::TriggerSystem* {
				const c5m::TriggerSystem* ts = nullptr;
				return ctl_ && ctl_->logic() && ctl_->logic()->triggers(file, ts).ok() ? ts : nullptr;
			};
			routes_->setTriggerFiles(files);
			geo_->setTriggerFiles(files);
			EffectRoutes* routes = routes_.get();
			routes_->setFlightPlanner([geo, routes](const TriggerIn& t, const std::string& effect, const float from[3],
			                                        c5r::effects::ProjectileSpec& out, std::string& why) {
				return geo->plan(t, effect, from, routes->now(), out, why);
			});
			tap_->setRoutes(routes_.get());
		}
	}
	return true;
}

void TriggerStream::pollRouteS(std::vector<std::string>& lines)
{
	if(!sec_)
		return;
	sec_->pollRouteS();
	std::string line;
	while(sec_->takeLine(line))
		lines.push_back(line);
}

size_t TriggerStream::fightArt(const srvunits::Fight& f, c5r::unitanim::Controller& c, c5r::unitanim::ArtSource& art,
                               c5r::unitanim::CombatArtTables& tables)
{
	ctl_ = &c;
	if(subs_ && cfg_.strikerWeapon)   // 5052 (W): who strikes in this fight
	{
		FightWeaponRoles::Fight r;
		r.seq = f.seq;
		r.attPlayer = f.attacker.owner;
		r.attUnit = f.attacker.city >= 0 ? -1 : f.attacker.unit;
		r.attCity = f.attacker.city;
		r.defPlayer = f.defender.owner;
		r.defUnit = f.defender.city >= 0 ? -1 : f.defender.unit;
		r.retaliates = (f.rec.flags & SrvFeed::kCombatDefenderRetaliates) != 0;
		r.attackerDamage = SrvFeed::CombatDamage(f.rec, 0);
		if(subs_->cityEra)   // 5058: a city side's owner era from its player record (-1: no core or no record)
		{
			r.attCityEra = f.attacker.city >= 0 ? ownerEra(f.attacker.owner) : -1;
			r.defCityEra = f.defender.city >= 0 ? ownerEra(f.defender.owner) : -1;
		}
		subs_->roles.started(r);
	}
	if(!geo_)
		return 0;
	if(core_)
		geo_->setWidth(core_->width());   // the map's width of this load
	geo_->fightStarted(f);
	std::map<std::string, const c5m::TriggerSystem*> todo;
	for(const srvunits::FightSide* s : {&f.attacker, &f.defender})
	{
		if(s->unit < 0)
			continue;
		const c5r::units::UnitInput* u = c.unitInput(s->owner, s->unit);
		if(!u)
			continue;
		c5r::units::PlayerInput pin;
		const c5r::units::PlayerInput* owner = nullptr;
		if(core_)
		{
			const srvunits::PlayerArt pa = core_->playerArt(s->owner);
			if(pa.have && pa.artOk)
			{
				pin = pa.in;
				owner = &pin;
			}
		}
		std::shared_ptr<c5r::units::UnitArt> ua(new c5r::units::UnitArt());
		if(!art.resolve(*u, owner, *ua).ok())
		{
			++geo_->counters().artFails;
			continue;
		}
		geo_->unitArt(s->owner, s->unit, ua);
		if(!names_)
			continue;
		for(const c5r::units::MemberArt& m : ua->members)
		{
			if(!speedDone_.insert(m.member).second)
				continue;
			// the first of the member's trigger files that hands an effect over (a Transfer row): applyEffectSpeeds
			for(const c5m::ResolvedTrigger& rt : m.files.timed_triggers)
			{
				const c5m::TriggerSystem* ts = nullptr;
				if(!c.logic() || !c.logic()->triggers(c5m::basename_lower(rt.file.name), ts).ok() || !ts)
					continue;
				bool transfer = false;
				for(const c5m::FtsTrigger& t : ts->triggers)
					transfer = transfer || t.type == c5m::TriggerType::Transfer;
				if(transfer)
				{
					todo[m.member] = ts;
					break;
				}
			}
		}
	}
	if(todo.empty() || !names_)
		return 0;
	const size_t n = c5r::effects::applyEffectSpeeds(tables, todo, *names_);
	geo_->counters().speedMembers += todo.size();
	geo_->counters().speedWeapons += n;
	return n;
}

bool TriggerStream::close(std::string& why)
{
	why.clear();
	bool ok = true;
	if(sec_)
		sec_->stop();
	if(log_ && log_->isOpen())
		ok = log_->close(why);
	return ok;
}

int32_t TriggerStream::ownerEra(int32_t owner) const
{
	if(!core_ || owner < 0)
		return -1;
	const std::vector<SrvFeed::PlayerRecord>& ps = core_->players();
	if((size_t)owner >= ps.size() || ps[owner].player != owner || ps[owner].era < 0)
		return -1;
	return ps[owner].era;
}

void TriggerStream::useCore(const srvunits::UnitsCore* core)
{
	core_ = core;   // 4728: the owners' art inputs, the map's width
	if(geo_ && core)
		geo_->setWidth(core->width());
	const c5r::heightfield::HeightGrid* const* grid = &grid_;
	root_ = [core, grid](int32_t player, int32_t unit, float out[3]) {
		if(!core)
			return false;
		if(core->rootOf(player, unit, out[0], out[1], out[2]))
			return true;   // mode exe: the newest position
		SrvFeed::UnitRecord r;
		if(!*grid || !core->record(player, unit, r) || r.plot < 0 || core->width() == 0)
			return false;
		const int32_t x = r.plot % (int32_t)core->width(), y = r.plot / (int32_t)core->width();
		if(!c5r::units::plotCentre(**grid, x, y, out[0], out[1]).ok())
			return false;
		out[2] = 0;   // the plot's height is not sampled [I]: 3D audio's distance from the camera dominates
		return true;
	};
}

uint64_t TriggerStream::terrainStandIn() const
{
	return subs_ ? subs_->standIn : 0;
}

bool TriggerStream::plotTag(int32_t terrainId, int32_t featureId, std::string& out)
{
	const PlotTags::From from = plotTags_.tagOf(terrainId, featureId, out);
	if(subs_)
	{
		NameCounters& n = subs_->n;
		++(from == PlotTags::From::Feature ? n.plotFeature : from == PlotTags::From::Terrain ? n.plotTerrain : n.plotDirt);
	}
	return true;
}

NameCounters TriggerStream::nameCounters() const
{
	return subs_ ? subs_->n : NameCounters();
}

}  // namespace srveffects
