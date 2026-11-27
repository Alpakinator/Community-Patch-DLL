// gr2_model.cpp - from the generic object tree of a Granny file to the plain model structures of gr2.h.
// Epic 3100 (issue 3101), own code, 2026-11-11. Member names are those the files' own type trees carry (one-off
// 9012, research/05-graphics-composition/landmarks-and-models.md 4.7-4.8).
#include "c5models/gr2.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace c5m {

static uint64_t akey(Addr a) { return ((uint64_t)a.sec << 32) | a.off; }

// (issue 4000, bug 0093 V10) the model structures read_gr2 builds count against the read budget (RawFile::charge_bytes)
// with their sizes; an index map entry counts as one unordered_map node (as kMapNodeBytes in gr2_file.cpp)
static const uint64_t kNode = 64;

const VertexComponent* VertexFormat::find(const char* name) const {
    for (const auto& c : components)
        if (c.name == name) return &c;
    return nullptr;
}

std::string VertexFormat::key() const {
    std::string s;
    for (size_t i = 0; i < components.size(); ++i) {
        if (i) s += ',';
        s += member_type_name(components[i].type);
        s += ':' + components[i].name + ':' + std::to_string(components[i].count);
    }
    return s;
}

int32_t Skeleton::find(const std::string& bone) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == bone) return (int32_t)i;
    return -1;
}

bool Bone::track_mask(const std::string& name, float& weight) const {
    for (const auto& tm : track_masks)
        if (tm.first == name) {
            weight = tm.second;
            return true;
        }
    return false;
}

static float h2f(uint16_t h) {
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 0x1F, m = h & 0x3FF, bits;
    if (e == 0) {
        float f = std::ldexp((float)m, -24);
        return (h >> 15) ? -f : f;
    }
    bits = e == 31 ? (s | 0x7F800000u | (m << 13)) : (s | ((e + 112) << 23) | (m << 13));
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

uint32_t Mesh::read(uint32_t v, const char* name, float out[4]) const {
    const VertexComponent* c = format.find(name);
    if (!c || v >= vertex_count) return 0;
    uint32_t n = c->count > 4 ? 4 : c->count;
    // (issue 9025, bug 0093 V3) never read past the vertex buffer, whatever vertex_count and stride say
    uint64_t at = (uint64_t)v * format.stride + c->offset, len = (uint64_t)n * member_type_size(c->type);
    if (at + len > vertices.size()) return 0;
    const uint8_t* p = vertices.data() + at;
    for (uint32_t i = 0; i < n; ++i) {
        switch (c->type) {
        case MemberType::Real32: std::memcpy(&out[i], p + 4 * i, 4); break;
        case MemberType::Real16: { uint16_t h; std::memcpy(&h, p + 2 * i, 2); out[i] = h2f(h); break; }
        case MemberType::NormalUInt8: out[i] = p[i] / 255.0f; break;
        case MemberType::UInt8: out[i] = p[i]; break;
        case MemberType::Int8: out[i] = (int8_t)p[i]; break;
        case MemberType::BinormalInt8: out[i] = std::fmax((int8_t)p[i] / 127.0f, -1.0f); break;
        case MemberType::NormalUInt16: { uint16_t x; std::memcpy(&x, p + 2 * i, 2); out[i] = x / 65535.0f; break; }
        case MemberType::UInt16: { uint16_t x; std::memcpy(&x, p + 2 * i, 2); out[i] = x; break; }
        case MemberType::Int16: { int16_t x; std::memcpy(&x, p + 2 * i, 2); out[i] = x; break; }
        case MemberType::BinormalInt16: { int16_t x; std::memcpy(&x, p + 2 * i, 2); out[i] = std::fmax(x / 32767.0f, -1.0f); break; }
        case MemberType::Int32: { int32_t x; std::memcpy(&x, p + 4 * i, 4); out[i] = (float)x; break; }
        case MemberType::UInt32: { uint32_t x; std::memcpy(&x, p + 4 * i, 4); out[i] = (float)x; break; }
        default: return 0;
        }
    }
    return n;
}

namespace {

std::string opt_str(const Object& o, const char* n) { return (!o.null() && o.has(n)) ? o.str(n) : std::string(); }
double opt_num(const Object& o, const char* n, double d = 0, uint32_t i = 0) {
    return (!o.null() && o.has(n)) ? o.number(n, i) : d;
}
Array opt_array(const Object& o, const char* n) { return (!o.null() && o.has(n)) ? o.array(n) : Array(); }
Object opt_ref(const Object& o, const char* n) { return (!o.null() && o.has(n)) ? o.ref(n) : Object(); }

std::string lower_basename(const std::string& s) {
    size_t p = s.find_last_of("/\\");
    std::string b = p == std::string::npos ? s : s.substr(p + 1);
    for (auto& ch : b)
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    return b;
}

bool is_number_type(MemberType t) {
    uint32_t i = (uint32_t)t;
    return i >= 10 && i <= 21;
}

// Copies count elements of type t (stride bytes apart) into little-endian bytes, swapping each primitive of a
// big-endian file according to the type tree.
void copy_le(const RawFile& f, const TypeDef* t, Addr base, uint32_t count, Bytes& out) {
    const Bytes& sec = f.section(base.sec);
    size_t n = (size_t)count * t->size;
    f.need(base, (uint32_t)n);
    // (issue 4000, bug 0093 V10) charged before the copy: 4,096 references to one 1 MiB mesh once asked for 4 GiB
    f.charge_bytes(n);
    out.assign(sec.begin() + base.off, sec.begin() + base.off + n);
    if (!f.header().big_endian) return;
    f.charge_steps((uint64_t)count * t->members.size());
    for (uint32_t v = 0; v < count; ++v)
        for (const Member& m : t->members) {
            uint32_t es = member_type_size(m.type);
            if (es != 2 && es != 4) continue;   // inline structs inside vertices are not met in the game's files
            uint32_t k = m.array_size ? m.array_size : 1;
            for (uint32_t i = 0; i < k; ++i) {
                uint8_t* p = out.data() + (size_t)v * t->size + m.offset + es * i;
                if (es == 2) std::swap(p[0], p[1]);
                else { std::swap(p[0], p[3]); std::swap(p[1], p[2]); }
            }
        }
}

Curve read_curve(const Object& curve2) {
    Curve c;
    if (curve2.null() || !curve2.has("CurveData")) return c;
    Object d = curve2.variant("CurveData");
    if (d.null()) return c;
    c.present = true;
    const TypeDef* t = d.type();
    const RawFile& f = *d.file();
    if (!t->members.empty()) {
        const Member& h = t->members[0];
        f.charge_bytes(h.name.size());
        c.format_name = h.name;
        if (c.format_name.compare(0, 16, "CurveDataHeader_") == 0) c.format_name = c.format_name.substr(16);
        if (h.type == MemberType::Inline && h.ref) {
            Object ho(d.file(), h.ref, Addr{d.addr().sec, d.addr().off + h.offset});
            c.format = (uint32_t)opt_num(ho, "Format");
            c.degree = (uint32_t)opt_num(ho, "Degree");
        }
    }
    for (size_t i = 1; i < t->members.size(); ++i) {
        const Member& m = t->members[i];
        if (is_number_type(m.type)) {
            std::vector<float> v;
            uint32_t n = m.array_size ? m.array_size : 1;
            f.charge_bytes(sizeof(c.scalars[0]) + m.name.size() + 4ull * n);
            for (uint32_t k = 0; k < n; ++k) v.push_back((float)d.number(m.name.c_str(), k));
            c.scalars.push_back(std::make_pair(m.name, v));
        } else if (m.type == MemberType::ReferenceToArray) {
            Array a = d.array(m.name.c_str());
            f.charge_bytes(sizeof(CurveArray) + m.name.size());
            CurveArray ca;
            ca.name = m.name;
            ca.count = a.size();
            if (a.type() && !a.type()->members.empty()) {
                ca.type = a.type()->members[0].type;
                if (a.size()) copy_le(*d.file(), a.type(), a.base(), a.size(), ca.data);
            }
            c.arrays.push_back(ca);
        }
    }
    return c;
}

}  // namespace

bool read_gr2(const uint8_t* data, size_t size, const StringDatabase* strings, FileInfo& out, Error* err) {
    out = FileInfo();
    RawFile f;
    // (issue 9025, bug 0093 V4) the load runs inside the try as well; RawFile::load also reports a failed
    // allocation itself as OutOfMemory
    try {
        if (!f.load(data, size, strings, err)) return false;
        out.header = f.header();
        out.stats = f.stats();
        for (uint32_t i = 0; i < f.section_count(); ++i) {
            out.section_size.push_back(f.header().sections[i].expanded_size);
            out.section_crc.push_back(crc32(f.section(i).data(), f.section(i).size()));
        }
        Object r = f.root();
        Object at = opt_ref(r, "ArtToolInfo");
        out.art_tool = opt_str(at, "FromArtToolName");
        out.units_per_meter = (float)opt_num(at, "UnitsPerMeter");
        for (int i = 0; i < 3; ++i) {
            if (!at.null() && at.has("UpVector")) out.up[i] = at.f32("UpVector", i);
            if (!at.null() && at.has("RightVector")) out.right[i] = at.f32("RightVector", i);
            if (!at.null() && at.has("BackVector")) out.back[i] = at.f32("BackVector", i);
            if (!at.null() && at.has("Origin")) out.origin[i] = at.f32("Origin", i);
        }
        out.exporter = opt_str(opt_ref(r, "ExporterInfo"), "ExporterName");
        out.from_file = opt_str(r, "FromFileName");

        Array tex = opt_array(r, "Textures");
        for (uint32_t i = 0; i < tex.size(); ++i) {
            f.charge_bytes(sizeof(std::string));
            out.textures.push_back(opt_str(tex.at(i), "FromFileName"));
        }

        std::unordered_map<uint64_t, int32_t> mat_index, skel_index, mesh_index, tg_index;
        Array mats = opt_array(r, "Materials");
        for (uint32_t i = 0; i < mats.size(); ++i) {
            Object m = mats.at(i);
            f.charge_bytes(sizeof(Material) + kNode);
            Material mm;
            if (!m.null()) {
                mat_index.emplace(akey(m.addr()), (int32_t)i);
                mm.name = opt_str(m, "Name");
                Array maps = opt_array(m, "Maps");
                for (uint32_t k = 0; k < maps.size(); ++k) {
                    Object mp = maps.at(k);
                    TextureMap tm;
                    tm.usage = opt_str(mp, "Usage");
                    tm.file = opt_str(opt_ref(opt_ref(mp, "Map"), "Texture"), "FromFileName");
                    f.charge_bytes(sizeof(TextureMap) + tm.file.size());
                    tm.basename = lower_basename(tm.file);
                    mm.maps.push_back(tm);
                }
                mm.texture_file = opt_str(opt_ref(m, "Texture"), "FromFileName");
                if (m.has("ExtendedData")) {
                    Object ed = m.variant("ExtendedData");
                    if (!ed.null()) {
                        for (const Member& em : ed.type()->members) {
                            if (em.type == MemberType::String) {
                                std::string v = ed.str(em.name.c_str());
                                f.charge_bytes(sizeof(mm.extended[0]) + em.name.size() + 2 * v.size());
                                if (em.name == "ShaderSet") mm.shader_set = v;
                                mm.extended.push_back(std::make_pair(em.name, v));
                            } else if (is_number_type(em.type) && em.array_size == 0) {
                                f.charge_bytes(sizeof(mm.extended[0]) + em.name.size() + 48);
                                char buf[48];
                                std::snprintf(buf, sizeof buf, "%.9g", ed.number(em.name.c_str()));
                                mm.extended.push_back(std::make_pair(em.name, std::string(buf)));
                            }
                        }
                    }
                }
            }
            out.materials.push_back(mm);
        }

        Array sks = opt_array(r, "Skeletons");
        for (uint32_t i = 0; i < sks.size(); ++i) {
            Object s = sks.at(i);
            f.charge_bytes(sizeof(Skeleton) + kNode);
            Skeleton sk;
            if (!s.null()) {
                skel_index.emplace(akey(s.addr()), (int32_t)i);
                sk.name = opt_str(s, "Name");
                sk.lod_type = (int32_t)opt_num(s, "LODType");
                Array bones = opt_array(s, "Bones");
                for (uint32_t k = 0; k < bones.size(); ++k) {
                    Object b = bones.at(k);
                    f.charge_bytes(sizeof(Bone));
                    Bone bb;
                    bb.name = opt_str(b, "Name");
                    bb.parent = (int32_t)opt_num(b, "ParentIndex", -1);
                    // Granny 2.8 names (the game's type trees): Transform, InverseWorldTransform (Real32[16])
                    if (b.has("Transform")) bb.local = b.transform("Transform");
                    else if (b.has("LocalTransform")) bb.local = b.transform("LocalTransform");
                    const char* iw = b.has("InverseWorldTransform") ? "InverseWorldTransform"
                                     : (b.has("InverseWorld4x4") ? "InverseWorld4x4" : nullptr);
                    if (iw) {
                        if (b.type()->find(iw)->array_size != 16)
                            throw ParseError(Err::TypeTreeCorrupt, "inverse world transform is not Real32[16]");
                        for (uint32_t q = 0; q < 16; ++q) bb.inverse_world[q] = b.f32(iw, q);
                    }
                    bb.lod_error = (float)opt_num(b, "LODError");
                    // 4410: ExtendedData.GrannyTrackMasks -> {Real32 <binding name>; ...} (unit-controller.md 13.3):
                    // a Reference in the game's files (an inline struct is read the same way). Scalar numbers only;
                    // any other shape leaves the list empty (a mask binding then finds no bone and is refused by name).
                    if (b.has("ExtendedData") && b.type()->find("ExtendedData")->type == MemberType::VariantReference) {
                        Object ed = b.variant("ExtendedData");
                        const Member* gm = ed.null() ? nullptr : ed.type()->find("GrannyTrackMasks");
                        Object tm;
                        if (gm && gm->ref && gm->array_size == 0) {
                            if (gm->type == MemberType::Reference) tm = ed.ref("GrannyTrackMasks");
                            else if (gm->type == MemberType::Inline) tm = ed.inline_member("GrannyTrackMasks");
                        }
                        if (!tm.null()) {
                            for (const Member& wm : tm.type()->members) {
                                if (!is_number_type(wm.type) || wm.array_size != 0) continue;
                                f.charge_bytes(sizeof(bb.track_masks[0]) + wm.name.size());
                                bb.track_masks.push_back(
                                    std::make_pair(wm.name, (float)tm.number(wm.name.c_str())));
                            }
                        }
                    }
                    sk.bones.push_back(bb);
                }
            }
            out.skeletons.push_back(sk);
        }

        Array meshes = opt_array(r, "Meshes");
        for (uint32_t i = 0; i < meshes.size(); ++i) {
            Object m = meshes.at(i);
            f.charge_bytes(sizeof(Mesh) + kNode);
            Mesh me;
            if (!m.null()) {
                mesh_index.emplace(akey(m.addr()), (int32_t)i);
                me.name = opt_str(m, "Name");
                Object vd = opt_ref(m, "PrimaryVertexData");
                Array verts = opt_array(vd, "Vertices");
                if (verts.type()) {
                    me.format.stride = verts.type()->size;
                    for (const Member& c : verts.type()->members) {
                        f.charge_bytes(sizeof(VertexComponent) + c.name.size());
                        VertexComponent vc;
                        vc.type = c.type;
                        vc.name = c.name;
                        vc.count = c.array_size ? c.array_size : 1;
                        vc.offset = c.offset;
                        me.format.components.push_back(vc);
                    }
                    me.vertex_count = verts.size();
                    if (verts.size()) copy_le(f, verts.type(), verts.base(), verts.size(), me.vertices);
                }
                Object topo = opt_ref(m, "PrimaryTopology");
                Array g = opt_array(topo, "Groups");
                for (uint32_t k = 0; k < g.size(); ++k) {
                    Object go = g.at(k);
                    f.charge_bytes(sizeof(TriGroup));
                    TriGroup tg;
                    tg.material = (uint32_t)opt_num(go, "MaterialIndex");
                    tg.tri_first = (uint32_t)opt_num(go, "TriFirst");
                    tg.tri_count = (uint32_t)opt_num(go, "TriCount");
                    me.groups.push_back(tg);
                }
                Array i32 = opt_array(topo, "Indices"), i16 = opt_array(topo, "Indices16");
                me.index_size = i16.size() ? 2 : (i32.size() ? 4 : 0);
                // the Python reference lists Indices first, then Indices16
                for (const Array* a : {&i32, &i16}) {
                    if (!a->size()) continue;
                    if (!a->type() || a->type()->members.empty())
                        throw ParseError(Err::TypeTreeCorrupt, "index array without a type");
                    const Member& im = a->type()->members[0];
                    uint32_t st = a->stride();
                    Addr b = a->base();
                    f.charge_bytes(4ull * a->size());
                    f.charge_steps(a->size());
                    me.indices.reserve(me.indices.size() + a->size());
                    for (uint32_t k = 0; k < a->size(); ++k) {
                        Addr e{b.sec, b.off + st * k + im.offset};
                        uint32_t v;
                        switch (im.type) {
                        case MemberType::UInt16: case MemberType::Int16: v = f.u16(e); break;
                        case MemberType::Int32: case MemberType::UInt32: v = f.u32(e); break;
                        default: throw ParseError(Err::TypeTreeCorrupt, "index type " + std::string(member_type_name(im.type)));
                        }
                        me.indices.push_back(v);
                    }
                }
                Array mb = opt_array(m, "MaterialBindings");
                for (uint32_t k = 0; k < mb.size(); ++k) {
                    Object mat = opt_ref(mb.at(k), "Material");
                    f.charge_bytes(sizeof(int32_t) + sizeof(std::string));
                    me.material_names.push_back(opt_str(mat, "Name"));
                    auto it = mat.null() ? mat_index.end() : mat_index.find(akey(mat.addr()));
                    me.materials.push_back(it == mat_index.end() ? -1 : it->second);
                }
                Array bb = opt_array(m, "BoneBindings");
                for (uint32_t k = 0; k < bb.size(); ++k) {
                    Object b = bb.at(k);
                    f.charge_bytes(sizeof(BoneBinding));
                    BoneBinding x;
                    x.bone_name = opt_str(b, "BoneName");
                    for (int q = 0; q < 3; ++q) {
                        if (b.has("OBBMin")) x.obb_min[q] = b.f32("OBBMin", q);
                        if (b.has("OBBMax")) x.obb_max[q] = b.f32("OBBMax", q);
                    }
                    me.bone_bindings.push_back(x);
                }
                me.morph_targets = opt_array(m, "MorphTargets").size();
            }
            out.meshes.push_back(std::move(me));
        }

        Array models = opt_array(r, "Models");
        for (uint32_t i = 0; i < models.size(); ++i) {
            Object m = models.at(i);
            f.charge_bytes(sizeof(ModelEntry));
            ModelEntry e;
            if (!m.null()) {
                e.name = opt_str(m, "Name");
                Object s = opt_ref(m, "Skeleton");
                e.skeleton_name = opt_str(s, "Name");
                if (!s.null()) {
                    auto it = skel_index.find(akey(s.addr()));
                    e.skeleton = it == skel_index.end() ? -1 : it->second;
                }
                if (m.has("InitialPlacement")) e.initial_placement = m.transform("InitialPlacement");
                Array mb = opt_array(m, "MeshBindings");
                for (uint32_t k = 0; k < mb.size(); ++k) {
                    Object me = opt_ref(mb.at(k), "Mesh");
                    f.charge_bytes(sizeof(int32_t) + sizeof(std::string));
                    e.mesh_names.push_back(opt_str(me, "Name"));
                    auto it = me.null() ? mesh_index.end() : mesh_index.find(akey(me.addr()));
                    e.meshes.push_back(it == mesh_index.end() ? -1 : it->second);
                }
            }
            out.models.push_back(e);
        }

        // One track group's contents (the root list's and, issue 4353, one an animation alone references).
        auto read_track_group = [&](Object t, TrackGroup& tg) {
            tg.name = opt_str(t, "Name");
            tg.vector_tracks = opt_array(t, "VectorTracks").size();
            tg.text_tracks = opt_array(t, "TextTracks").size();
            if (t.has("InitialPlacement")) tg.initial_placement = t.transform("InitialPlacement");
            tg.accumulation_flags = (uint32_t)opt_num(t, "AccumulationFlags");
            Array tts = opt_array(t, "TransformTracks");
            for (uint32_t k = 0; k < tts.size(); ++k) {
                Object tt = tts.at(k);
                f.charge_bytes(sizeof(TransformTrack));
                TransformTrack x;
                x.name = opt_str(tt, "Name");
                x.flags = (uint32_t)opt_num(tt, "Flags");
                if (tt.has("PositionCurve")) x.position = read_curve(tt.inline_member("PositionCurve"));
                if (tt.has("OrientationCurve")) x.orientation = read_curve(tt.inline_member("OrientationCurve"));
                if (tt.has("ScaleShearCurve")) x.scale_shear = read_curve(tt.inline_member("ScaleShearCurve"));
                tg.transform_tracks.push_back(std::move(x));
            }
        };
        Array tgs = opt_array(r, "TrackGroups");
        for (uint32_t i = 0; i < tgs.size(); ++i) {
            Object t = tgs.at(i);
            f.charge_bytes(sizeof(TrackGroup) + kNode);
            TrackGroup tg;
            if (!t.null()) {
                tg_index.emplace(akey(t.addr()), (int32_t)i);
                read_track_group(t, tg);
            }
            out.track_groups.push_back(std::move(tg));
        }

        Array anims = opt_array(r, "Animations");
        for (uint32_t i = 0; i < anims.size(); ++i) {
            Object a = anims.at(i);
            f.charge_bytes(sizeof(Animation));
            Animation an;
            if (!a.null()) {
                an.name = opt_str(a, "Name");
                an.duration = (float)opt_num(a, "Duration");
                an.time_step = (float)opt_num(a, "TimeStep");
                an.oversampling = (float)opt_num(a, "Oversampling");
                Array ts = opt_array(a, "TrackGroups");
                for (uint32_t k = 0; k < ts.size(); ++k) {
                    Object t = ts.at(k);
                    f.charge_bytes(sizeof(int32_t) + sizeof(std::string));
                    an.track_group_names.push_back(opt_str(t, "Name"));
                    auto it = t.null() ? tg_index.end() : tg_index.find(akey(t.addr()));
                    if (it == tg_index.end() && !t.null()) {
                        // A group the root list does not hold (VP's FBX clips leave that list empty, e.g.
                        // flagbearer_idle.gr2): read it from the animation's reference and append it (issue 4353).
                        f.charge_bytes(sizeof(TrackGroup) + kNode);
                        TrackGroup tg;
                        read_track_group(t, tg);
                        it = tg_index.emplace(akey(t.addr()), (int32_t)out.track_groups.size()).first;
                        out.track_groups.push_back(std::move(tg));
                        ++out.animation_only_track_groups;
                    }
                    an.track_groups.push_back(it == tg_index.end() ? -1 : it->second);
                }
            }
            out.animations.push_back(an);
        }
        f.walk_all();
        out.unresolved_strings = (uint32_t)f.unresolved_strings();
        out.stats = f.stats();   // with the read budget used by the whole read
    } catch (const ParseError& e) {
        if (err) *err = Error{e.code, e.what()};
        return false;
    } catch (const std::bad_alloc&) {
        if (err) *err = Error{Err::OutOfMemory, "out of memory (corrupt counts)"};
        return false;
    }
    return true;
}

}  // namespace c5m
