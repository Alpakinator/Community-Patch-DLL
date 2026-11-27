// c5r/gamefs/gamefiles.h - the game's virtual file system, rebuilt from an install: lookup by bare lower-case file
// name in the order the EXE resolves names (task 2901). Evidence and confidence marks:
// research/05-graphics-composition/asset-pipeline.md section 1.2.
//
// Model (search order, first layer that has the name wins):
//   1. extra loose roots (Options.extraLooseRoots, e.g. mod folders) when extraLooseFirst; earlier roots win   [I]
//   2. the package layer: every Assets/DLC/*/*.Civ5Pkg, in ascending (Priority, folder name) order; first all their
//      packs (<Paks>, <Common><Paks>, the active UISkin's Skin/GameplaySkin <Paks>; Resource/DX9/<n>.fpk, else
//      Resource/Common/<n>.fpk) with replace flags 4, then each package's own folder (the "Common" kind's loose files,
//      Options.mountPackageRoots; every Directory / MapDirectory / Skin and GameplaySkin Directory the package names,
//      of any skin, is excluded), then all their directories (<Gameplay><Directory> and <MapDirectory>, the active
//      UISkin's Skin/GameplaySkin <Directory>); Sounds/Speech and Scenarios excluded everywhere, flags 7
//      [C rule, I order; package folders: issue 3404, asset-pipeline.md 1.2]
//   3. Resource/DX9_LOW base packs, only with Options.includeDx9Low (the EXE mounts them into their own file
//      system above the base one when its texture-quality global 0xD90F90 is 0)                              [C]
//   4. the base file system (searched last by the EXE's lookup 0x8181E0): the 32 Resource/DX9 packs named in the
//      EXE's table 0xB943A0 and 13 Resource/Common packs (flags 0), Resource/Overrides (flags 4), Assets/ minus
//      */DLC/* (flags 1, or 6 with LooseFilesOverridePAK = 1)                                                  [C]
//   5. extra loose roots when !extraLooseFirst.
// Within a layer one entry per name; a later registration with flags F replaces an existing entry E when
// (F & 1 and E is loose) or (F & 0xE in {4, 6} and E is in a pack) or (F & 0xE == 2 or >= 8): the EXE's 0x832F30 [C].
// The time comparison the EXE adds for flags 6 (loose replaces a pack only if newer) is not emulated.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "c5r/gamefs/civ5pkg.h"
#include "c5r/gamefs/fpk.h"
#include "c5r/status.h"

namespace c5r { namespace gamefs {

class GameFiles {
public:
    struct Options {
        bool includeDx9Low = false;
        // Loose folders searched recursively, e.g. mod folders (MODS/(2) Vox Populi). Earlier roots win.
        std::vector<std::wstring> extraLooseRoots;
        bool extraLooseFirst = true;        // [I] the EXE imports mod files into its file system above the DLC
        std::string skinSet = "Expansion2"; // UISkin set whose directories and packs are mounted (BNW game)
        std::string platform = "Common";    // UISkin platform
        bool looseOverridesPak = false;     // config.ini LooseFilesOverridePAK (default 0)
        bool keepShadowed = false;          // keep every candidate per name, for duplicate analysis
        // Register each package's folder as the EXE's "Common" kind does when no package names a Directory for it
        // (0x630E50 kind 0, issue 3404). Off reproduces the model before 2026-11-13 (comparison only).
        bool mountPackageRoots = true;
    };

    // One registration of a name (with keepShadowed): where it came from and whether it is the one lookups return.
    struct Candidate {
        std::string origin;
        std::string layer;  // "extra", "packages", "dx9low", "base"
        bool fromPak = false;
        bool active = false;
        uint64_t size = 0;
    };

    GameFiles();
    ~GameFiles();
    GameFiles(const GameFiles&) = delete;
    GameFiles& operator=(const GameFiles&) = delete;

    // Opens the install at installRoot (the folder with Assets/ and Resource/). Packs that fail to open are reported
    // in warnings() and skipped; Io if Assets/ or Resource/ is missing.
    Status open(const std::wstring& installRoot, const Options& opt);
    Status open(const std::wstring& installRoot);  // default Options
    Status read(const std::string& name, std::vector<uint8_t>& out, std::string* origin = nullptr) const;
    bool exists(const std::string& name) const;
    size_t fileCount() const;
    std::vector<std::string> names() const;        // all resolvable names, sorted
    std::string origin(const std::string& name) const;  // "Resource/DX9/TerrainTextures.fpk:x.dds" or a loose path
    uint64_t size(const std::string& name) const;
    std::vector<Candidate> candidates(const std::string& name) const;  // needs keepShadowed; active one first
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::vector<std::wstring>& mountedPacks() const { return mountedPaths_; }
    const std::vector<Civ5Pkg>& packages() const { return packages_; }

    // The replace rule of the EXE (0x832F30), exposed for tests: does a registration with these flags replace an
    // existing entry that is loose (existingLoose) or in a pack?
    static bool replaces(uint32_t flags, bool existingLoose);

private:
    struct Ref {
        int32_t archive = -1;  // index into archives_, -1 = loose
        uint32_t index = 0;    // entry index, or index into loosePaths_
        uint64_t size = 0;
    };
    struct Layer {
        std::string label;
        std::unordered_map<std::string, Ref> map;
        std::unordered_map<std::string, std::vector<Ref>> shadowed;  // keepShadowed only
    };
    void registerName(Layer& L, const std::string& name, const Ref& r, uint32_t flags);
    int mountPack(Layer& L, const std::wstring& path, uint32_t flags);  // archive index or -1
    // excludeDirs: absolute folders (lower case, forward slashes) whose subtrees are skipped.
    void scanLoose(Layer& L, const std::wstring& dir, uint32_t flags, bool baseAssets, bool packageDir,
                   const std::vector<std::wstring>* excludeDirs = nullptr);
    std::string originOf(const Ref& r) const;
    const Ref* lookup(const std::string& name) const;

    Options opt_;
    std::wstring root_;
    std::vector<std::unique_ptr<FpkArchive>> archives_;
    std::vector<std::wstring> mountedPaths_;
    std::vector<std::wstring> loosePaths_;
    std::vector<Layer> layers_;  // search order
    std::unordered_map<std::string, std::pair<int, Ref>> final_;  // name -> (layer, ref)
    std::vector<Civ5Pkg> packages_;
    std::vector<std::string> warnings_;
};

// The base pack lists the EXE mounts at start (table 0xB943A0 and the pushes at 0x6D280A..0x6D28AD) [C].
const std::vector<std::string>& baseDx9Packs();
const std::vector<std::string>& baseCommonPacks();
const std::vector<std::string>& baseDx9LowPacks();  // table 0xB94340, mounted only when 0xD90F90 == 0

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);

}} // namespace c5r::gamefs
