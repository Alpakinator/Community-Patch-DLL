// c5r/shaders/d3dcheck.h - the official d3dcompiler's view of a blob (D3DDisassemble, D3DReflect) and the cross-check of
// this library's decoder against it (task 2902, epic 2900). Windows SDK d3dcompiler only (d3dcompiler_47.dll).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "c5r/status.h"
#include "c5r/shaders/reflect.h"

namespace c5r { namespace shaders {

// D3DDisassemble(blob, 0 flags). Device on a failed HRESULT.
Status d3dDisassemble(const uint8_t* blob, size_t size, std::string& text);

// What D3DReflect returns (ID3D11ShaderReflection::GetDesc). For the EXE's stripped blobs: S_OK, 0 constant buffers,
// 0 bound resources, the signature counts, 0 instructions (no STAT chunk).
struct D3DReflectInfo {
    long hr = 0;
    uint32_t version = 0, constantBuffers = 0, boundResources = 0, inputParameters = 0, outputParameters = 0,
             patchConstantParameters = 0, instructionCount = 0, tempRegisterCount = 0;
};
Status d3dReflectInfo(const uint8_t* blob, size_t size, D3DReflectInfo& out);

// The facts the cross-check reads out of D3DDisassemble text: the declaration lines (whitespace normalised; the
// immediate constant buffer as "dcl_immediateConstantBuffer [N]"), the executable instruction count, and every
// constant-buffer operand ("cb1[4].xyz" -> reads, components named; "cb0[r+5]" for dynamic reads).
struct DisasmFacts {
    std::vector<std::string> declLines;
    uint32_t instructionCount = 0;
    std::map<std::string, uint32_t> cbReads;   // key "cb<slot>[<reg>]" or "cb<slot>[r+<base>]" -> number of reads
    std::map<std::string, uint8_t> cbMasks;    // same keys -> union of the components named
};
DisasmFacts parseDisassembly(const std::string& text);
DisasmFacts factsOf(const ShaderReflection& r); // the same facts from this library's decoder

// Compares the decoder with the disassembly. Returns the number of differences; each is described in diffs.
size_t compareWithDisassembly(const ShaderReflection& r, const std::string& text, std::vector<std::string>& diffs);

}} // namespace c5r::shaders
