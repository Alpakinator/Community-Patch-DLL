// c5r/cities/catalogue.h - the city art catalogue: which model file and piece the EXE's City system draws for a
// culture, an era slot, a wall and a wonder or display building (ticket 4872, epic 0561; city-renderer.md 2.1).
//
// Written from the knowledge base's description of the EXE (D83; no EXE code): research/05-graphics-composition/
// cities-and-landmarks.md 1.2 (the city-set file, the seven culture names, positional era slots, the lighting rule),
// 1.6 (wonder art, the display buildings), 1.7 (the seven wall files, the Great Wall files and their pieces) and 1.10
// (art style -> culture, era -> slot). Nothing of the game is committed: the XML files and the database are read from
// the player's install and database at run time.
//
// Failure behaviour: every function returns a c5r::Status; nothing throws.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/status.h"

namespace c5r {
namespace gamefs { class GameFiles; }
namespace cities {

// ---------------------------------------------------------------------------------------------- cultures
// The EXE's seven culture names (cities-and-landmarks.md 1.2, the table at 0xB9A2E0), index = culture.
constexpr uint32_t kCultures = 7;
constexpr uint32_t kRuins = 6;
constexpr uint32_t kEurope = 3;
constexpr uint32_t kEraSlots = 4;
const char* cultureName(uint32_t culture);              // "America" .. "Ruins"; "?" out of range
int cultureIndex(const std::string& name);              // case-insensitive; -1 when not one of the seven

// The art style to culture (1.10, 0x765AA0): the ArtStyleTypes ID 0, 1, 2, 4, 5, 6 is the culture; anything else
// (3 included, and negative ids) is Europe.
uint32_t cultureFromArtStyle(int artStyleId);

// Era to slot (1.10, 0x7A1480): per era (database order) the first of CityArtType, then ArtPrefix, that matches
// ANCIENT / RENAISSANCE / INDUSTRIAL / MODERN (exact, upper case) gives slots 0-3; with no match the era before is
// tried, down to the first. slots: one per era; -1 when no era up to and including it matches. Checks: no eras
// [BadLayout].
struct EraRow { std::string type, cityArtType, artPrefix; };
Status eraSlots(const std::vector<EraRow>& eras, std::vector<int>& slots);

// ---------------------------------------------------------------------------------------------- city sets
// One <Era> of one <Culture> of a Civ5ArtDefines_CityBuildings*.xml (1.2).
struct CitySet {
    uint32_t culture = 0;
    uint32_t eraSlot = 0;          // positional: the n-th <Era> of the culture
    uint32_t lighting = 0;         // the lighting attribute's culture, else the culture; Mediterranean and Ruins -> 3
    std::string eraName;           // as written (read, never compared: "Rennaisance" does not matter)
    std::string granny;            // <PopulationBuildings><Granny>, lower-case base name
    std::string source;            // the file it came from (lower-case base name)
};
// Appends the sets of one file. Checks: malformed XML, a root that is not <BuildingArtInfos> [BadLayout]; a
// <Culture> without a name, a culture name not among the seven, more than kEraSlots <Era>, an <Era> without one
// <PopulationBuildings><Granny>, a lighting attribute naming no culture [BadLayout]. On failure out is unchanged.
Status parseCitySetFile(const char* data, size_t size, const std::string& source, std::vector<CitySet>& out);

// ---------------------------------------------------------------------------------------------- walls
// The seven wall files (1.7, the loader 0x64E4D0's table at 0xB9A5B0), in table order, and the three pieces it takes
// from each by model name.
constexpr uint32_t kWallFiles = 7;
const char* wallFile(uint32_t i);                       // "amer_walls.gr2" .. "wall_of_babylon.gr2"; "" out of range
extern const char* const kWallPieces[3];                // "WALL", "GATE", "TOWER"
// The Great Wall files and pieces, drawn by the City system half built and finished (1.7).
constexpr uint32_t kGreatWallFiles = 2;
const char* greatWallFile(uint32_t i);                  // "hb_great_wall.gr2", "great_wall.gr2"
extern const char* const kGreatWallPieces[4];           // "WALL1", "WALL2", "TOWER1", "TOWER2" (+ "_TREECUT" stencils)

// ---------------------------------------------------------------------------------------------- wonders
// A building with Buildings.DisplayPosition > 0 (1.6): the DLL sends GameplayWonderCreated for it.
enum class FormKind : uint8_t { Plain, Cultural, Era, Random };
struct WonderForm {
    std::string type;              // the WonderArtInfo Type ("POLYNESIAN CASTLE")
    FormKind kind = FormKind::Plain;
    std::string qualifier;         // the word added to the tag ("POLYNESIAN", "ANCIENT", "ONE"); "" for Plain
};
struct DisplayBuilding {
    std::string building;          // Buildings.Type
    std::string tag;               // ArtDefineTag
    int displayPosition = 0;
    bool cultural = false, era = false, random = false;   // ArtInfoCulturalVariation / EraVariation / RandomVariation
    bool worldWonder = false;      // BuildingClasses.MaxGlobalInstances = 1
    std::vector<WonderForm> forms; // the WonderArtInfo types it can draw (forms() below)
};
// The forms of a tag among the art types [I: the naming seen in the install's files, 1.6 and landmark-renderer.md 2.3;
// the EXE's own rule is not read]: Plain = the tag itself; Cultural = "<culture adjective> <tag>" with the adjective
// of a culture (AMERICAN, ASIAN, AFRICAN, EUROPEAN, MEDITERRANEAN, POLYNESIAN); Era = "<era word> <tag>" (ANCIENT,
// CLASSICAL, MEDIEVAL, RENAISSANCE, INDUSTRIAL, MODERN, POSTMODERN, FUTURE); Random = "<tag> <word>" (ONE, TWO,
// THREE, FOUR). Sorted by type.
std::vector<WonderForm> wonderForms(const std::string& tag, const std::vector<std::string>& artTypes);
const char* cultureAdjective(uint32_t culture);         // "AMERICAN" .. "POLYNESIAN"; "" for Ruins and out of range

// ---------------------------------------------------------------------------------------------- the catalogue
struct CatalogueCounts {
    uint32_t citySetFiles = 0;     // Civ5ArtDefines_CityBuildings*.xml read
    uint32_t citySets = 0;         // culture x era entries
    uint32_t citySetModels = 0;    // distinct .gr2 among them
    uint32_t wonderArtTypes = 0;   // distinct WonderArtInfo Types
    uint32_t wonderArtEntries = 0; // entries over every file (a later one replaces an earlier one)
    uint32_t worldWonders = 0;     // BuildingClasses.MaxGlobalInstances = 1
    uint32_t displayBuildings = 0; // DisplayPosition > 0
    uint32_t displayWorldWonders = 0;
    uint32_t displayWithoutArt = 0;// a display building none of whose forms is an art type
};

class ArtCatalogue {
public:
    // Reads every Civ5ArtDefines_CityBuildings*.xml of the install (base file first, then the others by name; a
    // later file's culture and slot replace an earlier one's [I]), the wonder art files (c5m::kWonderArtFiles through
    // c5m::ArtTables::load_wonders), Eras, Buildings and BuildingClasses from the database. Checks: no city-set file
    // [NotFound]; a file's errors (parseCitySetFile) with its name; a missing table or column [NotFound]; malformed
    // wonder art XML [BadLayout]. On failure nothing is kept.
    Status load(const gamefs::GameFiles& files, c5m::TableSource& db);
    bool loaded() const { return loaded_; }

    const std::vector<CitySet>& citySets() const { return sets_; }       // by culture, then slot
    const CitySet* citySet(uint32_t culture, uint32_t slot) const;      // null when the install has none
    const std::vector<std::string>& citySetFiles() const { return setFiles_; }
    const std::vector<c5m::WonderArt>& wonderArt() const { return wonders_; }   // the entry in force per Type
    const c5m::WonderArt* wonder(const std::string& type) const;
    const std::vector<DisplayBuilding>& displayBuildings() const { return display_; }
    const std::vector<EraRow>& eras() const { return eras_; }
    const std::vector<int>& eraSlotPerEra() const { return eraSlot_; }
    int eraSlot(const std::string& eraType) const;                      // -1 when unknown or unmatched
    const CatalogueCounts& counts() const { return counts_; }
    const std::vector<std::string>& wonderFilesMissing() const { return wonderMissing_; }

private:
    bool loaded_ = false;
    std::vector<CitySet> sets_;
    std::vector<std::string> setFiles_;
    std::vector<c5m::WonderArt> wonders_;
    std::map<std::string, size_t> wonderIndex_;
    std::vector<DisplayBuilding> display_;
    std::vector<EraRow> eras_;
    std::vector<int> eraSlot_;
    CatalogueCounts counts_;
    std::vector<std::string> wonderMissing_;
};

}}  // namespace c5r::cities
