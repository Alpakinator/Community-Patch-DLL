// c5r/shaders/reflect.h - reflection of a DXBC shader by its own SM4/SM5 token decoder (task 2902, epic 2900).
//
// Why not D3DReflect alone: the EXE's blobs are stripped to ISGN + OSGN (+ PCSG) + SHDR/SHEX, with no RDEF and no STAT
// chunk, so D3DReflect reports 0 constant buffers and 0 bound resources for every one of them. The declarations
// (constant-buffer slots and sizes, resource slots and dimensions, samplers, inputs and outputs with system values,
// temps, tessellation state) and the constant-buffer registers the code reads are all in the token stream, which
// this decoder reads. Its declaration lines and constant-buffer uses are cross-checked against D3DDisassemble for every
// blob in the EXE (c5r/shaders/d3dcheck.h, shaderdump --check).
//
// Errors of reflect() (negative tests in tests/shaders/reflect_test.cpp): container errors as parseDxbc; no SHDR/SHEX
// chunk: NotFound; a signature chunk whose element table does not fit: Truncated; a semantic name offset outside the
// chunk or an unterminated name: BadLayout; an instruction of length 0, an instruction or operand running past the
// end of its instruction or of the program: BadLayout; an operand with an N-component (3) count or an index
// representation > 4: Unsupported.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/status.h"
#include "c5r/shaders/dxbc.h"

namespace c5r { namespace shaders {

// ---- signatures (ISGN/OSGN/PCSG: 24-byte elements; ISG1/OSG1/PSG1: 32; OSG5: 28) ----
struct SigElement {
    std::string semantic;
    uint32_t semanticIndex = 0;
    uint32_t systemValue = 0;    // D3D_NAME
    uint32_t componentType = 0;  // D3D_REGISTER_COMPONENT_TYPE: 1 uint32, 2 sint32, 3 float32
    uint32_t reg = 0;
    uint8_t mask = 0;            // components present
    uint8_t rwMask = 0;          // input: components read by the shader; output: components NOT written (inverted)
    uint32_t stream = 0;
    uint32_t minPrecision = 0;
};
struct Signature {
    uint32_t fourcc = 0;         // 0 when the chunk is absent
    std::vector<SigElement> elems;
};

// ---- declarations from the token stream ----
struct CbDecl { uint32_t slot = 0, vec4Count = 0; bool dynamicIndexed = false; };
struct ResourceDecl {
    uint32_t opcode = 0;         // 88 dcl_resource, 161 raw, 162 structured, 156/157/158 UAV typed/raw/structured
    bool uav = false;
    uint32_t slot = 0;
    uint32_t dimension = 0;      // D3D10_SB_RESOURCE_DIMENSION: 1 buffer, 2 tex1d, 3 tex2d, 4 tex2dms, 5 tex3d,
                                 // 6 cube, 7 tex1darray, 8 tex2darray, 9 tex2dmsarray, 10 cubearray, 11 raw, 12 structured
    uint32_t sampleCount = 0;    // for the ms dimensions
    uint8_t returnType[4] = {};  // D3D10_SB_RESOURCE_RETURN_TYPE per component: 1 unorm 2 snorm 3 sint 4 uint 5 float 6 mixed
    uint32_t stride = 0;         // structured
    uint32_t flags = 0;          // UAV: bit 0 globally coherent, bit 1 has counter
};
struct SamplerDecl { uint32_t slot = 0; uint32_t mode = 0; /* 0 default, 1 comparison, 2 mono */ };
struct IoDecl {
    uint32_t opcode = 0;         // 95..103 dcl_input*/dcl_output*
    uint32_t operandType = 0;    // D3D10_SB_OPERAND_TYPE (1 input, 2 output, 12 oDepth, 25 vicp, ...)
    uint32_t indexDim = 0;
    uint32_t index[2] = {};
    uint8_t mask = 0;            // 0 when the operand has fewer than 4 components
    uint32_t systemValue = 0;    // D3D10_SB_NAME for the *_sgv/_siv forms, else 0
    uint32_t interpolation = 0;  // dcl_input_ps*: D3D10_SB_INTERPOLATION_MODE
};
struct IndexableTemp { uint32_t reg = 0, count = 0, components = 0; };
struct TessDecls {
    uint32_t domain = 0;         // 1 isoline, 2 tri, 3 quad
    uint32_t partitioning = 0;   // 1 integer, 2 pow2, 3 fractional_odd, 4 fractional_even
    uint32_t outputPrimitive = 0;// 1 point, 2 line, 3 triangle_cw, 4 triangle_ccw
    uint32_t inputControlPoints = 0, outputControlPoints = 0;
    float maxTessFactor = 0;     // 0 when not declared
    std::vector<uint32_t> forkInstanceCounts, joinInstanceCounts;
    uint32_t forkPhases = 0, joinPhases = 0;
};
struct GsDecls { uint32_t inputPrimitive = 0, outputTopology = 0, maxOutputVertices = 0, instanceCount = 0; };

// ---- constant-buffer registers the code reads ----
struct CbRegUse {
    uint32_t reg = 0;
    uint8_t swizzleMask = 0;     // components named by the source swizzle/select/mask (what the token says)
    uint8_t usedMask = 0;        // narrowed by the destination mask for component-wise ops and dp2/dp3 (an upper
                                 // bound of the components that affect the result)
    uint32_t reads = 0;
};
struct CbUse {
    uint32_t slot = 0;
    std::vector<CbRegUse> regs;  // immediately indexed reads, sorted by register
    std::vector<uint32_t> dynamicBases; // the immediate part of dynamically indexed reads (cb[rN.x + base]), sorted
    uint8_t dynamicSwizzleMask = 0;
    uint32_t reads = 0;          // every operand that reads this buffer
    uint32_t maxImmediateReg() const { return regs.empty() ? 0 : regs.back().reg; }
};

// One declaration rendered in D3DDisassemble's syntax (for the cross-check), in token order. The immediate
// constant buffer is rendered "dcl_immediateConstantBuffer [N]" (N vec4 rows).
struct ShaderReflection {
    Stage stage = Stage::Unknown;
    uint8_t major = 0, minor = 0;
    std::string chunks;          // "ISGN+OSGN+SHEX"
    Signature input, output, patchConstant;
    std::vector<CbDecl> cbuffers;
    std::vector<ResourceDecl> resources; // t# and u#
    std::vector<SamplerDecl> samplers;
    std::vector<IoDecl> inputs, outputs;
    uint32_t temps = 0;          // the largest dcl_temps (HS phases each declare their own)
    std::vector<IndexableTemp> indexableTemps;
    uint32_t globalFlags = 0;
    uint32_t immediateCbVec4 = 0;
    TessDecls tess;
    GsDecls gs;
    uint32_t threadGroup[3] = {};
    uint32_t instructionCount = 0; // executable instructions: every token that is not a declaration, customdata or
                                   // an hs_decls / hs_*_phase marker
    std::vector<CbUse> cbUse;      // sorted by slot; only buffers the code reads
    std::vector<std::string> declLines;
    const CbDecl* findCb(uint32_t slot) const;
    const CbUse* findCbUse(uint32_t slot) const;
};

Status reflect(const uint8_t* blob, size_t size, ShaderReflection& out);

// Names used in the D3DDisassemble syntax (also handy in CSV/JSON output).
const char* sysValueName(uint32_t d3d10sbName);     // "position", "vertex_id", ...; "" for 0
const char* resourceDimName(uint32_t dim);          // "texture2d", "texturecube", ...
const char* componentTypeName(uint32_t t);          // signature component type: "uint", "sint", "float"
std::string maskStr(uint8_t mask);                  // "xyzw" subset
std::string sigSummary(const Signature& s);         // "POSITION0@v0:xyzw NORMAL0@v1:xyz SV_Position0@v0:xyzw:pos"

}} // namespace c5r::shaders
