// effect_xml_reader.cpp - the effect definitions reader (ticket 4786, epic 0042, 2026-11-20). Own code,
// GPL-3.0-or-later. Behaviour as described in research/05-graphics-composition/effects-formats.md 1-3 and 14.
#include "c5models/effect_xml.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace c5m {

const char* const kFxBaseFiles[kFxKinds] = {
    "civ5artdefines_viseffects.xml",
    "civ5artdefines_viseffectparticles.xml",
    "civ5artdefines_viseffectmodels.xml",
    "civ5artdefines_eventeffects.xml",
};

const char* fx_kind_name(FxKind k) {
    switch (k) {
    case FxKind::VisEffect: return "VisEffect";
    case FxKind::Particle: return "Particle";
    case FxKind::Model: return "Model";
    case FxKind::Event: return "Event";
    }
    return "?";
}

namespace {

void set_err(Error* e, Err c, const std::string& w) {
    if (e) *e = {c, w};
}

std::string at(const std::string& source, int line) { return source + ": line " + std::to_string(line) + ": "; }

// The whole (trimmed) text must be a finite float.
bool to_float(const std::string& s, float& out) {
    if (s.empty()) return false;
    const char* b = s.c_str();
    char* e = nullptr;
    errno = 0;
    float v = std::strtof(b, &e);
    if (e == b || errno == ERANGE) return false;
    while (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n') ++e;
    if (*e != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool to_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    const char* b = s.c_str();
    char* e = nullptr;
    errno = 0;
    long v = std::strtol(b, &e, 10);
    if (e == b || errno == ERANGE) return false;
    while (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n') ++e;
    if (*e != '\0' || v < -2147483647L || v > 2147483647L) return false;
    out = (int)v;
    return true;
}

bool float_child(const XmlElement& e, const char* name, const std::string& source, float& out, bool* present,
                 Error* err) {
    const XmlElement* c = e.child(name);
    if (present) *present = c != nullptr;
    if (!c) return true;
    if (!to_float(c->text, out)) {
        set_err(err, Err::Xml, at(source, c->line) + "<" + name + "> \"" + c->text + "\" is not a number");
        return false;
    }
    return true;
}

bool float_attr(const XmlElement& e, const char* name, const std::string& source, float& out, Error* err) {
    const std::string* a = e.attr(name);
    if (!a) return true;   // absent: the default stays
    if (!to_float(*a, out)) {
        set_err(err, Err::Xml, at(source, e.line) + "<" + e.name + " " + name + "=\"" + *a + "\"> is not a number");
        return false;
    }
    return true;
}

bool vec_attrs(const XmlElement& e, const std::string& source, FxVec3& v, Error* err) {
    return float_attr(e, "x", source, v.x, err) && float_attr(e, "y", source, v.y, err) &&
           float_attr(e, "z", source, v.z, err);
}

bool check_root(const XmlElement& root, const char* want, const std::string& source, Error* err) {
    if (root.name != want) {
        set_err(err, Err::Xml, source + ": root element is <" + root.name + ">, expected <" + want + ">");
        return false;
    }
    return true;
}

// The non-empty <Name> text of a reference element, or "".
std::string ref_name(const XmlElement& e, const char* tag = "Name") {
    const XmlElement* n = e.child(tag);
    return n ? n->text : std::string();
}

bool parse_vis(const XmlElement& e, const std::string& source, VisEffectDef& d, Error* err) {
    for (auto& c : e.children) {
        if (c.name == "Name") {
            d.name = c.text;
        } else if (c.name == "Transform") {
            d.has_transform = true;
            for (auto& t : c.children) {
                if (t.name == "Translation") {
                    if (!vec_attrs(t, source, d.translation, err)) return false;
                } else if (t.name == "Rotation") {
                    if (!vec_attrs(t, source, d.rotation_deg, err)) return false;
                } else if (t.name == "Scale") {
                    if (!to_float(t.text, d.scale)) {
                        set_err(err, Err::Xml, at(source, t.line) + "<Scale> \"" + t.text + "\" is not a number");
                        return false;
                    }
                } else {
                    d.unknown["Transform/" + t.name]++;
                }
            }
        } else if (c.name == "fBaseDuration" || c.name == "fBaseDurationScale" || c.name == "fMovementSpeed") {
            float v = 0;
            if (!to_float(c.text, v)) {
                set_err(err, Err::Xml, at(source, c.line) + "<" + c.name + "> \"" + c.text + "\" is not a number");
                return false;
            }
            if (c.name == "fBaseDuration") d.base_duration = v;
            else if (c.name == "fBaseDurationScale") d.base_duration_scale = v;
            else {
                d.movement_speed = v;
                d.has_movement_speed = true;
            }
        } else if (c.name == "FOWMask") {
            int m = 0;
            if (!to_int(c.text, m) || m < 1 || m > 3) {
                set_err(err, Err::Xml, at(source, c.line) + "<FOWMask> \"" + c.text + "\" is not 1, 2 or 3");
                return false;
            }
            d.fow_mask = m;
            d.fow_mask_written = true;
        } else if (c.name == "ParticleEffects") {
            for (auto& p : c.children) {
                if (p.name != "ParticleEffect") {
                    d.unknown["ParticleEffects/" + p.name]++;
                    continue;
                }
                std::string n = ref_name(p);
                if (n.empty()) ++d.empty_refs;
                else d.particles.push_back(n);
            }
        } else if (c.name == "ModelEffects") {
            for (auto& m : c.children) {
                if (m.name != "ModelEffect") {
                    d.unknown["ModelEffects/" + m.name]++;
                    continue;
                }
                FxModelRef r;
                r.name = ref_name(m);
                if (r.name.empty()) {
                    ++d.empty_refs;
                    continue;
                }
                if (const XmlElement* a = m.child("Animation")) {
                    if (!to_int(a->text, r.animation)) {
                        set_err(err, Err::Xml, at(source, a->line) + "<Animation> \"" + a->text + "\" is not an integer");
                        return false;
                    }
                    r.has_animation = true;
                }
                d.models.push_back(r);
            }
        } else if (c.name == "ChildEffects") {
            for (auto& ch : c.children) {
                if (ch.name != "ChildEffect") {
                    d.unknown["ChildEffects/" + ch.name]++;
                    continue;
                }
                FxChildRef r;
                r.name = ref_name(ch);
                if (r.name.empty()) {
                    ++d.empty_refs;
                    continue;
                }
                if (const std::string* t = ch.attr("trigger")) r.trigger = *t;
                d.children.push_back(r);
            }
        } else if (c.name == "SoundEffects") {
            for (auto& s : c.children) {
                if (s.name != "SoundEffect") {
                    d.unknown["SoundEffects/" + s.name]++;
                    continue;
                }
                std::string n = ref_name(s);
                if (n.empty()) ++d.empty_refs;
                else d.sounds.push_back(n);
            }
        } else {
            d.unknown[c.name]++;
        }
    }
    if (d.name.empty()) {
        set_err(err, Err::Xml, at(source, e.line) + "<VisEffectArtInfo> without <Name>");
        return false;
    }
    return true;
}

template <class T>
bool parse_file_def(const XmlElement& e, const std::string& source, T& d, Error* err) {
    for (auto& c : e.children) {
        if (c.name == "Name") {
            d.name = c.text;
        } else if (c.name == "File") {
            if (d.file.empty()) d.file = c.text;
            else ++d.extra_files;
        }
    }
    if (d.name.empty()) {
        set_err(err, Err::Xml, at(source, e.line) + "<" + e.name + "> without <Name>");
        return false;
    }
    if (d.file.empty()) {
        set_err(err, Err::Xml, at(source, e.line) + "<" + e.name + "> " + d.name + " without <File>");
        return false;
    }
    return true;
}

void stamp(FxSource& s, const std::string& source, int entry, int line) {
    s.file = source;
    s.entry_index = entry;
    s.line = line;
}

}  // namespace

bool parse_viseffects_xml(const char* data, size_t size, const std::string& source, std::vector<VisEffectDef>& out,
                          Error* err) {
    XmlElement root;
    if (!parse_xml(data, size, root, err)) {
        if (err) err->what = source + ": " + err->what;
        return false;
    }
    if (!check_root(root, "VisEffectArtInfos", source, err)) return false;
    int n = 0;
    for (auto& e : root.children) {
        if (e.name != "VisEffectArtInfo") continue;
        VisEffectDef d;
        if (!parse_vis(e, source, d, err)) return false;
        stamp(d.src, source, n++, e.line);
        out.push_back(std::move(d));
    }
    return true;
}

bool parse_fx_particles_xml(const char* data, size_t size, const std::string& source, std::vector<ParticleDef>& out,
                            Error* err) {
    XmlElement root;
    if (!parse_xml(data, size, root, err)) {
        if (err) err->what = source + ": " + err->what;
        return false;
    }
    if (!check_root(root, "VisEffectParticleArtInfos", source, err)) return false;
    int n = 0;
    for (auto& e : root.children) {
        if (e.name != "VisEffectParticleArtInfo") continue;
        ParticleDef d;
        if (!parse_file_def(e, source, d, err)) return false;
        stamp(d.src, source, n++, e.line);
        out.push_back(std::move(d));
    }
    return true;
}

bool parse_fx_models_xml(const char* data, size_t size, const std::string& source, std::vector<ModelDef>& out,
                         Error* err) {
    XmlElement root;
    if (!parse_xml(data, size, root, err)) {
        if (err) err->what = source + ": " + err->what;
        return false;
    }
    if (!check_root(root, "VisEffectModelArtInfos", source, err)) return false;
    int n = 0;
    for (auto& e : root.children) {
        if (e.name != "VisEffectModelArtInfo") continue;
        ModelDef d;
        if (!parse_file_def(e, source, d, err)) return false;
        if (!float_child(e, "fScale", source, d.scale, nullptr, err)) return false;
        if (!float_child(e, "fScrollTime", source, d.scroll_time, nullptr, err)) return false;
        stamp(d.src, source, n++, e.line);
        out.push_back(std::move(d));
    }
    return true;
}

bool parse_event_effects_xml(const char* data, size_t size, const std::string& source,
                             std::vector<EventEffectDef>& out, Error* err) {
    XmlElement root;
    if (!parse_xml(data, size, root, err)) {
        if (err) err->what = source + ": " + err->what;
        return false;
    }
    if (!check_root(root, "EventEffectCollection", source, err)) return false;
    int n = 0;
    for (auto& e : root.children) {
        if (e.name != "EventEffect") continue;
        EventEffectDef d;
        d.event = ref_name(e, "EventName");
        if (d.event.empty()) {
            set_err(err, Err::Xml, at(source, e.line) + "<EventEffect> without <EventName>");
            return false;
        }
        if (const XmlElement* v = e.child("VisEffects"))
            for (auto& x : v->children)
                if (x.name == "VisEffect" && !ref_name(x).empty()) d.vis_effects.push_back(ref_name(x));
        if (const XmlElement* s = e.child("SoundEffects"))
            for (auto& x : s->children)
                if (x.name == "SoundEffect" && !ref_name(x, "ScriptID").empty()) d.sounds.push_back(ref_name(x, "ScriptID"));
        stamp(d.src, source, n++, e.line);
        out.push_back(std::move(d));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------- load list
namespace {
void collect_defines(const XmlElement& e, const std::string& package, std::vector<FxDefineFile>& out) {
    static const struct {
        const char* tag;
        FxKind kind;
    } kTags[] = {{"VisEffectInfoDefines", FxKind::VisEffect},
                 {"VisEffectParticleDefines", FxKind::Particle},
                 {"VisEffectModelDefines", FxKind::Model},
                 {"EventEffectDefines", FxKind::Event}};
    for (auto& c : e.children) {
        bool hit = false;
        for (auto& t : kTags)
            if (c.name == t.tag) {
                hit = true;
                if (!c.text.empty()) out.push_back({t.kind, c.text, package});
            }
        if (!hit) collect_defines(c, package, out);
    }
}
}  // namespace

bool parse_package_fx_defines(const char* data, size_t size, const std::string& package,
                              std::vector<FxDefineFile>& out, Error* err) {
    XmlElement root;
    if (!parse_xml(data, size, root, err)) {
        if (err) err->what = package + ": " + err->what;
        return false;
    }
    if (!check_root(root, "Civ5Package", package, err)) return false;
    collect_defines(root, package, out);
    return true;
}

std::vector<FxDefineFile> fx_load_list(const std::vector<std::vector<FxDefineFile>>& packages_in_order) {
    std::vector<FxDefineFile> r;
    for (int k = 0; k < kFxKinds; ++k) {
        r.push_back({(FxKind)k, kFxBaseFiles[k], ""});
        for (auto& p : packages_in_order)
            for (auto& f : p)
                if ((int)f.kind == k) r.push_back(f);
    }
    return r;
}

// ---------------------------------------------------------------------------------------------- substitution
std::vector<std::string> fx_variables(const std::string& name) {
    std::vector<std::string> r;
    size_t p = 0;
    while ((p = name.find("$(", p)) != std::string::npos) {
        size_t e = name.find(')', p + 2);
        if (e == std::string::npos) break;
        r.push_back(name.substr(p + 2, e - p - 2));
        p = e + 1;
    }
    return r;
}

bool fx_substitute(const std::string& name, const FxSubstitutions& s, std::string& out, Error* err) {
    std::string r;
    size_t p = 0;
    for (;;) {
        size_t q = name.find("$(", p);
        if (q == std::string::npos) {
            r += name.substr(p);
            break;
        }
        r += name.substr(p, q - p);
        size_t e = name.find(')', q + 2);
        if (e == std::string::npos) {
            set_err(err, Err::Xml, "unterminated \"$(\" in effect name " + name);
            return false;
        }
        std::string var = name.substr(q + 2, e - q - 2);
        const std::string* val = nullptr;
        bool known = true, has = false;
        if (var == "TERRAIN") {
            val = &s.terrain;
            has = s.has_terrain;
        } else if (var == "BASETERRAIN") {
            val = &s.base_terrain;
            has = s.has_base_terrain;
        } else if (var == "WEAPON") {
            val = &s.weapon;
            has = s.has_weapon;
        } else {
            known = false;
        }
        if (!known) {
            set_err(err, Err::NotFound, "unknown substitution $(" + var + ") in effect name " + name);
            return false;
        }
        if (!has) {
            set_err(err, Err::NotFound, "no value for $(" + var + ") in effect name " + name);
            return false;
        }
        r += *val;
        p = e + 1;
    }
    out = r;
    return true;
}

// ---------------------------------------------------------------------------------------------- library
void EffectLibrary::clear() {
    vis_ = Table<VisEffectDef>();
    part_ = Table<ParticleDef>();
    model_ = Table<ModelDef>();
    event_ = Table<EventEffectDef>();
    loaded_.clear();
    missing_.clear();
}

namespace {
template <class Tab, class Vec, class KeyFn>
void add_all(Tab& t, Vec& defs, const FxDefineFile& f, const std::string& origin, int file_index, KeyFn key) {
    for (auto& d : defs) {
        d.src.origin = origin;
        d.src.package = f.package;
        d.src.file_index = file_index;
        t.by[key(d)].push_back(t.all.size());
        t.all.push_back(std::move(d));
    }
}
}  // namespace

bool EffectLibrary::load(FileProvider& files, const std::vector<FxDefineFile>& list, Error* err) {
    clear();
    int index[kFxKinds] = {0, 0, 0, 0};
    for (auto& f : list) {
        Bytes b;
        std::string origin;
        if (!files.read(basename_lower(f.name), b, &origin)) {
            missing_.push_back(f);
            continue;
        }
        const char* p = (const char*)b.data();
        bool ok = true;
        int fi = index[(int)f.kind]++;
        switch (f.kind) {
        case FxKind::VisEffect: {
            std::vector<VisEffectDef> v;
            ok = parse_viseffects_xml(p, b.size(), f.name, v, err);
            if (ok) add_all(vis_, v, f, origin, fi, [](const VisEffectDef& d) { return d.name; });
            break;
        }
        case FxKind::Particle: {
            std::vector<ParticleDef> v;
            ok = parse_fx_particles_xml(p, b.size(), f.name, v, err);
            if (ok) add_all(part_, v, f, origin, fi, [](const ParticleDef& d) { return d.name; });
            break;
        }
        case FxKind::Model: {
            std::vector<ModelDef> v;
            ok = parse_fx_models_xml(p, b.size(), f.name, v, err);
            if (ok) add_all(model_, v, f, origin, fi, [](const ModelDef& d) { return d.name; });
            break;
        }
        case FxKind::Event: {
            std::vector<EventEffectDef> v;
            ok = parse_event_effects_xml(p, b.size(), f.name, v, err);
            if (ok) add_all(event_, v, f, origin, fi, [](const EventEffectDef& d) { return d.event; });
            break;
        }
        }
        if (!ok) {
            clear();
            return false;
        }
        loaded_.push_back(f);
    }
    return true;
}

const VisEffectDef* EffectLibrary::vis_effect(const std::string& n) const { return vis_.get(n); }
const ParticleDef* EffectLibrary::particle(const std::string& n) const { return part_.get(n); }
const ModelDef* EffectLibrary::model(const std::string& n) const { return model_.get(n); }
const EventEffectDef* EffectLibrary::event(const std::string& n) const { return event_.get(n); }

namespace {
template <class Tab>
std::vector<std::string> keys(const Tab& t) {
    std::vector<std::string> r;
    for (auto& kv : t.by) r.push_back(kv.first);
    std::sort(r.begin(), r.end());
    return r;
}
template <class Tab>
std::vector<FxSource> sources(const Tab& t, const std::string& n) {
    std::vector<FxSource> r;
    auto it = t.by.find(n);
    if (it != t.by.end())
        for (size_t i : it->second) r.push_back(t.all[i].src);
    return r;
}
template <class Tab>
std::vector<std::string> dups(const Tab& t) {
    std::vector<std::string> r;
    for (auto& kv : t.by)
        if (kv.second.size() > 1) r.push_back(kv.first);
    std::sort(r.begin(), r.end());
    return r;
}
}  // namespace

size_t EffectLibrary::distinct(FxKind k) const {
    switch (k) {
    case FxKind::VisEffect: return vis_.by.size();
    case FxKind::Particle: return part_.by.size();
    case FxKind::Model: return model_.by.size();
    case FxKind::Event: return event_.by.size();
    }
    return 0;
}

size_t EffectLibrary::entries(FxKind k) const {
    switch (k) {
    case FxKind::VisEffect: return vis_.all.size();
    case FxKind::Particle: return part_.all.size();
    case FxKind::Model: return model_.all.size();
    case FxKind::Event: return event_.all.size();
    }
    return 0;
}

std::vector<std::string> EffectLibrary::names(FxKind k) const {
    switch (k) {
    case FxKind::VisEffect: return keys(vis_);
    case FxKind::Particle: return keys(part_);
    case FxKind::Model: return keys(model_);
    case FxKind::Event: return keys(event_);
    }
    return {};
}

std::vector<FxSource> EffectLibrary::definitions(FxKind k, const std::string& n) const {
    switch (k) {
    case FxKind::VisEffect: return sources(vis_, n);
    case FxKind::Particle: return sources(part_, n);
    case FxKind::Model: return sources(model_, n);
    case FxKind::Event: return sources(event_, n);
    }
    return {};
}

std::vector<std::string> EffectLibrary::duplicated(FxKind k) const {
    switch (k) {
    case FxKind::VisEffect: return dups(vis_);
    case FxKind::Particle: return dups(part_);
    case FxKind::Model: return dups(model_);
    case FxKind::Event: return dups(event_);
    }
    return {};
}

namespace {
FileRef lookup(FileProvider& files, const std::string& written) {
    FileRef r;
    r.name = written;
    std::string key = basename_lower(written);
    r.found = !key.empty() && files.exists(key, &r.origin);
    if (r.found) {
        std::vector<std::string> d = files.duplicates(key);
        for (size_t i = 1; i < d.size(); ++i) r.others.push_back(d[i]);
    }
    return r;
}
}  // namespace

bool EffectLibrary::resolve(FileProvider& files, const std::string& name, const FxSubstitutions& subs,
                            ResolvedEffect& out, Error* err) const {
    out = ResolvedEffect();
    out.requested = name;
    if (!fx_substitute(name, subs, out.name, err)) return false;
    out.def = vis_effect(out.name);
    if (!out.def) {
        set_err(err, Err::NotFound, "unknown effect name: " + out.name);
        return false;
    }
    const VisEffectDef& d = *out.def;
    for (auto& pn : d.particles) {
        ResolvedFxParticle p;
        p.def = particle(pn);
        if (!p.def) {
            set_err(err, Err::NotFound, "effect " + d.name + ": unknown particle effect " + pn);
            return false;
        }
        p.file = lookup(files, p.def->file);
        if (!p.file.found) out.problems.push_back("particle file not found: " + p.def->file + " (" + pn + ")");
        out.particles.push_back(p);
    }
    for (auto& mr : d.models) {
        ResolvedFxModel m;
        m.ref = mr;
        m.def = model(mr.name);
        if (!m.def) {
            set_err(err, Err::NotFound, "effect " + d.name + ": unknown model effect " + mr.name);
            return false;
        }
        m.file = lookup(files, m.def->file);
        if (!m.file.found) out.problems.push_back("model file not found: " + m.def->file + " (" + mr.name + ")");
        out.models.push_back(m);
    }
    for (auto& cr : d.children) {
        ResolvedFxChild c;
        c.ref = cr;
        if (!fx_substitute(cr.name, subs, c.name, err)) {
            if (err) err->what = "effect " + d.name + ": child: " + err->what;
            return false;
        }
        c.def = vis_effect(c.name);
        if (!c.def) {
            set_err(err, Err::NotFound, "effect " + d.name + ": unknown child effect " + c.name);
            return false;
        }
        out.children.push_back(c);
    }
    return true;
}

namespace {
bool is_name_char(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// pieces: literal, var, literal, var, ..., literal (vars are wildcards over [A-Z0-9_]*)
bool match_from(const std::vector<std::string>& lit, size_t i, const std::string& s, size_t pos) {
    if (i + 1 == lit.size()) return s.size() - pos == lit[i].size() && s.compare(pos, std::string::npos, lit[i]) == 0;
    if (s.compare(pos, lit[i].size(), lit[i]) != 0) return false;
    size_t p = pos + lit[i].size();
    for (size_t q = p;; ++q) {
        if (match_from(lit, i + 1, s, q)) return true;
        if (q >= s.size() || !is_name_char(s[q])) return false;
    }
}
}  // namespace

std::vector<std::string> EffectLibrary::variants(const std::string& pattern) const {
    std::vector<std::string> lit;
    size_t p = 0;
    for (;;) {
        size_t q = pattern.find("$(", p);
        size_t e = q == std::string::npos ? std::string::npos : pattern.find(')', q + 2);
        if (q == std::string::npos || e == std::string::npos) {
            lit.push_back(pattern.substr(p));
            break;
        }
        lit.push_back(pattern.substr(p, q - p));
        p = e + 1;
    }
    std::vector<std::string> r;
    for (auto& kv : vis_.by)
        if (match_from(lit, 0, kv.first, 0)) r.push_back(kv.first);
    std::sort(r.begin(), r.end());
    return r;
}

float EffectLibrary::duration(const VisEffectDef& d, float animation_seconds) {
    bool animated = false;
    for (auto& m : d.models) animated = animated || m.has_animation;
    if (animated && animation_seconds >= 0) return animation_seconds * d.base_duration_scale;
    return d.base_duration * d.base_duration_scale;
}

std::string format_resolved_effect(const ResolvedEffect& r) {
    std::ostringstream o;
    o << "effect " << r.requested;
    if (r.name != r.requested) o << " -> " << r.name;
    o << "\n";
    if (r.def) {
        const VisEffectDef& d = *r.def;
        o << "  from " << d.src.origin << " (entry " << d.src.entry_index << ")\n";
        o << "  transform t(" << d.translation.x << ", " << d.translation.y << ", " << d.translation.z << ") r("
          << d.rotation_deg.x << ", " << d.rotation_deg.y << ", " << d.rotation_deg.z << ") s " << d.scale
          << "; duration " << d.base_duration << " x " << d.base_duration_scale << "; FOWMask " << d.fow_mask << "\n";
    }
    for (auto& p : r.particles)
        o << "  particle " << (p.def ? p.def->name : "?") << " -> " << basename_lower(p.file.name) << " "
          << (p.file.found ? p.file.origin : "MISSING") << "\n";
    for (auto& m : r.models)
        o << "  model " << m.ref.name << (m.ref.has_animation ? " anim " + std::to_string(m.ref.animation) : "")
          << " -> " << basename_lower(m.file.name) << " " << (m.file.found ? m.file.origin : "MISSING") << "\n";
    for (auto& c : r.children) o << "  child " << c.ref.trigger << " " << c.name << "\n";
    for (auto& p : r.problems) o << "  problem: " << p << "\n";
    return o.str();
}

}  // namespace c5m
