# Asset pipeline of the server's terrain renderer (epic 2900, package I2-D)

Status: done 2026-11-11 (opened 2026-11-10 23:20 EST); sections 1-5 are tasks 2901-2904 and 2906. Design: D13 (the server parses static assets from the installed
game files and takes from the EXE only what the EXE computes), scope D33 (terrain, water, rivers, borders, overlays;
no models). Code: `render/` (standalone CMake project; `python render/build.py --test`). Large and extracted data:
`D:/c5-64bit-work/2900/` (never committed). Evidence: `evidence/2900/`.

Inputs used throughout, read-only: the install copy `%USERPROFILE%\Desktop\pure_vp\Sid Meier's Civilization V`
(same `Resource/` as the Steam install; its `Assets/DLC/` also holds the VP modpack `MP_AUTOPLAY_VP_5_4_7`, which the
Steam install does not) and the EXE copy
`%USERPROFILE%\tools\ghidra-projects\bin\CivilizationV_DX11.exe` (SHA-256 `b2ce96ae...c88e`, identical to the Steam
EXE, build id 4390913).

The inventory of what the terrain renderer draws, and from which art, is in
[terrain-rendering-inventory.md](terrain-rendering-inventory.md). The terrain draw built on this pipeline (epic 3200):
[terrain-surface.md](terrain-surface.md).

**Build and test defaults since 2026-11-11 (bug 0091, team-3200 W1).** Without `--build-dir`, `render/build.py` builds
in `D:/c5-64bit-work/render-build/<key>/<config>`, `<key>` = 10 hex digits of the SHA-1 of the checkout's path (before:
`D:/c5-64bit-work/2900/build/<config>` for every checkout, whose CMake cache stopped any other checkout at configure);
test scratch defaults to `<build tree>/test-out`. A test executable with any skipped test exits 77 (before: only when
all skipped, so ctest showed partial skips as "Passed"), and `build.py --test` fails when ctest reports any skipped test.
`GameFiles::read` clears `*origin` on NotFound; `fpkscan sweep` exits 4 (not 3, the launch gates' code) when a file
fails. Tests that fail without each fix: `c5r_build_py_test` (item 1), `testkit_some_skipped_exits_77` (item 2),
`negative_section_*` (item 4, section 2.1).

## 1. Game files: FPK packs, loose-file lookup, DDS textures (task 2901)

Done 2026-11-11 (session 2026-11-10 23:29 to 2026-11-11 00:12 EST). Code: `render/include/c5r/gamefs/` (`fpk.h`, `gamefiles.h`, `civ5pkg.h`, `dds.h`),
`render/src/gamefs/`, tests `render/tests/gamefs/`, tool `render/tools/fpkscan/`. Evidence:
`evidence/2900/2901/`. All measurements on the install copy named above (which, unlike the Steam install, also holds the
Vox Populi modpack as `Assets/DLC/MP_AUTOPLAY_VP_5_4_7`, priority 300) and, for loose mod textures, on
`%USERPROFILE%/Documents/My Games/Sid Meier's Civilization 5/MODS`.

**API in two lines.** `FpkArchive::open/entries/read/find` (memory-mapped, read-only) over `parseFpkIndex`;
`GameFiles::open(root, Options)` then `read(name, bytes, &origin)` by bare file name in the game's order;
`parseDds(bytes) -> DdsImage` (D3D11 subresource order, conversions in `bytes`) and `createTexture(dev, img, &tex, &srv)`.

### 1.1 The `.fpk` format, verified on every pack [C]

`fpk_layout_check.py` over all 147 packs (30,557 entries), 2026-11-10: `u32 version` = 6 in all, `"FPK_"`, `u16` = 0 in
all, `u32 count` at offset 10, then per entry `u32 nameLen`, the name, pad to a multiple of 4 of `nameLen` (23,027
entries have non-zero pad bytes), `u32 a`, `u32 b`, `u32 size`, `u32 offset`. Confirms 9012's description
(landmarks-and-models.md 2.1) and adds:

- `a` is 0 in 30,442 entries; in the other 115 it holds values like 1,274,827,636 (2010-05-25 as Unix seconds) [I]: a
  file time; the EXE's flags-6 replace rule compares file times (1.2). `b` is 0 in every entry.
- Names: 1-74 characters, printable ASCII, no `/` or `\`, no upper case; no name twice inside one pack.
- Payloads: no overlaps and no shared ranges; 3,129 gaps between payloads; 35 packs have bytes after the last payload;
  the first payload starts at most 2 bytes after the index.
- **Decision:** `parseFpkIndex` rejects (BadLayout) any overlap, a payload inside the index, a payload past the end,
  duplicate names, a non-zero `u16`, bad name characters and name lengths 0 or above 260; Truncated for a short header
  or entry; BadMagic, BadVersion. Each check has a negative test (`fpk_test.cpp`).

### 1.2 Which copy of a name the game uses [C for the rules, I where marked]

Evidence: `evidence/2900/2901/lookup_order_evidence.txt` (addresses, decompile facts), headless Ghidra on a project copy,
2026-11-10. Summary:

- The engine resolves a file by **bare file name** (the path is stripped before hashing, 0x832BD0), case-insensitive.
- There is a **base file system** (0x06196A40), searched **last**, and a vector of further file systems (0x06196A3C),
  searched from the last element to the first (lookup 0x8181E0); the first one holding the name wins.
- Inside one file system each name has one entry; a second registration replaces it by the rule of 0x832F30: flags 0
  never replace; flags 1 replace loose entries only; flags 4 and 6 replace pack entries only (6 with a file-time
  comparison when `LooseFilesOverridePAK` = 1); flags 7 and 2 replace everything.
- **Base file system**, in mount order: the 32 `Resource/DX9/*Textures.fpk` of the table 0xB943A0 and 13
  `Resource/Common/*.fpk` (flags 0: the first pack mounted keeps a name), `Resource/Overrides` (flags 4; absent here),
  then loose `Assets/` without `*\DLC\*` with flags 1, or 6 if `config.ini` `LooseFilesOverridePAK` = 1 (0 on this
  machine). So **with the default setting a base pack beats a loose file of the same name under `Assets/`**.
- **`Resource/DX9_Low`**: mounted only when the global 0xD90F90 is 0 ([I] the low texture-quality option), as its own
  file system above the base one (22 packs, table 0xB94340), and next to every package pack. Then DX9_Low wins. The
  default `GameFiles` leaves it out (`Options::includeDx9Low`).
- **Packages** (`Assets/DLC/*/*.Civ5Pkg`, cvContentPackageManager / cvUISkinManager): for each kind (`Common`,
  `Gameplay`, `UISkin`) one file system, searched before the base one. Packs named in `<Paks>` resolve to
  `Resource/DX9/<n>.fpk`, else `Resource/Common/<n>.fpk` (flags 4: a later pack replaces an earlier pack's entry);
  directories (`<Directory>`, `Skin/Directory`, `GameplaySkin/Directory`) are registered with flags 7 (a loose
  package file beats every pack), with `*\Sounds\Speech\*` and `*\Scenarios\*` excluded. [I]: the order of packages
  inside a kind (we use ascending `<Priority>`, then folder name, so higher priority registers later and wins), the
  relative order of the kind/skin/DX9_Low file systems (all above base), and that only the active UISkin set
  (`Expansion2`, platform `Common`) counts.
- *Added 2026-11-13 (3404, terrain-bake-comparison.md 2):* **the "Common" kind also mounts the package folders
  themselves** [C rule, I for the exact folder]: 0x630E50 with kind 0 collects no Directory (no package names one for
  Common) and then registers the package folder with flags 7, excluding `*\Sounds\Speech\*`, `*\Scenarios\*` and every
  directory any package names for the other kinds. That is why `Assets/DLC/Shared/Terrain/TerrainDef.csv` (Upgrade1.Civ5Pkg
  names no Directory) beats `Resource/Common/TerrainModels.fpk`. `GameFiles` does this since 3404
  (`Options::mountPackageRoots`); before, it mounted only the named directories (the text above described that model).
- **Mods** (`MODS/...` or the modpack): imported by "SetActiveDLCandMods - Import Mod Files into filesystem" (0xA7BDA4);
  not traced. `GameFiles` puts `Options::extraLooseRoots` first by default (`extraLooseFirst`) [I].
- `GameFiles` rebuilds this as four layers (extra roots, packages, DX9_Low, base) and applies the replace rule inside
  each; `gamefiles_test.cpp` checks every rule on a synthetic install; the sweep of the real install is in 1.4.

### 1.3 DDS formats in the install and the conversions [C]

`fpkscan sweep` 2026-11-11 00:08 EST, hardware device NVIDIA GeForce RTX 4080 (FL 11_1), no window. 10,004 DDS in
packs, 3,287 loose (under `Assets/` and `MODS/`). Formats found (pack / loose):

| File format | Count | DXGI | Conversion |
|---|---|---|---|
| A8R8G8B8 (masks) | 2,368 + 18 cube / 1,351 | B8G8R8A8_UNORM | none |
| X8R8G8B8 | 1 + 2 cube / 0 | B8G8R8X8_UNORM | none |
| DXT1 | 978 + 1,055 cube / 492 | BC1_UNORM | none (corrected 2026-11-11 by 9019: was "950", the count before the 28 header-24 files parsed) |
| DXT3 / DXT2 | 511 / 382 + 19 DXT2 (a non-VP mod) | BC2_UNORM | DXT2 premultiplied alpha passed as is |
| DXT5 / DXT4 | 1,793 / 706 + 15 DXT4 (same mod) | BC3_UNORM | DXT4 as DXT5 |
| D3DFMT 114 (R32F) | 1,382 / 0 | R32_FLOAT | none |
| D3DFMT 36 (A16B16G16R16) | 5 / 302 (300 terrain `_H` pieces) | R16G16B16A16_UNORM | none |
| D3DFMT 113 (A16B16G16R16F) | 3 cube / 0 | R16G16B16A16_FLOAT | none |
| D3DFMT 116 (A32B32G32R32F) | 10 cube / 0 | R32G32B32A32_FLOAT | none |
| L16 | 1,144 / 1 | R16_UNORM | none (D3D9 sampled L as .rrr; a shader port reads .r) |
| L8 | 704 / 9 | R8_UNORM | none (same note) |
| A8 | 30 / 0 | A8_UNORM | none |

No **volume** texture (DDSCAPS2_VOLUME) and no DX10-header file exists anywhere in the install or the mods: the 3D
textures the DX11 terrain pixel shader samples are not files [I: built at run time; task 2903/2905 to identify].
Supported beyond what the install uses (tested on synthetic files): ATI1/ATI2/BC4U/BC4S/BC5U/BC5S, D3DFMT 110-112/115,
A8B8G8R8, G16R16, A2B10G10R10, A8L8, the DX10 header (1D/2D/3D, arrays, cube arrays), volume textures; converted to
B8G8R8A8_UNORM: R8G8B8, X8B8G8R8 (alpha 255), R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, X4R4G4B4, A8R3G3B2, R3G3B2, A4L4
(the 16-bit D3D11 formats are optional on a device).

Quirks found by the sweep: **28 DXT1 files** (14 each in `Expansion1BuildingsTextures.fpk` and
`Expansion2BuildingsTextures.fpk`, e.g. `alhambra_hb_diff.dds`) have header size 24 instead of 124 with an otherwise
valid header; `parseDds` accepts 24. **5 loose BC files of the VP modpack** (copied again in `MODS`, 10 rows) have a top
level that is not a multiple of 4 (`A__880.dds` 1091x1442, `ProjectTreasureFleet.dds` and `ProjectUN.dds` 939x614,
`sv_Amazonas_128.dds` and `sv_Redcoat.dds` 128x129): D3D11 refuses them (`CreateTexture2D` E_INVALIDARG on WARP for BC1
6x6, `dds_test.cpp`), so `parseDds` reports Unsupported. All are UI art, none terrain-side.

### 1.4 Sweep result (`evidence/2900/2901/sweep_summary.txt`, `sweep_archives.csv`, `terrain_side_files.csv`)

2026-11-11 00:08 EST (first run 2026-11-10 23:50), 16 s wall time (packs 2.5 s), `fpkscan sweep <install> --mods <MODS>`:

- **147 of 147 packs open** (`Resource/Common` 27, `Resource/DX9` 71, `Resource/DX9_Low` 49; none under `Assets/`),
  2,373,988,644 bytes, 30,557 entries (.gr2 12,412, .dds 10,004, .wav 3,516, .fxsxml 1,388, .fic 1,382, ...).
- **Packs: 10,004 DDS, 10,004 parsed and created** (after the header-24 fix; the first run had 9,976).
- **Loose: 3,287 DDS, 3,277 parsed and created**; the 10 failures are the 5 non-multiple-of-4 BC files above, twice.
- The game-file test `c5r_gamefs_game_test` loads through `GameFiles` and creates every DDS that resolves to
  `TerrainTextures.fpk`, `DecalTextures.fpk` or `Assets/terrain/`, plus samples (2026-11-11 00:07: 1,360 of 1,360 created): `missing_hex_H.dds` (128x128
  R16G16B16A16_UNORM), `forest_africa.dds` (BC3 256x256, 9 mips, resolved to the Vibrant Textures copy), `ice_environment_diffuse.dds` (BC1 cube).

**Terrain-side art** (paths under `Assets/terrain/`, the packs `TerrainTextures` / `DecalTextures`, `Assets/DLC/VT/Terrain`
and `Water`, or a name containing terrain, water, river, decal, border, fog, coast, ice, ocean or lake): 731 in packs
and 1,326 loose, **all parsed and created**.

| Where | DDS | Formats |
|---|---|---|
| `Assets/terrain/pieces/**` (299 pieces incl. 56 of 14 `Wonder_*` natural wonders, + `missing_hex` = 300; corrected 2026-11-11 by 9019 from "300 pieces incl. 15 `Wonder_*`") | 1,199 | `_H` 300 x A16B16G16R16 (149 at 128x128, 151 at 512x512); `_T1`, `_H_BLEND`, `_T_BLEND` 899 x A8R8G8B8 at the same two sizes; 300 `.csv` hex masks beside them |
| `Assets/terrain/Ice` | 0 | only `IceSettings.xml`, `IceSettings_Low.xml` |
| `Resource/DX9/TerrainTextures.fpk` | 126 | 99 DXT5, 12 DXT1 (11 cube), 8 A8R8G8B8, 7 L8 |
| `Resource/DX9/DecalTextures.fpk` | 75 | 47 DXT1, 24 DXT5, 2 DXT3, 1 A8R8G8B8, 1 L8 |
| `Resource/DX9_Low/TerrainTexturesDX9Low.fpk` | 45 | 34 DXT5, 11 DXT1 cube |
| `Assets/DLC/VT/**` (Vibrant Textures: Terrain, Water, Landmarks) | 47 | 41 DXT5, 4 DXT3, 1 DXT1, 1 A8R8G8B8 |
| other packs by name (`MiscTextures` 125, `ImprovementTextures` 99, `Expansion2BuildingsTextures` 41, `Upgrade1Textures` 33, `HutsBarbarianFortTextures` 29, ...) | 485 | mostly DXT5/DXT1; R32F and L8/L16 in leader packs |
| VP modpack / `MODS` (by name) | 28 / 52 | DXT5, DXT3, DXT1, A8R8G8B8, DXT4 |

Correction to GATE2 section 3 (a verbatim source, not edited): the terrain pieces' `_H.dds` are 128x128 for 149 pieces
and **512x512 for 151**, not all 128x128 (2026-11-10).

### 1.5 Duplicate names (`evidence/2900/2901/dups_summary.txt`)

`fpkscan dups <install>` 2026-11-11 00:08 EST (default options: Expansion2 skin, no DX9_Low, no mod roots):
25 packages, 98 packs mounted (49 of the 147 packs are never mounted with these options: the 49 `DX9_Low` packs), 31,859
resolvable names. **4,453 names are registered more than once; 1,587 of them with differing contents** (FNV-1a 64 of
each candidate): .dds 1,546 / 692 differing, .gr2 1,719 / 86, .xml 288 / 267, .lua 113 / 113, .mp3 375 / 375.
Per DDS candidate pair (winner over shadowed, differing): package pack over package pack 1,159 (265 differ), package pack
over base pack 527 (490 differ), package loose over base pack 68 (68 differ), package loose over package pack 13 (9).
`dups_dds_and_terrain.csv` lists every DDS and terrain-side name with all candidates.

Consequences:

- **Terrain:** 80 terrain-side names have several copies, 50 differ. The main case: **Vibrant Textures**
  (`Assets/DLC/VT`, package `VT_2.Civ5Pkg`, Expansion2 skin, `GameplaySkin/Directory` Terrain, Water, Cities, Landmarks)
  replaces 40 entries of `TerrainTextures.fpk` with different art (and 7 of `Upgrade1Textures.fpk`), e.g.
  `forest_africa.dds` resolves to `Assets/DLC/VT/Terrain/forest/forest_africa.dds`. A server that read
  `TerrainTextures.fpk` directly would draw different terrain than the owner's game; it must use `GameFiles`.
  Which installs on which machines hold VT, and the rule for colour comparisons (D94): section 1.7.
- `IceSettings.xml` exists in `Assets/terrain/Ice` and in `Resource/Common/TerrainModels.fpk`; with
  LooseFilesOverridePAK = 0 the pack copy wins. Same for `CultureBorders.xml` (`Assets/Overlays` loses to `MiscData.fpk`).
- landmarks-and-models.md 2 left open which `resource_deer.gr2` wins: `Expansion2Models.fpk` (180,556 B; package,
  priority 200) over `Expansion1Models.fpk` (same bytes) over base `MiscData.fpk` (182,620 B) [C rule, I package order].

### 1.6 Open points

- Package order inside a kind file system and the order of the kind / skin / DX9_Low file systems are inferred, not
  traced (0x6DB220, keys +0x7C/+0x80 of 0x819BE0). A live check: read a texture that differs between two packages
  through the running game (epic 3000's instance) or trace 0x8181E0's return for a known name.
- Mod import into the file system is not traced; mod roots are configurable (first by default).
- The flags-6 time comparison (LooseFilesOverridePAK = 1) is not emulated.
- The file-time meaning of the `a` field is inferred.
- Where the terrain shader's 3D textures come from (no volume DDS exists).

### 1.7 Vibrant Textures per install and machine (one-off 9049, 2026-11-13; rule D94)

Why: the owner, 2026-11-13: their workstation has a terrain texture pack [reworded for privacy] (richer, more saturated colours) that the laptop
lacks and oldpc may lack (D94, `research/07-architecture-decisions/decision-log.md`). The pack is Vibrant Textures
(`Assets/DLC/VT`, three packages `VT_0/1/2.Civ5Pkg` for the BaseGame, Expansion1 and Expansion2 skins, `<Directory>`
Cities, Landmarks, Terrain, Water; replaces 40 `TerrainTextures.fpk` entries and the water and river textures, section 1.5).

**The rule (D94, MC, 2026-11-13).** Every colour or picture comparison names its asset set: VT present or absent with
the VT folder hash below, the install path and the machine. It compares only like with like: server against EXE on the
same install. A comparison across machines or installs is invalid unless both sides have the same VT hash. A run
without VT is a separate data point and is labelled as one.

**Folder hash** (the method of 9047, so the hashes compare; script `D:/c5-64bit-work/9047/vthash.py`, and 9049's
survey version, which also reports the GUID references and the TerrainDef hash: `evidence/9049/vthash_survey.py`, its
`ROOTS_PLACEHOLDER` replaced by the list of installs; outputs `evidence/9049/{workstation,oldpc,laptop}.jsonl`; run
on a test host with `ssh <alias> "<python> -" < script`, which keeps `Meier's` out of the shell line): SHA-256 over the lines
`"<path relative to Assets/DLC/VT, forward slashes, lower case> <SHA-256 of the file>"`, sorted, joined by `\n` with no
newline at the end. Two values exist:

- **`45b3b43359279105fd9d0924e71192d413af38ad22c9d73fd00eacc2c73f0a45`** = the full pack: 55 files, 47,300,858 B, 51 DDS
  (43 under `Terrain/`, 3 under `Water/`, 4 `Cities/`, 1 `Landmarks/`), the 3 packages and `forests.xml`.
- **`291608a2762b79b14c64ee9da5bc8d403af950ee6b3b9e9f1b2086d7393a7579`** = **packages only**: 4 files, 3,142 B (the 3
  packages and `forests.xml`), **no DDS**. Textures resolve as without VT; treat it as VT-less for colour.

**Survey [L, read-only, 2026-11-13 12:44-12:50 EST]** (workstation by Python from bash; oldpc and laptop over SSH by alias
with the script on stdin; nothing written on any machine). The Shared `Assets/DLC/Shared/Terrain/TerrainDef.csv` is the
same everywhere (SHA-256 `ce16f7e024672fa7...`).

| Machine | Install | VT | Files | VT hash | Notes |
|---|---|---|---|---|---|
| workstation | Steam `C:/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V` | yes | 55 | 45b3b433... | The owner's own game; no modpack folder |
| workstation | `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V` | yes | 55 | 45b3b433... | also holds `MP_AUTOPLAY_VP_5_4_7` (GUID note below); the renderer's default `--game-dir` / `C5R_GAME_DIR` (`render/build.py`, `residency_drive.py`) |
| workstation | instances `%USERPROFILE%/c5-instances/c5h-a`, `c5h-b`, `c5h-c` `/install` | yes | 55 | 45b3b433... | copies of pure_vp (`instance.json` `source_install`), modpack folder excluded (profile mods548) |
| workstation | instances `D:/c5-64bit-work/instances/c5h-d`, `c5h-e` `/install` | yes | 55 | 45b3b433... | same |
| workstation | toolkit game `%USERPROFILE%/Documents/verns_civ5_vp_toolkit/verns_civ5_vp_toolkit/headless_data/game/VP_MODPACK_5_4_8_NEUTRALS/Sid Meier's Civilization V` | packages only | 4 | 291608a2... | `run_server.py`'s default `--game-dir` (headless, no drawing) |
| oldpc | Steam `C:/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V` | yes | 55 | 45b3b433... | also holds an old `MP_AUTOPLAY_VP_5_2_3` |
| oldpc | `E:/c5-testbed/base/pure_vp/Sid Meier's Civilization V` | yes | 55 | 45b3b433... | shipped from the workstation's pure_vp (`tools/testbed/bases.json`) |
| oldpc | instances `E:/c5-testbed/instances/tb1`, `tb2` `/install` | yes | 55 | 45b3b433... | copies of the base, with oldpc's own Steam EXE |
| oldpc | `E:/c5-testbed/base/toolkit-game/Sid Meier's Civilization V` | packages only | 4 | 291608a2... | |
| oldpc | `C:/civ5/game/Sid Meier's Civilization V` (another project's copy) | packages only | 4 | 291608a2... | |
| laptop | Steam `C:/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V` | **no** | - | - | stock Steam game: no `VT`, `VPUI`, `UI_bc1` |
| laptop | `C:/c5-testbed/base/pure_vp/Sid Meier's Civilization V` | yes | 55 | 45b3b433... | shipped from the workstation's pure_vp |
| laptop | instances `C:/c5-testbed/instances/tb1`, `tb2` `/install` | yes | 55 | 45b3b433... | copies of the base, with the laptop's own Steam EXE |
| laptop | `C:/c5-testbed/base/toolkit-game/Sid Meier's Civilization V` | packages only | 4 | 291608a2... | |
| laptop | `C:/civ5/game/Sid Meier's Civilization V` (another project's copy) | packages only | 4 | 291608a2... | |

No other install exists on these machines (Steam library folders read from `libraryfolders.vdf`: oldpc also lists
`D:`, `E:`, `F:/SteamLibrary`, none holds Civilization V; the `_test*` folders under `%USERPROFILE%/c5-instances` are
fixture instances with a stand-in EXE and no assets). So: the owner's remark holds for the laptop's Steam game only; **every
game copy that our tooling runs (c5h-*, tb1/tb2 on both test machines, pure_vp) carries the same full VT.**

**Does the game load VT there?**
- No setting switches it [L]: `config.ini` has no DLC or package key (workstation user folder; the testbeds'
  `base/user/config.ini` searched over SSH, no `DLC`/`package` line); `LooseFilesOverridePAK` = 0 does not touch
  package directories (flags 7, section 1.2). VT's packages carry a `<Key>` and no `<SteamApp>`, so Steam's per-game DLC
  switches, which act on Steam apps, cannot remove it [I].
- The EXE uses VT on the Goldberg instances [L by consequence, 9047]: the EXE's own bake caches on c5h-e equal the
  server's VT-based bake bit for bit on hundreds of continent-style tiles (`terrain-bake-verification.md` 4.3), which
  the base pack's textures could not give.
- **GUID shared with the VP modpack [L, I for the effect]:** VT's GUID `{b5932ae4-0f4f-498f-9333-e2d31b20e095}` is also
  the GUID of `MPModsPack.Civ5Pkg` in `MP_AUTOPLAY_VP_5_4_7` (pure_vp, both test bases), `MP_AUTOPLAY_VP_5_2_3` (oldpc's
  Steam game) and `VP_MODPACK_5_4_8_NEUTRALS` (toolkit copies). Whether the EXE keeps both packages of one GUID is not
  traced. It does not matter for our runs: the instances leave the modpack folder out, and `c5r::gamefs` does not use the
  GUID (`civ5pkg.cpp` only stores it) [C]. It would matter for a game started straight from pure_vp or oldpc's Steam
  install; check VT there before using such a run for colour.

**Audit of the colour and picture comparisons so far [2026-11-13]** (all on the workstation, RTX 4080; none ran on oldpc
or the laptop):

| Comparison | Server side | EXE side | Same asset set? |
|---|---|---|---|
| Epic 3200 live colour (`terrain-surface.md` 5, issue 3206; 9038's rerun and own mask, `terrain-surface-verification.md` 9) | the EXE's own bake caches through the bake tap, same process | same launch, c5h-d (W5 also c5h-e for one crash test) | yes, by construction: the server drew the EXE's caches [I]; VT 45b3b433 |
| 3020 geometry re-measurement | terrainshot | stock on c5h-c (Tiny), c5h-d (huge) | geometry only, colour not compared; both 45b3b433 |
| 3019 clean colour reference | not started (state ready) | - | - |
| 3404 acceptance B, `bakecompare` (`terrain-bake-comparison.md` 5) and 9047's rerun | `--game-dir` pure_vp | 3401 arm B caches baked on c5h-e | **crossed installs**, same VT hash 45b3b433 on both: **valid** (9047 4.3) |
| 3408 live picture against stock (`terrain-bake-comparison.md` 8) | `--game-dir %USERPROFILE%/c5-instances/c5h-b/install` (`D:/c5-64bit-work/3400/w8/server_series.sh`) | stock on c5h-b | yes, one install |
| 9047 live C/D and residency (`terrain-bake-verification.md` 5, 6) | c5h-e's install / pure_vp offline | c5h-e | one install for the live runs; the offline drive compares no colour |
| 2906 quadtest (section 5) | pure_vp through `GameFiles` | a reference drawn from the same file | one install |
| oldpc / laptop images | - | - | none: the only pictures made there are the headed probes' `out/shot.png` (oldpc 2, laptop 5 jobs, `quiet-testbed.md` 15), state shots, never compared for colour; every other image under the roots is a copy of the repository |

**Nothing has to be redone.** The one cross-install comparison (3404 B, and 9047's rerun of it) had the same VT hash on
both sides. Traps for later work: a colour run on the laptop's Steam game, or a renderer pointed at a toolkit copy
(packages-only VT), is a VT-less data point; a stock reference taken from the owner's Steam game or oldpc's Steam game has the
full VT but also a different DLC set (the modpack folders), so name it.

## 2. Shaders: the `.shader` section, index and reflection (task 2902)

Measured 2026-11-10 on the EXE copy named above (PE TimeDateStamp `0x546CD0A8`), offline; no game ran. Code:
`render/include/c5r/shaders/`, `render/src/shaders/`, tests `render/tests/shaders/` (46 tests, every check with a
negative test on synthetic data), tool `render/tools/shaderdump/`. Evidence: `evidence/2900/2902/`. Extracted blobs,
their D3DDisassemble text and per-blob reflection JSON (game data, not committed) are kept for task 2903 under
`D:/c5-64bit-work/2900/2902/dump/` (`blobs/`, `asm/`, `reflect/`, written by `shaderdump <exe> --out`).

### 2.1 The section and how it is split

- `.shader` is the 4th PE section [C]: RVA `0x5D9B000` (VA `0x619B000`), VirtualSize `0x6AC304` (6,996,740 B),
  file offset `0x8B6A00`, SizeOfRawData `0x6AC400` (the last 0xFC bytes are file alignment).
- Walked from the start, container by container (each parsed and its checksum verified; skipped by its total size):
  **1,417 DXBC containers, 6,993,892 B, and 712 gaps of exactly 4 zero bytes (2,848 B)**: 6,993,892 + 2,848 =
  6,996,740 = VirtualSize, so every byte is accounted for [C]. Every blob starts 8-aligned; the 712 gaps follow 712 of
  the 713 blobs whose size is 4 mod 8 (the 713th is the last blob, which ends at the section end) [C].
  *Since 2026-11-11 (bug 0091 item 4) `ShaderSection` fails with BadLayout on any gap longer than 4 bytes or not all
  zero*: before, a container with a damaged `DXBC` magic was walked over as one gap and the parse returned Ok with 1,416
  blobs and no create-site data (verification 9019, D4). Test: `negative_section_damaged_magic_is_bad_layout`.
- Chunks: `ISGN+OSGN+SHDR` 924 (SM4), `ISGN+OSGN+SHEX` 490, `ISGN+OSGN+PCSG+SHEX` 2 (the HS), `ISGN+PCSG+OSGN+SHEX`
  1 (the DS). **No RDEF, no STAT, no debug or private chunks in any blob** [C]. Sizes 176 to 52,732 B.
- Index (one row per blob: VA, file offset, size, stage, model, checksum, set, create site, handle global, VS
  vertex-format mask, chunks, signatures, cbuffer declarations, the cb registers read, resources with dimensions,
  samplers, temps, immediate-cb size, instruction count, D3DReflect's answer, decoder-vs-disassembly result):
  `evidence/2900/2902/shader_index.csv`, written by `shaderdump <exe> --index FILE`.

| Set (shader level) | VS | PS | HS | DS | Blobs | Bytes |
|---|---|---|---|---|---|---|
| 4_0 (levels 7, 8) | 126 | 320 | 0 | 0 | 446 | 1,754,088 |
| 4_1 (level 9) | 126 | 352 | 0 | 0 | 478 | 2,572,892 |
| 5_0 (level 10) | 128 | 362 | 2 | 1 | 493 | 2,666,912 |

Every blob's set equals its own shader model (vs_4_0 blobs are only in set 4_0, and so on) [C]. No GS or CS blob
exists [C].

### 2.2 Create sites and sets (from the code)

- Method [C]: an `llvm-objdump -d -M intel` listing of `.text` and an abstract interpreter over it
  (`evidence/2900/2902/create_sites.py`) that walks the two init functions once per shader level (7, 8, 9, 10). It
  follows the control flow, tracks pushed immediates and registers holding constants, evaluates `cmp eax, imm` on the
  level loaded from `[0x2e49edc]`, and at each call to a create function records blob, size, VS mask, call site and the
  16-bit global the next `mov word ptr [G], ax` stores the handle in. Output: `evidence/2900/2902/create_sites.csv`
  and the generated table `render/src/shaders/create_sites_table.inc` (addresses and numbers only).
- Result [C]: **1,417 of 1,417 blobs have exactly one create call site; 493 distinct handle globals**; 1,279 blobs are
  created by `0x4e5c20`, 138 by `0x80cd10`. Create functions (all `thiscall` on the renderer, callee-popped):
  `0x8bbc40` VS (blob, size, vertex-format mask; device vtable +0x30 = CreateVertexShader), `0x8bbbf0` PS (blob, size;
  +0x3C), `0x8bbcc0` HS (+0x40), `0x8bbd10` DS (+0x44). Each returns the renderer's running per-stage count
  (`renderer+0x458` VS, `+0x45c` PS, `+0x464` DS, `+0x468` HS) as the handle; the VS function also keeps the bytecode
  pointer and size in `0x3006f68[i]` / `0x3005f68[i]` (as GATE2 found live).
- Set selection [C]: both init functions load `[0x2e49edc]`; `< 7` creates no shader set at all (`jl` past it),
  `<= 8` takes the 4_0 branch, `== 9` the 4_1 branch, anything else (10) the 5_0 branch. `0x8bb640..` sets the level from
  the device's feature level: 9_3 -> 6, 10_0 -> 7, 10_1 -> 9, 11_0 -> 10 (and 8 on one other path) [C].
- Handle globals are shared across sets [C]: 446 are written by all three sets (one blob per set), 32 by 4_1 and 5_0
  only, 15 by 5_0 only. The 15 SM5-only handles are the tessellated terrain (VS `0x067d6490`, HS `0x067f98f0` and
  `0x067f9cb8`, DS `0x067fa080`), one VS (`0x067a8b18`, POSITION + TEXCOORD0, handle `0x2de1b20`) and 10 PS whose input
  is SV_POSITION + TEXCOORD0 (handles `0x2de1b24..0x2de1b58` and `0x2de1c54`; one of them, `0x067a8c78` 31,336 B, has a
  structured SRV and a structured UAV and no output).
- **Correction to GATE2** (`research/sources/shell-gates/GATE2.md` section 2 and its `shader_create_sites.json`, which
  stay as they are): GATE2's linear scan found 1,413 sites and counted 1,277 + 136; the three set branches end in
  tail-merged code (`push size; push blob; jmp` to a shared `call` at `0x4eebf0` and `0x80dc1c`), so four blobs
  (`0x068313b0`, `0x06832050`, `0x06846788`, `0x06846d48`, PS of sets 4_1/4_0) have their call there, and the two
  init functions create 1,279 and 138. GATE2's VS vertex-format masks are wrong for 20 of 380 VS (a register push or a
  push hoisted above the set branch was taken for the mask), and it has no handle global for the four calls just before
  each tail jump (their `mov word ptr [G], ax` sits after the jump). Its json also lists three calls of `0x8ed3d0` with
  "blob" `0x0666676d`, which is not a blob start (not a shader create). Everything else agrees: same 1,417 VA/size/model,
  the 1,413 call sites it found, every other handle, all cbuffer and texture declarations, sampler counts and input
  signatures, counts per set and the 493 handles. Comparison: `evidence/2900/2902/compare_gate2.py`, output
  `compare_gate2.txt` (2026-11-11).

Evidence files (`evidence/2900/2902/`): `shader_index.csv` (the index), `create_sites.py` + `create_sites.csv` (the
create-site derivation), `decoder_vs_disasm.txt` (cross-check summary; differences would be listed one per line),
`device_create.txt` (device and input-layout counts), `compare_gate2.py` + `compare_gate2.txt`.
- The library uses the generated table (not a run-time disassembly). The table's values are build 4390913's.
  (Corrected 2026-11-20, ticket 4966, epic 0590; replaced: "applies it only when the PE TimeDateStamp is 0x546CD0A8 ...
  Regenerate with create_sites.py for another build".) The table is now also the array `render_shader_create_sites` of
  the EXE build table, carried to every known build by `tools/exebuild/gen_table.py`. `ShaderSection` takes the build
  from the stamp and SizeOfImage together and applies the build's own row values. A blob keeps its build-4390913 VA as
  `va` (what `findByVa` and render/'s tables use) and its VA in the EXE as `exeVa`. A known build whose array is
  missing, incomplete or does not match fails the parse. An unknown build gets no create sites.
  Details: `research/02-exe-internals/exe-build-table.md` section 13.

### 2.3 Reflection: why D3DReflect is not enough, and the decoder

- **D3DReflect** (d3dcompiler_47, Windows SDK 10.0.26100) returns S_OK for all 1,417, with **0 constant buffers and 0
  bound resources for every blob** (no RDEF), the right signature counts, and 0 instructions (no STAT) [C]. It cannot
  give slots, sizes or which registers the code uses.
- **The decoder** (`reflect()`) reads the SM4/SM5 token stream itself: signatures (ISGN/OSGN/PCSG and the
  ISG1/OSG1/OSG5/PSG1 variants), `dcl_constantbuffer` (slot, vec4 count, immediate/dynamic indexed), `dcl_resource*`
  (slot, dimension incl. 3D, cube, arrays, MS, raw, structured; return types), `dcl_sampler` (mode), UAVs, every
  `dcl_input*`/`dcl_output*` with system value and interpolation, `dcl_temps`, `dcl_indexableTemp`, the immediate
  constant buffer, tessellation (domain, partitioning, output primitive, control-point counts, max tess factor, fork/join
  phases and instance counts), GS and CS declarations, the instruction count, and **for each constant buffer the vec4
  registers the code reads** with the components named by each read (`swizzleMask`) and the components that can affect
  the result (`usedMask`: the swizzle narrowed by the destination mask for component-wise ops and by dp2/dp3), plus the
  bases of dynamically indexed reads (`cb0[r0.x + 4]`) [C].
- **Cross-check**: `compareWithDisassembly` compares, per blob, every declaration line (rendered in D3DDisassemble's
  syntax), the instruction count, and every constant-buffer operand (reads per register, components named, dynamic
  bases) with the text of D3DDisassemble. **Agreement: 1,417 of 1,417 blobs, 0 differences** (2026-11-10,
  `evidence/2900/2902/decoder_vs_disasm.txt`) [C]. The same check passes on test shaders of every stage (VS, PS, HS,
  DS, GS, CS; SM4.0, 4.1, 5.0) compiled in the tests, and the tests show it reports a changed declaration, mask,
  register, dynamic base or instruction count.
- **Device check** (`shaderdump <exe> --device`, 2026-11-10, hardware device at feature level 11_0, no window):
  **1,417 of 1,417 create** (VS 380, PS 1,034, HS 2, DS 1); input layouts built from each VS's own input signature
  (32-bit formats by component type and mask): **379 built, 1 needs none, 0 failed**; the one is the terrain VS
  `0x067d6490`, whose only input is `SV_VertexID` [C]. Agrees with GATE2's `tap_server.exe shaders`.
- DXBC checksum: implemented in `dxbc.cpp` (MD5 with the DXBC final block; described in `dxbc.h`); it matches the
  stored checksum of all 1,417 blobs and the containers the official d3dcompiler writes for every container length
  class (tests) [C]. A corrupted byte fails with BadMagic in bytes 0-3 and with BadLayout in any chunk byte.

### 2.4 Facts the index shows about the terrain-side blobs (identification is task 2903's)

All in set 5_0, created by `0x4e5c20`; handle globals match GATE2's material 73 passes (VS `c8c`, PS `cb8`/`cbc`,
HS `cf4`, DS `d00`) [C]. Nine blobs: two VS, two HS, one DS and four PS, each with its create call site and handle
global in the product's generated create-site table (`render/src/shaders/create_sites_table.inc`, made by the
create-site tool of section 2.2) and its declarations readable with the decoder (`shaderdump`, section 2.3). What the
decoder shows that matters for the terrain work: the tessellated terrain VS takes no vertex input but `SV_VertexID`
and reads a height texture; the two HS and the DS work on a quad domain with 16 control points and integer
partitioning; the first terrain PS binds ten textures, two of them (t6, t7) 3D. (Per-blob index replaced by this
description, 2026-11-18, D83 audit.)

The handle globals of the terrain VS/HS/DS are SM5-only; the PS handles `0x2de1cb8`/`0x2de1cbc` are written by all
three sets, so the 4_x sets have their own terrain PS at the same handle [C].

### 2.5 API (namespace `c5r::shaders`)

    ShaderSection sec; sec.open(L"…/CivilizationV_DX11.exe");      // reads the file; Status on error
    for (const ShaderBlob& b : sec.blobs()) { b.va; b.size; b.stage; b.major; b.minor; b.set; b.createSite; b.handleGlobal; b.bytes; }
    const ShaderBlob* t = sec.findByVa(0x067d6490);
    ShaderReflection r; reflect(t->bytes, t->size, r);              // r.cbuffers, r.cbUse, r.resources, r.input, r.tess, ...
    d3dDisassemble(...), d3dReflectInfo(...), compareWithDisassembly(r, text, diffs);   // d3dcheck.h
    PeImage (pe.h), parseDxbc / dxbcChecksum (dxbc.h)

Tool: `shaderdump <exe> --out DIR | --index FILE | --va 0xVA | --check [LOG] | --device`.

### 2.6 Open points

- `usedMask` is an upper bound (destination-mask narrowing only for component-wise ops and dp2/dp3; sample, dp4 and
  the rest count every named component). Member boundaries inside a constant buffer are not recoverable from the
  blobs (no RDEF); task 2903 has to take them from the EXE's constant-buffer fill code.
- The level value 8 (4_0 set) comes from a second path of `0x8bb640..` not followed here [I].
- Not verified live: which handle a given draw uses (GATE2's material-table reading is static).

## 3. Terrain-side shader sets and their constant-buffer layouts (task 2903)

Done 2026-11-11 (session 00:13 to 01:00 EST, see tracker 2903). Static analysis of the EXE copy named above (build
4390913): headless Ghidra 12.1.4 (`-readOnly -noanalysis`) on a copy of the `civ5` project, an `llvm-objdump` listing, and
the 2902 decoder output; no game ran. Code: `render/include/c5r/terrainsets/terrainsets.h`, `render/src/terrainsets/`
(`sets.cpp` data, `validate.cpp` checks), tests `render/tests/terrainsets/`. Evidence: `evidence/2900/2903/` (scripts and
their outputs; the scripts expect the scratch tree `D:/c5-64bit-work/2900/2903/` with the listing `dx11.asm`).

### 3.1 How the EXE organises shaders: materials, programs, draw objects [C]

The answer to "which blob draws what" is a table in the EXE, not a guess from signatures:

- **Material table** `0x5d1e5c0 + m * 0x520` (94 materials, filled by straight-line generated code `0x4eebf5..0x4f5ca0`
  inside the shader init `0x4e5c20`): up to 64 passes of 5 words `{VS, PS, GS, HS, DS}` = the 16-bit handle globals
  2902 found (`create_sites.csv`), then at `+0x500` a header word, `+0x508` the D3D buffer of constant slot 0 (created by
  `0x8bcfb0(size, &out)`, sizes 0x10..0xf80) and `+0x510` its size. Material names: the pointer table `0xc9e890`
  (`{name, index}` pairs, `ArrowShader` = 0 .. `WorldCurrents` = 93), the same order as the strings at `0xa96334`
  (`.\shaderautogen\ShaderDefDX11.cpp`). Rebuilt by `materials.py` (`material_table.txt`, `material_passes.csv`).
  **GATE2's "material 73 = terrain tessellation" is right: material 73 is `TerrainShader`.**
- **Texture-slot names**: pointer table `0xc9e718` (one per material) to 16 entries of `{name, alt name, 0, kind}` (kind
  0 = 2D, 1 = 3D, 2 = cube); the entry index is the register `t#` (checked on WaterShader against the river draw object).
  Materials 73-78 and 69 have empty tables (`.bss`): their textures are bound by name-less code. `slot_names.txt`.
- **Shared constant buffers**: `0x8bd0a0(id, size)` at init creates 43 buffers (ids 1..0x2b) plus 3 (0x81..0x83) in
  `0x80cd10`; objects at `0x4f1dd88 + id * 0x1c`, the pointer of id `k` in the global `0x2de056c - 4k`. A CPU block is
  mapped with `0x8cfd50` (typed wrappers like `0x6be670` for 0x250 bytes) and uploaded at draw time.
- **Programs**: 97 constructors call `0x8c6a50(listA, listB, listC, ownCb, nA, nB, nC, material)`; descriptor at
  `0x4c4a198 + k * 0x30`. At draw time (`0x8bf5b0`) **slot 0 = the program's own block** (copied into the material's
  slot-0 buffer with Map/memcpy), **slots 1..nA = the shared buffers of listA, the same for VS, PS, HS and DS**
  (`VSSetConstantBuffers` vtable 0x1c, `PSSet..` 0x40, `HSSet..` 0xf8, `DSSet..` 0x108). `programs.txt`.
- **Draw objects** (`0x8c69b0(&obj, obj, material)`, then fields): `+0x18 + 2i` texture handle of `t i`, `+0x4c + 2i`
  sampler-state id, `+0x7c` count; VS textures `+0x6c`/`+0x74`/`+0x7d`; `+2` = 1 selects
  `D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST` (0x30) and 16 indices per primitive. `0x8bd160` turns handles into
  SRVs; a sampler state whose flag (`renderer+0x30568 + id*8`) is 1 takes the texture's second view [I: sRGB].
  `draw_objects.txt` lists every builder by material.
- **Sampler states** (13, `0x8be700`, emulated by `samplers.py`; all MinLOD 0, MaxLOD FLT_MAX, bias 0, comparison NEVER
  except 12):

| id | filter | address | aniso | 2nd view | | id | filter | address | aniso | 2nd view |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | MIN_MAG_MIP_LINEAR | wrap | - | no | | 7 | linear | wrap | 8 | yes |
| 1 | MIN_MAG_LINEAR_MIP_POINT | wrap | - | no | | 8 | linear | clamp | 8 | no |
| 2 | linear | wrap | - | yes | | 9 | MIN_MAG_MIP_POINT | wrap | - | no |
| 3 | linear | clamp | - | yes | | 10 | point | clamp | - | no |
| 4 | linear, point mip | clamp | - | yes | | 11 | point | clamp | - | yes |
| 5 | linear | clamp | - | no | | 12 | COMPARISON linear, LESS_EQUAL | clamp | - | no |
| 6 | linear | wrap | 8 (filter stays linear) | no | | | | | | |

### 3.2 Identification: set -> blobs -> system -> evidence

Blob VAs per pass and shader level: `material_passes.csv` and `render/src/terrainsets/sets.cpp` (the table's blob
lists were replaced by the pass structure, 2026-11-18, D83 audit; the VAs are in those two files). SM5 = level 10; every
pass has 4_1 and 4_0 equivalents except the tessellated terrain VS/HS/DS (SM5 only).

| Set (material) | Passes, SM5 blobs | EXE system and draw | Evidence | Conf |
|---|---|---|---|---|
| TerrainShader (73) | 0-2 tessellated (VS, HS, DS, PS; pass 2 has its own HS; three PS); 3-5: a plain VS with the same PS; 6-7: a third VS | TerrainSystem render `0x657070` -> TerrainRenderer draw `0x4241e0` -> per tile `0x5c37e0`; draw objects `0x5ad770` | program ctor `0x6f6000` names "TerrainShader", material 0x49; `0x5ad770` sets material 0x49; pass choice read in `0x5c37e0` | C |
| CacheBake (75) | one VS; two PS (1 target, 2 targets) | tile bake `0x694a60` ("Terrain Cache" targets tile+0x190/+0x194), draw object `0x6cf830` | ctor `0x546070` "CacheBake"; **GATE2 called these two PS "water"; they are not** | C |
| TerrainShaderScreen (76) | 5 passes, four VS; among the PS one with 16 textures and one with 24 textures and 3 targets | tile bake `0x694a60`: blends terrain pieces into the "Terrain Albiedo/Normal/Spec Temp" targets | ctor `0x4234d0`; builders `0x695508`, `0x695ae2` | C |
| NormalCalculate (74), CompositeShader (77), CompressBC (41) | see csv | TerrainRenderer programs (TR8+0x5f8, +0x5c8, +0x658); "Normal Calculation" and "DXT Compress" passes of `0x694a60` | ctors `0x537260`, `0x5ea6c0`, `0x76ba70`; constructed in `0x458420` | C (77: use I) |
| TerrainDecalShader (78) | one VS; two PS (2 targets; 3 targets, the one used) | TerrainDecalSystem batch draw `0x742ef0` (pass 1), into the bake: **the blend's three 512 R16G16B16A16_FLOAT targets (albedo, normal/height, spec), mip 0, after the blend and before NormalCalculate**, SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE); the tree-shadow decals with the same PS into the macro-shadow target alone, ONE / ONE. *Corrected 2026-11-15, epic 0166 (terrain-decals.md 4.1, 4.4, [L] on 7 captured decal bakes; replaced: 'into the bake ("Decal Layer")', mark 'C (target I)'): no separate decal layer, no CacheBake slot; pass 0 never drawn* | ctor `0x45ee20` (the TerrainDecalShader object at TDS+0x18, terrain-decals.md 1.1); builder `0x7432e8` | C, target L |
| WaterShader (91) | two VS (ocean, river); six PS (three ocean, two river, one more) | ocean/coast/lakes: `0x739ae0` (from `0x657070`); **rivers: RiverVisSystem draw object `0x6b1580` also uses WaterShader** | ctor `0x60f0b0`; builders `0x73ac66`, `0x6b1614`; slot names LEAN1_1.. | C |
| WaveShader (92), WorldCurrents (93) | see csv | coast waves (`0x6a6490`); the "Water Currents" 256x256 target `0xda12c4` | ctors `0x4bb550`, `0x774420` | I (draw) |
| RiverShader (69) | one VS, one PS | none found: no draw object of material 0x45 | ctor `0x5b0ba0` | I |
| DrawSimpleOverlay (67) | 6 passes, two VS | 10 builders; Overlay_System render `0x6ead50` draws into "Terrain Overlay" `0xda12a4` (culture borders, then "Post Culture Borders Z Clear", then overlays) | ctor `0x6da5a0`; `0x6ead50` fills its shared buffer 29 | C (borders I) |
| FOWBaseLayer (53), FOWDrawMask (52) | see csv | FluidFOW_System (`0x44a570` builds the draw objects, creates "FOW Mask" `0xda12ac`) | builders inside `0x44a570` | C |
| FOWParticleShader (51), FOWCell (50) | see csv | `0x68f470`; FOW_System start-up `0x5bb6e0` | | I / C |

*Corrected 2026-11-14 by issue 3957 (epic 0168) from `fog-of-war.md` 2.1, 2.3, 3.5-3.7 and 7.1-7.2 (tasks 3950 [C], 3951
[L], 3952, 3955 [L]); the two rows above are kept. Current roles: **FOWDrawMask (52)** passes 0-2 draw the mask "FOW Mask"
`0xda12ac` (pass 2 seen, 1 visible, 0 unexplored; MAX blend, 24 hexagons a draw), pass 3 is FOW CULL (depth only), pass 4
a visualiser; **FOWBaseLayer (53)** draws the three cloud planes into the scene after the models (t0 cloud
`fogcloudtile_dif_02/03/01.dds`, t1 the mask; it does not render the mask or the base layer); **FOWParticleShader (51)**
is FluidFOW's particle draw (draw object `+0x85C` of `0x68f470`, t0 `fx_fog_dif.dds`, t1 the mask), FOWLevel 3 only;
**FOWCell (50)** belongs to FOW_System (`0x5bb6e0`, "FOW.cpp") and draws only at FOWLevel 0.*
| IceShader (59), GridShader (58) | see csv | polar ice `0x4956f0`; GridShader draws UI nine-slice panels; the hex grid is pass 4 of DrawSimpleOverlay into the Terrain Overlay target (corrected 2026-11-14, `overlay-system.md`; was "hex grid `0x6ab930`"). *Corrected 2026-11-15 by 4301 / 4306 (epic 0169, `polar-ice.md` 2, 3, 7; replaced "polar ice `0x4956f0`" as the draw and the mark I for the ice): `0x4956F0` is the ice **mesh builder** (it makes one material-59 draw record per chunk at `0x495CEB`, hence the table's entry); the **draw is `0x5FACB0`**, called by the TerrainSystem render `0x657070` at `0x6570FA` (scene) and by the reflection pass `0x5A0F80` through `0x751F30` (behind view bit 0x1 at `0x5A103E`); one SM5 VS and one PS; cb1 = IceFrame, shared id 42, 480 B, row by row in `polar-ice.md` 7* | | C ice (4301, 4306) / C grid |

### 3.3 Constant-buffer layouts

Slot 0 of each set is its program's own buffer; slots 1.. are shared ids. Full member lists with the source of every
value: `sets.cpp`. The three that drive terrain and water, all filled once per view `v` (map-wrap copies, count
`0xd90fa4`):

**TerrainFrame, shared id 11, 0x250 bytes, slot 1 of TerrainShader / CacheBake / TerrainShaderScreen**, written by
`0x6add60` (this = TerrainRenderer `TR`) [C]:

| Offset | Member | Value |
|---|---|---|
| 0x000 | ViewProj float4x4 | g_View[v] (`0xda1310`+0x40v) x g_Proj (`0xda12d0`) |
| 0x040 | OverlayProj float4x4 | `0xda1690`+0x40v: one matrix per view v (stride 0x40, `g_NumViews` views; wrap copies take their own view's matrix), view-sized top-down ortho of Overlay_System `0x51b450` (world -> Terrain Overlay uv), snapped to 64 world units [C, `overlay-system.md` 4, corrected 2026-11-14] |
| 0x080 | (not written) | |
| 0x090 | FOWAnim float4 | `0xda1c40..4c` = (FluidFOW+0x1338, same, time * k, 0), FluidFOW update `0x4090f0` |
| 0x0a0 | BicubicBasis float4x4 | rows (-1/8, 7/8, -7/8, 1/8), (3/8, -3/2, 9/8, 0), (-3/8, 0, 3/8, 0), (1/8, 3/4, 1/8, 0) |
| 0x0e0 | TessFactor float | float(TR+0x2428): `(1 << int(lod * -0.75)) * [0xd90f14]` or `[0xd90f14]` (`0x4241e0`) |
| 0x0f0 | TessFactorOne float | 1.0 (read by HS `0x067f9cb8`) |
| 0x100 | HexScale float3 | (1.0, 0.866, 0.2) = `0xb9a6d4..dc`: plot x step, y step, height unit [I names] |
| 0x110 | AuxDir float3 | (0.707, 0, 0.707) set by the start-up `0x678290` |
| 0x120 | LightDir float3 | TR+0x40c [I: sun direction] |
| 0x130 | CameraPos float3 | g_InvView[v] translation |
| 0x140 | CameraAxisZ float3 | g_InvView[v] third column |
| 0x150 | HalfVector float3 | normalize(LightDir + CameraAxisZ) |
| 0x160 | HeightTexelScale float2 | (1/1024, -1/1024) |
| 0x170 | BakeTexelScale float2 | (1/512, 1/512) |
| 0x180 | MapSize float2 | map width, height in plots (GridHeightmap+0xc/+0x10) |
| 0x190 | Arg6 float | `[0xd90fa8]` [C, 2026-11-14 issue 0190: the eased shadow factor GVS+0x78C the shadow pass 0x523CA0 writes; 1.0 in 3601's captures; terrain-surface.md 9; was [I]] |
| 0x1a0..0x1e0 | flags (one float per register) | TR+0x2404 == 2; TR+0x241c; TR+0x2418; 1.0; TR+0x240c |
| 0x1f0 | ShadowMatrix0 float4x4 | `0xda1790`+0x40v (shadow view setup `0x615950`) [I use] |
| 0x230 | ScreenSize float2 | back-buffer width, height (`0x301056c`, `0x3010568`) |
| 0x240 | OverlaySize float | TR+0x528 = 2048.0 |

*Pointer 2026-11-13 (issue 3406): AuxDir and LightDir in play are (0.4330, -0.25, 0.8660) and (0, -0.4226, 0.9063),
set by 0x5BCEF0 right after 0x678290's placeholders; MapSize holds samples, not plots; at bake time the whole buffer is
written with an identity view. Current values and sources: `terrain-bake-passes.md` section 5.3.*

**DistanceFog, shared id 19, 0x20, slot 2 of TerrainShader**: FogColor (DistanceFog colour `0xd90ed4..dc`, 1),
FogParams = (-density `0xd90ecc`, exp(-density * h) * k / density * `0xda1c64`, 0, `0xd90fac`) [C].

**TerrainTile, own, 0x70, slot 0 of TerrainShader**, written per tile by `0x5c37e0` [C]: TileWorld float4x4 =
Translation(-half map width, -half map height * 0.866, 0); TileOffset float4 (tile+0xdc..e8, as float); InvGridSize
float2 (1/(w+1), 1/(h+1)); LodStep float2 at 0x60; LodMip float at 0x68 (TR+0x2424).

**WaterFrame, shared id 26, 0x250, slot 1 of WaterShader**, written by `0x739ae0` [C]: ViewProj 0x000, OverlayProj 0x040,
wave scale / phase / constants 0x080-0x0c0, Water fields 0x0d0-0x130 (Water+0xd0, +0x90, +0x9c, +0x70, +0x80, +0xa8,
+0xb8), FogParams 0x140, camera position 0x150 [I], ShadowMatrix1 (`0xda1890`) 0x160, ShadowMatrix0 0x1a0, world ->
height-map uv 0x1e0, `[0xd90fa8]` 0x220, FogColor 0x230, Water+0x6c 0x240.

Smaller ones [C]: **TerrainDecal** own 0x70 (DecalViewProj, (1,1,0,0), type entry +0x24..+0x30, +0x20; `0x742ef0`;
*corrected 2026-11-15, epic 0166 (terrain-decals.md 1.2, 4.2, 11.2): +0x24..+0x30 and +0x20 are the drawn decal
record's rectangle and priority, not a type entry; DecalViewProj has the closed form of terrain-decals.md 11.2 [L]*);
**TerrainLayers** id 18, 0x600 = float4[96] (`0x6c47f0`); **OverlayFrame** id 29, 0x40 = one matrix (`0x43c370`).
Not traced (one array member each, names [I]): the own buffers of TerrainShaderScreen (0x4c0), CompositeShader (0x50;
its use by the VS is [C], see 3.6), CompressBC (0x10), WaterShader (0x80, river World), RiverShader (0x60), WaveShader
(0xf80), DrawSimpleOverlay (0xe0), the four FOW programs (0x40, 0x90, 0x600, 0xc50), GridShader (0x160); shared ids 27,
28, 31, 39, 42, 43.
*Pointer 2026-11-14 (issue 3957, epic 0168): three of the four FOW programs' own buffers are traced in `fog-of-war.md`
3.3, 3.5, 7.1 and 7.2 [C, L]: FOWDrawMask 0x600 = 24 matrices of 4 rows (per hexagon or cull quad), FOWBaseLayer 0xC50
(3,328 B: 24 WVP and World matrices per draw, OverlayProj at rows 192-195, (k, k, z x s, 0) at row 196),
FOWParticleShader 0x90; FOWCell's 0x40 is not read. `sets.cpp`'s FOWDrawMask row carries the layout (3952).*
*Pointer 2026-11-14 (issue 3602): the WaterShader own buffer, WaveShader's own buffer and the shared ids 27 and 43 are
traced (`water-system.md` 8.1, `sets.cpp`); `material_table.txt`'s `hdr0`/`cbsize` columns belong to the row above
(`materials.py` reads them at material i-1's offsets), so its sizes are not the row's own; `programs.txt` is right.*

**Check against the blobs** (`validateSet`, test `game_terrainsets_validate_against_exe`): every declared buffer fits its
layout's size, every register component the code reads is covered by a member, every dynamically indexed read starts
in an array member, every declared texture and sampler has a slot of the right dimension, every blob is of the stage
its pass slot says and is stored in the handle the material table names. Result in 3.7.

### 3.4 Textures and samplers

TerrainShader PS (draw object `0x5ad770`; `style` = lowest set bit 1..4 of tile+0x138, 0 if none; style order of
`0xb9a428`: Generic, America, Asia, Africa, Europe):

| t | Name | Bound [C unless marked] | Sampler |
|---|---|---|---|
| 0 | TileCacheColor | tile+0x198 (+0x190 for the second LOD object): the tile's baked cache, CacheBake target 0, BC-compressed by CompressBC [I: lit albedo] | 5 |
| 1 | TerrainOverlay | `0xda12a4` "Terrain Overlay" 2048x2048 target (borders, overlays) | 5 |
| 2 | DistanceFogRamp | `0xda12b4` `assets/DistanceFog/FogRamp.dds` | 5 |
| 3 | ShadowTexture | `0xda12bc` "Shadow Texture Blur" (2048 x 2048 R16_UNORM, world-mapped through ShadowMatrix0, not screen-space: corrected 2026-11-14 by issue 0190, terrain-surface.md 9) | 0 |
| 4 | FOWMask | `0xda12ac` "FOW Mask" (FluidFOW) | 0 |
| 5 | FOWBaseLayer | `0xda12b0` FluidFOW cloud texture (`FogCloudTile_dif_0x.dds`; *2026-11-14, 3957 from `fog-of-war.md` 2.4 / 3.6 [C, L]: `FogCloudTile_dif_02.dds` at FOWLevel >= 1, a 32 x 32 RGBA8 of zeros at FOWLevel 0; was "`_dif_0x`"*) | 0 |
| 6 | TransFOWColor (3D) | `0xd90e60[style]`: ColorKey TerrainFOW LUT | 5 |
| 7 | ShadowColorKey (3D) | `0xd90e74[style]`: ColorKey TerrainShadow LUT | 5 |
| 8 | TileCacheSecond | tile+0x19c (+0x194): CacheBake target 1 [I] | 5 |
| 9 | SpecScreenMap | `0xda12c8` `assets/terrain/lighting/specscreenmap.dds` | 5 |
| VS 0 | TerrainHeight | tile+0xec | 0 (tessellated LOD objects), 9 (others) |

*Corrected 2026-11-15, epic 0169 (replaced: t3 "2048 x 2048 R16_UNORM" as the only form). t3's texture depends on the
shadow quality q = `[0xD90F9C]`, which is the ini's **`ShadowLevel`**, not `TerrainShadowQuality` [L, 4304,
`terrain-surface.md` 11.9]; the shadow setup `0x52FA70` makes ShadowBlur per q [C, 4302, 11 S2b; L, 4304 at every q]:
q 3 a 2048 x 2048 R16_UNORM target, q 2 1024 x 1024 R16_UNORM, q 1 no blur target (`[0xDA12BC]` holds ShadowRaw's handle:
the back buffer's size, R16G16B16A16_FLOAT, unblurred; the PS reads .r), q 0 a 4 x 4 R8G8B8A8_UNORM of zeros (no shadow,
the factor 0). A settings or window change can re-create it under the same handle. Arg6 = the eased factor `[0xD90FA8]`
(1.0 at q 3; it fades with the zoom at q 1-2 [L]). **In the server** (the EXE's binding above is unchanged): t3, Arg6
and ShadowMatrix0 come from the shadow tap since 4308 (`terrain-surface.md` 11.10; R16_UNORM and, at q 1, the float
map), and t6 and t7 are the tile style's volumes since 4305 (`terrain-surface.md` 12.6; on every install tested the
five files of each type give one volume, 12.2).*

The ten handles `0xda12a4..0xda12c8` are one static block the TerrainSystem hands to the renderer's start-up
(`0x678290` copies it to TR+0x548); the other entries: `0xda12a8` "Screen Overlay" (Overlay_System `0x6fff79`),
`0xda12b8` "Shadow Texture Raw", `0xda12c0` "Reflection Texture Raw" (Water::Startup `0x5f06a0`), `0xda12c4` "Water
Currents". CacheBake adds t4/t5 = the style's Sunlight / Shadowlight cube (`assets/terrain/lighting/Sunlight_<Style>_
Terrain.dds`, `Shadowlight_<Style>_Terrain(_2).dds`, sampler 2), t8 = ColorKey Terrain LUT, t9 FOWMask, t10 TerrainFOW
LUT, t11 SpecScreenMap; t0-t3 are its arguments [I: the blended temp targets]. WaterShader (names from the EXE): t0-t3
LEAN1_1..LEAN2_2 (Water bump .lean textures; ocean binds two), t4 ReflectionMap, t5 WaterBlendMap (ocean: "Water
Currents"), t6 WaterColor, t7 WorldHeightMap (ocean: TR+0x12ac[cell]; river: River+0x24), t8 WaterFogRamp (= the
DistanceFog ramp), t9 OverlayMap, t10 FOWMask, t11 TransFOWColor (3D), t12 ColorKey (3D, rivers), t13
DynamicShadowTexture (its sampler word is never written by `0x739ae0`, which writes t11's twice: an EXE slip [C]).

### 3.5 The 3D textures: ColorKey LUTs built at run time [C]

`ColorKeySystem::Startup` (`0x543bb0..`, `.\App\Graphics\ColorKeySystem.cpp`) creates, for 5 types (`0xb9a414`:
`assets\ColorKeys\Terrain`, `TerrainFOW`, `TerrainShadow`, `Buildings`, `Rivers`) x 5 styles (`0xb9a428`: Generic,
America, Asia, Africa, Europe), a **32 x 32 x 32 R8G8B8A8_UNORM volume** (`FTextureData` `0x8c8fa0(format 0x1c, 32, 32,
32, 1 mip)`), first filled with the identity (callback `0x752b20` writes (x, y, z, 1)), then loads
`%s_%s_ColorKey.tga` (`0x681a00`: decodes the image and copies **0x20000 bytes** = 32^3 x 4 of its pixels into the
volume). Handles in the table `0xd90e4c + type * 0x14 + style * 4` (TerrainFOW at `0xd90e60`, TerrainShadow `0xd90e74`,
Buildings `0xd90e88`, Rivers `0xd90e9c`). Files: `Assets/ColorKeys/` (loose; 22 TGA, e.g. `Terrain_Generic_ColorKey.tga`
1600x21, `TerrainFOW_*` 3840x9, `TerrainShadow_*` 1280x26, all 32-bit, origin bottom-left; each holds at least 32,768
pixels). [I]: the texel order of the copy (TGA row order, BGRA vs RGBA) - a server re-implementation must check it against a
live capture or the debug save path `0x49c130`. A missing file leaves the identity LUT, so a server can start with an
identity LUT and look right except for the colour grading.
*Settled 2026-11-13 by 3403 [L] (`terrain-bake-passes.md` 5.2; noted here 2026-11-15 by epic 0169; replaces the [I] on
the texel order above): decode the TGA into reading order (bottom-left origin: the last file row first), BGRA -> RGBA,
take the first 32,768 pixels (x fastest, then y, then z) as the volume; equal to the captured Terrain LUT byte for byte,
alpha included; the server's TerrainFOW, TerrainShadow, Terrain and Rivers volumes built this way equal the captures'
(`terrain-surface.md` 12.1). **Per style** (4303, `terrain-surface.md` 12.2 [L], ten installs, VT present): of the 25
names 21 exist (`Rivers_{America,Asia,Africa,Europe}` are missing, so those slots keep the identity in the EXE and the
server; the river draw uses Generic); the five files of each of Terrain, TerrainFOW, TerrainShadow and Buildings give
one volume per type, and Terrain's equals Buildings'; `Terrain_Africa` and `Terrain_Europe` differ from the others as
files only after the first 32,768 pixels. The 22nd TGA of the folder is `TerrainColorKey.tga`, which fits no type /
style name [L, listing of `pure_vp`, 2026-11-15]. No mod on the instances overrides a ColorKey file.*

### 3.6 For the server (I2-E): what drives these shaders, and the simplest pair for quadtest

- **Terrain draw is reusable as is**: the tessellated path needs no vertex buffer (SV_VertexID over a 16-control-point
  patch index list per tile), one height texture per tile, TerrainTile (7 registers, simple per-tile values), TerrainFrame
  (camera matrices, light, map size, tessellation factor) and DistanceFog. The server computes all of these from the
  camera and the map. The non-tessellated passes need the tile's vertex buffer (POSITION, NORMAL, TEXCOORD0).
- **The hard part is the bake, not the draw**: the terrain PS does not blend terrain textures; it samples **per-tile cache
  textures** (t0/t8) that the EXE bakes on the GPU: TerrainShaderScreen blends the terrain pieces of the cell (16-24
  textures: up to 8 layers of the tile, layer `k` = TR8+0xf10[tile+0x1fc[i]], t i = TR8+0x708[k], t 8+i = TR8+0x908[k];
  the 0x4c0 own buffer holds one 0x80-byte record per layer, not decoded; *2026-11-13, issue 3023: the per-tile list
  tile+0x1F8/+0x1FC, the TR8 tables and the bake's vertex format are decoded in `terrain-bake-records.md`; texture set k
  = row k of `TerrainDef.csv`*) into temp targets, NormalCalculate, decals
  (TerrainDecalShader), CacheBake lights them (sun/shadow cube per style, Terrain ColorKey) into two "Terrain Cache"
  targets, CompressBC compresses them. I2-E either re-implements this bake (the own buffers and the per-slot piece textures
  of TerrainShaderScreen still need tracing in `0x694a60`) or takes the cache textures from the EXE. Recommend tracing
  `0x694a60` next; it is the single function that runs the whole bake.
- **Inputs that are other systems' render targets**: Terrain Overlay (borders and hex overlays, Overlay_System), FOW Mask
  and the FOW cloud layer (FluidFOW), Shadow Texture Blur (screen-space shadow), Reflection Texture (water), Water
  Currents. For a first image the server can bind constant textures (overlay transparent, FOW mask "visible", shadow
  white, identity LUTs) and add each system later.
- **Water** needs WaterFrame (traced) and the Water object's fields (+0x6c..+0xe4, read from TerrainSystem+0x2772c; their
  meaning is [I]); rivers use the same WaterShader with their own vertex buffer and the river ColorKey.
- **Simplest VS/PS pair for task 2906 (quadtest): CompositeShader pass 0, VS `0x067e4fd8` + PS `0x067f7cb0` (SM5;
  4_1: `0x067e5310` + `0x067f7e48`).** VS input POSITION (R32G32B32A32_FLOAT per its signature, `.xyz` used), NORMAL
  (R32G32B32_FLOAT, unused), TEXCOORD0 (R32G32_FLOAT); cb0 = 0x50-byte buffer whose first 64 bytes are a row-major
  float4x4 `T` with SV_Position = (x, y, z) . rows 0-2 + row 3 (identity = positions already in clip space); PS = `t0.Sample(s0,
  TEXCOORD0)`, any 2D texture (e.g. a terrain DDS), sampler linear wrap (state 0). Next simplest: TerrainDecalShader pass 0
  (two targets) or the TerrainShader non-tessellated pass 4 (needs 3 buffers and 8 textures). *(2026-11-15, epic 0166:
  the EXE draws decals with pass 1, three targets, terrain-decals.md 4.1; pass 0 was never seen drawn.)*

### 3.7 Results, not verified, open points

- Test run 2026-11-11 00:57 EST (`python render/build.py --test --build-dir D:/c5-64bit-work/2900/build-2903`, clang-cl
  /W4 /WX, all 5 ctest executables pass): `c5r_terrainsets_test` 14 tests, 0 failed, 0 skipped. **18 sets, 342 blob
  references (SM5, 4_1, 4_0) validate against the EXE**: stages, create-site handles = material table, every declared
  cbuffer within its layout, every read register component covered, dynamic reads in arrays, every texture and sampler
  slot of the right dimension. Every check has a negative test on synthetic shaders compiled in the test
  (`terrainsets_test.cpp`).
- Not verified live [L]: which pass each draw selects at the owner's settings (TerrainTessLevel, BicubicTerrainTessellation),
  the real values of the TR and Water fields, the ColorKey texel order, and that rivers draw through `0x6b1580`
  (static reading only). RenderDoc was not run.
- Open: the TerrainShaderScreen own buffer and its 24 texture slots; the WaterShader own buffer (river World); the
  shared buffers 27, 28, 31, 39, 42, 43 (*2026-11-15, epic 0169: 42 is IceFrame, traced row by row in `polar-ice.md` 7*); blend/depth/rasterizer states of the terrain passes (not recovered: the
  material header words at +0x500 and the pass state were not decoded); which draw uses TerrainShader passes 2, 5 and 7
  (PS `0x067ea680` reads only the overlay); the meaning of the TR flags at +0x2404..+0x241c.
- Correction to GATE2 section 2 (verbatim source, not edited): the "water PS `0x067ebe78` / `0x067ed018`" are the
  CacheBake pixel shaders of the terrain bake; the water pixel shaders are `0x068179f0`, `0x0681b500`, `0x06820b00`
  (ocean) and `0x06819f60`, `0x06826ea0` (river). The terrain PS's 3D textures are ColorKey LUTs, not noise
  (2026-11-11).

## 4. Height field: re-bake or tap (task 2904)

**Decision (2026-11-11): tap.** The server takes the EXE's finished height field after `CreateTerrain` returns; it does
not re-compute it. The field is deterministic in principle (seeded by the map's random seed), but a re-bake means
porting six EXE stages, three of them rand-driven, with x87 float semantics, and could only be checked against a live
dump; the tap is exact by construction, is a few hours of work, and is what D13 asks for (the EXE computes it, the
server takes it). Static analysis only: Ghidra 12.1.4 headless on a read-only copy of the project, the EXE copy
(SHA-256 b2ce96ae...), the install copy's `TerrainPiecesLibrary.fxsb`. Nothing here was run live. Evidence and the
scripts: `evidence/2900/2904/` (`rand_in_bake.txt`, `StrXrefDecomp.java`, `pe.py`, `gh.sh`).

### 4.1 What fills 0xD91270 at load [C]

`LoadGameState::StartInitTerrainFromGameplayMap` 0x60ABB0 builds the per-plot table, then calls
`TerrainSystem::CreateTerrain` 0x709B50 (this 0xD165E0, args: the table, 1, `PreGame::mapRandomSeed()`; the seed is
`ICvPreGame1` vtable +0x118, read through the DLL's game context +0x1C). CreateTerrain, in order:

1. `srand(mapRandomSeed)`; TerrainDef.csv; the lighting DDS files.
2. `GridHeightmap::Init` 0x47E350 (ecx 0xD91270; W*64, (H+4)*64, sx 1.0, sy 0.866, sz 0.2; the row count includes
   4 plot rows of margin, see the 2026-11-11 correction in point 5 of 4.2): both layers are allocated in
   full and zeroed (every tile pooled), ready byte +0x09 = 1. Then `[0xD91274] = TerrainSystem+0x277F4 * 0.2 * 255`
   (water height).
3. 0x489CC0 (the coast grid: two points on every land/coast and coast/ocean hex edge, linked into chains, smoothed
   three times and pushed out by 4 samples at mountains), 0x59BBC0 > 0x491050 (the chains as polylines, in a fixed
   scan order) > 0x5674C0 (each jittered with `rand() % 1000` on a multiresolution pyramid; the first rand user after
   `srand`), 0x4BEF10 > 0x73C170 (the coast mesh's five lines per polyline; the middle one, the waterline, three
   Chaikin rounds plus the coast style's character offsets), then CreateTerrain's own offset lines one sample to
   either side of each waterline (tangents from the 3-point derivative 0x4595B0) and their self-intersection cleanup
   (0x67D5F0, 0x6545F0). The waterlines and the offset lines are the boundary and fill lists of the region fill in
   step 5. *Corrected 2026-11-20 (epic 0615, ticket 4910; full description `live-terrain.md` 8.10.1) [C]; was:
   "coastline curves, jittered with `rand() % 1000`; CreateTerrain then resamples and smooths them (0x4595B0 spline,
   0x6545F0 segment intersection)".*
4. `LoadTerrainPieces` 0x6A00A0 > 0x404170: loads `Resource/Common/TerrainPiecesLibrary.fxsb` when its first dword is
   0x14, otherwise the flat files under `assets/terrain/pieces`. The install copy's file starts with 20, so the binary
   library is the one used [C check + file, L not seen]. Its format is not decoded: 27,595,584 bytes, `20, 385024, 0,
   2, 4, ...`. It is not a cache of the 975 MB of piece DDS files, which it is far too small to hold [I].
   *Corrected 2026-11-18 (epic 0465, child 4902, W3; `live-terrain.md` 7.3) [C, W3]: the format is decoded (version 20,
   a row table, byte and u16 run-length arrays, 299 piece records of 0x56C bytes: a 10x10-cell pattern, a level 1-4 and
   four 128 or 512 square textures: height u16, material index, height weight, material weight; region u16 0 land, 1
   coast, 2 ocean). The flat DDS files are not the same data (heights equal in 53 of 299; T1 is a material index in the
   library and a colour image in the DDS), so the library is the reference. The EXE frees its loaded copy right after
   the blend, so it cannot be read live after the load. Was: "Its format is not decoded".*
5. `BlendTerrainHeightmap` 0x6BA6F0 > 0x425060 (`cvTerrainBlend.cpp`): one job per 64x64-sample block (one plot)
   through the engine job system (0x56B480; job vtable 0x9E04E8, body 0x608270). Per block it rasterises the coast and
   river polylines into a region mask with a flood fill (0x6414D0). It accumulates every placed piece whose region
   matches: `acc += (h16/65535 - offset) * w/255`, `wsum += w/255`, with h16 from the piece's height texture and w from
   its `_H_BLEND` mask, mirrored when the piece rectangle is reversed (0x463B10; readers 0x4BCD60 / 0x7940A0 /
   0x42E3A0). It also keeps the material byte of the piece with the highest `_T_BLEND` weight. It writes
   `round((acc/wsum + offset) * 65535)` through the setter 0x5A3490 and the material byte through 0x775130 (0x70B490).
6. `BlendAllCoastMeshes` 0x639D40 (`cvCoastMesh.cpp`): coast-strip jobs (vtable 0x9E4944, 0x626500 > 0x6990D0)
   write heights again along the coasts. On the load thread, `rand()` first picks one strip and then one of its
   segments for a special value (0x4173D0 - 0xFF).
   *Extended 2026-11-20 (epic 0615, ticket 4911; the full description is `live-terrain.md` 8.15) [C]:*
   - **The geometry.** The strip mesh is built first, by 0x4B2100 > job 0x9FA43C > 0x55F9F0, from the five offset lines
     per coast that 0x73C170 makes in step 3. The five lines sit at the CoastStyles.xml offsets: -20, -1.5, 0, +1.5,
     +20 samples in stock. Each line has 8n - 14 points.
   - **The bands.** There are four bands of two triangles per column step. Each vertex carries the style's height and
     weight (1 on lines 1-3, 0 on lines 0 and 4) and a band material: `HighBeach` / `LowBeach@TerrainType`, which is
     code 5 and resolves to material 3 `Euro_Coast`.
   - **The raster.** Every sample inside a triangle gets the new height `trunc((hv w + old (1 - w)) * 65535)`, where hv
     is the barycentric height and w the quadratic edge-softened weight. It also gets the first vertex's material. If
     the new height is at most 35,000, the American mountain top becomes the mountain base.
   - **The order.** The jobs run in two waves, even segments first, then odd ones.
   - **Correction of the special segment.** The "special value" has **no effect** on the field. The job only tests it
     to take an unused 128-byte temporary allocation. Its source, 0x416CF0, is a CEG routine. The two `rand()` draws
     count only for the stream position of step 7. Was: "for a special value", with the effect left open.
   - **The blender constants.** The blender's constants 15423 / 7650 / 13000 (+0x1D0..+0x1D8) are not read by this
     step.
7. River mesh 0x6D51D0 (rand in 0x631990 and 0x54BAB0), then 0x412680 carves the rivers and lakes. It rasterises
   triangles (0x5FC1C0 > 0x46AEF0: lerp toward the triangle's depth, never above the old height, never below a fixed
   floor of 0.2353 - 0.06 = 0.1753 (60/255 minus 0.06; corrected 2026-11-18 from ticket 0556,
   research/02-exe-internals/community-lake-victoria-thick-river.md; was: "never below the old height - 0.06")) and runs 0x4A7DD0 > 0x7A1FD0 > 0x772510 (blend to a target height). In x it wraps as
   `(W*100 + x) % W`.
   *Corrected 2026-11-20 (ticket 4911, `live-terrain.md` 8.15) [C]: 0x412680 carves **rivers only**.*
   - Its triangles come only from the river records, which 0x4954C0 builds from the river centre lines. It has no lake
     path.
   - "Rivers and lakes" above means the river carve meets a lake only where a river leaves or enters one (0556's thick
     river).
   - A lake's basin is made by the coast pieces of step 5 (lake plots are coast class, 4.2 point 1) together with the
     region fill and the step-6 strips along its shore.
8. Ice (0x5A0BC0), marsh replay (0x63B0F0, byte layer only), decals, the renderer (0x5BCEF0), which reads the field.

No step writes heights after load. The only writers of 0x5A3490 are the steps above, the editor loader 0x74D1A0 and
0x40B410, whose only reference is a pointer in .rdata. *Corrected 2026-11-18 by epic 0465 (4900, `live-terrain.md` 4.1) [C static, I for reachability]: 0x40B410's Init call targets its own 64 x 64 grid on the stack, not 0xD91270; 0x74D1A0 is the Nexus `TerrainEditorSystem` heightmap load, which builds its own renderer and is not reached in a game [I]. So neither writes the game's field.* CreateTerrain's only caller is LoadGameState. **The field is
static from the end of CreateTerrain until the next load**: plot changes made by the DLL during play (terrain or
plot type changes from Lua) do not reach it [C, static closure; indirect calls not exhaustively followed]. This
confirms the memory note "no live hills>mountain channel".

### 4.2 Answers to the five questions

1. **Inputs [C].** 0x60ABB0 fills an 8-byte HexPatternCell per plot: `{u32 flags, u8 continent (ICvPlot1::GetContinentType)}`.
   Flags: coast 0x2000, ocean 0x4000, mountain 0x400. Flat land is grass 0x1, plains 0x2, desert 0x4, tundra 0x8,
   snow 0x10; hills are grass 0x20, plains 0x40, desert 0x80, tundra 0x100, snow 0x200. Any other terrain id takes
   the plains bit. A natural wonder (the feature info's +0x24 test) replaces the flags: GEYSER 0x8000, CRATER 0x10000,
   GIBRALTAR 0x4000000, FUJI 0x40000, MESA 0x80000, REEF 0x100000, VOLCANO 0x200000, POTOSI 0x20000, ULURU 0x400000,
   MT_SINAI 0x800000, MT_KAILASH 0x1000000, SRI_PADA 0x2000000, KILIMANJARO 0x800, LAKE_VICTORIA 0x1000,
   SOLOMONS_MINES 0x4. FOUNTAIN_YOUTH and EL_DORADO keep the terrain value; any other wonder, including VP's own,
   gets 0x40000. A null plot is 0x4000, and pattern-grid cells that no plot covers keep the pre-fill 0x2000.
   A second pass reads river edges and flow directions (`IsNEOfRiver`/`IsWOfRiver`/`IsNWOfRiver` +0x40/+0x44/+0x48,
   flows +0x4C/+0x50/+0x54) into river records. It also reads ice (feature 0, 0x639850), marsh (feature 2, 0x630B70)
   and flood plains (feature 4, 0x6BF2A0). Plot to grid uses the hex matrices at 0xD91170..0xD91200. All of it goes
   through the DLL's `ICvPlot1` interface (slots +0x18 IsRiver, +0x64 IsHills, +0x6C IsMountain, +0x70 GetTerrainType,
   +0x74 GetFeatureType, +0xD8 GetContinentType). Coded as `c5r::heightfield::patternCellFlags`. Lakes have no
   separate flag here [I].
2. **Randomness [C].** CreateTerrain seeds the CRT with `srand(mapRandomSeed)`. rand() then shapes the coastline
   jitter, the coast-mesh special segment and the river mesh, all on the load thread. The block-blend and coast job
   bodies call no rand. So the field is a function of (map, mapRandomSeed, game files, rand order). Two things are
   not established: whether the load thread runs foreign jobs that call rand while it waits (0x81D790) [I]; and
   whether the piece choice in the matcher calls rand. *Answered 2026-11-18 (epic 0465, child 4902, W3; rules in
   `live-terrain.md` 7.1-7.2) [C, W3's static read]: the matcher is at 0x769960, reached only through the CEG stub
   0x574C00 (target computed at run time, so no static reference; found by the stub's own MD5 acceptance check), and
   it draws rand (random_shuffle of the anchor list per level and of the pattern and transform lists per anchor,
   0x648A00 via 0x693A00/0x907A00). Was: "through an indirect call (it is not in the static closure) [I]" and
   "HexPatternMatcher::ComputePatternPositions was not located; its mangled name at 0xA2B7E0 has no reference".*
   None of this blocks the tap.
   *Measured 2026-11-11 (epic 3200, issue 3203, W2; `shared-texture-and-compositor.md` section 11) [L]: two loads are
   not bit-identical. Four huge fields of one save (3004's dump, a load, a reload in the same process, a second launch)
   differ pairwise in 28-43 samples (64 in the union), all by 1 u16 except one by 46 (0.036 world units); the byte layer
   is identical; 50 of the 64 lie under water and the same blocks recur. Cause [I]: the order of the multithreaded
   blend jobs. A comparison of two taps or of a re-bake needs `hfbake compare --tol 1` (and a list of the few larger
   samples), not `--tol 0`.*
3. **Blend [C].** See 4.1 steps 5 to 7. The fields are weighted averages of piece heights per 64x64 block, followed by
   coast-strip writes and river and lake carving. Heights are u16. Both layers wrap in x when the map wraps (0x548980:
   `x = (W*100 + x) % W` when `[0xD90FBC] == 1`). The GPU bake ("Terrain Render/Height/...") reads the CPU field and
   does not write it back [I: no CPU writer outside 4.1].
4. **fxsb**: see 4.1 step 4.
5. **Layout [C, offsets and formulas confirmed live 2026-11-11, see below]** (`c5r/heightfield/heightfield.h` has the
   full table). Samples are W*64 x (H+4)*64, H = the DLL's map height in plots. *Correction 2026-11-11 (coordinator,
   from epic 3000's live dumps): 2904 first wrote W*64 x H*64 and oy = H*32*0.866; the grid has 4 extra plot rows.* The ushort layer at
   grid+0x0C is an FDataField of 16x16 tiles: tile directory pointer at +0x1C (8 bytes per tile: u16 state, 1 =
   constant with the value in the low bits, 0 = pooled with the index at +4), pool pointer at +0x28 (512 bytes per
   tile), pool size at +0x2C. The byte (material) layer has the same shape at grid+0x4C (directory +0x5C, pool
   +0x68, 256-byte tiles). Sample (x, y) lives in tile `(y>>4)*tilesX + (x>>4)`, at `(y&15)*16 + (x&15)`.
   Height: `z = u16 / 255 * 0.2` world units (0x6D8B60). Sample (c, r) is at world `x = c*1.0 - W*32`,
   `y = r*0.866 - (H+4)*32*0.866`. HexToWorld 0x6851C0 takes the sample `(int(w.x), int(w.y))` of `w = M_h2w(0xD91030) *
   hex`. **Size**: Init pools every tile, so the ushort pool is W*(H+4)*8,192 bytes and the byte pool W*(H+4)*4,096.
   On Huge 128x80 that is **84 MiB + 42 MiB** [L: pool count +0x2C = 172,032 tiles = tilesX*tilesY live], plus the
   directories (8 bytes per tile: 1.3 MiB each). Allocator not verified [I]. The pool comes from FFastVector (0x813A80) with a reserve in 0x798CB0, so it may
   not be the "Terrain.h:79 9.1 MB" block that STUBS/GATE2 named as the height field. That attribution is now in
   doubt (correction requested in 4.5).

### 4.3 The tap, specified

- **When it is valid.** From the return of CreateTerrain (0x60BBD6 in 0x60ABB0, load stage 2; ready byte
  `[0xD91279] == 1`) until the next load. The EXE re-runs MapSetup and CreateTerrain on every load, and nothing
  modifies the field in between (4.1). One copy per load is enough.
  - Under increment 2's stub c1, the bake still runs (terrain is kept, only not drawn), so the field is valid.
  - Under c3 (skip the bake), there is nothing to tap; the server would then have to re-bake (4.4).
- **What to copy** (all EXE-relative VAs; the DLL runs in-process and can read them directly):
  - the object: 0xC8 bytes at 0xD91270 (scales, offsets, layer descriptors);
  - the water height: f32 at 0xD91274, inside the object;
  - `M_h2w`: 64 bytes at 0xD91030;
  - the wrap flag: dword at 0xD90FBC;
  - the ushort directory: `[0xD91284]*[0xD91288]*8` bytes at `[0xD9128C]`;
  - the ushort pool: `[0xD9129C]*512` bytes at `[0xD91298]`;
  - optionally, the material byte layer: directory `*8` at `[0xD912CC]`, pool `[0xD912DC]*256` at `[0xD912D8]`.

  Flattening these on either side gives the dense grid: `c5r::heightfield::gridFromMemory`, or the EXE's own unused
  dense export **0x493DD0**(u16* heights, u8* bytes), which has no callers and walks both layers row-major
  (convention not checked: cdecl [I]). Dense size: W*(H+4)*4096*2 bytes. Huge 128x80 is 84 MiB of heights + 42
  MiB of bytes [L]; Standard 84x52 is 36.8 + 18.4 MiB (W*(H+4)*12 KiB in total; corrected 2026-11-11 from W*H).
- **Transport.** The format is "C5HF" (128-byte header + u16 rows + optional u8 rows: `c5r::heightfield::readTap` /
  `writeTap`). It goes in a named shared section that the DLL writes once per load, followed by a "terrain ready"
  event to the server. The server reads it with `readTap`.
  - Cost: one load-time copy of W*(H+4)*8 KiB, or W*(H+4)*12 KiB with the material layer, plus a transient section of the
    same size in the 32-bit process. The section can be written in chunks and unmapped after the server acks.
    That is a design note, not measured.
- **What invalidates it.** Only a new load (MapSetup 0x535BF0 rewrites `[0xD90FC4]`, then CreateTerrain re-inits)
  and game exit (`TerrainSystem::ShutdownGame` 0x6684D0 zeroes `[0xD90FC4]`; skipped under Terrain=1, see
  stubs/terrain.md). Nothing during play.

### 4.4 Re-bake: why not now, and what it would take

A re-bake would have to port steps 3 to 7 of 4.1 (coastline jitter and smoothing, the pattern matcher, region
rasterisation and flood fill, the piece sampler and the blend, coast-strip heights, river mesh and carving). It would
also need the fxsb decoded (*done 2026-11-18, `live-terrain.md` 7.3*) or the flat-file path matched, and the exact CRT `rand` sequence (MSVCR90 LCG
`x = x*214013 + 2531011`, `(x >> 16) & 0x7FFF`). The EXE works in x87 precision (the decompile shows `float10`
temporaries), so bit-exact output needs care. A ±1 u16 difference is 0.0008 world units, which is harmless; a flipped
branch in the region masks or the jitter is not. Estimate 40-80 h with live dumps to iterate against, versus 3-6 h
for the tap. It is worth doing only if a later increment drops the EXE's bake (c3) to recover the bake's memory.

### 4.5 Code and what epic 3000 should dump

- Code: `render/include/c5r/heightfield/heightfield.h`, `render/src/heightfield/heightfield.cpp`,
  `render/tests/heightfield/heightfield_test.cpp` (29 tests, every check with a negative test; synthetic data) and
  `render/tools/hfbake` (`decode` raw memory copies to C5HF + `.r16` + 16-bit PNG, `show`, `compare --tol`, `synth`).
  - 2026-11-11: `hfbake synth 40 24` ran the whole memory -> flatten -> tap path on a synthetic 40x24 field; the
    round trip is exact. Output: `D:/c5-64bit-work/2900/2904/synth/`.
- **Checked against epic 3000's live ground truth (2026-11-11 01:14 EST, coordinator; interim data of a team that
  has not merged yet, tracker 3004, branch `worktree-agent-ae601c7cc8cc4ccb4`)**: `D:/c5-64bit-work/i2-ground-truth/
  {huge,tiny}/` (huge save `C5H-VP8P-HUGE_0350` turn 350, 128x80; Tiny 56x36; reassembled u16 heightmaps, the first
  0xD0 bytes of the GridHeightmap object, grid constants, Lua HexToWorld pairs). Script and output:
  `evidence/2900/2904/ground_truth_check.py` / `.out.txt`. Results [L]: the layer descriptors sit where 4.2 says
  (ushort at +0x0C with directory +0x1C and pool +0x28, byte layer at +0x4C / +0x5C / +0x68, pool count = tiles X x
  Y); samplesY = 64*(H+4) on both maps (huge 5,376, Tiny 2,560), ox = 32*W, oy = 32*(H+4)*0.866; z = u16/255*0.2
  equals Lua HexToWorld z within 2e-6 at all 10 plots. `c5r::heightfield` takes the row count from the object
  (`mapH` there = grid plot rows = H+4), so the code needs no change; its comments now say so. Not done: a
  comparison of a tap made by our code with 3000's dump (needs the memory blocks listed below, not the reassembled
  grid), and the determinism check (two loads of one save).
- Before the ground truth existed, 2904 asked for the following. When epic 3000 dumps a game (cdb `.writemem`
  after the load reaches stage 4 or later), each dump needs these files:
  - `grid.bin`: 0xC8 bytes at 0xD91270;
  - `dir16.bin`: `[0xD91284]*[0xD91288]*8` bytes at `[0xD9128C]`;
  - `pool16.bin`: `[0xD9129C]*512` bytes at `[0xD91298]`;
  - `dir8.bin` / `pool8.bin`: the same for the byte layer at +0x5C / +0x68;
  - `h2w.bin`: 64 bytes at 0xD91030;
  - the dword at 0xD90FBC;
  - the map size and mapRandomSeed;
  - and, for the comparison with `HexToWorld`, a few `(hex, HexToWorld result)` pairs from Lua.

  `hfbake decode --obj grid.bin --dir16 ... OUT` turns them into C5HF, and two dumps of the same save loaded twice,
  compared with `hfbake compare A B --tol 0`, test the determinism claim of question 2. The tolerance for a future
  re-bake is 0 differing samples, or at most ±2 u16 (0.0016 world units) away from coast and river masks.
- Corrections for other files (requested, not applied; those files are not this task's):
  - `02-exe-internals/world-systems-and-stubs.md` section 7 and `allocations-and-memory-map.md` line 100: the
    height pool is W*(H+4)*8 KiB from FFastVector (corrected 2026-11-11 by 9019 from "W*H", per 4.2 point 5), not the 9.1 MB `Terrain.h:79` block [C formula, attribution I].
  - `coupling.md` / world-systems section 7: "+0x10/+0x1C tile directory and pool" should read: ushort layer
    descriptor at +0x0C, directory +0x1C, pool +0x28; byte layer at +0x4C, directory +0x5C, pool +0x68.
  - `world-systems-and-stubs.md` section 7 and `insertion-point-and-frame-structure.md`: the field is not "filled
    only by Init + 0x5A3490". Init only zeroes it; the blend, coast and carve steps of 4.1 fill it, through
    0x5A3490 and 0x772510.

## 5. quadtest: a game texture drawn by a game shader (task 2906)

Done 2026-11-11 01:05 EST by the coordinator (task 2906; the agent pool was full, so no worker). Code:
`render/tools/quadtest/` (`quadtest_core.h/.cpp` as a small library, `quadtest.cpp` the tool), tests
`render/tests/quadtest/`. Evidence: `evidence/2900/2906/quadtest_run.txt` and a 128x128 picture of the rotated quad
`quadtest_game_rotated_128.png` (the only game-derived image committed; the full-size PNGs are on
`D:/c5-64bit-work/2900/2906/`).

**Result [C, run 2026-11-11 01:05 EST, RTX 4080, hardware device, no window]: a game texture drawn by a game
shader, pixel-identical to a reference.**

- Texture: `euro_grassland_d.dds` resolved by `GameFiles` in the game's order to
  `Assets/DLC/VT/Terrain/grass/euro_grassland_d.dds` (Vibrant Textures overrides `TerrainTextures.fpk`, section 1.2),
  BC3_UNORM 1024x1024, 11 mips, through `parseDds` + `createTexture`.
- Shaders: `CompositeShader` (material 77) pass 0 from `terrainsets` (section 3): VS 0x067e4fd8 (vs_5_0, 820 B) and
  PS 0x067f7cb0 (ps_5_0, 404 B), read from the EXE's `.shader` section at run time by `ShaderSection`. The device
  built the input layout (POSITION R32G32B32A32_FLOAT, NORMAL R32G32B32_FLOAT, TEXCOORD0 R32G32_FLOAT, stride 36)
  against the VS's own signature.
- Constant buffer: the 0x50-byte `CompositeOwn` layout, member `Transform` (float4x4, offset 0) written **by name**
  from the layout (`writeMatrix`), sampler state 0 of `terrainsets::samplerStates()`.
- Checks: the game pair and a passthrough HLSL pair compiled at run time (`row_major` matrix, `mul(p, M)`) draw the
  same 256x256 image: identity transform 36,864 covered pixels (0.5625 of the target, as the quad's corners
  +-0.75 predict), max |difference| 0 in every channel; rotated (0.5 rad), scaled (0.8) and shifted (+0.1 x)
  transform 23,595 covered pixels, max |difference| 0. The rotated image differs from the identity one by a mean
  of 35.1 per channel, so the constant buffer's content drives the game VS. This confirms the bytecode's reading of
  `CompositeOwn.Transform` (row-major, `p.x*r0 + p.y*r1 + p.z*r2 + r3`) [C for the bytecode]. *Correction 2026-11-11
  (team-3200 W1, bug 0091 item 3, from verification 9019 section 6): the CompositeOwn layout itself is inferred from the
  VS's register use, not traced in the EXE's writer code [I]; the earlier text called it "recovered".*
- **Limit (bug 0091 item 3, stated 2026-11-11):** the reference arm shares the sampler state, the texture loader
  (`parseDds` / `createTexture`), the input layout and the quad with the game arm, so quadtest cannot detect a wrong
  recovered sampler state or a texture-loading error (verification 9019 broke the sampler on purpose and the test still
  passed). Equality of the two pictures proves only that the game VS/PS pair reads its constant buffer and texture as
  the passthrough pair does. An independent reference (a CPU decode of the BC3 mip and a point-sampled draw) was not
  built; the terrain draw's own tests (`render/tests/terrain/`, terrain-surface.md) check sampler state 0 indirectly
  (the height texture read through it must reproduce the CPU height formula to 0.005 world units).
- Tests (`c5r_quadtest_test`, 11): the full game run above (SKIP without `C5R_GAME_DIR`/`C5R_EXE`), the reference
  pipeline on a synthetic 2x2 texture, and negative tests with the exact error for: unknown set, missing pass, set
  VAs not in the section [NotFound]; missing member [NotFound], non-matrix member and member past the buffer end
  [BadLayout]; no texture view [BadLayout]; unknown sampler state [NotFound]; texture name not found [NotFound];
  image size mismatch [BadLayout]. Not covered by a negative test: "blob of the wrong stage" in `findPass` (it needs a
  set whose VAs name a blob of another stage; 2903's `validateBlob` has that negative test for the same condition).
- Run: `quadtest [--game DIR] [--exe PATH] [--texture NAME] [--set NAME] [--pass N] [--out DIR] [--evidence PNG]`;
  binary at `<build>/tools/quadtest/quadtest.exe`; exit 0 when every check passes.

**Stretch goal (a terrain patch with TerrainShader) not attempted** in this epic: the tessellated terrain draw needs
the per-tile bake caches (t0/t8 come from the GPU bake 0x694a60, section 3) and the TerrainFrame/TerrainTile values,
which are the first work items of package I2-E (see terrain-rendering-inventory.md).
