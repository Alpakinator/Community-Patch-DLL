// c5models/art.h - art resolution: from the game's own tables and art-define files to the .fxsxml, .gr2 and .dds
// files a landmark, a wonder or a unit member draws with. Epic 3100, issue 3104 (team-3100), 2026-11-11.
// Own code, GPL-3.0-or-later like the rest of the repository.
//
// Pieces, each usable on its own:
//   FileProvider        bytes by lower-case basename (the engine's lookup key; research landmarks-and-models.md 2)
//   DirectoryProvider   a FileProvider over an ordered list of folders (extracted archives, loose Assets, MODS)
//   parse_xml()         a small XML scanner (elements, attributes, text; comments, PIs, DOCTYPE, CDATA skipped)
//   parse_fxsxml()      an <Asset> file: Mesh, Animation, Texture, BoneUsage, DecisionTree, TimedTrigger, ...
//   parse_wonder_art()  a <WonderArtInfos> file (civ5artdefines_*wonders*.xml)
//   TableSource         rows of the game database: SqliteTables (winsqlite3.dll from System32) or TsvTables
//   ArtTables           the art tables, loaded once
//   resolve_*()         the three chains -> ResolvedArt
// The library never opens game archives: the server joins epic 2900's FPK reader by implementing FileProvider.
#pragma once

#include "c5models/gr2.h"   // Bytes, Err, Error

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace c5m {

// ---------------------------------------------------------------------------------------------- files
// "Assets/Units/Warrior/Warrior.fxsxml" or "Buildings\\X.fxsxml" -> "warrior.fxsxml" (ASCII lower case).
std::string basename_lower(const std::string& path);
std::string ascii_lower(const std::string& s);

class FileProvider {
public:
    virtual ~FileProvider() = default;
    // Reads the file whose lower-case basename is `basename_lower`. origin: where it came from ("fpk:Archive/x.gr2").
    virtual bool read(const std::string& basename_lower, Bytes& out, std::string* origin) = 0;
    // Existence without reading (default: read()).
    virtual bool exists(const std::string& basename_lower, std::string* origin);
    // Every origin that holds the name, the winning one first; size() > 1 means duplicates. Default: none known.
    virtual std::vector<std::string> duplicates(const std::string& basename_lower) const;
};

// An ordered list of root folders, each indexed recursively (sorted walk) by lower-case basename. The first root
// that holds a name wins; inside one root the first file in the sorted walk wins. Every other copy is recorded.
class DirectoryProvider : public FileProvider {
public:
    struct Entry {
        std::string name;      // lower-case basename
        std::string origin;    // tag + ":" + path relative to the root, forward slashes
        std::wstring path;     // full path
        uint64_t size = 0;
    };
    // Indexes `dir` (UTF-8) recursively and appends it after the roots already added. Err::NotFound if `dir` is
    // not a folder.
    bool add_root(const std::string& tag, const std::string& dir, Error* err);
    bool read(const std::string& basename_lower, Bytes& out, std::string* origin) override;
    bool exists(const std::string& basename_lower, std::string* origin) override;
    std::vector<std::string> duplicates(const std::string& basename_lower) const override;
    const std::vector<Entry>& entries() const { return entries_; }   // every file, in root then walk order
    bool read_entry(const Entry& e, Bytes& out) const;
    size_t root_count() const { return roots_; }
private:
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::vector<size_t>> index_;   // name -> entry indices, winner first
    size_t roots_ = 0;
};

bool read_whole_file(const std::string& utf8_path, Bytes& out);

// ---------------------------------------------------------------------------------------------- XML
struct XmlElement {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;   // document order, entities decoded
    std::string text;                                          // concatenated character data, trimmed
    std::vector<XmlElement> children;
    int line = 0;
    const std::string* attr(const char* name) const;           // exact name; nullptr if absent
    const XmlElement* child(const char* name) const;           // first child of that name
};
// Parses one document (UTF-8, optional BOM). Errors: Err::Xml with the line number (unbalanced tags, bad
// attribute syntax, unterminated constructs, no or several root elements, UTF-16).
bool parse_xml(const char* data, size_t size, XmlElement& root, Error* err);
// The scanner's one lenience, counted (issue 4402, MC #2017): a comment ends at the first "-->", so a comment that
// holds "--" (e.g. a second "<!--" inside it, which strict XML refuses) is accepted; and character data is kept in
// XmlElement::text, never an error, so stray text at element level is skipped by the format readers. Everything
// else malformed is still refused. One VP file meets the first rule (unit-controller.md 6.1).
struct XmlLenience {
    int dashed_comments = 0;   // comments whose body holds "--"
    int text_elements = 0;     // elements with non-blank character data (kept in text)
};
bool parse_xml(const char* data, size_t size, XmlElement& root, Error* err, XmlLenience* lenience);

// ---------------------------------------------------------------------------------------------- .fxsxml
struct FxsAnimation {
    std::string file;
    std::string ec_text;          // as written ("1160, 1180")
    std::vector<int> ec;          // parsed codes
    std::string name, layer;      // optional attributes
};
struct FxsTimedTrigger {
    std::string file, state, model;   // state "3" or "0,1,2"; "" if absent
};
struct FxsAsset {
    std::string mesh;                             // the one <Mesh file>
    int mesh_count = 0;                           // <Mesh> elements seen (1 in every game file)
    std::vector<FxsAnimation> animations;
    std::vector<std::string> textures;
    std::vector<std::string> bones;               // <BoneUsage><Bone name>
    std::vector<std::string> decision_trees;
    std::vector<FxsTimedTrigger> timed_triggers;
    std::vector<std::string> anim_graphs;         // <AnimGraph file> (.dge)
    std::vector<std::string> state_machines;      // <StateMachine file> (.fsmxml)
    std::map<std::string, int> unknown;           // element name -> count, for elements not listed above
};
// Root must be <Asset>; Err::Xml otherwise, or when a known element lacks its `file` attribute, or an ec is not
// a list of integers.
bool parse_fxsxml(const char* data, size_t size, FxsAsset& out, Error* err);

// ---------------------------------------------------------------------------------------------- wonders
struct WonderArt {
    std::string type;      // <Type>, = Buildings.ArtDefineTag ("STONEHENGE", "THE PYRAMIDS")
    std::string state;     // <WonderArtState state>
    float scale = 0;       // <fScale>
    std::string granny;    // <Granny>, an .fxsxml path as written
    std::string source;    // which art-define file (origin)
};
// Appends one entry per <WonderArtInfo>/<WonderArtState>. Root must be <WonderArtInfos>.
bool parse_wonder_art(const char* data, size_t size, const std::string& source, std::vector<WonderArt>& out,
                      Error* err);
// The wonder art-define files of the base game and the DLC on c5h-b's install (landmarks-and-models.md 1.4), in
// the order they are applied: a later file's entry for the same Type replaces an earlier one [I].
extern const char* const kWonderArtFiles[];
extern const size_t kWonderArtFileCount;

// ---------------------------------------------------------------------------------------------- database
struct DbValue {
    bool null = true;
    std::string text;
};
using DbRow = std::vector<DbValue>;

class TableSource {
public:
    virtual ~TableSource() = default;
    // Every row of `table`, the given columns in that order. Err::NotFound for a missing table or column.
    virtual bool rows(const std::string& table, const std::vector<std::string>& columns, std::vector<DbRow>& out,
                      Error* err) = 0;
};

// The game's SQLite database through winsqlite3.dll (System32), opened read-only and immutable
// (file:...?mode=ro&immutable=1). Never writes. Err::NotFound if winsqlite3.dll or the file is missing.
class SqliteTables : public TableSource {
public:
    SqliteTables();
    ~SqliteTables() override;
    SqliteTables(const SqliteTables&) = delete;
    SqliteTables& operator=(const SqliteTables&) = delete;
    static bool available();                      // winsqlite3.dll loads and exports what is needed
    bool open(const std::string& utf8_path, Error* err);
    bool query(const std::string& sql, std::vector<DbRow>& out, Error* err);
    bool rows(const std::string& table, const std::vector<std::string>& columns, std::vector<DbRow>& out,
              Error* err) override;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Fallback: one tab-separated text per table, first line = column names, "\N" = NULL, escapes \t \n \\.
// (tools/artresolve/dump_tables.py writes them with Python's sqlite3 module.)
class TsvTables : public TableSource {
public:
    bool add(const std::string& table, const std::string& tsv_text, Error* err);
    bool load_dir(const std::string& utf8_dir, Error* err);   // every <table>.tsv in the folder
    bool rows(const std::string& table, const std::vector<std::string>& columns, std::vector<DbRow>& out,
              Error* err) override;
private:
    struct Table {
        std::vector<std::string> columns;
        std::vector<DbRow> rows;
    };
    std::map<std::string, Table> tables_;
};

// ---------------------------------------------------------------------------------------------- art tables
struct LandmarkRow {
    std::string improvement, resource, era, state, layout, model, tech;
    bool model_null = false;
    float scale = 1;
    std::string terrain_contour;
};
struct UnitMemberInfo {
    std::string type, model, domain, material;
    float scale = 0;
    bool model_null = false;
};
struct UnitInfoMember {
    std::string member;       // ArtDefine_UnitInfoMemberInfos.UnitMemberInfoType
    int count = 0;            // NumMembers
};
struct ArtTables {
    std::vector<LandmarkRow> landmarks;                                    // ArtDefine_Landmarks
    std::map<std::string, std::string> unit_art;                           // Units.Type -> UnitArtInfo
    std::map<std::string, std::vector<UnitInfoMember>> unit_info_members;  // ArtDefine_UnitInfoMemberInfos
    std::map<std::string, UnitMemberInfo> unit_members;                    // ArtDefine_UnitMemberInfos by Type
    std::vector<std::string> unit_member_order;                            // ArtDefine_UnitMemberInfos row order
    std::map<std::string, std::string> building_art;                       // Buildings.Type -> ArtDefineTag
    std::vector<WonderArt> wonders;                                        // every entry of every file, in order
    std::vector<std::string> wonder_files_missing;

    bool load_database(TableSource& db, Error* err);
    // Reads kWonderArtFiles through the provider (missing files are listed, not fatal; malformed -> Err::Xml).
    bool load_wonders(FileProvider& files, Error* err);
    // The entry in force for a Type (the last one loaded) and how many entries carry that Type.
    const WonderArt* wonder(const std::string& type, int* entries = nullptr) const;
};

// ---------------------------------------------------------------------------------------------- resolution
struct FileRef {
    std::string name;                     // as written in the referencing file
    bool found = false;
    std::string origin;                   // the winning copy
    std::vector<std::string> others;      // further copies (duplicates), in provider order
};
struct ResolvedAnimation {
    FileRef file;
    std::string ec_text;
    std::vector<int> ec;
    std::string name;
};
struct ResolvedTrigger {
    FileRef file;
    std::string state;
};
struct ResolvedArt {
    std::string kind;                     // "landmark", "wonder", "unit"
    std::string key;                      // the art key ("ART_DEF_IMPROVEMENT_ACADEMY|ART_DEF_RESOURCE_ALL|Ancient|Constructed")
    std::string source_row;               // what the chain went through, human-readable
    std::string model_path;               // the .fxsxml as the table writes it
    float scale = 0;
    FileRef fxsxml;
    FileRef mesh;
    std::vector<ResolvedAnimation> animations;
    std::vector<FileRef> textures;
    std::vector<FileRef> decision_trees;
    std::vector<ResolvedTrigger> timed_triggers;
    std::vector<FileRef> state_machines;  // looked up too (an extraction may not hold .fsmxml / .dge)
    std::vector<FileRef> anim_graphs;
    std::vector<std::string> bones;
    std::map<std::string, int> unknown_elements;
    std::vector<std::string> problems;    // human-readable: missing files and data quirks (not fatal)

    bool fxsxml_ok() const { return fxsxml.found; }
    size_t missing_animations() const;
    size_t missing_textures() const;
    // fxsxml, mesh, every animation and every texture found.
    bool complete() const;
};

// .fxsxml (path or basename) -> everything it names. Err::NotFound (with the name) if the .fxsxml is absent,
// Err::Xml if it is malformed; every other missing file is a problem in `out`, not an error.
bool resolve_fxsxml(FileProvider& files, const std::string& model_path, ResolvedArt& out, Error* err);

// Chain 1: ArtDefine_Landmarks rows matching improvement and resource (and era / state unless empty). One
// ResolvedArt per row with a model; Err::NotFound if no row matches; a row's missing .fxsxml is a problem in it.
bool resolve_landmark(const ArtTables& t, FileProvider& files, const std::string& improvement,
                      const std::string& resource, const std::string& era, const std::string& state,
                      std::vector<ResolvedArt>& out, Error* err);
// Chain 2: Buildings.Type -> ArtDefineTag -> WonderArtInfo -> .fxsxml. Err::NotFound for each missing link.
bool resolve_wonder(const ArtTables& t, FileProvider& files, const std::string& building, ResolvedArt& out,
                    Error* err);
// Chain 2 from the art key directly (used by --all over every WonderArtInfo Type).
bool resolve_wonder_type(const ArtTables& t, FileProvider& files, const std::string& type, ResolvedArt& out,
                         Error* err);
// Chain 3: Units.Type -> UnitArtInfo -> ArtDefine_UnitInfoMemberInfos -> ArtDefine_UnitMemberInfos.Model.
// One ResolvedArt per member info. Err::NotFound for a missing link before the members.
bool resolve_unit(const ArtTables& t, FileProvider& files, const std::string& unit, std::vector<ResolvedArt>& out,
                  Error* err);
// Chain 3 from one member info Type.
bool resolve_unit_member(const ArtTables& t, FileProvider& files, const std::string& member, ResolvedArt& out,
                         Error* err);

// Multi-line text form (the tool's output and the evidence files).
std::string format_resolved(const ResolvedArt& r);

}  // namespace c5m
