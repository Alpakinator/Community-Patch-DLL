// summary.cpp - the canonical one-line JSON summary (models/README.md "Canonical summary"; definitions fixed by
// models/tools/pyref/gr2canon.py, issue 3102). Epic 3100 (issue 3101), own code, 2026-11-11.
#include "c5models/gr2.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>

namespace c5m {
namespace {

void jstr(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {   // strings are latin-1 bytes; non-ASCII as \u00XX like Python's json.dumps
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20 || c >= 0x7F) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += (char)c;
    }
    o += '"';
}
void jnum(std::string& o, double v) {
    if (std::isnan(v)) { o += "NaN"; return; }
    if (std::isinf(v)) { o += v > 0 ? "Infinity" : "-Infinity"; return; }
    char b[40];
    if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.9g", v);
    o += b;
}
void jint(std::string& o, long long v) { o += std::to_string(v); }
std::string lname(const std::string& s) {
    size_t p = s.find_last_of("/\\");
    std::string b = p == std::string::npos ? s : s.substr(p + 1);
    for (auto& ch : b)
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    return b;
}
template <class T, class F> void jlist(std::string& o, const std::vector<T>& v, F f) {
    o += '[';
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += ',';
        f(v[i]);
    }
    o += ']';
}

}  // namespace

std::string canonical_summary(const FileInfo& f, const std::string& label) {
    std::string o = "{\"file\":";
    jstr(o, label);
    o += ",\"endian\":";
    o += f.header.big_endian ? "\"BE\"" : "\"LE\"";
    o += ",\"tag\":"; jint(o, f.header.type_tag);
    o += ",\"sdb\":"; jint(o, f.header.string_db_crc);
    o += ",\"sections\":[";
    for (size_t i = 0; i < f.section_crc.size(); ++i) {
        if (i) o += ',';
        o += '['; jint(o, f.section_size[i]); o += ','; jint(o, f.section_crc[i]); o += ']';
    }
    o += "],\"unresolved\":"; jint(o, f.unresolved_strings);
    o += ",\"textures\":";
    jlist(o, f.textures, [&](const std::string& s) { jstr(o, lname(s)); });
    o += ",\"materials\":";
    jlist(o, f.materials, [&](const Material& m) {
        o += "{\"name\":"; jstr(o, m.name);
        o += ",\"shader\":"; jstr(o, m.shader_set);
        o += ",\"maps\":";
        jlist(o, m.maps, [&](const TextureMap& t) { o += '['; jstr(o, t.usage); o += ','; jstr(o, t.basename); o += ']'; });
        o += '}';
    });
    o += ",\"skeletons\":";
    jlist(o, f.skeletons, [&](const Skeleton& s) {
        o += "{\"name\":"; jstr(o, s.name);
        o += ",\"bones\":"; jint(o, (long long)s.bones.size());
        o += ",\"parents\":";
        jlist(o, s.bones, [&](const Bone& b) { jint(o, b.parent); });
        o += '}';
    });
    o += ",\"models\":";
    jlist(o, f.models, [&](const ModelEntry& m) {
        o += "{\"name\":"; jstr(o, m.name);
        o += ",\"skel\":"; jstr(o, m.skeleton_name);
        o += ",\"meshes\":";
        jlist(o, m.mesh_names, [&](const std::string& s) { jstr(o, s); });
        o += '}';
    });
    o += ",\"meshes\":";
    jlist(o, f.meshes, [&](const Mesh& m) {
        o += "{\"name\":"; jstr(o, m.name);
        o += ",\"v\":"; jint(o, m.vertex_count);
        o += ",\"stride\":"; jint(o, m.format.stride);
        o += ",\"fmt\":"; jstr(o, m.format.key());
        o += ",\"i\":"; jint(o, (long long)m.indices.size());
        o += ",\"isz\":"; jint(o, m.index_size);
        long long mx = -1;
        for (uint32_t ix : m.indices)
            if ((long long)ix > mx) mx = ix;
        o += ",\"imax\":"; jint(o, mx);
        o += ",\"groups\":";
        jlist(o, m.groups, [&](const TriGroup& g) {
            o += '['; jint(o, g.material); o += ','; jint(o, g.tri_first); o += ','; jint(o, g.tri_count); o += ']';
        });
        o += ",\"bones\":";
        jlist(o, m.bone_bindings, [&](const BoneBinding& b) { jstr(o, b.bone_name); });
        o += ",\"mats\":";
        jlist(o, m.material_names, [&](const std::string& s) { jstr(o, s); });
        o += ",\"bbox\":";
        bool any = m.format.find("Position") && m.vertex_count;
        if (!any) {
            o += "null";
        } else {
            // NaN components skipped per axis (fmin/fmax); an axis with only NaN gives NaN; infinities count
            double lo[3], hi[3];
            bool seen[3] = {false, false, false};
            for (uint32_t v = 0; v < m.vertex_count; ++v) {
                float p[4] = {0, 0, 0, 0};
                m.read(v, "Position", p);
                for (int k = 0; k < 3; ++k) {
                    if (std::isnan(p[k])) continue;
                    if (!seen[k]) { lo[k] = hi[k] = p[k]; seen[k] = true; }
                    else { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; }
                }
            }
            o += '[';
            for (int k = 0; k < 6; ++k) {
                if (k) o += ',';
                int a = k % 3;
                jnum(o, seen[a] ? (k < 3 ? lo[a] : hi[a]) : NAN);
            }
            o += ']';
        }
        o += ",\"morphs\":"; jint(o, m.morph_targets);
        o += '}';
    });
    o += ",\"tgs\":";
    // the root list only (issue 4353: groups reached only through an animation are appended after it)
    const std::vector<TrackGroup> root_tgs(f.track_groups.begin(),
                                           f.track_groups.end() - (std::ptrdiff_t)std::min<size_t>(
                                               f.animation_only_track_groups, f.track_groups.size()));
    jlist(o, root_tgs, [&](const TrackGroup& t) {
        o += "{\"name\":"; jstr(o, t.name);
        o += ",\"tt\":"; jint(o, (long long)t.transform_tracks.size());
        o += ",\"vt\":"; jint(o, t.vector_tracks);
        o += ",\"xt\":"; jint(o, t.text_tracks);
        std::map<std::string, int> c;
        for (const auto& tt : t.transform_tracks) {
            const Curve* cs[3] = {&tt.position, &tt.orientation, &tt.scale_shear};
            const char* ks[3] = {"P", "O", "S"};
            for (int k = 0; k < 3; ++k)
                if (cs[k]->present)
                    c[std::string(ks[k]) + ":" + cs[k]->format_name + ":" + std::to_string(cs[k]->format) + ":" +
                      std::to_string(cs[k]->degree)]++;
        }
        o += ",\"curves\":{";
        bool first = true;
        for (const auto& kv : c) {
            if (!first) o += ',';
            first = false;
            jstr(o, kv.first); o += ':'; jint(o, kv.second);
        }
        o += "}}";
    });
    o += ",\"anims\":";
    jlist(o, f.animations, [&](const Animation& a) {
        o += "{\"name\":"; jstr(o, a.name);
        o += ",\"dur\":"; jnum(o, a.duration);
        o += ",\"step\":"; jnum(o, a.time_step);
        o += ",\"over\":"; jnum(o, a.oversampling);
        o += ",\"tgs\":";
        jlist(o, a.track_group_names, [&](const std::string& s) { jstr(o, s); });
        o += '}';
    });
    o += '}';
    return o;
}

}  // namespace c5m
