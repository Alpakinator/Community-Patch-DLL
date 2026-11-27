// library.cpp - layer 3 of c5r::landmarks: the model library (ticket 4872, epic 0561). Own code from the knowledge
// base's description (D83): city-renderer.md 2.2 and 3.2, cities-and-landmarks.md 1.2, 1.6, 1.7, 2.1,
// landmarks-and-models.md 1-2, landmark-renderer.md.
#include "c5r/landmarks/library.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>

#include "c5r/cities/catalogue.h"
#include "c5r/farmfields/buildings.h"   // ticket 0653: FarmRowChoice, farmRowLevel
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/art.h"    // units::GameFilesProvider
#include "c5r/units/draw.h"   // units::loadStringDatabases

namespace c5r { namespace landmarks {

const char* sourceName(Source s) {
    switch (s) {
    case Source::LandmarkRow: return "landmark";
    case Source::Wonder: return "wonder";
    case Source::Wall: return "wall";
    case Source::GreatWall: return "greatwall";
    case Source::CitySet: return "cityset";
    }
    return "?";
}

Handler handlerFromName(const std::string& s) {
    static const char* const names[] = {"SNAPSHOT", "FARM", "SPECIAL", "PASTURE", "SPRING", "ANIMATED",
                                        "ANIMATED_ROTATED", "RANDOM"};
    for (int i = 0; i < 8; ++i)
        if (s == names[i]) return static_cast<Handler>(i);
    return Handler::Snapshot;
}

namespace {

const std::string& txt(const c5m::DbRow& r, size_t i) {
    static const std::string empty;
    return (i < r.size() && !r[i].null) ? r[i].text : empty;
}

std::string scaleKey(const std::string& name, float scale) {
    uint32_t bits;
    std::memcpy(&bits, &scale, 4);
    return name + "|" + std::to_string(bits);
}

}  // namespace

ModelLibrary::ModelLibrary() = default;
ModelLibrary::~ModelLibrary() = default;

Status ModelLibrary::readFile(const std::string& gr2, c5m::FileInfo& fi, std::string* origin,
                              std::vector<uint8_t>* keep) const {
    if (!gf_->exists(gr2)) return Status::fail(Err::NotFound, gr2 + ": not in the game's files");
    std::vector<uint8_t> bytes;
    C5R_TRY(gf_->read(gr2, bytes, origin));
    c5m::Error e;
    if (!c5m::read_gr2(bytes.data(), bytes.size(), strings_.get(), fi, &e))
        return Status::fail(Err::BadLayout, gr2 + ": " + c5m::err_name(e.code) + " " + e.what);
    if (keep) keep->swap(bytes);
    return Status::Ok();
}

namespace {
// t0 / t1 from the material's BuildingTextures parameter set when it names a file (ticket 4873; landmark-renderer.md
// 7.4 [C]): the EXE binds those, as for the units (unit-renderer.md 10.8). A path keeps only its base name.
void parameterTextures(const std::vector<std::vector<MaterialParam>>& params, FileLayout& lay) {
    for (Part& p : lay.parts) {
        if (p.materialIndex >= params.size()) continue;
        for (const MaterialParam& mp : params[p.materialIndex]) {
            if (mp.block != "BuildingTextures" || mp.value.empty()) continue;
            const std::string base = c5m::basename_lower(mp.value);
            if (mp.name == "Diffuse") p.t0 = base;
            else if (mp.name == "BuildingSREF") p.t1 = base;
        }
    }
}
}  // namespace

int32_t ModelLibrary::addFile(const std::string& gr2, float scale, ModelClass cls) {
    const std::string k = scaleKey(gr2, scale);
    auto it = fileIndex_.find(k);
    if (it != fileIndex_.end()) return static_cast<int32_t>(it->second);
    ModelFile f;
    f.gr2 = gr2;
    f.scale = scale;
    f.cls = cls;
    fileIndex_[k] = static_cast<uint32_t>(files_.size());
    files_.push_back(f);
    return static_cast<int32_t>(files_.size() - 1);
}

int32_t ModelLibrary::fileIndex(const std::string& gr2, float scale) const {
    auto it = fileIndex_.find(scaleKey(c5m::ascii_lower(gr2), scale));
    return it == fileIndex_.end() ? -1 : static_cast<int32_t>(it->second);
}

const Model* ModelLibrary::model(const std::string& name) const {
    auto it = modelIndex_.find(c5m::ascii_lower(name));
    return it == modelIndex_.end() ? nullptr : &models_[it->second];
}

Status ModelLibrary::load(const gamefs::GameFiles& files, c5m::TableSource& db, const cities::ArtCatalogue& art) {
    const auto t0 = std::chrono::steady_clock::now();
    evictAll();
    loaded_ = false;
    files_.clear();
    models_.clear();
    rows_.clear();
    modelIndex_.clear();
    fileIndex_.clear();
    keys_.clear();
    resident_.clear();
    counts_ = LibraryCounts();
    gf_ = &files;
    if (!art.loaded()) return Status::fail(Err::NotFound, "the art catalogue is not loaded");

    // The ArtDefine_Landmarks rows (cities-and-landmarks.md 2.1: the columns the EXE selects).
    std::vector<c5m::DbRow> rows;
    c5m::Error e;
    if (!db.rows("ArtDefine_Landmarks",
                 {"ImprovementType", "ResourceType", "Era", "State", "Scale", "TerrainContour", "Model",
                  "LayoutHandler", "Tech"},
                 rows, &e))
        return Status::fail(Err::NotFound, "database table ArtDefine_Landmarks: " + e.what);

    // Strings of the files that keep their names in a .gsd (the city sets do).
    strings_.reset(new c5m::StringDatabase());
    units::GameFilesProvider prov(files);
    {
        units::UnitStatus us = units::loadStringDatabases(files.names(), prov, *strings_);
        if (!us.ok()) return Status::fail(Err::BadLayout, "string databases: " + us.str());
    }

    // A model named by an .fxsxml (landmark rows and wonders): read it for its mesh and whether it animates.
    std::map<std::string, std::pair<std::string, bool>> fxs;   // fxsxml -> (gr2, animated); gr2 "" when unreadable
    std::map<std::string, std::string> fxsProblem;
    auto resolveFxs = [&](const std::string& name) -> const std::pair<std::string, bool>& {
        auto it = fxs.find(name);
        if (it != fxs.end()) return it->second;
        std::pair<std::string, bool> r("", false);
        std::vector<uint8_t> b;
        if (!files.exists(name)) {
            fxsProblem[name] = "not in the game's files";
        } else if (!files.read(name, b).ok()) {
            fxsProblem[name] = "unreadable";
        } else {
            c5m::FxsAsset fx;
            c5m::Error fe;
            if (!c5m::parse_fxsxml(reinterpret_cast<const char*>(b.data()), b.size(), fx, &fe))
                fxsProblem[name] = "malformed: " + fe.what;
            else if (fx.mesh.empty())
                fxsProblem[name] = "names no mesh";
            else
                r = std::make_pair(c5m::basename_lower(fx.mesh), !fx.animations.empty());
        }
        return fxs.emplace(name, r).first->second;
    };
    auto addModel = [&](const std::string& name, float scale, Source src, ModelClass cls) -> uint32_t {
        const std::string k = scaleKey(name, scale);
        auto it = modelIndex_.find(k);
        if (it != modelIndex_.end()) return it->second;
        Model m;
        m.name = name;
        m.source = src;
        m.cls = cls;
        m.scale = scale;
        const uint32_t idx = static_cast<uint32_t>(models_.size());
        models_.push_back(m);
        modelIndex_[k] = idx;
        if (!modelIndex_.count(name)) modelIndex_[name] = idx;   // model(name): the first scale
        return idx;
    };
    auto bindFxs = [&](uint32_t mi) {
        Model& m = models_[mi];
        if (m.file >= 0) return;
        const std::pair<std::string, bool>& r = resolveFxs(m.name);
        m.animated = r.second;
        if (!r.first.empty()) m.file = addFile(r.first, m.scale, m.cls);
    };

    // 1. Landmark rows.
    std::set<std::string> landmarkNames;
    for (const c5m::DbRow& r : rows) {
        LandmarkRow lr;
        lr.row.improvement = txt(r, 0);
        lr.row.resource = txt(r, 1);
        lr.row.era = txt(r, 2);
        lr.row.state = txt(r, 3);
        lr.row.scale = txt(r, 4).empty() ? 1.0f : std::strtof(txt(r, 4).c_str(), nullptr);
        lr.row.terrain_contour = txt(r, 5);
        lr.row.model = txt(r, 6);
        lr.row.model_null = r.size() < 7 || r[6].null || txt(r, 6).empty();
        lr.row.layout = txt(r, 7);
        lr.row.tech = txt(r, 8);
        lr.handler = handlerFromName(lr.row.layout);
        lr.contour = static_cast<uint8_t>(std::atoi(lr.row.terrain_contour.c_str()) & 0xFF);
        if (!lr.row.model_null) {
            const std::string name = c5m::basename_lower(lr.row.model);
            landmarkNames.insert(name);
            const uint32_t mi = addModel(name, lr.row.scale, Source::LandmarkRow, ModelClass::Landmark);
            models_[mi].rows.push_back(static_cast<uint32_t>(rows_.size()));
            bindFxs(mi);
            lr.model = static_cast<int32_t>(mi);
            ++counts_.landmarkRowsWithModel;
        }
        rows_.push_back(lr);
    }
    counts_.landmarkRows = static_cast<uint32_t>(rows_.size());
    counts_.landmarkModels = static_cast<uint32_t>(landmarkNames.size());

    // 2. Wonder art: every type in force (a later file's entry replaces an earlier one).
    std::set<std::string> drawn;
    for (const cities::DisplayBuilding& d : art.displayBuildings())
        for (const cities::WonderForm& f : d.forms) drawn.insert(f.type);
    std::set<std::string> wonderNames;
    for (const c5m::WonderArt& w : art.wonderArt()) {
        const std::string name = c5m::basename_lower(w.granny);
        wonderNames.insert(name);
        const float scale = w.scale > 0 ? w.scale : 1.0f;
        const uint32_t mi = addModel(name, scale, Source::Wonder, ModelClass::CityPiece);
        models_[mi].wonderTypes.push_back(w.type);
        bindFxs(mi);
        if (drawn.count(w.type)) ++counts_.wonderTypesDrawn;
    }
    counts_.wonderTypes = static_cast<uint32_t>(art.wonderArt().size());
    counts_.wonderModels = static_cast<uint32_t>(wonderNames.size());

    // 3. Walls and the Great Wall: the files by name at scale 1 [I: the wall scale is not read, landmark-renderer.md].
    for (uint32_t i = 0; i < cities::kWallFiles; ++i) {
        const uint32_t mi = addModel(cities::wallFile(i), 1.0f, Source::Wall, ModelClass::CityPiece);
        models_[mi].file = addFile(cities::wallFile(i), 1.0f, ModelClass::CityPiece);
    }
    for (uint32_t i = 0; i < cities::kGreatWallFiles; ++i) {
        const uint32_t mi = addModel(cities::greatWallFile(i), 1.0f, Source::GreatWall, ModelClass::CityPiece);
        models_[mi].file = addFile(cities::greatWallFile(i), 1.0f, ModelClass::CityPiece);
    }
    counts_.wallFiles = cities::kWallFiles;
    counts_.greatWallFiles = cities::kGreatWallFiles;

    // 4. City sets: one model per distinct file, the culture x slot entries pointing at it.
    std::set<std::string> setFiles;
    for (uint32_t si = 0; si < art.citySets().size(); ++si) {
        const cities::CitySet& cs = art.citySets()[si];
        setFiles.insert(cs.granny);
        const uint32_t mi = addModel(cs.granny, 1.0f, Source::CitySet, ModelClass::CityBlock);
        models_[mi].citySets.push_back(si);
        if (models_[mi].file < 0) models_[mi].file = addFile(cs.granny, 1.0f, ModelClass::CityBlock);
    }
    counts_.citySetEntries = static_cast<uint32_t>(art.citySets().size());
    counts_.citySetFiles = static_cast<uint32_t>(setFiles.size());

    // 5. Every file read once and laid out; the keys of its parts.
    std::map<MeshKey, std::vector<uint32_t>> owners;   // key -> files holding it
    for (uint32_t fi = 0; fi < files_.size(); ++fi) {
        ModelFile& f = files_[fi];
        c5m::FileInfo info;
        std::vector<uint8_t> raw;
        Status s = readFile(f.gr2, info, &f.origin, &raw);
        if (s.ok()) s = layoutFile(info, f.cls == ModelClass::CityBlock, f.scale, f.layout);
        if (s.ok()) {
            std::vector<std::vector<MaterialParam>> params;
            if (materialParams(raw.data(), raw.size(), strings_.get(), params).ok()) parameterTextures(params, f.layout);
            else ++counts_.paramsUnread;
        }
        FileBuffers fb;
        if (s.ok()) s = repackFile(info, f.layout, fb);
        if (s.ok())
            for (Part& p : f.layout.parts) {
                const std::vector<uint8_t>& vb = p.skinned ? fb.skinnedVertices : fb.vertices;
                s = meshKey(fb.indices.data(), f.layout.indexCount, f.layout.indexBytes, kLeadIndices, vb.data(),
                            vb.size(), p.key.firstIndex,
                            p.key.indexCount, p.key.baseVertex, p.key);
                if (!s.ok()) break;
            }
        if (!s.ok()) {
            f.ok = false;
            f.problem = s.str();
            f.layout = FileLayout();
            ++counts_.filesFailed;
            continue;
        }
        f.ok = true;
        ++counts_.filesOk;
        for (const c5m::ModelEntry& me : info.models) {
            f.models.push_back(me.name);
            std::vector<uint32_t> parts;
            for (uint32_t pi = 0; pi < f.layout.parts.size(); ++pi)
                if (std::find(me.meshes.begin(), me.meshes.end(), static_cast<int32_t>(f.layout.parts[pi].fileMesh)) !=
                    me.meshes.end())
                    parts.push_back(pi);
            f.modelParts.push_back(parts);
        }
        for (uint32_t pi = 0; pi < f.layout.parts.size(); ++pi) {
            const MeshKey& k = f.layout.parts[pi].key;
            ++counts_.parts;
            std::vector<uint32_t>& o = owners[k];
            if (std::find(o.begin(), o.end(), fi) == o.end()) o.push_back(fi);
            Hit h;
            h.file = fi;
            h.part = pi;
            keys_[k].push_back(h);
        }
    }
    counts_.files = static_cast<uint32_t>(files_.size());
    counts_.keys = static_cast<uint32_t>(keys_.size());
    for (auto& kv : owners) {
        if (kv.second.size() < 2) continue;
        ++counts_.keyCollisions;
        std::set<std::vector<std::pair<std::string, std::string>>> tex;
        for (uint32_t fi : kv.second)
            for (const Part& p : files_[fi].layout.parts)
                if (p.key == kv.first) tex.insert(p.textures);
        if (tex.size() > 1) ++counts_.keyCollisionsTextured;
    }
    // Models whose .fxsxml could not be read stay without a file.
    for (Model& m : models_)
        if (m.file < 0 && fxsProblem.count(m.name)) m.problem = m.name + ": " + fxsProblem[m.name];
    uint64_t bytes = 0;
    for (const ModelFile& f : files_) {
        bytes += sizeof(ModelFile) + f.gr2.size() + f.origin.size() + f.problem.size();
        for (const Part& p : f.layout.parts) {
            bytes += sizeof(Part) + p.mesh.size() + p.material.size() + p.shaderSet.size();
            for (auto& t : p.textures) bytes += t.first.size() + t.second.size() + 2 * sizeof(std::string);
        }
        for (auto& m : f.models) bytes += m.size() + sizeof(std::string);
    }
    for (const Model& m : models_) bytes += sizeof(Model) + m.name.size() + m.rows.size() * 4;
    bytes += rows_.size() * sizeof(LandmarkRow) + keys_.size() * (sizeof(MeshKey) + sizeof(Hit) + 16);
    counts_.catalogueBytes = bytes;
    resident_.resize(files_.size());
    loaded_ = true;
    counts_.loadSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return Status::Ok();
}

const ModelLibrary::Hit* ModelLibrary::find(const MeshKey& k) const {
    auto it = keys_.find(k);
    return it == keys_.end() || it->second.empty() ? nullptr : &it->second[0];
}

std::vector<ModelLibrary::Hit> ModelLibrary::candidates(const MeshKey& k) const {
    auto it = keys_.find(k);
    return it == keys_.end() ? std::vector<Hit>() : it->second;
}

std::vector<MeshKey> ModelLibrary::keys(ModelClass c) const {
    std::vector<MeshKey> v;
    for (auto& kv : keys_)
        if (!kv.second.empty() && files_[kv.second[0].file].cls == c) v.push_back(kv.first);
    std::sort(v.begin(), v.end());
    return v;
}

int32_t ModelLibrary::rowFile(int32_t row) const {
    if (row < 0 || static_cast<size_t>(row) >= rows_.size()) return -1;
    const int32_t m = rows_[row].model;
    if (m < 0 || static_cast<size_t>(m) >= models_.size()) return -1;
    const int32_t f = models_[m].file;
    if (f < 0 || static_cast<size_t>(f) >= files_.size() || !files_[f].ok) return -1;
    return f;
}

int32_t ModelLibrary::farmBuildingModel(int32_t file, uint8_t state) const {
    if (file < 0 || static_cast<size_t>(file) >= files_.size() || !files_[file].ok) return -1;
    const std::vector<std::string>& models = files_[file].models;
    for (size_t i = 0; i < models.size(); ++i) {
        const std::string m = c5m::ascii_lower(models[i]);
        const bool pl = m.compare(0, 3, "pl_") == 0, hb = m.compare(0, 3, "hb_") == 0;
        if (state == 3 ? pl : state == 1 ? hb : (!pl && !hb)) return static_cast<int32_t>(i);
    }
    return -1;
}

Status ModelLibrary::farmRows(farmfields::FarmRowChoice& out) const {
    out = farmfields::FarmRowChoice();
    if (!loaded_) return Status::fail(Err::NotFound, "farmRows: the model library is not loaded");
    auto upper = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    for (size_t i = 0; i < rows_.size(); ++i) {
        const LandmarkRow& lr = rows_[i];
        if (lr.handler != Handler::Farm) continue;
        ++out.farmHandlerRows;
        const std::string imp = upper(lr.row.improvement), res = upper(lr.row.resource), st = upper(lr.row.state);
        if (imp != "ART_DEF_IMPROVEMENT_FARM") continue;
        if (!(res.empty() || res == "ART_DEF_RESOURCE_NONE" || res == "RESOURCE_NONE" || res == "NONE")) continue;
        if (!(st.empty() || st == "ANY")) continue;
        const std::string name = "row " + std::to_string(i) + " (" + lr.row.improvement + " / " +
                                 (lr.row.resource.empty() ? std::string("no resource") : lr.row.resource) + " / Era " +
                                 (lr.row.era.empty() ? std::string("''") : lr.row.era) + " / " +
                                 (lr.row.model_null ? std::string("NULL model") : lr.row.model) + ")";
        farmfields::FarmRowRefusal ref;
        ref.row = static_cast<int32_t>(i);
        const int32_t level = farmfields::farmRowLevel(lr.row.era);
        if (level < 0) {
            ref.text = name + ": its Era is not an era the farm manager knows";
            out.refused.push_back(ref);
            continue;
        }
        if (lr.model < 0) {
            ref.text = name + ": the row names no model";
            out.refused.push_back(ref);
            continue;
        }
        const Model& m = models_[lr.model];
        if (m.file < 0) {
            ref.text = name + ": its model cannot be read (" + (m.problem.empty() ? std::string("no file named") : m.problem) + ")";
            out.refused.push_back(ref);
            continue;
        }
        const ModelFile& f = files_[m.file];
        if (!f.ok) {
            ref.text = name + ": its model file " + f.gr2 + " was refused (" + f.problem + ")";
            out.refused.push_back(ref);
            continue;
        }
        std::vector<int32_t>& lv = out.level[level];
        if (lv.size() >= farmfields::kFarmRowsPerLevel) {
            ref.text = name + ": a fourth row of era level " + std::to_string(level) +
                       " (the farm manager's table holds 3 per level)";
            out.refused.push_back(ref);
            continue;
        }
        lv.push_back(static_cast<int32_t>(i));
    }
    return Status::Ok();
}

Status ModelLibrary::resident(uint32_t file, const Buffers** out) {
    if (out) *out = nullptr;
    if (!loaded_ || file >= files_.size()) return Status::fail(Err::BadLayout, "file index out of range");
    const ModelFile& f = files_[file];
    if (!f.ok) return Status::fail(Err::NotFound, f.gr2 + ": not laid out (" + f.problem + ")");
    if (!resident_[file]) {
        c5m::FileInfo info;
        C5R_TRY(readFile(f.gr2, info, nullptr));
        std::unique_ptr<Buffers> b(new Buffers());
        C5R_TRY(repackFile(info, f.layout, *b));
        residentBytes_ += b->vertices.size() + b->skinnedVertices.size() + b->indices.size();
        ++residentFiles_;
        resident_[file] = std::move(b);
    }
    if (out) *out = resident_[file].get();
    return Status::Ok();
}

bool ModelLibrary::isResident(uint32_t file) const { return file < resident_.size() && resident_[file] != nullptr; }

void ModelLibrary::release(uint32_t file) {
    if (file >= resident_.size() || !resident_[file]) return;
    const Buffers& b = *resident_[file];
    residentBytes_ -= b.vertices.size() + b.skinnedVertices.size() + b.indices.size();
    --residentFiles_;
    resident_[file].reset();
}

void ModelLibrary::evictAll() {
    for (auto& r : resident_) r.reset();
    residentBytes_ = 0;
    residentFiles_ = 0;
}

}}  // namespace c5r::landmarks
