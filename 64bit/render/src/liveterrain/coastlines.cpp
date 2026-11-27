// render/src/liveterrain/coastlines.cpp - the EXE's coastline curves and region fill, ported (ticket 4910, epic 0615).
// Our own code, written from the description in research/05-graphics-composition/live-terrain.md 8.10 and 8.10.1
// (D83). The step numbers in the comments are 8.10.1's.
#include "c5r/liveterrain/coastlines.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace c5r { namespace liveterrain {

namespace {

std::string num(int64_t v) { return std::to_string(v); }

// ------------------------------------------------ geometry helpers
struct Cube { int32_t q = 0, r = 0, s = 0; };
// the six directions in the EXE's order (frames paragraph)
const int kDir[6][3] = {{0, 1, -1}, {-1, 0, 1}, {1, -1, 0}, {0, -1, 1}, {1, 0, -1}, {-1, 1, 0}};
// the corner directions of an owned edge d (p near point a, q near point b)
const int kP[3] = {4, 5, 3};
const int kQ[3] = {5, 3, 4};

struct V2 { float x = 0, y = 0; };

// a 16-byte point of the polylines and the coast mesh
struct MPt { float x = 0, y = 0, t = 0; uint8_t a = 0, b = 0; };

// the 3-point derivative with weights (f - 1, 1 - 2f, f) on 2D points, summed p0 c0 + c1 p1 + c2 p2 (x) and
// p1 c1 + p0 c0 + p2 c2 (y)
V2 deriv3(const V2& p0, const V2& p1, const V2& p2, float f) {
    const float c0 = 1.0f * f + -1.0f, c1 = 1.0f + -2.0f * f, c2 = 1.0f * f + 0.0f;
    V2 o; o.x = p0.x * c0 + c1 * p1.x + c2 * p2.x; o.y = p1.y * c1 + p0.y * c0 + p2.y * c2;
    return o;
}

// MSVC CRT rand
struct Crt {
    uint32_t s; uint64_t calls = 0;
    explicit Crt(uint32_t seed) : s(seed) {}
    int rand() { s = s * 214013u + 2531011u; ++calls; return int((s >> 16) & 0x7FFFu); }
};

// classes and kinds of a pattern-cell flag word (steps 1 and 7.3)
uint8_t cellClass(uint32_t f) {
    if (f & 0xF3EF9FFFu) return 0;
    if (f & 0x0C102000u) return 1;
    return 2;
}
uint8_t cellKind(uint32_t f) {
    if (f & 0x400u) return 11;
    if (f & 0x1u) return 1;
    if (f & 0x20u) return 2;
    if (f & 0x2u) return 3;
    if (f & 0x40u) return 4;
    if (f & 0x10u) return 5;
    if (f & 0x200u) return 6;
    if (f & 0x4u) return 7;
    if (f & 0x80u) return 8;
    if (f & 0x8u) return 9;
    if (f & 0x100u) return 10;
    if ((f & 0xF3EF9FFFu) == 0 && (f & 0x106000u)) return 12;
    return 0;
}

bool sameType(int code, int ref) { return (code < 12) == (ref < 12); }
int opp(int d) { return (d + 3) % 6; }

// ------------------------------------------------ the coast grid (steps 1-4)
struct Link { int slot = -1; int x = 0, y = 0; };   // target cell (unwrapped offset coordinates) and slot
struct Cell {
    bool exists = false;
    uint8_t cls = 2, kind = 0;
    bool mountain = false;
    int nOut = 0, nIn = 0;
    int code[3] = {0, 0, 0};
    uint8_t edgeKind[3] = {0, 0, 0};
    int inCode[3] = {0, 0, 0};
    V2 a[3], b[3];
    Link next[3], prev[3];
    bool used[3] = {false, false, false};
};

struct Grid {
    const MapFrame& fr;
    int gw, gh;
    std::vector<Cell> cells;   // index y * gw + x
    Cell none;                 // the EXE's default cell: no edges
    float h2w[16];
    int32_t G[16], Gi[16];
    float dx = 0;              // the map's width in samples
    V2 offA[3], offB[3];       // a_d and b_d

    explicit Grid(const MapFrame& f) : fr(f), gw(f.gridW()), gh(f.gridH()) {
        for (int i = 0; i < 16; ++i) { h2w[i] = f.h2w[i]; G[i] = f.G[i]; Gi[i] = f.Ginv[i]; }
        cells.assign(size_t(gw) * gh, Cell());
    }
    Cube cellCube(int x, int y) const {
        const int32_t q = x - (y >> 1), r = y, s = -q - r;
        Cube c;
        c.q = q * Gi[0] + r * Gi[4] + s * Gi[8] + Gi[12];
        c.r = q * Gi[1] + r * Gi[5] + s * Gi[9] + Gi[13];
        c.s = -(c.q + c.r);
        return c;
    }
    void cubeToCell(const Cube& c, int& x, int& y) const {
        const int32_t v0 = c.q * G[0] + c.r * G[4] + c.s * G[8] + G[12];
        const int32_t v1 = c.q * G[1] + c.r * G[5] + c.s * G[9] + G[13];
        y = v1; x = v0 + (v1 >> 1);
    }
    bool exists(int x, int y) const { return y >= 0 && y < gh && (fr.wrapX || (x >= 0 && x < gw)); }
    int wrapX(int x) const { if (!fr.wrapX) return x; while (x < 0) x += gw; return x % gw; }
    Cell& at(int x, int y) { return exists(x, y) ? cells[size_t(y) * gw + size_t(wrapX(x))] : none; }
    const Cell& at(int x, int y) const { return exists(x, y) ? cells[size_t(y) * gw + size_t(wrapX(x))] : none; }
    const Cell& atCube(const Cube& c) const { int x, y; cubeToCell(c, x, y); return at(x, y); }
    V2 toSample(float q, float r, float s) const {
        V2 o;
        o.x = q * h2w[0] + r * h2w[4] + s * h2w[8] + 1.0f * h2w[12];
        o.y = q * h2w[1] + r * h2w[5] + s * h2w[9] + 1.0f * h2w[13];
        return o;
    }
    V2 centre(const Cube& c) const { return toSample(float(c.q), float(c.r), float(c.s)); }
    // x shift of a cell given by unwrapped x: k * dx
    float seamShift(int xu) const {
        if (!fr.wrapX) return 0.f;
        int xw = xu; while (xw < 0) xw += gw; xw %= gw;
        return float((xu - xw) / gw) * dx;
    }
};

// step 2's fixed offsets a_d, b_d and the width dx
void makeOffsets(Grid& g) {
    const int32_t Tq = g.Gi[12], Tr = g.Gi[13], Ts = -(Tq + Tr);
    const V2 o = g.toSample(float(Tq), float(Tr), float(Ts));
    V2 n[6];
    for (int i = 0; i < 6; ++i) {
        const V2 h = g.toSample(float(Tq + kDir[i][0]), float(Tr + kDir[i][1]), float(Ts + kDir[i][2]));
        n[i].x = -o.x + h.x; n[i].y = -o.y + h.y;
    }
    const float third = 0.333333343f;
    auto corner = [&](int i, int j) { V2 c; c.x = (n[i].x + n[j].x) * third; c.y = (n[i].y + n[j].y) * third; return c; };
    for (int d = 0; d < 3; ++d) {
        const V2 cp = corner(d, kP[d]), cq = corner(d, kQ[d]);
        g.offA[d].x = cq.x * 0.25f + cp.x * 0.75f; g.offA[d].y = cq.y * 0.25f + cp.y * 0.75f;
        g.offB[d].x = cq.x * 0.75f + cp.x * 0.25f; g.offB[d].y = cq.y * 0.75f + cp.y * 0.25f;
    }
    // width: M_h2w(Ginv (W, 0, -W)) - M_h2w(T)
    const int32_t W = g.gw;
    const int32_t X = W * g.Gi[0] + (-W) * g.Gi[8] + g.Gi[12];
    const int32_t Y = W * g.Gi[1] + (-W) * g.Gi[9] + g.Gi[13];
    const V2 hw = g.toSample(float(X), float(Y), float(-(X + Y)));
    g.dx = -o.x + hw.x;
}

// steps 1 and 2
void buildEdges(Grid& g, const std::vector<uint32_t>& flags) {
    for (int y = 0; y < g.gh; ++y) for (int x = 0; x < g.gw; ++x) {
        Cell& c = g.cells[size_t(y) * g.gw + x];
        const uint32_t f = flags[size_t(y) * g.gw + x];
        c.exists = true; c.cls = cellClass(f); c.kind = cellKind(f); c.mountain = (f & 0x400u) != 0;
    }
    for (int y = 0; y < g.gh; ++y) for (int x = 0; x < g.gw; ++x) {
        Cell& c = g.cells[size_t(y) * g.gw + x];
        const Cube cc = g.cellCube(x, y);
        const V2 ctr = g.centre(cc);
        for (int d = 0; d < 6; ++d) {
            Cube nc; nc.q = cc.q + kDir[d][0]; nc.r = cc.r + kDir[d][1]; nc.s = cc.s + kDir[d][2];
            int nx, ny; g.cubeToCell(nc, nx, ny);
            if (!g.exists(nx, ny)) continue;
            const Cell& n = g.at(nx, ny);
            int code = -1; uint8_t k = 0;
            if (c.cls == 0 && n.cls == 1) { code = d; k = c.kind; }
            else if (c.cls == 1 && n.cls == 0) { code = d + 6; k = n.kind; }
            else if (c.cls == 1 && n.cls == 2) { code = d + 12; k = 12; }
            else if (c.cls == 2 && n.cls == 1) { code = d + 18; k = 12; }
            if (code < 0) continue;
            if (d < 3) {
                const int s = c.nOut++;
                c.code[s] = code; c.edgeKind[s] = k;
                c.a[s].x = g.offA[d].x + ctr.x; c.a[s].y = g.offA[d].y + ctr.y;
                c.b[s].x = g.offB[d].x + ctr.x; c.b[s].y = g.offB[d].y + ctr.y;
            } else {
                c.inCode[c.nIn++] = code;
            }
        }
    }
}

// step 3: the edge that continues edge (cube c, slot) at one corner; mode 1 = the q-corner, mode 0 = the p-corner
bool linkAt(const Grid& g, const Cube& cc, int slot, int mode, Cube& outCube, int& outSlot) {
    const Cell& c = g.atCube(cc);
    const int code = c.code[slot];
    const int d = code % 3;
    int pa = kP[d], pb = kQ[d];
    if (code > 11) { pa += 12; pb += 12; }
    const int da = pa % 6, db = pb % 6;
    Cube n1{cc.q + kDir[da][0], cc.r + kDir[da][1], cc.s + kDir[da][2]};
    Cube n2{cc.q + kDir[db][0], cc.r + kDir[db][1], cc.s + kDir[db][2]};
    Cube n3{cc.q + kDir[da][0] + kDir[db][0], cc.r + kDir[da][1] + kDir[db][1], cc.s + kDir[da][2] + kDir[db][2]};
    const Cell& N1 = g.atCube(n1);
    const Cell& N2 = g.atCube(n2);
    const Cell& N3 = g.atCube(n3);
    if (mode == 1) {
        for (int i = 0; i < c.nIn; ++i) {
            const int ic = c.inCode[i];
            if (!sameType(ic, pb) || ic % 6 != db) continue;
            for (int s = 0; s < N2.nOut; ++s)
                if (sameType(N2.code[s], pb) && N2.code[s] % 6 == opp(db)) { outCube = n2; outSlot = s; return true; }
        }
        for (int s = 0; s < N3.nOut; ++s)
            if (sameType(N3.code[s], pa) && N3.code[s] % 6 == opp(da)) { outCube = n3; outSlot = s; return true; }
    } else {
        for (int i = 0; i < c.nIn; ++i) {
            const int ic = c.inCode[i];
            if (!sameType(ic, pa) || ic % 6 != da) continue;
            for (int s = 0; s < N1.nOut; ++s)
                if (sameType(N1.code[s], pa) && N1.code[s] % 6 == opp(da)) { outCube = n1; outSlot = s; return true; }
        }
        for (int s = 0; s < N3.nOut; ++s)
            if (sameType(N3.code[s], pb) && N3.code[s] % 6 == opp(db)) { outCube = n3; outSlot = s; return true; }
    }
    return false;
}

bool reversedCode(int code) { return code % 12 > 5; }

void buildLinks(Grid& g) {
    for (int y = 0; y < g.gh; ++y) for (int x = 0; x < g.gw; ++x) {
        Cell& c = g.cells[size_t(y) * g.gw + x];
        const Cube cc = g.cellCube(x, y);
        for (int s = 0; s < c.nOut; ++s) {
            const bool rev = reversedCode(c.code[s]);
            Cube t; int ts;
            if (linkAt(g, cc, s, rev ? 0 : 1, t, ts)) { int tx, ty; g.cubeToCell(t, tx, ty); c.next[s].slot = ts; c.next[s].x = tx; c.next[s].y = ty; }
            if (linkAt(g, cc, s, rev ? 1 : 0, t, ts)) { int tx, ty; g.cubeToCell(t, tx, ty); c.prev[s].slot = ts; c.prev[s].x = tx; c.prev[s].y = ty; }
        }
    }
}

// step 4: the six-point window of an edge in walking order
void window6(const Grid& g, int x, int y, int s, V2 P[6]) {
    const Cell& c = g.cells[size_t(y) * g.gw + x];
    const V2 a = c.a[s], b = c.b[s];
    const float ex = a.x + -b.x, ey = a.y + -b.y;
    const float hx = ex * 0.5f, hy = ey * 0.5f;
    P[0].x = a.x + ex; P[0].y = a.y + ey;
    P[1].x = a.x + hx; P[1].y = hy + a.y;
    P[2] = a; P[3] = b;
    P[4].x = b.x - hx; P[4].y = b.y - hy;
    P[5].x = b.x - ex; P[5].y = b.y - ey;
    const Cube cc = g.cellCube(x, y);
    Cube t; int ts;
    if (linkAt(g, cc, s, 1, t, ts)) {
        int tx, ty; g.cubeToCell(t, tx, ty);
        const Cell& n = g.at(tx, ty);
        const float k = g.seamShift(tx);
        P[4].x = n.b[ts].x + k; P[4].y = n.b[ts].y;
        P[5].x = n.a[ts].x + k; P[5].y = n.a[ts].y;
    }
    if (linkAt(g, cc, s, 0, t, ts)) {
        int tx, ty; g.cubeToCell(t, tx, ty);
        const Cell& n = g.at(tx, ty);
        const float k = g.seamShift(tx);
        P[0].x = n.b[ts].x + k; P[0].y = n.b[ts].y;
        P[1].x = n.a[ts].x + k; P[1].y = n.a[ts].y;
    }
    if (reversedCode(c.code[s])) { std::swap(P[0], P[5]); std::swap(P[1], P[4]); std::swap(P[2], P[3]); }
}

// MC #9538 item 1 (live-terrain.md 8.10.3): 1 / |t| of a zero tangent is not a number the port may use. Each step that
// divides by a length tests the reciprocal and, when it is not finite, leaves the point unmoved and counts the case.
// The test is on the result, so a finite input takes exactly the operations it took before (the exact-port numbers).
inline bool usable(float inv) { return std::isfinite(inv); }

uint32_t smoothPasses(Grid& g) {
    uint32_t bad = 0;
    static const int kPasses[10] = {1, 2, 3, 2, 1, 2, 3, 2, 1, 2};
    std::vector<V2> sa(g.cells.size() * 3), sb(g.cells.size() * 3);
    for (int pass : kPasses) {
        for (int y = 0; y < g.gh; ++y) for (int x = 0; x < g.gw; ++x) {
            Cell& c = g.cells[size_t(y) * g.gw + x];
            const size_t base = (size_t(y) * g.gw + x) * 3;
            for (int s = 0; s < c.nOut; ++s) {
                if (pass == 2) { c.a[s] = sa[base + s]; c.b[s] = sb[base + s]; continue; }
                V2 P[6]; window6(g, x, y, s, P);
                const bool rev = reversedCode(c.code[s]);
                if (pass == 1) {
                    V2 f, h;
                    f.x = P[1].x * 0.25f + P[2].x * 0.5f + P[3].x * 0.25f;
                    f.y = P[1].y * 0.25f + P[2].y * 0.5f + P[3].y * 0.25f;
                    h.x = P[2].x * 0.25f + P[3].x * 0.5f + P[4].x * 0.25f;
                    h.y = P[2].y * 0.25f + P[3].y * 0.5f + P[4].y * 0.25f;
                    sa[base + s] = rev ? h : f; sb[base + s] = rev ? f : h;
                } else {
                    V2 na = c.a[s], nb = c.b[s];
                    if (c.mountain) {
                        // tangents (P3 - P1) / 2 and (P4 - P2) / 2 as the 3-point derivative at 1.5 and 2.5
                        const V2 t1 = deriv3(P[1], P[2], P[3], 0.5f), t2 = deriv3(P[2], P[3], P[4], 0.5f);
                        const float mx1 = -t1.x, mx2 = -t2.x;
                        const float i1 = 1.0f / std::sqrt(t1.y * t1.y + mx1 * mx1);
                        const float i2 = 1.0f / std::sqrt(t2.y * t2.y + mx2 * mx2);
                        if (!usable(i1) || !usable(i2)) ++bad;   // a zero tangent: no push (8.10.3)
                        else {
                            const V2 n1{i1 * t1.y, i1 * mx1}, n2{i2 * t2.y, i2 * mx2};
                            const V2& nb_ = rev ? n2 : n1;   // b gets n1 forward, n2 reversed (as read)
                            const V2& na_ = rev ? n1 : n2;
                            nb.y = nb_.y * 4.0f + nb.y; nb.x = nb.x + nb_.x * 4.0f;
                            na.x = na.x + na_.x * 4.0f; na.y = na_.y * 4.0f + na.y;
                        }
                    }
                    sa[base + s] = na; sb[base + s] = nb;
                }
            }
        }
    }
    return bad;
}

// ------------------------------------------------ step 5: extraction
struct Poly {
    std::vector<MPt> p;
    int type = 0; bool closed = false; float wrapX = 0, wrapY = 0;
    int sx = 0, sy = 0, ss = 0;
};

void appendEdge(const Grid& g, const Cell& c, int s, float shift, std::vector<MPt>& out, int& lastKind, float& flag,
                uint8_t& prevByte) {
    const bool rev = reversedCode(c.code[s]);
    const V2 p0 = rev ? c.b[s] : c.a[s], p1 = rev ? c.a[s] : c.b[s];
    const uint8_t k = c.edgeKind[s];
    for (int i = 0; i < 2; ++i) {
        if (lastKind != int(k)) { flag = (lastKind < 0) ? 1.0f : (flag == 1.0f ? 0.0f : 1.0f); lastKind = k; }
        MPt m; m.x = (i == 0 ? p0.x : p1.x) + shift; m.y = i == 0 ? p0.y : p1.y; m.t = flag;
        m.a = k; m.b = prevByte; prevByte = k;
        out.push_back(m);
    }
    (void)g;
}

bool trace(Grid& g, int sx, int sy, int ss, Poly& out) {
    out = Poly();
    out.sx = sx; out.sy = sy; out.ss = ss;
    Cell& start = g.cells[size_t(sy) * g.gw + sx];
    int lastKind = -1; float flag = 0.f; uint8_t prevByte = 0;
    appendEdge(g, start, ss, 0.f, out.p, lastKind, flag, prevByte);
    const bool rev = reversedCode(start.code[ss]);
    if (!rev) out.type = start.cls == 0 ? 0 : 1; else out.type = start.cls == 1 ? 0 : 1;
    Cell* cur = &start; int curSlot = ss;
    float shift = 0.f;
    int lastX = sx, lastY = sy, lastSlot = ss;
    while (cur->next[curSlot].slot >= 0 && !cur->used[curSlot]) {
        const Link L = cur->next[curSlot];
        int xw = L.x, k = 0;
        if (xw < 0) { while (xw < 0) { xw += g.gw; --k; } }
        if (xw >= g.gw) k = xw / g.gw;
        if (!g.fr.wrapX) k = 0;
        shift = float(k) * g.dx + shift;
        Cell& n = g.at(L.x, L.y);
        appendEdge(g, n, L.slot, shift, out.p, lastKind, flag, prevByte);
        cur->used[curSlot] = true;
        cur = &n; curSlot = L.slot;
        lastX = L.x; lastY = L.y; lastSlot = L.slot;
    }
    if (g.wrapX(lastX) == sx && lastY == sy && lastSlot == ss && out.p.size() >= 4) {
        out.closed = true;
        const size_t n = out.p.size();
        out.wrapX = -out.p[1].x + out.p[n - 1].x;
        out.wrapY = -out.p[1].y + out.p[n - 1].y;
    }
    return out.p.size() >= 4;
}

// ------------------------------------------------ step 6: the jitter on the reverse-Chaikin pyramid
struct Pyr {
    int L = 0, base = 0, count = 0;
    std::vector<V2> d;
};

void decimate(Pyr& P) {
    const int n = P.count, m = n / 2 + 1;
    int src, det, nb;
    if (P.L % 2 == 0) { src = 0; det = n + m; nb = n; }
    else { src = 2 * n - 2; det = n + 2; nb = 0; }
    std::vector<V2> s(P.d.begin() + src, P.d.begin() + src + n);
    for (int j = 0; j + 3 < n && j / 2 <= m - 3; j += 2) {
        V2 D;
        D.x = s[j].x * 0.25f + s[j + 1].x * -0.75f + s[j + 2].x * 0.75f + s[j + 3].x * -0.25f;
        D.y = s[j].y * 0.25f + s[j + 1].y * -0.75f + s[j + 2].y * 0.75f + s[j + 3].y * -0.25f;
        P.d[size_t(det + j / 2)] = D;
    }
    V2 C;
    C.x = s[0].x * 1.5f + s[1].x * -0.5f; C.y = s[0].y * 1.5f + s[1].y * -0.5f;
    P.d[size_t(nb)] = C;
    for (int j = 0; j + 3 < n && j / 2 <= m - 3; j += 2) {
        C.x = s[j].x * -0.25f + s[j + 1].x * 0.75f + s[j + 2].x * 0.75f + s[j + 3].x * -0.25f;
        C.y = s[j].y * -0.25f + s[j + 1].y * 0.75f + s[j + 2].y * 0.75f + s[j + 3].y * -0.25f;
        P.d[size_t(nb + 1 + j / 2)] = C;
    }
    C.x = s[n - 2].x * -0.5f + s[n - 1].x * 1.5f; C.y = s[n - 2].y * -0.5f + s[n - 1].y * 1.5f;
    P.d[size_t(nb + m - 1)] = C;
    P.L -= 1; P.base = nb; P.count = m;
}

void reconstruct(Pyr& P) {
    const int L2 = P.L + 1, m = P.count, u1 = 2 * m - 2;
    int cb, db, dst;
    if (L2 % 2 == 0) { cb = u1; db = 3 * m - 2; dst = 0; }
    else { cb = 0; db = 2 * m; dst = 4 * m - 6; }
    const std::vector<V2> c(P.d.begin() + cb, P.d.begin() + cb + m);
    const std::vector<V2> D(P.d.begin() + db, P.d.begin() + db + (m - 2));
    std::vector<V2> F(static_cast<size_t>(u1));
    const int nd = m - 2;
    F[0].x = D[0].x * -0.25f; F[0].y = D[0].y * -0.25f;
    F[1].x = D[0].x * -0.75f; F[1].y = D[0].y * -0.75f;
    for (int i = 1; i < nd; ++i) {
        F[size_t(2 * i)].x = D[size_t(i - 1)].x * 0.75f + D[size_t(i)].x * -0.25f;
        F[size_t(2 * i)].y = D[size_t(i - 1)].y * 0.75f + D[size_t(i)].y * -0.25f;
        F[size_t(2 * i + 1)].x = D[size_t(i - 1)].x * 0.25f + D[size_t(i)].x * -0.75f;
        F[size_t(2 * i + 1)].y = D[size_t(i - 1)].y * 0.25f + D[size_t(i)].y * -0.75f;
    }
    F[size_t(2 * nd)].x = D[size_t(nd - 1)].x * 0.75f; F[size_t(2 * nd)].y = D[size_t(nd - 1)].y * 0.75f;
    F[size_t(2 * nd + 1)].x = D[size_t(nd - 1)].x * 0.25f; F[size_t(2 * nd + 1)].y = D[size_t(nd - 1)].y * 0.25f;
    for (int j = m - 2; j >= 0; --j) {
        V2& o = F[size_t(2 * j + 1)];
        o.x = (c[size_t(j + 1)].x * 0.75f + c[size_t(j)].x * 0.25f) + o.x;
        o.y = (c[size_t(j + 1)].y * 0.75f + c[size_t(j)].y * 0.25f) + o.y;
        V2& e = F[size_t(2 * j)];
        e.x = (c[size_t(j + 1)].x * 0.25f + c[size_t(j)].x * 0.75f) + e.x;
        e.y = (c[size_t(j + 1)].y * 0.25f + c[size_t(j)].y * 0.75f) + e.y;
    }
    for (int i = 0; i < u1; ++i) P.d[size_t(dst + i)] = F[size_t(i)];
    P.L = L2; P.base = dst; P.count = u1;
}

uint32_t jitter(Poly& L, Crt& rng, uint32_t& bad) {
    const uint32_t n = uint32_t(L.p.size());
    uint32_t m = n; int k = 0;
    while (m >= 6 && k < 4) { if (m & 1) ++m; m = m / 2 + 1; ++k; }
    if (k == 0) return 0;
    const uint32_t total = (2u << (k - 1)) * (m - 2) + 2;
    Pyr P; P.count = int(total); P.d.assign(size_t(total) * 2 + 1, V2());
    for (uint32_t i = 0; i < n; ++i) { P.d[i].x = L.p[i].x; P.d[i].y = L.p[i].y; }
    for (uint32_t i = n; i < total; ++i) P.d[i] = P.d[n - 1];
    for (int r = 0; r < k; ++r) decimate(P);
    float A = float(std::pow(double(0.78f), double(5 - k)));
    uint32_t draws = 0;
    for (int r = 0; r < k; ++r) {
        reconstruct(P);
        V2* b = &P.d[size_t(P.base)];
        const int c = P.count;
        auto move = [&](int i, const V2& t, float d) {
            const float mtx = -t.x;
            const float sq = mtx * mtx;
            const float len = float(std::sqrt(double(t.y) * double(t.y) + double(sq)));
            const float inv = 1.0f / len;
            if (!usable(inv)) { ++bad; return; }   // a zero tangent: the point stays (its draw is made all the same)
            const float nx = t.y * inv, ny = inv * mtx;
            b[i].y = ny * d + b[i].y;
            b[i].x = nx * d + b[i].x;
        };
        {   // first point
            const int rv = rng.rand(); ++draws;
            const float d = ((float(rv % 1000) * 0.001f - 0.5f) * A) * 35.0f;
            move(0, deriv3(b[0], b[1], b[2], 0.0f), d);
        }
        for (int i = 1; i <= c - 2; ++i) {
            const int rv = rng.rand(); ++draws;
            const float d = ((float(rv % 1000) * 0.001f - 0.5f) * A) * 35.0f;
            move(i, deriv3(b[i - 1], b[i], b[i + 1], 0.5f), d);
        }
        {   // last point: a draw, but the move is A
            rng.rand(); ++draws;
            move(c - 1, deriv3(b[c - 3], b[c - 2], b[c - 1], 1.0f), A);
        }
        A = A * A;
    }
    for (uint32_t i = 0; i < n; ++i) { L.p[i].x = P.d[size_t(P.base) + i].x; L.p[i].y = P.d[size_t(P.base) + i].y; }
    if (L.closed) {
        L.p[0].x = L.p[n - 2].x - L.wrapX; L.p[0].y = L.p[n - 2].y - L.wrapY;
        L.p[1].x = L.p[n - 1].x - L.wrapX; L.p[1].y = L.p[n - 1].y - L.wrapY;
    }
    return draws;
}

// ------------------------------------------------ step 7: the coast mesh's five lines

// 3-point derivative / value of 16-byte points at parameter i + f (pointer already placed)
void d3(const MPt* p, float f, float& tx, float& ty) {
    const float c0 = 1.0f * f + -1.0f, c1 = -2.0f * f + 1.0f, c2 = 1.0f * f + 0.0f;
    tx = p[1].x * c1 + p[0].x * c0 + p[2].x * c2;
    ty = p[1].y * c1 + p[0].y * c0 + p[2].y * c2;
}
void v3(const MPt* p, float f, float& t, uint8_t& ka, uint8_t& kb) {
    const float f2 = f * f;
    const float c0 = 0.5f * f2 + -1.0f * f + 0.5f, c1 = -1.0f * f2 + 1.0f * f + 0.5f, c2 = 0.5f * f2 + 0.0f * f + 0.0f;
    t = p[1].t * c1 + p[0].t * c0 + p[2].t * c2;
    ka = p[0].a; kb = p[0].b;
}

std::vector<MPt> chaikin(const std::vector<MPt>& s) {
    const size_t n = s.size();
    std::vector<MPt> o(2 * n - 2);
    for (size_t j = 0; j + 1 < n; ++j) {
        const MPt& p = s[j]; const MPt& q = s[j + 1];
        MPt& e = o[2 * j]; MPt& f = o[2 * j + 1];
        e.x = q.x * 0.25f + p.x * 0.75f; e.y = q.y * 0.25f + p.y * 0.75f; e.t = q.t * 0.25f + p.t * 0.75f;
        f.x = q.x * 0.75f + p.x * 0.25f; f.y = q.y * 0.75f + p.y * 0.25f; f.t = q.t * 0.75f + p.t * 0.25f;
        e.a = f.a = q.a; e.b = f.b = q.b;
    }
    return o;
}

void meshLines(const Poly& L, const CoastStyles& st, std::vector<MPt> out[5], uint32_t& bad) {
    const std::vector<MPt>& P = L.p;
    const size_t n = P.size();
    for (int k = 0; k < 5; ++k) out[k].assign(n, MPt());
    int lastKind = -1; bool par = true;
    const CoastStyle* A = &st.lookup(-1); const CoastStyle* B = A;
    auto offsetPoint = [&](const MPt& src, float tx, float ty, float tval, uint8_t ka, uint8_t kb, size_t slot, bool closedForm) {
        if (int(ka) != lastKind) { A = &st.lookup(ka); B = &st.lookup(kb); par = !par; lastKind = ka; }
        const float w = par ? 1.0f - tval : tval;
        const float w1 = 1.0f - w;
        const float mtx = -tx;
        const float inv = float(1.0 / std::sqrt(double(mtx) * mtx + double(ty) * ty));
        if (!usable(inv)) {   // a zero tangent: the five lines take the point itself (8.10.3)
            ++bad;
            for (int k = 0; k < 5; ++k) out[k][slot] = src;
            return;
        }
        for (int k = 0; k < 5; ++k) {
            const float off = A->offset[k] * w + B->offset[k] * w1;
            MPt m = src;
            if (!closedForm) { m.x = off * inv * ty + src.x; m.y = off * inv * mtx + src.y; }
            else { const float ny = inv * ty; m.x = off * ny + src.x; m.y = off * inv * mtx + src.y; }
            out[k][slot] = m;
        }
    };
    if (!L.closed) {
        for (size_t j = 0; j < n; ++j) {
            float tx, ty, tv; uint8_t ka, kb;
            if (j == 0) { d3(&P[0], 0.0f, tx, ty); v3(&P[0], 0.0f, tv, ka, kb); }
            else if (j == n - 1) {
                const float tp = float(int(n) - 2) - 0.01f;
                const float f = tp - float(std::floor(double(tp)));
                d3(&P[n - 3], f, tx, ty); v3(&P[n - 3], f, tv, ka, kb);
            } else { d3(&P[j - 1], 0.5f, tx, ty); v3(&P[j - 1], 0.5f, tv, ka, kb); }
            offsetPoint(P[j], tx, ty, tv, ka, kb, j, false);
        }
    } else {
        for (size_t j = 0; j + 1 < n; ++j) {
            float tx, ty, tv; uint8_t ka, kb;
            if (j + 2 >= n) { d3(&P[j - 1], 1.0f, tx, ty); v3(&P[j - 1], 1.0f, tv, ka, kb); }
            else { d3(&P[j], 0.5f, tx, ty); v3(&P[j], 0.5f, tv, ka, kb); }
            offsetPoint(P[j + 1], tx, ty, tv, ka, kb, j + 1, true);
        }
        for (int k = 0; k < 5; ++k) {
            std::vector<MPt>& v = out[k];
            v[0].x = v[n - 2].x - L.wrapX; v[0].y = v[n - 2].y - L.wrapY; v[0].t = v[n - 2].t;
            v[0].a = v[n - 2].a; v[0].b = v[n - 2].b;   // not written by the EXE [I]
            v[n - 1].x = v[1].x + L.wrapX; v[n - 1].y = v[1].y + L.wrapY; v[n - 1].t = v[1].t;
        }
    }
    for (int k = 0; k < 5; ++k) { out[k] = chaikin(out[k]); out[k] = chaikin(out[k]); }
    // second pass: character offsets
    lastKind = -1; par = true;
    for (int k = 0; k < 5; ++k) {
        std::vector<MPt>& v = out[k];
        const size_t c = v.size();
        for (size_t j = 0; j + 1 < c; ++j) {
            float tx, ty;
            if (j + 2 >= c) d3(&v[j - 1], 1.0f, tx, ty); else d3(&v[j], 0.5f, tx, ty);
            const float inv = float(1.0 / std::sqrt(double(tx) * tx + double(ty) * ty));
            const float ux = inv * tx, uy = ty * inv, nuy = -uy;
            if (int(v[j].a) != lastKind) { A = &st.lookup(v[j].a); B = &st.lookup(v[j].b); par = !par; lastKind = v[j].a; }
            if (j == 0) continue;
            if (!usable(inv)) { ++bad; continue; }   // a zero tangent: no character offset for this point (8.10.3)
            const float w = par ? 1.0f - v[j].t : v[j].t;
            float ax = 0, ay = 0, bx = 0, by = 0;
            const int ca = A->character[k], cb = B->character[k];
            if (ca != -1 && size_t(ca) < st.characters.size() && !st.characters[size_t(ca)].empty()) {
                const auto& T = st.characters[size_t(ca)];
                const CoastPoint e = T[j % T.size()];
                ay = e.y * ux + uy * e.x;
                ax = e.x * ux + e.y * nuy;
            }
            if (cb != -1 && size_t(cb) < st.characters.size() && !st.characters[size_t(cb)].empty()) {
                const auto& T = st.characters[size_t(cb)];
                const CoastPoint e = T[j % T.size()];
                bx = e.x * ux + e.y * nuy;
                by = e.y * ux + uy * e.x;
            }
            const float w1 = 1.0f - w;
            v[j].x = ax * w + w1 * bx + v[j].x;
            v[j].y = v[j].y + ay * w + w1 * by;
        }
        if (L.closed) {
            v[0].x = v[c - 2].x - L.wrapX; v[0].y = v[c - 2].y - L.wrapY; v[0].t = v[c - 2].t;
            v[c - 1].x = v[1].x + L.wrapX; v[c - 1].y = v[1].y + L.wrapY; v[c - 1].t = v[1].t;
        }
    }
    for (int k = 0; k < 5; ++k) out[k] = chaikin(out[k]);
}

// ------------------------------------------------ step 8: offset lines and cleanup
double segDist(const V2& a, const V2& b, const V2& p) {
    // the differences are f32 (as the EXE keeps them), the products and roots wider
    const double vx = float(b.x + -a.x), vy = float(b.y + -a.y);
    const double w1x = float(p.x + -b.x), w1y = float(p.y + -b.y);
    const double w0x = float(p.x + -a.x), w0y = float(p.y + -a.y);
    if ((a.x != b.x || a.y != b.y) && vy * w1y + w1x * vx < 0.0) {
        if (0.0 <= w0y * -vy + w0x * -vx) return std::sqrt(w0y * w0y + w0x * w0x);
        return std::fabs((w0y * vx - vy * w0x) / std::sqrt(vy * vy + vx * vx));
    }
    return std::sqrt(w1y * w1y + w1x * w1x);
}

bool segCross(const V2& a0, const V2& a1, const V2& b0, const V2& b1, V2& X, float eps) {
    const float f6 = a0.x - a1.x, f5 = a1.y - a0.y;
    const float f1 = a0.x * f5 + a0.y * f6;
    const float f3 = b1.y - b0.y, f2 = b0.x - b1.x;
    const float f4 = b0.x * f3 + b0.y * f2;
    float det = f2 * f5 - f3 * f6;
    if (!(eps <= std::fabs(det))) return false;
    det = 1.0f / det;
    const float xx = (f2 * f1 - f4 * f6) * det;
    const float yy = (f4 * f5 - f3 * f1) * det;
    X.x = xx; X.y = yy;
    auto hiLo = [&](float p0, float p1, float q0, float q1, float v) {
        const float maxP = std::max(p0, p1), maxQ = std::max(q0, q1);
        const float lo = std::min(maxP, maxQ);
        if (!(v <= lo + eps)) return false;
        const float minP = std::min(p0, p1), minQ = std::min(q0, q1);
        const float hi = std::max(minP, minQ);
        return hi - eps <= v;
    };
    return hiLo(a0.x, a1.x, b0.x, b1.x, xx) && hiLo(a0.y, a1.y, b0.y, b1.y, yy);
}

void cleanup(std::vector<CoastPoint>& pts, int s, int e) {
    for (int i = s; i < e - 2; ++i) {
        auto P = [&](int k) { V2 v; v.x = pts[size_t(k)].x; v.y = pts[size_t(k)].y; return v; };
        const double d1 = segDist(P(i), P(i + 1), P(i + 2));
        const double d2 = segDist(P(i + 1), P(i + 2), P(i));
        const float mn = float(d2 <= d1 ? d2 : d1);
        if (double(mn) < 0.01) { pts[size_t(i + 1)] = pts[size_t(i + 2)]; continue; }
        const int lim = std::min(i + 50, e - 1);
        for (int j = i + 2; j < lim; ++j) {
            V2 X;
            if (segCross(P(i), P(i + 1), P(j), P(j + 1), X, 1e-4f)) {
                for (int k = j; k > i; --k) { pts[size_t(k)].x = X.x; pts[size_t(k)].y = X.y; }
                break;
            }
        }
    }
}

int truncI(float v) { return int(v); }

} // namespace

// ------------------------------------------------ styles
const CoastStyle& CoastStyles::lookup(int kind) const {
    static const CoastStyle zero;
    if (kind < 0 || kind >= kKinds || !present[kind]) return zero;
    return style[kind];
}

Status CoastStyles::characterTable(const std::string& text, std::vector<CoastPoint>& out) {
    std::vector<V2> r;
    size_t pos = 0;
    while (pos < text.size() && r.size() < 98) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t b = line.find('[');
        if (b == std::string::npos) continue;
        const char* c = line.c_str() + b + 1; char* end = nullptr;
        const float x = std::strtof(c, &end); if (end == c || *end != ',') continue;
        c = end + 1; const float y = std::strtof(c, &end); if (end == c) continue;
        V2 v; v.x = x; v.y = y; r.push_back(v);
    }
    if (r.size() < 98) return Status::fail(Err::BadLayout, "character spline: " + num(int64_t(r.size())) + " points, need 98");
    // two reverse-Chaikin decimations (coarse part only), then two Chaikin rounds
    auto coarse = [](const std::vector<V2>& s) {
        const size_t n = s.size(), m = n / 2 + 1;
        std::vector<V2> C(m);
        C[0].x = s[0].x * 1.5f + s[1].x * -0.5f; C[0].y = s[0].y * 1.5f + s[1].y * -0.5f;
        for (size_t j = 0; j + 2 < m; ++j) {
            const size_t i = 2 * j;
            C[j + 1].x = s[i].x * -0.25f + s[i + 1].x * 0.75f + s[i + 2].x * 0.75f + s[i + 3].x * -0.25f;
            C[j + 1].y = s[i].y * -0.25f + s[i + 1].y * 0.75f + s[i + 2].y * 0.75f + s[i + 3].y * -0.25f;
        }
        C[m - 1].x = s[n - 2].x * -0.5f + s[n - 1].x * 1.5f; C[m - 1].y = s[n - 2].y * -0.5f + s[n - 1].y * 1.5f;
        return C;
    };
    auto chk = [](const std::vector<V2>& s) {
        const size_t n = s.size();
        std::vector<V2> o(2 * n - 2);
        for (size_t j = 0; j + 1 < n; ++j) {
            o[2 * j].x = s[j + 1].x * 0.25f + s[j].x * 0.75f; o[2 * j].y = s[j + 1].y * 0.25f + s[j].y * 0.75f;
            o[2 * j + 1].x = s[j + 1].x * 0.75f + s[j].x * 0.25f; o[2 * j + 1].y = s[j + 1].y * 0.75f + s[j].y * 0.25f;
        }
        return o;
    };
    const std::vector<V2> s = chk(chk(coarse(coarse(r))));   // 98 points
    std::vector<V2> P(100);
    P[0].x = s[0].x - (s[1].x - s[0].x); P[0].y = s[97].y;
    for (size_t i = 0; i < 98; ++i) P[i + 1] = s[i];
    P[99].x = (s[1].x - s[0].x) + s[97].x; P[99].y = s[97].y;
    out.assign(98, CoastPoint());
    for (size_t c = 0; c < 98; ++c) {
        const V2 T = deriv3(P[c], P[c + 1], P[c + 2], 0.5f);
        const float ex = -r[c].x + s[c].x, ey = -r[c].y + s[c].y;
        const float len = float(std::sqrt(double(T.y) * T.y + double(T.x) * T.x));
        const float inv = 1.0f / len;
        if (!usable(inv))   // MC #9538 item 1: a spline file with two equal smoothed neighbours is refused, not used
            return Status::fail(Err::BadLayout, "character spline: point " + num(int64_t(c)) + " has no tangent");
        const float uy = inv * T.y, ux = inv * T.x;
        out[c].x = ex * ux + ey * uy;
        out[c].y = -uy * ex + ey * ux;
    }
    return Status::Ok();
}

namespace {
bool attr(const std::string& el, const char* name, std::string& v) {
    const std::string key = std::string(name) + "=\"";
    size_t p = 0;
    while ((p = el.find(key, p)) != std::string::npos) {
        if (p == 0 || el[p - 1] == ' ' || el[p - 1] == '\t' || el[p - 1] == '\n' || el[p - 1] == '\r') {
            const size_t b = p + key.size(), e = el.find('"', b);
            if (e == std::string::npos) return false;
            v = el.substr(b, e - b); return true;
        }
        p += key.size();
    }
    return false;
}
std::string element(const std::string& body, const char* tag) {
    const std::string open = std::string("<") + tag;
    size_t p = 0;
    while ((p = body.find(open, p)) != std::string::npos) {
        const char c = body[p + open.size()];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/' || c == '>') {
            const size_t e = body.find('>', p);
            if (e == std::string::npos) return std::string();
            return body.substr(p, e - p + 1);
        }
        p += open.size();
    }
    return std::string();
}
std::string stripComments(const std::string& s) {
    std::string o; size_t p = 0;
    while (true) {
        const size_t b = s.find("<!--", p);
        if (b == std::string::npos) { o += s.substr(p); break; }
        o += s.substr(p, b - p);
        const size_t e = s.find("-->", b + 4);
        if (e == std::string::npos) break;
        p = e + 3;
    }
    return o;
}
}

Status CoastStyles::parse(const std::string& xmlIn, const std::function<bool(const std::string&, std::string&)>& readFile,
                          CoastStyles& out) {
    static const char* kNames[kKinds] = {"Default", "Grass", "GrassHill", "Plains", "PlainsHill", "Snow", "SnowHill",
                                         "Desert", "DesertHill", "Tundra", "TundraHill", "Mountain", "DefaultShallows"};
    const std::string xml = stripComments(xmlIn);
    CoastStyles r;
    // the character splines, in list order
    size_t p = 0;
    while ((p = xml.find("<SplineCharacter", p)) != std::string::npos) {
        const size_t e = xml.find('>', p);
        if (e == std::string::npos) break;
        const std::string el = xml.substr(p, e - p + 1);
        p = e;
        if (el.compare(0, 20, "<SplineCharacterList") == 0) continue;
        std::string fn;
        if (!attr(el, "Filename", fn)) return Status::fail(Err::BadLayout, "coast styles: SplineCharacter without Filename");
        std::string text;
        if (!readFile(fn, text)) return Status::fail(Err::NotFound, "coast styles: character spline " + fn);
        std::vector<CoastPoint> t;
        C5R_TRY(characterTable(text, t));
        r.characters.push_back(std::move(t));
    }
    size_t q = 0; int styles = 0;
    while ((q = xml.find("<CoastStyle ", q)) != std::string::npos) {
        const size_t endq = xml.find("</CoastStyle>", q);
        if (endq == std::string::npos) return Status::fail(Err::BadLayout, "coast styles: unterminated CoastStyle");
        const std::string head = xml.substr(q, xml.find('>', q) - q + 1);
        const std::string body = xml.substr(q, endq - q);
        q = endq;
        std::string type;
        if (!attr(head, "type", type)) return Status::fail(Err::BadLayout, "coast styles: CoastStyle without type");
        int kind = -1;
        for (int k = 0; k < kKinds; ++k) if (type == kNames[k]) kind = k;
        const std::string so = element(body, "SmoothingOffset"), hb = element(body, "HighBeach"),
                          lb = element(body, "LowBeach"), wl = element(body, "Waterline");
        if (so.empty() || hb.empty() || lb.empty() || wl.empty())
            return Status::fail(Err::BadLayout, "coast styles: style " + type + " lacks an element");
        auto f = [&](const std::string& el, const char* a, float& v) { std::string s; if (!attr(el, a, s)) return false; v = std::strtof(s.c_str(), nullptr); return true; };
        auto ch = [&](const std::string& el, const char* a, int& v) { std::string s; if (!attr(el, a, s)) return false; const long n = std::strtol(s.c_str(), nullptr, 10); v = (n == 65535) ? -1 : int(n); return true; };
        CoastStyle s;
        float hs = 0, ls = 0, hbH = 0, hbO = 0, lbH = 0, lbO = 0, wH = 0, wO = 0;
        int hbC = -1, lbC = -1, wC = -1, hbT = -1, lbT = -1;
        bool ok = f(so, "HighSide", hs) && f(so, "LowSide", ls) && f(hb, "Height", hbH) && f(hb, "Offset", hbO) &&
                  f(lb, "Height", lbH) && f(lb, "Offset", lbO) && f(wl, "Height", wH) && f(wl, "Offset", wO);
        if (!ok) return Status::fail(Err::BadLayout, "coast styles: style " + type + " lacks a value");
        ch(hb, "Character", hbC); ch(lb, "Character", lbC); ch(wl, "Character", wC);
        ch(hb, "TerrainType", hbT); ch(lb, "TerrainType", lbT);
        s.offset[0] = -hs; s.offset[1] = -hbO; s.offset[2] = wO; s.offset[3] = lbO; s.offset[4] = ls;
        s.flag[0] = 0; s.flag[1] = 1; s.flag[2] = 1; s.flag[3] = 1; s.flag[4] = 0;
        s.height[0] = hbH; s.height[1] = hbH; s.height[2] = wH; s.height[3] = lbH; s.height[4] = lbH;
        s.character[0] = -1; s.character[1] = hbC; s.character[2] = wC; s.character[3] = lbC; s.character[4] = -1;
        s.terrainHigh = hbT; s.terrainLow = lbT;
        for (int k = 1; k < 4; ++k)
            if (s.character[k] >= int(r.characters.size()))
                return Status::fail(Err::BadLayout, "coast styles: style " + type + " names character " + num(s.character[k]));
        if (kind >= 0) { r.style[kind] = s; r.present[kind] = true; }
        ++styles;
    }
    if (styles == 0) return Status::fail(Err::BadLayout, "coast styles: no CoastStyle");
    out = std::move(r);
    return Status::Ok();
}

// ------------------------------------------------ the curves
Status buildCoastCurves(const MapFrame& fr, const PlotTable& plots, const CoastStyles& styles, uint32_t seed,
                        CoastCurves& out) {
    if (fr.plotsW <= 0 || fr.plotsH <= 0) return Status::fail(Err::BadLayout, "coastlines: empty map frame");
    if (fr.plotsW > kMaxPlotsSide || fr.plotsH > kMaxPlotsSide) return Status::fail(Err::Unsupported, "coastlines: map larger than 256 plots");
    if (plots.w != fr.plotsW || plots.h != fr.plotsH || plots.plots.size() != size_t(plots.w) * plots.h)
        return Status::fail(Err::BadLayout, "coastlines: plot table " + num(plots.w) + "x" + num(plots.h) + " for a " +
                            num(fr.plotsW) + "x" + num(fr.plotsH) + " map");
    Grid g(fr);
    // the pattern-cell flags (as the producer builds them)
    std::vector<uint32_t> flags(size_t(g.gw) * g.gh, heightfield::kCellBorder);
    for (int y = 0; y < plots.h; ++y) for (int x = 0; x < plots.w; ++x) {
        const int32_t q = x - (y >> 1), r = y, s = -q - r;
        const int32_t v0 = q * g.G[0] + r * g.G[4] + s * g.G[8] + g.G[12];
        const int32_t v1 = q * g.G[1] + r * g.G[5] + s * g.G[9] + g.G[13];
        const int cy = v1, cx = v0 + (v1 >> 1);
        if (cx < 0 || cx >= g.gw || cy < 0 || cy >= g.gh)
            return Status::fail(Err::BadLayout, "coastlines: plot " + num(x) + "," + num(y) + " maps outside the grid");
        flags[size_t(cy) * g.gw + cx] = heightfield::patternCellFlags(plots.plots[size_t(y) * plots.w + x].base);
    }
    makeOffsets(g);
    buildEdges(g, flags);
    buildLinks(g);
    const uint32_t smoothBad = smoothPasses(g);
    // step 5
    std::vector<Poly> polys;
    for (int pass = 0; pass < 2; ++pass)
        for (int y = 0; y < g.gh; ++y) for (int x = 0; x < g.gw; ++x)
            for (int s = 0; s < 3; ++s) {
                Cell& c = g.cells[size_t(y) * g.gw + x];
                if (s >= c.nOut || c.next[s].slot < 0 || c.used[s]) continue;
                if (pass == 0 && c.prev[s].slot >= 0) continue;
                Poly P;
                if (trace(g, x, y, s, P)) polys.push_back(std::move(P));
            }
    // step 6
    CoastCurves cv;
    Crt rng(seed);
    for (Poly& P : polys) {
        CoastLine L;
        L.type = P.type; L.closed = P.closed; L.startX = P.sx; L.startY = P.sy; L.startSlot = P.ss;
        L.points = uint32_t(P.p.size());
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint32_t v) { for (int b = 0; b < 4; ++b) { h ^= (v >> (8 * b)) & 0xFF; h *= 1099511628211ull; } };
        mix(uint32_t(P.type)); mix(P.closed ? 1u : 0u);
        for (const MPt& q : P.p) { uint32_t bx, by; std::memcpy(&bx, &q.x, 4); std::memcpy(&by, &q.y, 4); mix(bx); mix(by); }
        L.shape = h;
        L.draws = jitter(P, rng, L.degenerate);
        cv.lines.push_back(std::move(L));
    }
    cv.randDraws = rng.calls; cv.randStateAfter = rng.s;
    // step 7
    for (size_t i = 0; i < polys.size(); ++i) {
        std::vector<MPt> lines[5];
        meshLines(polys[i], styles, lines, cv.lines[i].degenerate);
        for (int k = 0; k < 5; ++k) {
            auto& m = cv.lines[i].mesh[k];
            m.resize(lines[k].size());
            for (size_t j = 0; j < lines[k].size(); ++j) {
                m[j].x = lines[k][j].x; m[j].y = lines[k][j].y; m[j].t = lines[k][j].t;
                m[j].styleA = lines[k][j].a; m[j].styleB = lines[k][j].b;
            }
        }
    }
    const uint64_t draws = cv.randDraws; const uint32_t after = cv.randStateAfter;
    C5R_TRY(assembleCurves(std::move(cv.lines), out));
    out.randDraws = draws; out.randStateAfter = after;
    out.degenerateSmooth = smoothBad; out.degenerate += smoothBad;
    return Status::Ok();
}

Status assembleCurves(std::vector<CoastLine> lines, CoastCurves& out) {
    for (const CoastLine& L : lines)
        if (L.mesh[2].size() < 2) return Status::fail(Err::BadLayout, "coastlines: a waterline of fewer than 2 points");
    CoastCurves cv;
    cv.lines = std::move(lines);
    for (const CoastLine& L : cv.lines) { cv.randDraws += L.draws; cv.degenerate += L.degenerate; }
    // step 8: the waterlines, then the offset lines
    int value = 0;
    for (const CoastLine& L : cv.lines) {
        CoastRange R; R.start = int32_t(cv.points.size());
        for (const CoastMeshPoint& m : L.mesh[2]) { CoastPoint c; c.x = m.x; c.y = m.y; cv.points.push_back(c); }
        R.end = int32_t(cv.points.size());
        if (L.type == 0) value = 0; else if (L.type == 1) value = 1;
        R.value = value;
        cv.boundary.push_back(R);
    }
    for (const CoastRange& B : cv.boundary) {
        const int n = B.end - B.start;
        const int t = B.value;
        for (int side = 0; side < 2; ++side) {
            CoastRange R; R.start = int32_t(cv.points.size());
            if (n != 1) {
                for (int i = 0; i + 1 < n; ++i) {
                    const CoastPoint p0 = cv.points[size_t(B.start + i)], p1 = cv.points[size_t(B.start + i + 1)];
                    V2 a{p0.x, p0.y}, b{p1.x, p1.y}, z{0, 0};
                    if (i + 2 < n) z = V2{cv.points[size_t(B.start + i + 2)].x, cv.points[size_t(B.start + i + 2)].y};
                    // the 3-point derivative at i (f = 0) is p1 - p0; at the end (f = 1, one point back) the same
                    V2 T;
                    if (i >= n - 2) { const CoastPoint pm = cv.points[size_t(B.start + i - 1)]; T = deriv3(V2{pm.x, pm.y}, a, b, 1.0f); }
                    else T = deriv3(a, b, z, 0.0f);
                    const float len = float(std::sqrt(double(T.y) * T.y + double(T.x) * T.x));
                    const float inv = 1.0f / len;
                    // MC #9538 item 1 (8.10.3): two equal neighbouring waterline points have no normal; the EXE's
                    // arithmetic would make a NaN offset point (and INT_MIN in the fill). The segment is skipped: the
                    // offset line goes on from the next segment, and the case is counted.
                    if (!usable(inv)) { ++cv.degenerate; continue; }
                    const float mx = (a.x + b.x) * 0.5f, my = (b.y + a.y) * 0.5f;   // midpoint
                    CoastPoint o;
                    if (side == 0) { const float nx = -(inv * T.x), ny = inv * T.y; o.x = mx + ny * -1.0f; o.y = nx * -1.0f + my; }
                    else { const float nx = -(inv * T.x), ny = inv * T.y; o.x = ny + mx; o.y = my + nx; }
                    cv.points.push_back(o);
                }
            }
            R.end = int32_t(cv.points.size());
            cleanup(cv.points, R.start, R.end);
            R.value = side == 0 ? t : (t == 0 ? 1 : (t == 1 ? 2 : t));
            cv.fill.push_back(R);
        }
    }
    out = std::move(cv);
    return Status::Ok();
}

// ------------------------------------------------ step 9 and 8.10: the region bytes
namespace {
struct BlockLists {
    std::vector<CoastRange> boundary, fill;
    struct Seed { int x, y; uint8_t v; };
    std::vector<Seed> seeds;
};

void listRanges(const std::vector<CoastRange>& ranges, const std::vector<CoastPoint>& pts, int sx, int sy, int nbx,
                std::vector<BlockLists>& blocks, bool fill) {
    for (const CoastRange& R : ranges) {
        int lastBx = -1, lastBy = -1;
        for (int i = R.start; i < R.end; ++i) {
            int x = truncI(pts[size_t(i)].x), y = truncI(pts[size_t(i)].y);
            while (x < 0) x += sx;
            while (y < 0) y += sy;
            const int bx = (x % sx) / 64, by = (y % sy) / 64;
            if (bx == lastBx && by == lastBy) continue;
            lastBx = bx; lastBy = by;
            std::vector<CoastRange>& L = fill ? blocks[size_t(by) * nbx + bx].fill : blocks[size_t(by) * nbx + bx].boundary;
            bool have = false;
            for (const CoastRange& e : L) if (e.start == R.start && e.end == R.end) { have = true; break; }
            if (!have) L.push_back(R);
        }
    }
}

struct Fill {
    int x0, y0, x1, y1, sx, sy;
    uint8_t* reg;   // 64 x 64
    std::vector<std::pair<int, int>> stack;
    bool inside(int px, int py, int& wx, int& wy) const {
        wx = (sx * 100 + px) % sx; wy = (sy * 100 + py) % sy;
        return x0 <= wx && wx < x1 && y0 <= wy && wy < y1;
    }
    void set(int px, int py, uint8_t v) { int wx, wy; if (inside(px, py, wx, wy)) reg[(wy & 63) * 64 + (wx & 63)] = v; }
    bool blocked(int px, int py) const { int wx, wy; if (!inside(px, py, wx, wy)) return true; return reg[(wy & 63) * 64 + (wx & 63)] != 4; }
    void push(int px, int py, uint8_t v) { if (!blocked(px, py)) { set(px, py, v); stack.push_back(std::make_pair(px, py)); } }
    void flood(uint8_t v) {
        while (!stack.empty()) {
            const std::pair<int, int> p = stack.back(); stack.pop_back();
            push(p.first + 1, p.second, v); push(p.first - 1, p.second, v);
            push(p.first, p.second + 1, v); push(p.first, p.second - 1, v);
        }
    }
    template <class F> static void line(int x0_, int y0_, int x1_, int y1_, F plot) {
        const bool steep = std::abs(x1_ - x0_) < std::abs(y1_ - y0_);
        int a0 = x0_, b0 = y0_, a1 = x1_, b1 = y1_;
        if (steep) { a0 = y0_; b0 = x0_; a1 = y1_; b1 = x1_; }
        if (a1 < a0) { std::swap(a0, a1); std::swap(b0, b1); }
        const int da = a1 - a0, db = std::abs(b1 - b0);
        int err = da / 2; const int step = b0 < b1 ? 1 : -1;
        int b = b0;
        for (int a = a0; a != a1 + 1; ++a) {
            if (steep) plot(b, a); else plot(a, b);
            err -= db;
            if (err < 0) { b += step; err += da; }
        }
    }
};
} // namespace

Status regionBlocks(const MapFrame& fr, const PlotTable& plots, const CoastCurves& cv, int bx0, int by0, int bx1,
                    int by1, std::vector<uint8_t>& out) {
    const int nbx = fr.plotsW, nby = fr.plotsH + 4, sx = fr.samplesX(), sy = fr.samplesY();
    if (nbx <= 0 || nby <= 0 || bx0 < 0 || by0 < 0 || bx1 > nbx || by1 > nby || bx0 >= bx1 || by0 >= by1)
        return Status::fail(Err::BadLayout, "region: block rectangle outside the map");
    if (plots.w != fr.plotsW || plots.h != fr.plotsH || plots.plots.size() != size_t(plots.w) * plots.h)
        return Status::fail(Err::BadLayout, "region: plot table size");
    for (const CoastRange& R : cv.boundary) if (R.start < 0 || R.end > int(cv.points.size()) || R.start > R.end) return Status::fail(Err::BadLayout, "region: boundary range");
    for (const CoastRange& R : cv.fill) if (R.start < 0 || R.end > int(cv.points.size()) || R.start > R.end) return Status::fail(Err::BadLayout, "region: fill range");
    // MC #9538 item 1 (8.10.3): every point a range uses must be finite and near the field before any is truncated: a NaN
    // truncates to INT_MIN, and the line fill and the wrap loops below would then walk about 2^31 steps on the worker
    {
        const double rx = double(kRegionPointReach) * sx, ry = double(kRegionPointReach) * sy;
        auto check = [&](const std::vector<CoastRange>& v, const char* what) -> Status {
            for (const CoastRange& R : v)
                for (int i = R.start; i < R.end; ++i) {
                    const CoastPoint& p = cv.points[size_t(i)];
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::fabs(double(p.x)) > rx || std::fabs(double(p.y)) > ry)
                        return Status::fail(Err::BadLayout, std::string("region: ") + what + " point " + num(i) +
                                            " is not finite or lies far outside the field (refused)");
                }
            return Status::Ok();
        };
        C5R_TRY(check(cv.boundary, "boundary"));
        C5R_TRY(check(cv.fill, "fill"));
    }
    std::vector<BlockLists> blocks(size_t(nbx) * nby);
    listRanges(cv.boundary, cv.points, sx, sy, nbx, blocks, false);
    listRanges(cv.fill, cv.points, sx, sy, nbx, blocks, true);
    // seeds: four per cell of the class grid
    Grid g(fr);
    std::vector<uint8_t> cls(size_t(g.gw) * g.gh, 1);   // margin rows: the border pre-fill is coast
    for (int y = 0; y < plots.h; ++y) for (int x = 0; x < plots.w; ++x) {
        const int32_t q = x - (y >> 1), r = y, s = -q - r;
        const int32_t v0 = q * g.G[0] + r * g.G[4] + s * g.G[8] + g.G[12];
        const int32_t v1 = q * g.G[1] + r * g.G[5] + s * g.G[9] + g.G[13];
        const int cy = v1, cx = v0 + (v1 >> 1);
        if (cx < 0 || cx >= g.gw || cy < 0 || cy >= g.gh) return Status::fail(Err::BadLayout, "region: plot outside the grid");
        cls[size_t(cy) * g.gw + cx] = cellClass(heightfield::patternCellFlags(plots.plots[size_t(y) * plots.w + x].base));
    }
    static const int kSeed[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (int y = 0; y < g.gh; ++y) for (int x = 0; x < g.gw; ++x) {
        const Cube c = g.cellCube(x, y);
        int cx, cy; g.cubeToCell(c, cx, cy);
        uint8_t v = 0;
        if (g.exists(cx, cy)) v = cls[size_t(cy) * g.gw + size_t(g.wrapX(cx))];
        const V2 h = g.centre(c);
        const int hx = truncI(h.x), hy = truncI(h.y);
        for (int k = 0; k < 4; ++k) {
            const int px = kSeed[k][0] + hx, py = kSeed[k][1] + hy;
            const int bx = px / 64, by = py / 64;   // toward zero; no lower-bound test
            if (bx < nbx && by < nby && bx >= 0 && by >= 0) {
                BlockLists::Seed s; s.x = px; s.y = py; s.v = v;
                blocks[size_t(by) * nbx + bx].seeds.push_back(s);
            }
        }
    }
    const int W = (bx1 - bx0) * 64, H = (by1 - by0) * 64;
    out.assign(size_t(W) * H, 2);
    std::vector<uint8_t> reg(64 * 64);
    for (int by = by0; by < by1; ++by) for (int bx = bx0; bx < bx1; ++bx) {
        std::fill(reg.begin(), reg.end(), uint8_t(4));
        Fill F; F.x0 = bx * 64; F.y0 = by * 64; F.x1 = F.x0 + 64; F.y1 = F.y0 + 64; F.sx = sx; F.sy = sy; F.reg = reg.data();
        const BlockLists& B = blocks[size_t(by) * nbx + bx];
        for (const CoastRange& R : B.boundary) {
            const uint8_t v = uint8_t(R.value);
            for (int i = R.start; i + 1 < R.end; ++i)
                Fill::line(truncI(cv.points[size_t(i)].x), truncI(cv.points[size_t(i)].y), truncI(cv.points[size_t(i + 1)].x),
                           truncI(cv.points[size_t(i + 1)].y), [&](int px, int py) { F.set(px, py, v); });
        }
        for (const CoastRange& R : B.fill) {
            const uint8_t v = uint8_t(R.value);
            for (int i = R.start; i + 1 < R.end; ++i)
                Fill::line(truncI(cv.points[size_t(i)].x), truncI(cv.points[size_t(i)].y), truncI(cv.points[size_t(i + 1)].x),
                           truncI(cv.points[size_t(i + 1)].y), [&](int px, int py) { F.push(px, py, v); });
            F.flood(v);
        }
        for (const BlockLists::Seed& s : B.seeds) {
            if (F.blocked(s.x, s.y)) continue;
            F.push(s.x, s.y, s.v);
            F.flood(s.v);
        }
        for (int r = 0; r < 64; ++r) for (int c = 0; c < 64; ++c) {
            uint8_t v = reg[size_t(r) * 64 + c]; if (v == 4) v = 2;
            out[size_t((by - by0) * 64 + r) * W + size_t((bx - bx0) * 64 + c)] = v;
        }
    }
    return Status::Ok();
}

Status regionField(const MapFrame& fr, const PlotTable& plots, const CoastCurves& cv, std::vector<uint8_t>& out) {
    return regionBlocks(fr, plots, cv, 0, 0, fr.plotsW, fr.plotsH + 4, out);
}

}} // namespace c5r::liveterrain
