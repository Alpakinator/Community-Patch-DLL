// srv_fight_geometry.cpp - a fight's figures, bones and flights for the trigger stream (ticket 4728; srv_fight_geometry.h).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/srv_fight_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "c5r/units/placement.h"

namespace srveffects {

namespace {

float attrFloat(const c5m::FtsTrigger& t, const char* name, float def, bool& found)
{
	auto it = t.attrs.find(name);
	if(it == t.attrs.end() || it->second.empty())
		return def;
	char* end = nullptr;
	const double v = std::strtod(it->second.c_str(), &end);
	if(end == it->second.c_str() || !std::isfinite(v))
		return def;
	found = true;
	return (float)v;
}

}  // namespace

bool FightGeometry::open(c5m::TableSource& db, std::string& why)
{
	why.clear();
	if(!c5r::effects::loadProjectileWeapons(db, weapons_, why))
		return false;
	std::vector<c5m::DbRow> rows;
	c5m::Error e;
	if(!db.rows("ArtDefine_UnitMemberCombats", {"UnitMemberType", "TargetRadius", "TargetHeight", "MoveRate"}, rows, &e))
	{
		why = "ArtDefine_UnitMemberCombats: " + e.what;
		return false;
	}
	for(const c5m::DbRow& r : rows)
	{
		if(r.size() != 4 || r[0].null)
			continue;
		auto f = [](const c5m::DbValue& v, float def) {
			const double d = v.null || v.text.empty() ? 0.0 : std::atof(v.text.c_str());
			return d > 0 && d < 1e6 ? (float)d : def;
		};
		MemberRow m;
		m.targetRadius = f(r[1], c5r::effects::kDefaultTargetRadius);
		m.targetHeight = r[2].null ? 0.0f : (float)std::atof(r[2].text.c_str());
		if(!std::isfinite(m.targetHeight))
			m.targetHeight = 0;
		m.moveRate = f(r[3], 0.78125f);
		rows_[r[0].text] = m;
	}
	return true;
}

void FightGeometry::setBoneSource(c5m::FileProvider* files, const c5m::StringDatabase* strings)
{
	poser_.reset(files ? new c5r::effects::BonePoser(*files, strings) : nullptr);
}

void FightGeometry::beginFrame(const c5r::unitanim::Controller& c)
{
	programs_.clear();
	if(fights_.empty())
		return;
	for(const c5r::unitanim::FigureProgram& p : c.programs())
		if(unitFight_.count(std::make_pair(p.player, p.id)))
			programs_[FigKey(p.player, p.id, (int32_t)p.figure)] = &p;
}

void FightGeometry::fightStarted(const srvunits::Fight& f)
{
	FightState s;
	s.att = f.attacker;
	s.def = f.defender;
	s.plot = f.rec.plot;
	fights_[f.seq] = s;
	if(f.attacker.unit >= 0)
		unitFight_[std::make_pair(f.attacker.owner, f.attacker.unit)] = f.seq;
	if(f.defender.unit >= 0)
		unitFight_[std::make_pair(f.defender.owner, f.defender.unit)] = f.seq;
}

void FightGeometry::unitArt(int32_t player, int32_t unit, std::shared_ptr<const c5r::units::UnitArt> art)
{
	if(art)
	{
		arts_[std::make_pair(player, unit)] = std::move(art);
		++c_.arts;
	}
}

const c5r::units::UnitArt* FightGeometry::artOf(int32_t player, int32_t unit) const
{
	auto it = arts_.find(std::make_pair(player, unit));
	return it == arts_.end() ? nullptr : it->second.get();
}

void FightGeometry::fightRun(uint32_t seq, const c5r::unitanim::CombatResult& r,
                             const std::shared_ptr<const c5r::unitanim::FightMotion>& m, double elapsed)
{
	auto it = fights_.find(seq);
	if(it == fights_.end())
		return;
	FightState& s = it->second;
	s.motion = m;
	s.elapsed = elapsed;
	for(size_t k = s.steps; k < r.steps.size(); ++k)
	{
		const c5r::unitanim::FigureStep& st = r.steps[k];
		if(st.target_player < 0)
			continue;   // no target named: the figure keeps its last one
		Target t;
		t.player = st.target_player;
		t.unit = st.target_id;
		t.figure = st.target_figure;
		s.targets[FigKey(st.player, st.id, (int32_t)st.figure)] = t;
	}
	s.steps = r.steps.size();
}

void FightGeometry::ownerEnds(const std::vector<srvunits::OwnerEnd>& ends)
{
	programs_.clear();   // pointers into the controller's programs: this frame's only
	for(const srvunits::OwnerEnd& e : ends)
	{
		if(e.why == srvunits::FightEnd::ClipEnded)
			continue;
		auto it = fights_.find(e.seq);
		if(it == fights_.end())
			continue;
		for(const srvunits::FightSide* sd : {&it->second.att, &it->second.def})
		{
			if(sd->unit < 0)
				continue;
			const auto key = std::make_pair(sd->owner, sd->unit);
			auto u = unitFight_.find(key);
			if(u != unitFight_.end() && u->second == e.seq)
			{
				unitFight_.erase(u);
				arts_.erase(key);
			}
		}
		fights_.erase(it);
	}
}

const FightGeometry::FightState* FightGeometry::fightOf(int32_t player, int32_t unit) const
{
	auto u = unitFight_.find(std::make_pair(player, unit));
	if(u == unitFight_.end())
		return nullptr;
	auto f = fights_.find(u->second);
	return f == fights_.end() ? nullptr : &f->second;
}

const c5r::units::MemberArt* FightGeometry::memberOf(int32_t player, int32_t unit, int32_t figure) const
{
	const c5r::units::UnitArt* a = artOf(player, unit);
	if(!a || figure < 0)
		return nullptr;
	int32_t at = 0;
	for(const c5r::units::MemberArt& m : a->members)
	{
		if(figure < at + m.count)
			return &m;
		at += m.count;
	}
	return nullptr;
}

float FightGeometry::ground(float x, float y) const
{
	if(!grid_)
		return 0;
	return (std::max)(c5r::units::heightAtWorld(*grid_, x, y), grid_->waterZ);
}

bool FightGeometry::frameOf(int32_t player, int32_t unit, int32_t figure, c5r::effects::FigureFrame& out)
{
	++c_.figureAsks;
	const FightState* f = fightOf(player, unit);
	c5r::unitanim::FigurePose p;
	if(!f || !f->motion || figure < 0 || !f->motion->pose(f->elapsed, player, unit, (uint16_t)figure, p))
	{
		++c_.figureNoMotion;
		return false;
	}
	const c5r::units::MemberArt* m = memberOf(player, unit, figure);
	out.x = p.x;
	out.y = p.y;
	// placement.h's z (the ground or the water, plus the member's ZOffset), plus 4726's height of an aircraft's flight
	out.z = ground(p.x, p.y) + (m && std::isfinite(m->zoffset) ? m->zoffset : 0.0f) + (std::isfinite(p.z) ? p.z : 0.0f);
	out.yaw = p.yaw;
	out.scale = m && std::isfinite(m->scale) && m->scale > 0 ? m->scale : 1.0f;
	lastYawKnown_ = p.yaw_known;   // 5064: false: p.yaw is 0, not the heading the figure is drawn at
	if(!p.yaw_known)
		++c_.figureYawUnknown;
	++c_.figurePosed;
	return true;
}

bool FightGeometry::figure(int32_t player, int32_t unit, int32_t figure, float out[3])
{
	c5r::effects::FigureFrame f;
	if(!frameOf(player, unit, figure, f))
		return false;
	out[0] = f.x;
	out[1] = f.y;
	out[2] = f.z;
	return true;
}

bool FightGeometry::unit(int32_t player, int32_t unit, float out[3])
{
	return root_ && root_(player, unit, out);
}

float FightGeometry::figureScale(int32_t player, int32_t unit, int32_t figure)
{
	const c5r::units::MemberArt* m = memberOf(player, unit, figure);
	return m && std::isfinite(m->scale) && m->scale > 0 ? m->scale : 1.0f;
}

bool FightGeometry::boneTransform(int32_t player, int32_t unit, int32_t figure, const std::string& bone, float pos[3],
                                  float orient[4])
{
	++c_.boneAsks;
	if(!poser_ || bone.empty())
		return false;
	c5r::effects::FigureFrame fr;
	if(!frameOf(player, unit, figure, fr))
	{
		++c_.boneNoFigure;
		return false;
	}
	const c5r::units::MemberArt* m = memberOf(player, unit, figure);
	if(!m)
	{
		++c_.boneNoArt;
		return false;
	}
	auto p = programs_.find(FigKey(player, unit, figure));
	if(p == programs_.end() || !p->second)
	{
		++c_.boneNoProgram;
		return false;
	}
	std::string why;
	if(!poser_->boneWorld(*m, p->second->program, bone, fr, pos, orient, &why))
	{
		++c_.boneRefused;
		return false;
	}
	++c_.boneOk;
	const double dx = pos[0] - fr.x, dy = pos[1] - fr.y, dz = pos[2] - fr.z;
	const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
	c_.boneDistSum += d;
	c_.boneDistMax = (std::max)(c_.boneDistMax, d);
	c_.boneHeightMin = (std::min)(c_.boneHeightMin, dz);
	c_.boneHeightMax = (std::max)(c_.boneHeightMax, dz);
	return true;
}

bool FightGeometry::bone(int32_t player, int32_t unit, int32_t figure, const std::string& bone, float out[3])
{
	float q[4];
	return boneTransform(player, unit, figure, bone, out, q);
}

bool FightGeometry::cityCentre(const FightState& f, float out[3]) const
{
	if(!grid_ || f.plot < 0 || width_ == 0)
		return false;
	float x = 0, y = 0;
	if(!c5r::units::plotCentre(*grid_, f.plot % (int32_t)width_, f.plot / (int32_t)width_, x, y).ok())
		return false;
	out[0] = x;
	out[1] = y;
	out[2] = ground(x, y);
	return true;
}

bool FightGeometry::plan(const TriggerIn& t, const std::string& effect, const float from[3], double now,
                         c5r::effects::ProjectileSpec& out, std::string& why)
{
	using namespace c5r::effects;
	++c_.plans;
	const FightState* f = fightOf(t.player, t.unit);
	if(!f)
	{
		++c_.planNoTarget;
		why = "no running fight of the striker";
		return false;
	}
	// the firing member and its weapon row
	const c5r::units::MemberArt* fm = memberOf(t.player, t.unit, t.figure);
	if(!fm)
	{
		++c_.planNoArt;
		why = "the striker's member is not known";
		return false;
	}
	auto wr = weapons_.find(fm->member);
	const ProjectileWeapon* w = wr == weapons_.end() ? nullptr : pickWeapon(wr->second, effect, 0);
	if(!w)
	{
		++c_.planNoWeapon;
		why = fm->member + ": no weapon row";
		return false;
	}
	LaunchRequest rq;
	rq.weapon = w;
	rq.launch = V3{from[0], from[1], from[2]};
	// the Transfer's tangent in the figure's frame (its trigger row; (0, -1, 0) when not written)
	bool tan = false;
	if(files_)
		if(const c5m::TriggerSystem* ts = files_(t.file))
			for(const c5m::FtsTrigger& r : ts->triggers)
				if(r.type == c5m::TriggerType::Transfer && r.id == t.trigger)
				{
					bool fx = false, fy = false, fz = false;
					rq.tangent_local = V3{attrFloat(r, "tangent.x", 0, fx), attrFloat(r, "tangent.y", -1, fy), attrFloat(r, "tangent.z", 0, fz)};
					tan = fx || fy || fz;
					break;
				}
	if(!tan)
		++c_.planNoTangent;
	FigureFrame sf;
	float yaw = 0;
	bool yawKnown = true;   // 5064: false when the firing figure's motion gives no heading (yaw 0 is then no heading)
	if(frameOf(t.player, t.unit, t.figure, sf))
	{
		yaw = sf.yaw;
		yawKnown = lastYawKnown_;
		if(!yawKnown)
			++c_.planYawUnknown;
	}
	auto setFigureWorld = [&rq, from](float y) {
		const float cy = std::cos(y), sy = std::sin(y);
		rq.figure_world.m[0] = cy;
		rq.figure_world.m[1] = sy;
		rq.figure_world.m[4] = -sy;
		rq.figure_world.m[5] = cy;
		rq.figure_world.m[12] = from[0];
		rq.figure_world.m[13] = from[1];
		rq.figure_world.m[14] = from[2];
	};
	setFigureWorld(yaw);
	rq.attacker_pos = rq.launch;
	rq.attacker_heading = yaw;
	{
		auto mr = rows_.find(fm->member);
		if(mr != rows_.end())
			rq.attacker_move_rate = mr->second.moveRate;
	}
	rq.water_level = grid_ ? grid_->waterZ : 0.0f;
	rq.effect_movement_speed = names_ ? names_->movementSpeed(effect) : 0.0f;
	rq.miss = false;   // the core decides hit or miss at the landing [I]
	// the core's target: the striking figure's last FigureStep target, at its pose now; else the other side
	Target tg;
	auto st = f->targets.find(FigKey(t.player, t.unit, t.figure));
	const bool attacker = f->att.owner == t.player && f->att.unit == t.unit;
	const srvunits::FightSide& other = attacker ? f->def : f->att;
	if(st != f->targets.end())
	{
		tg = st->second;
		++c_.planStepTarget;
	}
	else
	{
		tg.player = other.owner;
		tg.unit = other.unit;
		tg.figure = -1;
		++c_.planOpponent;
	}
	bool city = tg.unit < 0;
	c5r::effects::NameValues tv;
	if(city)
	{
		float c[3];
		if(!cityCentre(*f, c))
		{
			++c_.planNoTarget;
			why = "the city target has no centre (no grid or plot)";
			return false;
		}
		rq.target = TargetKind::Participant;
		rq.target_pos = V3{c[0], c[1], c[2]};
		tv.set_terrain("CITY").set_base_terrain("CITY");
		++c_.planCity;
	}
	else
	{
		float p[3];
		FigureFrame tf;
		if(tg.figure >= 0 && frameOf(tg.player, tg.unit, tg.figure, tf))
		{
			p[0] = tf.x;
			p[1] = tf.y;
			p[2] = tf.z;
		}
		else if(!(root_ && root_(tg.player, tg.unit, p)))
		{
			++c_.planNoTarget;
			why = "the target has no position";
			return false;
		}
		else
			p[2] = ground(p[0], p[1]);
		rq.target = TargetKind::Figure;
		rq.target_pos = V3{p[0], p[1], p[2]};
		if(const c5r::units::MemberArt* tm = memberOf(tg.player, tg.unit, tg.figure >= 0 ? tg.figure : 0))
		{
			auto mr = rows_.find(tm->member);
			if(mr != rows_.end())
			{
				rq.target_radius = mr->second.targetRadius;
				rq.target_height = mr->second.targetHeight;
				rq.target_move_rate = mr->second.moveRate;
			}
			if(tm->domain == "Air")
				rq.target = TargetKind::FlyingFigure;
		}
		std::string s;
		if(subs_ && subs_->terrain(tg.player, tg.unit, s) && !s.empty())
			tv.set_terrain(s).set_base_terrain(s);
	}
	if(projectilePose_ && !yawKnown && rq.target != TargetKind::FlyingFigure)
	{
		// 5064 (C3): no heading from the motion: the figure faces its target (as a standing figure with a target does,
		// motion.cpp FightMotion::pose), not yaw 0's fixed world direction
		const float dx = rq.target_pos.x - from[0], dy = rq.target_pos.y - from[1];
		if(dx != 0 || dy != 0)
		{
			yaw = c5r::unitanim::yawFacing(dx, dy);
			setFigureWorld(yaw);
			rq.attacker_heading = yaw;
			++c_.planYawToTarget;
		}
	}
	LaunchPlan lp;
	HeightFn height;
	if(grid_)
		height = [this](float x, float y) { return ground(x, y); };
	if(!planLaunch(rq, rng_, height, lp, &why))
	{
		++c_.planRefused;
		return false;
	}
	out.path = lp.path;
	out.t0 = now;
	out.effect = effect;
	out.scale = std::isfinite(fm->scale) && fm->scale > 0 ? fm->scale : 1.0f;   // the firing member's Scale (effects-server.md 2.4 [L])
	out.hit = true;
	out.children.clear();
	out.hit_effect.clear();
	if(names_)
	{
		names_->children(effect, tv, out.children);
		out.child_scale = w->child_scale;
		std::string hn;
		const NameResult r = w->hit_effect.empty() ? NameResult::NotDefined : names_->resolve(w->hit_effect, tv, hn);
		if(r == NameResult::Exact || r == NameResult::Fallback)
		{
			out.hit_effect = hn;
			out.hit_scale = c5r::effects::hitScaleOf(*w, out.scale);   // NULL: the firing member's (15.7.8)
		}
	}
	++c_.planned;
	return true;
}

}  // namespace srveffects
