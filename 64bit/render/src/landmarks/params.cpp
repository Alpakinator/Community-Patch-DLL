// params.cpp - the textures a BuildingShader material names in its FireGrafix parameter sets (ticket 4873, epic
// 0561; landmark-renderer.md 7.4). Own code over models' typed object view, after the units' reader
// (render/src/units/model_params.cpp, unit-renderer.md 10.8), for every parameter block rather than one.
//
// A .gr2 material's ExtendedData (FireGrafixMaterial) holds ParameterSetCount inline structs ParameterSet0..N-1, each
// with a ParamBlock name, ParamNames and per name a ParamStoreIdx into ParamDataT ({String} entries: file names). The
// EXE binds the files these name, not the material's 3ds Max Maps (the units' finding [L]; for the BuildingShader
// models landmark-renderer.md 7.4).
#include "c5r/landmarks/library.h"

#include <string>
#include <vector>

#include "c5models/gr2.h"

namespace c5r { namespace landmarks {

namespace {
c5m::Object sub(const c5m::Object& o, const char* name) {
    const c5m::Member* m = o.type() ? o.type()->find(name) : nullptr;
    if (!m) return c5m::Object();
    if (m->type == c5m::MemberType::Inline) return o.inline_member(name);
    if (m->type == c5m::MemberType::Reference) return o.ref(name);
    if (m->type == c5m::MemberType::VariantReference) return o.variant(name);
    return c5m::Object();
}
}  // namespace

Status materialParams(const uint8_t* data, size_t size, const c5m::StringDatabase* strings,
                      std::vector<std::vector<MaterialParam>>& out) {
    out.clear();
    c5m::RawFile f;
    c5m::Error e;
    if (!f.load(data, size, strings, &e)) return Status::fail(Err::BadLayout, "gr2 not loaded: " + e.what);
    try {
        c5m::Object root = f.root();
        if (!root.has("Materials")) return Status::Ok();
        c5m::Array mats = root.array("Materials");
        for (uint32_t i = 0; i < mats.size(); ++i) {
            out.emplace_back();
            c5m::Object m = mats.at(i);
            if (m.null()) continue;
            c5m::Object ed = sub(m, "ExtendedData");
            if (ed.null() || !ed.has("ParameterSetCount")) continue;
            const int32_t sets = ed.i32("ParameterSetCount");
            for (int32_t s = 0; s < sets && s < 64; ++s) {
                const std::string nm = "ParameterSet" + std::to_string(s);
                c5m::Object ps = sub(ed, nm.c_str());
                if (ps.null() || !ps.has("ParamBlock")) continue;
                if (!ps.has("ParamNames") || !ps.has("ParamStoreIdx") || !ps.has("ParamDataT")) continue;
                const std::string block = ps.str("ParamBlock");
                c5m::Array names = ps.array("ParamNames");
                c5m::Array store = ps.array("ParamStoreIdx");
                c5m::Array dataT = ps.array("ParamDataT");
                for (uint32_t k = 0; k < names.size() && k < store.size(); ++k) {
                    c5m::Object n = names.at(k), si = store.at(k);
                    if (n.null() || si.null()) continue;
                    const int32_t idx = si.i32("Int32");
                    if (idx < 0 || static_cast<uint32_t>(idx) >= dataT.size()) continue;
                    c5m::Object t = dataT.at(static_cast<uint32_t>(idx));
                    if (t.null()) continue;
                    out.back().push_back(MaterialParam{block, n.str("String"), t.str("String")});
                }
            }
        }
    } catch (const c5m::ParseError& pe) {
        out.clear();
        return Status::fail(Err::BadLayout, std::string("parameter sets: ") + pe.what());
    } catch (const std::exception& se) {
        out.clear();
        return Status::fail(Err::BadLayout, std::string("parameter sets: ") + se.what());
    }
    return Status::Ok();
}

}}  // namespace c5r::landmarks
