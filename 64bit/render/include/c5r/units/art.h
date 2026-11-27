// c5r/units/art.h - L0 art: from a unit type and its owner to the art row, the member infos and every file they name,
// through the install's and the active mods' files and the game database (epic 0041, U1 / issue 4352;
// research/05-graphics-composition/unit-renderer.md 6.1 and 10.1).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/units/status.h"

namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace units {

// The renderer's GameFiles (install packs, DLC, the active mods as extraLooseRoots) seen as models' FileProvider.
// Lookup by lower-case basename, as both sides key files. Not owning; the GameFiles outlives it.
class GameFilesProvider : public c5m::FileProvider {
public:
    explicit GameFilesProvider(const gamefs::GameFiles& gf) : gf_(gf) {}
    bool read(const std::string& basename_lower, c5m::Bytes& out, std::string* origin) override;
    bool exists(const std::string& basename_lower, std::string* origin) override;
private:
    const gamefs::GameFiles& gf_;
};

struct MemberArt {
    std::string member;          // ArtDefine_UnitMemberInfos.Type
    int count = 0;               // ArtDefine_UnitInfoMemberInfos.NumMembers
    float scale = 0;             // Scale (a float in the database)
    float zoffset = 0;           // ZOffset (NULL = 0)
    std::string domain, material_tag;
    std::string model;           // Model: the .fxsxml as written
    c5m::ResolvedArt files;      // .fxsxml, .gr2, animations, textures (models' chain 3)
};

struct UnitArt {
    std::string unit_type;       // "" when resolved from an art row directly
    std::string art_row;         // ArtDefine_UnitInfos.Type
    std::string formation;       // ArtDefine_UnitInfos.Formation ("" for NULL)
    int damage_states = 0;
    std::vector<MemberArt> members;   // ArtDefine_UnitInfoMemberInfos row order
    uint32_t figures() const;         // sum of the members' counts
};

// The art tables of one database, loaded once (open), and the resolution rules.
class ArtResolver {
public:
    // Loads Units, ArtDefine_UnitInfos, _UnitInfoMemberInfos, _UnitMemberInfos (ZOffset included), Eras,
    // Civilizations and MinorCivilizations (art style suffixes), PlayerColors and Colors. Code BadInput with the
    // database's message on a missing table or column.
    UnitStatus open(c5m::TableSource& db, c5m::FileProvider& files);

    // The art row a unit of `unit_type` shows for an owner of `art_style` in `era` (unit-renderer.md 10.1):
    //   cultural (UnitArtInfoCulturalVariation): <base><suffix> when that row exists, else <base>;
    //   era (UnitArtInfoEraVariation): <base>_<ArtPrefix> for the owner's era and then each earlier era, then
    //   <base>_LATE from the Industrial era on, else <base>.
    // art_style: an ArtStyleSuffix ("_AFRI"), a civilization or minor civilization Type (its suffix), or an
    // ArtStyleType whose civilizations all share one suffix; "" = no cultural variation. era: an Eras.Type, "" = none.
    // NoArtRow when the type is unknown, has no UnitArtInfo, or the row does not exist; BadInput for an art style
    // or era the database does not know.
    UnitStatus art_row(const std::string& unit_type, const std::string& art_style, const std::string& era,
                       std::string& row) const;

    // The art row -> member infos -> member rows -> files. NoArtRow, NoMembers, ModelNotFound (.fxsxml or .gr2),
    // BadModel (a malformed .fxsxml). Textures and animations missing are left to the model stage (it names them).
    // Issue 4464: cached per art row. The result depends only on the row, the tables read by open() and the files'
    // provider given to open(), so each row is resolved (its members' .fxsxml found, read and parsed) once after an
    // open() and every later call returns the same status and art (a failure included) without reading a file.
    // open() (a new database) and clear_cache() drop the cache. out.unit_type is "" (the row's art, not a unit's).
    UnitStatus resolve(const std::string& art_row, UnitArt& out);
    // The same, without a copy: `out` shares the cached art (never null; what resolve() would have filled in, a failed
    // row's partial art included; kept alive by the pointer across a clear or an open()). For the per-frame callers.
    UnitStatus resolve_shared(const std::string& art_row, std::shared_ptr<const UnitArt>& out);
    // The rule itself, uncached: every call reads and parses the members' .fxsxml again (the tests' reference and
    // UnitRenderer's cache-off arm).
    UnitStatus resolve_uncached(const std::string& art_row, UnitArt& out);
    // Drop every cached row (a caller whose files changed under the same tables). open() does it too.
    void clear_cache() { cache_.clear(); }
    size_t cached_rows() const { return cache_.size(); }
    // resolve_fxsxml calls since the resolver was made or last opened (every member of every uncached resolution).
    uint64_t fxsxml_parses() const { return fxsxml_parses_; }

    // Team colour: PlayerColors.SecondaryColor -> Colors, each channel as the EXE writes it into palette row 0:
    // trunc(float(v) * 255) / 255, alpha 1 (research unit-renderer.md 10.3). BadInput when the Type or colour is unknown.
    UnitStatus team_colour(const std::string& player_colour, float rgba[4]) const;

    // For the pre-check: every Units.Type (database order), every art style suffix present (sorted, "" first), every
    // Eras.Type (era order).
    const std::vector<std::string>& unit_types() const { return unit_types_; }
    const std::vector<std::string>& style_suffixes() const { return suffixes_; }
    const std::vector<std::string>& eras() const { return eras_; }
    bool has_row(const std::string& art_row) const;
    std::string suffix_of(const std::string& art_style, bool* ok) const;

    c5m::FileProvider* files() const { return files_; }

private:
    struct UnitRow { std::string art; bool cultural = false, era = false; };
    struct InfoRow { std::string formation; int damage_states = 0; };
    struct MemberRow { float scale = 0, zoffset = 0; std::string domain, model, material; bool model_null = false; };
    std::map<std::string, UnitRow> units_;
    std::map<std::string, InfoRow> infos_;
    std::map<std::string, std::vector<std::pair<std::string, int>>> info_members_;
    std::map<std::string, MemberRow> members_;
    std::vector<std::pair<std::string, std::string>> era_prefix_;    // Eras order: Type, ArtPrefix
    std::map<std::string, std::string> civ_suffix_;                  // civ / minor civ Type -> ArtStyleSuffix
    std::map<std::string, std::vector<std::string>> style_suffixes_; // ArtStyleType -> distinct suffixes
    std::map<std::string, std::string> player_colour_;               // PlayerColors.Type -> SecondaryColor
    std::map<std::string, std::vector<float>> colours_;              // Colors.Type -> r, g, b, a
    std::vector<std::string> unit_types_, suffixes_, eras_;
    c5m::FileProvider* files_ = nullptr;
    struct Cached { UnitStatus status; std::shared_ptr<const UnitArt> art; };
    std::map<std::string, Cached> cache_;   // issue 4464: art row -> its resolution
    uint64_t fxsxml_parses_ = 0;
};

}}  // namespace c5r::units
