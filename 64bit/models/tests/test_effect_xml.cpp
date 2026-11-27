// test_effect_xml.cpp - the effect definitions reader (ticket 4786): the four XML kinds, the package load list, the
// override order (last definition wins), the substitutions, bare-name file lookup and the refusals, each on built-in
// documents with negative cases; with the install (C5R_GAME_DIR, the render_game_dir setting, or C5M_FX_INSTALL)
// and an extraction of its Resource/Common archives (C5M_FX_EXTRACT: .psb .fxsxml .xml .ftsxml, made with
// research/05-graphics-composition/evidence/0320/extract_ext.py) the counts of effects-formats.md 0 and 3 checked
// against the real files. Nothing of the game's files is in this source (D83): the real part only counts and names.
#include "c5models/effect_xml.h"
#include "c5models/logic.h"
#include "testing.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

using namespace c5m;

namespace {

class FakeProvider : public FileProvider {
public:
    std::map<std::string, std::string> files;   // lower-case name -> text
    void put(const std::string& name, const std::string& text) { files[ascii_lower(name)] = text; }
    bool read(const std::string& n, Bytes& out, std::string* origin) override {
        auto it = files.find(n);
        if (it == files.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        if (origin) *origin = "fake:" + n;
        return true;
    }
};

std::string vis_entry(const std::string& name, const std::string& body) {
    return "<VisEffectArtInfo><Name>" + name + "</Name><Transform><Translation x=\"0\" y=\"0\" z=\"0\"/>"
           "<Rotation x=\"0\" y=\"0\" z=\"0\"/><Scale>1.0</Scale></Transform><fBaseDuration>1.0</fBaseDuration>"
           "<fBaseDurationScale>1.0</fBaseDurationScale>" + body + "</VisEffectArtInfo>";
}
std::string vis_file(const std::string& entries) {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<VisEffectArtInfos>" + entries + "</VisEffectArtInfos>";
}
std::string part_file(const std::string& entries) {
    return "<VisEffectParticleArtInfos>" + entries + "</VisEffectParticleArtInfos>";
}
std::string part_entry(const std::string& name, const std::string& file) {
    return "<VisEffectParticleArtInfo><Name>" + name + "</Name><File>" + file + "</File></VisEffectParticleArtInfo>";
}
std::string model_file(const std::string& entries) { return "<VisEffectModelArtInfos>" + entries + "</VisEffectModelArtInfos>"; }
std::string model_entry(const std::string& name, const std::string& file, const char* scale = "1.5") {
    return "<VisEffectModelArtInfo><Name>" + name + "</Name><File>" + file + "</File><fScale>" + scale +
           "</fScale><fScrollTime>2.0</fScrollTime></VisEffectModelArtInfo>";
}

void test_parse_vis() {
    std::string doc = vis_file(
        "<VisEffectArtInfo><Name>ART_DEF_VEFFECT_A</Name><!-- c --><FOWMask>3</FOWMask>"
        "<Transform><Translation x=\"1.5\" y=\"-2\" z=\"3\"/><Rotation x=\"90.0\" y=\"0\" z=\"45\"/><Scale>0.5</Scale>"
        "</Transform><fBaseDuration>0.7</fBaseDuration><fBaseDurationScale>2.0</fBaseDurationScale>"
        "<fMovementSpeed>12.5</fMovementSpeed>"
        "<ModelEffects><ModelEffect><Name>ART_DEF_MEFFECT_M</Name><Animation>1000</Animation></ModelEffect>"
        "<ModelEffect>\n </ModelEffect></ModelEffects>"
        "<ParticleEffects><ParticleEffect><Name>ART_DEF_PEFFECT_P1</Name></ParticleEffect><ParticleEffect></ParticleEffect>"
        "<ParticleEffect><Name>ART_DEF_PEFFECT_P2</Name></ParticleEffect></ParticleEffects>"
        "<ChildEffects><ChildEffect trigger=\"ProjectileEnd\"><Name>ART_DEF_VEFFECT_B_$(TERRAIN)</Name></ChildEffect></ChildEffects>"
        "<SoundEffects><SoundEffect><Name>AS3D_X</Name></SoundEffect></SoundEffects><Extra/></VisEffectArtInfo>" +
        vis_entry("ART_DEF_VEFFECT_B_DIRT", ""));
    std::vector<VisEffectDef> v;
    Error e;
    CHECK(parse_viseffects_xml(doc.data(), doc.size(), "a.xml", v, &e));
    CHECK_EQ(v.size(), (size_t)2);
    if (v.size() == 2) {
        const VisEffectDef& a = v[0];
        CHECK_EQ(a.name, std::string("ART_DEF_VEFFECT_A"));
        CHECK(a.fow_mask == 3 && a.fow_mask_written);
        CHECK(a.has_transform && a.translation.x == 1.5f && a.translation.y == -2.0f && a.translation.z == 3.0f);
        CHECK(a.rotation_deg.x == 90.0f && a.rotation_deg.z == 45.0f && a.scale == 0.5f);
        CHECK(a.base_duration == 0.7f && a.base_duration_scale == 2.0f);
        CHECK(a.has_movement_speed && a.movement_speed == 12.5f);
        CHECK_EQ(a.models.size(), (size_t)1);
        CHECK(!a.models.empty() && a.models[0].has_animation && a.models[0].animation == 1000);
        CHECK_EQ(a.particles.size(), (size_t)2);
        CHECK_EQ(a.empty_refs, 2);
        CHECK(a.children.size() == 1 && a.children[0].trigger == "ProjectileEnd" &&
              a.children[0].name == "ART_DEF_VEFFECT_B_$(TERRAIN)");
        CHECK(a.sounds.size() == 1 && a.sounds[0] == "AS3D_X");
        CHECK(a.unknown.count("Extra") == 1);
        CHECK(a.src.entry_index == 0 && a.src.file == "a.xml" && a.src.line == 2);
        CHECK(!v[1].fow_mask_written && v[1].fow_mask == 2 && v[1].src.entry_index == 1);
        // duration rule: animated model -> animation length x scale; else base x scale
        CHECK(EffectLibrary::duration(a, 3.0f) == 6.0f);
        CHECK(EffectLibrary::duration(a, -1.0f) == 1.4f);
        CHECK(EffectLibrary::duration(v[1], 3.0f) == 1.0f);
    }
    // negatives: each -> Err::Xml naming the file
    const std::string bad[] = {
        "<VisEffectArtInfos><VisEffectArtInfo><Name>X</Name>",                                    // not closed
        "<VisEffectParticleArtInfos/>",                                                           // wrong root
        vis_file("<VisEffectArtInfo><FOWMask>2</FOWMask></VisEffectArtInfo>"),                   // no Name
        vis_file(vis_entry("X", "<FOWMask>4</FOWMask>")),                                         // FOWMask range
        vis_file(vis_entry("X", "<FOWMask>two</FOWMask>")),                                       // FOWMask text
        vis_file("<VisEffectArtInfo><Name>X</Name><Transform><Scale>1.0x</Scale></Transform></VisEffectArtInfo>"),
        vis_file("<VisEffectArtInfo><Name>X</Name><Transform><Translation x=\"a\"/></Transform></VisEffectArtInfo>"),
        vis_file("<VisEffectArtInfo><Name>X</Name><fBaseDuration></fBaseDuration></VisEffectArtInfo>"),
        vis_file("<VisEffectArtInfo><Name>X</Name><fMovementSpeed>inf</fMovementSpeed></VisEffectArtInfo>"),
        vis_file(vis_entry("X", "<ModelEffects><ModelEffect><Name>M</Name><Animation>1.5</Animation></ModelEffect></ModelEffects>")),
        std::string("\xFF\xFE<\0V\0/\0>\0", 10),                                                  // UTF-16
        "",
    };
    for (auto& b : bad) {
        std::vector<VisEffectDef> bv;
        Error be;
        CHECK(!parse_viseffects_xml(b.data(), b.size(), "bad.xml", bv, &be));
        CHECK(be.code == Err::Xml);
        CHECK(be.what.find("bad.xml") != std::string::npos);
    }
}

void test_parse_others() {
    Error e;
    std::string p = part_file(part_entry("P1", "Assets\\Effects\\X\\FX_One.psb") +
                              "<VisEffectParticleArtInfo><Name>P2</Name><File>a/Two.psb</File><File>a/Two.psb</File>"
                              "</VisEffectParticleArtInfo>");
    std::vector<ParticleDef> pv;
    CHECK(parse_fx_particles_xml(p.data(), p.size(), "p.xml", pv, &e));
    CHECK(pv.size() == 2 && pv[0].file == "Assets\\Effects\\X\\FX_One.psb" && pv[1].extra_files == 1);
    std::string m = model_file(model_entry("M1", "Assets/fx_m.fxsxml", "0.25"));
    std::vector<ModelDef> mv;
    CHECK(parse_fx_models_xml(m.data(), m.size(), "m.xml", mv, &e));
    CHECK(mv.size() == 1 && mv[0].scale == 0.25f && mv[0].scroll_time == 2.0f);
    std::string ev = "<EventEffectCollection><EventEffect><EventName>GameplayFX</EventName><VisEffects><VisEffect>"
                     "<Name>ART_DEF_VEFFECT_A</Name></VisEffect></VisEffects><SoundEffects><SoundEffect>"
                     "<ScriptID>AS2D_X</ScriptID></SoundEffect></SoundEffects></EventEffect>"
                     "<EventEffect><EventName>OnlySound</EventName><SoundEffects><SoundEffect><ScriptID>S</ScriptID>"
                     "</SoundEffect></SoundEffects></EventEffect></EventEffectCollection>";
    std::vector<EventEffectDef> evv;
    CHECK(parse_event_effects_xml(ev.data(), ev.size(), "e.xml", evv, &e));
    CHECK(evv.size() == 2 && evv[0].event == "GameplayFX" && evv[0].vis_effects.size() == 1 && evv[0].sounds.size() == 1);
    CHECK(evv.size() == 2 && evv[1].vis_effects.empty());
    // negatives
    struct Bad {
        int kind;
        std::string doc;
    } bad[] = {
        {1, part_file("<VisEffectParticleArtInfo><Name>P</Name></VisEffectParticleArtInfo>")},   // no File
        {1, part_file("<VisEffectParticleArtInfo><File>x.psb</File></VisEffectParticleArtInfo>")},  // no Name
        {1, model_file("")},                                                                       // wrong root
        {2, model_file(model_entry("M", "m.fxsxml", "big"))},                                      // fScale text
        {2, part_file("")},                                                                        // wrong root
        {3, "<EventEffectCollection><EventEffect><VisEffects/></EventEffect></EventEffectCollection>"},   // no EventName
        {3, "<EventEffectCollection><EventEffect>"},                                               // not closed
    };
    for (auto& b : bad) {
        Error be;
        bool ok = true;
        if (b.kind == 1) {
            std::vector<ParticleDef> x;
            ok = parse_fx_particles_xml(b.doc.data(), b.doc.size(), "bad.xml", x, &be);
        } else if (b.kind == 2) {
            std::vector<ModelDef> x;
            ok = parse_fx_models_xml(b.doc.data(), b.doc.size(), "bad.xml", x, &be);
        } else {
            std::vector<EventEffectDef> x;
            ok = parse_event_effects_xml(b.doc.data(), b.doc.size(), "bad.xml", x, &be);
        }
        CHECK(!ok);
        CHECK(be.code == Err::Xml && be.what.find("bad.xml") != std::string::npos);
    }
}

void test_packages_and_list() {
    std::string pkg =
        "<Civ5Package><Priority>200</Priority><Gameplay><GameData>x.xml</GameData>"
        "<VisEffectInfoDefines>V_Pkg.xml</VisEffectInfoDefines><VisEffectParticleDefines>P_Pkg.xml</VisEffectParticleDefines>"
        "<VisEffectModelDefines/><VisEffectInfoDefines>V_Pkg_2.xml</VisEffectInfoDefines></Gameplay>"
        "<EventEffectDefines>E_Pkg.xml</EventEffectDefines></Civ5Package>";
    std::vector<FxDefineFile> a;
    Error e;
    CHECK(parse_package_fx_defines(pkg.data(), pkg.size(), "DLC/A/A.Civ5Pkg", a, &e));
    CHECK_EQ(a.size(), (size_t)4);   // the empty <VisEffectModelDefines/> is skipped
    if (a.size() == 4) {
        CHECK(a[0].kind == FxKind::VisEffect && a[0].name == "V_Pkg.xml" && a[0].package == "DLC/A/A.Civ5Pkg");
        CHECK(a[1].kind == FxKind::Particle && a[2].name == "V_Pkg_2.xml" && a[3].kind == FxKind::Event);
    }
    std::vector<FxDefineFile> b = {{FxKind::VisEffect, "V_B.xml", "B"}, {FxKind::Model, "M_B.xml", "B"}};
    std::vector<FxDefineFile> l = fx_load_list({a, b});
    // per kind: base, then packages in order, document order within a package
    std::vector<std::string> want = {"civ5artdefines_viseffects.xml", "V_Pkg.xml", "V_Pkg_2.xml", "V_B.xml",
                                      "civ5artdefines_viseffectparticles.xml", "P_Pkg.xml",
                                      "civ5artdefines_viseffectmodels.xml", "M_B.xml",
                                      "civ5artdefines_eventeffects.xml", "E_Pkg.xml"};
    CHECK_EQ(l.size(), want.size());
    for (size_t i = 0; i < l.size() && i < want.size(); ++i) CHECK_EQ(l[i].name, want[i]);
    // negatives
    std::vector<FxDefineFile> n;
    CHECK(!parse_package_fx_defines("<Package/>", 10, "p", n, &e) && e.code == Err::Xml);
    CHECK(!parse_package_fx_defines("<Civ5Package>", 13, "p", n, &e) && e.code == Err::Xml);
}

void test_substitution() {
    std::string out;
    Error e;
    FxSubstitutions s;
    s.set_terrain("DIRT").set_weapon("");
    CHECK(fx_substitute("ART_DEF_VEFFECT_X_IMPACT_$(TERRAIN)", s, out, &e) && out == "ART_DEF_VEFFECT_X_IMPACT_DIRT");
    CHECK(fx_substitute("ART_DEF_VEFFECT_ARMOR_DEATH_SM_$(WEAPON)", s, out, &e) && out == "ART_DEF_VEFFECT_ARMOR_DEATH_SM_");
    CHECK(fx_substitute("PLAIN", s, out, &e) && out == "PLAIN");
    CHECK(fx_substitute("$(TERRAIN)_$(TERRAIN)", s, out, &e) && out == "DIRT_DIRT");
    CHECK(!fx_substitute("A_$(BASETERRAIN)", s, out, &e) && e.code == Err::NotFound &&
          e.what.find("$(BASETERRAIN)") != std::string::npos);                       // no value given
    CHECK(!fx_substitute("A_$(COLOR)", s, out, &e) && e.code == Err::NotFound && e.what.find("COLOR") != std::string::npos);
    CHECK(!fx_substitute("A_$(TERRAIN", s, out, &e) && e.code == Err::Xml);
    std::vector<std::string> v = fx_variables("A_$(TERRAIN)_$(WEAPON)");
    CHECK(v.size() == 2 && v[0] == "TERRAIN" && v[1] == "WEAPON");
}

// A small synthetic install: base files, two packages that redefine names, a missing listed file.
void make_fake(FakeProvider& fp) {
    fp.put("civ5artdefines_viseffects.xml",
           vis_file(vis_entry("ART_DEF_VEFFECT_PROJ", "<ModelEffects><ModelEffect><Name>ART_DEF_MEFFECT_ARROW</Name>"
                                                      "</ModelEffect></ModelEffects><ParticleEffects><ParticleEffect>"
                                                      "<Name>ART_DEF_PEFFECT_TRAIL</Name></ParticleEffect></ParticleEffects>"
                                                      "<ChildEffects><ChildEffect trigger=\"ProjectileEnd\"><Name>"
                                                      "ART_DEF_VEFFECT_HIT_$(TERRAIN)</Name></ChildEffect></ChildEffects>") +
                    vis_entry("ART_DEF_VEFFECT_HIT_DIRT", "<FOWMask>3</FOWMask>") +
                    vis_entry("ART_DEF_VEFFECT_HIT_SNOW", "") + vis_entry("ART_DEF_VEFFECT_DUP", "<FOWMask>1</FOWMask>") +
                    vis_entry("ART_DEF_VEFFECT_SAME_FILE", "<FOWMask>1</FOWMask>") +
                    vis_entry("ART_DEF_VEFFECT_SAME_FILE", "<FOWMask>3</FOWMask>") +
                    vis_entry("ART_DEF_VEFFECT_BADPART", "<ParticleEffects><ParticleEffect><Name>ART_DEF_PEFFECT_NONE"
                                                         "</Name></ParticleEffect></ParticleEffects>") +
                    vis_entry("ART_DEF_VEFFECT_BADMODEL", "<ModelEffects><ModelEffect><Name>ART_DEF_MEFFECT_NONE"
                                                          "</Name></ModelEffect></ModelEffects>") +
                    vis_entry("ART_DEF_VEFFECT_BADCHILD", "<ChildEffects><ChildEffect trigger=\"ProjectileEnd\"><Name>"
                                                          "ART_DEF_VEFFECT_NOPE</Name></ChildEffect></ChildEffects>") +
                    vis_entry("ART_DEF_VEFFECT_NOFILE", "<ParticleEffects><ParticleEffect><Name>ART_DEF_PEFFECT_GONE"
                                                        "</Name></ParticleEffect></ParticleEffects>")));
    fp.put("civ5artdefines_viseffectparticles.xml",
           part_file(part_entry("ART_DEF_PEFFECT_TRAIL", "Assets\\Effects\\Trail\\FX_Trail_01.psb") +
                     part_entry("ART_DEF_PEFFECT_GONE", "Assets\\Effects\\FX_Not_There.psb")));
    fp.put("civ5artdefines_viseffectmodels.xml", model_file(model_entry("ART_DEF_MEFFECT_ARROW", "Assets/Units/proj_arrow.fxsxml")));
    fp.put("civ5artdefines_eventeffects.xml",
           "<EventEffectCollection><EventEffect><EventName>GameplayFX</EventName><VisEffects><VisEffect><Name>"
           "ART_DEF_VEFFECT_HIT_DIRT</Name></VisEffect></VisEffects></EventEffect></EventEffectCollection>");
    fp.put("V_A.xml", vis_file(vis_entry("ART_DEF_VEFFECT_DUP", "<FOWMask>2</FOWMask>")));
    fp.put("V_B.xml", vis_file(vis_entry("ART_DEF_VEFFECT_DUP", "<FOWMask>3</FOWMask>")));
    fp.put("fx_trail_01.psb", "PSB");
    fp.put("proj_arrow.fxsxml", "<Asset/>");
}

void test_library() {
    FakeProvider fp;
    make_fake(fp);
    std::vector<FxDefineFile> pa = {{FxKind::VisEffect, "V_A.xml", "A"}, {FxKind::Particle, "P_Missing.xml", "A"}};
    std::vector<FxDefineFile> pb = {{FxKind::VisEffect, "V_B.xml", "B"}};
    EffectLibrary lib;
    Error e;
    CHECK(lib.load(fp, fx_load_list({pa, pb}), &e));
    CHECK_EQ(lib.loaded_files().size(), (size_t)6);
    CHECK(lib.missing_files().size() == 1 && lib.missing_files()[0].name == "P_Missing.xml");
    CHECK_EQ(lib.distinct(FxKind::VisEffect), (size_t)9);
    CHECK_EQ(lib.entries(FxKind::VisEffect), (size_t)12);
    CHECK_EQ(lib.distinct(FxKind::Event), (size_t)1);
    // override order: the last definition in load order is in force, across files and inside one file
    const VisEffectDef* d = lib.vis_effect("ART_DEF_VEFFECT_DUP");
    CHECK(d && d->fow_mask == 3 && d->src.package == "B" && d->src.file_index == 2);
    std::vector<FxSource> defs = lib.definitions(FxKind::VisEffect, "ART_DEF_VEFFECT_DUP");
    CHECK(defs.size() == 3 && defs[0].package.empty() && defs[1].package == "A" && defs[2].package == "B");
    const VisEffectDef* s = lib.vis_effect("ART_DEF_VEFFECT_SAME_FILE");
    CHECK(s && s->fow_mask == 3 && s->src.entry_index == 5);
    std::vector<std::string> dup = lib.duplicated(FxKind::VisEffect);
    CHECK(dup.size() == 2 && dup[0] == "ART_DEF_VEFFECT_DUP" && dup[1] == "ART_DEF_VEFFECT_SAME_FILE");
    // the same list in the other package order gives the other winner
    EffectLibrary lib2;
    CHECK(lib2.load(fp, fx_load_list({pb, pa}), &e));
    CHECK(lib2.vis_effect("ART_DEF_VEFFECT_DUP") && lib2.vis_effect("ART_DEF_VEFFECT_DUP")->fow_mask == 2);

    // resolve with substitution, bare-name lookup of the .psb and .fxsxml
    ResolvedEffect r;
    FxSubstitutions sub;
    sub.set_terrain("DIRT");
    CHECK(lib.resolve(fp, "ART_DEF_VEFFECT_PROJ", sub, r, &e));
    CHECK(r.complete());
    CHECK(r.particles.size() == 1 && r.particles[0].file.found && r.particles[0].file.origin == "fake:fx_trail_01.psb");
    CHECK(r.models.size() == 1 && r.models[0].file.found && r.models[0].def->scale == 1.5f);
    CHECK(r.children.size() == 1 && r.children[0].name == "ART_DEF_VEFFECT_HIT_DIRT" && r.children[0].def &&
          r.children[0].def->fow_mask == 3);
    std::printf("%s", format_resolved_effect(r).c_str());
    CHECK(lib.resolve(fp, "ART_DEF_VEFFECT_HIT_$(TERRAIN)", sub, r, &e) && r.name == "ART_DEF_VEFFECT_HIT_DIRT");
    // negatives: unknown name (refused by name), child needing a value not given, unknown references
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_SEAGULLS_X", sub, r, &e) && e.code == Err::NotFound &&
          e.what.find("ART_DEF_VEFFECT_SEAGULLS_X") != std::string::npos);
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_HIT_$(TERRAIN)", FxSubstitutions(), r, &e) && e.code == Err::NotFound);
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_PROJ", FxSubstitutions(), r, &e) && e.code == Err::NotFound &&
          e.what.find("child") != std::string::npos);
    FxSubstitutions sand;
    sand.set_terrain("SAND");   // HIT_SAND is not defined
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_PROJ", sand, r, &e) && e.what.find("ART_DEF_VEFFECT_HIT_SAND") != std::string::npos);
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_BADPART", sub, r, &e) && e.what.find("ART_DEF_PEFFECT_NONE") != std::string::npos);
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_BADMODEL", sub, r, &e) && e.what.find("ART_DEF_MEFFECT_NONE") != std::string::npos);
    CHECK(!lib.resolve(fp, "ART_DEF_VEFFECT_BADCHILD", sub, r, &e) && e.what.find("ART_DEF_VEFFECT_NOPE") != std::string::npos);
    // a missing .psb is a problem, not an error
    CHECK(lib.resolve(fp, "ART_DEF_VEFFECT_NOFILE", sub, r, &e) && !r.complete() && r.problems.size() == 1);
    // variants of a name with variables
    std::vector<std::string> v = lib.variants("ART_DEF_VEFFECT_HIT_$(TERRAIN)");
    CHECK(v.size() == 2 && v[0] == "ART_DEF_VEFFECT_HIT_DIRT" && v[1] == "ART_DEF_VEFFECT_HIT_SNOW");
    CHECK(lib.variants("ART_DEF_VEFFECT_PROJ").size() == 1);
    CHECK(lib.variants("ART_DEF_VEFFECT_NONE_$(WEAPON)").empty());
    CHECK(lib.event("GameplayFX") && lib.event("GameplayFX")->vis_effects.size() == 1);

    // negatives of load: a malformed listed file -> Err::Xml naming it, library cleared
    fp.put("V_Bad.xml", "<VisEffectArtInfos><VisEffectArtInfo>");
    EffectLibrary lib3;
    CHECK(!lib3.load(fp, fx_load_list({{{FxKind::VisEffect, "V_Bad.xml", "C"}}}), &e));
    CHECK(e.code == Err::Xml && e.what.find("V_Bad.xml") != std::string::npos);
    CHECK(lib3.distinct(FxKind::VisEffect) == 0 && !lib3.vis_effect("ART_DEF_VEFFECT_PROJ"));
    // a base file that is missing is listed as missing; nothing else breaks
    FakeProvider empty;
    EffectLibrary lib4;
    CHECK(lib4.load(empty, fx_load_list({}), &e));
    CHECK(lib4.missing_files().size() == 4 && lib4.distinct(FxKind::VisEffect) == 0);
}

// ---------------------------------------------------------------------------------------------- real files
std::string env(const char* n) {
    const char* v = std::getenv(n);
    return v ? std::string(v) : std::string();
}

bool starts_with(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
bool ends_with(const std::string& s, const char* p) {
    size_t n = std::strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

void test_real() {
    std::string inst = env("C5R_GAME_DIR");
    if (inst.empty()) inst = env("C5M_FX_INSTALL");
    std::string ext = env("C5M_FX_EXTRACT");
    if (inst.empty() || ext.empty()) {
        std::printf("SKIP real files: C5R_GAME_DIR (or C5M_FX_INSTALL) and C5M_FX_EXTRACT must both be set\n");
        t::skipped() = true;
        return;
    }
    DirectoryProvider dp;
    Error e;
    // The archives first: with LooseFilesOverridePAK = 0 a base pack beats a loose file (asset-pipeline.md 1.2).
    if (!dp.add_root("fpk", ext, &e) || !dp.add_root("install", inst + "/Assets", &e)) {
        std::printf("SKIP real files: %s\n", e.what.c_str());
        t::skipped() = true;
        return;
    }
    // Packages: Assets/DLC/<folder>/<file>.Civ5Pkg in the walk's order (folder names, case-insensitive).
    std::vector<std::vector<FxDefineFile>> pkgs;
    int pkg_files = 0, pkg_bad = 0;
    for (auto& en : dp.entries()) {
        if (!starts_with(en.origin, "install:DLC/") || !ends_with(en.name, ".civ5pkg")) continue;
        std::string rel = en.origin.substr(std::strlen("install:DLC/"));
        if (std::count(rel.begin(), rel.end(), '/') != 1) continue;
        ++pkg_files;
        Bytes b;
        CHECK(dp.read_entry(en, b));
        std::vector<FxDefineFile> f;
        Error pe;
        if (!parse_package_fx_defines((const char*)b.data(), b.size(), rel, f, &pe)) {
            std::printf("package not read: %s\n", pe.what.c_str());
            ++pkg_bad;
            continue;
        }
        for (auto& x : f) std::printf("package %s: %s %s\n", rel.c_str(), fx_kind_name(x.kind), x.name.c_str());
        pkgs.push_back(f);
    }
    std::printf("packages: %d files, %d not read\n", pkg_files, pkg_bad);
    CHECK_EQ(pkg_bad, 0);
    std::vector<FxDefineFile> list = fx_load_list(pkgs);
    EffectLibrary lib;
    CHECK(lib.load(dp, list, &e));
    std::printf("load list %zu files, %zu read, %zu missing:", list.size(), lib.loaded_files().size(),
                lib.missing_files().size());
    for (auto& m : lib.missing_files()) std::printf(" %s(%s)", m.name.c_str(), m.package.c_str());
    std::printf("\n");
    // 4 base files and 34 package files; DLC_02's and DLC_Deluxe's are not loose but inside SpanishModels.fpk and
    // BabylonianModels.fpk, found by bare name like every other file (effects-formats.md 14.1).
    CHECK_EQ(list.size(), (size_t)38);
    CHECK(lib.missing_files().empty());

    // Census (effects-formats.md 0 and 3): 538 VisEffects, 235 particle and 66 model definitions, 4 events. The
    // duplicate entries are 103 / 10 / 7, not 0320's 92 / 9 / 5: 0320 did not see the 6 files in the two archives
    // above, whose names all have a definition elsewhere too (effects-formats.md 14.1, corrected 2026-11-20).
    const size_t want_distinct[kFxKinds] = {538, 235, 66, 4};
    const size_t want_dups[kFxKinds] = {103, 10, 7, 0};
    for (int k = 0; k < kFxKinds; ++k) {
        size_t dup_entries = lib.entries((FxKind)k) - lib.distinct((FxKind)k);
        std::printf("%s: %zu distinct, %zu entries, %zu duplicate entries, %zu names defined more than once\n",
                    fx_kind_name((FxKind)k), lib.distinct((FxKind)k), lib.entries((FxKind)k), dup_entries,
                    lib.duplicated((FxKind)k).size());
        CHECK_EQ(lib.distinct((FxKind)k), want_distinct[k]);
        CHECK_EQ(dup_entries, want_dups[k]);
    }
    // VisEffects with particles 477, models 101, both 42, a child 40, sounds 80, fMovementSpeed 11; FOWMask
    // written 3 thirteen times and 2 once; particle references per effect none 61, one 409, two 68.
    int with_p = 0, with_m = 0, both = 0, with_c = 0, with_s = 0, with_speed = 0, fow3 = 0, fow2w = 0;
    int per[3] = {0, 0, 0};
    std::set<std::string> psb_used;
    for (auto& n : lib.names(FxKind::VisEffect)) {
        const VisEffectDef& d = *lib.vis_effect(n);
        with_p += !d.particles.empty();
        with_m += !d.models.empty();
        both += !d.particles.empty() && !d.models.empty();
        with_c += !d.children.empty();
        with_s += !d.sounds.empty();
        with_speed += d.has_movement_speed;
        fow3 += d.fow_mask_written && d.fow_mask == 3;
        fow2w += d.fow_mask_written && d.fow_mask == 2;
        if (d.particles.size() < 3) per[d.particles.size()]++;
        for (auto& p : d.particles) {
            const ParticleDef* pd = lib.particle(p);
            CHECK(pd != nullptr);   // every particle reference has a definition
            if (pd) psb_used.insert(basename_lower(pd->file));
        }
        for (auto& m : d.models) CHECK(lib.model(m.name) != nullptr);
    }
    std::printf("VisEffects with particles %d, models %d, both %d, children %d, sounds %d, fMovementSpeed %d; "
                "FOWMask 3: %d, 2 written: %d; particles per effect %d/%d/%d; distinct .psb used %zu\n",
                with_p, with_m, both, with_c, with_s, with_speed, fow3, fow2w, per[0], per[1], per[2], psb_used.size());
    CHECK_EQ(with_p, 477);
    CHECK_EQ(with_m, 101);
    CHECK_EQ(both, 42);
    CHECK_EQ(with_c, 40);
    CHECK_EQ(with_s, 80);
    CHECK_EQ(with_speed, 11);
    CHECK_EQ(fow3, 13);
    CHECK_EQ(fow2w, 1);
    CHECK(per[0] == 61 && per[1] == 409 && per[2] == 68);
    CHECK_EQ(psb_used.size(), (size_t)232);

    // Bare-name lookup: every particle definition's .psb and every model definition's .fxsxml exists (235 / 66).
    int psb_ok = 0, fxs_ok = 0;
    for (auto& n : lib.names(FxKind::Particle)) psb_ok += dp.exists(basename_lower(lib.particle(n)->file), nullptr);
    for (auto& n : lib.names(FxKind::Model)) fxs_ok += dp.exists(basename_lower(lib.model(n)->file), nullptr);
    std::printf("particle files found %d of %zu, model files found %d of %zu\n", psb_ok,
                lib.distinct(FxKind::Particle), fxs_ok, lib.distinct(FxKind::Model));
    CHECK_EQ(psb_ok, 235);
    CHECK_EQ(fxs_ok, 66);

    // Every VisEffect in force resolves completely with the substitutions of a dirt plot and an empty weapon
    // (its children carry $(TERRAIN)); the 9 child names that need $(TERRAIN) resolve for each value they exist in.
    FxSubstitutions dirt;
    dirt.set_terrain("DIRT").set_base_terrain("DIRT").set_weapon("");
    int complete = 0, failed = 0;
    for (auto& n : lib.names(FxKind::VisEffect)) {
        ResolvedEffect r;
        Error re;
        if (!lib.resolve(dp, n, dirt, r, &re)) {
            std::printf("not resolved: %s\n", re.what.c_str());
            ++failed;
        } else {
            complete += r.complete();
        }
    }
    std::printf("VisEffects resolved completely with DIRT: %d, failed %d\n", complete, failed);
    CHECK_EQ(failed, 0);
    CHECK_EQ(complete, 538);
    std::set<std::string> child_patterns;
    for (auto& n : lib.names(FxKind::VisEffect))
        for (auto& c : lib.vis_effect(n)->children)
            if (!fx_variables(c.name).empty()) child_patterns.insert(c.name);
    std::printf("child names with a variable: %zu\n", child_patterns.size());
    for (auto& c : child_patterns) {
        std::vector<std::string> v = lib.variants(c);
        std::printf("  %s -> %zu variants\n", c.c_str(), v.size());
        CHECK(v.size() >= 2);   // as few as DIRT and one more (effects-formats.md 14.3)
        CHECK(std::find(v.begin(), v.end(), c.substr(0, c.find("$(")) + "DIRT") != v.end());
    }

    // The trigger names of every .ftsxml (archives and loose): 392 distinct Effect events, 386 defined as written,
    // 4 through a substitution, 2 dangling and refused by name.
    std::set<std::string> events;
    int fts_files = 0, fts_bad = 0;
    for (auto& en : dp.entries()) {
        if (!ends_with(en.name, ".ftsxml")) continue;
        Bytes b;
        if (!dp.read_entry(en, b)) continue;
        TriggerSystem ts;
        Error te;
        ++fts_files;
        if (!parse_ftsxml((const char*)b.data(), b.size(), ts, &te)) {
            ++fts_bad;
            continue;
        }
        for (auto& tr : ts.triggers)
            if (tr.type == TriggerType::Effect) {
                auto it = tr.attrs.find("event");
                if (it != tr.attrs.end()) events.insert(it->second);
            }
    }
    int exact = 0, subst = 0;
    std::vector<std::string> dangling;
    for (auto& n : events) {
        if (lib.vis_effect(n)) ++exact;
        else if (!fx_variables(n).empty() && !lib.variants(n).empty()) ++subst;
        else dangling.push_back(n);
    }
    std::printf(".ftsxml files %d (%d not read); Effect event names %zu: exact %d, by substitution %d, dangling %zu\n",
                fts_files, fts_bad, events.size(), exact, subst, dangling.size());
    CHECK_EQ(events.size(), (size_t)392);
    CHECK_EQ(exact, 386);
    CHECK_EQ(subst, 4);
    CHECK_EQ(dangling.size(), (size_t)2);
    for (auto& n : dangling) {
        ResolvedEffect r;
        Error re;
        CHECK(!lib.resolve(dp, n, dirt, r, &re) && re.code == Err::NotFound && re.what.find(n) != std::string::npos);
        std::printf("refused: %s\n", re.what.c_str());
    }
    CHECK(std::find(dangling.begin(), dangling.end(), "ART_DEF_VEFFECT_SEAGULLS_01") != dangling.end());
    CHECK(std::find(dangling.begin(), dangling.end(), "ART_DEF_VEFFECT_WORKBOAT_INDUSTRIAL_HOIST_SPLASH_01") !=
          dangling.end());
    // The 4 substitution names with values of the database (effects-formats.md 6).
    struct Sub {
        const char* name;
        FxSubstitutions s;
        const char* want;
    } subs[] = {
        {"ART_DEF_VEFFECT_MECH_FOOT_DUST_$(TERRAIN)", FxSubstitutions().set_terrain("SNOW"), "ART_DEF_VEFFECT_MECH_FOOT_DUST_SNOW"},
        {"ART_DEF_VEFFECT_GUNSHIP_AIRWAKE_$(BASETERRAIN)", FxSubstitutions().set_base_terrain("WATER"),
         "ART_DEF_VEFFECT_GUNSHIP_AIRWAKE_WATER"},
        {"ART_DEF_VEFFECT_ARMOR_DEATH_SM_$(WEAPON)", FxSubstitutions().set_weapon("METAL"), "ART_DEF_VEFFECT_ARMOR_DEATH_SM_METAL"},
        {"ART_DEF_VEFFECT_ARMOR_DEATH_MD_$(WEAPON)", FxSubstitutions().set_weapon(""), "ART_DEF_VEFFECT_ARMOR_DEATH_MD_"},
    };
    for (auto& s : subs) {
        CHECK(events.count(s.name) == 1);
        ResolvedEffect r;
        Error re;
        CHECK(lib.resolve(dp, s.name, s.s, r, &re) && r.name == s.want && r.complete());
    }

    // The override order as found (effects-formats.md 14.2): names whose copies differ, the winner's file.
    for (int k = 0; k < kFxKinds; ++k)
        for (auto& n : lib.duplicated((FxKind)k)) {
            std::vector<FxSource> d = lib.definitions((FxKind)k, n);
            std::printf("dup %s %s:", fx_kind_name((FxKind)k), n.c_str());
            for (auto& s : d) std::printf(" %s", s.file.c_str());
            std::printf("\n");
        }
    // Spot checks of the order: Shared (folder "Shared") after the base file, Expansion2 after DLC_07.
    const VisEffectDef* ps = lib.vis_effect("ART_DEF_VEFFECT_PILLAGED_SMOKE_01");
    CHECK(ps && ps->src.package == "Shared/Upgrade1.Civ5Pkg");
    const VisEffectDef* sl = lib.vis_effect("ART_DEF_VEFFECT_SLING_ROCK_IMPACT_DIRT");
    CHECK(sl && sl->src.package == "Expansion2/Expansion2.Civ5Pkg");

    // Two worked examples printed for the evidence: the archer's arrow and a catapult impact on snow.
    ResolvedEffect r;
    CHECK(lib.resolve(dp, "ART_DEF_VEFFECT_PROJ_ARROW_ARCHER", dirt, r, &e) && r.complete());
    std::printf("%s", format_resolved_effect(r).c_str());
    FxSubstitutions snow;
    snow.set_terrain("SNOW");
    CHECK(lib.resolve(dp, "ART_DEF_VEFFECT_CATAPULT_ROCK_IMPACT_$(TERRAIN)", snow, r, &e) && r.complete());
    std::printf("%s", format_resolved_effect(r).c_str());
    for (auto ev : {"SerialEventUnitCreated", "SerialEventUnitDestroyedInCombat", "GameplayFX", "NaturalWonderRevealed"})
        CHECK(lib.event(ev) != nullptr);
}

}  // namespace

int main() {
    test_parse_vis();
    test_parse_others();
    test_packages_and_list();
    test_substitution();
    test_library();
    test_real();
    return t::finish();
}
