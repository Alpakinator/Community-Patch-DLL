// test_mod_roots.cpp - issue 0443: the rule of units_mods.h that gives the units tests their install's mod roots
// (C5R_UNITS_MODS, else a pool instance's MODS folders, else none), on a hand-made instance layout under C5R_WORK.
// No game data.
#include <fstream>
#include <string>
#include <vector>

#include "units_mods.h"
#include "units_testkit.h"

using units_test::modRootsFor;

namespace {

void mkdirs(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '\\' || p[i] == '/') {
            if (!cur.empty() && cur.back() != ':') CreateDirectoryW(c5r::gamefs::fromUtf8(cur).c_str(), nullptr);
        }
        if (i < p.size()) cur += p[i];
    }
}

void writeFile(const std::string& p, const std::string& text) {
    std::ofstream f(c5r::gamefs::fromUtf8(p).c_str(), std::ios::binary | std::ios::trunc);
    f << text;
}

// A fresh layout root under C5R_WORK (old contents of this test's own folders are overwritten, not deleted).
std::string root(const char* name) {
    const std::string r = c5r::test::workDir() + "\\mod_roots_0443\\" + name;
    mkdirs(r);
    return r;
}

C5R_TEST(json_string_values) {
    using units_test::jsonString;
    CHECK_EQ(jsonString("{\"user_dir\": \"Documents\\\\My Games\\\\X\"}", "user_dir"), std::string("Documents\\My Games\\X"));
    CHECK_EQ(jsonString("{\"a\":\"q\\\"r\", \"user_dir\":\"u\"}", "a"), std::string("q\"r"));
    CHECK_EQ(jsonString("{\"name\": \"c5h-d\"}", "user_dir"), std::string());
    CHECK_EQ(jsonString("{\"user_dir\": \"open", "user_dir"), std::string());   // unterminated
    CHECK_EQ(units_test::parentDir("F:\\i\\c5h-d\\install\\"), std::string("F:\\i\\c5h-d"));
    CHECK_EQ(units_test::parentDir("install"), std::string());
}

C5R_TEST(env_value_wins) {
    const std::string r = root("env");
    mkdirs(r + "\\install");
    writeFile(r + "\\instance.json", "{\"user_dir\": \"U\"}");
    mkdirs(r + "\\U\\MODS\\(1) A");
    std::string how;
    const std::vector<std::string> got = modRootsFor(r + "\\install", "X:\\m1;;X:\\m2;", &how);
    CHECK(got == std::vector<std::string>({"X:\\m1", "X:\\m2"}));
    CHECK_EQ(how, std::string("C5R_UNITS_MODS"));
}

C5R_TEST(pool_instance_every_mods_folder_sorted) {
    const std::string r = root("pool");
    mkdirs(r + "\\install");
    writeFile(r + "\\instance.json", "{\n \"name\": \"t\",\n \"user_dir\": \"Docs\\\\My Games\\\\Civ\",\n \"x\": 1\n}");
    const std::string mods = r + "\\Docs\\My Games\\Civ\\MODS";
    mkdirs(mods + "\\(2) Vox Populi");
    mkdirs(mods + "\\(1) Community Patch");
    mkdirs(mods + "\\b lower");
    writeFile(mods + "\\(9) packed.zip", "not a folder");
    std::string how;
    const std::vector<std::string> got = modRootsFor(r + "\\install\\", "", &how);
    CHECK(got == std::vector<std::string>({mods + "\\(1) Community Patch", mods + "\\(2) Vox Populi", mods + "\\b lower"}));
    CHECK_EQ(how, "pool instance: " + mods);
}

C5R_TEST(pool_instance_default_user_dir) {
    const std::string r = root("pooldefault");
    mkdirs(r + "\\install");
    writeFile(r + "\\instance.json", "{\"name\": \"t\"}");
    const std::string mods = r + "\\Documents\\My Games\\Sid Meier's Civilization 5\\MODS";
    mkdirs(mods + "\\(2) Vox Populi");
    const std::vector<std::string> got = modRootsFor(r + "\\install", "", nullptr);
    CHECK(got == std::vector<std::string>({mods + "\\(2) Vox Populi"}));
}

C5R_TEST(no_instance_no_roots) {
    const std::string r = root("plain");
    mkdirs(r + "\\install");
    mkdirs(r + "\\Documents\\My Games\\Sid Meier's Civilization 5\\MODS\\(2) Vox Populi");   // not read without instance.json
    std::string how;
    CHECK(modRootsFor(r + "\\install", "", &how).empty());
    CHECK_EQ(how.compare(0, 4, "none"), 0);
    // an instance.json whose MODS folder is missing: no roots, not a failure
    const std::string r2 = root("nomods");
    mkdirs(r2 + "\\install");
    writeFile(r2 + "\\instance.json", "{\"user_dir\": \"U\"}");
    CHECK(modRootsFor(r2 + "\\install", "", nullptr).empty());
}

}  // namespace

C5R_TEST_MAIN
