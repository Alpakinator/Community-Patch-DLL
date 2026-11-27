// The `.shader` section of CivilizationV_DX11.exe split into DXBC blobs, with their create sites (task 2902; per EXE
// build since ticket 4966: the array render_shader_create_sites of the EXE build table).
#include "c5r/shaders/shadersection.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vp_exe_symbols.h"   // exebuild/include + the generated vp_exe_tables.gen.h (render/CMakeLists.txt)

namespace c5r { namespace shaders {

namespace {
const CreateSite kSites[] = {
#include "create_sites_table.inc"
};
const int kCols = 8;   // the columns of create_sites_table.inc and of the array

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

std::string buildName(int build) {
    const VpExeBuildInfo* b = VpExeBuildInfoOf(build);
    return b ? std::string("build ") + b->steamBuild : std::string("an unknown build");
}
} // namespace

const CreateSite* createSiteTable(size_t& count) {
    count = sizeof(kSites) / sizeof(kSites[0]);
    return kSites;
}

CreateSiteArray createSiteArray(int build) {
    CreateSiteArray a;
    int rows = 0, cols = 0;
    a.anchor = VpExeArray(kVpExeBuild4390913, VPXA_render_shader_create_sites, &rows, &cols);
    a.exe = VpExeArray(build, VPXA_render_shader_create_sites, nullptr, nullptr);
    a.rows = rows;
    a.cols = cols;
    return a;
}

std::string ShaderBlob::model() const {
    return std::string(stageName(stage)) + "_" + std::to_string(major) + "_" + std::to_string(minor);
}

Status ShaderSection::open(const std::wstring& exePath) {
    std::vector<uint8_t> file;
    C5R_TRY(readFile(exePath, file));
    return parse(std::move(file));
}

Status ShaderSection::parse(std::vector<uint8_t> file) {
    section_.clear();
    blobs_.clear();
    gaps_.clear();
    byVa_.clear();
    sitesApplied_ = false;
    build_ = kVpExeBuildUnknown;
    PeImage pe;
    C5R_TRY(pe.parse(file.data(), file.size()));
    timeDateStamp_ = pe.timeDateStamp();
    sizeOfImage_ = pe.sizeOfImage();
    const PeSection* s = pe.find(".shader");
    if (!s) return Status::fail(Err::NotFound, "no .shader section");
    sectionVa_ = pe.imageBase() + s->rva;
    sectionOffset_ = s->rawOffset;
    const uint32_t n = s->fileBackedSize();
    section_.assign(file.begin() + s->rawOffset, file.begin() + s->rawOffset + n);
    file.clear();
    file.shrink_to_fit();

    const uint8_t* d = section_.data();
    uint32_t pos = 0;
    auto gapTo = [&](uint32_t end) {
        SectionGap g;
        g.offset = pos;
        g.size = end - pos;
        for (uint32_t i = pos; i < end; ++i) g.allZero = g.allZero && d[i] == 0;
        gaps_.push_back(g);
        pos = end;
    };
    while (pos < n) {
        if (n - pos >= 4 && std::memcmp(d + pos, "DXBC", 4) == 0) {
            DxbcContainer c;
            Status st = parseDxbc(d + pos, n - pos, c, true);
            if (!st.ok()) {
                st.msg = "blob at VA " + hex(sectionVa_ + pos) + ": " + st.msg;
                return st;
            }
            ShaderBlob b;
            b.va = b.exeVa = sectionVa_ + pos;   // va becomes the build-4390913 VA in applyCreateSites (known builds)
            b.fileOffset = sectionOffset_ + pos;
            b.size = c.totalSize;
            b.stage = c.stage;
            b.major = c.major;
            b.minor = c.minor;
            std::memcpy(b.checksum, c.checksum, 16);
            b.bytes = d + pos;
            blobs_.push_back(b);
            pos += c.totalSize;
            continue;
        }
        // gap: up to the next "DXBC" on a 4-byte boundary (relative to the section start), or the end
        uint32_t q = (pos + 4) & ~3u;
        while (q < n && !(n - q >= 4 && std::memcmp(d + q, "DXBC", 4) == 0)) q += 4;
        const uint32_t gapStart = pos;
        gapTo(q < n ? q : n);
        // Bug 0091 item 4: the EXE's gaps are alignment padding, at most 4 bytes and all zero (build 4390913: 712 gaps
        // of exactly 4 zero bytes). A longer or non-zero gap is a damaged container (e.g. a broken "DXBC" magic) that
        // would otherwise be skipped silently with every blob after it shifted out of the create-site table.
        const SectionGap& g = gaps_.back();
        if (g.size > 4 || !g.allZero)
            return Status::fail(Err::BadLayout, "gap of " + std::to_string(g.size) + " bytes" +
                                                    (g.allZero ? "" : ", not all zero,") + " at VA " +
                                                    hex(sectionVa_ + gapStart) +
                                                    ": not alignment padding (a damaged DXBC container?)");
    }

    // create sites, per EXE build (ticket 4966): the build from the stamp and SizeOfImage together
    build_ = VpExeBuildFromHeader(timeDateStamp_, sizeOfImage_);
    if (build_ != kVpExeBuildUnknown) C5R_TRY(applyCreateSites());
    byVa_.reserve(blobs_.size());
    for (uint32_t i = 0; i < blobs_.size(); ++i) byVa_.emplace_back(blobs_[i].va, i);
    std::sort(byVa_.begin(), byVa_.end());
    return Status::Ok();
}

// Every row of the build's array at the build's own blob VA; the parse fails on anything less (never applied in part).
Status ShaderSection::applyCreateSites() {
    const CreateSiteArray compiled = createSiteArray(build_);
    const CreateSiteArray& a = testArray_ ? *testArray_ : compiled;
    const std::string who = buildName(build_) + " (stamp " + hex(timeDateStamp_) + ", SizeOfImage " + hex(sizeOfImage_) +
                            ")";
    if (!a.anchor || !a.exe || a.cols != kCols || a.rows <= 0)
        return Status::fail(Err::NotFound, who + ": this binary has no create-site array for the build (EXE build table "
                                                 "render_shader_create_sites; tools/exebuild/gen_table.py, setting exe_tables)");
    const Stage want[] = {Stage::Vertex, Stage::Pixel, Stage::Hull, Stage::Domain};
    const int addrCols[] = {0, 4, 5, 6};   // blob VA, init function, call site, handle global
    size_t matched = 0;
    for (int i = 0; i < a.rows; ++i) {
        const uint32_t* an = a.anchor + i * kCols;
        const uint32_t* ex = a.exe + i * kCols;
        for (int c : addrCols)
            if (an[c] && !ex[c])
                return Status::fail(Err::NotFound, who + ": create-site row " + std::to_string(i) + " column " +
                                                       std::to_string(c) + " (build-4390913 value " + hex(an[c]) +
                                                       ") is not carried to this build: the EXE build table is incomplete");
        ShaderBlob* b = const_cast<ShaderBlob*>(findByExeVa(ex[0]));
        if (!b || b->size != an[1] || an[2] > 3 || b->stage != want[an[2]])
            return Status::fail(Err::BadLayout, who + ": create-site row " + std::to_string(i) + ": no " +
                                                    std::to_string(an[1]) + "-byte blob of its stage at the carried VA " +
                                                    hex(ex[0]) + " (build-4390913 VA " + hex(an[0]) + ")");
        b->va = an[0];
        b->set = static_cast<int>(an[3]);
        b->createFunction = an[4];
        b->createSite = an[5];
        b->handleGlobal = an[6];
        b->vertexFormatMask = an[7];
        b->exeCreateFunction = ex[4];
        b->exeCreateSite = ex[5];
        b->exeHandleGlobal = ex[6];
        ++matched;
    }
    if (matched != blobs_.size())
        return Status::fail(Err::BadLayout, who + ": " + std::to_string(blobs_.size()) + " blobs in the section, " +
                                                std::to_string(matched) + " create-site rows");
    sitesApplied_ = true;
    return Status::Ok();
}

const ShaderBlob* ShaderSection::findByVa(uint32_t va) const {
    auto it = std::lower_bound(byVa_.begin(), byVa_.end(), std::make_pair(va, 0u));
    return it != byVa_.end() && it->first == va ? &blobs_[it->second] : nullptr;
}

const ShaderBlob* ShaderSection::findByExeVa(uint32_t va) const {
    auto it = std::lower_bound(blobs_.begin(), blobs_.end(), va,
                               [](const ShaderBlob& b, uint32_t v) { return b.exeVa < v; });
    return it != blobs_.end() && it->exeVa == va ? &*it : nullptr;
}

}} // namespace c5r::shaders
