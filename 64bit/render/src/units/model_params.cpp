// model_params.cpp - the textures a unit material names in its FireGrafix parameter sets (epic 0041, issue 4356;
// research/05-graphics-composition/unit-renderer.md 10.8). Own code over models' typed object view.
//
// A unit .gr2 material's ExtendedData (typeName FireGrafixMaterial) holds ParameterSetCount inline structs
// ParameterSet0..N-1. The set whose ParamBlock is "UnitShaderTextures" lists ParamNames (BaseTextureMap, SREFMap,
// IrradianceCubeMap, ...) and for each a ParamStoreIdx into ParamDataT, an array of {String} entries holding the
// texture file names. The EXE binds these, not the material's 3ds Max Maps: VP art that reuses a stock .gr2 (or was
// exported without Maps) names its own textures here [L: the catalogue's bound textures, 10.8].
#include <string>
#include <vector>

#include "c5models/gr2.h"

namespace c5r { namespace units {

namespace {
// A struct member that is inline or a reference (both forms occur in ExtendedData trees).
c5m::Object sub(const c5m::Object& o, const char* name) {
    const c5m::Member* m = o.type() ? o.type()->find(name) : nullptr;
    if (!m) return c5m::Object();
    if (m->type == c5m::MemberType::Inline) return o.inline_member(name);
    if (m->type == c5m::MemberType::Reference) return o.ref(name);
    if (m->type == c5m::MemberType::VariantReference) return o.variant(name);
    return c5m::Object();
}
}  // namespace

// Per material (in the file's Materials order): (parameter name, texture file name) of the UnitShaderTextures set.
// Materials without such a set get an empty list. False (why set) when the file does not load.
bool materialTextureParams(const uint8_t* data, size_t size, const c5m::StringDatabase* strings,
                           std::vector<std::vector<std::pair<std::string, std::string>>>& out, std::string* why) {
    out.clear();
    c5m::RawFile f;
    c5m::Error e;
    if (!f.load(data, size, strings, &e)) {
        if (why) *why = e.what;
        return false;
    }
    try {
        c5m::Object root = f.root();
        if (!root.has("Materials")) return true;
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
                if (ps.null() || !ps.has("ParamBlock") || ps.str("ParamBlock") != "UnitShaderTextures") continue;
                if (!ps.has("ParamNames") || !ps.has("ParamStoreIdx") || !ps.has("ParamDataT")) continue;
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
                    out.back().emplace_back(n.str("String"), t.str("String"));
                }
            }
        }
    } catch (const c5m::ParseError& pe) {
        if (why) *why = pe.what();
        out.clear();
        return false;
    } catch (const std::exception& se) {
        if (why) *why = se.what();
        out.clear();
        return false;
    }
    return true;
}

}}  // namespace c5r::units
