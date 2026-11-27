// SM4/SM5 token-stream decoder and signature reader (task 2902). See c5r/shaders/reflect.h.
//
// The token format (D3D10/D3D11 "tokenized program format"): version token, length token, then instructions. An
// opcode token holds the opcode in bits 0-10, the instruction length in DWORDs in bits 24-30 (customdata: in the next
// DWORD) and an "extended" flag in bit 31 (further opcode tokens follow while their bit 31 is set). Operand tokens
// hold the component count (bits 0-1), the selection mode and mask/swizzle/select (2-3, 4-11), the operand type
// (12-19), the index dimension (20-21), the representation of each index (22-24, 25-27, 28-30) and an extended
// flag (31). Written from the public description of the format; the constants are the D3D10_SB_* / D3D11_SB_* values.
#include "c5r/shaders/reflect.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "c5r/binreader.h"

namespace c5r { namespace shaders {

namespace {

// Opcodes used by name here.
enum : uint32_t {
    OP_DP2 = 15, OP_DP3 = 16, OP_DP4 = 17, OP_CUSTOMDATA = 53, OP_IMUL = 38, OP_SINCOS = 77, OP_UDIV = 78,
    OP_UMUL = 81, OP_INTERFACE_CALL = 120, OP_UADDC = 132, OP_USUBB = 133, OP_SWAPC = 142,
    OP_DCL_RESOURCE = 88, OP_DCL_CONSTANT_BUFFER = 89, OP_DCL_SAMPLER = 90, OP_DCL_INDEX_RANGE = 91,
    OP_DCL_GS_OUTPUT_TOPOLOGY = 92, OP_DCL_GS_INPUT_PRIMITIVE = 93, OP_DCL_MAX_OUTPUT_VERTEX_COUNT = 94,
    OP_DCL_INPUT = 95, OP_DCL_INPUT_SGV = 96, OP_DCL_INPUT_SIV = 97, OP_DCL_INPUT_PS = 98, OP_DCL_INPUT_PS_SGV = 99,
    OP_DCL_INPUT_PS_SIV = 100, OP_DCL_OUTPUT = 101, OP_DCL_OUTPUT_SGV = 102, OP_DCL_OUTPUT_SIV = 103,
    OP_DCL_TEMPS = 104, OP_DCL_INDEXABLE_TEMP = 105, OP_DCL_GLOBAL_FLAGS = 106,
    OP_HS_DECLS = 113, OP_HS_CONTROL_POINT_PHASE = 114, OP_HS_FORK_PHASE = 115, OP_HS_JOIN_PHASE = 116,
    OP_DCL_STREAM = 143, OP_DCL_FUNCTION_BODY = 144, OP_DCL_FUNCTION_TABLE = 145, OP_DCL_INTERFACE = 146,
    OP_DCL_INPUT_CP_COUNT = 147, OP_DCL_OUTPUT_CP_COUNT = 148, OP_DCL_TESS_DOMAIN = 149,
    OP_DCL_TESS_PARTITIONING = 150, OP_DCL_TESS_OUTPUT_PRIMITIVE = 151, OP_DCL_HS_MAX_TESSFACTOR = 152,
    OP_DCL_HS_FORK_INSTANCE_COUNT = 153, OP_DCL_HS_JOIN_INSTANCE_COUNT = 154, OP_DCL_THREAD_GROUP = 155,
    OP_DCL_UAV_TYPED = 156, OP_DCL_UAV_RAW = 157, OP_DCL_UAV_STRUCTURED = 158, OP_DCL_TGSM_RAW = 159,
    OP_DCL_TGSM_STRUCTURED = 160, OP_DCL_RESOURCE_RAW = 161, OP_DCL_RESOURCE_STRUCTURED = 162,
    OP_DCL_GS_INSTANCE_COUNT = 206,
};
// Operand types used by name.
enum : uint32_t { OT_IMM32 = 4, OT_IMM64 = 5, OT_CONSTANT_BUFFER = 8, OT_NULL = 13 };

bool isDeclOrMarker(uint32_t op) {
    return (op >= 88 && op <= 106) || (op >= 113 && op <= 116) || (op >= 143 && op <= 162) || op == 206 ||
           op == OP_CUSTOMDATA;
}

// Component-wise ops: each destination component depends only on the same component of each source.
bool isComponentWise(uint32_t op) {
    static const uint32_t ops[] = {0,  1,  11, 12, 14, 24, 25, 26, 27, 28, 29, 30, 32, 33, 34, 35, 36, 37, 38,
                                   39, 40, 41, 42, 43, 47, 49, 50, 51, 52, 54, 55, 56, 57, 59, 60, 64, 65, 66,
                                   67, 68, 75, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 122, 123, 124, 125,
                                   129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142};
    for (uint32_t o : ops)
        if (o == op) return true;
    return false;
}
bool hasTwoDests(uint32_t op) {
    return op == OP_SINCOS || op == OP_UDIV || op == OP_IMUL || op == OP_UMUL || op == OP_UADDC || op == OP_USUBB ||
           op == OP_SWAPC;
}

struct Index {
    uint32_t rep = 0;
    uint32_t imm = 0;
    bool relative = false;
};
struct Operand {
    uint32_t type = 0;
    uint32_t numComp = 0;   // 0, 1 or 4
    uint32_t selMode = 0;   // 0 mask, 1 swizzle, 2 select1
    uint8_t mask = 0;
    uint8_t swz[4] = {0, 1, 2, 3};
    uint32_t indexDim = 0;
    Index idx[3];
    uint8_t readMask() const { // components named by the operand
        if (numComp == 1) return 1;
        if (numComp != 4) return 0;
        if (selMode == 0) return mask ? mask : 0xF;
        if (selMode == 1) return static_cast<uint8_t>((1 << swz[0]) | (1 << swz[1]) | (1 << swz[2]) | (1 << swz[3]));
        return static_cast<uint8_t>(1 << swz[0]);
    }
    uint8_t writeMask() const {
        if (numComp == 4 && selMode == 0) return mask ? mask : 0xF;
        if (numComp == 1) return 1;
        return 0xF;
    }
};

Status layout(const std::string& m) { return Status::fail(Err::BadLayout, m); }

Status readOperand(const uint32_t* t, uint32_t& p, uint32_t end, Operand& o, int depth) {
    if (depth > 3) return layout("relative addressing nested too deeply");
    if (p >= end) return layout("operand past the end of its instruction");
    const uint32_t tok = t[p++];
    const uint32_t nc = tok & 3;
    if (nc == 3) return Status::fail(Err::Unsupported, "operand with N components");
    o.numComp = nc == 0 ? 0 : nc == 1 ? 1 : 4;
    if (nc == 2) {
        o.selMode = (tok >> 2) & 3;
        if (o.selMode == 0) o.mask = static_cast<uint8_t>((tok >> 4) & 0xF);
        else if (o.selMode == 1)
            for (int i = 0; i < 4; ++i) o.swz[i] = static_cast<uint8_t>((tok >> (4 + 2 * i)) & 3);
        else if (o.selMode == 2) o.swz[0] = static_cast<uint8_t>((tok >> 4) & 3);
        else return Status::fail(Err::Unsupported, "operand selection mode 3");
    }
    o.type = (tok >> 12) & 0xFF;
    o.indexDim = (tok >> 20) & 3;
    bool ext = (tok >> 31) != 0;
    while (ext) {
        if (p >= end) return layout("extended operand token past the end of its instruction");
        ext = (t[p++] >> 31) != 0;
    }
    if (o.type == OT_IMM32 || o.type == OT_IMM64) {
        uint32_t k = o.numComp == 1 ? 1u : o.numComp == 4 ? 4u : 0u;
        if (o.type == OT_IMM64) k *= 2;
        if (end - p < k) return layout("immediate operand past the end of its instruction");
        p += k;
    }
    for (uint32_t i = 0; i < o.indexDim; ++i) {
        Index& ix = o.idx[i];
        ix.rep = (tok >> (22 + 3 * i)) & 7;
        if (ix.rep > 4) return Status::fail(Err::Unsupported, "index representation " + std::to_string(ix.rep));
        if (ix.rep == 0 || ix.rep == 3) {
            if (p >= end) return layout("operand index past the end of its instruction");
            ix.imm = t[p++];
        } else if (ix.rep == 1 || ix.rep == 4) {
            if (end - p < 2) return layout("64-bit operand index past the end of its instruction");
            ix.imm = t[p];
            p += 2;
        }
        if (ix.rep >= 2 && ix.rep != 1) {
            ix.relative = true;
            Operand rel;
            C5R_TRY(readOperand(t, p, end, rel, depth + 1));
        }
    }
    return Status::Ok();
}

const char* operandPrefix(uint32_t type, bool inDcl) {
    switch (type) {
    case 0: return "r";
    case 1: return "v";
    case 2: return "o";
    case 3: return "x";
    case 6: return "s";
    case 7: return "t";
    case 8: return inDcl ? "CB" : "cb";
    case 9: return "icb";
    case 11: return "vPrim";
    case 12: return "oDepth";
    case 13: return "null";
    case 15: return "oMask";
    case 16: return "m";
    case 22: return "vOutputControlPointID";
    case 23: return "vForkInstanceID";
    case 24: return "vJoinInstanceID";
    case 25: return "vicp";
    case 26: return "vocp";
    case 27: return "vpc";
    case 28: return "vDomain";
    case 30: return "u";
    case 31: return "g";
    case 32: return "vThreadID";
    case 33: return "vThreadGroupID";
    case 34: return "vThreadIDInGroup";
    case 35: return "vCoverage";
    case 36: return "vThreadIDInGroupFlattened";
    case 37: return "vGSInstanceID";
    case 38: return "oDepthGE";
    case 39: return "oDepthLE";
    case 40: return "vCycleCounter";
    default: return "?";
    }
}

std::string renderOperand(const Operand& o) {
    std::string s = operandPrefix(o.type, true);
    auto ix = [](const Index& i) { return i.relative ? std::string("?") : std::to_string(i.imm); };
    if (o.indexDim == 1) s += ix(o.idx[0]);
    else if (o.indexDim >= 2) {
        // 2-D operands print as v[a][b] for inputs, CB0[n] etc. for the others
        if (o.type == 1 || o.type == 25 || o.type == 26 || o.type == 2) s += "[" + ix(o.idx[0]) + "][" + ix(o.idx[1]) + "]";
        else s += ix(o.idx[0]) + "[" + ix(o.idx[1]) + "]";
        if (o.indexDim == 3) s += "[" + ix(o.idx[2]) + "]";
    }
    if (o.numComp == 4) {
        static const char xyzw[] = "xyzw";
        if (o.selMode == 0) { if (o.mask) s += "." + maskStr(o.mask); }
        else if (o.selMode == 1) { s += "."; for (int i = 0; i < 4; ++i) s += xyzw[o.swz[i]]; }
        else { s += "."; s += xyzw[o.swz[0]]; }
    }
    return s;
}

const char* returnTypeName(uint32_t r) {
    static const char* n[] = {"?", "unorm", "snorm", "sint", "uint", "float", "mixed", "double", "continued", "unused"};
    return r < 10 ? n[r] : "?";
}
const char* interpName(uint32_t m) {
    static const char* n[] = {"undefined", "constant", "linear", "linear centroid", "linear noperspective",
                              "linear noperspective centroid", "linear sample", "linear noperspective sample"};
    return m < 8 ? n[m] : "?";
}
const char* samplerModeName(uint32_t m) {
    static const char* n[] = {"mode_default", "mode_comparison", "mode_mono"};
    return m < 3 ? n[m] : "mode_?";
}
std::string fmtFloat(float f) {
    char b[64];
    std::snprintf(b, sizeof b, "%f", static_cast<double>(f));
    return b;
}

Status parseSignature(const DxbcChunk& c, Signature& out) {
    out = Signature();
    out.fourcc = c.fourcc;
    uint32_t esz = 24;
    bool hasStream = false, hasMinPrec = false;
    if (c.fourcc == fourcc('O', 'S', 'G', '5')) { esz = 28; hasStream = true; }
    else if (c.fourcc == fourcc('I', 'S', 'G', '1') || c.fourcc == fourcc('O', 'S', 'G', '1') ||
             c.fourcc == fourcc('P', 'S', 'G', '1')) { esz = 32; hasStream = true; hasMinPrec = true; }
    BinReader r(c.data, c.size);
    C5R_TRY(r.need(8, (c.tag() + " header").c_str()));
    const uint32_t count = r.u32();
    r.u32();
    if (8 + static_cast<uint64_t>(count) * esz > c.size)
        return Status::fail(Err::Truncated, c.tag() + ": " + std::to_string(count) + " elements of " +
                                                std::to_string(esz) + " B do not fit in " + std::to_string(c.size) + " B");
    for (uint32_t i = 0; i < count; ++i) {
        SigElement e;
        if (hasStream) e.stream = r.u32();
        const uint32_t nameOff = r.u32();
        e.semanticIndex = r.u32();
        e.systemValue = r.u32();
        e.componentType = r.u32();
        e.reg = r.u32();
        e.mask = r.u8();
        e.rwMask = r.u8();
        r.u16();
        if (hasMinPrec) e.minPrecision = r.u32();
        if (nameOff >= c.size) return layout(c.tag() + ": semantic name offset " + std::to_string(nameOff) + " outside the chunk");
        const void* z = std::memchr(c.data + nameOff, 0, c.size - nameOff);
        if (!z) return layout(c.tag() + ": unterminated semantic name");
        e.semantic.assign(reinterpret_cast<const char*>(c.data + nameOff),
                          static_cast<const uint8_t*>(z) - (c.data + nameOff));
        out.elems.push_back(e);
    }
    return Status::Ok();
}

struct Decoder {
    ShaderReflection& R;
    std::map<uint32_t, CbUse> use;
    std::map<std::pair<uint32_t, uint32_t>, CbRegUse> regs;

    explicit Decoder(ShaderReflection& r) : R(r) {}

    void noteCbRead(const Operand& o, uint8_t used) {
        if (o.indexDim < 2 || o.idx[0].relative) return; // SM5.1-style ranges are not in the EXE
        const uint32_t slot = o.idx[0].imm;
        CbUse& u = use[slot];
        u.slot = slot;
        ++u.reads;
        if (o.idx[1].relative) {
            const uint32_t base = o.idx[1].rep == 2 ? 0 : o.idx[1].imm;
            if (std::find(u.dynamicBases.begin(), u.dynamicBases.end(), base) == u.dynamicBases.end())
                u.dynamicBases.push_back(base);
            u.dynamicSwizzleMask |= o.readMask();
        } else {
            CbRegUse& g = regs[std::make_pair(slot, o.idx[1].imm)];
            g.reg = o.idx[1].imm;
            g.swizzleMask |= o.readMask();
            g.usedMask |= used;
            ++g.reads;
        }
    }

    Status instruction(uint32_t op, const uint32_t* t, uint32_t p, uint32_t end) {
        ++R.instructionCount;
        if (op == OP_INTERFACE_CALL) {
            if (p >= end) return layout("fcall without its function index");
            ++p;
        }
        std::vector<Operand> ops;
        while (p < end) {
            Operand o;
            C5R_TRY(readOperand(t, p, end, o, 0));
            ops.push_back(o);
        }
        const size_t nd = hasTwoDests(op) ? 2 : 1;
        uint8_t dstMask = 0;
        const bool cw = isComponentWise(op) && ops.size() > nd;
        if (cw)
            for (size_t i = 0; i < nd; ++i)
                if (ops[i].type != OT_NULL) dstMask |= ops[i].writeMask();
        for (size_t i = 0; i < ops.size(); ++i) {
            const Operand& o = ops[i];
            if (o.type != OT_CONSTANT_BUFFER) continue;
            uint8_t used = o.readMask();
            if (cw && i >= nd && o.numComp == 4 && o.selMode == 1) {
                used = 0;
                for (int c = 0; c < 4; ++c)
                    if (dstMask & (1 << c)) used |= static_cast<uint8_t>(1 << o.swz[c]);
            } else if ((op == OP_DP2 || op == OP_DP3) && o.numComp == 4 && o.selMode == 1) {
                used = 0;
                for (int c = 0; c < (op == OP_DP2 ? 2 : 3); ++c) used |= static_cast<uint8_t>(1 << o.swz[c]);
            }
            noteCbRead(o, used);
        }
        return Status::Ok();
    }

    Status oneOperand(const uint32_t* t, uint32_t& p, uint32_t end, Operand& o) {
        C5R_TRY(readOperand(t, p, end, o, 0));
        return Status::Ok();
    }
    Status dword(const uint32_t* t, uint32_t& p, uint32_t end, uint32_t& v, const char* what) {
        if (p >= end) return layout(std::string(what) + " missing");
        v = t[p++];
        return Status::Ok();
    }

    Status declaration(uint32_t op, uint32_t tok, const uint32_t* t, uint32_t p, uint32_t end) {
        std::string line;
        Operand o;
        uint32_t v = 0;
        switch (op) {
        case OP_DCL_RESOURCE: {
            C5R_TRY(oneOperand(t, p, end, o));
            C5R_TRY(dword(t, p, end, v, "dcl_resource return type"));
            ResourceDecl d;
            d.opcode = op;
            d.slot = o.indexDim ? o.idx[0].imm : 0;
            d.dimension = (tok >> 11) & 0x1F;
            d.sampleCount = (tok >> 16) & 0x7F;
            for (int i = 0; i < 4; ++i) d.returnType[i] = static_cast<uint8_t>((v >> (4 * i)) & 0xF);
            R.resources.push_back(d);
            line = std::string("dcl_resource_") + resourceDimName(d.dimension);
            if (d.dimension == 4 || d.dimension == 9) line += "(" + std::to_string(d.sampleCount) + ")";
            line += " (";
            for (int i = 0; i < 4; ++i) line += std::string(i ? "," : "") + returnTypeName(d.returnType[i]);
            line += ") " + renderOperand(o);
            break;
        }
        case OP_DCL_CONSTANT_BUFFER: {
            C5R_TRY(oneOperand(t, p, end, o));
            CbDecl d;
            d.slot = o.idx[0].imm;
            d.vec4Count = o.indexDim >= 2 ? o.idx[1].imm : 0;
            d.dynamicIndexed = ((tok >> 11) & 1) != 0;
            R.cbuffers.push_back(d);
            line = "dcl_constantbuffer CB" + std::to_string(d.slot) + "[" + std::to_string(d.vec4Count) + "], " +
                   (d.dynamicIndexed ? "dynamicIndexed" : "immediateIndexed");
            break;
        }
        case OP_DCL_SAMPLER: {
            C5R_TRY(oneOperand(t, p, end, o));
            SamplerDecl d;
            d.slot = o.indexDim ? o.idx[0].imm : 0;
            d.mode = (tok >> 11) & 0xF;
            R.samplers.push_back(d);
            line = "dcl_sampler " + renderOperand(o) + ", " + samplerModeName(d.mode);
            break;
        }
        case OP_DCL_INDEX_RANGE:
            C5R_TRY(oneOperand(t, p, end, o));
            C5R_TRY(dword(t, p, end, v, "dcl_indexrange count"));
            line = "dcl_indexrange " + renderOperand(o) + " " + std::to_string(v);
            break;
        case OP_DCL_GS_OUTPUT_TOPOLOGY: {
            static const char* n[] = {"undefined", "pointlist", "linelist", "linestrip", "trianglelist", "trianglestrip"};
            R.gs.outputTopology = (tok >> 11) & 0x3F;
            line = std::string("dcl_outputtopology ") + (R.gs.outputTopology < 6 ? n[R.gs.outputTopology] : "?");
            break;
        }
        case OP_DCL_GS_INPUT_PRIMITIVE: {
            static const char* n[] = {"undefined", "point", "line", "triangle", "?", "?", "lineadj", "triangleadj"};
            R.gs.inputPrimitive = (tok >> 11) & 0x3F;
            line = std::string("dcl_inputprimitive ") + (R.gs.inputPrimitive < 8 ? n[R.gs.inputPrimitive] : "?");
            break;
        }
        case OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
            C5R_TRY(dword(t, p, end, R.gs.maxOutputVertices, "dcl_maxout count"));
            line = "dcl_maxout " + std::to_string(R.gs.maxOutputVertices);
            break;
        case OP_DCL_INPUT: case OP_DCL_INPUT_SGV: case OP_DCL_INPUT_SIV: case OP_DCL_INPUT_PS:
        case OP_DCL_INPUT_PS_SGV: case OP_DCL_INPUT_PS_SIV: case OP_DCL_OUTPUT: case OP_DCL_OUTPUT_SGV:
        case OP_DCL_OUTPUT_SIV: {
            C5R_TRY(oneOperand(t, p, end, o));
            IoDecl d;
            d.opcode = op;
            d.operandType = o.type;
            d.indexDim = o.indexDim;
            for (uint32_t i = 0; i < o.indexDim && i < 2; ++i) d.index[i] = o.idx[i].imm;
            d.mask = o.numComp == 4 && o.selMode == 0 ? o.mask : 0;
            const bool sv = op == OP_DCL_INPUT_SGV || op == OP_DCL_INPUT_SIV || op == OP_DCL_INPUT_PS_SGV ||
                            op == OP_DCL_INPUT_PS_SIV || op == OP_DCL_OUTPUT_SGV || op == OP_DCL_OUTPUT_SIV;
            if (sv) C5R_TRY(dword(t, p, end, d.systemValue, "system value name"));
            const bool ps = op == OP_DCL_INPUT_PS || op == OP_DCL_INPUT_PS_SGV || op == OP_DCL_INPUT_PS_SIV;
            if (ps) d.interpolation = (tok >> 11) & 0xF;
            static const char* names[] = {"dcl_input", "dcl_input_sgv", "dcl_input_siv", "dcl_input_ps",
                                          "dcl_input_ps_sgv", "dcl_input_ps_siv", "dcl_output", "dcl_output_sgv",
                                          "dcl_output_siv"};
            line = names[op - OP_DCL_INPUT];
            line += " ";
            if (ps) line += std::string(interpName(d.interpolation)) + " ";
            line += renderOperand(o);
            if (sv) line += std::string(", ") + sysValueName(d.systemValue);
            (op >= OP_DCL_OUTPUT ? R.outputs : R.inputs).push_back(d);
            break;
        }
        case OP_DCL_TEMPS:
            C5R_TRY(dword(t, p, end, v, "dcl_temps count"));
            R.temps = std::max(R.temps, v);
            line = "dcl_temps " + std::to_string(v);
            break;
        case OP_DCL_INDEXABLE_TEMP: {
            IndexableTemp x;
            C5R_TRY(dword(t, p, end, x.reg, "dcl_indexableTemp register"));
            C5R_TRY(dword(t, p, end, x.count, "dcl_indexableTemp count"));
            C5R_TRY(dword(t, p, end, x.components, "dcl_indexableTemp components"));
            R.indexableTemps.push_back(x);
            line = "dcl_indexableTemp x" + std::to_string(x.reg) + "[" + std::to_string(x.count) + "], " +
                   std::to_string(x.components);
            break;
        }
        case OP_DCL_GLOBAL_FLAGS: {
            static const char* n[] = {"refactoringAllowed", "enableDoublePrecisionFloatOps", "forceEarlyDepthStencil",
                                      "enableRawAndStructuredBuffers", "skipOptimization", "enableMinimumPrecision",
                                      "enable11_1DoubleExtensions", "enable11_1ShaderExtensions"};
            R.globalFlags = (tok >> 11) & 0x1FFF;
            line = "dcl_globalFlags ";
            bool first = true;
            for (int i = 0; i < 8; ++i)
                if (R.globalFlags & (1u << i)) { line += std::string(first ? "" : " | ") + n[i]; first = false; }
            break;
        }
        case OP_HS_DECLS: line = "hs_decls"; break;
        case OP_HS_CONTROL_POINT_PHASE: line = "hs_control_point_phase"; break;
        case OP_HS_FORK_PHASE: line = "hs_fork_phase"; ++R.tess.forkPhases; break;
        case OP_HS_JOIN_PHASE: line = "hs_join_phase"; ++R.tess.joinPhases; break;
        case OP_DCL_STREAM:
            C5R_TRY(oneOperand(t, p, end, o));
            line = "dcl_stream " + renderOperand(o);
            break;
        case OP_DCL_INPUT_CP_COUNT:
            R.tess.inputControlPoints = (tok >> 11) & 0x3F;
            line = "dcl_input_control_point_count " + std::to_string(R.tess.inputControlPoints);
            break;
        case OP_DCL_OUTPUT_CP_COUNT:
            R.tess.outputControlPoints = (tok >> 11) & 0x3F;
            line = "dcl_output_control_point_count " + std::to_string(R.tess.outputControlPoints);
            break;
        case OP_DCL_TESS_DOMAIN: {
            static const char* n[] = {"domain_undefined", "domain_isoline", "domain_tri", "domain_quad"};
            R.tess.domain = (tok >> 11) & 3;
            line = std::string("dcl_tessellator_domain ") + n[R.tess.domain];
            break;
        }
        case OP_DCL_TESS_PARTITIONING: {
            static const char* n[] = {"partitioning_undefined", "partitioning_integer", "partitioning_pow2",
                                      "partitioning_fractional_odd", "partitioning_fractional_even"};
            R.tess.partitioning = (tok >> 11) & 7;
            line = std::string("dcl_tessellator_partitioning ") + (R.tess.partitioning < 5 ? n[R.tess.partitioning] : "?");
            break;
        }
        case OP_DCL_TESS_OUTPUT_PRIMITIVE: {
            static const char* n[] = {"output_undefined", "output_point", "output_line", "output_triangle_cw",
                                      "output_triangle_ccw"};
            R.tess.outputPrimitive = (tok >> 11) & 7;
            line = std::string("dcl_tessellator_output_primitive ") +
                   (R.tess.outputPrimitive < 5 ? n[R.tess.outputPrimitive] : "?");
            break;
        }
        case OP_DCL_HS_MAX_TESSFACTOR:
            C5R_TRY(dword(t, p, end, v, "dcl_hs_max_tessfactor value"));
            std::memcpy(&R.tess.maxTessFactor, &v, 4);
            line = "dcl_hs_max_tessfactor l(" + fmtFloat(R.tess.maxTessFactor) + ")";
            break;
        case OP_DCL_HS_FORK_INSTANCE_COUNT:
            C5R_TRY(dword(t, p, end, v, "dcl_hs_fork_phase_instance_count value"));
            R.tess.forkInstanceCounts.push_back(v);
            line = "dcl_hs_fork_phase_instance_count " + std::to_string(v);
            break;
        case OP_DCL_HS_JOIN_INSTANCE_COUNT:
            C5R_TRY(dword(t, p, end, v, "dcl_hs_join_phase_instance_count value"));
            R.tess.joinInstanceCounts.push_back(v);
            line = "dcl_hs_join_phase_instance_count " + std::to_string(v);
            break;
        case OP_DCL_THREAD_GROUP:
            for (int i = 0; i < 3; ++i) C5R_TRY(dword(t, p, end, R.threadGroup[i], "dcl_thread_group size"));
            line = "dcl_thread_group " + std::to_string(R.threadGroup[0]) + ", " + std::to_string(R.threadGroup[1]) +
                   ", " + std::to_string(R.threadGroup[2]);
            break;
        case OP_DCL_UAV_TYPED: case OP_DCL_UAV_RAW: case OP_DCL_UAV_STRUCTURED:
        case OP_DCL_RESOURCE_RAW: case OP_DCL_RESOURCE_STRUCTURED: {
            C5R_TRY(oneOperand(t, p, end, o));
            ResourceDecl d;
            d.opcode = op;
            d.uav = op <= OP_DCL_UAV_STRUCTURED;
            d.slot = o.indexDim ? o.idx[0].imm : 0;
            if (d.uav) d.flags = ((tok >> 16) & 1) | (((tok >> 23) & 1) << 1);
            const std::string sfx = std::string((d.flags & 1) ? "_glc" : "") + ((d.flags & 2) ? "_opc" : "");
            if (op == OP_DCL_UAV_TYPED) {
                C5R_TRY(dword(t, p, end, v, "dcl_uav_typed return type"));
                d.dimension = (tok >> 11) & 0x1F;
                for (int i = 0; i < 4; ++i) d.returnType[i] = static_cast<uint8_t>((v >> (4 * i)) & 0xF);
                line = std::string("dcl_uav_typed_") + resourceDimName(d.dimension) + sfx + " (";
                for (int i = 0; i < 4; ++i) line += std::string(i ? "," : "") + returnTypeName(d.returnType[i]);
                line += ") " + renderOperand(o);
            } else if (op == OP_DCL_UAV_RAW || op == OP_DCL_RESOURCE_RAW) {
                d.dimension = 11;
                line = std::string(d.uav ? "dcl_uav_raw" : "dcl_resource_raw") + sfx + " " + renderOperand(o);
            } else {
                C5R_TRY(dword(t, p, end, d.stride, "structured stride"));
                d.dimension = 12;
                line = std::string(d.uav ? "dcl_uav_structured" : "dcl_resource_structured") + sfx + " " +
                       renderOperand(o) + ", " + std::to_string(d.stride);
            }
            R.resources.push_back(d);
            break;
        }
        case OP_DCL_TGSM_RAW:
            C5R_TRY(oneOperand(t, p, end, o));
            C5R_TRY(dword(t, p, end, v, "dcl_tgsm_raw size"));
            line = "dcl_tgsm_raw " + renderOperand(o) + ", " + std::to_string(v);
            break;
        case OP_DCL_TGSM_STRUCTURED: {
            uint32_t n = 0;
            C5R_TRY(oneOperand(t, p, end, o));
            C5R_TRY(dword(t, p, end, v, "dcl_tgsm_structured stride"));
            C5R_TRY(dword(t, p, end, n, "dcl_tgsm_structured count"));
            line = "dcl_tgsm_structured " + renderOperand(o) + ", " + std::to_string(v) + ", " + std::to_string(n);
            break;
        }
        case OP_DCL_GS_INSTANCE_COUNT:
            C5R_TRY(dword(t, p, end, R.gs.instanceCount, "dcl_gsinstances count"));
            line = "dcl_gsinstances " + std::to_string(R.gs.instanceCount);
            break;
        default:
            // dcl_function_body / table / interface (class linkage) and reserved opcodes: skipped by length.
            line = "dcl_opcode_" + std::to_string(op);
            break;
        }
        R.declLines.push_back(line);
        return Status::Ok();
    }

    Status run(const DxbcChunk& prog) {
        const uint32_t n = prog.size / 4;
        std::vector<uint32_t> tv(n);
        std::memcpy(tv.data(), prog.data, static_cast<size_t>(n) * 4);
        const uint32_t* t = tv.data();
        uint32_t pos = 2;
        while (pos < n) {
            const uint32_t tok = t[pos];
            const uint32_t op = tok & 0x7FF;
            if (op == OP_CUSTOMDATA) {
                if (pos + 1 >= n) return layout("customdata block without its length");
                const uint32_t len = t[pos + 1];
                if (len < 2 || len > n - pos) return layout("customdata length " + std::to_string(len) + " at token " + std::to_string(pos));
                if ((tok >> 11) == 3) {
                    R.immediateCbVec4 += (len - 2) / 4;
                    R.declLines.push_back("dcl_immediateConstantBuffer [" + std::to_string((len - 2) / 4) + "]");
                }
                pos += len;
                continue;
            }
            const uint32_t len = (tok >> 24) & 0x7F;
            if (len == 0) return layout("instruction of length 0 at token " + std::to_string(pos));
            if (len > n - pos) return layout("instruction at token " + std::to_string(pos) + " runs past the program end");
            const uint32_t end = pos + len;
            uint32_t p = pos + 1;
            bool ext = (tok >> 31) != 0;
            while (ext) {
                if (p >= end) return layout("extended opcode token past the end of its instruction");
                ext = (t[p++] >> 31) != 0;
            }
            if (isDeclOrMarker(op)) C5R_TRY(declaration(op, tok, t, p, end));
            else C5R_TRY(instruction(op, t, p, end));
            pos = end;
        }
        for (auto& kv : regs) {
            CbUse& u = use[kv.first.first];
            u.regs.push_back(kv.second);
        }
        for (auto& kv : use) {
            std::sort(kv.second.dynamicBases.begin(), kv.second.dynamicBases.end());
            R.cbUse.push_back(kv.second);
        }
        return Status::Ok();
    }
};

} // namespace

const CbDecl* ShaderReflection::findCb(uint32_t slot) const {
    for (const CbDecl& c : cbuffers)
        if (c.slot == slot) return &c;
    return nullptr;
}
const CbUse* ShaderReflection::findCbUse(uint32_t slot) const {
    for (const CbUse& c : cbUse)
        if (c.slot == slot) return &c;
    return nullptr;
}

const char* sysValueName(uint32_t n) {
    static const char* names[] = {"",
                                  "position",
                                  "clip_distance",
                                  "cull_distance",
                                  "rendertarget_array_index",
                                  "viewport_array_index",
                                  "vertex_id",
                                  "primitive_id",
                                  "instance_id",
                                  "is_front_face",
                                  "sampleIndex",
                                  "finalQuadUeq0EdgeTessFactor",
                                  "finalQuadVeq0EdgeTessFactor",
                                  "finalQuadUeq1EdgeTessFactor",
                                  "finalQuadVeq1EdgeTessFactor",
                                  "finalQuadUInsideTessFactor",
                                  "finalQuadVInsideTessFactor",
                                  "finalTriUeq0EdgeTessFactor",
                                  "finalTriVeq0EdgeTessFactor",
                                  "finalTriWeq0EdgeTessFactor",
                                  "finalTriInsideTessFactor",
                                  "finalLineDetailTessFactor",
                                  "finalLineDensityTessFactor"};
    return n < sizeof(names) / sizeof(names[0]) ? names[n] : "?";
}

const char* resourceDimName(uint32_t d) {
    static const char* n[] = {"unknown",        "buffer",         "texture1d",        "texture2d",
                              "texture2dms",    "texture3d",      "texturecube",      "texture1darray",
                              "texture2darray", "texture2dmsarray", "texturecubearray", "raw", "structured"};
    return d < 13 ? n[d] : "?";
}

const char* componentTypeName(uint32_t t) {
    static const char* n[] = {"unknown", "uint", "sint", "float"};
    return t < 4 ? n[t] : "?";
}

std::string maskStr(uint8_t mask) {
    std::string s;
    static const char xyzw[] = "xyzw";
    for (int i = 0; i < 4; ++i)
        if (mask & (1 << i)) s += xyzw[i];
    return s;
}

std::string sigSummary(const Signature& s) {
    std::string out;
    for (const SigElement& e : s.elems) {
        if (!out.empty()) out += ' ';
        out += e.semantic + std::to_string(e.semanticIndex) + "@v" + std::to_string(e.reg) + ":" + maskStr(e.mask);
        if (e.systemValue) out += ":sv" + std::to_string(e.systemValue);
        if (e.componentType != 3) out += std::string(":") + componentTypeName(e.componentType);
    }
    return out;
}

Status reflect(const uint8_t* blob, size_t size, ShaderReflection& out) {
    out = ShaderReflection();
    DxbcContainer c;
    C5R_TRY(parseDxbc(blob, size, c, true));
    const DxbcChunk* prog = c.program();
    if (!prog) return Status::fail(Err::NotFound, "no SHDR/SHEX chunk");
    out.stage = c.stage;
    out.major = c.major;
    out.minor = c.minor;
    out.chunks = c.chunkList();
    for (const DxbcChunk& ch : c.chunks) {
        const std::string tag = ch.tag();
        if (tag == "ISGN" || tag == "ISG1") C5R_TRY(parseSignature(ch, out.input));
        else if (tag == "OSGN" || tag == "OSG1" || tag == "OSG5") C5R_TRY(parseSignature(ch, out.output));
        else if (tag == "PCSG" || tag == "PSG1") C5R_TRY(parseSignature(ch, out.patchConstant));
    }
    Decoder d(out);
    return d.run(*prog);
}

}} // namespace c5r::shaders
