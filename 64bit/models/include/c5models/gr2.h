// c5models/gr2.h - reader for Civilization V's Granny 2 files (.gr2, .gsd) without the Granny runtime.
// Epic 3100 (team-3100), 2026-11-10. Own code, GPL-3.0-or-later like the rest of the repository; the Oodle1
// decoder in src/oodle1.cpp is a port of a Boost-licensed file and keeps its notice there.
//
// Layers, each usable on its own:
//   1. parse_header()       header and section table (little- and big-endian files)
//   2. oodle1_decompress()  Granny compression type 2
//   3. RawFile              decompressed sections, pointer fix-ups looked up (not written), the type tree,
//                           names through a StringDatabase; generic typed access through Object / Array
//   4. read_gr2()           the plain model structures below (meshes, skeletons, models, materials, animations)
//   5. check_geometry()     index ranges, declared sizes, bone weights, finite bounds
// The library never opens files: every entry point takes the file's bytes, so that the caller decides where
// bytes come from (loose files, the FPK reader of epic 2900, a test buffer).
//
// Language level C++14 (decision D15). Errors: the high-level functions return false and fill an Error; the
// low-level Object/Array accessors throw ParseError (caught by read_gr2()).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace c5m {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------------------------- errors
enum class Err {
    None = 0,
    Truncated,              // the file ends before a structure it declares
    BadMagic,               // not a Granny 2 file
    BadHeader,              // header fields out of range (size, version, section count, total size)
    BadSectionTable,        // a section's stored data, fix-up or marshalling table lies outside the file
    UnsupportedCompression, // compression other than 0 (none) and 2 (Oodle1)
    DecompressCorrupt,      // the Oodle1 stream is inconsistent (bad parameters, back-reference before start, overrun)
    DecompressSize,         // the decoder did not end exactly at the declared expanded size
    FixupOutOfRange,        // a fix-up's source or target lies outside its section
    TypeTreeCorrupt,        // unknown member type, unterminated or cyclic type definition
    ObjectOutOfRange,       // an object, array or string read outside its section
    BadStringDatabase,      // a .gsd that does not have the expected root
    MissingMember,          // a member the model reader needs is absent from the file's type
    GeometryCheck,          // check_geometry() found a violation
    Xml,                    // malformed .fxsxml / art-define XML
    NotFound,               // a referenced file or row does not exist
    BadCrc,                 // header CRC (zlib CRC-32 of bytes 0x68 .. total size) does not match the file
    OutOfMemory,            // an allocation failed (sizes within the limits below, but more than the process has)
    ReadBudget,             // the file makes the reader build more than kMaxBuiltBytes or do more than kMaxReadSteps
};
const char* err_name(Err e);

struct Error {
    Err code = Err::None;
    std::string what;
};

class ParseError : public std::runtime_error {
public:
    ParseError(Err c, const std::string& what) : std::runtime_error(what), code(c) {}
    Err code;
};

// ---------------------------------------------------------------------------------------------- layer 1
struct SectionHeader {
    uint32_t compression = 0, data_offset = 0, data_size = 0, expanded_size = 0, alignment = 0;
    uint32_t stop0 = 0, stop1 = 0;                 // ends of Oodle1 sub-streams 0 and 1
    uint32_t fixup_offset = 0, fixup_count = 0;    // 12-byte entries: u32 src offset, u32 target section, u32 target offset
    uint32_t marshal_offset = 0, marshal_count = 0;// 16-byte entries: u32 count, u32 offset, u32 type section, u32 type offset
};

struct Addr {
    uint32_t sec = 0, off = 0;
    bool operator==(const Addr& o) const { return sec == o.sec && off == o.off; }
};

struct Header {
    bool big_endian = false;
    uint32_t header_size = 0, header_format = 0;
    uint32_t version = 0, total_size = 0, crc = 0, section_table_offset = 0, section_count = 0;
    Addr root_type, root_object;
    uint32_t type_tag = 0;
    uint32_t extra_tags[4] = {0, 0, 0, 0};
    uint32_t string_db_crc = 0;                    // header 0x58: 0 = strings inline, else the .gsd's DatabaseCRC
    std::vector<SectionHeader> sections;
};

// Limits on what a file may declare (issue 9025, bug 0093; research/05-graphics-composition/
// model-reader-verification.md section 11 gives the game's own maxima they are measured against). A file over a
// limit is refused with the named error before anything is allocated or recursed.
// - Oodle1 section: expanded size <= kMaxExpandRatio x stored size (BadSectionTable). Game maximum 39.5.
// - Sum of all sections' expanded sizes <= kMaxExpandedTotal (BadSectionTable). Game maximum 7,919,104 B.
// - Nesting of type definitions that reference one another (RawFile::typedef_at): kMaxTypeNesting
//   (TypeTreeCorrupt). Game maximum 8.
//
// Per-file read budget (issue 4000, bug 0093 V10/V11; section 13 of the same research file). A small file can name the
// same bytes many times (thousands of references to one mesh, overlapping type definitions, one array walked through
// thousands of types, one long string behind every slot of a string database), so the memory and work of a read are
// not bounded by the size of the file or of its sections. Every read of one file (read_gr2, StringDatabase::add_gsd)
// therefore counts, and refuses with ReadBudget past either limit:
// - built bytes: what the reader builds beyond the section buffers (type records, copied vertices, indices and
//   curves, every string it returns, the model structures, the walk's bookkeeping): kMaxBuiltBytes.
// - read steps: objects and elements walked, array elements read, member look-ups (each costs the members it scans),
//   type members read: kMaxReadSteps.
// Game maxima over all 12,761 files (2026-11-13): 8,296,788 built bytes and 729,912 steps (both
// LeaderModels.fpk/04_light_test.gr2); string databases 241,932 B and 37,978 steps. Margins 32x and 23x.
const uint32_t kMaxExpandRatio = 256;
const uint64_t kMaxExpandedTotal = 256ull << 20;
const uint32_t kMaxTypeNesting = 64;
const uint64_t kMaxBuiltBytes = 256ull << 20;
const uint64_t kMaxReadSteps = 16ull << 20;

bool parse_header(const uint8_t* data, size_t size, Header& out, Error* err);

// ---------------------------------------------------------------------------------------------- layer 2
// Decodes one Oodle1 section. src/src_size: the stored section; stop0/stop1/dst_size from the section header.
// big_endian: the file is big-endian; its three 12-byte parameter blocks then hold big-endian words (established
// 2026-11-11 on the one big-endian file of the install, MiscData.fpk/cityruins_improved.gr2).
bool oodle1_decompress(const uint8_t* src, size_t src_size, uint32_t stop0, uint32_t stop1, uint32_t dst_size,
                       Bytes& out, Error* err, bool big_endian = false);

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0);   // zlib's CRC-32

// ---------------------------------------------------------------------------------------------- strings
// Union of one or more Granny string databases (*_stringdatabase.gsd). A string slot in a file whose header
// field 0x58 is non-zero holds hash(s) = crc32(s) ^ 0xFFFFFFFF (research/05-graphics-composition/
// landmarks-and-models.md section 4.6).
class StringDatabase {
public:
    bool add_gsd(const uint8_t* data, size_t size, Error* err);
    const std::string* find(uint32_t h) const;
    size_t size() const { return map_.size(); }
    uint32_t database_crc() const { return crc_; }
    static uint32_t hash(const std::string& s);
private:
    std::unordered_map<uint32_t, std::string> map_;
    uint32_t crc_ = 0;
};

// ---------------------------------------------------------------------------------------------- layer 3
enum class MemberType : uint32_t {
    None = 0, Inline = 1, Reference = 2, ReferenceToArray = 3, ArrayOfReferences = 4, VariantReference = 5,
    ReferenceToVariantArray = 7, String = 8, Transform = 9, Real32 = 10, Int8 = 11, UInt8 = 12, BinormalInt8 = 13,
    NormalUInt8 = 14, Int16 = 15, UInt16 = 16, BinormalInt16 = 17, NormalUInt16 = 18, Int32 = 19, UInt32 = 20,
    Real16 = 21, EmptyReference = 22,
};
const char* member_type_name(MemberType t);
uint32_t member_type_size(MemberType t);   // 0 for Inline (size of the referenced type) and None

struct TypeDef;
struct Member {
    MemberType type = MemberType::None;
    std::string name;
    const TypeDef* ref = nullptr;   // Inline, Reference, ReferenceToArray, ArrayOfReferences
    uint32_t array_size = 0;        // 0 = scalar
    uint32_t offset = 0;            // byte offset in the owning struct
    uint32_t size = 0;              // element size x max(array_size, 1)
};
struct TypeDef {
    Addr addr;
    std::vector<Member> members;
    uint32_t size = 0;
    bool complete = false;      // false while its members are being read (cycle detection)
    bool has_pointers = true;   // false: no String/reference member at any depth (walk_all skips its arrays)
    const Member* find(const char* name) const;
};

struct Transform {
    uint32_t flags = 0;
    float translation[3] = {0, 0, 0};
    float rotation[4] = {0, 0, 0, 1};   // quaternion x, y, z, w
    float scale_shear[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

class RawFile;
class Array;

// A typed view of one object in a file. Accessors throw ParseError; a default Object is null.
class Object {
public:
    Object() = default;
    Object(const RawFile* f, const TypeDef* t, Addr a) : f_(f), t_(t), a_(a) {}
    bool null() const { return f_ == nullptr || t_ == nullptr; }
    explicit operator bool() const { return !null(); }
    const TypeDef* type() const { return t_; }
    Addr addr() const { return a_; }
    bool has(const char* name) const;                 // its look-up counts against the read budget

    // Scalars (element idx of a fixed array member). Integer types are returned unnormalised.
    double number(const char* name, uint32_t idx = 0) const;
    uint32_t u32(const char* name, uint32_t idx = 0) const { return (uint32_t)number(name, idx); }
    int32_t i32(const char* name, uint32_t idx = 0) const { return (int32_t)number(name, idx); }
    float f32(const char* name, uint32_t idx = 0) const { return (float)number(name, idx); }
    std::string str(const char* name) const;          // "" for a NULL string
    bool str_null(const char* name) const;
    Transform transform(const char* name) const;
    Object ref(const char* name) const;               // Reference (null if NULL)
    Object variant(const char* name) const;           // VariantReference (null if NULL)
    Object inline_member(const char* name) const;     // Inline struct member
    Array array(const char* name) const;              // ReferenceToArray, ArrayOfReferences, ReferenceToVariantArray

    const RawFile* file() const { return f_; }
private:
    const Member& need(const char* name) const;
    const RawFile* f_ = nullptr;
    const TypeDef* t_ = nullptr;
    Addr a_;
};

class Array {
public:
    Array() = default;
    uint32_t size() const { return count_; }
    Object at(uint32_t i) const;                      // null for a NULL entry of an ArrayOfReferences
    const TypeDef* type() const { return t_; }
    bool of_references() const { return refs_; }
    Addr base() const { return base_; }
    uint32_t stride() const { return t_ ? t_->size : 0; }
private:
    friend class Object;
    const RawFile* f_ = nullptr;
    const TypeDef* t_ = nullptr;
    Addr base_;
    uint32_t count_ = 0;
    bool refs_ = false;
};

struct LoadStats {
    uint32_t fixups = 0, marshallings = 0;
    uint32_t decoder_overrun_max = 0;   // bytes a block wrote past its sub-stream end (0 in a clean file)
    uint64_t built_bytes = 0, read_steps = 0;   // read budget used (kMaxBuiltBytes, kMaxReadSteps)
};

class RawFile {
public:
    // Decompresses every section and indexes the fix-ups. strings may be null (files with inline strings).
    bool load(const uint8_t* data, size_t size, const StringDatabase* strings, Error* err);
    const Header& header() const { return h_; }
    const Bytes& section(uint32_t i) const { return sec_[i]; }
    uint32_t section_count() const { return (uint32_t)sec_.size(); }
    const LoadStats& stats() const { return stats_; }
    Object root() const;

    // Low-level, all bounds-checked, all byte-order aware (big-endian files are swapped on read).
    bool fixup(Addr slot, Addr& target) const;         // false: no fix-up (a NULL pointer)
    uint32_t u32(Addr a) const;
    uint16_t u16(Addr a) const;
    uint8_t u8(Addr a) const;
    float f32(Addr a) const;
    void need(Addr a, uint32_t n) const;               // throws ObjectOutOfRange
    std::string cstr(Addr a) const;
    std::string string_slot(Addr slot) const;          // fix-up -> C string, else database hash
    bool string_slot_null(Addr slot) const;
    const TypeDef* typedef_at(Addr a) const;           // cached
    // The per-file read budget (kMaxBuiltBytes, kMaxReadSteps): each call adds to it and throws ParseError
    // (Err::ReadBudget) once a total is over its limit. Callers charge before they build or loop.
    void charge_bytes(uint64_t n) const;
    void charge_steps(uint64_t n) const;
    // Distinct string slots that neither a fix-up nor the database resolved (files with header 0x58 != 0).
    size_t unresolved_strings() const { return unresolved_.size(); }
    // Visits every object reachable from the root (all members, arrays, variants, type names) so that
    // unresolved_strings() covers the whole file, as the Python reference counts it.
    void walk_all() const;
private:
    bool load_sections(const uint8_t* data, Error* err);
    void walk(const TypeDef* t, Addr a, int depth) const;
    void walk_string_slot(Addr slot) const;   // string_slot() without building the string (walk_all)
    bool visit_once(uint64_t k) const;        // true the first time walk() sees key k
    mutable std::unordered_map<uint64_t, char> unresolved_;
    // walk()'s visited (address, type) keys: an open-addressing set, 0 = empty slot, at most half full (issue 4000:
    // an unordered_map node per visit was most of the time of a long walk)
    mutable std::vector<uint64_t> visited_;
    mutable size_t visited_n_ = 0;
    Header h_;
    std::vector<Bytes> sec_;
    std::vector<std::vector<std::pair<uint32_t, Addr>>> fix_;   // per section, sorted by source offset
    const StringDatabase* strings_ = nullptr;
    mutable LoadStats stats_;   // mutable: the const accessors count the read budget in it
    mutable std::unordered_map<uint64_t, std::unique_ptr<TypeDef>> types_;
    mutable uint32_t type_depth_ = 0;   // current recursion depth of typedef_at (limit kMaxTypeNesting)
};

// ---------------------------------------------------------------------------------------------- layer 4
struct VertexComponent {
    MemberType type = MemberType::None;
    std::string name;
    uint32_t count = 0;    // components (3 for a Real32[3] position)
    uint32_t offset = 0;   // in the vertex
};
struct VertexFormat {
    std::vector<VertexComponent> components;
    uint32_t stride = 0;
    const VertexComponent* find(const char* name) const;
    std::string key() const;   // "Real32:Position:3,NormalUInt8:BoneWeights:4,..." (the sweep's grouping key)
};

struct BoneBinding {
    std::string bone_name;
    float obb_min[3] = {0, 0, 0}, obb_max[3] = {0, 0, 0};
};
struct TriGroup {
    uint32_t material = 0;        // index into Mesh::materials
    uint32_t tri_first = 0, tri_count = 0;
};
struct Mesh {
    std::string name;
    VertexFormat format;
    uint32_t vertex_count = 0;
    Bytes vertices;                    // vertex_count * format.stride bytes, little-endian
    uint32_t index_size = 0;           // 2 (Indices16) or 4 (Indices); 0 when the mesh has no indices
    std::vector<uint32_t> indices;
    std::vector<TriGroup> groups;
    std::vector<int32_t> materials;    // MaterialBindings -> index into FileInfo::materials (-1 if not found)
    std::vector<std::string> material_names;   // MaterialBindings[].Material.Name ("" for NULL)
    std::vector<BoneBinding> bone_bindings;
    uint32_t morph_targets = 0;
    // Reads component `name` of vertex v as up to 4 floats (NormalUInt8 / NormalUInt16 normalised to 0..1,
    // BinormalInt8/16 to -1..1, other integers as their value). Returns the component count, 0 if absent.
    uint32_t read(uint32_t v, const char* name, float out[4]) const;
};

struct Bone {
    std::string name;
    int32_t parent = -1;
    Transform local;
    float inverse_world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};   // row-major as stored (row vectors)
    float lod_error = 0;
    // 4410 (epic 0374, additive): the bone's track-mask weights, ExtendedData.GrannyTrackMasks as stored: one
    // (member name, weight) per scalar member of that struct (a Reference in the game's files; an inline struct is read
    // the same way), e.g. ("TrackMaskAIM_LAYER", 1). A .dge Trackmask
    // binding names one of these members (research/05-graphics-composition/unit-controller.md 13.3). Empty when the
    // bone has no ExtendedData or no GrannyTrackMasks struct.
    std::vector<std::pair<std::string, float>> track_masks;
    // The weight of member `name`; false when the bone has no such member.
    bool track_mask(const std::string& name, float& weight) const;
};
struct Skeleton {
    std::string name;
    std::vector<Bone> bones;
    int32_t lod_type = 0;
    int32_t find(const std::string& bone) const;
};

struct ModelEntry {
    std::string name;
    int32_t skeleton = -1;             // index into FileInfo::skeletons
    Transform initial_placement;
    std::vector<int32_t> meshes;       // MeshBindings -> index into FileInfo::meshes
    std::string skeleton_name;
    std::vector<std::string> mesh_names;
};

struct TextureMap {
    std::string usage;                 // "Diffuse", "BuildingSREF", "BaseTextureMap", ...
    std::string file;                  // the texture's FromFileName as stored (artist path)
    std::string basename;              // lower-case file name, the key the game's archives use
};
struct Material {
    std::string name;
    std::string shader_set;            // ExtendedData.ShaderSet ("" if none)
    std::vector<TextureMap> maps;
    std::string texture_file;          // the material's own Texture, if any
    std::vector<std::pair<std::string, std::string>> extended;   // scalar ExtendedData members, as text
};

// One curve, kept as stored: the header (Format, Degree), the format's scalar members and its arrays.
struct CurveArray {
    std::string name;                  // "KnotsControls", "Knots", "Controls", ...
    MemberType type = MemberType::None;
    uint32_t count = 0;                // elements
    Bytes data;                        // count * member_type_size(type) bytes, little-endian
};
struct Curve {
    bool present = false;
    std::string format_name;           // "D3K16uC16u" (from the CurveDataHeader_<name> member type name)
    uint32_t format = 0, degree = 0;
    std::vector<std::pair<std::string, std::vector<float>>> scalars;   // e.g. ControlScales [3]
    std::vector<CurveArray> arrays;
};
struct TransformTrack {
    std::string name;
    uint32_t flags = 0;
    Curve position, orientation, scale_shear;
};
struct TrackGroup {
    std::string name;
    std::vector<TransformTrack> transform_tracks;
    uint32_t vector_tracks = 0, text_tracks = 0;
    Transform initial_placement;
    uint32_t accumulation_flags = 0;
};
struct Animation {
    std::string name;
    float duration = 0, time_step = 0, oversampling = 0;
    std::vector<int32_t> track_groups;   // index into FileInfo::track_groups
    std::vector<std::string> track_group_names;
};

struct FileInfo {
    Header header;
    LoadStats stats;
    std::vector<uint32_t> section_crc;   // crc32 of each decompressed section
    std::vector<uint32_t> section_size;
    std::string art_tool, exporter, from_file;
    float units_per_meter = 0;
    float up[3] = {0, 0, 0}, right[3] = {0, 0, 0}, back[3] = {0, 0, 0}, origin[3] = {0, 0, 0};
    std::vector<std::string> textures;   // root Textures, FromFileName
    std::vector<Material> materials;
    std::vector<Skeleton> skeletons;
    std::vector<Mesh> meshes;
    std::vector<ModelEntry> models;
    std::vector<TrackGroup> track_groups;
    // Track groups reached only through an animation's TrackGroups references, not the root list (VP's FBX clips,
    // e.g. flagbearer_idle.gr2, have an empty root list): appended after the root ones, this many at the end; the
    // canonical summary lists the root ones only (epic 0041, issue 4353).
    uint32_t animation_only_track_groups = 0;
    std::vector<Animation> animations;
    uint32_t unresolved_strings = 0;
};

bool read_gr2(const uint8_t* data, size_t size, const StringDatabase* strings, FileInfo& out, Error* err);

// ---------------------------------------------------------------------------------------------- layer 5
struct GeometryReport {
    uint32_t meshes = 0, vertices = 0, triangles = 0;
    std::vector<std::string> violations;   // empty = every check passed
};
// Checks per mesh: vertex bytes == count x stride; index count a multiple of 3; every index < vertex count;
// groups inside the index range; every position (and normal) finite; bone weights in a skinned vertex sum to
// 1 (+-2/255) and bone indices < bone binding count; bone bindings name bones of the mesh's model skeleton.
// When the vertex bytes do not match count x stride, or BoneWeights / BoneIndices lie outside the vertex, that is
// reported and the per-vertex checks that would read those bytes are not run (issue 9025).
bool check_geometry(const FileInfo& f, GeometryReport& rep, Error* err);

// ---------------------------------------------------------------------------------------------- summary
// The canonical one-line JSON summary that the C++ sweep and the Python reference (models/tools/pyref) both
// write; format in models/README.md section "Canonical summary".
std::string canonical_summary(const FileInfo& f, const std::string& label);

}  // namespace c5m
