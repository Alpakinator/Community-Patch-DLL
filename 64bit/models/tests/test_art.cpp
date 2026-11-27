// test_art.cpp - art resolution (issue 3104): XML scanner, .fxsxml and WonderArtInfo parsers, TSV tables, the three
// chains on a synthetic FileProvider, each check with a negative test; with C5M_SAMPLES the three real examples
// (academy row, BUILDING_STONEHENGE, UNIT_WARRIOR) through winsqlite3 and the extraction.
#include "c5models/art.h"
#include "testing.h"

#include <cstdlib>
#include <map>

using namespace c5m;

namespace {

class FakeProvider : public FileProvider {
public:
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> files;   // name -> [(origin, text)]
    void put(const std::string& name, const std::string& text, const std::string& origin = "fake:") {
        files[ascii_lower(name)].emplace_back(origin + name, text);
    }
    bool read(const std::string& n, Bytes& out, std::string* origin) override {
        auto it = files.find(n);
        if (it == files.end()) return false;
        out.assign(it->second.front().second.begin(), it->second.front().second.end());
        if (origin) *origin = it->second.front().first;
        return true;
    }
    std::vector<std::string> duplicates(const std::string& n) const override {
        std::vector<std::string> r;
        auto it = files.find(n);
        if (it != files.end())
            for (auto& x : it->second) r.push_back(x.first);
        return r;
    }
};

bool xml(const std::string& s, XmlElement& root, Error* e) { return parse_xml(s.data(), s.size(), root, e); }

void test_xml_scanner() {
    XmlElement r;
    Error e;
    CHECK(xml("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- c --><!DOCTYPE x><A a='1' b=\"x &amp; &lt;y&gt; &#65;&#x42;\">"
              " t1 <B/><![CDATA[<raw>]]><C c=\"2\">inner</C><!-- c2 --></A>\n<!-- tail -->",
              r, &e));
    CHECK_EQ(r.name, std::string("A"));
    CHECK(r.attr("a") && *r.attr("a") == "1");
    CHECK(r.attr("b") && *r.attr("b") == "x & <y> AB");
    CHECK_EQ(r.children.size(), (size_t)2);
    CHECK(r.child("C") && r.child("C")->text == "inner");
    CHECK_EQ(r.text, std::string("t1 <raw>"));
    CHECK(r.attr("zz") == nullptr);
    // negative: each malformed construct -> Err::Xml
    const char* bad[] = {
        "<A><B></A>",              // mismatched end tag
        "<A>",                     // not closed
        "<A a=1/>",                // unquoted attribute
        "<A a=\"1\"b=\"2\"/>",     // no space between attributes
        "<A a=\"1\" a=\"2\"/>",    // duplicate attribute
        "<A/><B/>",                // two roots
        "",                        // no root
        "<!-- unterminated",       // comment
        "<A>&bogus;</A>",          // unknown entity
        "<A a=\"<\"/>",            // '<' in attribute
        "\xFF\xFE<\0A\0/\0>\0",    // UTF-16
    };
    for (const char* b : bad) {
        Error be;
        XmlElement br;
        std::string s(b, b[0] == '\xFF' ? 10 : std::strlen(b));
        CHECK(!xml(s, br, &be));
        CHECK(be.code == Err::Xml);
        CHECK(!be.what.empty());
    }
    Error le;
    XmlElement lr;
    CHECK(!xml("<A>\n\n<B>\n</A>", lr, &le));
    CHECK(le.what.find("line 4") != std::string::npos);   // the line of the offending end tag
}

const char* kWarriorFxs =
    "<Asset>\n"
    "\t<Mesh file=\"Warrior.gr2\"/>\n"
    "\t<Animation file=\"Warrior_IdleA.gr2\" ec=\"1000\"/>\n"
    "\t<Animation file=\"Warrior_Attack_City.gr2\" ec=\"1160, 1180\"/>\n"
    "\t<Animation file=\"Warrior_Gone.gr2\" ec=\"1200\" name=\"death\" layer=\"0\"/>\n"
    "\t<Texture file=\"Warrior_DIFF.dds\"/>\n"
    "\t<Texture file=\"Warrior_Missing.dds\" source=\"Max\"/>\n"
    "\t<TimedTrigger file=\"FX_Triggers_Warrior.ftsxml\" state=\"3\"/>\n"
    "\t<DecisionTree file=\"LandmarkDecisionTree4.ddtxml\"/>\n"
    "\t<BoneUsage><Bone name=\"bone_CLUB\"/><Bone name=\"Base HumanSpine2\"/></BoneUsage>\n"
    "\t<AnimGraph file=\"IDLE_GRAPH.dge\"/>\n"
    "\t<StateMachine file=\"1_OMNI_STATE_01.fsmxml\"/>\n"
    "\t<Novel thing=\"1\"/>\n"
    "</Asset>\n";

void test_fxsxml() {
    FxsAsset a;
    Error e;
    std::string s = kWarriorFxs;
    CHECK(parse_fxsxml(s.data(), s.size(), a, &e));
    CHECK_EQ(a.mesh, std::string("Warrior.gr2"));
    CHECK_EQ(a.mesh_count, 1);
    CHECK_EQ(a.animations.size(), (size_t)3);
    CHECK(a.animations[1].ec.size() == 2 && a.animations[1].ec[0] == 1160 && a.animations[1].ec[1] == 1180);
    CHECK_EQ(a.animations[1].ec_text, std::string("1160, 1180"));
    CHECK_EQ(a.animations[2].name, std::string("death"));
    CHECK_EQ(a.textures.size(), (size_t)2);
    CHECK_EQ(a.bones.size(), (size_t)2);
    CHECK(a.timed_triggers.size() == 1 && a.timed_triggers[0].state == "3");
    CHECK_EQ(a.decision_trees.size(), (size_t)1);
    CHECK_EQ(a.anim_graphs.size(), (size_t)1);
    CHECK_EQ(a.state_machines.size(), (size_t)1);
    CHECK(a.unknown.size() == 1 && a.unknown["Novel"] == 1);   // tolerated and counted
    // negatives
    const char* bad[] = {
        "<Assets><Mesh file=\"x.gr2\"/></Assets>",                              // wrong root
        "<Asset><Texture file=\"a.dds\"/></Asset>",                             // no mesh
        "<Asset><Mesh/></Asset>",                                               // mesh without file
        "<Asset><Mesh file=\"x.gr2\"/><Animation file=\"y.gr2\" ec=\"1x\"/></Asset>",   // bad ec
        "<Asset><Mesh file=\"x.gr2\"></Asset>",                                 // malformed XML
    };
    for (const char* b : bad) {
        FxsAsset ba;
        Error be;
        CHECK(!parse_fxsxml(b, std::strlen(b), ba, &be));
        CHECK(be.code == Err::Xml);
    }
}

const char* kWonders1 =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<WonderArtInfos>\n"
    "  <!-- WONDER STONEHENGE -->\n"
    "  <WonderArtInfo><Type>STONEHENGE</Type><WonderArtState state=\"Any\"><fScale>0.05</fScale>"
    "<Granny>Assets/Buildings/Wonders/Stonehenge/Stonehenge.fxsxml</Granny></WonderArtState></WonderArtInfo>\n"
    "  <WonderArtInfo><Type>THE PYRAMIDS</Type><WonderArtState state=\"Any\"><fScale>0.05</fScale>"
    "<Granny>Pyramid.fxsxml</Granny></WonderArtState></WonderArtInfo>\n"
    "</WonderArtInfos>\n";
const char* kWonders2 =
    "<WonderArtInfos><WonderArtInfo><Type>THE PYRAMIDS</Type><WonderArtState state=\"Any\"><fScale>0.2</fScale>"
    "<Granny>Buildings\\Wonders\\Pyramid2.fxsxml</Granny></WonderArtState></WonderArtInfo></WonderArtInfos>";

void test_wonder_parser() {
    std::vector<WonderArt> w;
    Error e;
    CHECK(parse_wonder_art(kWonders1, std::strlen(kWonders1), "src1", w, &e));
    CHECK_EQ(w.size(), (size_t)2);
    CHECK(w[0].type == "STONEHENGE" && w[0].state == "Any" && w[0].scale > 0.049f && w[0].scale < 0.051f);
    CHECK_EQ(w[1].granny, std::string("Pyramid.fxsxml"));
    const char* bad[] = {
        "<Wonders/>",
        "<WonderArtInfos><WonderArtInfo><WonderArtState><Granny>x</Granny></WonderArtState></WonderArtInfo></WonderArtInfos>",
        "<WonderArtInfos><WonderArtInfo><Type>X</Type><WonderArtState><fScale>0.1</fScale></WonderArtState></WonderArtInfo></WonderArtInfos>",
        "<WonderArtInfos><WonderArtInfo><Type>X</Type><WonderArtState><fScale>abc</fScale><Granny>a</Granny></WonderArtState></WonderArtInfo></WonderArtInfos>",
    };
    for (const char* b : bad) {
        std::vector<WonderArt> bw;
        Error be;
        CHECK(!parse_wonder_art(b, std::strlen(b), "bad", bw, &be));
        CHECK(be.code == Err::Xml);
    }
}

void test_basename() {
    CHECK_EQ(basename_lower("Assets/Units/Warrior/Warrior.fxsxml"), std::string("warrior.fxsxml"));
    CHECK_EQ(basename_lower("Buildings\\Wonders\\X.FXSXML "), std::string("x.fxsxml"));
    CHECK_EQ(basename_lower("plain.gr2"), std::string("plain.gr2"));
}

// The synthetic database: one TSV per table.
void fill_tables(TsvTables& ts) {
    Error e;
    CHECK(ts.add("ArtDefine_Landmarks",
                 "Era\tState\tScale\tImprovementType\tLayoutHandler\tResourceType\tModel\tTerrainContour\tTech\n"
                 "Ancient\tConstructed\t0.75\tART_DEF_IMPROVEMENT_ACADEMY\tSNAPSHOT\tART_DEF_RESOURCE_ALL\t"
                 "Assets/Buildings/Improvements/Academy/ANC_Academy.fxsxml\t1\t\\N\n"
                 "Ancient\tPillaged\t0.75\tART_DEF_IMPROVEMENT_ACADEMY\tSNAPSHOT\tART_DEF_RESOURCE_ALL\t"
                 "Assets/PL_ANC_Academy.fxsxml\t1\t\\N\n"
                 "Any\tAny\t1\tART_DEF_IMPROVEMENT_FARM\tFARM\tART_DEF_RESOURCE_WHEAT\t\\N\t1\t\\N\n",
                 &e));
    CHECK(ts.add("Units", "Type\tUnitArtInfo\tCost\nUNIT_WARRIOR\tART_DEF_UNIT__WARRIOR\t40\nUNIT_GHOST\tART_DEF_UNIT_GHOST\t1\n", &e));
    CHECK(ts.add("ArtDefine_UnitInfoMemberInfos",
                 "UnitInfoType\tUnitMemberInfoType\tNumMembers\n"
                 "ART_DEF_UNIT__WARRIOR\tART_DEF_UNIT_MEMBER_WARRIOR\t6\n"
                 "ART_DEF_UNIT__WARRIOR\tART_DEF_UNIT_MEMBER_NOBODY\t3\n",
                 &e));
    CHECK(ts.add("ArtDefine_UnitMemberInfos",
                 "Type\tScale\tZOffset\tDomain\tModel\tMaterialTypeTag\n"
                 "ART_DEF_UNIT_MEMBER_WARRIOR\t0.14\t\\N\t\tAssets/Units/Warrior/Warrior.fxsxml\tCLOTH\n",
                 &e));
    CHECK(ts.add("Buildings", "Type\tArtDefineTag\nBUILDING_STONEHENGE\tSTONEHENGE\nBUILDING_PYRAMID\tTHE PYRAMIDS\n"
                              "BUILDING_NOART\t\\N\nBUILDING_LOST\tLOST WONDER\n",
                 &e));
}

void test_tsv() {
    TsvTables ts;
    Error e;
    CHECK(ts.add("T", "a\tb\nx\\ty\t\\N\n", &e));
    std::vector<DbRow> rows;
    CHECK(ts.rows("T", {"b", "a"}, rows, &e));
    CHECK(rows.size() == 1 && rows[0][0].null && !rows[0][1].null && rows[0][1].text == "x\ty");
    CHECK(!ts.rows("T", {"c"}, rows, &e) && e.code == Err::NotFound);
    CHECK(!ts.rows("U", {"a"}, rows, &e) && e.code == Err::NotFound);
    CHECK(!ts.add("V", "a\tb\nonly-one\n", &e));   // field count
    CHECK(!ts.add("W", "a\nbad\\q\n", &e));        // bad escape
}

void test_chains() {
    FakeProvider fp;
    fp.put("ANC_Academy.fxsxml",
           "<Asset><Mesh file=\"ANC_Academy.gr2\" source=\"Max\"/><Texture file=\"ANC_Academy_DIFF.dds\"/>"
           "<Texture file=\"Generic_Grey_8.dds\"/></Asset>");
    fp.put("anc_academy.gr2", "x", "fpk:ImprovementModels/");
    fp.put("anc_academy.gr2", "y", "mods:VP/");          // a duplicate in a later root
    fp.put("anc_academy_diff.dds", "x");
    // Generic_Grey_8.dds deliberately missing
    fp.put("Warrior.fxsxml", kWarriorFxs);
    fp.put("warrior.gr2", "x");
    fp.put("warrior_idlea.gr2", "x");
    fp.put("warrior_attack_city.gr2", "x");
    fp.put("warrior_diff.dds", "x");
    fp.put("fx_triggers_warrior.ftsxml", "x");
    fp.put("Stonehenge.fxsxml", "<Asset><Mesh file=\"Stonehenge.gr2\"/><Texture file=\"Stonehenge_DIFF.dds\"/></Asset>");
    fp.put("stonehenge.gr2", "x");
    fp.put("stonehenge_diff.dds", "x");
    fp.put("Pyramid2.fxsxml", "<Asset><Mesh file=\"Pyramid.gr2\"/>");   // malformed
    fp.put(kWonderArtFiles[0], kWonders1, "fpk:WonderModels/");
    fp.put(kWonderArtFiles[kWonderArtFileCount - 1], kWonders2, "install:DLC/Expansion2/");

    TsvTables ts;
    fill_tables(ts);
    ArtTables t;
    Error e;
    CHECK(t.load_database(ts, &e));
    CHECK_EQ(t.landmarks.size(), (size_t)3);
    CHECK(t.landmarks[2].model_null);
    CHECK_EQ(t.building_art.size(), (size_t)3);   // NULL tag skipped
    CHECK(t.load_wonders(fp, &e));
    CHECK_EQ(t.wonders.size(), (size_t)3);
    CHECK_EQ(t.wonder_files_missing.size(), kWonderArtFileCount - 2);
    int n = 0;
    const WonderArt* pw = t.wonder("THE PYRAMIDS", &n);
    CHECK(pw && n == 2 && pw->granny.find("Pyramid2") != std::string::npos);   // the last loaded wins

    // chain 1
    std::vector<ResolvedArt> out;
    CHECK(resolve_landmark(t, fp, "ART_DEF_IMPROVEMENT_ACADEMY", "ART_DEF_RESOURCE_ALL", "Ancient", "Constructed", out, &e));
    CHECK_EQ(out.size(), (size_t)1);
    if (!out.empty()) {
        const ResolvedArt& r = out[0];
        CHECK(r.fxsxml.found && r.mesh.found);
        CHECK_EQ(r.mesh.origin, std::string("fpk:ImprovementModels/anc_academy.gr2"));
        CHECK(r.mesh.others.size() == 1 && r.mesh.others[0] == "mods:VP/anc_academy.gr2");
        CHECK_EQ(r.textures.size(), (size_t)2);
        CHECK(r.textures[0].found && !r.textures[1].found);    // missing texture reported, not fatal
        CHECK_EQ(r.missing_textures(), (size_t)1);
        CHECK(!r.complete());
        CHECK(r.scale > 0.74f && r.scale < 0.76f);
        bool reported = false;
        for (auto& p : r.problems) reported |= p.find("Generic_Grey_8.dds") != std::string::npos;
        CHECK(reported);
        CHECK(format_resolved(r).find("MISSING") != std::string::npos);
    }
    // all rows of the improvement: the pillaged row's .fxsxml is missing -> recorded in the row, not an error
    out.clear();
    CHECK(resolve_landmark(t, fp, "ART_DEF_IMPROVEMENT_ACADEMY", "ART_DEF_RESOURCE_ALL", "", "", out, &e));
    CHECK_EQ(out.size(), (size_t)2);
    if (out.size() == 2) {
        CHECK(!out[1].fxsxml.found);
        CHECK(!out[1].problems.empty() && out[1].problems[0].find("pl_anc_academy.fxsxml") != std::string::npos);
    }
    // a NULL-model row matches but resolves nothing
    out.clear();
    CHECK(resolve_landmark(t, fp, "ART_DEF_IMPROVEMENT_FARM", "ART_DEF_RESOURCE_WHEAT", "", "", out, &e));
    CHECK(out.empty());
    // negative: no such row
    out.clear();
    CHECK(!resolve_landmark(t, fp, "ART_DEF_IMPROVEMENT_NONE_SUCH", "ART_DEF_RESOURCE_ALL", "", "", out, &e));
    CHECK(e.code == Err::NotFound);

    // resolve_fxsxml directly: missing -> NotFound with the name; malformed -> Xml
    {
        ResolvedArt r;
        Error fe;
        CHECK(!resolve_fxsxml(fp, "Assets/Nowhere/Missing_Model.fxsxml", r, &fe));
        CHECK(fe.code == Err::NotFound);
        CHECK(fe.what.find("missing_model.fxsxml") != std::string::npos);
        ResolvedArt r2;
        Error fe2;
        CHECK(!resolve_fxsxml(fp, "Pyramid2.fxsxml", r2, &fe2));
        CHECK(fe2.code == Err::Xml);
    }

    // chain 2
    ResolvedArt w;
    CHECK(resolve_wonder(t, fp, "BUILDING_STONEHENGE", w, &e));
    CHECK(w.complete());
    CHECK_EQ(w.key, std::string("BUILDING_STONEHENGE|STONEHENGE"));
    CHECK_EQ(w.mesh.name, std::string("Stonehenge.gr2"));
    CHECK(w.scale > 0.049f && w.scale < 0.051f);
    ResolvedArt w2;
    CHECK(!resolve_wonder(t, fp, "BUILDING_NOART", w2, &e) && e.code == Err::NotFound);
    ResolvedArt w3;
    CHECK(!resolve_wonder(t, fp, "BUILDING_LOST", w3, &e) && e.code == Err::NotFound);
    CHECK(e.what.find("LOST WONDER") != std::string::npos);
    ResolvedArt w4;
    CHECK(!resolve_wonder(t, fp, "BUILDING_PYRAMID", w4, &e) && e.code == Err::Xml);   // the overriding entry's file is malformed
    bool multi = false;
    for (auto& p : w4.problems) multi |= p.find("2 WonderArtInfo entries") != std::string::npos;
    CHECK(multi);

    // chain 3
    std::vector<ResolvedArt> u;
    CHECK(resolve_unit(t, fp, "UNIT_WARRIOR", u, &e));
    CHECK_EQ(u.size(), (size_t)2);
    if (u.size() == 2) {
        const ResolvedArt& r = u[0];
        CHECK(r.fxsxml.found && r.mesh.found);
        CHECK_EQ(r.animations.size(), (size_t)3);
        CHECK(r.animations[0].file.found && r.animations[1].file.found && !r.animations[2].file.found);
        CHECK_EQ(r.missing_animations(), (size_t)1);
        CHECK(r.animations[1].ec.size() == 2);
        CHECK(r.timed_triggers.size() == 1 && r.timed_triggers[0].file.found && r.timed_triggers[0].state == "3");
        CHECK(r.decision_trees.size() == 1 && !r.decision_trees[0].found);
        CHECK(r.bones.size() == 2);
        CHECK(r.unknown_elements.count("Novel") == 1);
        CHECK(r.scale > 0.139f && r.scale < 0.141f);
        CHECK(!u[1].fxsxml.found);   // member info missing from ArtDefine_UnitMemberInfos
        CHECK(!u[1].problems.empty() && u[1].problems[0].find("ART_DEF_UNIT_MEMBER_NOBODY") != std::string::npos);
    }
    u.clear();
    CHECK(!resolve_unit(t, fp, "UNIT_NONE_SUCH", u, &e) && e.code == Err::NotFound);
    CHECK(!resolve_unit(t, fp, "UNIT_GHOST", u, &e) && e.code == Err::NotFound);   // no member rows

    // a malformed wonder art file fails load_wonders with Err::Xml
    FakeProvider bad;
    bad.put(kWonderArtFiles[0], "<WonderArtInfos><WonderArtInfo>");
    ArtTables t2;
    CHECK(!t2.load_wonders(bad, &e) && e.code == Err::Xml);
}

void test_directory_provider() {
    DirectoryProvider dp;
    Error e;
    CHECK(!dp.add_root("x", "Z:/definitely/not/here/c5m", &e) && e.code == Err::NotFound);
}

// ---------------------------------------------------------------------------------------------- real files
std::string env_or(const char* name, const std::string& dflt) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : dflt;
}

// The user's home folder with forward slashes (USERPROFILE, else HOME), "" when neither is set. Ticket 4711: the
// default instance paths below are written from it, so no user folder is named here (the same paths as before).
std::string home_dir() {
    std::string h = env_or("USERPROFILE", env_or("HOME", ""));
    for (char& c : h)
        if (c == '\\') c = '/';
    return h;
}

void test_real() {
    std::string samples = t::samples();
    if (samples.empty()) {
        std::printf("SKIP real examples: C5M_SAMPLES not set\n");
        t::skipped() = true;
        return;
    }
    std::string db = env_or("C5M_ARTDB", "D:/c5-64bit-work/3100/art/Civ5DebugDatabase.db");
    // Defaults: the test instance c5h-b under <home>/c5-instances (C5M_INSTALL_ASSETS and C5M_MODS override them).
    std::string inst = env_or("C5M_INSTALL_ASSETS", home_dir() + "/c5-instances/c5h-b/install/Assets");
    std::string mods = env_or("C5M_MODS", home_dir() + "/c5-instances/c5h-b/Documents/My Games/Sid Meier's Civilization 5/MODS");
    DirectoryProvider dp;
    Error e;
    if (!dp.add_root("fpk", samples + "/fpk", &e) || !dp.add_root("install", inst, &e) || !dp.add_root("mods", mods, &e)) {
        std::printf("SKIP real examples: %s\n", e.what.c_str());
        t::skipped() = true;
        return;
    }
    CHECK(SqliteTables::available());
    SqliteTables sq;
    if (!sq.open(db, &e)) {
        std::printf("SKIP real examples: %s\n", e.what.c_str());
        t::skipped() = true;
        return;
    }
    ArtTables at;
    CHECK(at.load_database(sq, &e));
    CHECK(at.load_wonders(dp, &e));
    CHECK(at.wonder_files_missing.empty());
    CHECK(at.landmarks.size() > 600);

    std::vector<ResolvedArt> out;
    CHECK(resolve_landmark(at, dp, "ART_DEF_IMPROVEMENT_ACADEMY", "ART_DEF_RESOURCE_ALL", "Ancient", "Constructed", out, &e));
    CHECK_EQ(out.size(), (size_t)1);
    if (!out.empty()) {
        CHECK(out[0].complete());
        CHECK_EQ(basename_lower(out[0].mesh.name), std::string("anc_academy.gr2"));
        CHECK_EQ(out[0].textures.size(), (size_t)6);
        std::printf("%s", format_resolved(out[0]).c_str());
    }
    ResolvedArt w;
    CHECK(resolve_wonder(at, dp, "BUILDING_STONEHENGE", w, &e));
    CHECK(w.complete());
    CHECK_EQ(basename_lower(w.mesh.name), std::string("stonehenge.gr2"));
    std::printf("%s", format_resolved(w).c_str());
    std::vector<ResolvedArt> u;
    CHECK(resolve_unit(at, dp, "UNIT_WARRIOR", u, &e));
    CHECK_EQ(u.size(), (size_t)3);
    for (auto& r : u) {
        CHECK(r.complete());
        CHECK(r.animations.size() >= 30);
        std::printf("%s", format_resolved(r).c_str());
    }
    // negative on the real data: a model name that is nowhere
    ResolvedArt nf;
    CHECK(!resolve_fxsxml(dp, "Assets/No/Such_Model_3104.fxsxml", nf, &e) && e.code == Err::NotFound);
}

}  // namespace

int main() {
    test_basename();
    test_xml_scanner();
    test_fxsxml();
    test_wonder_parser();
    test_tsv();
    test_chains();
    test_directory_provider();
    test_real();
    return t::finish();
}
