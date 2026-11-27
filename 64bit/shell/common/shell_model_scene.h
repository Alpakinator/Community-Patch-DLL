// shell_model_scene.h - the EXE's BuildingShader programs as the compositor's model filter needs them (epic 0561,
// ticket 4874; research/05-graphics-composition/city-renderer.md 3.1, 3.2 and 10). Written 2026-11-18 for this repository
// (GPL-3.0-or-later). Plain C data, no render header: shell_composite.dll (x86) and the server (x64) include it alike.
//
// The programs: the DXBC containers' SHA-256 digests (D83: digests, never bytes), from the 3910 capture's shader files
// and equal to the blobs of build 4390913 at exeVa (ticket 4874, P5 offline, city-renderer.md 10.1 [C]). Materials and
// passes: 4870's table (`render/evidence/0561/4870/programs.csv`, `filter_table_draft.csv`).
//
// The classes (4870, P1): the rigid classes split by VS - city blocks material 5 (VS 7ec4e9a5 with PS c704c80b), single
// models material 3 (VS c4bb25da; wonders and display buildings: cities), instanced single models material 4 (VS
// 7ec4e9a5 with PS f0e1f0a6: landmarks); only the skinned class (material 6, VS 7a9e17f6) serves both bits, so its bit
// comes from the server's identity set (shell_model_tap.h).
//
// DROP: the colour-pass (pass 0) pairs, dropped only into a target with a depth view (the scene and the water reflection;
// the shadow mask 13383 has none: 4870's 34 colour-program draws into the mask are kept by that test). KEEP: the mask
// pass (pass 1), recognised and always passed (the ShadowShader, terrain and water read the mask: 4870 P2).
// shell/tests/test_model_programs_table.py checks this file against 4870's committed table (digest prefixes, stages,
// VAs, materials, passes, pairs and actions) and that a mutated copy is caught. The server's program table (4873) is to
// include this file or carry the same check.
#pragma once
#include <stdint.h>

#define VPMODEL_STAGE_VS 1u
#define VPMODEL_STAGE_PS 2u

// materials as bits of VPModelProgram::materials
#define VPMODEL_MAT1 1u    /* BuildingShader */
#define VPMODEL_MAT3 2u    /* BuildingShader_CODE (single models: wonders, display buildings) */
#define VPMODEL_MAT4 4u    /* BuildingShader_CODE_Instanced (landmarks) */
#define VPMODEL_MAT5 8u    /* BuildingShader_CODE_Instanced_Opaque (city blocks) */
#define VPMODEL_MAT6 16u   /* BuildingShader_Skinned_CODE (animated parts) */

// the classes of a dropped draw (VPModelTapDraw::cls)
#define VPMODEL_CLASS_BLOCK 0u     /* material 5: instanced, slot 1 stride 64, 32-byte vertices; bit cities */
#define VPMODEL_CLASS_SINGLE 1u    /* material 3: one draw per mesh, world rows in VS cb0 rows 0-3; bit cities */
#define VPMODEL_CLASS_INST 2u      /* material 4: instanced, slot 1 stride 64, 20-byte vertices; bit landmarks */
#define VPMODEL_CLASS_SKINNED 3u   /* material 6: slot 1 R32 ids (stride 4), palette VS t27; bit from the identity set */
#define VPMODEL_CLASSES 4u
#define VPMODEL_CLASS_NONE 0xFFu

typedef struct VPModelProgram
{
	uint8_t sha256[32];   // digest of the DXBC container
	uint32_t exeVa;       // build 4390913: the blob's VA (path 2 checks the program's create site, unit_filter.cpp)
	uint8_t stage;        // VPMODEL_STAGE_*
	uint8_t materials;    // VPMODEL_MAT* bits of the materials that use it in this pass
	uint8_t pass;         // 0 = colour (scene and reflection), 1 = shadow mask
	uint8_t pad;
} VPModelProgram;

typedef struct VPModelPair
{
	uint8_t cls;          // VPMODEL_CLASS_*
	uint8_t vs;           // index into kVPModelPrograms
	uint8_t ps;
	uint8_t bit;          // VPMODE_CITIES (2), VPMODE_LANDMARKS (4), or 0: from the identity set (skinned)
} VPModelPair;

#define VPMODEL_PROGRAMS 10
#define VPMODEL_DROP_PROGRAMS 5   /* [0..4] pass 0; [5..9] pass 1 (KEEP) */
static const VPModelProgram kVPModelPrograms[VPMODEL_PROGRAMS] = {
	// [0] VS 7ec4e9a5, materials 4 and 5, pass 0
	{ { 0x7e,0xc4,0xe9,0xa5,0xaa,0xe0,0xc3,0x95,0x45,0x9f,0x39,0xd1,0xc4,0x8f,0x7b,0xcd,
	    0x63,0xbf,0xa3,0x44,0xac,0x2b,0x54,0x7d,0x9e,0x42,0x61,0x19,0x14,0x6c,0x60,0xa3 }, 0x619F1E8u, VPMODEL_STAGE_VS, VPMODEL_MAT4 | VPMODEL_MAT5, 0, 0 },
	// [1] PS c704c80b, material 5, pass 0
	{ { 0xc7,0x04,0xc8,0x0b,0x3b,0x53,0x95,0x51,0x05,0x62,0xe4,0x3b,0x4b,0x1e,0x6d,0xd8,
	    0x25,0x03,0xbb,0xfe,0x95,0x55,0x4f,0x8b,0x09,0x07,0x57,0xa7,0xec,0x0d,0x0f,0x38 }, 0x61A7D00u, VPMODEL_STAGE_PS, VPMODEL_MAT5, 0, 0 },
	// [2] PS f0e1f0a6, materials 3, 4 and 6, pass 0
	{ { 0xf0,0xe1,0xf0,0xa6,0x6c,0xdf,0x5f,0xe1,0x41,0xb7,0x6d,0x98,0x63,0x4f,0xbd,0xaf,
	    0xa6,0x2f,0x17,0xcf,0x8a,0x36,0xd4,0x36,0x9c,0x4c,0x0c,0xdc,0xe2,0x25,0x63,0xca }, 0x61A56E8u, VPMODEL_STAGE_PS, VPMODEL_MAT3 | VPMODEL_MAT4 | VPMODEL_MAT6, 0, 0 },
	// [3] VS c4bb25da, materials 1 and 3, pass 0
	{ { 0xc4,0xbb,0x25,0xda,0x57,0xa8,0xd0,0x31,0x97,0xc9,0x20,0xd5,0xec,0x3b,0xe0,0x1d,
	    0xa0,0x81,0x10,0x8a,0x72,0x37,0x90,0x07,0xdc,0x35,0x47,0x21,0x44,0x9f,0xac,0xda }, 0x619BE88u, VPMODEL_STAGE_VS, VPMODEL_MAT1 | VPMODEL_MAT3, 0, 0 },
	// [4] VS 7a9e17f6, material 6, pass 0
	{ { 0x7a,0x9e,0x17,0xf6,0x68,0x7a,0x6e,0x3c,0x02,0x8f,0x37,0x1d,0xee,0x53,0x55,0x02,
	    0x10,0xab,0x3b,0x2e,0x44,0xac,0x4d,0xf3,0xee,0x78,0xfe,0x2d,0xab,0xf1,0x0b,0x1f }, 0x61A0CB0u, VPMODEL_STAGE_VS, VPMODEL_MAT6, 0, 0 },
	// [5] VS 88c2b7d4, materials 4 and 5, pass 1 (mask)
	{ { 0x88,0xc2,0xb7,0xd4,0x05,0xff,0xd3,0xd1,0xd6,0x23,0x74,0x92,0x99,0x6b,0xbe,0xfe,
	    0xa2,0xa6,0x42,0x89,0xbb,0x84,0xc3,0xbe,0x66,0x72,0xc3,0x9b,0x1b,0x2b,0x26,0x92 }, 0x61A0320u, VPMODEL_STAGE_VS, VPMODEL_MAT4 | VPMODEL_MAT5, 1, 0 },
	// [6] PS 73daa800, material 5, pass 1 (mask)
	{ { 0x73,0xda,0xa8,0x00,0xe7,0x4b,0xb5,0xf6,0xf5,0x62,0x94,0x19,0x37,0xed,0xb8,0x9b,
	    0xde,0xf8,0x72,0x5b,0xb6,0x9f,0xd0,0x55,0x02,0x93,0xbf,0xdb,0x51,0x42,0xec,0xbb }, 0x61A9C98u, VPMODEL_STAGE_PS, VPMODEL_MAT5, 1, 0 },
	// [7] PS e12e9eaf, materials 3, 4 and 6, pass 1 (mask)
	{ { 0xe1,0x2e,0x9e,0xaf,0x3a,0xeb,0xf0,0x79,0xe5,0x4c,0xc2,0xfb,0x47,0x0e,0xee,0x76,
	    0x09,0x67,0x92,0x21,0x7d,0x89,0xe3,0x71,0x48,0x8c,0xed,0xfa,0x07,0x10,0xc8,0xc1 }, 0x61A76F8u, VPMODEL_STAGE_PS, VPMODEL_MAT3 | VPMODEL_MAT4 | VPMODEL_MAT6, 1, 0 },
	// [8] VS 0a3f2230, material 3, pass 1 (mask)
	{ { 0x0a,0x3f,0x22,0x30,0x27,0x43,0x25,0x5d,0xf3,0x76,0xee,0x56,0x6a,0xdf,0x5d,0x2a,
	    0x8c,0x85,0x7d,0x16,0x16,0x85,0xe3,0xf9,0x77,0x33,0x82,0xea,0xb2,0x95,0x7a,0x08 }, 0x619E9B0u, VPMODEL_STAGE_VS, VPMODEL_MAT3, 1, 0 },
	// [9] VS 34b98e63, material 6, pass 1 (mask)
	{ { 0x34,0xb9,0x8e,0x63,0x1a,0xc1,0x90,0xef,0xf6,0x03,0x82,0x75,0x64,0x49,0x42,0xb7,
	    0x5d,0xb3,0xd6,0x67,0x31,0x86,0x38,0x6d,0x94,0xdb,0x47,0x17,0xe8,0x90,0xef,0x96 }, 0x61A2BB8u, VPMODEL_STAGE_VS, VPMODEL_MAT6, 1, 0 },
};

// The DROP pairs (colour pass) by class; a draw of a pair is dropped only under the rule of city-renderer.md 3.2 / 10.3.
#define VPMODEL_DROP_PAIRS 4
static const VPModelPair kVPModelDropPairs[VPMODEL_DROP_PAIRS] = {
	{ VPMODEL_CLASS_BLOCK, 0, 1, 0x2u },     // material 5: cities
	{ VPMODEL_CLASS_SINGLE, 3, 2, 0x2u },    // material 3: cities (wonders, display buildings)
	{ VPMODEL_CLASS_INST, 0, 2, 0x4u },      // material 4: landmarks
	{ VPMODEL_CLASS_SKINNED, 4, 2, 0u },     // material 6: cities or landmarks, by the identity set
};
