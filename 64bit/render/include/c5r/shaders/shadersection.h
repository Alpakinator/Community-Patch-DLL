// c5r/shaders/shadersection.h - the DXBC blobs of CivilizationV_DX11.exe's `.shader` section, read from the file at run
// time and split into containers (task 2902, epic 2900).
//
// Split: the section's file-backed bytes are walked from the start; a DXBC container is parsed (checksum verified)
// where "DXBC" stands and skipped by its total size; anything between containers is a gap, scanned for the next
// "DXBC" on a 4-byte boundary and reported in gaps() (offset, size, all zero or not). Every byte is accounted for:
// sum(blob sizes) + sum(gap sizes) == section bytes. In build 4390913: 1,417 blobs, 6,993,892 B, 712 gaps of 4 zero
// bytes (each blob starts 8-aligned), 0 bytes after the last blob within VirtualSize 0x6ac304.
//
// Create sites: a generated table (create_sites_table.inc, from evidence/2900/2902/create_sites.py over an llvm-objdump
// listing of the EXE of build 4390913) maps each blob VA to the EXE's init function, the call site, the 16-bit handle
// global and the set (shader level 7/8 -> 4_0, 9 -> 4_1, 10 -> 5_0).
//
// EXE builds (ticket 4966, epic 0590, D202). That table is also the array `render_shader_create_sites` of the EXE build
// table (exebuild/symbols/render.tsv): tools/exebuild carries every cell to each known build, and the build compiles the
// carried values in (vp_exe_tables.gen.h, local only). parse() takes the build from the EXE's PE stamp and SizeOfImage
// (exebuild/include/vp_exe_build.h):
//   - a known build: every row is applied at the build's own blob VA (size and stage must agree); each blob then has two
//     VAs: `va`, its VA in build 4390913 (the name render/'s tables and the catalogue use; findByVa takes it), and
//     `exeVa`, its VA in this EXE (findByExeVa). The create-site fields are kept in build-4390913 terms (what the
//     terrain-set tables compare with) and in this build's terms (exe*). A known build whose array is missing or has a
//     cell not carried, a row whose blob is not at its carried VA, or a blob no row names: the parse FAILS (loud), it is
//     never applied in part.
//   - an unknown build (any other EXE, and the synthetic EXEs of the tests): the section is split as before, va == exeVa,
//     no create sites (set -1, sites 0) and build() == kVpExeBuildUnknown. A caller that needs the EXE's blobs then
//     fails at its first findByVa (NotFound, naming the VA); the product never gets there on an unknown EXE, because the
//     64-bit path is not started for it (ticket 0588).
//
// Errors of open()/parse(): readFile Io; PE errors as PeImage::parse; no `.shader` section: NotFound; a container that
// fails parseDxbc: that error (with its VA); a container that runs past the section: Truncated; a gap longer than 4
// bytes or not all zero (not alignment padding, e.g. a container with a damaged "DXBC" magic): BadLayout (bug 0091
// item 4; before 2026-11-11 such a gap was reported in gaps() and the parse returned Ok).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "c5r/status.h"
#include "c5r/shaders/dxbc.h"
#include "c5r/shaders/pe.h"

namespace c5r { namespace shaders {

struct ShaderBlob {
    uint32_t va = 0;                // the blob's VA in build 4390913 (render/'s tables name blobs by it); = exeVa when
                                    // the build is unknown
    uint32_t exeVa = 0;             // its VA in the EXE parsed (ticket 4966)
    uint32_t fileOffset = 0, size = 0;
    Stage stage = Stage::Unknown;
    uint8_t major = 0, minor = 0;
    uint8_t checksum[16] = {};
    const uint8_t* bytes = nullptr; // into the section buffer of the owning ShaderSection
    int set = -1;                   // 40, 41, 50 (shader-model set the EXE selects by shader level), -1 if unknown
    uint32_t createSite = 0;        // address of the call to the create function, 0 if unknown
    uint32_t handleGlobal = 0;      // 16-bit global the returned handle is stored in, 0 if unknown
    uint32_t createFunction = 0;    // the init function holding the call (0x4e5c20 or 0x80cd10), 0 if unknown
    uint32_t vertexFormatMask = 0;  // VS only: third argument of CreateVertexShader 0x8bbc40
    // The same three in the EXE parsed (ticket 4966; build-4390913 values above): 0 when no create sites are applied
    uint32_t exeCreateSite = 0, exeHandleGlobal = 0, exeCreateFunction = 0;
    std::string model() const;      // "vs_5_0"
};

struct SectionGap {
    uint32_t offset = 0;            // within the section
    uint32_t size = 0;
    bool allZero = true;
};

// The create-site array of one build, as the EXE build table carries it: rows x cols cells, row-major, in the column
// order of create_sites_table.inc (blob VA, size, stage, set, init function, call site, handle global, vertex mask).
// anchor = the build-4390913 cells (the committed table), exe = the build's own cells (0 = not carried). Ticket 4966.
struct CreateSiteArray {
    const uint32_t* anchor = nullptr;
    const uint32_t* exe = nullptr;
    int rows = 0, cols = 0;
};
// The array compiled in for a build (vp_exe_tables.gen.h); exe == nullptr for a build the binary has no table for.
CreateSiteArray createSiteArray(int build);

class ShaderSection {
public:
    // Reads the EXE (read-only) and splits its `.shader` section.
    Status open(const std::wstring& exePath);
    // The same from a whole PE file already in memory (tests build synthetic ones).
    Status parse(std::vector<uint8_t> fileBytes);

    const std::vector<ShaderBlob>& blobs() const { return blobs_; }
    const std::vector<SectionGap>& gaps() const { return gaps_; }
    // A blob by its build-4390913 VA (the VAs in render/'s tables), on any known build; on an unknown build by its VA.
    const ShaderBlob* findByVa(uint32_t va) const;
    // A blob by its VA in the EXE parsed.
    const ShaderBlob* findByExeVa(uint32_t va) const;
    uint32_t sectionVa() const { return sectionVa_; }   // in the EXE parsed
    uint32_t sectionFileOffset() const { return sectionOffset_; }
    uint32_t sectionSize() const { return static_cast<uint32_t>(section_.size()); }
    uint32_t timeDateStamp() const { return timeDateStamp_; }
    uint32_t sizeOfImage() const { return sizeOfImage_; }
    int build() const { return build_; }   // kVpExeBuild4390913, kVpExeBuild26577625, ... or kVpExeBuildUnknown (-1)
    bool createSitesApplied() const { return sitesApplied_; } // true when every row matched every blob (known build)

    // Tests only: parse() uses this array instead of the compiled one (null: the compiled one again).
    void setCreateSiteArrayForTest(const CreateSiteArray* a) { testArray_ = a; }

private:
    Status applyCreateSites();
    std::vector<uint8_t> section_;
    std::vector<ShaderBlob> blobs_;
    std::vector<SectionGap> gaps_;
    std::vector<std::pair<uint32_t, uint32_t>> byVa_;   // (va, index in blobs_), sorted by va
    uint32_t sectionVa_ = 0, sectionOffset_ = 0, timeDateStamp_ = 0, sizeOfImage_ = 0;
    int build_ = -1;
    bool sitesApplied_ = false;
    const CreateSiteArray* testArray_ = nullptr;
};

// The committed create-site table (build 4390913; create_sites_table.inc), the anchor list of the array above.
struct CreateSite {
    uint32_t blobVa, size, stage /* 0 VS, 1 PS, 2 HS, 3 DS */;
    int set;
    uint32_t function, callSite, handleGlobal, vertexFormatMask;
};
const CreateSite* createSiteTable(size_t& count);
// (kCreateSiteTimeDateStamp, the stamp 0x546CD0A8 the table was applied on before ticket 4966, is gone: the build comes
// from exebuild/include/vp_exe_build.h, stamp and SizeOfImage together.)

}} // namespace c5r::shaders
