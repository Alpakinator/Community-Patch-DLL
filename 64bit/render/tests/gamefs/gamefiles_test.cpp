// render/tests/gamefs/gamefiles_test.cpp - XML / Civ5Pkg parser and the lookup order on a synthetic install
// (task 2901). The install is built under C5R_WORK (D:), never in a game folder.
#include <cstdio>
#include <windows.h>
#include "c5r/gamefs/civ5pkg.h"
#include "c5r/gamefs/gamefiles.h"
#include "synth.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::gamefs;

namespace {
Status xml(const char* s, std::unique_ptr<XmlNode>& r) { return parseXml(s, std::strlen(s), r); }
Status pkg(const char* s, Civ5Pkg& p) { return parseCiv5Pkg(s, std::strlen(s), p); }

void mkdirs(const std::string& p) {
    std::string cur;
    for (char c : p) {
        cur += c;
        if (c == '/' && cur.size() > 3) CreateDirectoryA(cur.c_str(), nullptr);
    }
    CreateDirectoryA(p.c_str(), nullptr);
}
void writeFile(const std::string& p, const std::vector<uint8_t>& v) {
    size_t sl = p.rfind('/');
    mkdirs(p.substr(0, sl));
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return;
    if (!v.empty()) std::fwrite(v.data(), 1, v.size(), f);
    std::fclose(f);
}
void writeText(const std::string& p, const char* s) {
    writeFile(p, std::vector<uint8_t>(s, s + std::strlen(s)));
}
std::vector<uint8_t> bytes(const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }
std::string text(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }
std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// A small install exercising every layer and rule. Contents name their origin.
std::string makeInstall() {
    std::string root = c5r::test::workDir() + "/c5r_gamefs_install";
    // Base packs: TerrainTextures (DX9 list) and TerrainModels (Common list). "both.dds" is in both: the first mounted
    // (DX9, flags 0) must win. "over.dds" is also in Resource/Overrides (flags 4 replaces a pack entry).
    writeFile(root + "/Resource/DX9/TerrainTextures.fpk",
              synth::fpk({{"both.dds", bytes("dx9")}, {"over.dds", bytes("pack")}, {"loose.dds", bytes("pack")},
                          {"pkgpak.dds", bytes("base")}, {"pkgdir.dds", bytes("base")}}));
    writeFile(root + "/Resource/Common/TerrainModels.fpk",
              synth::fpk({{"both.dds", bytes("common")}, {"terraindef.csv", bytes("base-pack")}}));
    writeFile(root + "/Resource/Overrides/over.dds", bytes("overrides"));
    // Base loose: loose.dds exists in a pack, so flags 1 keeps the pack; only.dds is new; DLC folder excluded.
    writeFile(root + "/Assets/terrain/loose.dds", bytes("assets"));
    writeFile(root + "/Assets/terrain/only.dds", bytes("assets"));
    writeFile(root + "/Assets/DLC/Unlisted/notmounted.dds", bytes("dlc-not-a-package-dir"));
    // Low-quality pack, mounted only with includeDx9Low.
    writeFile(root + "/Resource/DX9_Low/TerrainTexturesDX9Low.fpk", synth::fpk({{"both.dds", bytes("low")}}));
    // Packages: A (priority 100) and B (priority 200). Both ship a pack with pkgpak.dds; B's is mounted later and wins.
    // A has a directory with pkgdir.dds (flags 7: beats every pack, including B's pack below).
    writeFile(root + "/Resource/DX9/PkgATextures.fpk", synth::fpk({{"pkgpak.dds", bytes("A-pack")}, {"pkgdir.dds", bytes("A-pack")}}));
    writeFile(root + "/Resource/Common/PkgBModels.fpk", synth::fpk({{"pkgpak.dds", bytes("B-pack")}, {"pkgdir.dds", bytes("B-pack")}}));
    writeText(root + "/Assets/DLC/A/A.Civ5Pkg",
              "<?xml version=\"1.0\"?>\n<Civ5Package><Priority>100</Priority><Paks><File>PkgATextures.fpk</File></Paks>"
              "<UISkin name=\"X\" set=\"Expansion2\" platform=\"Common\"><GameplaySkin><Directory>Terrain</Directory>"
              "</GameplaySkin></UISkin><UISkin name=\"Y\" set=\"BaseGame\" platform=\"Common\"><GameplaySkin>"
              "<Directory>Other</Directory></GameplaySkin></UISkin></Civ5Package>");
    writeFile(root + "/Assets/DLC/A/Terrain/pkgdir.dds", bytes("A-dir"));
    writeFile(root + "/Assets/DLC/A/Terrain/Scenarios/scen.dds", bytes("excluded"));
    writeFile(root + "/Assets/DLC/A/Other/otherskin.dds", bytes("other-skin"));
    writeText(root + "/Assets/DLC/B/B.Civ5Pkg",
              "<Civ5Package><Priority>200</Priority><Common><Paks><File>PkgBModels.fpk</File><File>Missing.fpk</File>"
              "</Paks></Common></Civ5Package>");
    writeText(root + "/Assets/DLC/Broken/Broken.Civ5Pkg", "<Civ5Package><Priority>x</Priority></Civ5Package>");
    // Package S, shaped like Assets/DLC/Shared (Upgrade1.Civ5Pkg): Paks only, no Directory of any kind. Its folder is
    // registered as the "Common" kind's loose files (flags 7, issue 3404): Terrain/terraindef.csv beats the base pack's
    // copy and a package pack's copy; Sounds/Speech and Scenarios below it stay out. Package G names a Gameplay
    // Directory and a MapDirectory (both mounted as before) and a BaseGame skin directory (inactive skin: excluded from
    // its folder's scan too).
    writeFile(root + "/Resource/DX9/PkgSTextures.fpk", synth::fpk({{"terraindef.csv", bytes("S-pack")}}));
    writeText(root + "/Assets/DLC/S/S.Civ5Pkg",
              "<Civ5Package><Paks><File>PkgSTextures.fpk</File></Paks></Civ5Package>");
    writeFile(root + "/Assets/DLC/S/Terrain/TerrainDef.csv", bytes("S-loose"));
    writeFile(root + "/Assets/DLC/S/Sounds/Speech/speech.wav", bytes("excluded"));
    writeFile(root + "/Assets/DLC/S/Scenarios/s2.dds", bytes("excluded"));
    writeText(root + "/Assets/DLC/G/G.Civ5Pkg",
              "<Civ5Package><Gameplay><Directory>Gameplay</Directory><MapDirectory>Maps</MapDirectory></Gameplay>"
              "<UISkin name=\"Z\" set=\"BaseGame\" platform=\"Common\"><Skin><Directory>UI/Base</Directory></Skin>"
              "</UISkin></Civ5Package>");
    writeFile(root + "/Assets/DLC/G/Gameplay/gdir.xml", bytes("G-gameplay"));
    writeFile(root + "/Assets/DLC/G/Maps/gmap.civ5map", bytes("G-map"));
    writeFile(root + "/Assets/DLC/G/UI/Base/baseskin.xml", bytes("G-baseskin"));
    writeFile(root + "/Assets/DLC/G/UI/loose.lua", bytes("G-root"));
    // Two packages in one folder (like UI_bc1's EUI_0 / EUI_2), one skin each: the inactive skin's directory stays out of
    // the folder scan although the other package does not name it (one exclusion list over all packages).
    writeText(root + "/Assets/DLC/U/U0.Civ5Pkg",
              "<Civ5Package><UISkin name=\"B\" set=\"BaseGame\" platform=\"Common\"><Skin><Directory>Art0</Directory>"
              "</Skin></UISkin></Civ5Package>");
    writeText(root + "/Assets/DLC/U/U2.Civ5Pkg",
              "<Civ5Package><UISkin name=\"E\" set=\"Expansion2\" platform=\"Common\"><Skin><Directory>Art2</Directory>"
              "</Skin></UISkin></Civ5Package>");
    writeFile(root + "/Assets/DLC/U/Art0/u0.xml", bytes("U-basegame"));
    writeFile(root + "/Assets/DLC/U/Art2/u2.xml", bytes("U-expansion2"));
    writeFile(root + "/Assets/DLC/U/rootfile.txt", bytes("U-root"));
    // Extra loose roots: mod1 wins over mod2 and over everything else.
    writeFile(root + "/../c5r_gamefs_mod1/Art/both.dds", bytes("mod1"));
    writeFile(root + "/../c5r_gamefs_mod2/Art/both.dds", bytes("mod2"));
    return root;
}
} // namespace

C5R_TEST(xml_parses_elements_attributes_text) {
    std::unique_ptr<XmlNode> r;
    CHECK_OK(xml("\xEF\xBB\xBF<?xml version=\"1.0\"?><!-- c --><A x='1' y=\"a&amp;b\"> t <B/><B>u<![CDATA[<v>]]></B>"
                 "<!-- in --></A>  ", r));
    CHECK(r && r->name == "A");
    CHECK(r->attrs["y"] == "a&b");
    CHECK_EQ(r->all("B").size(), size_t(2));
    CHECK(r->texts("B")[1] == "u<v>");
    CHECK(r->text == "t");
}

C5R_TEST(negative_xml_truncated) {
    std::unique_ptr<XmlNode> r;
    CHECK_ERR(xml("<A><B>", r), Err::Truncated);
    CHECK_ERR(xml("<A x=", r), Err::Truncated);
    CHECK_ERR(xml("<A x='1", r), Err::Truncated);
    CHECK_ERR(xml("<A", r), Err::Truncated);
    CHECK_ERR(xml("<!-- never closed", r), Err::Truncated);
    CHECK_ERR(xml("<A><!-- never closed", r), Err::Truncated);
    CHECK_ERR(xml("<A></A", r), Err::Truncated);
    CHECK_ERR(xml("<A>x<![CDATA[y", r), Err::Truncated);
    CHECK(!r);
}

C5R_TEST(negative_xml_mismatched_close) {
    std::unique_ptr<XmlNode> r;
    CHECK_ERR(xml("<A><B></A></B>", r), Err::BadLayout);
}

C5R_TEST(negative_xml_no_root_or_trailing) {
    std::unique_ptr<XmlNode> r;
    CHECK_ERR(xml("   ", r), Err::BadLayout);
    CHECK_ERR(xml("text", r), Err::BadLayout);
    CHECK_ERR(xml("<A/><B/>", r), Err::BadLayout);
    CHECK_ERR(xml("<A/>tail", r), Err::BadLayout);
}

C5R_TEST(negative_xml_too_deep) {
    std::string s;
    for (int i = 0; i < 70; ++i) s += "<a>";
    for (int i = 0; i < 70; ++i) s += "</a>";
    std::unique_ptr<XmlNode> r;
    CHECK_ERR(xml(s.c_str(), r), Err::BadLayout);
}

C5R_TEST(negative_xml_bad_attribute) {
    std::unique_ptr<XmlNode> r;
    CHECK_ERR(xml("<A x=1/>", r), Err::BadLayout);
    CHECK_ERR(xml("<A x/>", r), Err::BadLayout);
    CHECK_ERR(xml("<A =\"1\"/>", r), Err::BadLayout);
    CHECK_ERR(xml("<A /x>", r), Err::BadLayout);
    CHECK_ERR(xml("< A/>", r), Err::BadLayout);
}

C5R_TEST(civ5pkg_extracts_mount_fields) {
    Civ5Pkg p;
    CHECK_OK(pkg("<Civ5Package><GUID>{g}</GUID><SteamApp>1</SteamApp><Priority>250</Priority><Key>k</Key>"
                 "<Paks><File>A.fpk</File></Paks><Common><Paks><File>B.fpk</File></Paks></Common>"
                 "<Gameplay><Directory>Gameplay</Directory><MapDirectory>Maps</MapDirectory></Gameplay>"
                 "<UISkin name=\"n\" set=\"Expansion2\" platform=\"Common\"><Skin><Directory>UI/Art</Directory>"
                 "<Paks><File>C.fpk</File></Paks></Skin><GameplaySkin><Directory>UI/InGame</Directory></GameplaySkin>"
                 "</UISkin></Civ5Package>", p));
    CHECK_EQ(p.priority, 250);
    CHECK(p.hasSteamApp && p.hasKey && p.guid == "{g}");
    CHECK(p.paks.size() == 2 && p.paks[1] == "B.fpk");
    CHECK(p.gameplayDirs.size() == 1 && p.gameplayDirs[0] == "Gameplay");
    CHECK(p.gameplayMapDirs.size() == 1 && p.gameplayMapDirs[0] == "Maps");
    CHECK(p.skins.size() == 1 && p.skins[0].set == "Expansion2" && p.skins[0].skinPaks[0] == "C.fpk");
    CHECK(p.skins[0].skinDirs[0] == "UI/Art" && p.skins[0].gameplaySkinDirs[0] == "UI/InGame");
    CHECK_OK(pkg("<Civ5Package/>", p));
    CHECK_EQ(p.priority, 0);
}

C5R_TEST(negative_civ5pkg_root_and_priority) {
    Civ5Pkg p;
    CHECK_ERR(pkg("<Package/>", p), Err::BadLayout);
    CHECK_ERR(pkg("<Civ5Package><Priority>1x</Priority></Civ5Package>", p), Err::BadLayout);
    CHECK_ERR(pkg("<Civ5Package><Priority></Priority></Civ5Package>", p), Err::BadLayout);
    CHECK_ERR(pkg("<Civ5Package><Priority>1234567890</Priority></Civ5Package>", p), Err::BadLayout);
    CHECK_ERR(pkg("<Civ5Package>", p), Err::Truncated);
}

C5R_TEST(replace_rule_matches_exe_0x832f30) {
    // flags 0: never replaces.
    CHECK(!GameFiles::replaces(0, true));
    CHECK(!GameFiles::replaces(0, false));
    // flags 1 (base Assets, LooseFilesOverridePAK 0): replaces loose, keeps packs.
    CHECK(GameFiles::replaces(1, true));
    CHECK(!GameFiles::replaces(1, false));
    // flags 4 (packs of packages, Resource/Overrides): replaces packs, keeps loose.
    CHECK(!GameFiles::replaces(4, true));
    CHECK(GameFiles::replaces(4, false));
    // flags 6 (Assets with LooseFilesOverridePAK 1): replaces packs (the EXE adds a time check), keeps loose.
    CHECK(!GameFiles::replaces(6, true));
    CHECK(GameFiles::replaces(6, false));
    // flags 7 (package directories): replaces both.
    CHECK(GameFiles::replaces(7, true));
    CHECK(GameFiles::replaces(7, false));
    // flags 2 (xml include pattern): replaces both.
    CHECK(GameFiles::replaces(2, true));
    CHECK(GameFiles::replaces(2, false));
}

C5R_TEST(gamefiles_lookup_order_on_synthetic_install) {
    std::string root = makeInstall();
    GameFiles gf;
    GameFiles::Options o;
    o.keepShadowed = true;
    CHECK_OK(gf.open(wide(root), o));
    std::vector<uint8_t> b;
    std::string origin;
    CHECK_OK(gf.read("both.dds", b, &origin));
    CHECK(text(b) == "dx9");                      // base packs: first mounted wins (flags 0)
    CHECK(origin == "Resource/DX9/TerrainTextures.fpk:both.dds");
    CHECK_OK(gf.read("OVER.DDS", b));
    CHECK(text(b) == "overrides");                // Resource/Overrides (flags 4) replaces a base pack entry
    CHECK_OK(gf.read("loose.dds", b));
    CHECK(text(b) == "pack");                     // base Assets (flags 1) does not replace a pack entry
    CHECK_OK(gf.read("Assets/terrain/only.dds", b, &origin));
    CHECK(text(b) == "assets" && origin == "Assets/terrain/only.dds");  // paths resolve by bare name
    CHECK(!gf.exists("notmounted.dds"));          // Assets/DLC/* is not part of the base scan
    CHECK_OK(gf.read("pkgpak.dds", b));
    CHECK(text(b) == "B-pack");                   // later (higher-priority) package pack replaces the earlier one
    CHECK_OK(gf.read("pkgdir.dds", b));
    CHECK(text(b) == "A-dir");                    // package directory (flags 7) beats every pack
    CHECK(!gf.exists("scen.dds"));                // Scenarios excluded in package directories
    CHECK(!gf.exists("otherskin.dds"));           // directories of other UISkin sets are not mounted
    CHECK_ERR(gf.read("nothing.dds", b), Err::NotFound);
    // bug 0091 item 5: NotFound clears the origin of the previous call (origin still holds "Assets/terrain/only.dds")
    CHECK(!origin.empty());
    CHECK_ERR(gf.read("nothing.dds", b, &origin), Err::NotFound);
    CHECK(origin.empty() && b.empty());
    CHECK_EQ(gf.packages().size(), size_t(6));    // A, B, G, S, U0, U2; Broken.Civ5Pkg reported, skipped
    bool sawBroken = false, sawMissing = false;
    for (const std::string& w : gf.warnings()) {
        sawBroken |= w.find("Broken.Civ5Pkg") != std::string::npos;
        sawMissing |= w.find("Missing.fpk") != std::string::npos;
    }
    CHECK(sawBroken && sawMissing);
    std::vector<GameFiles::Candidate> c = gf.candidates("both.dds");
    CHECK_EQ(c.size(), size_t(2));
    CHECK(c[0].active && c[0].layer == "base" && !c[1].active);
    CHECK(gf.size("pkgdir.dds") == 5);
    CHECK(gf.fileCount() == gf.names().size());
}

// Issue 3404: the package folder itself is mounted (the EXE's "Common" kind, 0x630E50 kind 0), minus the directories
// the package names for the other kinds, Sounds/Speech and Scenarios.
C5R_TEST(gamefiles_package_folder_is_mounted_like_the_shared_dlc) {
    std::string root = makeInstall();
    GameFiles gf;
    GameFiles::Options o;
    o.keepShadowed = true;
    CHECK_OK(gf.open(wide(root), o));
    std::vector<uint8_t> b;
    std::string origin;
    CHECK_OK(gf.read("terraindef.csv", b, &origin));
    CHECK(text(b) == "S-loose");                  // loose package-folder file beats the package pack and the base pack
    CHECK(origin == "Assets/DLC/S/Terrain/TerrainDef.csv");
    CHECK_OK(gf.read("Assets/DLC/Shared/Terrain/TerrainDef.csv", b));  // any path: bare name
    CHECK(text(b) == "S-loose");
    std::vector<GameFiles::Candidate> c = gf.candidates("terraindef.csv");
    CHECK_EQ(c.size(), size_t(3));                // S loose (active), S pack (shadowed), base pack
    CHECK(c.size() == 3 && c[0].active && !c[0].fromPak && c[2].layer == "base");
    CHECK_OK(gf.read("gdir.xml", b));
    CHECK(text(b) == "G-gameplay");               // Gameplay Directory, as before
    CHECK_OK(gf.read("gmap.civ5map", b));
    CHECK(text(b) == "G-map");                    // MapDirectory is a Gameplay directory too
    CHECK_OK(gf.read("loose.lua", b));
    CHECK(text(b) == "G-root");                   // a file of G's folder outside every named directory
    CHECK(gf.exists("s.civ5pkg") && gf.exists("g.civ5pkg"));
    CHECK_OK(gf.read("rootfile.txt", b));
    CHECK(text(b) == "U-root");                   // a shared package folder is scanned (once)
    CHECK_OK(gf.read("u2.xml", b));
    CHECK(text(b) == "U-expansion2");             // the active skin's directory
}

C5R_TEST(negative_gamefiles_package_folder_exclusions) {
    std::string root = makeInstall();
    GameFiles gf;
    CHECK_OK(gf.open(wide(root)));
    std::vector<uint8_t> b;
    CHECK_ERR(gf.read("speech.wav", b), Err::NotFound);     // *\Sounds\Speech\* below a package folder
    CHECK_ERR(gf.read("s2.dds", b), Err::NotFound);         // *\Scenarios\*
    CHECK_ERR(gf.read("baseskin.xml", b), Err::NotFound);   // a directory of an inactive skin stays out of the folder scan
    CHECK_ERR(gf.read("otherskin.dds", b), Err::NotFound);
    CHECK_ERR(gf.read("u0.xml", b), Err::NotFound);         // another package's inactive skin directory in a shared folder
    CHECK_ERR(gf.read("notmounted.dds", b), Err::NotFound); // a DLC folder without a Civ5Pkg is not a package
    // The option off: the model before 2026-11-13, the base pack's older copy wins.
    GameFiles g2;
    GameFiles::Options o2;
    o2.mountPackageRoots = false;
    CHECK_OK(g2.open(wide(root), o2));
    std::string origin;
    CHECK_OK(g2.read("terraindef.csv", b, &origin));
    CHECK(text(b) == "S-pack" && origin == "Resource/DX9/PkgSTextures.fpk:terraindef.csv");
    CHECK_ERR(g2.read("loose.lua", b), Err::NotFound);
}

C5R_TEST(gamefiles_dx9low_and_extra_roots) {
    std::string root = makeInstall();
    GameFiles gf;
    GameFiles::Options o;
    o.includeDx9Low = true;
    CHECK_OK(gf.open(wide(root), o));
    std::vector<uint8_t> b;
    CHECK_OK(gf.read("both.dds", b));
    CHECK(text(b) == "low");                      // DX9_Low file system is searched before the base one
    GameFiles g2;
    GameFiles::Options o2;
    o2.extraLooseRoots = {wide(root + "/../c5r_gamefs_mod1"), wide(root + "/../c5r_gamefs_mod2")};
    CHECK_OK(g2.open(wide(root), o2));
    CHECK_OK(g2.read("both.dds", b));
    CHECK(text(b) == "mod1");
    o2.extraLooseFirst = false;
    GameFiles g3;
    CHECK_OK(g3.open(wide(root), o2));
    CHECK_OK(g3.read("both.dds", b));
    CHECK(text(b) == "dx9");
    GameFiles g4;
    GameFiles::Options o4;
    o4.looseOverridesPak = true;                  // LooseFilesOverridePAK = 1: base loose files replace packs
    CHECK_OK(g4.open(wide(root), o4));
    CHECK_OK(g4.read("loose.dds", b));
    CHECK(text(b) == "assets");
}

C5R_TEST(negative_gamefiles_missing_install) {
    GameFiles gf;
    CHECK_ERR(gf.open(L"D:/c5-64bit-work/2900/test-out/no-such-install"), Err::Io);
    CHECK(!gf.exists("anything.dds"));
}
