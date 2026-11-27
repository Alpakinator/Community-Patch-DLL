// D3DDisassemble / D3DReflect wrappers and the decoder-vs-disassembly cross-check (task 2902).
#include "c5r/shaders/d3dcheck.h"

#include <windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <sstream>

namespace c5r { namespace shaders {

namespace {
std::string hr(long h) {
    char b[32];
    std::snprintf(b, sizeof b, "0x%08lx", static_cast<unsigned long>(h));
    return b;
}
std::string norm(const std::string& s) {
    std::string o;
    bool sp = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r') { sp = !o.empty(); continue; }
        if (sp) o += ' ';
        sp = false;
        o += c;
    }
    return o;
}
bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
uint8_t lettersMask(const std::string& s) {
    uint8_t m = 0;
    for (char c : s) {
        if (c == 'x') m |= 1;
        else if (c == 'y') m |= 2;
        else if (c == 'z') m |= 4;
        else if (c == 'w') m |= 8;
    }
    return m;
}
} // namespace

Status d3dDisassemble(const uint8_t* blob, size_t size, std::string& text) {
    text.clear();
    ID3DBlob* out = nullptr;
    const HRESULT h = D3DDisassemble(blob, size, 0, nullptr, &out);
    if (FAILED(h) || !out) {
        if (out) out->Release();
        return Status::fail(Err::Device, "D3DDisassemble " + hr(h));
    }
    text.assign(static_cast<const char*>(out->GetBufferPointer()), out->GetBufferSize());
    while (!text.empty() && text.back() == '\0') text.pop_back();
    out->Release();
    return Status::Ok();
}

Status d3dReflectInfo(const uint8_t* blob, size_t size, D3DReflectInfo& out) {
    out = D3DReflectInfo();
    ID3D11ShaderReflection* r = nullptr;
    const HRESULT h = D3DReflect(blob, size, IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&r));
    out.hr = h;
    if (FAILED(h) || !r) return Status::fail(Err::Device, "D3DReflect " + hr(h));
    D3D11_SHADER_DESC d = {};
    const HRESULT g = r->GetDesc(&d);
    r->Release();
    if (FAILED(g)) {
        out.hr = g;
        return Status::fail(Err::Device, "GetDesc " + hr(g));
    }
    out.version = d.Version;
    out.constantBuffers = d.ConstantBuffers;
    out.boundResources = d.BoundResources;
    out.inputParameters = d.InputParameters;
    out.outputParameters = d.OutputParameters;
    out.patchConstantParameters = d.PatchConstantParameters;
    out.instructionCount = d.InstructionCount;
    out.tempRegisterCount = d.TempRegisterCount;
    return Status::Ok();
}

DisasmFacts parseDisassembly(const std::string& text) {
    DisasmFacts f;
    std::istringstream in(text);
    std::string raw;
    bool started = false;  // after the model line ("vs_5_0")
    int icbDepth = 0;
    uint32_t icbRows = 0;
    while (std::getline(in, raw)) {
        const std::string s = norm(raw);
        if (s.empty() || startsWith(s, "//")) continue;
        if (icbDepth > 0) {
            for (char c : s) {
                if (c == '{') { ++icbDepth; ++icbRows; }
                else if (c == '}') --icbDepth;
            }
            if (icbDepth == 0) f.declLines.push_back("dcl_immediateConstantBuffer [" + std::to_string(icbRows) + "]");
            continue;
        }
        if (!started) {
            if (s.size() == 6 && s[2] == '_' && s[4] == '_') started = true; // "ps_5_0"
            continue;
        }
        if (startsWith(s, "dcl_immediateConstantBuffer")) {
            icbDepth = 0;
            icbRows = 0;
            for (char c : s) {
                if (c == '{') { if (icbDepth++ > 0) ++icbRows; }
                else if (c == '}') --icbDepth;
            }
            if (icbDepth == 0) f.declLines.push_back("dcl_immediateConstantBuffer [" + std::to_string(icbRows) + "]");
            continue;
        }
        if (startsWith(s, "dcl_") || s == "hs_decls" || s == "hs_control_point_phase" || s == "hs_fork_phase" ||
            s == "hs_join_phase") {
            f.declLines.push_back(s);
            continue;
        }
        ++f.instructionCount;
        // constant-buffer operands: "cb<slot>[<reg>]" or "cb<slot>[<rel> + <base>]", then ".<letters>"
        for (size_t i = 0; (i = s.find("cb", i)) != std::string::npos; i += 2) {
            if (i > 0 && (std::isalnum(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '_')) continue;
            size_t j = i + 2;
            const size_t d0 = j;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            if (j == d0 || j >= s.size() || s[j] != '[') continue;
            const std::string slot = s.substr(d0, j - d0);
            size_t close = j, depth = 0;
            for (; close < s.size(); ++close) {
                if (s[close] == '[') ++depth;
                else if (s[close] == ']' && --depth == 0) break;
            }
            if (close >= s.size()) continue;
            const std::string inner = s.substr(j + 1, close - j - 1);
            std::string letters;
            size_t k = close + 1;
            if (k < s.size() && s[k] == '.') {
                ++k;
                while (k < s.size() && std::strchr("xyzw", s[k])) letters += s[k++];
            }
            const uint8_t m = letters.empty() ? 0xF : lettersMask(letters);
            f.cbReads["cb" + slot + "[*]"]++;
            bool imm = !inner.empty();
            for (char c : inner) imm = imm && std::isdigit(static_cast<unsigned char>(c));
            if (imm) {
                const std::string key = "cb" + slot + "[" + std::to_string(std::stoul(inner)) + "]";
                f.cbReads[key]++;
                f.cbMasks[key] |= m;
            } else {
                const size_t plus = inner.rfind('+');
                std::string base = "0";
                if (plus != std::string::npos) base = norm(inner.substr(plus + 1));
                f.cbReads["cb" + slot + "[r+" + base + "]"] = 0;
                f.cbMasks["cb" + slot + "[r+*]"] |= m;
            }
        }
    }
    return f;
}

DisasmFacts factsOf(const ShaderReflection& r) {
    DisasmFacts f;
    f.declLines = r.declLines;
    f.instructionCount = r.instructionCount;
    for (const CbUse& u : r.cbUse) {
        const std::string slot = std::to_string(u.slot);
        f.cbReads["cb" + slot + "[*]"] = u.reads;
        for (const CbRegUse& g : u.regs) {
            const std::string key = "cb" + slot + "[" + std::to_string(g.reg) + "]";
            f.cbReads[key] = g.reads;
            f.cbMasks[key] = g.swizzleMask;
        }
        for (uint32_t b : u.dynamicBases) f.cbReads["cb" + slot + "[r+" + std::to_string(b) + "]"] = 0;
        if (!u.dynamicBases.empty()) f.cbMasks["cb" + slot + "[r+*]"] = u.dynamicSwizzleMask;
    }
    return f;
}

size_t compareWithDisassembly(const ShaderReflection& r, const std::string& text, std::vector<std::string>& diffs) {
    diffs.clear();
    const DisasmFacts a = factsOf(r);
    const DisasmFacts b = parseDisassembly(text);
    const size_t n = std::max(a.declLines.size(), b.declLines.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < a.declLines.size() ? a.declLines[i] : "(none)";
        const std::string y = i < b.declLines.size() ? b.declLines[i] : "(none)";
        if (x != y) diffs.push_back("decl " + std::to_string(i) + ": decoder '" + x + "' vs disassembly '" + y + "'");
    }
    if (a.instructionCount != b.instructionCount)
        diffs.push_back("instructions: decoder " + std::to_string(a.instructionCount) + " vs disassembly " +
                        std::to_string(b.instructionCount));
    auto cmpMap = [&diffs](const char* what, const std::map<std::string, uint32_t>& x,
                           const std::map<std::string, uint32_t>& y) {
        for (const auto& kv : x) {
            auto it = y.find(kv.first);
            if (it == y.end()) diffs.push_back(std::string(what) + " " + kv.first + ": only in the decoder");
            else if (it->second != kv.second)
                diffs.push_back(std::string(what) + " " + kv.first + ": decoder " + std::to_string(kv.second) +
                                " vs disassembly " + std::to_string(it->second));
        }
        for (const auto& kv : y)
            if (!x.count(kv.first)) diffs.push_back(std::string(what) + " " + kv.first + ": only in the disassembly");
    };
    cmpMap("cb reads", a.cbReads, b.cbReads);
    std::map<std::string, uint32_t> ma, mb;
    for (const auto& kv : a.cbMasks) ma[kv.first] = kv.second;
    for (const auto& kv : b.cbMasks) mb[kv.first] = kv.second;
    cmpMap("cb mask", ma, mb);
    return diffs.size();
}

}} // namespace c5r::shaders
