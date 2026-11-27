// render/src/terrainsets/sets.cpp - data: the terrain-side shader sets of CivilizationV_DX11.exe build 4390913 (task 2903).
// Every address is a VA at ImageBase 0x400000. Facts, evidence and method: research/05-graphics-composition/
// asset-pipeline.md section 3 and evidence/2900/2903/. "TR" = the TerrainRenderer object (0x29d1c bytes, built by
// 0x5bcef0, pointer TerrainSystem+0x75fa4 = 0xd8c584); "TR8" = its sub-object at TR+8 (constructor 0x458420) that the
// terrain tiles point to (tile+0x13c).
#include "c5r/terrainsets/terrainsets.h"

#include <cstring>

namespace c5r { namespace terrainsets {

namespace {

const Conf C = Conf::Confirmed;
const Conf I = Conf::Inferred;
const MemberType F1 = MemberType::Float, F2 = MemberType::Float2, F3 = MemberType::Float3, F4 = MemberType::Float4,
                 M4 = MemberType::Float4x4;
const TexDim T2 = TexDim::Tex2D, T3 = TexDim::Tex3D, TC = TexDim::Cube;

// ---------------------------------------------------------------- shared buffers (renderer ids, 0x8bd0a0 at 0x4e5c20)

// id 11, 0x250: the terrain frame constants, written by 0x6add60 (this = TR) once per view from the terrain draw 0x4241e0,
// the water-less load-screen draw 0x4ae6f0 and the overlay draw 0x782b70. v = view index (map wrap copies, count 0xd90fa4).
const CbLayout kTerrainFrame = {"TerrainFrame", 11, 0x250, "0x6add60", {
    {"ViewProj", 0x000, M4, 1, "D3DXMatrixMultiply(arg1 = g_View[v] 0xda1310+0x40v, arg3 = g_Proj 0xda12d0)", C},
    {"OverlayProj", 0x040, M4, 1, "*arg5 = 0xda1690+0x40v, top-down ortho written by Overlay_System 0x51b450; world -> Terrain Overlay texture", C},
    {"FOWAnim", 0x090, F4, 1, "0xda1c40..4c: (FluidFOW+0x1338, FluidFOW+0x1338, accumulated time * 0x9ed65c, 0), FluidFOW update 0x4090f0", C},
    {"BicubicBasis", 0x0a0, M4, 1, "constant rows (-1/8, 7/8, -7/8, 1/8), (3/8, -3/2, 9/8, 0), (-3/8, 0, 3/8, 0), (1/8, 3/4, 1/8, 0) [I: cubic patch basis]", C},
    {"TessFactor", 0x0e0, F1, 1, "float(TR+0x2428) = (1 << int(lod * -0.75)) * 0xd90f14 or 0xd90f14, set per frame by 0x4241e0", C},
    {"TessFactorOne", 0x0f0, F1, 1, "1.0 (0x9f72f4); read by the second hull shader 0x067f9cb8", C},
    {"HexScale", 0x100, F3, 1, "TR+0x1acc..d4 = 0xb9a6d4..dc = (1.0, 0.866, 0.2): plot x spacing, y spacing, height unit [I meaning]", C},
    {"AuxDir", 0x110, F3, 1, "TR+0x400..408 = (0.707, 0, 0.707) from the start-up 0x678290 [I: a fixed direction]", C},
    {"LightDir", 0x120, F3, 1, "TR+0x40c..414 [I: sun direction, TerrainSystem light 0x7572c..]", C},
    {"CameraPos", 0x130, F3, 1, "g_InvView[v] row 3 (0xda1410+0x40v, floats 12..14)", C},
    {"CameraAxisZ", 0x140, F3, 1, "g_InvView[v] floats 2, 6, 10", C},
    {"HalfVector", 0x150, F3, 1, "normalize(LightDir + CameraAxisZ)", C},
    {"HeightTexelScale", 0x160, F2, 1, "(1/1024, -1/1024) = 0x9de4f0, 0x9eae88; the terrain VS multiplies the height-texture texel coordinate by it", C},
    {"BakeTexelScale", 0x170, F2, 1, "(1/512, 1/512) = 0x9ecfb8 [I: cache texel size]", C},
    {"MapSize", 0x180, F2, 1, "float(TR+0x1ab0), float(TR+0x1ab4) = GridHeightmap+0xc/+0x10 (map width, height in plots)", C},
    {"Arg6", 0x190, F1, 1, "6th argument; 0x4241e0 passes 0xd90fa8 [I meaning]", C},
    {"FlagMode2", 0x1a0, F1, 1, "TR+0x2404 == 2 ? 1 : 0", C},
    {"FlagB", 0x1b0, F1, 1, "TR+0x241c != 0 ? 1 : 0", C},
    {"FlagC", 0x1c0, F1, 1, "TR+0x2418 != 0 ? 1 : 0", C},
    {"One", 0x1d0, F1, 1, "1.0", C},
    {"FlagD", 0x1e0, F1, 1, "TR+0x240c != 0 ? 1 : 0", C},
    {"ShadowMatrix0", 0x1f0, M4, 1, "*arg4 = 0xda1790+0x40v, written by the shadow view setup 0x615950 [I: shadow / screen projection]", C},
    {"ScreenSize", 0x230, F2, 1, "float(0x301056c), float(0x3010568): back-buffer width, height", C},
    {"OverlaySize", 0x240, F1, 1, "TR+0x528 = 2048.0 (set by 0x5bcef0); the Terrain Overlay target is 2048 x 2048 [I]", C},
}};

// id 19, 0x20: distance fog, written at the end of 0x6add60 from the DistanceFog globals (0xda1c50.., copied from the
// DistanceFog system 0xd90ec4 by 0x510466).
const CbLayout kDistanceFog = {"DistanceFog", 19, 0x20, "0x6add60", {
    {"FogColor", 0x00, F4, 1, "0xda1c50..5c = (DistanceFog color 0xd90ed4..dc, 1.0)", C},
    {"FogParams", 0x10, F4, 1, "x = -0xda1c60 (density 0xd90ecc); y = exp(-density * h) * (0x9ee11c / density) * 0xda1c64 (h = a camera field); z = 0; w = 0xda1c6c (0xd90fac)", C},
}};

// id 26, 0x250: the water frame constants, written per view by the water draw 0x739ae0 (this = Water, TerrainSystem+0x2772c).
const CbLayout kWaterFrame = {"WaterFrame", 26, 0x250, "0x739ae0", {
    {"ViewProj", 0x000, M4, 1, "g_View[v] x g_Proj", C},
    {"OverlayProj", 0x040, M4, 1, "0xda1690+0x40v (as TerrainFrame.OverlayProj)", C},
    {"WaveScale", 0x080, F4, 1, "sqrt(k) * Water+0xe4 * constants 0xa05214, 0x9feaf8, 0x9ee640, 0x9e4940", C},
    {"WavePhase", 0x090, F4, 1, "s * (n, n+1, n+2, n+3), n = trunc(Water+0xd4 * W), s = 1/W, W = world box width 0xd90fd8 - 0xd90fcc: whole bump tiles, not time (3604, water-system.md 10.1)", C},
    {"WaveConst", 0x0a0, F4, 1, "(0x9fef20, 0x9e395c, 0x9fef20, 0x9e395c)", C},
    {"WaveR11", 0x0b0, F1, 1, "Water+0xdc * s * n (the time-like member; WavePhase s, n) (3604)", C},
    {"WaveR12", 0x0c0, F1, 1, "Water+0xc8 * HexScale.z * 255: the water height, the VS z (3604)", C},
    {"WaterR13", 0x0d0, F1, 1, "Water+0xd0", C},
    {"WaterColorA", 0x0e0, F3, 1, "Water+0x90..98 [I meaning]", C},
    {"WaterColorB", 0x0f0, F3, 1, "Water+0x9c..a4 [I meaning]", C},
    {"WaterR16", 0x100, F4, 1, "Water+0x70..7c", C},
    {"WaterR17", 0x110, F4, 1, "Water+0x80..8c", C},
    {"WaterR18", 0x120, F4, 1, "Water+0xa8..b4", C},
    {"WaterR19", 0x130, F4, 1, "Water+0xb8..c4", C},
    {"FogParams", 0x140, F4, 1, "as DistanceFog.FogParams (computed again in 0x739ae0)", C},
    {"CameraPos", 0x150, F3, 1, "0xda1440+0x40v: inverse view translation (3604)", C},
    {"ShadowMatrix1", 0x160, M4, 1, "0xda1890+0x40v (shadow view setup 0x615950)", C},
    {"ShadowMatrix0", 0x1a0, M4, 1, "0xda1790+0x40v", C},
    {"HeightmapUV", 0x1e0, M4, 1, "Scaling(1/0xd90fe4, k/0xd90fe8, ...) x Translation: world -> WorldHeightMap uv [I meaning]", C},
    {"R34", 0x220, F1, 1, "0xd90fa8 = GVS+0x78C, the camera's eased shadow factor (written by 0x523d97) (3604)", C},
    {"FogColor", 0x230, F4, 1, "0xda1c50..5c", C},
    {"R36", 0x240, F1, 1, "Water+0x6c", C},
}};

// id 18, 0x600: per-layer parameters of the terrain blend (TerrainShaderScreen), written by 0x6c47f0 (this = TR).
const CbLayout kTerrainLayers = {"TerrainLayers", 18, 0x600, "0x6c47f0", {
    {"Layer", 0x000, F4, 96, "for i < TR+0x1118: x = int(TR+0xe18[TR+0x1018[i]-ish] * k * dt) * scale, y = float(TR+0x1018[i]), z = w = 0", C},
}};

// id 29, 0x40: overlay view-projection, copied by 0x43c370 from its matrix argument (Overlay_System).
const CbLayout kOverlayFrame = {"OverlayFrame", 29, 0x40, "0x43c370", {
    {"OverlayViewProj", 0x00, M4, 1, "the matrix argument of 0x43c370", C},
}};

// Shared buffers whose fill was not traced: one array member covering the buffer (names not recovered).
const CbLayout kBakeNormal = {"BakeNormal", 39, 0x20, "0x694a60 (0x6958fe), not traced", {
    {"Data", 0x00, F4, 2, "not traced", I}}};
// id 27, 0x80: the coast-wave frame constants, written per view by Wave_System's render 0x643b60 (this = Wave_System
// 0x1a0c6c8; its fields from WaveSettings.xml, 0x549c80) [C, issue 3602].
const CbLayout kWaveFrame = {"WaveFrame", 27, 0x80, "0x643b60", {
    {"ViewProj", 0x00, M4, 1, "D3DXMatrixMultiply(g_View[v] 0xda1310+0x40v, g_Proj 0xda12d0)", C},
    {"StartDistance", 0x40, F1, 1, "Wave+0x50 * Wave+0x1c: StartDistance (WaveSettings.xml) scaled by Wave+0x50; was named SizeMin (3606, water-system.md 12.1)", C},
    {"EndDistance", 0x44, F1, 1, "Wave+0x50 * Wave+0x20: EndDistance (WaveSettings.xml) scaled by Wave+0x50; was named SizeMax (3606, water-system.md 12.1)", C},
    {"InvLifetime", 0x48, F1, 1, "1 / Wave+0x24 (Wave+0x24 = lifetime; the update 0x6ab850 restarts a wave past it)", C},
    {"PlaneZ", 0x4c, F1, 1, "*(*(0xd90fc4) + 4) + 1.0: the z of every wave vertex [I: water height + 1]", C},
    {"FadeInStart", 0x50, F1, 1, "Wave+0x28", C},
    {"FadeInRate", 0x54, F1, 1, "1 / (Wave+0x2c - Wave+0x28); VS: alpha = sat((age - start) * rate) * (1 - sat(fade out))", C},
    {"FadeOutStart", 0x58, F1, 1, "Wave+0x30", C},
    {"FadeOutRate", 0x5c, F1, 1, "1 / (Wave+0x34 - Wave+0x30)", C},
    {"AtlasScale", 0x60, F2, 1, "1 / float(Wave+0x8), 1 / float(Wave+0xc): one atlas cell of Wave_Coastline_ALL.dds; .zw not written", C},
    {"Tint", 0x70, F4, 1, "(Wave+0x40, +0x44, +0x48, 1.0); the PS multiplies the texel by .xyz and the fade, then ^(1/2.2)", C},
}};
// id 43, 0x40: the currents constants, written by 0x53a9f0 (this = Water 0xd3dd0c) before its one draw [C, issue 3602].
const CbLayout kCurrentsFrame = {"CurrentsFrame", 43, 0x40, "0x53a9f0", {
    {"Size", 0x00, F2, 1, "float([0xd91264]), float([0xd91268]) (bss, written through a pointer): the grid's plots, Tiny 56 x 40 [L: capture fit]; "
                          "the Sobel step is 1 / Size, the noise lattice floor(Size / NoiseCell)", C},
    {"NoiseAmplitude", 0x10, F1, 1, "Water+0x64 = 0.1 (0x9e402c, Water::Startup); the debug hook 0x406c90 sets it to its 2nd arg * 0.01", C},
    {"NoiseCell", 0x20, F1, 1, "Water+0x68 = 2.0 (0x9dbbbc); hook 0x406c90: float(3rd arg)", C},
    {"GradientScale", 0x30, F1, 1, "Water+0x60 = 0.1 (0x9e402c); hook 0x406c90: 1st arg * 0.01", C},
}};
const CbLayout kRiverFrame = {"RiverFrame", 31, 0xd0, "not traced", {
    {"ViewProj", 0x00, M4, 1, "not traced (VS reads registers 0-3 fully)", I},
    {"Data", 0x40, F4, 9, "not traced", I}}};
const CbLayout kIceFrame = {"IceFrame", 42, 0x1e0, "0x5fb0ff region, not traced", {
    {"ViewProj", 0x00, M4, 1, "not traced (VS reads registers 0-3 fully)", I},
    {"Data", 0x40, F4, 26, "not traced", I}}};
const CbLayout kGridFrame = {"GridFrame", 28, 0xf0, "0x6a2cc0 region, not traced", {
    {"Data", 0x00, F4, 15, "not traced", I}}};

// ---------------------------------------------------------------- own buffers (slot 0, copied at draw time)

// TerrainShader, 0x70: per terrain tile, written by the tile draw 0x5c37e0 (this = tile) into the program's block (0x6bbe60).
const CbLayout kTerrainTile = {"TerrainTile", -1, 0x70, "0x5c37e0", {
    {"TileWorld", 0x00, M4, 1, "D3DXMatrixTranslation(-TR8+0x1ab0, -TR8+0x1ab4, -TR8+0x1ab8) = minus (half map width, half map height * 0.866, 0) in world units", C},
    {"TileOffset", 0x40, F4, 1, "float(tile+0xdc, +0xe0, +0xe4, +0xe8); the VS adds .xy and .zw to the patch grid position (height-texture texels) [I meaning]", C},
    {"InvGridSize", 0x50, F2, 1, "1/(tile+0xd4 + 1), 1/(tile+0xd8 + 1)", C},
    {"LodStep", 0x60, F2, 1, "(tile+0xd4 - 1)/(n - 1), (tile+0xd8 - 1)/(m - 1), n, m halved tile+0x1c4 times", C},
    {"LodMip", 0x68, F1, 1, "TR+0x2424 (floor of the terrain LOD); the VS samples the height texture at this mip", C},
}};

// TerrainDecalShader, 0x70: per decal batch, written by 0x742ef0 (TerrainDecalSystem).
const CbLayout kTerrainDecal = {"TerrainDecal", -1, 0x70, "0x742ef0", {
    {"DecalViewProj", 0x00, M4, 1, "the matrix argument (5th) of 0x742ef0", C},
    {"Const", 0x40, F4, 1, "(1, 1, 0, 0)", C},
    {"DecalParams", 0x50, F4, 1, "decal type entry +0x24..+0x30", C},
    {"DecalParam6", 0x60, F1, 1, "decal type entry +0x20", C},
}};

const CbLayout kTerrainScreenOwn = {"TerrainScreenOwn", -1, 0x4c0, "not traced (0x694a60 bake)", {
    {"Header", 0x000, F4, 9, "not traced (VS reads .xy of registers 0-8)", I},
    {"Pieces", 0x090, F4, 64, "not traced (indexed dynamically from register 9)", I},
    {"R73", 0x490, F4, 1, "not traced", I},
    {"R74", 0x4a0, F4, 1, "not traced", I},
    {"R75", 0x4b0, F4, 1, "not traced", I}}};
// The pass-0 VS 0x067e4fd8 computes SV_Position = p.x * r0 + p.y * r1 + p.z * r2 + r3 (p = POSITION.xyz: a row
// vector times a row-major 4x4) and passes TEXCOORD0 through; the pass-0 PS 0x067f7cb0 returns t0.Sample(s0, uv) [C,
// disassembly]. The filler of the CPU block was not traced.
const CbLayout kCompositeOwn = {"CompositeOwn", -1, 0x50, "not traced", {
    {"Transform", 0x00, M4, 1, "row-major transform of POSITION (VS: p.x*r0 + p.y*r1 + p.z*r2 + r3) [C use]; writer not traced", I},
    {"R4", 0x40, F4, 1, "not traced (pass-1 VS reads it)", I}}};
const CbLayout kCompressOwn = {"CompressOwn", -1, 0x10, "not traced", {
    {"Param", 0x00, F4, 1, "not traced (PS reads .x)", I}}};
// WaterShader own, 0x80: the river draw 0x463640 writes its first 64 bytes (an identity matrix, the river mesh is in
// world units) through the block of River+0x10; nothing writes or reads 0x40..0x7f; the ocean passes declare no cb0
// [C, issue 3602].
const CbLayout kWaterOwn = {"WaterOwn", -1, 0x80, "0x463640", {
    {"World", 0x00, M4, 1, "identity (0x463640, 16 floats from a local identity); the river VS reads .xyz of rows 0-3", C}}};
const CbLayout kRiverOwn = {"RiverOwn", -1, 0x60, "not traced", {
    {"World", 0x00, M4, 1, "not traced", I},
    {"Data", 0x40, F4, 2, "not traced", I}}};
// WaveShader own, 0xf80 = 248 records of 16 bytes: per draw batch, 0x643b60 copies the batch's records from the wave
// array Wave+0x5c (memcpy of count * 16 bytes from the batch's first index; the batch builder 0x6a6490 cuts batches of at
// most 248 = 0xf8 waves) [C, issue 3602].
const CbLayout kWaveOwn = {"WaveOwn", -1, 0xf80, "0x643b60", {
    {"Waves", 0x000, F4, 248, "per wave (atlas cell u, v offset (0x5eb230 picks the cell), age in seconds (0x6ab850 adds dt; "
                              "past Wave+0x24 a new negative start delay from the LCG 0x343fd / 0x269ec3), unused); the VS "
                              "indexes it by BLENDINDICES0.x", C}}};
const CbLayout kOverlayOwn = {"OverlayOwn", -1, 0xe0, "not traced", {
    {"World", 0x00, M4, 1, "not traced", I},
    {"Params", 0x40, F4, 8, "not traced (VS reads .xy of 4, 5, 7, 8, 9, 11)", I},
    {"Color", 0xc0, F4, 1, "not traced (PS reads it whole) [I: overlay colour]", I},
    {"R13", 0xd0, F1, 1, "not traced", I}}};
const CbLayout kFowCellOwn = {"FOWCellOwn", -1, 0x40, "not traced", {
    {"ViewProj", 0x00, M4, 1, "not traced", I}}};
const CbLayout kFowParticleOwn = {"FOWParticleOwn", -1, 0x90, "not traced", {
    {"Transform0", 0x00, M4, 1, "not traced", I},
    {"Transform1", 0x40, M4, 1, "not traced", I},
    {"R8", 0x80, F4, 1, "not traced", I}}};
// Traced by task 3952 (2026-11-14; fog-of-war.md 3.3, 7.1): 24 float4x4 rows (4 float4 each), one per hexagon slot of the
// batch, indexed by the vertex's NORMAL (the slot); shared by the VS and the PS of passes 0-2.
const CbLayout kFowDrawMaskOwn = {"FOWDrawMaskOwn", -1, 0x600, "the FOW mask render 0x49c940 (passes 0-2, 24 hexagons a draw)", {
    {"Hexagons", 0x000, F4, 96, "24 x float4x4 row-major: Translate(hx, hy, -hx-hy) x M_h2w 0xd91030 x Scale(sx, sy, 1) x "
                                "Translate(-ox, -oy, 0) x OverlayProj[v] 0xda1690+0x40v; unused slots 0 [C 3950, L 3952 cb0 of "
                                "four captures]", C}}};
// FOWBaseLayerOwn (task 3955, fog-of-war.md 3.5 and 7.2): written per draw of 24 plane quads; the EXE's buffer is 3,328 B
// [L]. The VS indexes both arrays by NORMAL.z x 4 (the quad's slot).
const CbLayout kFowBaseLayerOwn = {"FOWBaseLayerOwn", -1, 0xc50, "FluidFOW base layer render (fog-of-war.md 3.5)", {
    {"WVP", 0x000, F4, 96, "24 float4x4 (row vectors) World x View x Proj of the slot's quad (indexed dynamically)", Conf::Live},
    {"World", 0x600, F4, 96, "24 float4x4 World = Scale(32 sx, 32 sy, 1) x Translate(block centre, plane height 11.2 / 38.25 / 51) (indexed dynamically)", Conf::Live},
    {"OverlayProj", 0xc00, M4, 1, "0xda1690 + 0x40 v: the view's overlay window, for the mask's uv", Conf::Live},
    {"FOWAnim", 0xc40, F4, 1, "(FOWAnim.x, FOWAnim.y, FOWAnim.z x s, FOWAnim.w x s), s = 1.0 / 1.5 / 2.0 per plane", Conf::Live}}};
const CbLayout kGridOwn = {"GridOwn", -1, 0x160, "not traced", {
    {"Data", 0x000, F4, 22, "not traced (indexed dynamically from 0 and 9)", I}}};

// ---------------------------------------------------------------- texture slots

// TerrainShader draw object, built by 0x5ad770 (material 0x49): 10 PS textures, VS t0. TR8 block = TR+0x548.. =
// 0xda12a4.. (10 handles TerrainSystem passes to the renderer's start-up 0x678290).
const std::vector<TexSlot> kTerrainPs = {
    {0, T2, "TileCacheColor", "tile+0x198 (or tile+0x190): the tile's baked colour cache (CacheBake target 0, BC-compressed by CompressBC) [I: lit albedo]", 5, C},
    {1, T2, "TerrainOverlay", "0xda12a4 \"Terrain Overlay\" 2048x2048 target (Overlay_System 0x6ead50: culture borders and hex overlays)", 5, C},
    {2, T2, "DistanceFogRamp", "0xda12b4 assets/DistanceFog/FogRamp.dds (0x664780)", 5, C},
    {3, T2, "ShadowTexture", "0xda12bc \"Shadow Texture Blur\" (or the raw one, 0x52fa70)", 0, C},
    {4, T2, "FOWMask", "0xda12ac \"FOW Mask\" target (FluidFOW 0x44a570)", 0, C},
    {5, T2, "FOWBaseLayer", "0xda12b0 FluidFOW fog cloud texture (FogCloudTile_dif_0x.dds or built)", 0, C},
    {6, T3, "TransFOWColor", "0xd90e60[style]: ColorKey TerrainFOW 32^3 RGBA8 LUT (ColorKeySystem)", 5, C},
    {7, T3, "ShadowColorKey", "0xd90e74[style]: ColorKey TerrainShadow 32^3 RGBA8 LUT", 5, C},
    {8, T2, "TileCacheSecond", "tile+0x19c (or tile+0x194): the tile's second cache (CacheBake target 1) [I: specular / normal]", 5, C},
    {9, T2, "SpecScreenMap", "0xda12c8 assets/terrain/lighting/specscreenmap.dds (0x561a3d)", 5, C},
};
const std::vector<TexSlot> kTerrainVs = {
    {0, T2, "TerrainHeight", "tile+0xec: the tile's height texture (sampled with SampleLevel at TerrainTile.LodMip)", 0, C},
};

// CacheBake draw object 0x6cf830 (material 0x4b): 12 PS textures, VS t0.
const std::vector<TexSlot> kCacheBakePs = {
    {0, T2, "BakeInput0", "argument 3 of 0x6cf830 [I: blended albedo from TerrainShaderScreen]", 5, C},
    {1, T2, "BakeInput1", "argument 4 [I: blended normal]", 5, C},
    {2, T2, "BakeInput2", "argument 6 [I: blended specular]", 5, C},
    {3, T2, "BakeInput3", "argument 5 [I: decal layer]", 5, C},
    {4, TC, "SunlightCube", "TR8+0x3d0[style] <- assets/terrain/lighting/Sunlight_<Style>_Terrain.dds (TerrainSystem+0x75f74)", 2, C},
    {5, TC, "ShadowlightCube", "TR8+0x3e4[style] <- Shadowlight_<Style>_Terrain(_2).dds (TerrainSystem+0x75f88)", 2, C},
    {8, T3, "TerrainColorKey", "0xd90e4c[style]: ColorKey Terrain 32^3 RGBA8 LUT", 5, C},
    {9, T2, "FOWMask", "0xda12ac", 5, C},
    {10, T3, "TransFOWColor", "0xd90e60[style]", 5, C},
    {11, T2, "SpecScreenMap", "0xda12c8", 0, C},
};

// TerrainShaderScreen (blend of the tile's terrain layers into the bake targets), draw objects in 0x694a60: with the
// global 0xd90f28 == 0, pass 0 with 16 textures: for layer i < tile+0x1f8 (k = TR8+0xf10[tile+0x1fc[i]]) t i =
// TR8+0x708[k], t 8+i = TR8+0x908[k], sampler 0, unused slots 0; else 24 textures (unused = TR8+0xb08), 3 samplers,
// pass 1 (TerrainUseAdvancedGPU 0xd90f24 != 0) or 2.
const std::vector<TexSlot> kTerrainScreenPs = [] {
    std::vector<TexSlot> v;
    for (uint32_t i = 0; i < 8; ++i)
        v.push_back({i, T2, "LayerTextureA", "TR8+0x708[layer k] [I: the layer's diffuse texture]", 0, C});
    for (uint32_t i = 8; i < 16; ++i)
        v.push_back({i, T2, "LayerTextureB", "TR8+0x908[layer k] [I: the layer's second texture]", 0, C});
    for (uint32_t i = 16; i < 24; ++i)
        v.push_back({i, T2, "LayerTextureC", "24-texture variant only; not traced per slot", -1, I});
    return v;
}();
const std::vector<TexSlot> kTerrainScreenVs = {
    {0, T2, "PieceHeight", "not traced [I: piece height map]", -1, I},
};

const std::vector<TexSlot> kOneTex = {
    {0, T2, "Texture", "not traced", -1, I},
};

const std::vector<TexSlot> kNormalCalcPs = {
    {0, T2, "HeightTemp", "not traced [I: TR+0x69c \"Terrain Height Temp\"]", -1, I},
};
const std::vector<TexSlot> kCompositePs = {
    {0, T2, "Source", "not traced [I: bake temp target]", -1, I},
};

const std::vector<TexSlot> kDecalPs = {
    {0, T2, "DecalDiffuse", "decal type entry +0x14 [I: \"Terrain Decal Diffuse\"]", 2, C},
    {1, T2, "DecalHeight", "decal type entry +0x18 [I: \"Terrain Decal Height\"]", 0, C},
    {2, T2, "DecalThird", "decal type entry +0x1c [I]", 0, C},
};

// WaterShader slots: names from the EXE's table 0xc9e418; the ocean draw object is built in 0x739ae0, the river one in
// 0x6b1580 (both material 0x5b).
const std::vector<TexSlot> kWaterPs = {
    {0, T2, "LEAN1_1", "ocean: Water+0x1c = waterbumps16.lean four-channel block (R16G16B16A16_UNORM); river: 0xd3dd30 = "
                       "Water+0x24, riversmooth16.lean four-channel (Water::Startup 0x5f06a0; c5r/terrainsets/lean.h)", 0, C},
    {1, T2, "LEAN1_2", "ocean: Water+0x20 = waterbumps16.lean one-channel block (R16_UNORM); river: 0xd3dd38 = Water+0x2c, "
                       "riversmooth16.lean one-channel", 0, C},
    {2, T2, "LEAN2_1", "ocean: none; river: 0xd3dd34 = Water+0x28, riverrough_new16.lean four-channel", 0, C},
    {3, T2, "LEAN2_2", "ocean: none; river: 0xd3dd3c = Water+0x30, riverrough_new16.lean one-channel", 0, C},
    {4, T2, "ReflectionMap", "0xda12c0 \"Reflection Texture Raw\" (Water::Startup)", 0, C},
    {5, T2, "WaterBlendMap", "ocean: 0xda12c4 \"Water Currents\" 256x256 RGBA8 target (WorldCurrents pass 3, c5r/water/currents.h), "
                             "sampler 0; river: 0xd3dd1c = Water+0x10 = river_alpha_new.dds, sampler 5", 0, C},
    {6, T2, "WaterColor", "ocean: Water+0x14 = waterdepthcolor_new.dds; river: 0xd3dd24 = Water+0x18 = river_diffuse_new.dds", 3, C},
    {7, T2, "WorldHeightMap", "ocean: TR+0x12ac[cell] height texture; river: River+0x24 (32x32)", 0, C},
    {8, T2, "WaterFogRamp", "0xda12b4 (the DistanceFog ramp)", 5, C},
    {9, T2, "OverlayMap", "0xda12a4 Terrain Overlay", 0, C},
    {10, T2, "FOWMask", "0xda12ac FOW Mask", 0, C},
    {11, T3, "TransFOWColor", "0xd90e60[0] ColorKey TerrainFOW Generic", 5, C},
    {12, T3, "ColorKey", "river: 0xd90e9c[0] ColorKey Rivers Generic; ocean: not set (passes 0, 2, 3 do not read it)", 5, C},
    {13, T2, "DynamicShadowTexture", "ocean: 0xda12bc Shadow Texture Blur (its sampler word is never written, 0x739ae0 writes t11's twice)", -1, C},
};

const std::vector<TexSlot> kWavePs = {{0, T2, "g_WaveTexture", "Wave+0x68 = Wave_Coastline_ALL.dds (0x549c80; EffectTextures.fpk), "
                                                            "draw objects 0x6a6490", 3, C}};
const std::vector<TexSlot> kCurrentsPs = {{0, T2, "TerrainHeight", "draw-object texture handle 0 (0x444870 writes 0; nothing sets it "
                                                                   "later) [C]; what handle 0 resolves to is [I] (a null view gives "
                                                                   "a zero gradient: the map is then the noise alone)", 0, C}};
const std::vector<TexSlot> kRiverShaderPs = {{1, T2, "Texture1", "not traced (no draw object of material 0x45 found)", -1, I}};

const std::vector<TexSlot> kOverlayPs = {
    {0, T2, "GlowSampler", "not traced", -1, I},
    {1, T2, "RingSampler", "not traced", -1, I},
    {2, T2, "AlphaSampler", "not traced", -1, I},
};

const std::vector<TexSlot> kFowParticlePs = {
    {0, T2, "PartTexture", "not traced", -1, I},
    {1, T2, "FOWMask", "not traced [I: 0xda12ac]", -1, I},
};
const std::vector<TexSlot> kFowLayerPs = {
    {0, T2, "FOWBaseLayerTexture", "not traced [I: 0xda12b0]", -1, I},
    {1, T2, "FOWBaseMask", "not traced", -1, I},
};

const std::vector<TexSlot> kIcePs = {
    {0, TC, "g_Cube", "not traced", -1, I},
    {1, T2, "g_Diffuse", "not traced", -1, I},
    {2, T2, "g_Lean1", "not traced", -1, I},
    {3, T2, "g_Lean2", "not traced", -1, I},
    {4, T2, "g_Diffuse_Side", "not traced", -1, I},
    {5, T2, "g_Lean1_Side", "not traced", -1, I},
    {6, T2, "g_Lean2_Side", "not traced", -1, I},
    {7, T2, "g_FogRamp", "not traced", -1, I},
    {8, T2, "FOWMask", "not traced", -1, I},
    {9, T2, "FOWBaseLayer", "not traced", -1, I},
    {10, T3, "FOWColorKey", "not traced [I: a ColorKey LUT]", -1, I},
};
const std::vector<TexSlot> kGridPs = {
    {0, T2, "BaseIndexTexture", "not traced", -1, I},
    {1, T2, "BaseBlockTexture", "not traced", -1, I},
    {2, T2, "ColorTexturePointSampled", "not traced", -1, I},
    {3, T2, "ColorTextureLinearFiltered", "not traced", -1, I},
};

const char* kTerrainInput =
    "tessellated passes 0-2: no vertex buffer, 16-control-point patch list (topology 0x30) over the tile's index buffer "
    "tile+0xf8; the VS takes SV_VertexID only: gx = (id & 255) - 3, gy = (id >> 8) - 3; height = "
    "t0.SampleLevel(((gx, gy) * LodStep + TileOffset.xy + TileOffset.zw + 0.5) * HeightTexelScale, LodMip).r * "
    "HexScale.z * 255. Non-tessellated passes 3-7: POSITION.xyzw, NORMAL.xyz, TEXCOORD0.xy (VS mask 0x7), vertex buffer "
    "tile+0xf4, index buffer tile+0x168";

const std::vector<ShaderSetDesc> kSets = {
    {"TerrainShader", 73, "terrain",
     "TerrainSystem render 0x657070 -> TerrainRenderer draw 0x4241e0 (per view: 0x6add60 frame constants, then per tile "
     "0x5c37e0); draw objects 0x5ad770 (via 0x690f50 non-tessellated, 0x768160 tessellated LODs)",
     {
         {0, {0x2de1c8c, 0x2de1cb8, 0x2de1cf4, 0x2de1d00}, {0x067d6490, 0x067e6e58, 0x067f98f0, 0x067fa080}, {0, 0x067e78e8}, {0, 0x067e8328},
          "tessellated, tile flag +0x2fc set (PS with t8/t9)"},
         {1, {0x2de1c8c, 0x2de1cbc, 0x2de1cf4, 0x2de1d00}, {0x067d6490, 0x067e8d68, 0x067f98f0, 0x067fa080}, {0, 0x067e95f0}, {0, 0x067e9e38},
          "tessellated (TerrainTessLevel 0xd90f70 != 0 and BicubicTerrainTessellation 0xd90f1c != 0): the normal terrain pass"},
         {2, {0x2de1c8c, 0x2de1cc0, 0x2de1cf8, 0x2de1d00}, {0x067d6490, 0x067ea680, 0x067f9cb8, 0x067fa080}, {0, 0x067ea818}, {0, 0x067ea9a8},
          "tessellated, factor 1, PS reads only t1 [I: not selected by 0x5c37e0]"},
         {3, {0x2de1c90, 0x2de1cb8, 0, 0}, {0x067d6798, 0x067e6e58, 0, 0}, {0x067d6d80, 0x067e78e8}, {0x067d7360, 0x067e8328},
          "not tessellated, tile flag +0x2fc set"},
         {4, {0x2de1c90, 0x2de1cbc, 0, 0}, {0x067d6798, 0x067e8d68, 0, 0}, {0x067d6d80, 0x067e95f0}, {0x067d7360, 0x067e9e38},
          "not tessellated (TessLevel != 0, Bicubic off)"},
         {5, {0x2de1c90, 0x2de1cc0, 0, 0}, {0x067d6798, 0x067ea680, 0, 0}, {0x067d6d80, 0x067ea818}, {0x067d7360, 0x067ea9a8},
          "[I: not selected by 0x5c37e0]"},
         {6, {0x2de1c94, 0x2de1cbc, 0, 0}, {0x067d7940, 0x067e8d68, 0, 0}, {0x067d7df8, 0x067e95f0}, {0x067d82b0, 0x067e9e38},
          "TerrainTessLevel == 0 (fallback mesh, no height texture in the VS)"},
         {7, {0x2de1c94, 0x2de1cc0, 0, 0}, {0x067d7940, 0x067ea680, 0, 0}, {0x067d7df8, 0x067ea818}, {0x067d82b0, 0x067ea9a8},
          "[I: not selected by 0x5c37e0]"},
     },
     {{0, &kTerrainTile}, {1, &kTerrainFrame}, {2, &kDistanceFog}},
     kTerrainPs, kTerrainVs, kTerrainInput,
     "Pass choice 0x5c37e0 [C]: TessLevel ? (Bicubic ? 1 : 4) : 6; tile+0x2fc != 0 -> Bicubic ? 0 : 3. Draw: "
     "DrawIndexedInstanced(16 * patches) for patch lists. HS: quad domain, integer partitioning, triangle_ccw, 16 control "
     "points, factor TerrainFrame.TessFactor. The texture `style` index is the lowest set bit 1..4 of tile+0x138 (0 = Generic, "
     "1 America, 2 Asia, 3 Africa, 4 Europe [I: order of 0xb9a428]). Blend/depth/raster state not recovered.",
     C},
    {"CacheBake", 75, "terrain-bake",
     "terrain tile bake 0x694a60 (\"Terrain Cache\" targets tile+0x190/+0x194), draw object 0x6cf830",
     {
         {0, {0x2de1c9c, 0x2de1cc8, 0, 0}, {0x067d8cb8, 0x067ebe78, 0, 0}, {0x067d8f98, 0x067ec478}, {0x067d9278, 0x067eca48},
          "one target"},
         {1, {0x2de1c9c, 0x2de1ccc, 0, 0}, {0x067d8cb8, 0x067ed018, 0, 0}, {0x067d8f98, 0x067ed838}, {0x067d9278, 0x067ee018},
          "two targets (SV_Target0, SV_Target1)"},
     },
     {{1, &kTerrainFrame}},
     kCacheBakePs, {{0, T2, "TerrainHeight", "tile+0xec (draw object VS t0, sampler 1)", 1, C}},
     "POSITION.xyzw, NORMAL.xyz, TEXCOORD0.xy",
     "GATE2 called 0x067ebe78 / 0x067ed018 the water PS; the material table says CacheBake (material 75) [C]. "
     "The VS declares no texture although the draw object binds one.",
     C},
    {"TerrainShaderScreen", 76, "terrain-bake",
     "terrain tile bake 0x694a60 (draw objects 0x695508, 0x695ae2): blends the cell's terrain pieces into the "
     "\"Terrain Albiedo / Normal / Spec Temp\" targets (TR+0x6a8.. )",
     {
         {0, {0x2de1ca0, 0x2de1cd0, 0, 0}, {0x067d9550, 0x067ee7f0, 0, 0}, {0x067da4f0, 0x067efa60}, {0x067db490, 0x067f0c50}, ""},
         {1, {0x2de1ca4, 0x2de1cd4, 0, 0}, {0x067dc430, 0x067f1e40, 0, 0}, {0x067defd8, 0x067f36e0}, {0x067e1ab8, 0x067f4ec0}, ""},
         {2, {0x2de1ca0, 0x2de1cd4, 0, 0}, {0x067d9550, 0x067f1e40, 0, 0}, {0x067da4f0, 0x067f36e0}, {0x067db490, 0x067f4ec0}, ""},
         {3, {0x2de1ca8, 0x2de1cd8, 0, 0}, {0x067e4590, 0x067f6698, 0, 0}, {0x067e4900, 0x067f6c80}, {0x067e4c70, 0x067f7258}, ""},
         {4, {0x2de1c98, 0x2de1cdc, 0, 0}, {0x067d8760, 0x067f7830, 0, 0}, {0x067d8928, 0x067f79b8}, {0x067d8af0, 0x067f7b38}, ""},
     },
     {{0, &kTerrainScreenOwn}, {1, &kTerrainFrame}, {2, &kTerrainLayers}},
     kTerrainScreenPs, kTerrainScreenVs,
     "POSITION.xyzw, TEXCOORD0.xy, TEXCOORD1.xyz, TEXCOORD2.xyzw, TEXCOORD3.xyzw, BLENDINDICES0.xyzw, BLENDINDICES1.xyzw",
     "Pass 0 (0xd90f28 == 0): 16 textures, 8 layers x 2; passes 1/2: 24 textures. The own buffer is filled in 0x694a60 "
     "with one 0x80-byte record per layer (8 layers) [I: layout not decoded]; slot 2 = TerrainLayers.", C},
    {"NormalCalculate", 74, "terrain-bake", "terrain tile bake 0x694a60 (\"Normal Calculation\"), draw object 0x515630",
     {{0, {0x2de1c98, 0x2de1cc4, 0, 0}, {0x067d8760, 0x067eab30, 0, 0}, {0x067d8928, 0x067eb1c8}, {0x067d8af0, 0x067eb820}, ""}},
     {{1, &kBakeNormal}}, kNormalCalcPs, {}, "POSITION.xyzw, NORMAL.xyz, TEXCOORD0.xy", "", C},
    {"CompositeShader", 77, "terrain-bake", "TerrainRenderer program TR8+0x5c8 (0x5ea6c0); draw not traced",
     {{0, {0x2de1cac, 0x2de1ce0, 0, 0}, {0x067e4fd8, 0x067f7cb0, 0, 0}, {0x067e5310, 0x067f7e48}, {0x067e5648, 0x067f7fd8}, ""},
      {1, {0x2de1cb0, 0x2de1ce4, 0, 0}, {0x067e5978, 0x067f8160, 0, 0}, {0x067e5cc8, 0x067f8328}, {0x067e6018, 0x067f84e8}, ""}},
     {{0, &kCompositeOwn}}, kCompositePs, {}, "POSITION.xyzw, NORMAL.xyz, TEXCOORD0.xy", "", I},
    {"CompressBC", 41, "terrain-bake", "TerrainRenderer program TR8+0x658 (0x76ba70); \"DXT Compress\" of the tile cache in 0x694a60",
     {{0, {0x2de1984, 0x2de1988, 0, 0}, {0x06743180, 0x067434f0, 0, 0}, {0x067432a8, 0x067451e0}, {0x067433d0, 0x06746ee0}, ""},
      {1, {0x2de1984, 0x2de198c, 0, 0}, {0x06743180, 0x06748bd8, 0, 0}, {0x067432a8, 0x0674b4f0}, {0x067433d0, 0x0674df00}, ""},
      {2, {0x2de1984, 0x2de1990, 0, 0}, {0x06743180, 0x06750908, 0, 0}, {0x067432a8, 0x06751c18}, {0x067433d0, 0x06752f28}, ""},
      {3, {0x2de1984, 0x2de1994, 0, 0}, {0x06743180, 0x06754298, 0, 0}, {0x067432a8, 0x06755a10}, {0x067433d0, 0x06757030}, ""},
      {4, {0x2de1984, 0x2de1998, 0, 0}, {0x06743180, 0x06758650, 0, 0}, {0x067432a8, 0x0675c410}, {0x067433d0, 0x06760338}, ""}},
     {{0, &kCompressOwn}}, kOneTex, {}, "(VS without inputs used by the material: a full-screen pass)", "", C},
    {"TerrainDecalShader", 78, "decal",
     "TerrainDecalSystem (TerrainSystem+0) batch draw 0x742ef0, pass 1, into the bake (\"Decal Layer\" in 0x694a60) [I]",
     {{0, {0x2de1cb4, 0x2de1ce8, 0, 0}, {0x067e6360, 0x067f86a0, 0, 0}, {0x067e6708, 0x067f8988}, {0x067e6ab0, 0x067f8c60}, "two targets"},
      {1, {0x2de1cb4, 0x2de1cec, 0, 0}, {0x067e6360, 0x067f8f30, 0, 0}, {0x067e6708, 0x067f9280}, {0x067e6ab0, 0x067f95b8},
       "three targets; the one 0x742ef0 uses"}},
     {{0, &kTerrainDecal}}, kDecalPs, {}, "POSITION.xy, TEXCOORD0.xy (decal batch vertices, 0x7fff8-byte chunks)",
     "Roads, railroads, farms and other decal producers write into TerrainDecalSystem (stubs/terrain.md section 3).", C},
    {"WaterShader", 91, "water",
     "ocean, coast and lakes: water draw 0x739ae0 (TerrainSystem render 0x657070 when TerrainSystem+0x75fda); rivers: "
     "RiverVisSystem draw object 0x6b1580 (River render 0x463640)",
     {{0, {0x2de1da0, 0x2de1da8, 0, 0}, {0x06815a48, 0x068179f0, 0, 0}, {0x06815f90, 0x06818698}, {0x068164d8, 0x068192f0}, "ocean/lakes, water quality [0xd90f68] 0"},
      {1, {0x2de1da4, 0x2de1dac, 0, 0}, {0x06816a20, 0x06819f60, 0, 0}, {0x06816f68, 0x0681a6b0}, {0x068174b0, 0x0681adc8}, "river, water quality 0 (no LEAN textures read)"},
      {2, {0x2de1da0, 0x2de1db0, 0, 0}, {0x06815a48, 0x0681b500, 0, 0}, {0x06815f90, 0x0681d1f8}, {0x068164d8, 0x0681ee70}, "ocean/lakes, water quality 1"},
      {3, {0x2de1da0, 0x2de1db4, 0, 0}, {0x06815a48, 0x06820b00, 0, 0}, {0x06815f90, 0x06822c98}, {0x068164d8, 0x06824d90}, "ocean/lakes, water quality 2"},
      {4, {0x2de1da4, 0x2de1db8, 0, 0}, {0x06816a20, 0x06826ea0, 0, 0}, {0x06816f68, 0x06828a38}, {0x068174b0, 0x0682a548}, "river, water quality 1 or 2"},
      {5, {0x2de1da0, 0x2de1dbc, 0, 0}, {0x06815a48, 0x0682c078, 0, 0}, {0x06815f90, 0x0682c228}, {0x068164d8, 0x0682c3d8}, "no texture; not selected by Water::Startup"}},
     {{0, &kWaterOwn}, {1, &kWaterFrame}}, kWaterPs, {},
     "ocean: POSITION0.xyzw float4 = (world x, world y, u, v) with (u, v) the plot's place in its 896-sample cell's height "
     "page * (1/1024, -1/1024); one quad per visible plot (4 vertices, indices 0,1,2,0,2,3), rebuilt every frame by 0x739ae0 "
     "into the dynamic vertex buffer Water+0x54 (format id 0xe, 16 bytes) and index buffer Water+0x58 (R16), one draw per "
     "cell [C, issue 3602; corrected: 2903 had the two buffers swapped]; river: POSITION.xyz, TEXCOORD0.xy, TEXCOORD1.xy, "
     "vertex buffer River+0x1c (format id 10), index buffer River+0x20",
     "Pass choice [C, issue 3602]: ocean pass = Water+0xe8, set by Water::Startup from the water-quality global [0xd90f68]: "
     "1 -> 2, 2 -> 3, else 0; river pass (0x463640) = 4 if [0xd90f68] is 1 or 2, else 1; pass 5 is not selected. Pass "
     "records (0x8cd6a0 arguments 2-4): ocean (8, 3, 0, \"Water\"), river (8, 4, 0, \"River Visualization\") [C values, I "
     "meaning]. Ocean passes read t0, t1, t4..t11, t13; river passes add t2, t3, t12. Blend, depth and rasterizer "
     "states: not in the draw objects (water-system.md 8, question 6).", C},
    {"WaveShader", 92, "water",
     "coast waves: Wave_System (0x1a0c6c8) render 0x643b60 draws its batches (draw objects built by 0x6a6490, called from "
     "0x67c170; program 0x4bb550, own buffer 0xf80) in a pass \"Waves\" (0x8cd6a0 arguments 4, 10, 0)",
     {{0, {0x2de1dcc, 0x2de1dd0, 0, 0}, {0x0682c580, 0x0682d118, 0, 0}, {0x0682c960, 0x0682d328}, {0x0682cd40, 0x0682d530}, ""}},
     {{0, &kWaveOwn}, {1, &kWaveFrame}}, kWavePs, {},
     "POSITION0.xy (wave centre; stored as float16, 0x6a6490 decodes x, y at a 12-byte stride for the batch bounds), "
     "TEXCOORD0.xyzw (.xy corner in [0, 1] -> offset (2c - 1) * size, .zw uv in the atlas cell), BLENDINDICES0.x (wave index)",
     "A batch is drawn only where its bounds overlap a view's rectangle (0xda1b60..). PS output = (texel * fade * Tint)^(1/2.2), "
     "alpha 1 [C]; the blend state is not in the draw object.", C},
    {"WorldCurrents", 93, "water",
     "\"Water Currents\" 256x256 RGBA8 target 0xda12c4 (TerrainSystem::Startup 0x6b8546, only when [0xd90f68] > 0): "
     "0x53a9f0 (from Water::Startup 0x5f06a0, from 0x444870 via 0x4b2050, and from the debug hook 0x406c90) draws pass 3 "
     "only, once per load, through the draw object Water+0x48 (0x444870) and the program Water+0x50 (0x774420)",
     {{0, {0x2de1de0, 0x2de1de4, 0, 0}, {0x0682d730, 0x0682db50, 0, 0}, {0x0682d890, 0x0682dc98}, {0x0682d9f0, 0x0682dde0}, ""},
      {1, {0x2de1de0, 0x2de1de8, 0, 0}, {0x0682d730, 0x0682df20, 0, 0}, {0x0682d890, 0x0682e530}, {0x0682d9f0, 0x0682eb00}, ""},
      {2, {0x2de1de0, 0x2de1dec, 0, 0}, {0x0682d730, 0x0682f0c8, 0, 0}, {0x0682d890, 0x0682f820}, {0x0682d9f0, 0x0682ff78}, ""},
      {3, {0x2de1de0, 0x2de1df0, 0, 0}, {0x0682d730, 0x068306d0, 0, 0}, {0x0682d890, 0x068313b0}, {0x0682d9f0, 0x06832050},
       "the one the EXE draws: lerp(noise, Sobel flow of t0, min(|gradient|, 1))"}},
     {{1, &kCurrentsFrame}}, kCurrentsPs, {},
     "POSITION0.xyz (passed through as clip space), TEXCOORD0.xy: a full-screen quad of 2 triangles (draw-object buffer "
     "handles 1 / 1)",
     "Passes 0-2 are not drawn by the EXE [C: 0x53a9f0 is the only draw and names pass 3]: pass 0 writes (0.5 - 0.5u, "
     "0.5 + 0.5v, 0, 1), pass 1 the Sobel flow alone, pass 2 the noise alone. Pass record (0x8cd6a0 arguments 2-4): "
     "(0, 9, 0x80, \"WaterCurrent\") [C values, I meaning]. Behaviour: water-system.md section 8.", C},
    {"RiverShader", 69, "river", "program 0x5b0ba0; no draw object of material 0x45 found: rivers draw with WaterShader (0x6b1580)",
     {{0, {0x2de1c38, 0x2de1c3c, 0, 0}, {0x067cfce8, 0x067d0a20, 0, 0}, {0x067d0150, 0x067d0dc8}, {0x067d05b8, 0x067d1168}, ""}},
     {{0, &kRiverOwn}, {1, &kRiverFrame}}, kRiverShaderPs, {}, "per the VS signature", "", I},
    {"DrawSimpleOverlay", 67, "overlay",
     "hex overlays (selection, rings, glow) drawn by 10 systems (draw objects 0x4d0548 .. 0x723699); Overlay_System render "
     "0x6ead50 draws into the \"Terrain Overlay\" target 0xda12a4 (culture borders first, then \"Post Culture Borders Z Clear\") "
     "and fills OverlayFrame [I: borders use this material]",
     {{0, {0x2de1bf0, 0x2de1bf8, 0, 0}, {0x067cb630, 0x067cc8a8, 0, 0}, {0x067cba00, 0x067cca50}, {0x067cbdd0, 0x067ccbf8}, ""},
      {1, {0x2de1bf0, 0x2de1bfc, 0, 0}, {0x067cb630, 0x067ccda0, 0, 0}, {0x067cba00, 0x067ccfa8}, {0x067cbdd0, 0x067cd1a8}, ""},
      {2, {0x2de1bf4, 0x2de1c00, 0, 0}, {0x067cc1a0, 0x067cd3a8, 0, 0}, {0x067cc3f8, 0x067cd508}, {0x067cc650, 0x067cd668}, ""},
      {3, {0x2de1bf0, 0x2de1c04, 0, 0}, {0x067cb630, 0x067cd7c8, 0, 0}, {0x067cba00, 0x067cda10}, {0x067cbdd0, 0x067cdc48}, ""},
      {4, {0x2de1bf0, 0x2de1c08, 0, 0}, {0x067cb630, 0x067cde78, 0, 0}, {0x067cba00, 0x067ce038}, {0x067cbdd0, 0x067ce1f0}, ""},
      {5, {0x2de1bf0, 0x2de1c0c, 0, 0}, {0x067cb630, 0x067ce3a8, 0, 0}, {0x067cba00, 0x067ce508}, {0x067cbdd0, 0x067ce668}, ""}},
     {{0, &kOverlayOwn}, {1, &kOverlayFrame}}, kOverlayPs, {}, "per the VS signature", "", I},
    {"FOWBaseLayer", 53, "fow", "FluidFOW_System (start-up 0x44a570 builds the draw objects): renders the FOW base layer",
     {{0, {0x2de1a5c, 0x2de1a84, 0, 0}, {0x0677c140, 0x0677f210, 0, 0}, {0x0677c5a0, 0x0677f428}, {0x0677ca00, 0x0677f630}, ""}},
     {{0, &kFowBaseLayerOwn}}, kFowLayerPs, {}, "per the VS signature",
     "Three planes after the world, 24 quads per draw (task 3955, fog-of-war.md 7.2 [L]): t0 the plane's cloud "
     "(fogcloudtile_dif_02 / _03 / _01), t1 the FOW mask; blend SRC_ALPHA / INV_SRC_ALPHA, depth LESS_EQUAL without write, "
     "cull FRONT with FrontCounterClockwise; samplers linear wrap. c5r::fow::CloudRenderer draws it.", C},
    {"FOWDrawMask", 52, "fow", "FluidFOW_System (0x44a570): draws the \"FOW Mask\" target 0xda12ac that terrain and water sample",
     {{0, {0x2de1a50, 0x2de1a70, 0, 0}, {0x0677a9f8, 0x0677df08, 0, 0}, {0x0677acc8, 0x0677e058}, {0x0677af98, 0x0677e1a8}, "mask: unexplored plots, hexagon set 1 (1.40x), third draw group [L 3952]"},
      {1, {0x2de1a50, 0x2de1a74, 0, 0}, {0x0677a9f8, 0x0677e2f8, 0, 0}, {0x0677acc8, 0x0677e448}, {0x0677af98, 0x0677e598}, "mask: visible plots, set 0 (1.33x), second group [L 3952]"},
      {2, {0x2de1a50, 0x2de1a78, 0, 0}, {0x0677a9f8, 0x0677e6e8, 0, 0}, {0x0677acc8, 0x0677e838}, {0x0677af98, 0x0677e988}, "mask: seen plots, set 0 (1.33x), first group [L 3952]"},
      {3, {0x2de1a54, 0x2de1a7c, 0, 0}, {0x0677b260, 0x0677ead8, 0, 0}, {0x0677b480, 0x0677ebb0}, {0x0677b6a0, 0x0677ec88}, "FOW CULL (depth only, fog-of-war.md 3.5)"},
      {4, {0x2de1a58, 0x2de1a80, 0, 0}, {0x0677b8b8, 0x0677ed60, 0, 0}, {0x0677bb90, 0x0677eef8}, {0x0677be68, 0x0677f088}, "mask visualiser (no caller found)"}},
     {{0, &kFowDrawMaskOwn}}, kFowLayerPs, {},
     "passes 0-2 [L 3952]: POSITION float3 (cube corner), NORMAL float3 (hexagon slot 0..23), COLOR0, COLOR1 rgba8 (0), TEXCOORD0 float2 (weight 1 inner, 0 outer); stride 40; R32 indices, 48 a hexagon",
     "passes 0-2: blend ONE/ONE MAX (colour and alpha), depth off, cull none; no texture bound (kFowLayerPs is pass 4's t1)", C},
    {"FOWParticleShader", 51, "fow", "draw object 0x68f470 [I: FOW cloud particles]",
     {{0, {0x2de1a48, 0x2de1a68, 0, 0}, {0x067796f0, 0x0677d4b0, 0, 0}, {0x06779a10, 0x0677d6e8}, {0x06779d30, 0x0677d910}, ""},
      {1, {0x2de1a4c, 0x2de1a6c, 0, 0}, {0x0677a050, 0x0677db38, 0, 0}, {0x0677a388, 0x0677dc80}, {0x0677a6c0, 0x0677ddc8}, ""}},
     {{0, &kFowParticleOwn}}, kFowParticlePs, {}, "per the VS signature", "", I},
    {"FOWCell", 50, "fow", "FOW_System (0xdaaa20) start-up 0x5bb6e0 builds the draw objects; render 0x55b8b0",
     {{0, {0x2de1a40, 0x2de1a60, 0, 0}, {0x067789d8, 0x0677ce60, 0, 0}, {0x06778bf8, 0x0677cf70}, {0x06778e18, 0x0677d080}, ""},
      {1, {0x2de1a44, 0x2de1a64, 0, 0}, {0x06779038, 0x0677d188, 0, 0}, {0x06779278, 0x0677d298}, {0x067794b8, 0x0677d3a8}, ""}},
     {{0, &kFowCellOwn}}, {}, {}, "per the VS signature",
     "The DisableSystems FOW key changes nothing visible (disable-systems-and-masks.md); FluidFOW is the visible fog.", C},
    {"IceShader", 59, "terrain", "polar ice (cvTerrainIce); draw object 0x4956f0",
     {{0, {0x2de1af8, 0x2de1afc, 0, 0}, {0x067a07e8, 0x067a1688, 0, 0}, {0x067a0cc8, 0x067a3900}, {0x067a11a8, 0x067a5af8}, ""}},
     {{1, &kIceFrame}}, kIcePs, {}, "per the VS signature", "", I},
    {"GridShader", 58, "overlay", "hex grid; draw object 0x6ab930",
     {{0, {0x2de1adc, 0x2de1ae4, 0, 0}, {0x0679dfd0, 0x0679f8e8, 0, 0}, {0x0679e358, 0x0679faf0}, {0x0679e6e0, 0x0679fce8}, ""},
      {1, {0x2de1adc, 0x2de1ae8, 0, 0}, {0x0679dfd0, 0x0679fed8, 0, 0}, {0x0679e358, 0x067a01e8}, {0x0679e6e0, 0x067a04e8}, ""},
      {2, {0x2de1ae0, 0x2de1ae4, 0, 0}, {0x0679ea68, 0x0679f8e8, 0, 0}, {0x0679ef40, 0x0679faf0}, {0x0679f418, 0x0679fce8}, ""}},
     {{0, &kGridOwn}, {1, &kGridFrame}}, kGridPs, {}, "per the VS signature", "", I},
};

// The 13 sampler states of 0x8be700 (renderer+0x30564 + id*8; +0x30568 = second-view flag). Filter / address values
// recovered by emulating the init (evidence/2900/2903/samplers.py); MaxLOD = FLT_MAX, MinLOD 0, bias 0 for all.
const std::vector<SamplerStateDesc> kSamplers = {
    {0, 0x15, {1, 1, 1}, 0, 1, false, C},  // linear, wrap
    {1, 0x14, {1, 1, 1}, 0, 1, false, C},  // linear, point mip, wrap
    {2, 0x15, {1, 1, 1}, 0, 1, true, C},
    {3, 0x15, {3, 3, 3}, 0, 1, true, C},   // linear, clamp
    {4, 0x14, {3, 3, 3}, 0, 1, true, C},
    {5, 0x15, {3, 3, 3}, 0, 1, false, C},  // linear, clamp
    {6, 0x15, {1, 1, 1}, 8, 1, false, C},  // MaxAnisotropy 8 with a linear filter (as written)
    {7, 0x15, {1, 1, 1}, 8, 1, true, C},
    {8, 0x15, {3, 3, 3}, 8, 1, false, C},
    {9, 0x00, {1, 1, 1}, 0, 1, false, C},  // point, wrap
    {10, 0x00, {3, 3, 3}, 0, 1, false, C}, // point, clamp
    {11, 0x00, {3, 3, 3}, 0, 1, true, C},
    {12, 0x95, {3, 3, 3}, 0, 4, false, C}, // comparison linear, clamp, LESS_EQUAL
};

} // namespace

const std::vector<ShaderSetDesc>& allSets() { return kSets; }

const ShaderSetDesc* findSet(const char* name) {
    if (!name) return nullptr;
    for (const ShaderSetDesc& s : kSets)
        if (std::strcmp(s.name, name) == 0) return &s;
    return nullptr;
}

const std::vector<SamplerStateDesc>& samplerStates() { return kSamplers; }

const SamplerStateDesc* samplerState(uint32_t id) {
    for (const SamplerStateDesc& s : kSamplers)
        if (s.id == id) return &s;
    return nullptr;
}

}} // namespace c5r::terrainsets
