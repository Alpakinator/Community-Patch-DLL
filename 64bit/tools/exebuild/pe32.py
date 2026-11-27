"""pe32.py - a small, dependency-free reader for 32-bit PE files (ticket 0582, 2026-11-19).

Origin: a copy of research/02-exe-internals/evidence/0582/pe32.py at commit 94a825474 (ticket 0582), taken into
tools/exebuild/ on 2026-11-19 by ticket 4960 (epic 0590); the evidence copy stays as it was.

Written for the comparison of the pre-update Civ V EXE (Steam build 4390913, TimeDateStamp 0x546CD0A8) with
Firaxis' 2026-11-19 update (Steam build 26577625). Reads only; never writes the file it reads. Prints nothing
taken from the file beyond header facts, names of imports/exports/sections and counts (D83).

    python -I pe32.py <file.exe|dll> [--json out.json]

Library use: PE(path) -> .sections, .imports {dll: [(name_or_ordinal, iat_rva)]}, .exports [(ordinal, name, rva)],
.relocs (sorted RVAs of HIGHLOW fixups), .rva_to_off(), .read_rva(), .summary().
"""
import hashlib
import json
import math
import struct
import sys

IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

DLLCHAR = {
    0x0020: "HIGH_ENTROPY_VA", 0x0040: "DYNAMIC_BASE", 0x0080: "FORCE_INTEGRITY", 0x0100: "NX_COMPAT",
    0x0200: "NO_ISOLATION", 0x0400: "NO_SEH", 0x0800: "NO_BIND", 0x1000: "APPCONTAINER", 0x2000: "WDM_DRIVER",
    0x4000: "GUARD_CF", 0x8000: "TERMINAL_SERVER_AWARE",
}
DIRS = ["export", "import", "resource", "exception", "security", "basereloc", "debug", "architecture",
        "globalptr", "tls", "load_config", "bound_import", "iat", "delay_import", "clr", "reserved"]


def entropy(b):
    if not b:
        return 0.0
    c = [0] * 256
    for x in b:
        c[x] += 1
    n = len(b)
    return -sum(v / n * math.log2(v / n) for v in c if v)


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not MZ")
        self.e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        if d[self.e_lfanew:self.e_lfanew + 4] != b"PE\0\0":
            raise ValueError("not PE")
        fh = self.e_lfanew + 4
        (self.machine, self.nsec, self.timestamp, _, _, self.opt_size, self.characteristics) = \
            struct.unpack_from("<HHIIIHH", d, fh)
        oh = fh + 20
        magic = struct.unpack_from("<H", d, oh)[0]
        if magic != 0x10B:
            raise ValueError("not PE32 (magic %#x)" % magic)
        self.entry_rva = struct.unpack_from("<I", d, oh + 16)[0]
        self.image_base = struct.unpack_from("<I", d, oh + 28)[0]
        self.section_alignment, self.file_alignment = struct.unpack_from("<II", d, oh + 32)
        self.os_ver = struct.unpack_from("<HH", d, oh + 40)
        self.subsys_ver = struct.unpack_from("<HH", d, oh + 48)
        self.size_of_image, self.size_of_headers, self.checksum = struct.unpack_from("<III", d, oh + 56)
        self.subsystem, self.dll_characteristics = struct.unpack_from("<HH", d, oh + 68)
        nrva = struct.unpack_from("<I", d, oh + 92)[0]
        self.dirs = {}
        for i in range(min(nrva, 16)):
            rva, size = struct.unpack_from("<II", d, oh + 96 + 8 * i)
            self.dirs[DIRS[i]] = (rva, size)
        so = oh + self.opt_size
        self.sections = []
        for i in range(self.nsec):
            o = so + 40 * i
            name = d[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", d, o + 8)
            ch = struct.unpack_from("<I", d, o + 36)[0]
            self.sections.append(dict(name=name, rva=va, vsize=vsize, raw_off=roff, raw_size=rsize, ch=ch))
        self._imports = None
        self._exports = None
        self._relocs = None

    # ---------------------------------------------------------------- address helpers
    def section_of(self, rva):
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["raw_size"]):
                return s
        return None

    def rva_to_off(self, rva):
        if rva < self.size_of_headers:
            return rva
        s = self.section_of(rva)
        if s is None or rva - s["rva"] >= s["raw_size"]:
            return None
        return s["raw_off"] + rva - s["rva"]

    def read_rva(self, rva, n):
        o = self.rva_to_off(rva)
        if o is None:
            return None
        return self.data[o:o + n]

    def u32(self, rva):
        b = self.read_rva(rva, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def cstr(self, rva, limit=512):
        o = self.rva_to_off(rva)
        if o is None:
            return None
        e = self.data.find(b"\0", o, o + limit)
        return self.data[o:e].decode("latin-1") if e >= 0 else None

    # ---------------------------------------------------------------- directories
    @property
    def imports(self):
        if self._imports is None:
            self._imports = {}
            rva, size = self.dirs.get("import", (0, 0))
            if rva:
                i = 0
                while True:
                    desc = self.read_rva(rva + 20 * i, 20)
                    if not desc or len(desc) < 20:
                        break
                    oft, _, _, name_rva, ft = struct.unpack("<IIIII", desc)
                    if not (oft or name_rva or ft):
                        break
                    dll = self.cstr(name_rva)
                    lst = []
                    thunk = oft or ft
                    j = 0
                    while True:
                        v = self.u32(thunk + 4 * j)
                        if not v:
                            break
                        if v & 0x80000000:
                            nm = "#%d" % (v & 0xFFFF)
                        else:
                            nm = self.cstr(v + 2)
                        lst.append((nm, ft + 4 * j))
                        j += 1
                    self._imports[dll] = lst
                    i += 1
        return self._imports

    @property
    def delay_imports(self):
        out = {}
        rva, size = self.dirs.get("delay_import", (0, 0))
        if not rva:
            return out
        i = 0
        while True:
            desc = self.read_rva(rva + 32 * i, 32)
            if not desc or len(desc) < 32:
                break
            attrs, name_rva, _, iat, int_, _, _, _ = struct.unpack("<8I", desc)
            if not name_rva:
                break
            rel = (lambda x: x) if attrs & 1 else (lambda x: x - self.image_base)
            dll = self.cstr(rel(name_rva))
            names = []
            j = 0
            while True:
                v = self.u32(rel(int_) + 4 * j)
                if not v:
                    break
                names.append("#%d" % (v & 0xFFFF) if v & 0x80000000 else self.cstr(rel(v) + 2))
                j += 1
            out[dll] = names
            i += 1
        return out

    @property
    def exports(self):
        if self._exports is None:
            self._exports = []
            rva, size = self.dirs.get("export", (0, 0))
            if rva:
                (_, _, _, _, name_rva, base, nfunc, nname, af, an, ao) = struct.unpack("<IIHHIIIIIII",
                                                                                     self.read_rva(rva, 40))
                names = {}
                for k in range(nname):
                    names[struct.unpack("<H", self.read_rva(ao + 2 * k, 2))[0]] = self.cstr(self.u32(an + 4 * k))
                for k in range(nfunc):
                    f = self.u32(af + 4 * k)
                    if f:
                        self._exports.append((base + k, names.get(k), f))
        return self._exports

    @property
    def relocs(self):
        if self._relocs is None:
            out = []
            rva, size = self.dirs.get("basereloc", (0, 0))
            p = 0
            while rva and p < size:
                page, bsize = struct.unpack("<II", self.read_rva(rva + p, 8))
                if bsize < 8:
                    break
                ent = self.read_rva(rva + p + 8, bsize - 8)
                for k in range(0, len(ent), 2):
                    e = struct.unpack_from("<H", ent, k)[0]
                    if e >> 12 == 3:
                        out.append(page + (e & 0xFFF))
                p += bsize
            out.sort()
            self._relocs = out
        return self._relocs

    def tls_callbacks(self):
        rva, size = self.dirs.get("tls", (0, 0))
        if not rva:
            return []
        cb_va = struct.unpack("<IIIIII", self.read_rva(rva, 24))[3]
        out = []
        if cb_va:
            k = 0
            while True:
                v = self.u32(cb_va - self.image_base + 4 * k)
                if not v:
                    break
                out.append(v)
                k += 1
        return out

    def load_config(self):
        rva, size = self.dirs.get("load_config", (0, 0))
        if not rva:
            return None
        b = self.read_rva(rva, 0x5C)
        sz = struct.unpack_from("<I", b, 0)[0]
        r = dict(size=sz)
        if sz >= 0x40:
            r["security_cookie_va"] = struct.unpack_from("<I", b, 0x3C)[0]
        if sz >= 0x48:
            r["seh_table_va"], r["seh_count"] = struct.unpack_from("<II", b, 0x40)
        if sz >= 0x5C:
            r["guard_cf_check_va"], _, r["guard_cf_table_va"], r["guard_cf_count"], r["guard_flags"] = \
                struct.unpack_from("<IIIII", b, 0x48)
        return r

    def debug_entries(self):
        rva, size = self.dirs.get("debug", (0, 0))
        out = []
        for k in range(size // 28 if rva else 0):
            e = self.read_rva(rva + 28 * k, 28)
            _, ts, _, _, typ, dsize, drva, dptr = struct.unpack("<IIHHIIII", e)
            item = dict(type=typ, timestamp=ts, size=dsize)
            if typ == 2 and dptr:
                cv = self.data[dptr:dptr + dsize]
                if cv[:4] == b"RSDS":
                    item["pdb"] = cv[24:].split(b"\0")[0].decode("latin-1")
                    item["age"] = struct.unpack_from("<I", cv, 20)[0]
            out.append(item)
        return out

    def certificate(self):
        """Authenticode blob facts (file offset, not RVA). Signer names are read from the PKCS#7 by a crude search
        for the X.520 commonName/organizationName strings; use PowerShell Get-AuthenticodeSignature for the truth."""
        off, size = self.dirs.get("security", (0, 0))
        if not off:
            return None
        blob = self.data[off:off + size]
        names = []
        k = 0
        while True:
            k = blob.find(b"\x06\x03\x55\x04", k)
            if k < 0:
                break
            oid = blob[k + 4]
            if oid in (3, 10) and k + 7 < len(blob):
                ln = blob[k + 6]
                s = blob[k + 7:k + 7 + ln].decode("latin-1", "replace")
                names.append(("CN" if oid == 3 else "O") + "=" + s)
            k += 5
        seen = []
        for n in names:
            if n not in seen:
                seen.append(n)
        return dict(file_offset=off, size=size, names=seen, at_end=(off + size == len(self.data)))

    def rich(self):
        d = self.data
        k = d.find(b"Rich", 0x80, self.e_lfanew)
        if k < 0:
            return None
        key = struct.unpack_from("<I", d, k + 4)[0]
        start = None
        for p in range(0x80, k, 4):
            if struct.unpack_from("<I", d, p)[0] ^ key == 0x536E6144:  # "DanS"
                start = p
                break
        if start is None:
            return None
        out = []
        for p in range(start + 16, k, 8):
            cid, cnt = struct.unpack_from("<II", d, p)
            cid ^= key
            cnt ^= key
            out.append(("%d.%d" % (cid >> 16, cid & 0xFFFF), cnt))
        return out

    def version_strings(self):
        """FileVersion/ProductVersion etc. from the version resource, by a UTF-16 scan of .rsrc."""
        s = next((x for x in self.sections if x["name"] == ".rsrc"), None)
        if not s:
            return {}
        b = self.data[s["raw_off"]:s["raw_off"] + s["raw_size"]]
        out = {}
        for key in ("CompanyName", "FileDescription", "FileVersion", "ProductName", "ProductVersion",
                    "OriginalFilename", "InternalName", "LegalCopyright"):
            kk = key.encode("utf-16-le") + b"\0\0"
            p = b.find(kk)
            if p < 0:
                continue
            p += len(kk)
            while p % 4:
                p += 1
            e = b.find(b"\0\0", p)
            while e > 0 and (e - p) % 2:
                e = b.find(b"\0\0", e + 1)
            out[key] = b[p:e].decode("utf-16-le", "replace")
        return out

    # ---------------------------------------------------------------- summary
    def summary(self):
        secs = []
        for s in self.sections:
            raw = self.data[s["raw_off"]:s["raw_off"] + s["raw_size"]]
            flags = ("R" if s["ch"] & IMAGE_SCN_MEM_READ else "-") + ("W" if s["ch"] & IMAGE_SCN_MEM_WRITE else "-") + \
                    ("X" if s["ch"] & IMAGE_SCN_MEM_EXECUTE else "-")
            nrel = sum(1 for r in self.relocs if s["rva"] <= r < s["rva"] + max(s["vsize"], s["raw_size"]))
            secs.append(dict(name=s["name"], va=self.image_base + s["rva"], vsize=s["vsize"], raw_size=s["raw_size"],
                             flags=flags, entropy=round(entropy(raw), 3), relocs_in=nrel,
                             sha256=hashlib.sha256(raw).hexdigest()))
        return dict(
            file=self.path, size=len(self.data), sha256=hashlib.sha256(self.data).hexdigest(),
            timestamp="0x%08X" % self.timestamp, machine="0x%X" % self.machine, image_base="0x%X" % self.image_base,
            entry_va="0x%X" % (self.image_base + self.entry_rva), size_of_image="0x%X" % self.size_of_image,
            checksum="0x%X" % self.checksum, subsystem=self.subsystem, os_ver=self.os_ver,
            dll_characteristics=[n for b, n in DLLCHAR.items() if self.dll_characteristics & b],
            directories={k: ["0x%X" % v[0], v[1]] for k, v in self.dirs.items() if v[0]},
            sections=secs, n_relocs=len(self.relocs),
            imports={dll: [n for n, _ in lst] for dll, lst in self.imports.items()},
            delay_imports=self.delay_imports,
            exports=[(o, n) for o, n, _ in self.exports],
            tls_callbacks=["0x%X" % v for v in self.tls_callbacks()],
            load_config=self.load_config(), debug=self.debug_entries(), certificate=self.certificate(),
            rich=self.rich(), version=self.version_strings(),
        )


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    pe = PE(argv[1])
    s = pe.summary()
    if "--json" in argv:
        with open(argv[argv.index("--json") + 1], "w") as f:
            json.dump(s, f, indent=1)
    print("%s  %d B  sha256 %s" % (s["file"], s["size"], s["sha256"]))
    print("stamp %s  base %s  entry %s  image 0x%X  checksum %s  dllchar %s" % (
        s["timestamp"], s["image_base"], s["entry_va"], pe.size_of_image, s["checksum"],
        ",".join(s["dll_characteristics"])))
    for x in s["sections"]:
        print("  %-8s va 0x%08X vsize 0x%08X raw 0x%08X %s H=%.2f relocs %d" % (
            x["name"], x["va"], x["vsize"], x["raw_size"], x["flags"], x["entropy"], x["relocs_in"]))
    print("imports: " + ", ".join("%s(%d)" % (k, len(v)) for k, v in s["imports"].items()))
    if s["delay_imports"]:
        print("delay imports: " + ", ".join("%s(%d)" % (k, len(v)) for k, v in s["delay_imports"].items()))
    print("exports %d, tls callbacks %s, relocs %d" % (len(s["exports"]), s["tls_callbacks"], s["n_relocs"]))
    print("load_config %s" % s["load_config"])
    print("debug %s" % s["debug"])
    print("certificate %s" % s["certificate"])
    print("version %s" % s["version"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
