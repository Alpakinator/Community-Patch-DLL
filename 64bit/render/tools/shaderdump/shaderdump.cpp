// shaderdump (task 2902): index, reflect and cross-check the DXBC blobs of CivilizationV_DX11.exe's `.shader` section.
//
//   shaderdump <exe> --out DIR      index CSV, per-blob reflection JSON, D3DDisassemble text and raw blobs to DIR
//                                   (game data: DIR must be scratch outside the repository and any game folder,
//                                   e.g. C:/c5-64bit-work/2900/2902/dump; D133: never on D:)
//   shaderdump <exe> --index FILE   only the index CSV (addresses, sizes, counts: no bytecode), e.g. for evidence
//   shaderdump <exe> --va 0x...     one blob's reflection on stdout
//   shaderdump <exe> --check [LOG]  decoder vs D3DDisassemble for every blob; agreement summary (and the log)
//   shaderdump <exe> --device       create every blob on a hardware D3D11 device (no window) and an input layout
//                                   from each VS's own input signature; counts
// The EXE is opened read-only.
#include <windows.h>
#include <d3d11.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "c5r/shaders/d3dcheck.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"

using namespace c5r;
using namespace c5r::shaders;

namespace {

std::wstring widen(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], static_cast<int>(w.size()));
    w.resize(n > 0 ? static_cast<size_t>(n - 1) : 0);
    return w;
}
std::string hex(uint32_t v, int w = 8) {
    char b[24];
    std::snprintf(b, sizeof b, "0x%0*x", w, v);
    return b;
}
std::string checksumHex(const uint8_t c[16]) {
    std::string s;
    char b[4];
    for (int i = 0; i < 16; ++i) { std::snprintf(b, sizeof b, "%02x", c[i]); s += b; }
    return s;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}
bool writeFile(const std::string& path, const void* data, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data, 1, n, f) == n;
    return std::fclose(f) == 0 && ok;
}
std::string setName(int s) { return s == 40 ? "4_0" : s == 41 ? "4_1" : s == 50 ? "5_0" : ""; }

// "cb1[0-3;5]" from the immediate reads; dynamic reads "cb0[r+0;r+4]" (no commas: the field is CSV)
std::string usedSummary(const ShaderReflection& r) {
    std::string out;
    for (const CbUse& u : r.cbUse) {
        if (!out.empty()) out += ' ';
        out += "cb" + std::to_string(u.slot) + "[";
        bool first = true;
        for (size_t i = 0; i < u.regs.size();) {
            size_t j = i;
            while (j + 1 < u.regs.size() && u.regs[j + 1].reg == u.regs[j].reg + 1) ++j;
            out += std::string(first ? "" : ";") + std::to_string(u.regs[i].reg);
            if (j > i) out += "-" + std::to_string(u.regs[j].reg);
            first = false;
            i = j + 1;
        }
        for (uint32_t b : u.dynamicBases) { out += std::string(first ? "" : ";") + "r+" + std::to_string(b); first = false; }
        out += "]";
    }
    return out;
}
std::string cbDecls(const ShaderReflection& r) {
    std::string s;
    for (const CbDecl& c : r.cbuffers)
        s += std::string(s.empty() ? "" : " ") + "cb" + std::to_string(c.slot) + "[" + std::to_string(c.vec4Count) + "]" +
             (c.dynamicIndexed ? "dyn" : "");
    return s;
}
std::string resDecls(const ShaderReflection& r) {
    std::string s;
    for (const ResourceDecl& d : r.resources)
        s += std::string(s.empty() ? "" : " ") + (d.uav ? "u" : "t") + std::to_string(d.slot) + ":" + resourceDimName(d.dimension);
    return s;
}
std::string smpDecls(const ShaderReflection& r) {
    std::string s;
    for (const SamplerDecl& d : r.samplers)
        s += std::string(s.empty() ? "" : " ") + "s" + std::to_string(d.slot) + (d.mode == 1 ? ":cmp" : d.mode == 2 ? ":mono" : "");
    return s;
}

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 32) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}
std::string sigJson(const Signature& s) {
    std::string o = "[";
    for (size_t i = 0; i < s.elems.size(); ++i) {
        const SigElement& e = s.elems[i];
        o += std::string(i ? ", " : "") + "{\"semantic\": " + jstr(e.semantic) + ", \"index\": " + std::to_string(e.semanticIndex) +
             ", \"register\": " + std::to_string(e.reg) + ", \"mask\": " + jstr(maskStr(e.mask)) +
             ", \"rwMask\": " + jstr(maskStr(e.rwMask)) + ", \"systemValue\": " + std::to_string(e.systemValue) +
             ", \"componentType\": " + jstr(componentTypeName(e.componentType)) + ", \"stream\": " + std::to_string(e.stream) + "}";
    }
    return o + "]";
}

std::string reflectionJson(const ShaderBlob& b, const ShaderReflection& r, const D3DReflectInfo& d) {
    std::string o = "{\n";
    o += "  \"va\": " + jstr(hex(b.va)) + ", \"fileOffset\": " + jstr(hex(b.fileOffset)) + ", \"size\": " + std::to_string(b.size) + ",\n";
    o += "  \"model\": " + jstr(b.model()) + ", \"chunks\": " + jstr(r.chunks) + ", \"checksum\": " + jstr(checksumHex(b.checksum)) + ",\n";
    o += "  \"set\": " + jstr(setName(b.set)) + ", \"createFunction\": " + jstr(hex(b.createFunction, 6)) +
         ", \"createSite\": " + jstr(hex(b.createSite, 6)) + ", \"handleGlobal\": " + jstr(hex(b.handleGlobal, 7)) +
         ", \"vertexFormatMask\": " + jstr(hex(b.vertexFormatMask, 1)) + ",\n";
    o += "  \"input\": " + sigJson(r.input) + ",\n  \"output\": " + sigJson(r.output) + ",\n  \"patchConstant\": " + sigJson(r.patchConstant) + ",\n";
    o += "  \"cbuffers\": [";
    for (size_t i = 0; i < r.cbuffers.size(); ++i)
        o += std::string(i ? ", " : "") + "{\"slot\": " + std::to_string(r.cbuffers[i].slot) + ", \"vec4\": " +
             std::to_string(r.cbuffers[i].vec4Count) + ", \"dynamicIndexed\": " + (r.cbuffers[i].dynamicIndexed ? "true" : "false") + "}";
    o += "],\n  \"cbUse\": [";
    for (size_t i = 0; i < r.cbUse.size(); ++i) {
        const CbUse& u = r.cbUse[i];
        o += std::string(i ? ",\n    " : "\n    ") + "{\"slot\": " + std::to_string(u.slot) + ", \"reads\": " + std::to_string(u.reads) + ", \"regs\": [";
        for (size_t k = 0; k < u.regs.size(); ++k)
            o += std::string(k ? ", " : "") + "[" + std::to_string(u.regs[k].reg) + ", " + jstr(maskStr(u.regs[k].swizzleMask)) + ", " +
                 jstr(maskStr(u.regs[k].usedMask)) + ", " + std::to_string(u.regs[k].reads) + "]";
        o += "], \"dynamicBases\": [";
        for (size_t k = 0; k < u.dynamicBases.size(); ++k) o += std::string(k ? ", " : "") + std::to_string(u.dynamicBases[k]);
        o += "], \"dynamicMask\": " + jstr(maskStr(u.dynamicSwizzleMask)) + "}";
    }
    o += "],\n  \"resources\": [";
    for (size_t i = 0; i < r.resources.size(); ++i) {
        const ResourceDecl& d2 = r.resources[i];
        o += std::string(i ? ", " : "") + "{\"slot\": " + jstr(std::string(d2.uav ? "u" : "t") + std::to_string(d2.slot)) +
             ", \"dimension\": " + jstr(resourceDimName(d2.dimension)) + ", \"stride\": " + std::to_string(d2.stride) + "}";
    }
    o += "],\n  \"samplers\": [";
    for (size_t i = 0; i < r.samplers.size(); ++i)
        o += std::string(i ? ", " : "") + "{\"slot\": " + std::to_string(r.samplers[i].slot) + ", \"mode\": " + std::to_string(r.samplers[i].mode) + "}";
    o += "],\n  \"temps\": " + std::to_string(r.temps) + ", \"indexableTemps\": " + std::to_string(r.indexableTemps.size()) +
         ", \"immediateCbVec4\": " + std::to_string(r.immediateCbVec4) + ", \"instructions\": " + std::to_string(r.instructionCount) + ",\n";
    o += "  \"tess\": {\"domain\": " + std::to_string(r.tess.domain) + ", \"partitioning\": " + std::to_string(r.tess.partitioning) +
         ", \"outputPrimitive\": " + std::to_string(r.tess.outputPrimitive) + ", \"inputControlPoints\": " +
         std::to_string(r.tess.inputControlPoints) + ", \"outputControlPoints\": " + std::to_string(r.tess.outputControlPoints) +
         ", \"forkPhases\": " + std::to_string(r.tess.forkPhases) + "},\n";
    o += "  \"d3dReflect\": {\"hr\": " + jstr(hex(static_cast<uint32_t>(d.hr))) + ", \"constantBuffers\": " + std::to_string(d.constantBuffers) +
         ", \"boundResources\": " + std::to_string(d.boundResources) + ", \"inputParameters\": " + std::to_string(d.inputParameters) +
         ", \"outputParameters\": " + std::to_string(d.outputParameters) + ", \"instructionCount\": " + std::to_string(d.instructionCount) + "},\n";
    o += "  \"declarations\": [";
    for (size_t i = 0; i < r.declLines.size(); ++i) o += std::string(i ? ",\n    " : "\n    ") + jstr(r.declLines[i]);
    o += "]\n}\n";
    return o;
}

const char* kCsvHeader =
    "va,file_offset,size,stage,model,checksum,set,create_function,create_site,handle_global,vs_format_mask,chunks,"
    "inputs,outputs,patch_constants,cbuffers,cb_used,resources,samplers,temps,indexable_temps,icb_vec4,instructions,"
    "d3dreflect_hr,d3dreflect_cb,d3dreflect_res,d3dreflect_in,d3dreflect_out,decoder_vs_disasm\n";

std::string csvRow(const ShaderBlob& b, const ShaderReflection& r, const D3DReflectInfo& d, size_t diffs) {
    std::string s;
    s += hex(b.va) + "," + hex(b.fileOffset, 6) + "," + std::to_string(b.size) + "," + stageName(b.stage) + "," + b.model() + "," +
         checksumHex(b.checksum) + "," + setName(b.set) + "," + hex(b.createFunction, 6) + "," + hex(b.createSite, 6) + "," +
         hex(b.handleGlobal, 7) + "," + (b.stage == Stage::Vertex ? hex(b.vertexFormatMask, 1) : "") + "," + r.chunks + "," +
         sigSummary(r.input) + "," + sigSummary(r.output) + "," + sigSummary(r.patchConstant) + "," + cbDecls(r) + "," +
         usedSummary(r) + "," + resDecls(r) + "," + smpDecls(r) + "," + std::to_string(r.temps) + "," +
         std::to_string(r.indexableTemps.size()) + "," + std::to_string(r.immediateCbVec4) + "," + std::to_string(r.instructionCount) + "," +
         hex(static_cast<uint32_t>(d.hr)) + "," + std::to_string(d.constantBuffers) + "," + std::to_string(d.boundResources) + "," +
         std::to_string(d.inputParameters) + "," + std::to_string(d.outputParameters) + "," +
         (diffs == 0 ? "agree" : std::to_string(diffs) + " diffs") + "\n";
    return s;
}

int usage() {
    std::fprintf(stderr, "usage: shaderdump <exe> (--out DIR | --index FILE | --va 0xVA | --check [LOG] | --device)\n");
    return 2;
}

int runDevice(const ShaderSection& sec) {
    ID3D11Device* dev = nullptr;
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_9_1;
    HRESULT h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 1, D3D11_SDK_VERSION, &dev, &got, nullptr);
    if (FAILED(h)) { std::printf("D3D11CreateDevice failed 0x%08lx\n", static_cast<unsigned long>(h)); return 1; }
    std::map<std::string, int> ok, fail;
    int layoutOk = 0, layoutFail = 0, layoutNone = 0;
    for (const ShaderBlob& b : sec.blobs()) {
        IUnknown* sh = nullptr;
        switch (b.stage) {
        case Stage::Vertex: h = dev->CreateVertexShader(b.bytes, b.size, nullptr, reinterpret_cast<ID3D11VertexShader**>(&sh)); break;
        case Stage::Pixel: h = dev->CreatePixelShader(b.bytes, b.size, nullptr, reinterpret_cast<ID3D11PixelShader**>(&sh)); break;
        case Stage::Hull: h = dev->CreateHullShader(b.bytes, b.size, nullptr, reinterpret_cast<ID3D11HullShader**>(&sh)); break;
        case Stage::Domain: h = dev->CreateDomainShader(b.bytes, b.size, nullptr, reinterpret_cast<ID3D11DomainShader**>(&sh)); break;
        case Stage::Geometry: h = dev->CreateGeometryShader(b.bytes, b.size, nullptr, reinterpret_cast<ID3D11GeometryShader**>(&sh)); break;
        default: h = E_INVALIDARG; break;
        }
        (SUCCEEDED(h) ? ok : fail)[b.model()]++;
        if (FAILED(h)) std::printf("create FAILED %s %s 0x%08lx\n", hex(b.va).c_str(), b.model().c_str(), static_cast<unsigned long>(h));
        if (sh) sh->Release();
        if (b.stage != Stage::Vertex) continue;
        ShaderReflection r;
        if (!reflect(b.bytes, b.size, r).ok()) { ++layoutFail; continue; }
        std::vector<D3D11_INPUT_ELEMENT_DESC> el;
        for (const SigElement& e : r.input.elems) {
            if (e.systemValue != 0) continue; // SV_VertexID etc. come from the input assembler, not a buffer
            int n = 0;
            for (int c = 0; c < 4; ++c) if (e.mask & (1 << c)) n = c + 1;
            static const DXGI_FORMAT f32[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
            static const DXGI_FORMAT u32[] = {DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32A32_UINT};
            static const DXGI_FORMAT s32[] = {DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32A32_SINT};
            const DXGI_FORMAT* tab = e.componentType == 1 ? u32 : e.componentType == 2 ? s32 : f32;
            D3D11_INPUT_ELEMENT_DESC d = {e.semantic.c_str(), e.semanticIndex, tab[n ? n - 1 : 0], 0,
                                          D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0};
            el.push_back(d);
        }
        if (el.empty()) { ++layoutNone; std::printf("no input layout needed: %s %s (%s)\n", hex(b.va).c_str(), b.model().c_str(), sigSummary(r.input).c_str()); continue; }
        ID3D11InputLayout* il = nullptr;
        h = dev->CreateInputLayout(el.data(), static_cast<UINT>(el.size()), b.bytes, b.size, &il);
        if (SUCCEEDED(h)) ++layoutOk;
        else { ++layoutFail; std::printf("input layout FAILED %s 0x%08lx\n", hex(b.va).c_str(), static_cast<unsigned long>(h)); }
        if (il) il->Release();
    }
    dev->Release();
    int total = 0, totalFail = 0;
    std::printf("device: hardware, feature level 0x%x, no window\n", static_cast<unsigned>(got));
    for (auto& kv : ok) { std::printf("created %s: %d\n", kv.first.c_str(), kv.second); total += kv.second; }
    for (auto& kv : fail) { std::printf("FAILED %s: %d\n", kv.first.c_str(), kv.second); totalFail += kv.second; }
    std::printf("created %d of %d; input layouts from the VS input signature: %d built, %d need none (system values only), %d failed\n",
                total, total + totalFail, layoutOk, layoutNone, layoutFail);
    return totalFail || layoutFail ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string exe = argv[1], mode = argv[2];
    ShaderSection sec;
    Status st = sec.open(widen(exe));
    if (!st.ok()) { std::fprintf(stderr, "shaderdump: %s\n", st.str().c_str()); return 1; }

    if (mode == "--device") return runDevice(sec);

    if (mode == "--va") {
        if (argc < 4) return usage();
        const ShaderBlob* b = sec.findByVa(static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)));
        if (!b) { std::fprintf(stderr, "shaderdump: no blob starts at %s\n", argv[3]); return 1; }
        ShaderReflection r;
        st = reflect(b->bytes, b->size, r);
        if (!st.ok()) { std::fprintf(stderr, "shaderdump: %s\n", st.str().c_str()); return 1; }
        D3DReflectInfo d;
        d3dReflectInfo(b->bytes, b->size, d);
        std::fputs(reflectionJson(*b, r, d).c_str(), stdout);
        return 0;
    }

    std::string outDir, indexFile, checkLog;
    if (mode == "--out" && argc >= 4) outDir = argv[3];
    else if (mode == "--index" && argc >= 4) indexFile = argv[3];
    else if (mode == "--check") checkLog = argc >= 4 ? argv[3] : "";
    else return usage();

    if (!outDir.empty()) {
        // never into the repository's tree of game data, a game folder or next to the EXE
        const std::string o = lower(outDir), e = lower(exe);
        const std::string exeDir = e.substr(0, e.find_last_of('/') == std::string::npos ? 0 : e.find_last_of('/'));
        if (o.find("steamapps") != std::string::npos || o.find("civilization v") != std::string::npos ||
            (!exeDir.empty() && o.compare(0, exeDir.size(), exeDir) == 0)) {
            std::fprintf(stderr, "shaderdump: refusing to write game data into %s\n", outDir.c_str());
            return 2;
        }
        for (const char* sub : {"", "/reflect", "/asm", "/blobs"}) CreateDirectoryA((outDir + sub).c_str(), nullptr);
    }

    std::string csv = kCsvHeader, log;
    size_t agree = 0, reflectOk = 0, d3dOk = 0, d3dZeroCb = 0;
    std::map<std::string, size_t> perModel;
    for (const ShaderBlob& b : sec.blobs()) {
        ShaderReflection r;
        st = reflect(b.bytes, b.size, r);
        if (!st.ok()) { log += hex(b.va) + " reflect FAILED " + st.str() + "\n"; continue; }
        ++reflectOk;
        D3DReflectInfo d;
        if (d3dReflectInfo(b.bytes, b.size, d).ok()) { ++d3dOk; if (d.constantBuffers == 0 && d.boundResources == 0) ++d3dZeroCb; }
        std::string text;
        size_t nd = 0;
        std::vector<std::string> diffs;
        if (!d3dDisassemble(b.bytes, b.size, text).ok()) { nd = 1; diffs.push_back("D3DDisassemble failed"); }
        else nd = compareWithDisassembly(r, text, diffs);
        if (nd == 0) ++agree;
        for (const std::string& x : diffs) log += hex(b.va) + " " + b.model() + " " + x + "\n";
        ++perModel[b.model()];
        csv += csvRow(b, r, d, nd);
        if (!outDir.empty()) {
            const std::string base = hex(b.va) .substr(2) + "_" + b.model();
            const std::string j = reflectionJson(b, r, d);
            if (!writeFile(outDir + "/reflect/" + base + ".json", j.data(), j.size()) ||
                !writeFile(outDir + "/asm/" + base + ".asm", text.data(), text.size()) ||
                !writeFile(outDir + "/blobs/" + base + ".dxbc", b.bytes, b.size)) {
                std::fprintf(stderr, "shaderdump: write failed under %s\n", outDir.c_str());
                return 1;
            }
        }
    }
    uint64_t bytes = 0, gapBytes = 0, gapNonZero = 0;
    for (const ShaderBlob& b : sec.blobs()) bytes += b.size;
    for (const SectionGap& g : sec.gaps()) { gapBytes += g.size; gapNonZero += g.allZero ? 0 : 1; }
    char sum[1024];
    std::snprintf(sum, sizeof sum,
                  "section VA 0x%08x file offset 0x%x size %u; blobs %zu (%llu B), gaps %zu (%llu B, %llu not all zero); "
                  "create sites applied: %s\nreflected %zu; D3DReflect S_OK %zu (0 cbuffers and 0 bound resources: %zu); "
                  "decoder agrees with D3DDisassemble on %zu of %zu\n",
                  sec.sectionVa(), sec.sectionFileOffset(), sec.sectionSize(), sec.blobs().size(),
                  static_cast<unsigned long long>(bytes), sec.gaps().size(), static_cast<unsigned long long>(gapBytes),
                  static_cast<unsigned long long>(gapNonZero), sec.createSitesApplied() ? "yes" : "no", reflectOk, d3dOk,
                  d3dZeroCb, agree, sec.blobs().size());
    std::fputs(sum, stdout);
    for (auto& kv : perModel) std::printf("  %s: %zu\n", kv.first.c_str(), kv.second);
    if (checkLog.empty() && mode == "--check") std::fputs(log.c_str(), stdout); // the differences, if any
    log = std::string(sum) + log;
    if (!checkLog.empty() && !writeFile(checkLog, log.data(), log.size())) return 1;
    if (!indexFile.empty() && !writeFile(indexFile, csv.data(), csv.size())) return 1;
    if (!outDir.empty()) {
        if (!writeFile(outDir + "/shader_index.csv", csv.data(), csv.size()) || !writeFile(outDir + "/check.txt", log.data(), log.size()))
            return 1;
    }
    return agree == sec.blobs().size() ? 0 : 1;
}
