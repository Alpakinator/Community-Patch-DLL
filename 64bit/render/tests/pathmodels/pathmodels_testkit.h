// pathmodels_testkit.h - shared helpers of the c5r::pathmodels tests (ticket 0300): 3801's recorded preview as a
// PathInput, the expected instance sequence, matrix comparison.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "c5r/pathmodels/pathmodels.h"
#include "f6496_reference.h"

namespace pmt {

using namespace c5r::pathmodels;

// The served nodes of 3801's preview (arrows-and-movement-path.md 2.3 [L]): (x, y, turn).
inline std::vector<PathNode> f6496Nodes() {
    const int v[5][3] = {{50, 13, 22}, {51, 14, 21}, {52, 14, 120}, {52, 13, 121}, {53, 13, 220}};
    std::vector<PathNode> n(5);
    for (int i = 0; i < 5; ++i) {
        n[i].x = static_cast<int16_t>(v[i][0]);
        n[i].y = static_cast<int16_t>(v[i][1]);
        n[i].turn = static_cast<int16_t>(v[i][2]);
    }
    return n;
}

// The capture's draws that carry each node's disc (Center* mesh 0): their translation is the node's ground position.
inline const int* f6496DiscDraws() {
    static const int d[5] = {775, 777, 791, 805, 821};
    return d;
}

inline const f6496::Draw& draw(int n) {
    for (const auto& d : f6496::kDraws)
        if (static_cast<int>(d.n) == n) return d;
    std::fprintf(stderr, "no draw %d in the reference\n", n);
    std::abort();
}

inline PathInput f6496Input() {
    PathInput in;
    in.nodes = f6496Nodes();
    for (int i = 0; i < 5; ++i) {
        const auto& d = draw(f6496DiscDraws()[i]);
        Vec3 p;
        p.x = d.world[12];
        p.y = d.world[13];
        p.z = d.world[14];
        in.pos.push_back(p);
    }
    return in;
}

// The instance sequence the capture shows (two draws per instance, path-models.md 3.1).
inline std::vector<Asset> f6496Assets() {
    std::vector<Asset> a;
    a.push_back(Asset::CenterOneTurn);
    for (int node = 1; node <= 4; ++node) {
        if (node == 4) a.push_back(Asset::SelectedAniThreeTurns);
        a.push_back(Asset::CenterThreeTurns);
        for (int k = 0; k < (node == 4 ? 3 : 6); ++k) a.push_back(Asset::ConnectorThreeTurns);
    }
    return a;
}

inline float maxDiff(const float* a, const float* b, int n) {
    float m = 0;
    for (int i = 0; i < n; ++i) m = std::fmax(m, std::fabs(a[i] - b[i]));
    return m;
}

// Rotation part (rows 0-2) within rotTol, translation (row 3) within posTol.
inline bool matNear(const float* got, const float* want, float rotTol, float posTol, std::string* why) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            const float tol = r == 3 ? posTol : rotTol;
            const float d = std::fabs(got[r * 4 + c] - want[r * 4 + c]);
            if (!(d <= tol)) {
                if (why) {
                    char b[160];
                    std::snprintf(b, sizeof b, "row %d col %d: got %.7g want %.7g (diff %.3g, tol %.3g)", r, c,
                                  got[r * 4 + c], want[r * 4 + c], d, tol);
                    *why = b;
                }
                return false;
            }
        }
    return true;
}

}  // namespace pmt
