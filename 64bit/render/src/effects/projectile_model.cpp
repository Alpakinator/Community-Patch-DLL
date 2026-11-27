// projectile_model.cpp - option A of ticket 4792: the projectile's rigid model through the unit renderer's material 87
// path (c5r/effects/projectile_model.h). Off by default (lead-0042 #7986).
#include "c5r/effects/projectile_model.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iterator>

#include "c5models/art.h"
#include "c5r/gamefs/civ5pkg.h"

namespace c5r { namespace effects {

namespace {
const float kDeg = 3.14159265358979323846f / 180.0f;
M4 rotX(float a) {
    M4 m;
    const float c = std::cos(a), s = std::sin(a);
    m.m[5] = c; m.m[6] = s; m.m[9] = -s; m.m[10] = c;
    return m;
}
M4 rotY(float a) {
    M4 m;
    const float c = std::cos(a), s = std::sin(a);
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
    return m;
}
M4 rotZ(float a) {
    M4 m;
    const float c = std::cos(a), s = std::sin(a);
    m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c;
    return m;
}
}  // namespace

bool loadInstallEffects(const gamefs::GameFiles& gf, c5m::FileProvider& files, c5m::EffectLibrary& lib,
                        std::string& err) {
    err.clear();
    std::vector<const gamefs::Civ5Pkg*> pkgs;
    for (const gamefs::Civ5Pkg& p : gf.packages()) pkgs.push_back(&p);
    auto lower = [](std::wstring s) {
        for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
        return s;
    };
    std::stable_sort(pkgs.begin(), pkgs.end(), [&](const gamefs::Civ5Pkg* a, const gamefs::Civ5Pkg* b) {
        return lower(a->folder) < lower(b->folder);
    });
    std::vector<std::vector<c5m::FxDefineFile>> lists;
    for (const gamefs::Civ5Pkg* p : pkgs) {
        std::ifstream in(p->file, std::ios::binary);
        if (!in) {
            err = "package not readable: " + gamefs::toUtf8(p->file);
            return false;
        }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<c5m::FxDefineFile> f;
        c5m::Error e;
        if (!c5m::parse_package_fx_defines(data.data(), data.size(), gamefs::toUtf8(p->file), f, &e)) {
            err = "package " + gamefs::toUtf8(p->file) + ": " + e.what;
            return false;
        }
        lists.push_back(f);
    }
    c5m::Error e;
    if (!lib.load(files, c5m::fx_load_list(lists), &e)) {
        err = "effect definitions: " + e.what;
        return false;
    }
    return true;
}

M4 effectLocal(const c5m::VisEffectDef& def) {
    M4 s;
    if (!def.has_transform) return s;
    s.m[0] = s.m[5] = s.m[10] = def.scale;
    M4 r = mul(mul(rotX(def.rotation_deg.x * kDeg), rotY(def.rotation_deg.y * kDeg)), rotZ(def.rotation_deg.z * kDeg));
    M4 out = mul(s, r);
    out.m[12] = def.translation.x;
    out.m[13] = def.translation.y;
    out.m[14] = def.translation.z;
    return out;
}

bool resolveProjectileModel(const c5m::EffectLibrary& lib, c5m::FileProvider& files, const std::string& effect,
                            ProjectileModelArt& out, std::string* why, const c5m::FxSubstitutions* subs) {
    out = ProjectileModelArt();
    out.effect = effect;
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    // the models do not depend on the children's surface; DIRT only lets the resolver reach the children
    c5m::FxSubstitutions dirt;
    dirt.set_terrain("DIRT").set_base_terrain("DIRT").set_weapon("");
    c5m::ResolvedEffect r;
    c5m::Error e;
    if (!lib.resolve(files, effect, subs ? *subs : dirt, r, &e)) return fail(effect + ": " + e.what);
    if (r.models.empty()) return fail(effect + ": no model effect");
    out.local = effectLocal(*r.def);
    out.problems = r.problems;
    for (const c5m::ResolvedFxModel& m : r.models) {
        if (!m.def) return fail(effect + ": model " + m.ref.name + " has no definition");
        units::MemberArt a;
        a.member = m.ref.name;
        a.count = 1;
        a.scale = m.def->scale > 0 ? m.def->scale : 1.0f;
        a.domain = "Air";
        a.model = m.def->file;
        if (!c5m::resolve_fxsxml(files, m.def->file, a.files, &e)) return fail(effect + ": " + e.what);
        out.parts.push_back(a);
    }
    return true;
}

units::UnitStatus appendProjectileDraws(units::ModelCache& cache, const std::map<std::string, ProjectileModelArt>& arts,
                                        const std::vector<ModelInstance>& instances, units::FramePlan& plan,
                                        size_t* skipped_meshes, size_t* skipped_instances) {
    const float white[4] = {1, 1, 1, 1};
    for (const ModelInstance& in : instances) {
        auto it = arts.find(in.effect);
        if (it == arts.end()) {
            if (skipped_instances) ++*skipped_instances;
            continue;
        }
        const ProjectileModelArt& art = it->second;
        M4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = in.scale;
        const M4 world = mul(mul(sc, art.local), in.world);
        units::Float4 rows[3];
        for (int i = 0; i < 3; ++i)   // [R | T] in the column form of yawRows: row i = column i of the row-vector matrix
            rows[i] = units::Float4{world.m[0 * 4 + i], world.m[1 * 4 + i], world.m[2 * 4 + i], world.m[3 * 4 + i]};
        units::UnitPlan up;
        up.player = -2;
        up.id = static_cast<int32_t>(in.handle);
        for (size_t p = 0; p < art.parts.size(); ++p) {
            const units::UnitModel* model = nullptr;
            units::UnitStatus st = cache.get(art.parts[p], model);
            if (!st.ok()) return st;
            for (const units::ModelMesh& mesh : model->meshes) {
                if (mesh.material != units::Material::Static) {
                    if (skipped_meshes) ++*skipped_meshes;
                    continue;
                }
                units::MeshDraw d;
                d.model = model;
                d.mesh = &mesh;
                d.material = units::Material::Static;
                d.member = static_cast<uint16_t>(p);
                units::Float4 cb[16];
                units::staticCb0(white, 1.0f, 0.0f, 0.0f, rows, cb);
                d.cb0.assign(cb, cb + 16);
                up.draws.push_back(d);
            }
        }
        plan.units.push_back(up);
    }
    return units::UnitStatus::Ok();
}

}}  // namespace c5r::effects
