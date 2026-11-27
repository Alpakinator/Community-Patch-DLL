// landmarks_testkit.h - the inputs the c5r::landmarks and c5r::cities tests read at run time (ticket 4872): the install
// (C5R_GAME_DIR, as every render test), the game database copy and the copied capture samples. A missing input skips
// (and build.py counts a skip as a failure). Nothing of the game is in the repository (D83).
//
//   C5R_LANDMARK_DB       the database copy (Civ5DebugDatabase.db of a CP + VP game); else C5R_UNITS_DB; else the
//                         units tests' default copy F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db (read once)
//   C5R_LANDMARK_SAMPLES  the folder the capture samples were copied to (landmark-renderer.md 4: <export>/draws/... and
//                         <export>/buffers/... of 3910's W01, Fanc01, Lanc07, Lanc18, Lanc20); default the kept folder
//                         C:/c5-64bit-work/keep/landmark-oracle-samples (ticket 0621: never under a ticket's tree, which a
//                         clean-up removes; was C:/c5-64bit-work/0561/4872/samples)
#pragma once
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/cities/catalogue.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/library.h"
#include "testkit.h"

namespace lmt {

inline std::string env(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : def;
}

inline std::string dbPath() {
    return env("C5R_LANDMARK_DB", env("C5R_UNITS_DB", "F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db"));
}

inline std::string samplesDir() { return env("C5R_LANDMARK_SAMPLES", "C:/c5-64bit-work/keep/landmark-oracle-samples"); }

inline bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(c5r::gamefs::fromUtf8(path).c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// The install, the database, the catalogue and the library, loaded once per test executable.
struct Install {
    c5r::gamefs::GameFiles files;
    c5m::SqliteTables db;
    c5r::cities::ArtCatalogue art;
    c5r::landmarks::ModelLibrary lib;
    c5r::Status artStatus, libStatus;
    bool opened = false;
};

inline Install& install(bool withLibrary) {
    static Install in;
    static bool tried = false, triedLib = false;
    const std::string game = c5r::test::gameDir();
    if (game.empty()) c5r::test::skip("C5R_GAME_DIR not set");
    if (!c5m::SqliteTables::available()) c5r::test::skip("winsqlite3.dll not available");
    if (!tried) {
        tried = true;
        c5m::Error e;
        if (!in.db.open(dbPath(), &e)) c5r::test::skip("database not opened: " + dbPath() + ": " + e.what);
        c5r::Status s = in.files.open(c5r::gamefs::fromUtf8(game));
        if (!s.ok()) c5r::test::skip("install not opened: " + s.str());
        in.artStatus = in.art.load(in.files, in.db);
        in.opened = true;
    }
    if (!in.opened) c5r::test::skip("install or database not opened");
    if (withLibrary && !triedLib) {
        triedLib = true;
        in.libStatus = in.lib.load(in.files, in.db, in.art);
    }
    return in;
}

}  // namespace lmt
