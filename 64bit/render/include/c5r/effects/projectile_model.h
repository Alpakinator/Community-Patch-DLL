// c5r/effects/projectile_model.h - option A of ticket 4792 (lead-0042 #7986: behind a flag, off by default): the server
// draws a projectile's rigid .gr2 model itself, through the unit renderer's own path for rigid meshes (material 87,
// cb0 world rows, units::ModelCache and UnitRenderer::draw: D85, reuse the engine's own functionality). Product
// default B (ModelMode::EffectOnly) draws nothing here: the EXE's VisEffect draws model and trail (stage A).
//
// Named difference [I]: the EXE draws projectile models with its SimpleShader set (material 71,
// effects-formats.md 5); the unit renderer's material 87 program stands in for it until a capture of a projectile draw
// gives SimpleShader's inputs. The model files are read from the player's install at run time (D83).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "c5models/effect_xml.h"
#include "c5r/effects/projectile.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/draw.h"
#include "c5r/units/units.h"

namespace c5r { namespace effects {

// The parts of one VisEffect that the server would draw: one member-like entry per model reference, with the scale the
// model is drawn at (ModelDef::fScale) and the effect definition's own transform (translation, rotation in degrees,
// scale; the order of the rotations is [I]: x, then y, then z).
struct ProjectileModelArt {
    std::string effect;
    std::vector<units::MemberArt> parts;   // MemberArt::member = the model name, files = resolve_fxsxml of its .fxsxml
    M4 local;                              // the VisEffect's Transform as a matrix (identity when it has none)
    std::vector<std::string> problems;     // files not found (not fatal)
};
// Resolves `effect` (already substituted) to its models. False with `why` when the effect is unknown or has no model,
// or a model's .fxsxml is missing or malformed. `subs` fill the children's variables for the resolver (null: DIRT for
// $(TERRAIN) and $(BASETERRAIN), "" for $(WEAPON); the models do not depend on them).
bool resolveProjectileModel(const c5m::EffectLibrary& lib, c5m::FileProvider& files, const std::string& effect,
                            ProjectileModelArt& out, std::string* why, const c5m::FxSubstitutions* subs = nullptr);
// The install's effect definitions (effects-formats.md 14.4): the base files, then every package's define files in the
// order of the packages' folder names (case-insensitive; 14.2 item 3, the EXE's find order on NTFS [I]), read through
// `files` (the GameFiles provider). False with `err` when a .Civ5Pkg does not parse or the library does not load.
bool loadInstallEffects(const gamefs::GameFiles& gf, c5m::FileProvider& files, c5m::EffectLibrary& lib,
                        std::string& err);
// The definition's transform (scale, rotations x-y-z in degrees, translation) in the row-vector convention.
M4 effectLocal(const c5m::VisEffectDef& def);

// Appends one UnitPlan per instance to `plan` (player -2, id = the flight's handle; status Ok) with a static draw per
// rigid mesh of each part (world rows = (local x instance world) as [R | T]; colour white, opaque). Meshes that are not
// rigid (material 89 / 90) are skipped and counted in `skipped_meshes`. `arts` by effect name; an instance whose effect
// has no entry is skipped and counted in `skipped_instances`. Returns the first ModelCache failure, else Ok.
units::UnitStatus appendProjectileDraws(units::ModelCache& cache, const std::map<std::string, ProjectileModelArt>& arts,
                                        const std::vector<ModelInstance>& instances, units::FramePlan& plan,
                                        size_t* skipped_meshes = nullptr, size_t* skipped_instances = nullptr);

}}  // namespace c5r::effects
