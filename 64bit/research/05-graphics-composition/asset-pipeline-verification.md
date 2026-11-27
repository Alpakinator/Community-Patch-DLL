# Independent verification of epic 2900's asset pipeline (one-off 9019)

Status: written 2026-11-11 by the verifier of tracker 9019 (an agent that did not build epic 2900), session 01:28 to
02:10 EST. Subject: `render/` and `asset-pipeline.md` / `terrain-rendering-inventory.md` as merged to `main` at
8ff0c55 (checked from a fresh clone of `main` at 3ef3eba, which adds only issue 9019 and MC notes). Nothing was
launched; no game instance; nothing written into a game folder or save. Inputs, read-only: the install copy
`%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`, the owner's `MODS` folder, the EXE copy
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe` (SHA-256 b2ce96ae...c88e, build 4390913), and epic
3000's interim ground truth `D:/c5-64bit-work/i2-ground-truth/` (interim data of another team; results resting on it
say so). Scripts and the harness: `evidence/9019/`; their outputs: `evidence/9019/results/`.

Confidence marks as in AGENTS.md: [C] confirmed by my own decompile/dump/measurement; [R] confirmed only by rerunning
the team's own tool (my label for this file); [I] inferred.

## 1. Verdict per claim

| # | Claim | Verdict | Evidence |
|---|---|---|---|
| 1 | Fresh-clone build, 0 warnings /W4 /WX, 7 of 7 pass, game tests run | **Confirmed with one exception**: the documented command fails from a fresh clone on this machine (D1); with `--build-dir` everything holds, twice (0 warnings, 7/7, 153 tests, 0 skipped). Partial skips are invisible to ctest (D2) | section 2 |
| 2 | gamefs: 147/147 packs, 10,004/10,004 DDS, 1,360 terrain-side, 10 loose failures, no volume/DX10 | **Confirmed by own measurement** (own FPK/DDS parser: all counts, every one of 10,004 rows field by field, 5 packs entry by entry); D3D11 creation and the 1,360 only by rerun [R]; lookup order not independently checked | section 3 |
| 3 | shaders: 1,417 blobs, own checksum, decoder = D3DDisassemble 1,417/1,417, create sites, device creation, no RDEF | **Confirmed by own measurement** for the walk, the counts, the checksum (own implementation, 1,417/1,417) and the decoder (own D3DDisassemble comparison, 20/20 random and 1,417/1,417); create sites and device creation only by rerun [R] | section 3 |
| 4 | terrainsets: 18 sets, layouts clean against 342 references, 13 samplers, ColorKey 3D tables | **Confirmed by own measurement for TerrainFrame, TerrainTile, WaterFrame** (bytecode reads: 0 errors; writer code: every checked offset as stated); 18 sets / 342 references by rerun [R]; samplers and ColorKey not checked | section 5.1 |
| 5 | heightfield: z = u16/255*0.2, 64*(H+4) rows, tap format | **Confirmed by own measurement**, resting on epic 3000's interim data for the samples: formula from my own disassembly of 0x6D8B60; row count from four EXE values on two maps; 60 plots of my choice bit-identical between my formula and `render/`; 10 Lua points within 4.3e-7 | section 5.2 |
| 6 | quadtest pixel-identical to a reference, identity and rotated | **Confirmed** (max 0 on this GPU); fails as it should for a wrong cb offset; **cannot fail** for a wrong sampler state (D3); the "recovered layout" it uses is an untraced [I] one | section 6 |
| 7 | negative tests for readers and decoders | **Confirmed and extended**: ~260,000 hostile calls under AddressSanitizer, 0 crashes, hangs, ASan reports or out-of-range Ok results; one silent wrong answer in `ShaderSection` (D4) | section 4 |

## 2. Build from a fresh clone (claim 1)

- **The documented one command fails from a fresh clone on this machine** [C, 2026-11-11 01:30]: `python
  render/build.py --test` in `D:/c5-64bit-work/9019/clone` stops at configure in 5.7 s: `CMake Error: The source
  ".../9019/clone/render/CMakeLists.txt" does not match the source ".../worktrees/agent-af1b0c2f62a50b7be/render/
  CMakeLists.txt" used to generate cache`. Cause: without `--build-dir`, `build.py` uses the fixed folder
  `D:/c5-64bit-work/2900/build/<config>` whenever `D:/c5-64bit-work` exists, so every checkout on this machine shares
  one CMake cache, and the first checkout to configure it owns it (here the 2900 coordinator's worktree). Every
  other worktree, clone or team gets this error; a checkout that does configure it takes it from the others. The
  team's own fresh-clone run must have passed `--build-dir` (the epic's log does not say). The same default sends
  `C5R_WORK` (test scratch) to `D:/c5-64bit-work/2900/test-out` and the testkit's `workDir()` falls back to it too,
  so another team's scratch folder receives writes. Defect D1.
- With `--build-dir D:/c5-64bit-work/9019/build/Release --work D:/c5-64bit-work/9019/test-out` (otherwise as
  documented, through `quietlock.py run`): **0 warnings** (37 compile steps, every one with `/W4 /WX`, read from
  `build.ninja`), 51 build steps, **7 of 7 ctest executables pass**, 23.3 s wall clock including configure and
  tests (tests 7.9 s). Second run, `--clean` rebuild at 01:32: 0 warnings, 7 of 7 pass, 20.9 s (tests 6.6 s) [C].
- **Game-file tests ran, none skipped** [C]: each test executable run directly with the same environment reports
  `common 3, gamefs 48, gamefs_game 2, heightfield 29, quadtest 11, shaders 46, terrainsets 14` run, 0 failed, 0
  skipped (153 tests). Without `C5R_GAME_DIR`/`C5R_EXE`, `gamefs_game` exits 77 (ctest: skipped), but `shaders`,
  `quadtest` and `terrainsets` exit 0 with 2, 2 and 1 of their tests skipped: **ctest then reports them "Passed"**,
  because `runAll` returns 77 only when every test of the executable skipped. A clone without the game files
  shows 7 of 7 passed with five game tests silently not run. Defect D2.
- `render/` depends on nothing outside itself [C]: no include, CMake or Python path into `server/`, `proxy/` or `ipc/`
  (the only mentions are comments; `cmake/clang-cl-x64.cmake` is a copy of the ipc one); `build.py` imports
  `perf/quietlock/gate.py` optionally.

## 3. Sweeps of claims 2 and 3: the team's tools and my own

### 3.1 Rerun of the team's tools [R], 2026-11-11 01:32-01:33

- `fpkscan sweep` (16.9 s): 147 of 147 packs, 2,373,988,644 bytes, 30,557 entries; packs 10,004 DDS / 10,004 parsed /
  10,004 created; loose 3,287 / 3,277 / 3,277; terrain-side packs 731/731/731, loose 1,326/1,326/1,326. Identical to the
  epic's numbers. Its exit code was 3 (the tool returns 3 when any loose file fails), which collides with the
  launch-gate convention "3 = lock held" when run under `quietlock.py run`. Defect D6 (cosmetic).
- `c5r_gamefs_game_test`: "terrain packs + Assets/terrain resolved through the lookup: 1360 of 1360 created".
- `shaderdump --check`: section VA 0x0619b000, file offset 0x8b6a00, size 6,996,740; 1,417 blobs (6,993,892 B), 712 gaps
  (2,848 B, all zero); D3DReflect S_OK 1,417 with 0 cbuffers and 0 bound resources; decoder agrees with
  D3DDisassemble on 1,417 of 1,417. `--device`: 1,417 of 1,417 created; input layouts 379 built, 1 none needed, 0
  failed. Per model: vs 126/126/128, ps 320/352/362, hs 2, ds 1.

### 3.2 My own readers [C]

`indep_fpk_dds.py` (own FPK directory and DDS header parser from the format description and Microsoft's DDS_HEADER;
nothing from `render/`), 2026-11-11 01:34:

- 147 packs, all version 6 with u16 0; 30,557 entries; 10,004 `.dds`; the extension census equals fpkscan's line for
  line. 28 DDS with header size 24 (as the epic says).
- Formats of the 10,004 packed DDS: A8 30, A8R8G8B8 2,368 + 18 cube, D3DFMT_113 3 cube, D3DFMT_114 1,382, D3DFMT_116 10
  cube, D3DFMT_36 5, DXT1 978 + 1,055 cube, DXT3 511, DXT5 1,793, L16 1,144, L8 704, X8R8G8B8 1 + 2 cube; no volume.
  **For all 10,004 the payload is exactly the bytes the mip chain needs** (my computation).
- Every one of the 10,004 rows of fpkscan's `sweep_files.csv` agrees with my parse in width, height, mips, format,
  byte size and cube flag: 0 mismatches, 0 rows only on one side.
- Seed 9019: 5 random packs (`DX9_Low/BrazilPedroTexturesDX9Low`, `Common/DenmarkModels`, `DX9/ByzantiumEmpressTheodora
  Textures`, `DX9_Low/KoreaTexturesDX9Low`, `DX9/Expansion2EffectsTextures`; 284 entries): name, size and offset of
  every entry identical to `fpkscan list`. 20 random packed DDS printed side by side in the output: all equal.
- `indep_loose_dds.py`: 3,287 loose DDS under `Assets/` and `MODS/`; **0 volume, 0 DX10 header**; exactly 10
  block-compressed files whose top level is not a multiple of 4 (the 5 VP files named in the epic, twice).

Correction found on the way: `asset-pipeline.md` 1.3 listed DXT1 in packs as "950 + 1,055 cube"; the sweep and my
parse both give 978 (the 950 was the first run, before the 28 header-24 files parsed). Corrected there.

`indep_dxbc.py` (own PE section walk, own DXBC checksum written from the public description, D3DDisassemble through
ctypes with my own text parsing), 2026-11-11 01:35:

- `.shader` VA 0x619b000, file offset 0x8b6a00, VirtualSize 6,996,740; 1,417 containers, 6,993,892 B; 712 four-byte
  gaps, all zero; sum = VirtualSize. Models vs 126/126/128, ps 320/352/362, hs 2, ds 1; chunk sets ISGN+OSGN+SHDR 924,
  ISGN+OSGN+SHEX 490, ISGN+OSGN+PCSG+SHEX 2, ISGN+PCSG+OSGN+SHEX 1. All equal to the epic.
- **My checksum matches the stored checksum of 1,417 of 1,417 blobs**; one flipped byte changes it.
- D3DDisassemble against the team's decoder output (`shaderdump --out`, reflect JSON): constant-buffer slots and
  sizes, texture slots and dimensions, sampler slots, `dcl_temps`, instruction count and every constant-buffer
  register read with its components: **20 of 20 random blobs agree (seed 9019), and 1,417 of 1,417 when run on all**.
  (My first two runs reported differences that were bugs of my own text parser: `exp` taken for a number, and
  `dcl_resource_structured` / `hs_decls` not handled; fixed, then 0 differences.)

Not checked independently: the create sites (1,417 of 1,417, from an abstract interpreter over the code), the set
assignment, and the GameFiles lookup order (`asset-pipeline.md` 1.2): these rest on the team's static analysis only.
The device creation of every blob and every DDS rests on the team's tools [R].

## 4. Hostile input (claim 7)

Harness `evidence/9019/attack.cpp` (own code, links the `render/src` files of `main` unchanged), built with
**AddressSanitizer** by `build_attack.py --msvc` (MSVC cl.exe and its ASan runtime; LLVM 19.1.7's clang-cl ASan
runtime crashes at start-up on this Windows 11 26100 machine, "interception_win: unhandled instruction" then a
segmentation fault even for the usage message, so it was not usable). Run 2026-11-11 01:57:48-02:02:45 by
`run_attack.py`, one process per phase, ASan quarantine 64 MB, allocations over 1 GB refused, a watchdog at 1.5 GB
private bytes, through `quietlock.py run`. Mutations are deterministic (mt19937 seed 9019). Every Ok result is
checked for pointers outside the input (FPK entries, DDS subresources, DXBC chunks), each DDS subresource's first and
last byte is read under ASan, each call is timed (over 2 s = finding) and its commit growth recorded.

| Reader | Inputs | Results | Peak private | Findings |
|---|---|---|---|---|
| `parseDds` | 13 real seeds (every pack format incl. cube, header-24, 128x129 BC, R16G16B16A16) x (every truncation, 5,000 1-3 bit flips, 22 header lies: sizes 0 / huge / 65536, mips 33 / 255 / 2^32-1, depth huge, volume depth 4096, pitch, pixel-format flags, DX10 fourcc without header, cube with one face) + DX10 headers with 11 lies (array 0 / 2^32-1, cube array 2^30, 3D, 1D, unknown and huge DXGI format, BC1 array 7) and flips | 118,956 calls: Ok 50,885, Unsupported 27,354, BadLayout 19,656, Truncated 18,916, BadMagic 2,145 | 689 MB | 0 |
| `parseDxbc` + `reflect` | 10 real blobs (VS/PS/HS/DS, SM4.0/4.1/5.0) x (every 3rd truncation, 4,000 flips and 2,000 dword overwrites **with the checksum recomputed** so the token decoder sees them, size / chunk count / chunk offset lies) | 66,302 calls each; reflect: Ok 41,374, BadLayout 11,757, Truncated 7,424, Unsupported 4,537, NotFound 448, BadMagic 389, BadVersion 373. **Every one of 18,776 single-bit flips (each bit 0 of every byte of the 10 blobs) is rejected** with the checksum on | 456 MB | 0 |
| `parseFpkIndex` | a 5-entry pack x (every truncation to 4 KB, then every 997 B; 20,000 flips; count / nameLen / size / offset lies incl. offset+size wrapping 2^32 and 1 byte past the end) | 24,219: Ok 19,973 (all entries inside the file), BadLayout 4,146, Truncated 98, BadVersion 2 | 178 MB | 0 |
| `ShaderSection::parse` (PE) | the EXE truncated at 17 points (DOS header, PE header, section table, inside `.shader`) and 14 header lies (e_lfanew huge / past end, 65,535 or 0 sections, optional header 65,535, `.shader` VirtualSize / SizeOfRawData / PointerToRawData / RVA huge, first blob size 2^32-1 / 4 / 0, first blob magic, garbage in the first gap) | 31: Ok 5, Truncated 22, BadLayout 4 | 171 MB | 0 crashes; one silent case, below |
| `readTap`, `gridFromMemory` | a 3x2-plot C5HF and its memory image x (truncations, 3,000 flips, 72 field lies incl. mapW 2^26 so that mapW*64 wraps to 0; 3,000 mutations of object, directories and pool size; every dword of the object x 9 values; pool-index lies) | readTap 3,419: Ok 3,003, Truncated 345, BadLayout 61, BadVersion 9, BadMagic 1; gridFromMemory 3,460: BadLayout 2,395, Ok 1,065 | 208 MB | 0 |
| `parseCiv5Pkg` / `parseXml` | `Expansion2.Civ5Pkg` x (every truncation, 5,000 flips), nesting 1,000 / 100,000 / 1,000,000 levels, 9 broken snippets | 27,568: Truncated 22,543, BadLayout 3,083, Ok 1,942; no stack overflow at 10^6 levels | 377 MB | 0 |
| files and paths | zero-length, 10-byte and half-length packs, a missing path, a directory given as a file, a pack and the EXE under `ümläut 日本 Ж\`, `GameFiles` on a missing root, an empty folder and a fake install (zero-length and truncated base packs, a non-ASCII loose file) | zero/10 B: Truncated; half: BadLayout (payload past end); missing: Io (error 2); directory: Io (error 5); non-ASCII pack and EXE: Ok (5 entries, 1,417 blobs); GameFiles: Io for non-installs, Ok with 44 warnings for the fake install, which resolves `löose.dds` | 113 MB | 0 |

Result [C]: **no crash, no hang, no ASan report, no Ok pointing outside its input, no allocation over 1 GB and
no call over 2 s in 260,000 hostile calls.** Two silent cases:

- **ShaderSection skips a damaged container as a gap** (defect D4) [C, 2026-11-11 02:04, `shaderdump --index` on two
  EXE copies under `D:/c5-64bit-work/9019/pe/`]: with the `DXBC` magic of the first or the second blob overwritten,
  the open succeeds (exit 0) with **1,416 blobs, 713 gaps, 1 not all zero, and "create sites applied: no"**, i.e. one
  shader silently missing and the set / create-site / handle data of all 1,416 others dropped (the table is applied
  only when it matches as a whole). The walk in `shadersection.cpp` treats anything that does not start with `DXBC`
  as a gap up to the next `DXBC`; the header comment promises "a container that fails parseDxbc: that error". Only
  `gaps()` and the tool's summary line show it. Proposed fix: fail with BadLayout when a gap is longer than 4 bytes
  or not all zero (the EXE has 712 gaps, all exactly 4 zero bytes).
- `GameFiles::read` leaves `*origin` unchanged on NotFound (it showed the previous call's origin); harmless when the
  Status is checked (defect D5, cosmetic).

Lookup is case-insensitive for ASCII as stated (`fpkscan lookup ... EURO_GRASSLAND_D.DDS` resolves to the VT copy).

History of this run: a first, unbounded run of the same harness in one process (01:52:26-01:55:47, 3 min 21 s), started
without `quietlock.py run` because I counted it as a test rather than a build, reached 3.4 GB of commit while epic
2800 measured, and the MC stopped it (the machine hit its commit limit at 01:52 and 2800's
game crashed during a load). It printed nothing (stdout was buffered, and a finding would have been flushed at once),
so no finding was lost; the capped rerun above covers the same corpus. The capped rerun passed the gate at 01:57:48;
team 7000 took the lock at 01:58 for a turnbench series, so the rerun (peak 689 MB private) overlapped the first
4 min 45 s of that series. Lesson for the next verifier: any ASan or
fuzz run goes through `quietlock.py run` with a memory cap, even when it is not a build.

## 5. Constant-buffer layouts and the height field (claims 4 and 5)

### 5.1 Three layouts against the bytecode and the EXE's code

`cb_layout_check.py` (2026-11-11 01:41): for every blob of the material (all passes, SM5, 4_1, 4_0) D3DDisassemble's
constant-buffer reads are mapped onto the layout; a read outside a member, or of a component the member's type lacks
(float = .x, float2 = .xy, float3 = .xyz), would be an error.

| Layout (material, slot) | Blobs / declaring the slot | Registers read | Errors | Members read by some blob |
|---|---|---|---|---|
| TerrainFrame (73, 1) | 19 / 16 (vec4 10..35) | 0-7, 9-13 xyzw; 14.x, 15.x, 16.xyz, 19.xyz, 22.xy, 25.x, 27.x, 28.x, 31-34.xyw | 0 | 13 of 24 |
| TerrainFrame (75, 1) and (76, 1) | 9 / 3 and 24 / 3 | 21.xyz; 16.xyz, 17.xy, 22.xy | 0 | 1 and 3 |
| TerrainTile (73, 0) | 19 / 8 (vec4 4, 5, 7) | 0-4 xyzw, 5.xy, 6.xyz | 0 | 5 of 5 |
| WaterFrame (91, 1) | 24 / 21 (vec4 11..37) | 0-3, 8-10, 16-19 xyzw; 4-7 .xy; 11-13 .x; 14-15 .xyz; 20, 22-29 .xyw; 21, 35 .xyz; 30-33 .xy; 34, 36 .x | 0 | 22 of 22 |

From the EXE's code (my own `llvm-objdump -d` of the writers, 2026-11-11 01:39-01:45) [C]:

- **TerrainTile, 0x5c37e0**: the block pointer comes from 0x6bbe60; `rep movsd` of 16 dwords from the result of the
  matrix call 0x8a86bc (arguments `-[TR8+0x1ab0]`, `-[TR8+0x1ab4]`, `-[TR8+0x1ab8]`, sign flip by xor with -0.0 at
  0x9e0a00) to +0x00; `cvtsi2ss` of tile+0xdc/e0/e4/e8 to +0x40/44/48/4c; 1.0/(tile+0xd4 + 1) and 1.0/(tile+0xd8 + 1)
  to +0x50/+0x54; (d4-1)/(n-1), (d8-1)/(m-1) with n, m halved tile+0x1c4 times to +0x60/+0x64; `[TR+0x2424]` to +0x68.
  Every member, offset and source as the epic states.
- **TerrainFrame, 0x6add60**: matrices by `rep movsd` to +0x00 and +0x40 and +0x1f0; +0x100..108 from TR+0x1acc..d4
  (= 1.0, 0.866, 0.2 at 0xb9a6d4..dc, read from the file); +0x110..118 from TR+0x400..408; +0x120..128 from
  TR+0x40c..414; +0x230/+0x234 from 0x301056c/0x3010568 (float); +0xe0 = `fild` of a clamped integer; +0xf0 = 1.0
  (0x9f72f4); +0x90 from 0xda1c40..; +0x150..158; +0x160/+0x164 = 0x9de4f0/0x9eae88 = +1/1024, -1/1024; +0x170/+0x174 =
  0x9ecfb8 = 1/512; +0x180/+0x184 = float(TR+0x1ab0/+0x1ab4); +0x190; +0x240 = TR+0x528; +0x1a0 = (TR+0x2404 == 2);
  +0x1b0 = (TR+0x241c != 0); +0x1c0 = (TR+0x2418 != 0); +0x1d0 = 1.0; +0x1e0 = (TR+0x240c != 0). As the epic states.
- **WaterFrame, 0x739ae0**: stores to +0xb0, +0xc0, +0xd0, +0xe0..e8, +0xf0..f8, +0x100..13c, +0x140..158 (with +0x144 by
  `fstp`), +0x220, +0x230..23c, +0x240: every scalar member of the layout has a store at its offset; the matrix
  members were not traced instruction by instruction.

Not verifiable from bytecode: the 11 TerrainFrame members no blob reads (AuxDir, LightDir, CameraAxisZ, HalfVector,
BakeTexelScale, MapSize, FlagMode2, One, FlagD, ScreenSize, OverlaySize); their offsets rest on the writer code
above only. Names and meanings marked [I] in `sets.cpp` stay [I]; nothing here was observed live.

### 5.2 Height field against epic 3000's interim ground truth (rests on 3000's interim data)

`hf_check.py` 2026-11-11 01:57; data: `D:/c5-64bit-work/i2-ground-truth/{huge,tiny}` (epic 3000, interim; huge save
`C5H-VP8P-HUGE_0350` turn 350, 128x80; Tiny 56x36). **Everything in this subsection rests on that interim data.**

- **The formula from the EXE's code** [C, my disassembly of HeightAtGrid 0x6D8B60]: when `[0xD90FBC] == 1`, x =
  (samplesX*100 + x) mod samplesX (`imul 0x64; idiv`); outside [0, width) x [0, height) of the layer at +0x0C it
  returns 0.0; else `cvtsi2ss(u16) * [0x9F9E34] * [grid+0xB0]`. `0x9F9E34` read from the EXE file = 0.0039215689 =
  float32(1/255); grid+0xB0 in both dumped object headers = 0.2. So z = u16 / 255 * 0.2 in float32, in that order.
- **Row count** [C on 3000's data]: huge raw 88,080,384 B = 8,192 x 5,376 = 64*128 x 64*(80+4); Tiny 3,584 x 2,560 =
  64*56 x 64*(36+4). Independent of the reassembled raw, the EXE's own values agree: object samplesX/Y (+0xA0/+0xA4)
  8192/5376 and 3584/2560; `world_height` (0xD90FE8) = (samplesY - 1) * 0.866 exactly on both maps (4,654.75 and
  2,216.094); `grid_oy` = 32*(H+4)*0.866 (2,327.808 and 1,108.48); `wrap_period` = 64W - 1.
- **30 plots per map chosen by me** (the four corners + 26 random, seed 9019; none of 3000's 10 Lua plots): plot ->
  hex `(gx - (gy >> 1), gy)` -> `w = hex * M_h2w` -> sample `(int w.x, int w.y)` -> z by the float32 formula above, compared
  with `render/`'s `heightAtSample` on the same grid (run through `attack.exe --hf`): **60 of 60 bit-identical**, max
  |dz| 0 (huge: 12 plots with non-zero height up to z 18.27; Tiny: 13 up to z 34.32). `render/`'s `sampleToWorld` equals
  (c - ox, r*0.866 - oy) and lies within 1 world unit of HexToWorld's continuous position at all 60 (the integer
  truncation of the sample). The corner plots (0,0), (W-1,0), (0,H-1), (W-1,H-1) map to samples x 32..8191 and y 128..5184:
  2 plot rows of margin below the first plot row and 2 above the last.
- **The 10 Lua HexToWorld z values** (3000's plots): my z equals Lua's within 4.3e-7 at all 10.
- Not checked: a tap made through `gridFromMemory` from real memory blocks (3000's dump is a reassembled grid, not the
  directory and pool), and the determinism of two loads. The world z of my 30 plots is not a live value: it is the
  EXE formula applied to the dumped samples; the live check of the formula remains 3000's 10 Lua points.

## 6. quadtest (claim 6)

Runs 2026-11-11 01:53, RTX 4080 hardware device, no window, `c5r_quadtest_test.exe` with the game files:

| Build | Change | Result |
|---|---|---|
| as merged | none | 11 run, 0 failed; identity covered 36,864 px, max difference 0; rotated max 0; moved mean 35.13 [C] |
| break 1 | `CompositeOwn.Transform` offset 0x00 -> 0x10 in `sets.cpp` | **fails** (3 checks): identity max 157, rotated max 155, moved mean 0.00 |
| break 2 | sampler state 0 changed from linear/wrap (0x15, wrap) to point/clamp (0x0, clamp) in `sets.cpp` | **passes**: max 0, max 0, moved mean 36.02 |

So the pixel identity is real and the test catches a wrong constant-buffer layout. It **cannot** catch a wrong sampler
state, or anything else both arms share: the reference HLSL pair is drawn with the same sampler state object, texture,
input layout and quad as the game pair (`quadtest_core.cpp` `drawWith`), so a mistake in the recovered sampler table
(or in `parseDds`/`createTexture`) changes both images equally. Defect D3 (low): the claim "pixel-identical to a
reference" is true, but it validates the shaders and the cb layout only. A fix is to compare against a reference
computed on the CPU from the decoded texture with the documented filter (or at least to assert a known texel at a
known pixel for point sampling).

## 7. The research as a stranger reads it (task F)

Read 2026-11-11 01:40-01:50: `asset-pipeline.md` (735 lines), `terrain-rendering-inventory.md`, issues 2900-2906.

- Dates and sources: every section names its session date, inputs (install copy, EXE copy with hash) and evidence
  folder; measurements carry dates. Confidence marks are present on nearly every claim; sections 1.2, 3 and 4 mark
  [I] where the tracker does. The section README lists both new files [C].
- Stale or wrong statements found (corrected in place on 2026-11-11 with a trace of the old text):
  `asset-pipeline.md` 1.3 DXT1 count 950 -> 978; 1.4 "300 pieces incl. 15 `Wonder_*`" -> 299 pieces incl. 56 of 14
  wonders + `missing_hex` (counted: 14 wonder folders x 4 styles); 4.5 correction list "W*H*8 KiB" -> W*(H+4)*8 KiB;
  `terrain-rendering-inventory.md` 3 "80-120 MiB" -> 84 + 42 MiB. Not corrected (tracker files of the closed team,
  for the MC): issue 2904's Findings still say "samples W*64 x H*64" and "80 + 40 MiB on Huge", which section 4.2 has
  since corrected to W*64 x (H+4)*64 and 84 + 42 MiB.
- Confirmed vs inferred: one overstatement. The epic and issue 2906 say quadtest fills its constant buffer "by member
  name from the recovered layout". The layout it uses, `CompositeOwn`, is one of the *untraced* ones (`sets.cpp`:
  writer "not traced", member `Transform` marked Conf::Inferred; the name was given by 2903 from the VS bytecode).
  quadtest proves the bytecode reading of that buffer [C], not a layout recovered from the EXE; no EXE-traced layout
  (TerrainFrame, TerrainTile, WaterFrame) is exercised by a draw.
- Dependencies: `render/` uses nothing in `server/`, `proxy/` or `ipc/` [C] (section 2).
- Game binaries and bulk game data in the repository [C, `git diff 3de3946 8ff0c55`]: none. One game-derived image,
  `evidence/2900/2906/quadtest_game_rotated_128.png` (128x128 RGBA, 15,363 B), as declared. The largest evidence
  files are tables of names, sizes, addresses and checksums (`shader_index.csv` 499 KB, `dups_dds_and_terrain.csv`
  435 KB, `terrain_side_files.csv` 301 KB, `material_table.txt` 91 KB); `render/src/shaders/create_sites_table.inc`
  (100 KB) holds 1,417 rows of addresses and sizes, no bytecode; no decompiled EXE code (the `FUN_00...` strings in
  `evidence/2900/2903/programs.txt` are Ghidra function labels).
- Scratch left by the epic on D: `D:/c5-64bit-work/2900/` still holds `build/` (the shared cache of defect D1), `2901`,
  `2902` (the 29 MB dump the epic says it kept for 2903), `2903`, `2906`.

## 8. Defects and what was not verified

### 8.1 Defects (none blocks I2-E; bug ids to be issued by the MC)

| Id | Severity | Defect | Proposed fix |
|---|---|---|---|
| D1 | medium | `render/build.py` without `--build-dir` uses `D:/c5-64bit-work/2900/build/<config>` for every checkout on the machine: the documented one command fails at configure from any checkout but the first ("source does not match the source used to generate cache"); tests' `C5R_WORK` and `testkit.cpp`'s `workDir()` default into `D:/c5-64bit-work/2900/test-out` | key the default build folder by checkout, e.g. `WORK_ROOT / "render-build" / sha1(str(REPO))[:8] / config`, and the work folder likewise |
| D2 | low | a test executable with some tests skipped and none failed returns 0, so ctest reports "Passed"; without game files `shaders`, `quadtest`, `terrainsets` hide 5 skipped game tests | `runAll`: `if (skipped) return 77;` after the failure check (ctest then shows "Skipped", with the other tests' failures still reported first) |
| D3 | low | quadtest's reference shares sampler, texture, input layout and quad with the game arm, so it cannot detect a wrong recovered sampler state or a texture-loading error (break 2 passes) | add a CPU reference from the decoded DDS for a point-sampled draw, or assert known texels |
| D4 | low | `ShaderSection` turns a damaged container into a non-zero gap and returns Ok with 1,416 blobs and no create-site data | BadLayout for a gap longer than 4 bytes or not all zero |
| D5 | cosmetic | `GameFiles::read` leaves `*origin` from the previous call when it returns NotFound | clear `*origin` at entry |
| D6 | cosmetic | `fpkscan sweep` exits 3 when a loose file fails, the code the launch gates use for "lock held" | use another code (e.g. 4) |

Documentation corrected in place on 2026-11-11 (no issue needed): `asset-pipeline.md` 1.3, 1.4 and 4.5 and
`terrain-rendering-inventory.md` 3 (section 7). For the MC: issue 2904's Findings still carry the uncorrected
W*64 x H*64 and 80 + 40 MiB; the epic's wording "filled by member name from the recovered layout" overstates (section 7).

### 8.2 Not verified, and why

- The GameFiles lookup order against the EXE (asset-pipeline 1.2): needs the decompile or a live read through the game;
  out of time and no game launch allowed.
- The create sites and set assignment of all 1,417 blobs: the team's abstract interpreter only; an independent check
  needs a second disassembly walk (2-3 h).
- D3D11 creation of every DDS and every blob: rerun of the team's tools only (any own creation code would repeat theirs).
- The 13 sampler states and the ColorKey 3D-texture construction: not checked.
- TerrainFrame's 11 members no shader reads: offsets confirmed from the writer code only; meanings stay [I].
- The height-field tap from real memory blocks (`gridFromMemory` on a dump of directory and pool) and the determinism
  of two loads: 3000's dump is a reassembled grid.
- Anything live: no game was run.

## 9. MSVC ASan run (issue 3209, epic 3200, 2026-11-11)

Team-3200 W6, branch `team-3200-w6` (team branch 09789cd merged, render variant committed in e018d0c), MSVC 19.43.34810
(toolset 14.43.34808), runtime `clang_rt.asan_dynamic-x86_64.dll` / `-i386.dll` of that toolset. No game; every run
through `perf/quietlock/quietlock.py run --who 3200` under a job-object memory cap (`shell/tools/memcap.py`).

**render/ [C]: 9 of 9 test programs pass, 0 skipped, 0 ASan reports** (2026-11-11 15:02:55-15:03:42, ctest 38.9 s,
peak committed 1,589 MB of a 6,144 MB cap). Command: `python render/build.py --asan --test --build-dir
D:/c5-64bit-work/3200/w6/render-build-asan` with the default game inputs (C5R_GAME_DIR = the `pure_vp` install copy,
C5R_EXE = the Ghidra copy), so the game-file tests ran. The variant is new in this issue (render/README.md "AddressSanitizer
variant"): cl + link through `render/cmake/msvc-asan-x64.cmake`, `/fsanitize=address /Zi /Oy-` on every target incl.
c5render and the test kit, `/W4 /WX /wd4100`. **cl accepted the whole tree with /W4 /WX unchanged**: no code change
was needed for cl. Reports go to `log_path="<tree>/asan-logs/asan"` with `log_exe_name=1` (files
`asan.<exe>.<pid>`), and any non-empty file fails the run. Positive control: the canary `c5r_asan_canary` (one byte past
a 16-byte heap block) must report first; it did (`SUMMARY: AddressSanitizer: heap-buffer-overflow asan_canary.cpp:15
in main`), which proves the instrumentation, the runtime DLL from the vcvarsall PATH and the quoted log path. Normal
clang-cl build after the change: 9 of 9, 0 skipped (15:21:23-15:22:11).

ASan time cost per program (ASan / clang-cl Release, one run each, not a benchmark): c5r_shaders_test 16.1 s / 0.7 s,
c5r_gamefs_game_test 7.0-8.5 s, c5r_terrain_test 6.5 s / 3.2 s, c5r_quadtest_test 6.0 s / 2.8 s; the others under 1 s.

**shell/ [C]: 237 of 237 pass, 0 skipped, 0 ASan reports** (`cmd /c shell\build.cmd asan` into
`D:/c5-64bit-work/3200/w6/shell-build-asan` 15:03:55-15:04:32, then `python shell/tools/memcap.py --mb 6144 -- python -m
pytest shell/tests -v -p no:cacheprovider -rs` 15:04:35-15:21:01, 986.9 s, peak committed 1,016 MB;
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1, `ASAN_OPTIONS=log_path="D:/c5-64bit-work/3200/w6/asan-shell-run1/asan"`,
C5_ASAN_LOG_DIR the same folder, which stayed empty). Per file: test_errors 56, test_tap 55, test_terrain_scene 59,
test_bake_tap 27, test_camera 16, test_tagged 12, test_camera_offline 6, test_build_cmd 3, test_standin 3.
`shell_server.exe`, `shell_composite.dll` and `test_host.exe` all import the ASan runtime (checked by name in the
binaries).

Not covered [I]: the shell ASan build links the **non-instrumented** c5render (`shell/build.cmd` builds it with clang-cl
and turns off the STL container annotations), so c5r::terrain inside `shell_server.exe` ran unchecked in the shell
suite; it is checked only by render/'s own tests above. Proposal for `shell/build.cmd asan` (not changed here, not W6's
file): build c5render with `render/build.py --asan --target c5render` into `%OUT%\render` and drop `SAN64DEF`, so the
server and its library share the instrumentation (untested). Not run under ASan: the render tools (fpkscan, shaderdump, hfbake,
quadtest, terrainshot) outside what the tests call, and anything in the game process.
