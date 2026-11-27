# Effects: the files and formats of particles, projectiles and combat effects, and what the server needs to draw them

Issue 0320, 2026-11-15, implementer impl-B-0320 (account B), branch `task-0320-effects-formats`. Research spike C4 of
the Architect's increment 3 sheet (`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md`
sections 5 and 7, item N8 of 1a). Knowledge base only (D83): nothing of the game's files or the EXE is committed; the
evidence folder holds our own scripts and their outputs (counts and names). Marks: `[C]` read in the files, the
database, the DLL source or the EXE's bytes; `[L]` seen live (here only by citing earlier issues); `[I]` inferred.

Inputs: the Goldberg install copy of instance c5h-b (`%USERPROFILE%/c5-instances/c5h-b/install`, read only), its
`CivilizationV_DX11.exe` (SHA-256 `04cfdc71...`, the analysed one), a copy of its `Civ5DebugDatabase.db` (cp+vp+eui,
2026-11-15 01:55, copy SHA-256 `90075fa4...`), the MODS folder of that instance (Community Patch, Vox Populi, EUI
compatibility files), and the 3100 extraction kept on D: for the unit `.ftsxml` examples. Scripts and outputs:
`evidence/0320/` (section 13).

## 0. Results in short

1. **One effect = one `VisEffectArtInfo`** (538 of them, `ART_DEF_VEFFECT_*`) [C]. It has a transform, a duration,
   an optional fog rule and up to four lists: particle effects (`ART_DEF_PEFFECT_*` -> one `.psb` file each), model
   effects (`ART_DEF_MEFFECT_*` -> an `.fxsxml` -> a Granny `.gr2` with FX shaders), child effects fired at the end of
   a projectile's flight, and sounds. 477 of the 538 have particles, 101 have models (42 have both), 40 have a child.
2. **Particles are Fork Particle's** (third-party middleware, statically linked like Granny) [C for the names in the
   EXE: `FForkSDK.cpp`, `FForkPSystem.cpp`, `FForkPRenderBuffer.cpp`, `ForkParticles`, five `Fork*Shader` sets; I for
   the vendor]. Their files are binary `.psb`: 384 in the install, 2.77 MB, each a 64-byte header and a chain of
   1 to 18 emitter blocks (1,232 emitters). **The chain layout holds for 384 of 384 files** [C]; the meaning of the
   parameter bytes inside an emitter block is **not** known and is the main cost of drawing particles in the server.
3. **No database table holds effects** [C]: the definitions are four XML kinds in `EffectData.fpk` and 37 loose DLC
   files. The database only names effects: `ArtDefine_UnitMemberCombatWeapons.HitEffect`/`ID` (impacts, projectile
   effects), the projectile flight columns, `Eras.CityBombardEffectTag`, and the terrain and feature
   `EffectTypeTag` that fill the `$(TERRAIN)` substitution. Vox Populi, CP and EUI add no effect file [C].
4. **Six ways an effect is started** (section 6): timed triggers in animations (7,049 Effect rows and 603 projectile
   Transfer rows in 673 `.ftsxml`), landmark states, combat (hit effects, projectile ends, city bombard), engine events
   (`EventEffects`), the DLL's `GameplayDoFX`, and names the EXE starts itself (city damage smoke, paradrop). 392
   distinct names are used by triggers; **386 resolve exactly, 4 through a substitution, 2 are dangling** [C].
5. **What the server needs**: an XML reader with name resolution and substitutions, the `.psb` reader and a
   re-implementation of Fork's emitter simulation and drawing (described in this knowledge base first, D83), FX model
   drawing on the model reader and the unit pose layer, a projectile flight model, and trigger input from the unit
   controller (U5), the combat choreography (C3), landmark states and the server's fog (FOWMask).
6. **Size of C4b: 76-142 h** [I] (section 11), above the sheet's guess of 25-50 h, because the Fork parameter
   semantics and simulation are a research-and-rewrite job of their own (32-62 h of it). A look-alike subset without
   Fork particles (models, projectiles, model-effect smoke) is 25-45 h and is a fallback only (D66). The cheapest
   route stays the EXE's own Particle, Projectile and Combat systems over the server's picture, measured by 0318 with
   the EXE drawing units [L]; whether that holds once the server draws the units is not known (section 12).
7. **Stage A's entry** (ticket 4798, 2026-11-20) [C]: the EXE starts, moves and stops an effect through records on its
   4 MB queue that the Particle system listens to: a world matrix, a scale, the name's CRC-32 and a caller-chosen owner
   key; the same key and name again moves the live effect. No model is needed (premises B1 and B2 hold); section 8.1.
   **The shell's player** (ticket 4727, 2026-11-20, section 15.7) posts them from the proxy's frame hook through the
   EXE's own three record allocators (build-table rows `effect_alloc_*`), offline-tested on 0557's 19 recorded fights;
   it refuses build 26577625 until the handlers' layout is read there.

## 1. The definition chain [C]

```
trigger (timed trigger / landmark state / combat / event / DLL FX call / EXE name)
  -> VisEffectArtInfo  ART_DEF_VEFFECT_*   (Civ5ArtDefines_VisEffects*.xml)
       Transform (Translation x y z, Rotation x y z in degrees, Scale), fBaseDuration, fBaseDurationScale,
       FOWMask (optional), fMovementSpeed (optional, 11 effects)
       ParticleEffects -> VisEffectParticleArtInfo ART_DEF_PEFFECT_* -> File = a .psb   (..._VisEffectParticles*.xml)
       ModelEffects    -> VisEffectModelArtInfo    ART_DEF_MEFFECT_* -> File = an .fxsxml, fScale, fScrollTime
                          (+ optional <Animation> code on the reference, 1000 in 15 cases)   (..._VisEffectModels*.xml)
       ChildEffects    -> ChildEffect trigger="ProjectileEnd" -> another VisEffect (40 effects, the only trigger value)
       SoundEffects    -> audio script names (80 effects; audio stays in the EXE, D80)
```

- **Duration rule**, from the comment in the base file: with a model and an animation, the effect lasts the
  animation's length times `fBaseDurationScale`; otherwise `fBaseDuration` x `fBaseDurationScale`.
- **FOWMask**, from the same comment: 1 = drawn only in revealed fog, 2 = only in visible plots (the default),
  3 = both. Used: default 524, 3 thirteen times, 2 once; the schema allows only 2 and 3.
- **Files are found by bare file name.** The `File` values carry art-tree paths (`Assets\Effects\Askia\...`) that do
  not exist in the install; every one of the 235 particle files and 66 model files exists by bare lower-case name in
  an archive [C], as for landmarks (`landmarks-and-models.md` 1). The `.psb` texture slots are found the same way,
  112 of 122 distinct names (section 4; *2026-11-23, 4787: was "112 of 123", one of the 123 was a false hit, 16.2*).
- **The fourth kind, `EventEffectCollection`** (`Civ5ArtDefines_EventEffects*.xml`, 8 files) maps an engine event
  name to VisEffects and sounds: `SerialEventUnitCreated` -> `ART_DEF_VEFFECT_TEST`,
  `SerialEventUnitDestroyedInCombat` -> `ART_DEF_VEFFECT_MULTIPLAYER_UNIT_DEATH_01`, `GameplayFX` ->
  `ART_DEF_VEFFECT_LOOK_AT`, `NaturalWonderRevealed` (sound only). Four event names over all 8 files.
- **A schema ships with it** (`civ5artdefines_viseffects.xsd`, EffectData.fpk) and matches the files.
- **Example, the archer's arrow:** `ART_DEF_VEFFECT_PROJ_ARROW_ARCHER` = model `ART_DEF_MEFFECT_ARROW_ARCHER`
  (`proj_arrow_archer.fxsxml` -> `proj_arrow_archer.gr2`, 48 vertices, `SimpleShader`, the archer's diffuse texture)
  + particle `ART_DEF_PEFFECT_STR_ARROW_TRAIL_01` (a `.psb` trail) + child on `ProjectileEnd`
  `ART_DEF_VEFFECT_PROJ_ARROW_IMPACT`; `..._ARCHER_HAND` is the same model alone, the arrow held in the hand.

## 2. Where the files are [C]

| Archive or folder | Effect content |
|---|---|
| `Resource/Common/EffectData.fpk` (3.2 MB, 383 entries) | the four base definition files and the `.xsd`; 319 `.psb` (2.41 MB); 25 `.fxsxml` + 30 `.gr2` (model effects: city damage states, pillaged smoke, god ray, oil, nuke domes, propellers, reef caustics, uranium glow); 2 `.ftsxml` (nuke domes); also `fluidfowsettings.xml`, `wavesettings.xml` |
| other `Resource/Common/*Models.fpk` | 65 more `.psb` (Expansion2 30, Expansion1 21, Upgrade1 6, Korea 3, ConquestDeluxe 2, Polynesia 2, Babylonian 1); the projectile models (21 of the 66 model files are in UnitModels) |
| `Resource/DX9/EffectTextures.fpk` (190 `.dds`, 15.2 MB), `Expansion1EffectsTextures.fpk` (11), `Expansion2EffectsTextures.fpk` (5) | particle and FX-model textures: 206 files, 17.0 MB; DXT5 150, DXT1 30, 32-bit uncompressed 25, DXT3 1; largest side 4 to 1024 px, mostly 128-256 |
| `Assets/DLC/{DLC_01,03,04,05,06,07,Expansion,Expansion2,Shared}/Effects/` | 37 loose definition files (Expansion2 6, DLC_07 3, the others 4 each); 145 of the 538 VisEffects have their last definition there (Expansion2 109, Shared 13, DLC_05 11, DLC_04 5, DLC_07 4, DLC_03 3) |
| MODS (CP, VP, EUI) | none [C: no definition file, no `.psb`] |
| *(added 2026-11-20, 4786)* `Resource/Common/SpanishModels.fpk`, `BabylonianModels.fpk` | 4 definition files each (`*_DLC_02.xml`, `*_DLC_Deluxe.xml`): the files that `SpainInca.Civ5Pkg` and `Babylon.Civ5Pkg` name, found by bare name; 0320's walk of the loose folders missed them (section 14.1) |

Counts per archive: `evidence/0320/archives_with_effects.txt`.

## 3. The definition files in numbers [C] (`evidence/0320/fx_defs.out.txt`)

- 41 files: 11 VisEffect, 11 particle, 11 model, 8 event. 538 VisEffects, 235 particle definitions, 66 model
  definitions (all `.fxsxml`; every one carries `fScale` and `fScrollTime`, nothing else).
- **Duplicate names** *(corrected 2026-11-20, ticket 4786, section 14.1-14.2; replaced "92 VisEffects, 9 particle and
  5 model definitions are defined twice ... load order not read [I: the later, expansion copy]")*: over the files the
  EXE loads there are 103 / 10 / 7 duplicate entries (84 / 10 / 6 names defined more than once), because six more
  definition files live inside `SpanishModels.fpk` and `BabylonianModels.fpk` (DLC_02, DLC_Deluxe), which 0320 did
  not see. The last definition in load order wins [C]; the load order is in 14.2.
- Particle effects per VisEffect: none 61, one 409, two 68. Every particle and model reference resolves to a
  definition; 9 child references do not resolve literally, and the six printed all carry `$(TERRAIN)`.
- 232 distinct `.psb` files are reached from some VisEffect (of 384 in the install).
- 148 VisEffects are named by no trigger, no `HitEffect` and no event: they are children, terrain variants of
  impacts (`*_IMPACT_DIRT` etc.), the bombard tags and names the EXE starts itself (section 6).

## 4. The `.psb` particle file [C for the layout over all 384 files; I where marked]

Checked by `evidence/0320/psb_survey.py` (written from hex dumps; no outside source): **384 of 384 files** parse
with this layout to their exact end.

| Offset | Size | Content |
|---|---|---|
| 0x00 | 4 | `PSB\0` |
| 0x04 | 4 | version, 100 in every file |
| 0x08 | 4 | payload size = file size - 0x40 |
| 0x0C | 4 | 0x40, the header size |
| 0x10 | 4 | 16 in every file (meaning not known) |
| 0x14 | 0x2C | zero |
| 0x40 | ... | emitter blocks, one after another, up to the end of the file |

An emitter block:

- `+0x000` the emitter's name, 256 bytes, zero-padded (`FXP_Wood_Shards_sm_01`, `FXP_Ring`, `FXP_FireGround_01`).
- `+0x100` u32 the block's size; the next block starts there. 49 distinct sizes, from 1,488 bytes (746 of the 1,232
  emitters) to 61,808 *(corrected 2026-11-23, ticket 4787, 16.1; replaced "to 19,632", which is the most frequent large
  size, 12 blocks, not the largest)*.
- `+0x104` u32 272 in every block (meaning not known).
- After that, a run of 32-bit floats and integers (values like 1.0, 0.3, 150.0, 30.0 are visible in dumps): the
  emitter's parameters. **Their meaning is not decoded** [I: rates, lifetimes, velocities, sizes, colours and their
  curves over the particle's life, as any emitter has].
- String slots of a u32 (1 in the dumps) and a 256-byte name: a `Custom Data` slot (`Custom Data` is its default
  text; in some files it holds an art-tree texture path instead), then texture slots (`.dds`), 1 to 84 per emitter
  (776 emitters have one, 314 two, 3 none). A block of 1,488 bytes with one texture slot suggests 0x4B0 bytes of fixed
  part plus 0x120 per slot, and the first dumped blocks fit that; larger blocks carry more data before the slots [I].
  *(2026-11-23, ticket 4787: the slot rule as built and checked on all files is in 16.2: the Custom Data slot's flag is
  0, the texture and shader slots' flag 1, slots 0x120 bytes apart; the first flag-1 texture slot is at 0x4B0 in 854
  emitters [C].)*
- 354 blocks name a `.txt`: `FakeVolume.txt` 350, `Brightness.txt` 3, `Mask.txt` 1. These names match three of the
  EXE's five Fork shader sets (`ForkFakeVolumeShader`, `ForkBrightnessShader`, `ForkMaskShader`); the other blocks
  would use `ForkDefaultShader` or `ForkGlowShader` [I].
- **Textures**: 122 distinct names in 3,100 references; 112 exist in an archive (EffectTextures 106, Expansion1
  Effects 2, Expansion2 Effects 1, ConquestDeluxe 1, Polynesia 1, Misc 1); 10 do not (for example
  `fx_smokecloud03.dds`, `fx_trailfire_02.dds`) [C]. *(Corrected 2026-11-23, ticket 4787, 16.2; replaced "123 distinct
  names in 3,102 references ... 11 do not (... `?.dds`)": the survey's string search also matched `?.dds` twice inside
  float bytes of one emitter, at unaligned offsets; it is not a slot.)* What the EXE draws for a missing texture is not
  known.

**Outside knowledge** (web search 2026-11-15): a 2010 trade-press article says Firaxis used the Fork Particle toolset
for Civilization V's visual effects. One open project, `lu-rebuilt/psb-editor` (GitHub, AGPL-3.0 by its page), edits
Fork `.psb` files of LEGO Universe; its page says its format knowledge was reverse-engineered from that game's
binaries. Neither its code nor its format notes were read here. Whether we may read or take from it (AGPL with our
GPL-3.0-or-later under D22, and its origin under D83's intent) is a question for the MC, as `granny-ro-js` was in the
sheet's 3.4. LEGO Universe's `.psb` version may differ from 100.

## 5. Model effects [C for the examples; the 66 were not all summarised]

- 66 model definitions -> `.fxsxml` (one `<Mesh>`, `<Texture>` preload hints as for landmarks) -> `.gr2`, read with the
  existing readers (9012's Python and `models/`). Effect meshes are rigid (60-byte stride) or skinned (68).
- Shaders seen: **`FXShader`** (city damage smoke, pillaged smoke, god ray), **`FXShader_Skinned`** (oil slick, 7
  bones), **`SimpleShader`** (projectile models: the arrow). The FX sets bind up to four layers of
  `BaseTextureMap`, `UVScrollingMap`, `AlphaLookupMap` (`evidence/2900/2903/slot_names.txt`): scrolled UV maps and an
  alpha lookup, which `fScrollTime` presumably times [I]. `FXShader_CODE` and `FXShader_Skinned_CODE` have six passes
  each (`material_table.txt`); which code path uses them is not known.
- Animated model effects carry their own clips (`fx_oil_idlea.gr2`, `fxb_nuke_dome_bones_idlea.gr2`) and the
  `<Animation>1000</Animation>` reference: the units' pose layer (U2) plays them.
- City damage: `fxm_city_damage_state_{first,second,third}_0{1,2,3}` (9 models) and the EXE's own names
  `ART_DEF_VEFFECT_CITY_DAMAGED_SMOKE_*` (section 6).

## 6. How an effect is started [C for the data; I for the EXE's handling where marked]

| Route | Data | Counts |
|---|---|---|
| **Timed triggers** in a model's animation (`.ftsxml`, `model-reader.md` 6.4 d) | `FTimedTriggerEffect`: `event` = a VisEffect, `bone` = the attachment bone, `time` and `duration` in seconds from the clip start, `ec` = the action code of the clip | 673 files (666 in archives + 7 loose), 7,049 Effect rows, 392 distinct names, 502 distinct bones (`World_Center` 555, `World` 364, `tank_bone_main` 202, `FX_Muzzle_Bone` 152, `point_STRING` 104, ...) |
| | `FTimedTriggerTransfer`: hands an effect started by the trigger `refid` of the same file to a projectile, with `speed` and a `tangent` | 603 rows: 579 point at an Effect trigger, 21 at a Sound trigger, 3 at no row; speed 0 to 182.4, median 12.8; action codes 1160 (174), 1180 (167), 2100 (85), 1100 (84), 2160/2180 (38 each), 1140 (16) |
| **Landmark states** | `<TimedTrigger file="FX_Triggers_*.ftsxml" state="3">` in a landmark `.fxsxml` (pillaged smoke; `landmarks-and-models.md` 1.2) | |
| **Combat** | `ArtDefine_UnitMemberCombatWeapons`: `HitEffect` (impact at the target, 8 values, 7 of them `..._IMPACT_$(TERRAIN)`), `ID` (projectile effect: `ART_DEF_VEFFECT_MECH_ROCKET_PROJECTILE` 6, `..._TRAIL_RAILGUN_PROJ`, `..._PLASMA_RIFLE_PROJ`; `PROJECTILE` 7 rows, a marker, not an effect name [I]), `ProjectileSpeed`, `ProjectileTurnRateMin/Max`, `ProjectileChildEffectScale`, `HitRadius`, `HitEffectScale`, `AreaDamageDelay`, `ContinuousFire`, `WaitForEffectCompletion`, `TargetGround`, `IsDropped`, `MissTargetSlopRadius`, `VisKillStrengthMin/Max`, `WeaponTypeTag` (FLAMING_ARROW 90, EXPLOSIVE 79, METAL 60, BULLET 51, ...); plus the child on `ProjectileEnd` | 377 rows, 249 unit members |
| | City ranged attack: `ICvCity1::GetCityBombardEffectTag` (the DLL returns `Eras.CityBombardEffectTag` of the city's era: `CITY_BOMBARD_EARLY` / `_MID` / `_LATE`), which are VisEffect names in the base file (`CvCity.cpp` 34199) | 3 effects |
| **Engine events** | `EventEffects`: `SerialEventUnitCreated`, `SerialEventUnitDestroyedInCombat`, `GameplayFX`, `NaturalWonderRevealed` (section 1) | 4 events |
| **The DLL's FX call** | `ICvEngineUtility::GameplayDoFX(ICvPlot1 *, int)` (vtable slot 177, `proxy/model/policy-table.tsv` row 385): the DLL calls it from notifications and from minor-civ code with a plot (`CvNotifications.cpp` 313, 317, 791, 863, 881, 1142; `CvMinorCivAI.cpp` 15852). That it raises `GameplayFX` and so `ART_DEF_VEFFECT_LOOK_AT` at the plot is [I] | 7 call sites |
| **Names in the EXE** | `ART_DEF_VEFFECT_CITY_DAMAGED_SMOKE_{FIRST_01..03, SECOND_01, THIRD_01}`, `ART_DEF_VEFFECT_PARADROP`, `ART_DEF_VEFFECT_XCOM_DROP`, `ART_DEF_VEFFECT_FARM_PILLAGED_SMOKE_01` (`evidence/0320/exe_fx_strings.out.txt`) | 8 names |

**Substitutions** [C for the names, I for the rule]: trigger events and `HitEffect` values use `$(TERRAIN)` (impacts),
`$(BASETERRAIN)` and `$(WEAPON)`. The impact families exist per surface: `DIRT` 11, `CITY` 10, `AIR` 8, `SNOW`,
`SAND`, `MUD`, `LEAVES`, `WATER` 7 each, `WATER_CITY` 1; the database's `Terrains.EffectTypeTag` (DIRT, SAND, SNOW,
WATER) and `Features.EffectTypeTag` (SNOW, LEAVES, MUD, WATER) supply the values for a plot; `CITY` and `AIR` follow
from the target. `$(WEAPON)` presumably takes `WeaponTypeTag` [I]. Of 392 trigger names, 386 resolve as written, 4
carry a substitution, 2 resolve to nothing (`ART_DEF_VEFFECT_SEAGULLS_01`,
`ART_DEF_VEFFECT_WORKBOAT_INDUSTRIAL_HOIST_SPLASH_01`, 3 rows).

**Features**: `Features.EffectType` has one non-null value, `EFFECT_BIRDSCATTER` [C]; how it is drawn is not read.

## 7. Projectiles [C for the data, I for the flight]

A ranged attack's projectile is an effect that the firing clip starts on a bone and then hands over: in the archer's
`fx_triggers_archer.ftsxml`, ec 1100 starts `ART_DEF_VEFFECT_PROJ_ARROW_ARCHER` on `Point_PROJECTILE` at 0.363 s and a
Transfer at 0.504 s (`refid` = that trigger) gives it speed 16.99 and a tangent of (0.004, -1.0, 0.002). From there
the projectile flies to the target: the flight's parameters are the weapon row's `ProjectileSpeed`, turn rates,
`TargetGround`, `IsDropped`, `MissTargetSlopRadius` (where a miss lands) and `fMovementSpeed` on the VisEffect (11
effects); at the end the `ProjectileEnd` child and the `HitEffect` start. The trajectory itself (an arc for arrows,
straight for bullets, a fall for bombs) is the EXE's code and is not read [I]. 0318 saw the archers' arrow trails
drawn by the EXE over the server's picture 2.6 s after the order, in stock's place and shape [L,
`02-exe-internals/disable-systems-and-masks.md`, "The beta's gap alternative"].

## 8. The EXE side [C for strings and references; I for roles]

Systems and source files named in the EXE (`evidence/0320/exe_fx_strings.out.txt`): `cvVisEffectSystem.cpp`
(effects; "Render cvVisEffectSystem"), `cvVisEffectSystemModelLibrary.cpp` (model effects), `cvParticleVisSystem.cpp`
with `cvParticleVisExternalJob` and the job name `ParticleVisJob` (particle simulation on the job threads),
`cvParticleSimSystem` (subscribes to events), `cvCombatSimSystem.cpp` (`RunCombatSim`, `EndCombatSim`,
`AbortCombatSim`), `cvParticleEffectTrigger`, the Firaxis engine wrapper of Fork (`fireengine2\FForkPSystemBase.h`,
`FForkPSystem.cpp`, `FForkPRenderBuffer.cpp`, `FForkSDK.cpp`), `FTimedTriggerEffect` ("Triggers a particle effect event
when fired") and `FTimedTriggerTransfer` ("Transfers the ownership of a particle or sound effect event when fired").
Debug counters: `NumSimulatedEmitters`, `NumSimulatedParticles`, `NumRenderedParticles`, `NumRenderedParticlePolys`.

The functions that load or use the effect data, found from the code sites that hold the address of one of the
strings above or of the effect files' names (`evidence/0320/exe_fx_xrefs.py`; a raw search for the 32-bit immediate in
`.text`; the enclosing function is guessed from the preceding int3 padding, so the function starts are [I]; the
table of strings and their site VAs was replaced by this list, 2026-11-18, D83 audit). Roles [I]:

- 0x4AA4F0 loads the VisEffect definitions; 0x79F280 the particle effect definitions; 0x458550 sets up the VisEffect
  system and its model effects; 0x4FCEC0 loads the event effects.
- 0x705A20 parses a VisEffect art entry (its particle, model and child effects and its fog-of-war mask setting);
  0x638650 parses a model definition (the scroll time); 0x6EDF90 reads a weapon's projectile art (speed, child
  effect scale).
- 0x6BD530 and 0x6FE710 register the combat simulation's events (0x6FE710 also the particle reload event);
  0x4C7AD0 and 0x731FF0 are the combat simulation.
- 0x4686C0 and 0x475E80 are the particle system's start-ups (the Particle key's startups are 0x542100 and 0x67B360,
  `disable-systems-and-masks.md` 2); 0x823490 sets up the Fork SDK; 0x6D26A0 mounts the effect data archive.
- 0x6DEF50 and 0x58C1D0 start effects from the EXE itself (a damaged city's smoke, the paradrop). *Refined
  2026-11-20 by ticket 4798 (section 8.1) [C]: 0x6DEF50 is the city system's set-up that hashes the nine smoke names
  into the object (+0x7F67C..+0x7F69C); the smoke is started and stopped by 0x6A0340. 0x58C1D0 is engine utility
  vftable 0x9EA718 slot 154 (read as `GameplayUnitParadrop` [I]) and starts the paradrop itself.*
- 0x6F4F00, 0x5E7600, 0x4B7FC0, 0x52AED0, 0x571590 and 0x7895E0 construct the Fork and FX shader sets: these six
  starts equal the constructors in `evidence/2900/2903/programs.txt` [C], which checks the guessing rule.

**Shaders** [C, `evidence/2900/2903/`]: material sets 45-49 `ForkDefaultShader`, `ForkGlowShader`,
`ForkBrightnessShader`, `ForkMaskShader`, `ForkFakeVolumeShader`, two passes each, all with `BaseSampler` and
`AuxSampler0`; the first four share one vertex shader, FakeVolume has its own. Sets 54-57 are the FX model shaders
(above). All are in the EXE's `.shader` section, so the server can draw with the EXE's own compiled shaders read from
the player's install at run time, as it does for terrain (D83, D85).

**Switches**: `[DisableSystems]` Particle (0x1000), Projectile (0x2000, inside Particle's branch) and Combat (0x800)
were off in the product (`proxy/src/proxy_session_keys.h`, `VP64_D33_KEYS_A2`) until 2026-11-15 and are on since
(issue 0368, D147: the EXE draws the effects; `disable-systems-and-masks.md` 5); on 2026-11-09 Particle off saved
43 MB on the huge save; FluidFOW's `FluidParticles.cpp` is a different system (fog), not effects
(`disable-systems-and-masks.md` 2, `allocations-and-memory-map.md` 5).

### 8.1 Stage A: how the EXE starts, moves and stops an effect (ticket 4798, spike S-B, premises B1 and B2)

Ticket 4798 (epic 0042, route S stage A, Architect's note `../07-architecture-decisions/architect-2026-11-20-route-s-combat.md`
2.1 and 5), worker W6 of team-0042 (account B), 2026-11-20 03:18-04:20 EST. Read by hand with llvm-objdump 19.1.7 (Intel
syntax) on the old build's analysis copy (`%USERPROFILE%\tools\ghidra-projects\bin\CivilizationV_DX11.exe`, PE stamp
0x546CD0A8, build 4390913); addresses only, behaviour described here, no listing committed (D83). The scratch listing
and the three small query scripts (string references, import names, function print) stayed in the scratch folder.
Marks as in the head of this file. Nothing of build 26577625 was read (D200/D202).

**In short.** No function call is needed: an effect is started, moved and stopped by **posting a record on the EXE's
4 MB queue** (`g_EventSystemLocalMachine` 0x1A0E400, `../02-exe-internals/message-queue-and-fixed-buffers.md` 1), which
the Particle system listens to. **B1 holds [C]**: a start record carries a world transform, a scale and the effect's
name hash, and no model. **B2 holds [C]**: posting a start record again with the same owner key and the same name moves
the live effect (its transform is replaced and its fog test re-run); the EXE's own projectile flight does exactly that
every frame. The stop is a record that names the owner key.

**The records** [C]. Each is 0x80 bytes with the queue's usual header (`[size 0x80][type][0][0]`), the payload at
record + 0x10. Payload offsets:

| Type | Meaning | Payload | Stock writers |
|---|---|---|---|
| 0x29 | start or move an **owned, lasting** effect | +0x00 a 4x4 world matrix (D3DX row-major, translation at +0x30/+0x34/+0x38, world units); +0x40 the **owner key**, 16 bytes (two 64-bit words; the second is 0 in every stock writer read); +0x50 the **name hash**; +0x54 the **scale** (float, 1.0 in stock) | the city damage smoke (0x6A0340, called from 0x537BF0), the pillaged farm smoke (0x754330), the projectile's flight (0x7454E0, every frame), a debug start at the origin (0x499680); allocator 0x866AB0 |
| 0x28 | start or move an **owned one-shot** effect | as 0x29 | the model's timed triggers (0x4CE510, called from 0x5D65B0: matrix = the bone's world transform, key = model id and trigger index, sign-extended, scale = a float of the trigger's model, hash of the trigger's event name) and four other sites (0x4A6390, 0x634460, 0x759110, 0x76F390, not read); allocator 0x846030 |
| 0x31 | start an **unowned one-shot** effect | as 0x29; +0x40..+0x4F are ignored (the paradrop leaves them unset) | the paradrop (0x58C1D0: matrix = rotation about the vertical axis towards the flight direction plus the target plot's world position, scale 0.08, `ART_DEF_VEFFECT_XCOM_DROP` for `UNIT_XCOM_SQUAD`, else `ART_DEF_VEFFECT_PARADROP`); allocator 0x86C270 |
| 0x2A | **stop** the owner's effect | +0x00 the owner key (16 bytes) | the city smoke before a restart, the pillaged farm smoke, the debug start (key 0); allocator 0x866A50 |
| 0x2B | hand the owner's effect to a second owner [I: `FTimedTriggerTransfer`] | +0x00 the old key, +0x10 the new key | helper 0x86E610 |

- **Name hash** [C]: CRC-32 with the reflected polynomial 0xEDB88320 (table at 0xCB78B8, filled at start-up by
  0x9C4BC0), start value 0xFFFFFFFF, **no final inversion**, over the exact name bytes (0x46E1F0 for a C string, 0x830890
  with a length). In Python: `zlib.crc32(name) ^ 0xFFFFFFFF`; `ART_DEF_VEFFECT_PARADROP` -> 0x26800651. The effect
  definitions are found by this hash (the system's hash map at +0x17D3C, 0xBC-byte definitions at +0x17D30).
- **The owner key is the handle** [C]. Nothing is returned to the poster. The effect manager hashes the 16-byte key
  with the same CRC (0x712320) and keeps **one live effect per key** (a map at manager + 0xD0, and a reverse map from
  instance to key at + 0xFC). A start with a key that already has an effect of **the same name** updates that instance
  (0x5DB2B0: new transform = the definition's transform times the record's matrix, scale = definition scale times the
  record's, fog test again) and returns it: that is the move. With **another name** the old effect is stopped and
  removed first (0x426700), then the new one is created (0x6D55F0). Stock keys are object addresses or ids
  (the city smoke uses the address of its own smoke slot); a caller must choose keys that no stock writer uses
  [I: any value with a tag in the second word avoids them, since every stock writer read leaves it 0 or an index].
- **One-shot and lasting** [C for the flag, I for its meaning]: after the start, the instance's byte +0x70 is set to 0
  for 0x29 and to 1 for 0x28 and 0x31 (0x4E25D0). Read as "free the instance when its duration ends" for 1 and "keep
  until stopped" for 0; stock uses 0 for smoke that lasts and 1 for triggers and the paradrop. The live check is to
  confirm it.
- **Stop** [C for the path, I for the look]: 0x2A finds the key and removes it (0x426700); the instance is stopped
  through 0x69DDA0 (its emitters and models through 0x483D30, a notification to the manager's owner, the slot freed by
  0x4848A0). Whether live particles die out or vanish at once was not read.
- **Fog** [C for the test, I for the grid's meaning]: create (0x6D55F0) and move (0x5DB2B0) both test the effect's
  position against the current view (view record `[system + 0xC] + [system + 0x17DE4] * 0x8FC`): when the view's word
  + 0x8F0 is non-zero, the world position is turned into a plot (0x7561C0), the plot is checked to be on the map
  (0x4496D0: width + 0x94, height + 0x98, wrap byte + 0x9C) and the view's per-plot byte grid at + 0x84C is read there
  (0x680E80) and ANDed with the definition's `FOWMask` byte (+0xBA). A create whose AND is zero makes no effect at all
  (index -1); a move stores the plot (instance + 0x4C) and the result as a visibility flag. With + 0x8F0 zero there is
  no test. Read as the view's fog states per plot [I: FOWMask 1 = revealed, 2 = visible matches bits of such a grid];
  who fills the grid and what + 0x8F0 is (written by 0x721D40, a view's cull-rectangle update, from 0x5F2A40 and
  0x796C70; compared with -1 there, so an index) was not read, so what happens with the EXE's FOW key off is the live
  check's question (note 7).

**Thread and moment** [C]. Writers allocate with `InterlockedExchangeAdd` on the current channel
(`0x1A0F080 + [0x1A0E400] * 0x800180`; the channel array is the index global + 0xC80) and fill the record afterwards.
The records are dispatched on the **main thread**, in the frame function's fourth pump (call at 0x4B4067 -> 0x459D20,
which swaps the channel's two buffers and calls the dispatcher 0x6A5970; the handler slot of each type is the shared
trampoline 0x63E530 -> 0x8668E0 that walks the type's listener list). So a record posted in frame N is applied in the
first pump after it. Stock posts from the main thread (city and farm smoke, the projectile, the triggers) and from the
game-core thread (the paradrop handler is an engine utility method the DLL calls) [C for the sites, I for the threads of
the trigger sites]. A writer on another thread races the swap (a record allocated but not yet filled when the buffers
flip) [I]; the shell posting on the main thread at frame insertion avoids it by construction.

**The listener** [C]. The Particle key's start-up 0x67B360 calls 0x6A1C50, which registers one object on types 0x28
(0x77DC60), 0x29 (0x56A2D0), 0x2A (0x4037E0), 0x2B (0x693290) and 0x31 (0x752EB0), besides three listeners on queue B.
So **with Particle off nothing listens and the records are dropped**; Combat and Projectile play no part in this path.
Each handler first tests the object's busy bit (+0x98 bit 0): when set, it copies the record to the object's deferred
list (+0x88) for later [I: replayed after the particle job]; otherwise it builds a descriptor (matrix, scale, hash) and
hands it to the effect manager at object + 0x9C, with a job function per type (0x29 -> 0x52DF30, 0x28 -> 0x64D390,
0x2A -> 0x4CFEF0, 0x2B -> 0x420AF0, 0x31 -> 0x705320).

**The EXE's own trail mover (B2 in stock)** [C]: the projectile object's frame step 0x7454E0 (called from 0x49F9D0 and
0x765200) computes the flight parameter from elapsed time over the flight time, the position and tangent on the path
(0x600720), builds an orientation from the tangent, multiplies it with the stored local transform and posts a 0x29 with
its stored key, hash and scale, every frame; when the time is up it ends the flight (0x707840: two records of types
0x34 and 0x36, not read, read as the `ProjectileEnd` children [I]).

**What stage A needs from the build table** (asked of lead-0590 by name on thread 0590, 2026-11-20): one data entry,
the queue's index global (`g_EventSystemLocalMachine`, anchor 0x1A0E400; the channel base is + 0xC80 and the stride
0x800180 in the pump's code, worth a pattern guard), and the three record types as guarded constants of the build
(start 0x29, one-shot start 0x28, stop 0x2A), read from the three allocators 0x866AB0, 0x846030 and 0x866A50 (each
writes its type as an immediate; their bytes are the natural guard). Move needs no entry of its own (a start with the
same key and name). Calling the allocators instead of writing the header by hand is equivalent; either way the poster
must be on the main thread.

**Not verified by the static read**: the deferred replay's moment, the one-shot flag's meaning, the stop's look, the
fog with FOW off, the 0x34/0x36 records, the four 0x28 writers not read.

#### 8.1.1 Live check (ticket 4798) [L where marked]

**Method.** A spike proxy (`proxy/src/proxy_fxspike.{h,cpp}` with a two-line chain in `proxy_ui_bridge_rt.cpp`, commit
1f6aadf6f on branch `w6-4798`, **never merged**; the branch's tip reverts it) posts the records of 8.1 from the proxy's
main-thread frame hook (the bridge's frame Lua hook, the same one as ticket 0586's live reload), driven by a request
file `fxspike_req.txt` in the proxy's folder; it answers in `fxspike_ack.txt` with the frame and thread id. Every
address and record type is in that request (taken by the driver from a local JSON outside git, D202), and the PE time
stamp it names must equal the running EXE's, else nothing is posted. The name hash is computed by the proxy (CRC-32 as
in 8.1). Driver: `proxy/evidence/0042/4798/run_4798.py` (run_4771.py's prep, arm, run and restore with its own
phases), arm `s1`: product path, `ServerRenderer = 1`, `ServerUnits = 1`, the D33 set (FOW and FluidFOW off in the EXE)
plus `[DisableSystems] Combat = 1, Projectile = 1` for the session, Particle on. Plot world positions from the EXE's
own grid and heights (`shell/tools/gt_reader.py`). Effects: `ART_DEF_VEFFECT_MACHINE_GUN_MUZZLE` and
`ART_DEF_VEFFECT_ARTILLERY_IMPACT_DIRT` as one-shots (0x28), `ART_DEF_VEFFECT_CITY_DAMAGED_SMOKE_THIRD_01` as the lasting
effect (0x29) for the stop, the move (300 frames along four plots, B2) and the fog (a revealed but not visible plot, an
unrevealed plot, a visible control). Owner keys carry 0x4798 in both 64-bit words (no stock writer's key).
Expected from 8.1: the one-shots draw and end by themselves; the smoke lasts until its stop; the moved smoke follows the
line; with the EXE's fog object absent (FOW off) the fog test is skipped and the smoke draws on every plot [I].

**Result so far (2026-11-20 05:15-05:55, c5h-g, F:, old build, EXE based at 0x00970000: the old build's header sets
DYNAMIC_BASE, so relocation is normal) [L]: no effect was posted yet.** Run r1 stopped before the launch (the worktree's
`tools/harness/native` was not built). Runs r2 and r3 each ended in **a crash without a box** (0xC0000005, execution at
an address in no module: 0x004FF454 in r2, 0x0091F874 in r3; main thread) about 45 s after the save loaded, at the
moment of the driver's first picture (`a_before`). In r3 the spike proxy logged its configure line and "frame hook
alive", but **no "request seen" line**, so the crash came before any request was read and before any record of this
ticket was posted. In r2 it was the same: no fxspike line. Both restores were rc 0 (backup restore ok, provision
verify 0). The arm (`ServerUnits = 1` with Combat and Projectile off) is new: W3's 4771 runs on the same layout used
`ServerUnits = 0` with Combat off, or `ServerUnits = 1` with Combat on. Run r4 (control: W3's proxy 1502a3a5 without
the spike code, same arm and steps, no request) was started at 05:53 to tell whether this is a product-path crash outside
4798 (COMMON 30, for the MC); its result is in ticket 4798's log. **Correction (lead-0042, 2026-11-20 06:02): r4 did NOT
crash** (run rc 0, empty crash list, 0 LOUD lines, restore rc 0, verify 0), so r2/r3's crashes come from the spike build
(commits 1f6aadf6f and 69714aa3b: the frame Lua hook chain `FrameLuaChain4798` in proxy_ui_bridge_rt.cpp and
proxy_fxspike), not from the arm; 4796's frame hook in the same place ran 4 of 4 fights without a crash. The spike
prototype is not merged anywhere. The root cause is ticket 4720's. B1 and B2 stay `[C]` only; the live questions (draw,
depth, fog with FOW off, one-shot lifetime) are open. (Replaced 2026-11-20 by 4720: "Root cause: not determined yet".)

**ROOT CAUSE, current statement (ticket 4725, W19 of team-0042, 2026-11-20 10:20 EST) [L for the registers of one
instrumented launch, C for the disassembly of that build and the object files]:** the spike's `proxy_fxspike.cpp`
includes `<string>` and `<vector>` without the proxy's `_SECURE_SCL 0` / `_HAS_ITERATOR_DEBUGGING 0` (which every other
container TU of the proxy gets from `proxy_rt_logic.h`, `proxy_report.h`, `proxy_pack.h` or the DLL's headers), so it
compiles the VC9 STL with `_SECURE_SCL=1` (VC9's default): a checked `std::vector` of 24 bytes whose iterator is two
words. The other 29 container TUs use `_SECURE_SCL=0` (16-byte vector, one-word iterator). Both define
`std::vector<std::string>::_Insert_n` under ONE mangled name (the setting is not part of the name); the linker keeps one
COMDAT copy, the `_SECURE_SCL=0` one (3 stack words, `ret 0xC`). `Poll`'s `lines.push_back(text.substr(...))` calls
`vector::insert(end(), value)`, a function that only the spike's TU defines (so it is the `_SECURE_SCL=1` one); it passes
`_Insert_n` 4 words and expects `ret 0x10`, so after the call ESP is 4 bytes low; the wrapper's pop-only epilogue then restores ESI, EDI, EBX and EBP from one slot too low and its `ret 0x10`
returns to its saved EBP: an address on the main thread's stack. That is every observed fault (execution in no module at
a stack address, the four low halves within 0x460). It ran clean offline because the offline harness compiles only the
spike's TU (one setting, one layout). Not the EXE, not the hook form, not the CRT, not the file read as such. Details:
"The call named" below. (Replaces 2026-11-20 10:20 the statement "narrowed to the spike's file read inside the game; what
in the game process makes the read path return into the stack is not known" of 4720.)

**Root cause of r2/r3, static part (ticket 4720, W12 of team-0042, 2026-11-20 06:25-07:00 EST)** [C for the code, L for
the logs, I where marked]. (Corrected 08:30 by the runs below: the static part named the request's arrival and left
open "the EXE's MODS reaction or the spike's read"; R-mods cleared the MODS write, R-logs reproduced the crash with the
request under Logs. Current statement: **"Live result" below.**) Sources: the spike's code (1f6aadf6f, 69714aa3b), the run folders `live-r2`..`live-r4`, the
proxy's `exe-crash.txt` of each run and the driver's `run.json` (all outside git), the old build's analysis copy for the
arithmetic (addresses only, D83).
- **The timing** [L, logs]. The driver wrote `fxspike_req.txt` into the proxy's folder `MODS\(1) Community Patch` at
  05:25:51.874 (r2) and 05:45:44.363 (r3); the proxy's crash filter stamped the crash at 05:25:52.224 and 05:45:44.614:
  **0.350 s and 0.251 s after the write**, in both runs at the first write. The spike's frame hook had run clean before
  it (r3: "frame hook alive, frame 1" at 41.30 s on the proxy's clock, the crash at 87.27 s, about 1,380 frames at the
  30-fps cap, `Poll` looking for the missing file every 10th frame, 0.33 s). r4 (the control) and 4796's run wrote
  nothing under MODS during play (r4's driver skipped every request); the composite and the run folder write to
  `<user>\Logs` (0148's fix).
- **No spike command ran in r3** [C, L]. The r3 build (69714aa3b) logs "request seq N seen" right after it parses the
  first line, before any command; that line is absent while the crash line follows on the same log (the proxy's log
  lines are written synchronously), so the crash came before or outside the spike's parse, and before any record was
  posted or any address of the request was used. r3's request (`b0_config`) carried configuration lines only (stamp and
  three VAs), which store numbers. (r2's build had no such log line, and its request held two one-shot starts; what r2
  ran is unknown.)
- **Not an unrebased address** [C]. Both fault addresses lie below the EXE's base 0x00970000, so neither is inside the
  relocated EXE. Read as VAs at 0x400000 on the old build, 0x4FF454 is the fourth byte of a five-byte instruction at
  0x4FF450 and 0x91F874 an instruction inside a function (after a `test`/`je`): neither is a function entry nor the
  return address of a call, which an unrebased call or return would give. Neither equals a VA of the local address file
  or its rebased value. "Execution in no module" on the main thread reads as a call or return to a non-code address
  (a stale function pointer or a damaged stack) [I]; the crash filter keeps no stack (D83), so the caller is unknown.
- **The calling chain is not the cause** [C]: `combat-choreography.md` 3.5.8 (same convention as the slot, each part
  called once, static code, `L` not used by the spike).
- **Open between two causes** [I]. (a) The EXE's own reaction to a change in its watched MODS tree
  (`renderer-in-server-design.md` 4.14: one subtree change notification on `<user>\MODS` with the filter FILE_NAME |
  DIR_NAME | LAST_WRITE; the callback sets the byte `[g_Civ5App] + 0xEF0`; the main thread then stalls 120-170 ms; what it
  does there was not traced, and no reader of + 0xEF0 by that displacement was found in `.text` besides the callback's
  write) on this arm (`ServerUnits = 1` with Combat and Projectile off, first used by 4798). Against it: 0148's probes
  wrote under MODS at 2 Hz for minutes on the product path without a crash (2026-11-13, older builds, ServerUnits off),
  and the HI tester's composite dump (`tools/hi/capture.py`) creates `shell_composite.shot` beside the composite during
  play [C for the code; the arms of those runs not checked]. (b) The spike's own read between `GetFileAttributesExA` and
  its first log line (`fopen`, `fread`, `fclose`, `std::string` parsing): nothing there calls through a pointer that could
  reach an address in no module [I]. Neither is closed by reading.
- **The arms that decide it** (4720 part 3; each one change from a run already made, old build, c5h-*, windowed):
  **R-mods**: r4's control (W3's proxy 1502a3a5, arm s1, same steps) plus one change, the driver creating a small file in
  `MODS\(1) Community Patch` one second after the picture `a_before`, then 30 s idle. A crash there means any writer
  under MODS crashes this arm of the product path (COMMON 30, outside 4798: the cause moves to the EXE's MODS reaction).
  **R-logs**: r3's spike build with the request and answer files moved to `<user>\Logs\VP64\4720\` (the only change).
  No crash there clears the spike's read and lets the B1/B2 steps run in the same launch.
- **How they run** (written 07:20, before the runs; grant #7919, c5h-g on F:, windowed, 07:35-08:35). Driver
  `proxy/evidence/0042/4720/run_4720.py` (run_4798.py with the request folder `<user>\Logs\VP64\4720` and the mode
  `FX_MODS_WRITE=1`) and `chain_4720.sh INSTANCE NAME mods|logs ADDRS` (F: read latency under 50 ms and the EXE stamp
  0x546CD0A8 checked before the launch; the probe file must be absent before, is removed after the game stopped and
  checked absent; restore by SHA-256). Layouts: copies of W6's (server e85af4a5, composite 4a2ad069, x64 DLL 78355a75);
  R-mods with W3's proxy 1502a3a5, R-logs with the spike variant e754e52b (branch `w12-4720-spike` b025f920a = 69714aa3b
  plus the folder change, built from the pin's source 1fd51353e; never merged). Expected if (a): R-mods crashes about
  0.3 s after its write and R-logs does not; if (b): R-mods is clean and R-logs crashes at its first request.
- **Live result (2026-11-20 07:37-07:57, c5h-g, claim and release posted on thread 0042) [L]: (b). The crash follows the
  spike's first request wherever the file lies; a write under MODS alone does not crash.**
  - **R-mods** (07:39-07:46, EXE base not logged by W3's proxy): the driver created
    `MODS\(1) Community Patch\4720-mods-probe.txt` at 07:43:29.962; the game ran on 44 s (pictures at +22 s and +43 s),
    run rc 0, crash list empty, 0 LOUD lines; the file removed after the stop and checked absent; restore rc 0, verify 0.
    The lockstep CSV shows one pair of Present intervals of 67 and 56 ms about 70 s into the composite's record and no
    interval over 100 ms after the load: on the Tiny save the EXE's MODS reaction, if that pair is it, costs about 120 ms
    and nothing else [I for the pairing: the CSV's clock was not mapped to the wall clock].
  - **R-logs** (07:48-07:57, spike e754e52b, EXE base 0x00EA0000): "frame hook alive, frame 1" at 50.73 s on the proxy's
    clock; the request `b0_config` (configuration lines only) written to `<user>\Logs\VP64\4720\fxspike_req.txt` at
    07:52:56.527; **crash at 07:52:56.814 (0.287 s later), 0xC0000005, execution at 0x007EF5DC in no module, main thread**
    (the proxy names the EXE's main thread its "boot thread": r3's log shows the same id for both); no "request seen" line
    again. Restore rc 0, verify 0; the empty `Logs\VP64\4720` folder removed. Fault below the EXE base again.
  - So three of three spike launches crashed 0.25-0.35 s after their first request (one `Poll` period is 10 frames,
    0.33 s), each before the "request seen" line, and no spike launch has crashed before a request (46-58 s of frames).
- **Offline, the spike's code does not crash** [L, 2026-11-20 08:15-08:25]: `proxy/evidence/0042/4720/fxspike_offline.cpp`
  drives the spike's `proxy_fxspike.cpp` (w12-4720-spike) as the drain does (Configure with a proxy folder under a fake
  `MODS`, `OnFrame` 2400 times, the exact `b0_config` bytes at frames 500 and 1000, a start at 1500 that the stamp check
  refuses), built by `build_offline.py` with the proxy's x86 unit-test toolchain twice: static CRT, and `--md` against
  msvcr90/msvcp90 with the VC90 side-by-side manifest as the proxy uses them. Both: 2400 frames, 3 of 3 requests answered
  "done", the frame-1801 log line written, exit 0. The read and parse path (`GetFileAttributesExA`, `CompareFileTime`,
  `fopen`/`fread`/`fclose` of msvcr90, msvcp90's `std::string`, the vector of lines) is sound outside the game.
- **What is left** [C for the code read, I for the reading]. In the game the first request makes the spike run, for the
  first time since frame 1: the read path above and then the `Log` call of "request seen", which calls through the
  function pointer `g_log` (set once in `Configure`, the only writer of that word in the whole proxy by its address; the
  word lies right after `g_track` in the spike's `.bss`). Between frame 1 and the request no spike code calls `Log` (the
  next "alive" line is at frame 1801, about 60 s at the cap, which no launch reached). Two readings remain and the logs
  cannot tell them apart: (1) something in the game process changes what the read path does (a hooked or re-entered
  CRT or kernel32 path inside the EXE's Lua call), or (2) `g_log` (or the stack the call returns through) is damaged
  between frame 1 and the request, and the first `Log` after frame 1 jumps to the damaged value: an address in no
  module, as observed, different in every run. The fault addresses (0x004FF454, 0x0091F874, 0x007EF5DC) all look like
  heap addresses of a 32-bit process, which fits (2) better than (1) [I].
- **No dump to read** [C, 2026-11-20 08:35]: the three kept crash folders (`<instance>\run\crash\20261120_052553_pid7888`,
  `..._054545_pid69044`, `..._075258_pid46852`) hold the Goldberg `crashes.txt` (code and address only), the EXE's
  `instruments.log` and a manifest with `wer_dump: no_wer` (the game had ended without a box, so no WER dump was taken);
  the proxy's `exe-crash.txt` records code, address, thread and time only (no registers, stack or memory, by its design
  under D83); no `.dmp` exists under the instance or in the user's CrashDumps folder for these times. So EIP is the only
  register known (the fault address itself); ESP and the return address were never recorded, and the static route ends
  here.
- **R-decide: the read path, not `g_log`** [L, 2026-11-20 08:31-08:41, c5h-g, MC-B's grant 08:28, spike e754e52b, EXE
  base 0x00F40000; claim, restore (rc 0, verify 0) and release 08:41 posted on thread 0042]. (i) No request: the spike's
  second `Log` call, "frame hook alive, frame 1801", was written at 106.59 s on the proxy's clock (17.1 s after
  `a_before`) and the game ran on: `g_log` and the `Log` path are sound, reading (2) is out. (ii) 20 s later the driver
  wrote a request file whose first line is not `seq` (`Poll` reads it whole and returns before any `Log`) at
  08:37:56.216; **crash at 08:37:56.348 (0.132 s later), 0xC0000005, execution at 0x00EFF414 in no module, main thread**.
  (iii) did not run. So the crash is inside the spike's read of an existing request file, in the game process only:
  between `GetFileAttributesExA` returning true and `Poll`'s return (`CompareFileTime`, msvcr90's `fopen`/`fread`/`fclose`,
  msvcp90's `std::string` calls, the vector of lines and their destruction) - the same code that runs clean offline.
- **The fault addresses point into the main thread's stack** [C for the arithmetic, I for the reading]. The four
  faults 0x004FF454, 0x0091F874, 0x007EF5DC and 0x00EFF414 all have their low 16 bits between 0xF414 and 0xF874; four
  unrelated addresses would land within 0x460 of each other modulo 64 KB by chance with a probability below 10^-4. A
  thread's stack is reserved at a 64-KB-aligned place that moves from launch to launch while the call depth of the drain
  stays nearly the same, so these are most likely addresses on the main thread's stack at the drain's depth (all lie
  below the EXE's image, where the main thread's stack is reserved in these launches [I: not read from a dump]).
  Execution at a stack address is the signature of a `ret` that pops a stack value (a saved frame pointer) instead of
  its return address, i.e. a stack pointer off by one or more words after a call in the read path (a calling-convention
  or argument-count mismatch between the caller and the function the import slot reaches in the game process), or of a
  return address overwritten with a stack address. The proxy's own calls in that path were checked in the disassembly
  of e754e52b (`fopen` +8, `fread` +16, `fclose` +4 cleaned by the caller; `GetFileAttributesExA`, `CompareFileTime` and
  the msvcp90 `__thiscall` members cleaned by the callee) and match; offline, with msvcr90/msvcp90 and the same code, the
  path is clean. What in the game process makes the read path return into the stack is not known.
- **What it means and what is next** (4720's text; superseded 2026-11-20 10:20 by "The call named" below and the
  current statement at the head of this root cause). The root cause is narrowed to the spike's file read inside the game, a code path
  that only the spike used on the frame path; the hook form, the chain, the EXE addresses, the MODS tree and `g_log` are
  all cleared. `combat-choreography.md` 3.5.8 point 4 already forbids file I/O and `std::string`/`std::vector` growth on
  the frame path, which removes this path from 4773 and 4795. Closing it needs one instrumented build (a `Log` line and
  the stack pointer before and after each call of `Poll`, then the same (ii) request): the last line written names the
  call, and an ESP change across it names the mismatch. For B1/B2, a spike that takes its request through a path other
  than a file read on the frame path (for example the Lua channel the harness already has, or a request read on another
  thread and handed over through a fixed buffer) avoids the faulty path without knowing its cause.
- **The arm that decided it** (lead-0042 said yes in principle, #7994; MC-B granted 08:28): the same spike build e754e52b, one launch,
  three phases 20 s apart: (i) no request until the frame-1801 "alive" line has been written (about 75 s after the load):
  a crash there is the `Log` path (reading 2) with no file involved; (ii) a request file with no `seq` line (`Poll`
  reads it and returns before any `Log`): a crash there is the read path (reading 1); (iii) the `b0_config` request.
- **The instrumented launch (ticket 4725, W19 of team-0042, MC #8033; method written 2026-11-20 09:55 before the
  result).** Build: branch `w19-4725-spike` @0e0b67907 (never merged) = the spike b025f920a plus an instrument only:
  `Fx4725Probe(tag)`, a naked function (`pushfd; pushad; push esp; call Fx4725Record; ...; popad; popfd; ret`) that
  changes no register of its caller and writes one line per call with the caller's ESP at the call, EBP, EBX, ESI, EDI,
  EAX, the probe's return address as an offset in the proxy, and, inside `Poll`'s body, whether the body's return-address
  slot (`_AddressOfReturnAddress()` at entry) still holds its entry value. `Poll` became a wrapper (probes 0x1F0/0x1F1)
  around the old body (`PollBody`, `__declspec(noinline)`), which carries a probe after every call: 0x100 entry, 0x101
  the path string, 0x102/0x1102 `GetFileAttributesExA` (false/true), 0x103 `CompareFileTime`, 0x104 `fopen`, 0x105 the
  empty `std::string`, 0x106 each `fread`, 0x107 each `append`, 0x108 `fclose`, 0x109 each `find`, 0x10A each
  `push_back(substr)`, 0x10B/0x10C/0x10D/0x10E the split of the first line and the `seq` test, 0x10F the end; 0x300/0x301
  around `Poll` in `OnFrame`, 0x400/0x401 at the Lua hook's entry and exit (`FrameLuaChain4798`). Lines go by `WriteFile`
  on the probe's own handle (no CRT, nothing buffered in the process) to `<user>\Logs\VP64\4720\probe4725-<pid>.log`. A
  vectored exception handler (first in the chain, returns CONTINUE_SEARCH) writes the first exceptions with every
  register, the last probe tag and 40 stack words from ESP-16, each named by its module and offset when it lies in one.
  Offline (W12's harness `proxy/evidence/0042/4725/build_offline.py --md`, msvcr90/msvcp90): 2400 frames, 3 of 3
  requests, every probe inside the body at one ESP and the return slot unchanged [L]. Proxy f62d217e (pin 1fd51353e),
  layout `F:/c5-64bit-work/0042/w19/layout` = W12's R-logs layout with only the proxy changed. Driver
  `proxy/evidence/0042/4725/run_4725.py` mode `FX_4725` (3 s after `a_before` the request without a `seq` line that
  crashed R-decide (ii), 20 s; then `b0_config`, 10 s) through `chain_4725.sh` (F: latency, stamp 0x546CD0A8, restore by
  SHA-256). Read: the last probe line before the exception names the call; an ESP or callee-saved register that differs
  from the line before names the mismatch; a `SLOT_CHANGED` names an overwritten return address.
- **The call named (run i1, 2026-11-20 09:51-09:54 EST, c5h-g on F:, old build, stamp 0x546CD0A8 checked, F: read
  latency 5.8 ms; MC-B's direct grant; claim 09:49, restore rc 0 with backup_restore_ok and provision verify 0, release
  09:56; pid 35224, EXE base 0x00720000, proxy base 0x5B480000)** [L for the log, C for the disassembly of proxy
  f62d217e and its objects].
  - The no-seq request was written at 09:53:58.461; **the crash came at 09:53:58.616 (0.155 s later): 0xC0000005,
    execution at 0x06DDF758 in no module, main thread (3824, the proxy's boot thread)**, the same signature as r2, r3,
    R-logs and R-decide (fifth of five spike launches with a request; low half 0xF758, inside the 0xF414-0xF874 band).
  - Probe log (`C:/c5-64bit-work/0042/w19/live-i1/probe4725-35224.log`, 565 lines, outside git): before the request
    every `Poll` ran 0x300 -> 0x1F0 -> 0x100 -> 0x101 -> 0x102 -> 0x1F1 -> 0x301 with the hook's ESP constant (0x400/0x401 at
    0x06DDF7A4, `OnFrame`'s 0x300 at 0x06DDF75C, the body at 0x06DDE42C, the body's EBP 0x06DDF758). With the file:
    0x1102, 0x103 (`CompareFileTime`), 0x104 (`fopen`), 0x105, 0x106/0x107/0x106 (`fread` 0x53 bytes, `append`, `fread`
    0), 0x108 (`fclose`), 0x109 (the first `find`): **every one at ESP 0x06DDE42C, EBP 0x06DDF758, the return slot
    0x06DDF75C unchanged** (`slot_ok`). So every call up to and including the first `find` returned clean: not
    `GetFileAttributesExA`, `CompareFileTime`, `fopen`, `fread`, `fclose`, msvcp90's `append` or `find`. The next probe,
    0x10A (after `lines.push_back(text.substr(i, j - i))`), never came.
  - The vectored handler's record of the fault: EIP 0x06DDF758, **ESP 0x06DDE42C, EBP 0x6D7F5EBB, EBX 0, EDI 0x06DDF430,
    ESI 0x06DDF470**, last tag 0x109, the only exception after the request. EIP is the body's own EBP; EBP is the value the
    body kept in EBX (msvcp90's `~basic_string`, loaded from its import slot); EBX is 0 (the body's EDI then, the empty
    vector's end); EDI is the body's ESI then (0x06DDF430, the iterator buffer). The stack below ESP still holds the last
    call's frame: [ESP-0x10] = proxy+0x5DF4C (the return into the body after `call 0x1005F9F0`), then that call's four
    arguments 0x06DDF430, 0x37D8EC50 (the vector's container proxy, a heap block), 0, 0x06DDF470 (the substring).
  - The disassembly of this build: after 0x109 the body calls msvcp90's `substr` (`?substr@...QBE?AV12@II@Z`, thiscall,
    the result's address and two counts: 3 words pushed, 3 popped), then, the vector being empty, `0x1005F9F0` =
    `std::vector<std::string>::insert(const_iterator, const std::string&)` of the spike's TU, which pushes 4 words (a
    two-word checked iterator, the count 1, the value) and calls `0x10058C10` = `_Insert_n`, which reads 3 stack words
    (`0x8`, `0xC`, `0x10` off EBP: a one-word iterator, the count, the value), uses the 16-byte vector layout (first, last,
    end at +4/+8/+0xC; the spike's TU uses +0xC/+0x10/+0x14) and returns with `ret 0xC`. `0x1005F9F0` (`push ebp; mov
    ebp, esp; push ebx; push edi; push esi`, ending `pop esi; pop edi; pop ebx; pop ebp; ret 0x10`) then pops one slot low:
    ESI <- [0x06DDE408], EDI <- saved ESI 0x06DDF430, EBX <- saved EDI 0, EBP <- saved EBX 0x6D7F5EBB, and `ret 0x10`
    takes the saved EBP 0x06DDF758 as its return address with ESP = 0x06DDE418 + 0x14 = 0x06DDE42C. Every register of the
    fault record follows exactly.
  - Why the two copies: `proxy/evidence/0042/4725/scl_census.py` (preprocesses each TU with build_proxy.py's flags) gives
    `proxy_fxspike.cpp` `_SECURE_SCL=1` and 29 other container TUs `_SECURE_SCL=0`; `comdat_overlap.py` over this build's
    objects lists 13 std:: container functions defined by both settings, among them `vector<string>::_Insert_n` (spike vs
    `proxy_db`, `proxy_pacing`, `proxy_pack`, `proxy_purity`, `proxy_report`, `proxy_rt`, ...) and the vector's destructor;
    the linker kept the `_SECURE_SCL=0` `_Insert_n`. The offline harness (W12's) compiles the spike's TU alone, so its
    `_Insert_n` matched: clean in 2400 frames.
  - **So the five crashes are one defect of the spike's build, an ODR violation between translation units of the proxy
    (the same template, two layouts), on the first `push_back` of a `std::vector<std::string>` in the spike.** The file
    read only made the spike reach that line. COMMON 30: root cause determined [L + C]; the spike is never merged, so no
    product bug follows from the crash itself. The product's exposure is in `combat-choreography.md` 3.5.8 ("The 4725
    defect class").
- **For every frame hook from now on**: `combat-choreography.md` 3.5.8 (point 5: nothing under MODS at run time, from
  the hook or its test driver).

## 9. What the server needs to read and draw them

| Need | Input | Exists today | Missing |
|---|---|---|---|
| Effect definitions | 41 XML files; bare-name lookup in the archives | the `.fpk` reader (`models/`, 9012), the art resolver | an XML reader for the four kinds, the override order, the `$(TERRAIN)`/`$(BASETERRAIN)`/`$(WEAPON)` substitution |
| Particle files | 384 `.psb` | the chain layout (section 4) | the meaning of every emitter parameter; Fork's emission, motion, life curves, spawn shapes, sorting and texture animation, written from a knowledge-base description of the EXE's Fork code (D83) |
| Particle drawing | 206 effect textures (BC1/BC3, small); the five Fork shader sets | the DDS path and the shader-blob replay of terrain | the CPU-built particle quads in the layout Fork's vertex shader expects, the blend and depth states of the two passes, FOWMask through the server's fog (0168) |
| Model effects | 66 `.fxsxml` -> `.gr2`; FXShader sets | `.gr2` reader, mesh repack (U1), pose layer (U2) | the FX constant buffer (0xC50 bytes for `FXShader`), UV scroll and alpha lookup timing |
| Projectiles | Transfer rows, weapon rows, `fMovementSpeed`, `ProjectileEnd` children | the data (section 7) | the flight model and the target point from the combat choreography (C3) |
| Trigger input | timed triggers by clip time, landmark states, combat events, `GameplayDoFX`, city damage, EXE names | the trigger files are read (3100) | the controller (U5) that plays clips and fires their triggers on bone positions; the per-unit visual queue (C2); a feed for `GameplayDoFX` and the bombard tag |
| Oracle | stock pictures and captures with the three switches on | 0318's stock arm; RenderDoc (0302) | an effect capture per class; particles need many frames, as the pose tap does for units (sheet 3.3) |

## 10. Premises for C4b

- **P1** the Fork emitter parameters can be described from the EXE's loader and simulation well enough to write a
  faithful simulation (as N9 assumes for the combat). Untested; the largest risk.
- **P2** Fork draws with CPU-built vertex buffers through `FForkPRenderBuffer` and the five shader sets, nothing
  else on the GPU [I from the names].
- **P3** effect randomness (spawn positions, rates) cannot be frame-exact, as with random idles (sheet 8 item 4): the
  acceptance would compare the emitter's parameters and a statistic of the picture, not pixels per frame.
- **P4** the 11 missing particle textures and 2 dangling trigger names draw nothing in stock [I].

## 11. Size of C4b [I]

| Part | Hours |
|---|---|
| E1 definitions: four XML kinds, override order, substitutions, bare-name lookup | 4-8 |
| E2 `.psb` reader on the proven layout | 2-4 |
| E3 the emitter parameters: description of the EXE's Fork loader in the knowledge base, then field-by-field checks on captures (1,232 emitters, 49 block sizes) | 16-32 |
| E4 the particle simulation from E3 (emission, life, motion, forces, size, colour and rotation over life, spawn shapes) | 16-30 |
| E5 particle drawing: quads, the five Fork shader sets with the EXE's blobs, blend and depth, sorting, FOWMask | 8-14 |
| E6 model effects: FX shader sets, UV scroll, alpha lookup, animated effect models on the pose layer | 6-12 |
| E7 projectiles: hand-off, flight, misses, drops, `ProjectileEnd` children, hit effects | 8-14 |
| E8 trigger routes into the server's scene (controller triggers, landmark states, combat, events, `GameplayDoFX`, city damage, bombard tags) | 6-12 |
| E9 checks against stock captures per effect class and acceptance | 10-16 |
| **C4b total** | **76-142** |

The sheet's guess was 25-50 h. A subset without Fork particles (E1, E6, E7, part of E8) is about 25-45 h and shows
projectile models, smoke and damage models but no trails, sparks, dust or fire: a look-alike, a fallback only (D66).

## 12. Cheaper routes and open questions

- **The EXE's own systems.** 0318 turned Particle, Projectile and Combat back on over the product path: the EXE drew
  the combat, the arrows and the strikes over the server's picture as stock does, for +15 to +51 MB [L, Tiny, EXE
  drawing units]. With the server drawing the units in state (ii), the EXE's effects would follow the EXE's own
  simulated units, so they might still line up with the server's figures if both follow the same calls. Not tested; and
  0317 found that state (ii) stalls the AI turn (`unit-state-ii.md` 0). A check of 2-4 h after U6 would say whether
  this route can replace C4b for the beta and beyond.
- **Stage A's entry (ticket 4798, 2026-11-20)** [C]: an effect is started, moved and stopped from outside the EXE's
  figures by posting 0x29 / 0x28 / 0x2A records on the 4 MB queue, keyed by a caller-chosen 16-byte owner key and the
  name's CRC-32; section 8.1.
- **The `.psb` project of another game** (section 4): reading it could shorten E3 considerably if allowed; the MC
  decides (licence AGPL-3.0, origin a decompile of another game's binaries).
- Not read: the EXE's handling of `$(WEAPON)` and `$(BASETERRAIN)` (the variants that exist: section 14.3), the
  projectile trajectory, `EFFECT_BIRDSCATTER`, what the `_CODE` FX passes draw, the emitter parameter bytes.

## 13. Evidence and commands

All under `research/05-graphics-composition/evidence/0320/`, run 2026-11-15 01:57-02:12 EST on the workstation. Scratch
(extracted files, the database copy) in `C:/c5-64bit-work/0320/`, removed at hand-back.

| Script | Run | Output |
|---|---|---|
| `census.py INSTALL OUT` | `quietlock.py run` (reads all archives, ~2 GB) | index of every archive entry (scratch), `archives_with_effects.txt` |
| `extract_ext.py INSTALL OUT .psb .ftsxml .fxsxml` | `quietlock.py run` | 2,438 files to scratch |
| `psb_survey.py DIR` | | `psb_survey.out.txt` |
| `psb_peek.py FILE` | | strings with offsets of one file (scratch only) |
| `fx_defs.py EFFECTDATA INSTALL EXT INDEX PSBTEX DB` | | `fx_defs.out.txt` |
| `transfers.py DIR...` | | `transfers.out.txt` |
| `db_fx.py DB` | | effect-related columns (scratch) |
| `dds_survey.py DIR...` | | `dds_survey.out.txt` |
| `exe_fx_strings.py EXE`, `exe_fx_xrefs.py EXE` | | `exe_fx_strings.out.txt`, `exe_fx_xrefs.out.txt` |

Sources: the install's `EffectData.fpk`, `*Models.fpk`, `*EffectsTextures.fpk`, `Assets/DLC/*/Effects/*.xml`, the
database copy; `CivilizationV_DX11.exe` (strings and immediates only); DLL source `CvNotifications.cpp`,
`CvMinorCivAI.cpp`, `CvCity.cpp` (the owner's fork checkout); `model-reader.md` 6.4, `landmarks-and-models.md` 1,
`disable-systems-and-masks.md`, `unit-state-ii.md`, `evidence/2900/2903/`; the Architect's increment 3 sheet;
web: Game Developer 2010 article on Firaxis and Fork Particle, `github.com/lu-rebuilt/psb-editor` (page only).

## 14. E1 as built: the effect definitions reader (ticket 4786, 2026-11-20)

W7 of team-0042 (account B), branch `w7-4786` from `epic-0042-combat-movement`. Stage A work (D211, the Architect's
route S note 1.3): the server resolves an effect's name, with its substitutions, before it sends an EffectStart record;
4792's projectile models read the same definitions. Code: `models/include/c5models/effect_xml.h`,
`models/src/effect_xml_reader.cpp`, test `models/tests/test_effect_xml.cpp` (globbed by `models/CMakeLists.txt`, no
CMake change). Inputs of every number below: the install of the `render_game_dir` setting (a pure-VP install, VP 5.4.7
as DLC, Vibrant Textures; read only) and an extraction of its `Resource/Common` archives made with 0320's
`extract_ext.py ... .psb .fxsxml .xml .ftsxml` into `F:/c5-64bit-work/0042/w7/extract` (2,532 files, 8.7 MB; not in
git). The 37 loose definition files of this install have the same names as c5h-b's (0320's). Evidence:
`render/evidence/0042/4786/`. Static EXE reads: the old build's analysis copy (4390913), llvm-objdump 19, addresses
only (D83).

### 14.1 What the reader reads, and the census checked [C, 2026-11-20 04:16, test run]

- **Load list.** Per kind the base file (`Civ5ArtDefines_VisEffects.xml`, `..._VisEffectParticles.xml`,
  `..._VisEffectModels.xml`, `..._EventEffects.xml`; the EXE names them `Assets\Effect\...` and finds them by bare name
  in `EffectData.fpk`), then the files that the packages name in `<VisEffectInfoDefines>`, `<VisEffectParticleDefines>`,
  `<VisEffectModelDefines>` and `<EventEffectDefines>` (at the package root, as in `Polynesia.Civ5Pkg`, or inside
  `<Gameplay>`, as in `Expansion1/2.Civ5Pkg`). On this install: 4 base + 34 package files = **38 files, all found**.
  `SpainInca.Civ5Pkg` and `Babylon.Civ5Pkg` name `*_DLC_02.xml` / `*_DLC_Deluxe.xml`, which are not loose but inside
  `SpanishModels.fpk` and `BabylonianModels.fpk` (section 2, added row). Loose files that no package names are not
  read: DLC_01's four (empty), and the EventEffects files of DLC_04, 05, 06, Expansion and Shared (all empty). Only
  DLC_03 names an EventEffects file (empty too). 25 packages read, none refused by the strict XML reader.
- **Census, checked by the test against sections 0 and 3:** 538 VisEffects, 235 particle and 66 model definitions,
  4 events (`SerialEventUnitCreated`, `SerialEventUnitDestroyedInCombat`, `GameplayFX`, `NaturalWonderRevealed`).
  VisEffects in force with particles 477, models 101, both 42, a child 40, sounds 80, `fMovementSpeed` 11; FOWMask
  written 3 thirteen times and 2 once; particle references per effect none 61, one 409, two 68; 232 distinct `.psb`
  used. All equal 0320's numbers. (The ticket's "538 / 477 / 101 / 40" are VisEffects / with particles / with models
  / with a child, not the four kinds' counts.)
- **Duplicates (correction of section 3):** entries read 641 / 245 / 73 / 4 for 538 / 235 / 66 / 4 names: 103 / 10 /
  7 / 0 duplicate entries, 84 / 10 / 6 / 0 names defined more than once (0320: 92 / 9 / 5, without the six archive
  files, whose names are all defined elsewhere too, so the distinct counts do not change).
- **Bare-name lookup:** every particle definition's `.psb` (235 of 235) and every model definition's `.fxsxml` (66 of
  66) exists by `basename_lower()` of its `File` [C]. One base particle entry repeats its `<File>` with the same value
  (`ParticleDef::extra_files`); the first is used. Three reference elements of the base file have no name (an empty
  `<ParticleEffect>`, two empty `<ModelEffect>`): skipped and counted (`VisEffectDef::empty_refs`), as 0320's count did.
- **Every VisEffect resolves** completely (definitions and files) with `$(TERRAIN)` = `$(BASETERRAIN)` = DIRT and
  `$(WEAPON)` empty: 538 of 538.
- **Trigger names:** over 691 `.ftsxml` (the archives' 666 and the install's loose ones; this install holds VP's too,
  0320 counted 673 on c5h-b) the Effect events are 392 distinct names: **386 defined as written, 4 through a
  substitution, 2 dangling**. The two dangling names are refused by name (`Err::NotFound`, "unknown effect name:
  ART_DEF_VEFFECT_SEAGULLS_01" and "... ART_DEF_VEFFECT_WORKBOAT_INDUSTRIAL_HOIST_SPLASH_01").

### 14.2 The override order [C for the rule, C/I for the package order]

Read in the EXE's loaders (old build): the VisEffect loader 0x4AA4F0, the particle loader 0x79F280 (called from the
model-effect set-up 0x458550, which also calls 0x4AA4F0) and the model definitions through 0x7787C0.

1. **The file list** [C]: the base file is pushed first, then the collector 0x7903B0 (tag name, list, ...) walks the
   global package array (0x2D6FBB4, count 0x2D6FBB8) from its first element to its last and appends each package's
   files of that tag (0x6DB220 per package). The four loaders use the four tag strings above.
2. **Last definition wins** [C for VisEffects, particles and models]: a pre-pass 0x56F570 calls 0x413030 for each
   loaded document in list order; for every `<...ArtInfo>` it interns the `<Name>` and writes (document index, entry
   index) into a map with find-or-insert, unconditionally, so a later occurrence overwrites an earlier one, inside a
   file as well as across files. The main loop then loads an entry only when its (document, entry) is the one the map
   holds (the VisEffect loader also skips a name already in its table). The particle loader calls the same pre-pass
   (0x79F33C), the model path too (0x778851). **Events**: the loader 0x4FCEC0 uses a different pre-pass (0x4104B0),
   not read; no event name is defined twice on the install, so it does not matter today. The reader keeps every
   definition and puts the last in force for all four kinds.
3. **The package order** [C for the append, I for the enumeration]: cvContentPackageManager's scan (0x550F40)
   appends each package that passes its two checks (0x596860, 0x4E42F0) to the array in the order the file search for
   `*.civ5pkg` (0xA39E50, through 0x8187E0 / 0x818BF0, which call the OS find functions) returns them. No sort of the
   array was found: its 18 code sites are the scan, a clear (0x528CE0), indexed reads (0x4F8CE0, 0x64CFC0) and
   iterations (0x505D10, 0x6255C0, 0x4C94F0, 0x5B2DD0, 0x5D7EA0, 0x7903B0). So the order is the search's, which on
   NTFS is folder-name order [I]: DLC_01 ... DLC_07, DLC_Deluxe, DLC_SP_Maps*, Expansion, Expansion2, (VP's
   modpack), Shared, Tablet, UI_bc1, VPUI, VT. The test builds the list in that order (DirectoryProvider's sorted
   walk). **`<Priority>` plays no part in this list** [I from the code read; render's `GameFiles` uses
   (Priority, folder) for the file-system layers, `asset-pipeline.md` 1.2, which is a different mechanism].
4. **What depends on it:** 19 names have copies whose contents differ (`differing_duplicates.out.txt`). For 11 of them
   both candidate orders give the same winner (8 pillage, nuke and trebuchet effects: Shared's copy over the base file;
   `PROJ_ARROW_IMPACT`: Expansion2's over the base; `ATLATL`: the two possible winners have the same content;
   `PEFFECT_FIRE_NEBUCHADNEZZAR_01`: DLC_Deluxe's over the base). **The 8 `ART_DEF_VEFFECT_SLING_ROCK_IMPACT_*`
   differ**: with the folder order Expansion2's copy wins, with a priority order (DLC_07 = 300 > Expansion2 = 200)
   DLC_07's would. The reader follows the read order; a live check would settle it cheaply: the EXE logs "Loading
   VisEffect %s from %s" (0xA31B40) for each definition it keeps, if its log is on, or stage A's spike 4798 can start
   one sling impact.

### 14.3 Substitutions [C for the names, I for the EXE's choice of values]

- The reader replaces `$(TERRAIN)`, `$(BASETERRAIN)` and `$(WEAPON)` with values the caller sets
  (`FxSubstitutions`); any other `$(...)` and a variable without a value are refused with `Err::NotFound` naming it; an
  unterminated `$(` is `Err::Xml`. The library never picks values (the server knows the plot and the weapon).
- **`$(WEAPON)` can be empty**: the base file defines `ART_DEF_VEFFECT_ARMOR_DEATH_SM_` and `..._MD_` beside the
  `_BLUNT` and `_METAL` variants. The four trigger names with a variable: `ARMOR_DEATH_SM_$(WEAPON)`,
  `ARMOR_DEATH_MD_$(WEAPON)` (variants empty, BLUNT, METAL), `GUNSHIP_AIRWAKE_$(BASETERRAIN)` and
  `MECH_FOOT_DUST_$(TERRAIN)` (DIRT, LEAVES, MUD, SAND, SNOW, WATER each).
- **The `ProjectileEnd` children with `$(TERRAIN)`** (9 names) do not exist for every surface: CATAPULT_ROCK_IMPACT
  7 (no CITY), RAILGUN_IMPACT 7 (no CITY), GUIDED_MISSILE / SLING_ROCK / TANK_IMPACT 8 (AIR, CITY, DIRT, LEAVES, MUD,
  SAND, SNOW, WATER), and FIERY_CATAPULTROCKHIT, FIRE_ARROW_IMPACT, MECH_ROCKET_IMPACT, ROCKET_ARTILLERY_IMPACT only 2
  (CITY, DIRT). So a fire arrow's child on snow has no definition: the reader refuses it by name. **What the EXE does
  then (a fallback to DIRT, nothing, or another value) is not read** [I]; 4793 must choose and should check it on
  stock with stage A's entry. `EffectLibrary::variants(pattern)` lists the values that exist.

### 14.4 The interface for 4793 and 4792

- `parse_package_fx_defines()` per `.Civ5Pkg` (in the package order of 14.2), `fx_load_list()`, then
  `EffectLibrary::load(FileProvider&, list)`. The server's FileProvider is the FPK reader of `render/` (`GameFiles`)
  behind the model library's `FileProvider` interface, as for the art resolver; the test uses `DirectoryProvider`
  over the extraction and the install's `Assets`.
- `resolve(files, name, subs, out)`: the definition in force, its particles (`.psb` by bare name), models (`.fxsxml`
  by bare name, then `resolve_fxsxml()` for the mesh and textures), children after the same substitution. Refusals
  (`Err::NotFound`, naming it): unknown effect name, a particle or model reference without a definition, an undefined
  child. A missing `.psb` / `.fxsxml` is a problem in the result, not an error.
- `EffectLibrary::duration(def, animation_seconds)`: the rule of section 1. `definitions()` / `duplicated()` give every
  copy of a name with file, package, entry and line.
- Not built here: the particle parameter half (stage B), any rule that picks a terrain or weapon value.

### 14.5 Tests and commands

- `python -I models/build.py --target test_effect_xml` (clang-cl, /W4 /WX) and `python -I models/build.py --asan
  --target test_effect_xml` (cl.exe /fsanitize=address); run with `C5R_GAME_DIR` = the `render_game_dir` setting (or
  `C5M_FX_INSTALL`) and `C5M_FX_EXTRACT` = the extraction, through `quietlock.py run`; the ASan executable needs the
  MSVC ASan runtime folder (`VC/Tools/MSVC/<ver>/bin/Hostx64/x64`) on PATH (without it the run exits 127 with no
  output). Without the two variables the real part prints SKIP and the test exits 77.
- Results [C, 2026-11-20]: plain **876 checks, 0 failures**, exit 0 (04:16); ASan **876 checks, 0 failures**, exit 0
  (04:22, no ASan report). Outputs: `render/evidence/0042/4786/test_effect_xml.{plain,asan}.out.txt` (the transform
  lines of the two worked examples removed: game data, D83); `differing_duplicates.py` and its output for 14.2 item 4.
- Negative cases (synthetic documents): malformed XML, UTF-16, wrong root per kind, a definition without Name / File /
  EventName, numbers that do not parse (Scale, Translation attribute, fBaseDuration, fMovementSpeed `inf`, fScale,
  Animation `1.5`), FOWMask 4 and text, a package without the `Civ5Package` root or unclosed; a listed file missing
  (recorded, skipped), a malformed listed file (load fails naming it, library cleared), all four base files missing;
  unknown effect name, unknown particle / model / child reference, a child needing a value not given, an unknown or
  unterminated variable; the override order in both package orders and inside one file.

## 15. E8 as built: a fight's effects into the trigger stream (ticket 4793, route S stage A, 2026-11-20)

W22 of team-0042 (account B), branch `w22-4793` from `epic-0042-combat-movement` with `w15-4792` and `w16-4722` merged
in (both unmerged to the team branch when this was written). Stage A (D211; the Architect's route S note 2.1 and 2.3):
every effect start that belongs to a fight leaves the server as an EffectStart / EffectMove / EffectStop record of
`shell/common/vp_trigger.h` (4721), written by 4722's emitter, and the EXE's Particle system plays it through the shell's
player (4798's records 0x28 / 0x29 / 0x2A, section 8.1). This section is the **server side, offline**; the shell's player
is not built (it waits for this side; 4725 is written, see 8.1.1 and `combat-choreography.md` 3.5.8). Marks: `[C]`
checked by this worker in code, tests or the install's files read at run time, `[C DLL]` read in the DLL's source,
`[I]` this section's design choice or reading. Nothing from the EXE or the game's files is committed (D83); no EXE address
is used.

### 15.1 The routes (effects-formats.md 6, the starts that belong to a fight)

| Route | What the server sends | Code |
|---|---|---|
| Clip `FTimedTriggerEffect` of a fighting figure (attacks, charges, deaths, victory: 4722's ownership table) | the name resolved by 4786's library (15.2), then 4722's emitter as before: EffectStart at the trigger's bone (root until the host gives bones), lasting ones (duration > 0 with a bone) follow it with EffectMove and end with EffectStop | `EffectRoutes::intake` in front of `TriggerEmitter::triggers` |
| The projectile in the hand: an Effect row that a Transfer row names by `refid` (579 of the 603 Transfer rows, section 6) | started by the routes through the emitter's API (EffectStart, follows), its handle kept per figure; at the Transfer the flight (4792's `ProjectileSet`) takes **that handle** (no second EffectStart: the EXE's own hand-over 0x2B keeps one instance, `effects-server.md` 1.4); not taken within its trigger's duration (0.5 s when it has none [I]) it is stopped | `startHeld`, `transfer`, `addFlight` |
| A unit's projectile flight (4792) | EffectMove each frame with orientation and velocity; at the landing the `ProjectileEnd` children and the weapon's `HitEffect` as **one-shot** EffectStarts at the end point; no stop at the landing (the EXE posts none); a fight's end with the flight in the air: EffectStop, impact dropped (note 1.5 item 4) | `EffectRoutes::pass` (the 1:1 adapter, W16 #8010; W15's open item) |
| A combat hit (4777's `CombatHit`) of a weapon without a flight (`projectile` false: fighter and gunship machine guns, anti-air) | its `HitEffect` (`$(TERRAIN)` of the target's plot) as a one-shot at `hit.pos` at `hit.time`, scale `HitEffectScale`; more than 0.1 s late: not sent (note 1.5 item 1) | `planHitEffect` -> `applyHit` |
| A city's strike (`CombatHit::city` and `projectile`): the era's bombard tag (`Eras.CityBombardEffectTag`) | the tag is a projectile VisEffect [C, the install's base file: `CITY_BOMBARD_EARLY` = the archer's arrow model + `STR_ARROW_TRAIL_03` + child `PROJ_ARROW_IMPACT`, `_MID` child `CATAPULT_ROCK_IMPACT_$(TERRAIN)`, `_LATE` child `TANK_IMPACT_$(TERRAIN)`; `fBaseDuration` 0.01 / 0.01 / 0.1]: flown by 4792's `ProjectileSet` from the city's centre (+10 units [I]) on 4792's arc with a 45-degree launch [I] to `hit.pos`, from `hit.launch` to `hit.time` (the core's `city_flight` 0.6 s), children with the target's surface; owned by the fight's unit (a city is no unit of the emitter: the record names the target unit, R7) | `planHitEffect` (CityVolley) |
| A city's answer in a melee (`city`, not `projectile`) | nothing [I: what stock shows there is not read] | `HitRoute::CityDefence` |
| An area share (a nuke's members, a garrison) | nothing: the blast's effect rides on the first hit (4777) | `HitRoute::None` |
| The DLL's `GameplayDoFX` | **no route: it is never called in a fight** [C DLL: the only callers are `CvNotifications.cpp` 313, 317, 791, 863, 881, 1142 and `CvMinorCivAI.cpp` 15852; none in `CvUnitCombat.cpp` or `CvUnit.cpp`, fork checkout 2026-11-20] | - |
| Not a fight's (note 2.3 item 4): city damage smoke, pillage smoke, landmark states, the paradrop | nothing: they stay the EXE's (Particle stays on) | - |

The hit's sound (`CombatHit::sound`): sent as a one-shot SoundStart only when it names a known 3D script (a city's
`city_sound`); a unit weapon's value is its sound tag (`WeaponTypeSoundOverrideTag` / `WeaponTypeTag`), a `$(WEAPON)`
value of the clips' own Sound triggers, not a script, so it is counted (`hitSoundNotScript`) and not sent [I].

**The projectile's speed (W15's open item, `effects-server.md` 2.5 item 2):** `applyEffectSpeeds(CombatArtTables&, member
trigger files, names)` sets a member's projectile weapons' `speed` to the projectile VisEffect's `fMovementSpeed` when it is
above 0 (the EXE's rule, `effects-server.md` 1.1 item 3: the catapult's rock 3.3 over its NULL row's 2.0). It is a
library call; **the call belongs where the server builds its `CombatArtTables`** (4774's `AnimRunner` after
`loadCombatTables`, with the members' trigger files from the controller): not in this ticket's touches, named for that
owner.

### 15.2 Names (4786's library with a fallback)

`c5r::effects::EffectNames` (`render/include/c5r/effects/effect_routes.h`): `$(TERRAIN)`, `$(BASETERRAIN)` (the terrain's
value when only that is given) and `$(WEAPON)` from the figure's values (the emitter's `Substitutions`). Results:
`Exact`; `Fallback` (the surface's variant is not defined, or no surface is known: **DIRT** [I: the EXE's choice is not
read, 14.3], counted per resolved name); `WeaponEmpty` (no weapon value: the empty variant, which the base file defines for
`ARMOR_DEATH_*`); `NotDefined` (the 2 dangling trigger names, or no variant at all: refused by name, **never sent**);
`BadName` (an unknown variable, an unterminated `$(`). A weapon value whose variant is not defined is refused, not
fallen back. On the install (the `render_game_dir` copy, pure VP 5.4.7 as DLC, 2026-11-20) [C, `test_effect_routes`
install part]: all 538 VisEffects resolve as themselves; every `HitEffect` family of the database (the 7 `$(TERRAIN)`
families and `NUCLEAR_BOMB_01`) resolves on all 8 surfaces (DIRT, SAND, SNOW, MUD, LEAVES, WATER, CITY, AIR): 64 of 64,
5 of them by the DIRT fallback (`ANTIAIR_IMPACT` has only DIRT, CITY and AIR: SAND, SNOW, MUD, LEAVES and WATER fall
back); `FIRE_ARROW_IMPACT_$(TERRAIN)` on SNOW falls back to DIRT, `TANK_IMPACT_$(TERRAIN)` on
SNOW is exact; `SEAGULLS_01` and `WORKBOAT_INDUSTRIAL_HOIST_SPLASH_01` are refused.

### 15.3 For the shell's player (stage A executor; the proposal; built by ticket 4727 as 15.7) [I]

- EffectStart without `FOLLOWS` -> record 0x28 (owned one-shot, the trigger's own type in stock); with `FOLLOWS` (a
  flight, a held projectile, a lasting effect on a bone) -> 0x29 (owned, lasting; stock's flight posts 0x29 every frame).
  EffectMove -> the same type again with the same owner key and name (B2: same key + same name = move). EffectStop ->
  0x2A. OwnerEnd sends nothing for effects: the server writes the stops of its lasting effects before the OwnerEnd, and
  one-shots ring out (the emitter keeps no one-shot: `StartSpec::oneShot`, below).
- The owner key: one value per (generation, handle) with a tag in the second 64-bit word (no stock writer's key has one,
  8.1); the name hash is the CRC of 8.1 over the record's resolved name; the matrix from `pos`, `orient` (quaternion) and
  `scale`. Whether 0x28's "free at the end of its duration" cuts a moved instance, and the definition's transform times the
  record's matrix, are the live check's (8.1 "Not verified").
- On the EXE's main thread at frame F's insertion, with 3.5.8's form (no allocation, no file I/O, no `std::string`
  growth; the proxy's `_SECURE_SCL 0` / `_HAS_ITERATOR_DEBUGGING 0` before any STL header, 4725) and the queue's
  addresses by name through the build table (D202).

### 15.4 As built (files)

- `render/include/c5r/effects/effect_routes.h`, `render/src/effects/effect_routes.cpp` (in `c5render`, globbed):
  `EffectNames`, `HeldEffects` (Transfer `refid` -> Effect rows per trigger file), `planHitEffect` (routes `None`,
  `Flight`, `OneShot`, `CityVolley`, `CityDefence`, `Refused`), `applyEffectSpeeds`.
- `server/src/effects/srv_effect_routes.{h,cpp}`: `srveffects::EffectRoutes` in front of 4722's emitter: `beginFrame`,
  `fightStarted`, `intake` (names; held starts; Transfers through a `FlightPlanner` the host gives: 4792's `planLaunch`
  with the core's target), `hits` (applied at their moment, later frames included), `addFlight`, `ownerEnds` (a fight's
  flights removed with their trails, its pending hits dropped), `endFrame` (flights stepped, their records to the
  emitter). Trigger files by name (`setTriggerFiles`: the controller's `LogicCache::triggers`, read once each).
- The emitter hunk (lead-0042 #8138, its own commit): `StartSpec::oneShot`: a one-shot start is not kept, so its owner's
  end sends no EffectStop that would cut an impact still ringing out (note 1.5 item 2), move and stop refuse its handle,
  and more than `lateDrop` late it is refused (counted `droppedLate`).
- `server/tests/effects/trigger_scene.cpp` / `run_trigger_scenes.py` / `build_trigger_scene.py` (4722's tool): `--effects`
  runs the routes in front of the emitter on 0557's fights (the install's library, the controller's trigger files, the
  weapon rows, a stand-in planner: the held effect's point to the opponent's root on 4792's arc with the EXE's time rule;
  the core's own target point is not exposed at the Transfer moment) and prints an `effects:` line; default off (4722's
  runs unchanged).
- Not wired into server.exe (hunks asked of lead-0042, 15.6): `AnimTriggerTap` carrying the routes, the core's hits from
  `ControllerFightPlayer`, the `CombatArtTables` speed call, the bones (4722's `FigurePositions::boneTransform` and
  `figureScale`, which the host does not fill yet: records start at the figure's root with identity orientation).

### 15.5 Tests and commands

Workstation, 2026-11-20, branch `w22-4793` after the merge of the team branch (8eeb012e2), build trees on F:
(`render/build.py --build-dir <F:>/build/rel|asan`), the install of `render_game_dir` (pure VP 5.4.7 as DLC) and 4872's
database copy, every run through `quietlock.py run` [C: these runs]:

| What | Command | Result |
|---|---|---|
| `c5r_effects_test_effect_routes` (render; synthetic library + install part) | `render/build.py --target c5r_effects_test_effect_routes --test -R c5r_effects_test_effect_routes`, and with `--asan --mem-cap-mb 4096` (the `c5r_asan_canary` target built first) | plain 1 of 1 passed (5 tests, 0 failed checks); ASan passed, **0 ASan reports**, peak 222 MB |
| `server/tests/effects/run_effect_routes_test.py --out DIR --render-build RBUILD [--asan --render-asan-build RBUILD_ASAN]` (routes + emitter + fight clock linked with `c5render.lib` / `c5r_models.lib`) | | plain **260 passed, 0 failed**; the injected failure exits 1; ASan 260 / 0, canary caught, peak 62 MB |
| 0557's 19 recorded melee fights through 4775's core, the fight clock, the controller and the emitter with the routes | `server/tests/effects/build_trigger_scene.py --out DIR --c5render RBUILD`, then `run_trigger_scenes.py --exe DIR/bin/trigger_scene.exe --game ... --db ... --work ... --effects --effects-csv ...` | **`RESULT PASS 19/19`**, every case `effects: OK` (below); the MSVC ASan build of the tool (`--asan --c5render RBUILD_ASAN`): **`RESULT PASS 19/19`**, totals identical record for record count, every case exit 0 (an ASan report aborts the case) |
| `test_scene_units` (4774's suite; H1 / H2 compile into it) | `server/tests/units/build_units_tests.py --out DIR --c5render RBUILD`, run with `C5R_GAME_DIR` / `C5R_EXE` under `memcap.py` | **67 of 67 cases passed** (plain; not run under ASan here) |

0557's 19 fights with `--effects` (totals; counts only, the logs with names stay on F: outside git): records 5,006 (the
same stream checks as 4722's: 0 loops unstopped, 0 records after their owner's end, 0 of an EXE clip); **EffectStart 450**
(241 following), EffectMove 355, EffectStop 241; **0 names undefined** in the library, 0 EffectStop of a handle never
started; clip Effect triggers 411, all `Exact` (no fallback, no refusal: the scenes stand on DIRT); **held 28, taken by a
flight 25, expired 3** (the ships' arrows in s-n1 / s-n3: 3 Transfers found no held effect waiting, `noHeld` 3, the hold
of a second volley replaced or outlived [I]); flights 25, each one EffectStart (the held one) and its moves, **25
`ProjectileEnd` children** at the landing, 0 forced ends, 0 refused records; hits 127: **20 one-shots** (weapons with a
`HitEffect` and no flight, in s-i3 9, s-d2 4, s-d3 2, s-n2 4, s-c3 1), 107 without an effect name, 0 city volleys (no
recorded city strike, 4777 8.4); hit sounds: 20 weapon tags, none a script (none sent).

### 15.6 Not verified, open

- Nothing ran with the game, the proxy or a shell player: the records are checked offline only (4721's contract check,
  4722's stream check, a shell-side read of the section).
- Bones: the server's pose source has no bone query; until the host fills `boneTransform`, stage A's effects start at
  the figure's root (feet) with identity orientation, so a muzzle flash or the arrow in the hand is not at its bone.
- The DIRT fallback for a missing surface variant, the city volley's launch (height, angle) and the city's answer in a
  melee (nothing) are [I]: each needs one look at stock (a stage A live check, or a stock capture).
- The flight planner in the server (4792's `planLaunch` with the core's target figure and hit / miss at the Transfer
  moment) is not built; the scene tool's planner is a stand-in (the opponent's root, no random offset).
- The server.exe wiring (lead-0042's yes needed, files not in this ticket's touches): H1 `srv_trigger_tap.{h,cpp}` (the
  tap carries the routes), H2 one line in `srv_units_anim.cpp` (`ControllerFightPlayer::advance` hands the core's hits to
  the tap), the `applyEffectSpeeds` call after `loadCombatTables` in `AnimRunner`.
- `$(WEAPON)` of a victim's Sound triggers (the attacker's weapon in stock?) is 4722's, not read here.

### 15.7 The shell's player as built (ticket 4727, effects stage A, 2026-11-20)

W26 of team-0042 (account B), 2026-11-20 14:15-15:30 EST, branch `w26-4727` (epic-0042-combat-movement with `w19-4723`
and `w22-4793` merged in). The shell side of 15.3, after the pattern of 4723's sound executor
(`unit-combat-server.md` 10). Marks: `[C]` checked by this worker in code, tests, the generator or byte reads of the old
build's golden copy (named per item), `[L]` live, `[I]` a design choice or reading. Offline so far: no instance (the
live smoke is 15.7.6). No EXE byte listing and no address of build 26577625 is written anywhere (D83, D202).

#### 15.7.1 What it is

- `proxy/src/proxy_effect_player.{h,cpp}` (new). Three parts as 4723's: the **core** (`Attach`, `Step`, `Detach` on a
  `Poster` of one plain function pointer, `post(kind, payload)`) reads the trigger stream up to the frame shown and keeps
  every effect under its handle with its owner (fight = owner / fightSeq; figure; clip instance) in a fixed table of 512
  effects and 64 fights; the **binding** (`PosterOfExe`) is the only reader of the build table; the **glue** (`Init`,
  `FrameHook`, `SetServerFights`, the D118 report of stage `effect-player`) is the only part that needs the proxy runtime.
  No STL in the core and the binding; no allocation, file I/O or wait on the frame path; nothing throws out of the hook
  (`combat-choreography.md` 3.5.8 points 1-6).
- **The hook** (lead-0590 #8368, one hunk in `proxy/src/proxy_ui_bridge_rt.cpp` `Init` plus its forward declaration):
  the plain frame slot is one chain, `ProxyEffectPlayer::FrameHook` -> `ProxySoundExec::FrameHook` ->
  `ProxyCombatHold::FrameHook` -> `ProxyUiDraw::OnFrame` (when on), each called once per drain. Off (the product today):
  one test of a word and return.
- **On** only with route S's server fights (`ProxyEffectPlayer::SetServerFights(true)`, which 4782/4795 calls with the
  hold's and the sound executor's), or the TEST ONLY variable `VP64_TEST_EFFECT_PLAYER=1` of the EXE's process. The
  binding is checked at once; the attach (`<base>.Trig` and `<base>.Frame` mapped read and write, though the player
  stores nothing: a read-only view crashed r3, 15.7.7) at the next frame.
- **One ring, two readers** [I, a design choice]: the section's reader block (readSeq, readerPid, the counters) is the
  sound executor's; the player keeps its own cursor and never writes the section (checked by the test: the reader block's
  128 bytes unchanged over a step). So R3's back-pressure follows the sound executor's reading only; the player counts
  records it finds overwritten (`lost`) and its watchdogs end what they would have ended. Both read at the same frame in
  the same drain, so neither lags the other by more than a frame.
- **The frame shown**: 4723's rule (min of the publish block's frame id and the ring's `latestFrameId`), a copy of its
  12-line reader.

#### 15.7.2 The EXE entries: three record allocators [C, byte reads of build 4390913's golden copy, 2026-11-20 14:20]

Instead of writing the queue's header by hand, the player calls the EXE's own allocator of each record type (8.1's
"calling the allocators is equivalent"):

| Row (`exebuild/symbols/proxy.tsv`) | Anchor | What |
|---|---|---|
| `effect_alloc_oneshot` | 0x846030 | allocates one 0x80-byte record of type **0x28** on the 4 MB queue and returns its payload |
| `effect_alloc_lasting` | 0x866AB0 | the same for type **0x29** |
| `effect_alloc_stop` | 0x866A50 | the same for type **0x2A** |
| `effect_oneshot_call` | 0x4CE52D | the call of `effect_alloc_oneshot` in the model's timed-trigger start 0x4CE510 (E8 at 0x4CE53D) |
| `effect_lasting_call` | 0x745755 | the call of `effect_alloc_lasting` in the projectile's frame step 0x7454E0 (E8 at 0x745765) |
| `effect_stop_call` | 0x754336 | the call of `effect_alloc_stop` in the pillaged farm's smoke 0x754330 (E8 at 0x754349) |

- **Behaviour of each allocator** [C]: no argument; reads the channel index (`qg_channel_index`), takes the channel
  (`qg_channels` + index * 0x800180) and its current buffer (flip counter at + 0x800100, bit 0, buffers 0x400080 apart),
  reserves 0x80 bytes by `InterlockedExchangeAdd` through the EXE's import slot (`qg_iat_interlockedexchangeadd`, the
  slot the queue guard of ticket 0492 replaces, so a full buffer is diverted to the guard's scratch and the record is
  dropped, never written past the end), writes the header `[0x80][type][0][0]` and returns the record + 0x10 in EAX; a
  plain `ret`. ESI is saved; EAX and ECX are changed. Declared `typedef void*(__cdecl* VpEffectAllocFn)()` (no stack
  word); `effect_player_abi_test.cpp` calls it on a stand-in with the same shape and `ret`: ESP unchanged, the positive
  control (`ret 4`) seen as +4 [C].
- **Look-alikes** [C]: the three bodies are the same 0x53 bytes but for the type immediate. So each pattern row is the
  **whole body** with the three queue operands as `R(qg_channel_index)`, `R(qg_channels)`,
  `R(qg_iat_interlockedexchangeadd)` and the type byte as a literal (a swapped carry fails at that byte), and each has a
  caller row whose `E8 C(effect_alloc_*)` must lead to it (lead-0590 #8203 / #8368's rule for short look-alikes,
  `../02-exe-internals/exe-build-table.md` 8 step 4). The three callers are the stock writers 8.1 names (the one-shot
  timed trigger, the projectile's flight, the pillaged farm's smoke) and fill the payload as the player does: the
  flight copies its 16-dword matrix into the record (`rep movsd` of 0x10 dwords), the farm smoke writes its key from
  its object + 0x88 / + 0x8C with the second 64-bit word 0, and posts 0x29 with its key sign-extended from the object's
  address (`cdq`).
- **The payload the player writes** (8.1's layout; one `memcpy` of 0x70 bytes under `__try`): 0x28 / 0x29: +0x00 the
  world matrix, D3DX row-major (rows = rotated basis vectors, translation at +0x30/+0x34/+0x38, +0x3C = 1); +0x40 the
  owner key; +0x50 the name hash; +0x54 the scale; +0x58..+0x6F zero. 0x2A: +0x00 the key, the rest zero. The player
  reads no EXE structure: nothing of the effect manager, the view or the Particle system is read.
- **The owner key** [I]: words (handle, generation, 0x00004727, 0x56503634 'VP64'): the second 64-bit word is never 0 and
  never a sign-extended small index, the two forms every stock writer read uses (8.1 and the farm smoke above), so no
  stock key equals one of the player's; a new generation gives new keys.
- **The name hash** [C]: CRC-32 of 8.1 computed in the proxy (`NameHash`, a static table), checked by the test on 8.1's
  value (`ART_DEF_VEFFECT_PARADROP` -> 0x26800651); once per name index per generation, cached.
- **The matrix** [C for the formula against D3DXMatrixRotationQuaternion's layout; I for the server's convention]: the
  record's quaternion (x, y, z, w) normalised (zero, huge or NaN: identity); the server sends identity until its hosts
  fill `boneTransform` (15.6), so the orientation is not exercised by today's records.

#### 15.7.3 The rules as built [C: proxy/tests/effect_player, 121 checks of the core since 15.7.7's regression test]

| Rule | As built |
|---|---|
| R2 frame rule | at frame G every unread record with frameId <= G in seq order, at most 1,024 per frame |
| Start | FOLLOWS -> 0x29 (lasting), else 0x28 (one-shot); a one-shot more than 3 frames before G and a lasting start more than 90 frames (3 s at the cap) before G are dropped (`stale`); a start of an ended fight is refused (`after end`); a name that is not a published effect name is refused (`unknown name`, LOUD once per name) |
| Move | the start's type again with the same key and hash (B2); several moves of one effect in one frame are posted **once**, at the end of the step, with the newest pose and the start's scale; a move of an unknown handle is counted |
| Stop | 0x2A with the key; a move and a stop of one effect in one frame post only the stop; a stop of a one-shot (a trigger with a duration and no bone) is posted too (its key may still hold the instance) |
| Fight OwnerEnd | the fight's lasting effects still known are stopped (`orphan`: the server writes their stops before the end, so this is a guard); its one-shots are forgotten without a post (the EXE frees them at their duration's end) |
| ClipEnded | the same for that (player, unit, figure, instance); the fight goes on |
| Watchdog | a fight with no record for 22 s: its lasting effects stopped (LOUD) |
| Cap | a lasting effect alive 25 s (the 20 s limit + 5 s) is stopped whatever its fight sends (LOUD once): the repeating-sound lesson, no orphan |
| Forget | a one-shot 10 s after its last record leaves the table, nothing posted |
| R6 | a new generation or a stopped server (magic 0): every lasting effect stopped, everything forgotten, the name cache dropped; the first generation after the attach is a join |
| Full | a 513th effect or a 65th fight is not started (LOUD once) |
| Faults | a fault in an allocator call or the payload write (each under `__try`) turns the player off for the proxy load: LOUD, the D118 report (`effect-player-fault`) |

#### 15.7.4 Tests and commands [C, workstation, 2026-11-20]

| What | Command | Result |
|---|---|---|
| Core, ABI, ASan, recorded fights | `python -I proxy/tests/effect_player/test_effect_player.py --scratch C:/c5-64bit-work/0042/w26/t4727 --log <F:/c5-64bit-work/0042/w22/scenes-e1/*/triggers.log, 19 files>` through `quietlock.py run` (14:44) | **50/50**: core 113/113 on the proxy's toolchain (clang-cl -m32, VC9 headers) and under MSVC /fsanitize=address (0 reports, the canary caught), the `--expect-fail` negative, ABI 4/4 and its negative, the 19 logs 19/19 plain and 19/19 ASan |
| 0557's 19 fights with 4793's effects (W22's `trigger_scene --effects` logs, names from the install, outside git) | the same run | totals: 450 EffectStarts (241 lasting), 355 moves, 241 stops; **1,046 posts** (0x28 209 and 0x29 596, starts and moves together, of them 355 moves; 0x2A 241); 0 unknown names, 0 stale, 0 after end, 0 unknown handles, **0 orphan stops** (the server stopped every lasting effect itself), 0 lasting keys posted after their stop, 0 lasting effects left; one post per start. These are W22's totals of 15.5 record for record |
| gen_table (main's 2b), both commands of `../02-exe-internals/exe-build-table.md` 6 | `C:/c5-64bit-work/0042/w26/gen.sh` (14:30) | identity exit 0, carry to 26577625 exit 0: **259/259 patterns OK on both builds**, 403/403 symbols (26577625: 23 hand, 358 high, 22 medium; the six rows all high: the allocators by 96-byte windows, the callers by 16-byte windows, 1 vote each); nothing generated committed |
| Proxy build (the only builder that compiles `proxy/src/*.cpp`; `server/build_server.py` calls it) | `python -I proxy/build_proxy.py --out C:/c5-64bit-work/0042/w26/proxy-a --src <git archive of the pin 1fd51353e> --config release` through quietlock (14:47) | `build_proxy: OK in 66 s`, proxy sha256 0205741a..., purity test passed for these inputs |
| After merging main (0609, 0606, stage 3; 445b48326), 15:15-15:22 | the same gen_table commands, build (`--out .../proxy-b`), census and suite | gen_table both exit 0 (259/259, 403/403); `build_proxy: OK in 79 s`, proxy **d43e366d**; census: the player's TU at 0; suite 50/50 |
| STL settings (4725's census) | `proxy/evidence/0042/4725/scl_census.py` and `comdat_overlap.py --scl1 proxy_budget,proxy_farm_guard,proxy_luaexec,proxy_shell` | the player's TU at `_SECURE_SCL=0` with 31 others; 4 TUs of other tickets at 1 (ticket 0609's); **0 std:: functions defined by both settings** |

#### 15.7.5 Build 26577625 (D214)

*Added 2026-11-20 (ticket 4783, MC #8543):* the player is now armed by route S's common state (`proxy_route_s.h`,
`unit-combat-server.md` 16) at the server's question, together with the sound executor and the hold; when it refuses
(any build) or stops, NONE of the three is armed, the fights play stock, and its D118 reason goes into route S's one
report (`ProxyEffectPlayer::SetReportSink`) instead of a popup of its own.

**Current (2026-11-20 16:20, W26):** ticket 0618 (w5-0590; `../02-exe-internals/new-exe-build-26577625.md` C.6.4 on
epic-0590's branch, lead-0590 #8510, MC #8501) read the player's whole path on 26577625 unchanged [C: the allocators,
the callers, the key map, the stop and the listeners]. So `kLayoutVerified` lists **both builds** since commit after
3fccf9cdb; the player arms on 26577625 too. Its own live start and stop on a 26577625 instance are still owed (and the
player was seen to draw on 4390913 on 2026-11-20 by 15.7.8; not yet on 26577625). The text below, written before 0618, is kept as the
record of the refusal path, which now applies only to a third, unknown build. (Replaced: "So `PosterOfExe` refuses that
build (`kLayoutVerified` lists 4390913 only)".)

- **Carry cross-check** [C, gen_table above]: the three allocators' full-body patterns pass on 26577625, so their bodies,
  the three queue operands and the three type bytes (0x28, 0x29, 0x2A) are the same there; the three caller rows' `C()`
  tokens pass, so the carried stock writers call exactly the carried allocators (a swap of the look-alikes would fail
  both the type byte and the call target).
- **Not read on 26577625**: the payload layout the Particle system's handlers read (8.1: matrix, key, hash, scale), the
  key map's "same key and name = move" and the stop, and the hash. So `PosterOfExe` refuses that build
  (`kLayoutVerified` lists 4390913 only), as 4723 refuses it for the FSound layout. Lifting it needs a read of the three
  handlers (8.1's listener of types 0x28 / 0x29 / 0x2A and their job functions) on that build: ticket 0618's list
  (posted on thread 0042 #8408).
- **What the player sees there** [I, from the code paths]: under route S, `SetServerFights(true)` answers false with one
  LOUD line in vpproxy.log and the D118 report (stage `effect-player`, code `effect-player-bind`, non-fatal: the helper's
  popup "The patch cannot show the fights' effects on this game ... the game goes on without projectiles and hit effects
  in the fights"). 4773's hold refuses the same build (`combat-choreography.md` 3.5.7) and so does 4723's executor, so
  route S does not run there at all: the EXE shows fights as with its Combat system off, and the server's fights have
  neither sound nor effects. The caller (4782/4795) should arm the three together and report once, or the player sees up
  to three popups [I: the caller is not built]. Without route S (the product today: Combat on) the player is off on both
  builds and nothing changes: the EXE draws its own projectiles and hits.

#### 15.7.6 Not verified, open

- **The live smoke** (MC-B's grant 15:21 through lead-0042: c5h-a on C:, 15:25-15:50, one windowed launch; method
  written 15:30 before the result): `proxy/evidence/0042/4727/chain_4727.sh c5h-a r1` with `W26_4727_ROOT`,
  `W26_LAYOUT`, `W26_TINY` on C: (no F: input during lead-0454's rule A run, #8300); layout = W19's 4723 layout without
  .pdb files (server e85af4a5, composite 4a2ad069, x64 DLL 78355a75) with proxy d43e366d; arm h5;
  `VP64_TEST_EFFECT_PLAYER=1` and `VP64_TEST_SOUND_EXEC=1`; the driver plays the server's part of `.Trig` (W25's 4728
  server not ready): three 0x28 hits at the capital and beside it, a 0x29 projectile 120 units up moved 600 units east
  in 3 s, its stop, the fight's end. Result: below (15.7.7). The questions it answers are 8.1's: the draw at the record's matrix, the
  0x28 lifetime (freed at its duration's end, a moved one-shot), the stop's look, the fog test with the EXE's FOW key off,
  the definition's transform times the record's matrix.
- The order of the drain and the 4 MB queue's pump within one EXE frame: a record posted in frame N is dispatched in the
  first pump after it (8.1); whether that is the same frame was not measured.
- The Particle system's busy bit (8.1: records deferred while its job runs) and its replay moment.
- Bones: until the server's hosts fill `boneTransform`, every effect starts at the figure's root with identity
  orientation (15.6); the player passes on whatever the record carries.
- A stream written by server.exe itself (TriggerStream with the routes, 15.6's H1/H2 wiring): the player has run on
  recorded logs only.

#### 15.7.7 Live smoke on c5h-a (2026-11-20 15:31-16:01 EST) [L where marked]

Configuration as 15.7.6 (c5h-a on C:, Goldberg, EXE stamp 0x546CD0A8 checked, claim w26-0042 15:26, released 16:03).
Run folders `C:/c5-64bit-work/0042/w26/s4727/live-r1..r4`, pictures `.../s4727/pics/h5` (DIAGNOSTIC, D112, outside git).

- **r1, r2: no launch.** r1's prep stopped at `provision.py set-proxy --pdb` (the C: layout copy had left out the .pdb
  files); its restore then failed on the missing `vpproxy.ini` (set-proxy had not run) and left the run's save copy
  `C5H-4727-TINY_0018` in ModdedSaves, so r2's prep refused ("exists already"). The 15 backed-up instance files were
  checked equal to r1's backup by SHA-256 (nothing had been changed), the save copy (this ticket's own, SHA-256 equal to
  the source save) was removed by hand. Lesson for a C: layout copy: the harness's set-proxy needs every .pdb.
- **r3: crash without a box (the player's defect) [L for the record, C for the code].** Proxy d43e366d. 54.11 s after
  the proxy's start "effect player: attached", then at 15:40:35.061 exception 0xC0000005, **write** to 0x0AED0438 at
  proxy+0x6138A on the main thread (the proxy's boot thread), no box; the process ended. The address symbolizes (the
  build's .pdb, llvm-symbolizer) to `VPTrigLoad64` (vp_trigger.h 272) <- `FrameShown` <- `ProxyEffectPlayer::FrameHook`.
  **Root cause** (COMMON 30): the glue mapped `<base>.Frame` and `<base>.Trig` with `FILE_MAP_READ`, to leave the
  sound executor's reader block alone; but on x86 `VPTrigLoad64` is `_InterlockedCompareExchange64(p, 0, 0)`, a
  `lock cmpxchg8b`, which is a write access even when the value is unchanged, so the first 64-bit load faults on a
  read-only page. The faulting address is the Frame view's base + 0x438 = the ring's `latestFrameId` (ring block at
  1024, the field at + 56): the first 64-bit load of the first frame after the attach. The offline tests had stepped the
  player on a `VirtualAlloc` section that is writable, so they could not see it. No record had been written by the
  driver (the crash came at the attach frame). Restore rc 0 (backup_restore_ok, provision verify 0). Crash record and
  folder copied to `F:/c5-64bit-work/0042/w26/crash-r3/` (exe-crash.txt, crashes.txt, instruments.log; no dump: the
  game ended without a box).
  **Fix** (proxy 26173e15 and later): both views `FILE_MAP_READ | FILE_MAP_WRITE` (`kSectionViewAccess` in the
  header); the player still stores nothing into the section. **Regression test**: `CaseMappedView` maps a pagefile
  section, gives the player a view with `kSectionViewAccess` and steps it under `__try` (no fault, the start posted, the
  reader block unchanged), and shows that a `FILE_MAP_READ` view faults in `VPTrigLoad64`; with the constant set back to
  `FILE_MAP_READ` the test fails (119/121, checked 16:10). **Rule for every reader of a vp_trigger / vp_frame section on
  x86: map it writable**, because its 64-bit loads are interlocked.
- **r4: clean, records executed, nothing seen [L].** Proxy 26173e15, 15:57-16:01. Run rc 0, crash list empty, no box,
  restore rc 0. Both armed, both attached at the first in-game frame (39.31 s). The driver wrote 90 records (frame ids
  from 805): 3 one-shot hits (the weapon HitEffect read from the database, $(TERRAIN) = DIRT), the first era's city
  bombard tag (prefixed `ART_DEF_VEFFECT_`) started with FOLLOWS 120 units over the capital, 84 moves over 3 s, its stop,
  the fight's end. The player's counters at frame 1801: **records 90 (starts 4, moves 84, stops 1, owner ends 1),
  posted one-shot 3, lasting 85 (moves 84), stop 1, post failed 0**, 0 stale, 0 unknown name or handle, 0 orphan,
  forgotten 3, lost 0; the sound executor counted the same 90 (executedTotal 3 / 88 / 89 / 90 after the steps). So the
  allocators were called 89 times on the running EXE without a fault.
  **But no effect is visible** in the 41 pictures (0.25 s apart, from 1 s before the hits to 1.8 s after the stop):
  neither the three hits at and beside the capital nor the projectile; the changed-pixel count against the frame before
  grows smoothly (units' idle animation and water), with no local change at the posting moments. Not decided why;
  the candidates, each for one diagnostic launch: (a) the records reach no listener or are deferred forever (8.1's busy
  bit and its replay [I]); (b) the definitions are not found by the hash (the bombard tag's VisEffect name is [I], the
  hit name is a database value); (c) the fog test with the EXE's FOW key off (8.1, + 0x8F0 / the per-plot grid) drops
  the create (index -1); (d) the effects are drawn but hidden under the composite with `ServerRenderer = 1` (D147 says
  the EXE draws effects over the server's picture; not checked for effects the EXE did not start itself); (e) the
  position: HexToWorld's point as the effect's world translation [I]. A launch with `ServerRenderer = 0` (the EXE draws
  the world, Particle on) and a stock-started effect for control (a pillaged plot's smoke) separates (d) from (a)-(c);
  reading the effect manager's live count after a post separates (a)/(b)/(c) (an EXE read through a table row).
  *Answered by 15.7.8 (W29, 2026-11-20): r4's two names could not draw anything, whatever the player did.*

#### 15.7.8 Why r4 showed nothing, and the deciding launch r5 (W29, 2026-11-20) [C / L where marked]

W29 of team-0042 (account B), branch `w29-4727` from `w26-4727` @7dd9f3138, 16:44 EST on.

**Offline, the names [C: the installed definitions extracted by 4786 (`F:/c5-64bit-work/0042/w7/extract`, section 14)
and c5h-b's game data, 2026-11-20 16:50].** r4's driver chose its two names in a way that could not show anything:

- **The hits**: `SELECT HitEffect ... WHERE HitEffect LIKE '%IMPACT%' ORDER BY HitEffect LIMIT 1` gives
  `ART_DEF_VEFFECT_ANTIAIR_IMPACT_$(TERRAIN)`, so `..._ANTIAIR_IMPACT_DIRT`. That VisEffect has an empty
  `<ParticleEffects>`, no `<ModelEffects>` and one `<SoundEffect>` (a bullet hit sound, 0.1 s): **nothing to draw, in
  stock too.** The other IMPACT hit effects (ARTILLERY, CANNON, GUIDED_MISSILE, TANK with DIRT) have particles.
- **The projectile**: `Eras.CityBombardEffectTag` is `CITY_BOMBARD_EARLY` (`_MID`, `_LATE`), and that is the VisEffect's
  own `<Name>` in `Civ5ArtDefines_VisEffects.xml` (a model effect, the archer's arrow, `fBaseDuration` 0.01). The driver
  prefixed it with `ART_DEF_VEFFECT_`, a name with no definition, so the EXE's hash map has nothing under its hash and
  no effect is made (**candidate (b), by the driver**). The player's own name check cannot see this: it checks the
  server's name table kind, not the EXE's definitions (the server resolves names through 4786's library, 15.2).
- So r4 says nothing about candidates (a), (c), (d), (e). The player's counters (89 posts, 0 faults) stand.

**r5, the deciding launch** (written 17:12 before the run): c5h-b (C:, old build), arm h5 as r4 (`ServerRenderer = 1`,
FOW key off, Particle on, Combat and Projectile off), `VP64_TEST_EFFECT_PLAYER=1`, layout
`C:/c5-64bit-work/0042/w29/layout4727` (W19's 4723 layout with its .pdb files, proxy c3462e2f = the branch's tip
7dd9f3138). Driver `run_4727.py` with `W29_4727_NAMES` = a local JSON outside git: hit `ART_DEF_VEFFECT_ARTILLERY_IMPACT_DIRT`
(two particle effects), lasting `ART_DEF_VEFFECT_CITY_DAMAGED_SMOKE_THIRD_01` (a fire particle and a model), projectile
`CITY_BOMBARD_EARLY`. Steps: (0) **stock control** in the same view: the capital's damage set to 60 % (the EXE's city
damage smoke) and an improvement within 2 plots pillaged (the EXE's pillage smoke; a farm made first if none), pictures
4 s; (a) the hit at the capital and 90 units either side, four rounds 0.75 s apart; (a2) the lasting smoke 90 units
south, at rest for 3 s; (b) the projectile moved 600 units in 3 s; (c) both stopped; (d) the fight's end. Reading: the
control seen -> effects can be seen over the composite in this configuration and the fog test passes at those plots
((d) out for EXE-started effects, (c) out at the capital); ours seen -> the player works and r4's names were the whole
cause; control seen and ours not -> (a) or (e) remain, the live count decides (not built: it needs a new row through
lead-0590); neither seen -> (d) or the configuration.

**r5 result (c5h-b, 17:22-17:27 EST; claim w29-0042 17:18) [L].** Chain rc 0, run rc 0, crash list empty, no box,
restore rc 0 (`backup_restore_ok`, provision verify 0). The player armed ("armed on Steam build 4390913") and attached at
38.7 s. Counters at frame 1801: **records 101 (starts 14, moves 84, stops 2, owner ends 1), posted one-shot 12, lasting
86 (moves 84), stop 2, post failed 0**, 0 stale, 0 unknown name or handle, 0 lost; the sound executor counted the same
101. Pictures (74, 0.25 s apart, DIAGNOSTIC, D112; `F:/c5-64bit-work/0042/w29/pics-r5`, the city at screen centre):

| What | Seen |
|---|---|
| Stock control: the capital's damage (159 HP) and a farm made and pillaged at (7,22) | the EXE's city fire and smoke at the capital and the pillage smoke at the farm, from 0.5 s after the Lua call **[L]** |
| The player's 0x28 hits (ARTILLERY_IMPACT_DIRT, record scale 1.0) at the capital and 90 units either side, 4 rounds | **drawn**: explosions with debris and flashes centred on the capital, in 13 of 16 frames (3 flat PrintWindow frames) **[L]**; but **huge**: one hit covers most of the screen (below) |
| The player's lasting 0x29 (CITY_DAMAGED_SMOKE_THIRD_01, scale 1.0) 90 units south, at rest 3 s | **drawn** at that point: a small fire the size of the stock city fires **[L]** |
| The projectile (CITY_BOMBARD_EARLY, the arrow model, scale 1.0) 120 units up, re-posted with the same key and name every ~33 ms for 3 s | **drawn and moving**: the arrow and its shadow on the water; its right edge moves from x 795 to 1668 px over frames 4-9 (B2 holds live) **[L]**; it hangs along the identity orientation (the definition's -90 degrees about z only), not along the flight: the server sends identity until its hosts fill `boneTransform` (15.6) |
| 0x2A stops of the projectile and of the lasting fire | **both gone** in the next pictures; the stock control's smoke and fire stay **[L]** |

**The candidates, current statement:**

- (a) records reach no listener or are deferred forever: **out [L]**: every kind drew, at its frame.
- (b) definitions not found by the hash: **r4's projectile only, by the driver's prefix [C]**; the player's CRC-32 finds
  the definitions (all three names drew) **[L]**.
- (c) the fog test with the EXE's FOW key off: **out at visible plots [L]** (stock and player effects drew with the D33
  set, FOW off). A plot out of sight was not tried.
- (d) hidden under the composite with `ServerRenderer = 1`: **out [L]**, for EXE-started and player-started effects alike.
- (e) the position: **out [L]**: HexToWorld's (x, y, z), z up, is the effects' world space; each effect drew at its
  point (the hits at the capital, the fire 90 units south, the arrow 120 units up).

**Scale (new, for the server side) [L for the look, C/I for the cause].** The record's scale multiplies the
definition's (8.1). At 1.0 the city fire looks as stock's (stock's city smoke posts 1.0, 8.1), but the artillery hit
fills the screen and the arrow is the length of a forest plot. The stock writers of unit effects post the unit's own
scale: the timed trigger "a float of the trigger's model" (8.1) and, for a weapon's `HitEffectScale` that is NULL or 0
or below, "the member's own value at + 0x140" (`effects-server.md` 1, item 1). The member art's `fScale` values of the
artillery's members on c5h-b are 0.07-0.12 [C, the instance's art defines], and no `HitEffectScale` element is written
in its XML (so the column is NULL for every weapon there [I: SQL updates not searched]). The server's tables take a
NULL `HitEffectScale` as **1.0** (`render/include/c5r/effects/projectile.h` `hit_effect_scale`, `unitanim/combat.h`
`effect_scale`, "taken as 1.0 [I]"), so route S's hits would be about ten times stock's size. *(Fixed on this branch
2026-11-20, "The scale fix" below.)* Not 4727's files: the
player passes the record's scale as it is (`ScaleOf`: only 0, negative, huge or NaN become 1.0). r6 below checks the
size at 0.12.

**r6 (c5h-b, 17:36-17:41 EST) [L]:** r5 again with record scale 0.12 for the hit and the arrow (names file key
`hit_scale` / `proj_scale`; the lasting fire 1.0). Clean (chain, run and restore rc 0, verify 0, no box). Counters:
records 100, posted one-shot 12, lasting 85 (83 moves), stop 2, post failed 0. The three hits are now each a flash
about the size of a figure group, at the capital and 90 units either side (`F:/c5-64bit-work/0042/w29/r6-crops.png`,
`pics-r6`; changed pixels per frame at most 76,104 against r5's 1,549,103); the arrow is small, as stock's. **So the
drawn size follows the record's scale exactly, and stock-sized unit effects need the member's scale (about 0.1), not
1.0.** Seen too: each flash has a straight horizontal lower edge, the line where a camera-facing quad at ground height
goes into the server's terrain depth [I: the depth test that D147's composite gives the EXE's effects; not compared with
a stock artillery hit]. c5h-b released 17:44.

**Outcome for 4727 [L]:** the player starts, moves and stops the EXE's own effects at the server's figures with
`ServerRenderer = 1` (the acceptance's "seen"). No change to the player was needed; the driver now takes its names and
scales from a local file (`W29_4727_NAMES`, outside git, D83) and starts a stock control first. Open, outside 4727:
(1) the server's NULL `HitEffectScale` should be the member's scale as stock's, not 1.0 (**done below**); (2) the
projectile's orientation along its flight (15.6, the hosts' `boneTransform`); (3) the fog test at a plot out of sight
with the FOW key off (not tried); (4) build 26577625 live (15.7.5); (5) a city volley's scale: `planHitEffect` sends the
era's `CITY_BOMBARD_*` flight and its children at 1.0 (a city has no member), and r5 drew that arrow at 1.0 tree-sized;
stock's value for a city's volley was not read [I: likely smaller]. The live count of the effect manager was not needed
and no build-table row was added.

**The scale fix (W29 on lead-0042's order 17:46, branch `w29-4727` with `w25-4728` and `w27-4729` merged, the stage-5
candidate) [C: the tests below].** One rule for a weapon's hit effect, as the EXE's reader has it (`effects-server.md`
1.1 item 1): `HitEffectScale` when written and above 0, else the **striking member's own scale**; 1.0 only when no member
is known (counted). Where:

- **A unit's projectile** (4792's flight, its `HitEffect` where it lands): `c5r::effects::hitScaleOf(weapon,
  member_scale)` in `render/include/c5r/effects/projectile.h`; `server/src/effects/srv_fight_geometry.cpp` passes the
  firing member's `Scale` (the flight's own scale), `render/tools/effects/projscene.cpp` the same,
  `server/tests/effects/trigger_scene.cpp` its flight's scale (1: that scene has no member scale, so its totals do not
  change).
- **A weapon without a flight** (4777's `CombatHit`, the `OneShot` route): `CombatWeapon::effect_scale` and
  `CombatHit::scale` are now **0 for unset** (`render/src/unitanim/combat_tables.cpp`, `unitanim/combat.h`);
  `planHitEffect` keeps 0 (`effect_routes.cpp`, `HitEffectPlan::scale`); `EffectRoutes::applyHit`
  (`server/src/effects/srv_effect_routes.cpp`) fills it with `FigurePositions::figureScale` of the striking figure
  (`hitMemberScale`, counters `hitScaleMember` / `hitScaleUnknown`).
- **Regressions** that fail with the old 1.0: `render/tests/effects/test_projectile.cpp` (`hitScaleOf`: NULL and 0 take
  the member's 0.12 / 0.14, a written 0.4 stays, no member -> 1.0), `render/tests/effects/test_effect_routes.cpp`
  (`planHitEffect` 0 and -2 -> 0), `server/tests/effects/test_effect_routes.cpp` `testHitScale` (member 0.12 -> a record
  at 0.12; a figure whose member scale is not known -> 1.0 counted; a set 0.5 stays).
- Not changed: the figures' own trigger effects already take `figureScale` (4793, `srv_trigger_emitter.cpp`); the flight
  itself already takes the firing member's `Scale` (effects-server.md 2.4 [L]); `ProjectileChildEffectScale` keeps its
  rules (0 or below: 0.14).

**Tests of the stage-5 candidate (workstation, 2026-11-20 18:06-18:58 EST, `C:/c5-64bit-work/0042/w29/chain_scale.sh`,
W25's chain with this branch; build trees `C:/c5-64bit-work/0042/w29/t/build/rel|asan`, the install of `render_game_dir`
and a copy of W25's database (4872's copy under 0561 is gone: the first pass failed 0/19 on "database not found"), every
step through `quietlock.py run`, one at a time) [C: these runs]:**

| What | Plain | ASan (MSVC `/fsanitize=address`) |
|---|---|---|
| render: every target built; ctest `c5r_effects_*` and `c5r_unitanim_*` | 29 of 29 passed | `c5r_effects_test_effect_routes` and `c5r_effects_test_projectile` passed, 0 ASan reports (canary built) |
| `trigger_scene --effects` on 0557's 19 fights | `RESULT PASS 19/19`; totals as W22's: EffectStart 450 (241 following), EffectMove 355, EffectStop 241, hits 127, one-shots 20, 0 undefined | `RESULT PASS 19/19` |
| `trigger_scene --geometry` | `RESULT PASS 19/19` | not run |
| `run_effect_routes_test.py` | first pass 273 / 2 failed: the new `testHitScale` used figure -1 for "unknown", but `CombatHit::figure` is `uint16_t` (the case became a known figure); corrected to a figure without a member scale (756adb8fa); then **275 passed, 0 failed** | 275 / 0, canary caught |
| `run_trigger_emitter_test.py` | OK (0 failing) | OK |
| `test_scene_units` | 70 of 70 cases passed (peak 195 MB) | 70 of 70, 0 reports, peak 1,210 MB |
| `test_unitfeed.py -k "aiturn or special_fights or controller_timeline"` | 4 passed | 4 passed (ASan unitfeed) |
| links: `build_units_extras_tests.py`, `build_scenebundle_tests.py` (no rule A run: MC #8300), `build_server.py --config release --offline` (inputs F:/c5-64bit-work/0454/build-inputs, as W25's) | OK; server.exe in 236 s | - |

## 16. E2 as built: the `.psb` particle file reader (ticket 4787, 2026-11-23)

W103 of epic 0042 (account B), branch `w103-4787` from main. Stage B groundwork (the Architect's route S note 2.2): the
reader is the field-meaning guide for the particle tap's fitting (4788); it is not on the path to a fight as stock.
Code: `models/include/c5models/psb.h`, `models/src/psb_reader.cpp`, test `models/tests/test_psb.cpp` (globbed by
`models/CMakeLists.txt`, no CMake change). Written from section 4 and the files read at run time (D83): nothing of the
game's files and no EXE read. Inputs of every number below: the install of the `render_game_dir` setting (a pure-VP
install, VP as DLC, Vibrant Textures; read only); the test reads the `.fpk` archives of its `Resource/Common`, `DX9`
and `DX11` (98 archives, 30,021 entries) itself, through a test-only FileProvider written from `asset-pipeline.md` 1.1,
so no extraction is needed. Evidence: `models/evidence/4787/test_psb.{plain,asan}.out.txt` (counts and names only).

### 16.1 What the reader reads, and the census checked [C, 2026-11-23 15:56 plain and 15:58 ASan, test run]

- `parse_psb(bytes)`: the header (`PSB\0`, version 100, payload size = file size - 0x40, header size 0x40; the u32 at
  0x10 and whether 0x14-0x40 are zero are recorded, not required), then the block chain to the exact end of the file.
  Per block: the name up to its NUL, the size, the u32 at +0x104, **the whole block as raw bytes** (`PsbEmitter::raw`,
  block-relative offsets as in section 4; the parameters are `raw[0x108 .. size)`, not decoded) and its string slots
  (16.2).
- **Census, checked by the test against section 4:** 384 `.psb` entries (360 distinct names: some names are held by
  more than one archive; 0320 counted files too), 2,772,208 bytes, **384 of 384 parse**; version 100, the u32 at 0x10
  = 16 and zero bytes to 0x40 in all 384; emitters per file 1:159 2:53 3:43 4:40 5:22 6:18 7:11 8:14 9:6 10:8 11:2 12:3
  13:2 17:1 18:2; **1,232 emitters**; **49 block sizes**, 1,488 bytes x 746, smallest 1,488, **largest 61,808**
  (section 4 said 19,632: corrected there); +0x104 = 272 in all 1,232. All as 0320's survey except the largest size.

### 16.2 String slots: the rule and what it finds [C over all 384 files for the counts; I for the meanings]

- **The rule** (`find_psb_slots`): from +0x108 on, at every 4-aligned block offset `o` with `o + 4 + 256 <= size`: the
  u32 at `o` is 0 or 1, and the 256 bytes from `o + 4` hold printable ASCII (0x20-0x7E) of at least one character ending
  with a NUL inside the field, whose text is `Custom Data` or ends in `.dds`, `.tga` (a texture) or `.txt` (a shader
  set). A found slot's 256-byte field is not scanned again. The rule is a search, not a decoded field: the fields that
  give the slots' count and place are not decoded (4788).
- **What it finds**: 3,100 texture references, of which 2,750 are flag-1 slots and 350 are art-tree paths ending in
  `.dds` held in the flag-0 Custom Data slot (one each in 350 of the 354 blocks with a shader slot [C, own scratch
  survey with the same rule]; that these are the 350 `FakeVolume.txt` blocks is likely, not checked); 1,208 Custom Data slots with their default text; 354 shader slots (`fakevolume.txt` 350,
  `brightness.txt` 3, `mask.txt` 1), all flag 1. Every flag-1 slot (3,104) ends inside its block with 0x120 bytes (flag,
  name, 28 bytes).
- **Texture slots per emitter**: 0:3 1:776 2:314 3:50 4:28 5:8 6:14 7:2 8:6 9:6 13:2 14:2 16:3 24:2 55:1 60:1 64:12 77:1
  84:1. This is 0320's histogram with its one 10-reference emitter counted at 8: **0320's search matched `?.dds` twice in
  that emitter inside float bytes at unaligned offsets**; the rule does not (aligned flag, whole field), so there are
  122 distinct texture names in 3,100 references, not 123 in 3,102 (section 4 corrected).
- **Hints for 4788** [C for the counts, own scratch survey with the same rule; I for the meaning]: the first flag-1
  texture slot is at +0x4B0 in 854 emitters, +0x5F0 in 313, +0x450 in 24, +0x8F0 in 6; the u32 at +0x234 equals the
  number of flag-1 texture slots in 1,202 of 1,232 blocks, and the u32 at +0x228 equals 0x3A0 + 0x120 x that number in
  854 (a count and an end offset of the slot array in the common layout [I]); the 28 bytes after a texture slot's name
  are 0, 0, 0, 1.0, 1.0, 0, 0 in 1,886 of 2,750 flag-1 texture slots; the next most frequent values (12 slots each) are
  steps of 0.125 / 0.25 / 0.5 that look like sub-rectangles of a texture sheet [I].

### 16.3 Textures by bare name [C, test run]

- `resolve_psb_textures(file, FileProvider&)` looks every texture slot up by `basename_lower(text)` (`exists`); shader
  and Custom Data slots are not looked up.
- **122 distinct names: 112 found, 10 missing** (36 references): found in `Resource/DX9` `EffectTextures.fpk` 106,
  `Expansion1EffectsTextures.fpk` 2, `Expansion2EffectsTextures.fpk` 1, `ConquestDeluxeTextures.fpk` 1,
  `PolynesiaTextures.fpk` 1, `MiscTextures.fpk` 1 (0320's numbers). Missing: `fx_arrowup_01.dds`, `fx_seagull_01.dds`,
  `fx_smokecloud03.dds`, `fx_tank_treads.dds`, `fx_trailcloud_01.dds`, `fx_trailfire_02.dds`,
  `fx_unitselectedglow_02.dds`, `fx_waterfalls01.dds`, `fx_wave_base01.dds`, `gui_unitselectedglow_01.dds`. What the EXE
  draws for them stays open (section 4).

### 16.4 The interface for 4788 and the server

- `parse_psb()` -> `PsbFile` (header fields, `emitters`); `PsbEmitter` (`name`, `file_offset`, `size`, `field_104`,
  `raw`, `slots`, `texture_count()`); `PsbSlot` (`offset` of the flag, `flag`, `kind` Texture / Shader / CustomData,
  `text`, `bare`, `found`, `origin`). The server's FileProvider is render/'s `GameFiles` behind the model library's
  interface, as in 14.4; the reader opens no archive itself.
- Refusals (`Error`, the output cleared): `Truncated` (shorter than the header, a payload size above the bytes present,
  a block or a block's fixed part running past the end), `BadMagic`, `BadHeader` (version not 100, header size not
  0x40, payload size below the bytes present, no block, a block size below 0x108 or not a multiple of 4, a name without
  its NUL), `ReadBudget` (above 64 MB or 4,096 blocks), `OutOfMemory`.
- Not built: the parameters' meaning, the emitter simulation, drawing, and which Fork shader set a block without a
  `.txt` uses (section 4).

### 16.5 Tests and commands

- Build: `python -I perf/quietlock/quietlock.py run --who <t> --what "test_psb" -- python -I models/build.py --target
  test_psb --out <dir>`, and the same with `--asan --out <asan dir>` (cl.exe `/fsanitize=address`).
- Run `<dir>/test_psb.exe` through `quietlock.py run` with `C5R_GAME_DIR` = the `render_game_dir` setting (or
  `C5M_PSB_INSTALL`); the ASan executable needs the MSVC ASan runtime folder (`VC/Tools/MSVC/<ver>/bin/Hostx64/x64`) on
  PATH (without it: exit 127, no output). Without the variable the install part prints SKIP and the test exits 77.
- Results [C, 2026-11-23]: plain **147 checks, 0 failures**, exit 0 (15:56); ASan **147 checks, 0 failures**, exit 0,
  no ASan report (15:58); clang-cl `/W4 /WX` clean.
- Negative cases (built-in files): every truncation of a good two-block file (all 3,952 lengths: `Truncated`), a
  truncated file with its header rewritten to match (chain past the end), a tail shorter than a block's fixed part,
  bad magic, version 99, header size 0x44, a byte after the payload, no block, block sizes 0, 0x104, 0x5D2,
  0x7FFFFFFF, 0xFFFFFFFF (`BadHeader`), a block size past the end and a chain that lands short of the end
  (`Truncated`), a name without its NUL, 4,097 blocks (`ReadBudget`; 4,096 accepted), a size above 64 MB, a null
  buffer; the slot rule against flag 2, no extension, a non-printable character, an empty name, the unaligned `?.dds`
  case, a field without its NUL, a slot-like pair inside a slot's padding, a field crossing the block end; 20,000
  seeded mutations (3,562 accepted with the chain invariant, 16,438 refused with a code), no crash.
