// library_report.cpp - ticket 4872's report of the model library over an install (not a test): the counts, the files
// that failed, and for captured draws given on the command line the key's hit and the byte compare of the repack.
// Inputs read at run time: C5R_GAME_DIR (the install), C5R_LANDMARK_DB (the database copy). Prints counts, names and
// hashes only; nothing of the game is written (D83).
//
//     c5r_landmark_report [--files] [--residency] [draw ...]
//     draw = <ib file>,<2|4>,<vb file>,<stride>,<firstIndex>,<indexCount>,<baseVertex>   (a capture export's buffers)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/cities/catalogue.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/library.h"

using namespace c5r;

namespace {

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(gamefs::fromUtf8(path).c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, c)) v.push_back(x);
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    const char* game = std::getenv("C5R_GAME_DIR");
    const char* dbp = std::getenv("C5R_LANDMARK_DB");
    if (!game || !*game || !dbp || !*dbp) {
        std::fprintf(stderr, "C5R_GAME_DIR and C5R_LANDMARK_DB must be set\n");
        return 2;
    }
    bool listFiles = false, residency = false;
    std::vector<std::string> draws;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--files")) listFiles = true;
        else if (!std::strcmp(argv[i], "--residency")) residency = true;
        else draws.push_back(argv[i]);
    }
    gamefs::GameFiles files;
    Status s = files.open(gamefs::fromUtf8(game));
    if (!s.ok()) {
        std::fprintf(stderr, "open: %s\n", s.str().c_str());
        return 1;
    }
    c5m::SqliteTables db;
    c5m::Error e;
    if (!db.open(dbp, &e)) {
        std::fprintf(stderr, "database: %s\n", e.what.c_str());
        return 1;
    }
    cities::ArtCatalogue art;
    s = art.load(files, db);
    if (!s.ok()) {
        std::fprintf(stderr, "catalogue: %s\n", s.str().c_str());
        return 1;
    }
    const cities::CatalogueCounts& cc = art.counts();
    std::printf("catalogue: %u city-set files, %u sets, %u set models; wonder art %u types (%u entries); world "
                "wonders %u; display buildings %u (%u world wonders, %u without art)\n",
                cc.citySetFiles, cc.citySets, cc.citySetModels, cc.wonderArtTypes, cc.wonderArtEntries,
                cc.worldWonders, cc.displayBuildings, cc.displayWorldWonders, cc.displayWithoutArt);
    landmarks::ModelLibrary lib;
    s = lib.load(files, db, art);
    if (!s.ok()) {
        std::fprintf(stderr, "library: %s\n", s.str().c_str());
        return 1;
    }
    const landmarks::LibraryCounts& c = lib.counts();
    std::printf("library: landmark rows %u (%u with a model, %u models); wonder types %u (%u models, %u drawn); "
                "walls %u + great wall %u; city sets %u entries, %u files\n",
                c.landmarkRows, c.landmarkRowsWithModel, c.landmarkModels, c.wonderTypes, c.wonderModels,
                c.wonderTypesDrawn, c.wallFiles, c.greatWallFiles, c.citySetEntries, c.citySetFiles);
    std::printf("files %u (ok %u, failed %u), parts %u, keys %u, key collisions %u (textures differ %u), catalogue "
                "%.1f KiB, load %.2f s\n",
                c.files, c.filesOk, c.filesFailed, c.parts, c.keys, c.keyCollisions, c.keyCollisionsTextured,
                c.catalogueBytes / 1024.0, c.loadSeconds);
    uint32_t forms[3] = {0, 0, 0}, noFile = 0;
    for (const landmarks::ModelFile& f : lib.files())
        if (f.ok) ++forms[static_cast<int>(f.layout.form)];
    for (const landmarks::Model& m : lib.models())
        if (m.file < 0) {
            ++noFile;
            std::printf("model without a file: %s %s\n", landmarks::sourceName(m.source), m.problem.c_str());
        }
    std::printf("forms: Float32 %u, Half20 %u, Skinned28 %u; models %zu, without a file %u\n", forms[0], forms[1],
                forms[2], lib.models().size(), noFile);
    for (const landmarks::ModelFile& f : lib.files())
        if (!f.ok) std::printf("failed: %s (scale %g, %s): %s\n", f.gr2.c_str(), f.scale, landmarks::className(f.cls),
                               f.problem.c_str());
    if (listFiles)
        for (const landmarks::ModelFile& f : lib.files())
            if (f.ok)
                std::printf("file %s scale %g %s %s v %u i %u ib%u parts %zu unpacked %zu\n", f.gr2.c_str(), f.scale,
                            landmarks::className(f.cls), landmarks::formName(f.layout.form), f.layout.vertexCount,
                            f.layout.indexCount, f.layout.indexBytes * 8, f.layout.parts.size(),
                            f.layout.unpacked.size());
    if (residency) {
        const auto t0 = std::chrono::steady_clock::now();
        uint32_t bad = 0;
        for (uint32_t i = 0; i < lib.files().size(); ++i)
            if (lib.files()[i].ok && !lib.resident(i, nullptr).ok()) ++bad;
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("residency: %u files resident, %.1f MiB, %.2f s, %u refused\n", lib.residentFiles(),
                    lib.residentBytes() / 1048576.0, sec, bad);
    }
    int failures = 0;
    for (const std::string& d : draws) {
        const std::vector<std::string> f = split(d, ',');
        if (f.size() != 7) {
            std::fprintf(stderr, "bad draw spec %s\n", d.c_str());
            return 2;
        }
        std::vector<uint8_t> ib, vb;
        if (!readAll(f[0], ib) || !readAll(f[2], vb)) {
            std::fprintf(stderr, "cannot read %s or %s\n", f[0].c_str(), f[2].c_str());
            return 1;
        }
        const uint32_t ibytes = static_cast<uint32_t>(std::atoi(f[1].c_str()));
        const uint32_t stride = static_cast<uint32_t>(std::atoi(f[3].c_str()));
        const uint32_t first = static_cast<uint32_t>(std::atoi(f[4].c_str()));
        const uint32_t count = static_cast<uint32_t>(std::atoi(f[5].c_str()));
        const int32_t base = std::atoi(f[6].c_str());
        landmarks::MeshKey k;
        s = landmarks::meshKey(ib.data(), ib.size() / ibytes, ibytes, landmarks::kLeadIndices, vb.data(), vb.size(),
                               first,
                               count, base, k);
        if (!s.ok()) {
            std::printf("draw %s: key %s\n", d.c_str(), s.str().c_str());
            ++failures;
            continue;
        }
        const landmarks::ModelLibrary::Hit* h = lib.find(k);
        std::printf("draw first %u count %u base %d: key %016llx/%016llx -> %s\n", first, count, base,
                    static_cast<unsigned long long>(k.indexHash), static_cast<unsigned long long>(k.vertexHash),
                    h ? "hit" : "MISS");
        landmarks::ModelLibrary::Hit byIndex;
        if (!h)   // the index buffer alone: which file, and how its vertices differ
            for (uint32_t fi = 0; fi < lib.files().size() && !h; ++fi)
                for (uint32_t pi = 0; pi < lib.files()[fi].layout.parts.size(); ++pi) {
                    const landmarks::MeshKey& o = lib.files()[fi].layout.parts[pi].key;
                    const landmarks::FileLayout& L = lib.files()[fi].layout;
                    if (o.firstIndex == first && o.indexCount == count && o.baseVertex == base &&
                        static_cast<size_t>(L.indexCount) * L.indexBytes == ib.size()) {
                        byIndex.file = fi;
                        byIndex.part = pi;
                        h = &byIndex;
                        std::printf("    a file of the same index buffer size and range: index hash %s, vertex hash %s\n",
                                    o.indexHash == k.indexHash ? "equal" : "differs",
                                    o.vertexHash == k.vertexHash ? "equal" : "differs");
                        break;
                    }
                }
        if (!h) {
            ++failures;
            continue;
        }
        const landmarks::ModelFile& mf = lib.files()[h->file];
        const landmarks::Part& p = mf.layout.parts[h->part];
        std::printf("    %s scale %g %s mesh %s group %u material %s t0 %s t1 %s\n", mf.gr2.c_str(), mf.scale,
                    landmarks::formName(mf.layout.form), p.mesh.c_str(), p.group, p.material.c_str(), p.t0.c_str(),
                    p.t1.c_str());
        for (const landmarks::ModelLibrary::Hit& o : lib.candidates(k))
            if (o.file != h->file)
                std::printf("    also %s scale %g t0 %s\n", lib.files()[o.file].gr2.c_str(), lib.files()[o.file].scale,
                            lib.files()[o.file].layout.parts[o.part].t0.c_str());
        const landmarks::ModelLibrary::Buffers* b = nullptr;
        s = lib.resident(h->file, &b);
        if (!s.ok()) {
            std::printf("    resident: %s\n", s.str().c_str());
            ++failures;
            continue;
        }
        // Index bytes over the range, vertex bytes over the mesh's vertices.
        const bool ibEq = b->indices.size() >= (static_cast<size_t>(first) + count) * ibytes &&
                          std::memcmp(b->indices.data() + static_cast<size_t>(first) * ibytes,
                                      ib.data() + static_cast<size_t>(first) * ibytes,
                                      static_cast<size_t>(count) * ibytes) == 0;
        const std::vector<uint8_t>& ov = p.skinned ? b->skinnedVertices : b->vertices;
        const uint32_t ms = p.skinned ? 28u : landmarks::vertexStride(mf.layout.form);
        const size_t vOff = static_cast<size_t>(base) * stride, vLen = static_cast<size_t>(p.vertexCount) * stride;
        size_t diff = 0, firstDiff = static_cast<size_t>(-1);
        if (ms != stride || vOff + vLen > vb.size() || vOff + vLen > ov.size()) {
            std::printf("    vertex stride %u vs captured %u, or range past a buffer\n", ms, stride);
            ++failures;
            continue;
        }
        for (size_t i = 0; i < vLen; ++i)
            if (ov[vOff + i] != vb[vOff + i]) {
                ++diff;
                if (firstDiff == static_cast<size_t>(-1)) firstDiff = i;
            }
        const bool wholeIb = b->indices.size() == ib.size();
        const bool wholeVb = ov.size() == vb.size();
        std::printf("    index bytes %s; vertex bytes %zu of %zu differ%s; whole buffers: ib %s (%zu vs %zu), vb %s "
                    "(%zu vs %zu)\n",
                    ibEq ? "equal" : "DIFFER", diff, vLen,
                    diff ? (" (first at byte " + std::to_string(firstDiff) + ")").c_str() : "",
                    wholeIb ? "same size" : "size differs", b->indices.size(), ib.size(),
                    wholeVb ? "same size" : "size differs", ov.size(), vb.size());
        if (diff) {
            const size_t v = firstDiff / stride;
            std::printf("    vertex %zu ours:", v);
            for (uint32_t i = 0; i < stride; ++i) std::printf(" %02x", ov[vOff + v * stride + i]);
            std::printf("\n    vertex %zu capt:", v);
            for (uint32_t i = 0; i < stride; ++i) std::printf(" %02x", vb[vOff + v * stride + i]);
            std::printf("\n");
        }
        if (!ibEq) {
            size_t n = 0, at = static_cast<size_t>(-1);
            for (size_t i = ibytes; i < ib.size() && i < b->indices.size(); ++i)
                if (ib[i] != b->indices[i]) {
                    ++n;
                    if (at == static_cast<size_t>(-1)) at = i / ibytes;
                }
            std::printf("    whole index buffer after the pad: %zu bytes differ, first at index %zu\n", n, at);
        }
        if (!ibEq || diff) ++failures;
    }
    return failures ? 1 : 0;
}
