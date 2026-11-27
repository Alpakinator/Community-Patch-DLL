// c5models/effect_xml.h - the effect definitions reader (E1 of C4b; ticket 4786, epic 0042, 2026-11-20).
// Own code, GPL-3.0-or-later like the rest of the repository. Written from the knowledge base
// (research/05-graphics-composition/effects-formats.md 1-3 and section 14 "E1 as built"); nothing of the game's
// files is held here (D83): the definitions are read from the player's install at run time through a FileProvider.
//
// The four XML kinds (effects-formats.md 1):
//   VisEffectArtInfos          ART_DEF_VEFFECT_*  one effect: transform, duration, FOWMask, particle / model / child /
//                                                 sound lists
//   VisEffectParticleArtInfos  ART_DEF_PEFFECT_*  -> File, a .psb found by bare lower-case name
//   VisEffectModelArtInfos     ART_DEF_MEFFECT_*  -> File, an .fxsxml found by bare lower-case name; fScale, fScrollTime
//   EventEffectCollection      engine event name  -> VisEffect names and sound script ids
//
// Load order and duplicates (effects-formats.md 14.2): per kind the base file of EffectData.fpk first, then every
// package's <VisEffectInfoDefines> / <VisEffectParticleDefines> / <VisEffectModelDefines> / <EventEffectDefines>
// in package order, each package's entries in document order. For a name defined more than once the LAST
// definition in that order is in force (the EXE records (file, entry) per name and overwrites it on every later
// occurrence, then loads only the recorded one) [C for VisEffects, particles and models; I for events].
//
// Substitutions: $(TERRAIN), $(BASETERRAIN) and $(WEAPON) in a name are replaced by values the caller supplies
// (the server knows the plot and the weapon); any other $(...) is refused. The library never picks the values.
#pragma once

#include "c5models/art.h"   // FileProvider, FileRef, XmlElement, Bytes, Err, Error

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace c5m {

enum class FxKind { VisEffect = 0, Particle = 1, Model = 2, Event = 3 };
constexpr int kFxKinds = 4;
const char* fx_kind_name(FxKind k);   // "VisEffect", "Particle", "Model", "Event"

// Where a definition came from: the file (bare name as listed), its origin in the provider, the package that named
// it ("" for the base file), the file's position in the kind's load list, the entry's position in the file, the line.
struct FxSource {
    std::string file;
    std::string origin;
    std::string package;
    int file_index = -1;
    int entry_index = -1;
    int line = 0;
};

struct FxVec3 {
    float x = 0, y = 0, z = 0;
};

struct FxModelRef {
    std::string name;                  // ART_DEF_MEFFECT_*
    bool has_animation = false;        // <Animation> present (15 references, all 1000, effects-formats.md 1)
    int animation = 0;
};
struct FxChildRef {
    std::string name;                  // a VisEffect name, may carry $(TERRAIN)
    std::string trigger;               // the trigger attribute ("ProjectileEnd" on every child of the install)
};

struct VisEffectDef {
    std::string name;
    FxVec3 translation;
    FxVec3 rotation_deg;               // degrees, as the file's comment says
    float scale = 1;
    bool has_transform = false;
    float base_duration = 0;
    float base_duration_scale = 1;
    int fow_mask = 2;                  // 1 revealed only, 2 visible only (default), 3 both
    bool fow_mask_written = false;
    float movement_speed = 0;
    bool has_movement_speed = false;
    std::vector<std::string> particles;   // ART_DEF_PEFFECT_* in document order (empty <Name> skipped)
    std::vector<FxModelRef> models;
    std::vector<FxChildRef> children;
    std::vector<std::string> sounds;      // <SoundEffects><SoundEffect><Name> (audio stays the EXE's, D80)
    int empty_refs = 0;                   // <ParticleEffect> / <ModelEffect> / <ChildEffect> / <SoundEffect> without a
                                          // name: skipped (3 in the base file, effects-formats.md 14.1)
    std::map<std::string, int> unknown;   // element name -> count, for elements not listed above
    FxSource src;
};
struct ParticleDef {
    std::string name;
    std::string file;                  // the first <File> as written (an art-tree path); looked up by basename_lower()
    int extra_files = 0;               // further <File> elements (one entry of the base file repeats its File)
    FxSource src;
};
struct ModelDef {
    std::string name;
    std::string file;                  // the first <File>: an .fxsxml path as written
    int extra_files = 0;
    float scale = 1;                   // fScale
    float scroll_time = 0;             // fScrollTime
    FxSource src;
};
struct EventEffectDef {
    std::string event;                 // <EventName>
    std::vector<std::string> vis_effects;
    std::vector<std::string> sounds;   // <SoundEffects><SoundEffect><ScriptID>
    FxSource src;
};

// One file's definitions. The root must be the kind's root element; Err::Xml (with source and line) for a wrong
// root, a definition without <Name> / <EventName>, a particle or model definition without <File>, a number that does
// not parse (Translation / Rotation attributes, Scale, fBaseDuration, fBaseDurationScale, FOWMask, fMovementSpeed,
// fScale, fScrollTime, Animation) or a FOWMask outside 1..3. A reference element without a name is skipped and
// counted (VisEffectDef::empty_refs). Entries are appended in document order with src.entry_index counting them from
// 0; src.file is `source`.
bool parse_viseffects_xml(const char* data, size_t size, const std::string& source, std::vector<VisEffectDef>& out,
                          Error* err);
bool parse_fx_particles_xml(const char* data, size_t size, const std::string& source, std::vector<ParticleDef>& out,
                            Error* err);
bool parse_fx_models_xml(const char* data, size_t size, const std::string& source, std::vector<ModelDef>& out,
                         Error* err);
bool parse_event_effects_xml(const char* data, size_t size, const std::string& source,
                             std::vector<EventEffectDef>& out, Error* err);

// ---------------------------------------------------------------------------------------------- load list
struct FxDefineFile {
    FxKind kind = FxKind::VisEffect;
    std::string name;                  // bare file name as written ("Civ5ArtDefines_VisEffects_Expansion2.xml")
    std::string package;               // "" for a base file, else the package's label
};
// The four base files (in EffectData.fpk; the EXE names them as Assets\Effect\Civ5ArtDefines_*.xml and finds them
// by bare name), one per kind, in FxKind order.
extern const char* const kFxBaseFiles[kFxKinds];

// Every effect define element of one .Civ5Pkg, in document order, at any depth (the install has them at the root
// and inside <Gameplay>). Tags: VisEffectInfoDefines, VisEffectParticleDefines, VisEffectModelDefines,
// EventEffectDefines; an empty element is skipped. Root must be <Civ5Package>, Err::Xml otherwise.
bool parse_package_fx_defines(const char* data, size_t size, const std::string& package,
                              std::vector<FxDefineFile>& out, Error* err);

// Per kind: the base file, then the packages' files in the order given (packages in load order, each package's
// files in document order). The caller orders the packages (effects-formats.md 14.2: the EXE keeps them in the
// order it finds the *.Civ5Pkg files).
std::vector<FxDefineFile> fx_load_list(const std::vector<std::vector<FxDefineFile>>& packages_in_order);

// ---------------------------------------------------------------------------------------------- substitution
struct FxSubstitutions {
    // A value that is not set leaves the variable unresolvable; set to "" it substitutes the empty string (the base
    // file defines ART_DEF_VEFFECT_ARMOR_DEATH_SM_ and _MD_ for an empty $(WEAPON)).
    bool has_terrain = false, has_base_terrain = false, has_weapon = false;
    std::string terrain, base_terrain, weapon;
    FxSubstitutions& set_terrain(const std::string& v) { terrain = v; has_terrain = true; return *this; }
    FxSubstitutions& set_base_terrain(const std::string& v) { base_terrain = v; has_base_terrain = true; return *this; }
    FxSubstitutions& set_weapon(const std::string& v) { weapon = v; has_weapon = true; return *this; }
};
// The variables a name uses, in order ("TERRAIN", "BASETERRAIN", "WEAPON", or any other name as written).
std::vector<std::string> fx_variables(const std::string& name);
// Replaces every $(TERRAIN) / $(BASETERRAIN) / $(WEAPON). Err::NotFound naming the variable and the name for an
// unknown variable or one without a value; Err::Xml for an unterminated "$(".
bool fx_substitute(const std::string& name, const FxSubstitutions& subs, std::string& out, Error* err);

// ---------------------------------------------------------------------------------------------- library
struct ResolvedFxParticle {
    const ParticleDef* def = nullptr;
    FileRef file;                      // the .psb by bare name
};
struct ResolvedFxModel {
    FxModelRef ref;
    const ModelDef* def = nullptr;
    FileRef file;                      // the .fxsxml by bare name (resolve_fxsxml() takes it from there)
};
struct ResolvedFxChild {
    FxChildRef ref;                    // as written
    std::string name;                  // after substitution
    const VisEffectDef* def = nullptr;
};
struct ResolvedEffect {
    std::string requested;             // as asked (may carry variables)
    std::string name;                  // after substitution
    const VisEffectDef* def = nullptr;
    std::vector<ResolvedFxParticle> particles;
    std::vector<ResolvedFxModel> models;
    std::vector<ResolvedFxChild> children;
    std::vector<std::string> problems; // files that the provider does not hold (not fatal)
    bool complete() const { return problems.empty(); }
};

class EffectLibrary {
public:
    // Reads every file of the list through the provider, in list order. A listed file the provider does not hold is
    // recorded in missing_files() and skipped (two packages of the install name files that do not exist). A
    // malformed file -> Err::Xml naming it; the library is then unusable (cleared).
    bool load(FileProvider& files, const std::vector<FxDefineFile>& list, Error* err);
    void clear();

    // The definition in force for a name (the last one read), or nullptr.
    const VisEffectDef* vis_effect(const std::string& name) const;
    const ParticleDef* particle(const std::string& name) const;
    const ModelDef* model(const std::string& name) const;
    const EventEffectDef* event(const std::string& event_name) const;

    size_t distinct(FxKind k) const;       // names with a definition
    size_t entries(FxKind k) const;        // definitions read, duplicates included
    std::vector<std::string> names(FxKind k) const;   // sorted
    // Every definition of a name, in load order (the last is in force); empty if the name is unknown.
    std::vector<FxSource> definitions(FxKind k, const std::string& name) const;
    // Names defined more than once, sorted.
    std::vector<std::string> duplicated(FxKind k) const;

    const std::vector<FxDefineFile>& loaded_files() const { return loaded_; }
    const std::vector<FxDefineFile>& missing_files() const { return missing_; }

    // Substitutes, finds the VisEffect and its particle, model and child definitions, and looks up the .psb and
    // .fxsxml files by bare lower-case name. Err::NotFound naming it for: a VisEffect name (after substitution) that
    // is not defined, a particle or model reference without a definition, a child (after the same substitution)
    // that is not defined; the substitution errors of fx_substitute(). A missing .psb / .fxsxml file is a problem
    // in `out`, not an error.
    bool resolve(FileProvider& files, const std::string& name, const FxSubstitutions& subs, ResolvedEffect& out,
                 Error* err) const;

    // The VisEffect names in force that a name with variables can become: each variable matches any run of
    // [A-Z0-9_] (also empty). A name without variables gives itself if defined. Sorted.
    std::vector<std::string> variants(const std::string& pattern) const;

    // The effect's duration in seconds by the rule of the base file's comment: with a model reference that has an
    // <Animation>, the animation's length times fBaseDurationScale; else fBaseDuration times fBaseDurationScale.
    // animation_seconds < 0 means "not known": the second rule is used.
    static float duration(const VisEffectDef& d, float animation_seconds);

private:
    template <class T>
    struct Table {
        std::vector<T> all;                                        // every definition, load order
        std::unordered_map<std::string, std::vector<size_t>> by;   // name -> indices into all, load order
        const T* get(const std::string& n) const {
            auto it = by.find(n);
            return it == by.end() ? nullptr : &all[it->second.back()];
        }
    };
    Table<VisEffectDef> vis_;
    Table<ParticleDef> part_;
    Table<ModelDef> model_;
    Table<EventEffectDef> event_;
    std::vector<FxDefineFile> loaded_, missing_;
};

// Multi-line text form of a resolved effect (tests and logs).
std::string format_resolved_effect(const ResolvedEffect& r);

}  // namespace c5m
