// placement.cpp - layer 1 of c5r::pathmodels: the instance list of the move-order path (ticket 0300).
// Own code from the knowledge base's description (D83): arrows-and-movement-path.md 2.3, path-models.md 3.
#include "c5r/pathmodels/pathmodels.h"

#include <cmath>
#include <string>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"

namespace c5r { namespace pathmodels {

namespace {

bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

Mat4 scaledAt(const Vec3& p, float s) {
    Mat4 m;
    m.m[0] = m.m[5] = m.m[10] = s;
    m.m[12] = p.x;
    m.m[13] = p.y;
    m.m[14] = p.z;
    return m;
}

// The connector frame (path-models.md 3.2): row 0 the side normalise(d.y, -d.x, 0), row 1 the step normalise(d) in
// three dimensions, row 2 world up, row 3 the position. Returns false for a step of zero length.
bool connectorFrame(const Vec3& a, const Vec3& b, Mat4& m) {
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float len2 = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.0f) || !(len2 > 0.0f)) return false;
    m = Mat4();
    m.m[0] = dy / len2;
    m.m[1] = -dx / len2;
    m.m[2] = 0.0f;
    m.m[4] = dx / len;
    m.m[5] = dy / len;
    m.m[6] = dz / len;
    m.m[8] = 0.0f;
    m.m[9] = 0.0f;
    m.m[10] = 1.0f;
    return true;
}

struct Kind {
    Asset center, selected, connector;
};

Kind kindOf(Asset center) {
    const uint8_t c = static_cast<uint8_t>(center);   // Center* 0..5 -> SelectedAni* 6..11, Connector* 12..17
    return {center, static_cast<Asset>(c + 6), static_cast<Asset>(c + 12)};
}

class List {
public:
    List(std::vector<Instance>& out, PlacementStats& st) : out_(out), st_(st) {}
    void add(Asset a, const Mat4& m, uint16_t node) {
        if (out_.size() >= kMaxInstances) {
            ++st_.dropped;
            return;
        }
        Instance i;
        i.asset = a;
        i.world = m;
        i.node = node;
        out_.push_back(i);
    }
private:
    std::vector<Instance>& out_;
    PlacementStats& st_;
};

}  // namespace

const char* assetName(Asset a) {
    static const char* const names[kAssetCount] = {
        "CenterOneTurn", "CenterTwoTurns", "CenterThreeTurns", "CenterInaccessible", "CenterEmbark", "CenterAttack",
        "SelectedAniOneTurn", "SelectedAniTwoTurns", "SelectedAniThreeTurns", "SelectedAniInaccessible",
        "SelectedAniEmbark", "SelectedAniAttack",
        "ConnectorOneTurn", "ConnectorTwoTurns", "ConnectorThreeTurns", "ConnectorInaccessible", "ConnectorEmbark",
        "ConnectorAttack"};
    const size_t i = static_cast<size_t>(a);
    return i < kAssetCount ? names[i] : "?";
}

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

Status placePath(const PathInput& in, std::vector<Instance>& out, PlacementStats* stats) {
    out.clear();
    PlacementStats local;
    PlacementStats& st = stats ? *stats : local;
    st = PlacementStats();
    const size_t n = in.nodes.size();
    if (n > kMaxNodes)
        return Status::fail(Err::BadLayout, std::to_string(n) + " path nodes, the record holds at most " +
                                                std::to_string(kMaxNodes));
    if (in.pos.size() != n)
        return Status::fail(Err::BadLayout, std::to_string(in.pos.size()) + " positions for " + std::to_string(n) +
                                                " nodes");
    if (!in.embark.empty() && in.embark.size() != n)
        return Status::fail(Err::BadLayout, "embark flags: " + std::to_string(in.embark.size()) + " for " +
                                                std::to_string(n) + " nodes");
    for (size_t i = 0; i < n; ++i)
        if (!finite3(in.pos[i]))
            return Status::fail(Err::BadLayout, "node " + std::to_string(i) + "'s position is not finite");
    if (!in.show) return Status::Ok();   // DisplayMovementIndicator(false): the list is cleared, nothing is shown
    List list(out, st);
    if (n == 0) {
        // No path: one marker at the cursor's plot (2.3), Center + SelectedAni of the cursor's kind [I: scale 1].
        if (!finite3(in.cursor)) {
            out.clear();
            return Status::fail(Err::BadLayout, "the cursor position is not finite");
        }
        const Asset c = in.cursorKind == CursorKind::Embark   ? Asset::CenterEmbark
                        : in.cursorKind == CursorKind::Attack ? Asset::CenterAttack
                                                              : Asset::CenterInaccessible;
        const Kind k = kindOf(c);
        // Center first, then its SelectedAni (path-models.md 7.5; corrected 2026-11-18, part 2: was the reverse)
        list.add(k.center, scaledAt(in.cursor, 1.0f), 0);
        list.add(k.selected, scaledAt(in.cursor, 1.0f), 0);
        return Status::Ok();
    }
    // Node 0: the unit's plot, CenterOneTurn, unscaled.
    list.add(Asset::CenterOneTurn, scaledAt(in.pos[0], 1.0f), 0);
    for (size_t i = 1; i < n; ++i) {
        const bool last = i + 1 == n;
        const PathNode& nd = in.nodes[i];
        const float scale = (last || in.nodes[i + 1].turn > nd.turn) ? kTurnEndScale : kMidScale;
        // Corrected 2026-11-18 (part 2, path-models.md 7.5, from the placement's code read again): an attack or embark
        // node gets its Center, then its SelectedAni (an embark node on every node, not only the last); a turn node gets
        // its SelectedAni first (the last node only), then its Center. The step into the node has 3 chevrons when the
        // node is the last and attack or a turn node, 6 otherwise (an embark destination keeps 6).
        Kind k;
        bool centerFirst = false, withSelected = last, threeChevrons = last;
        if (last && in.lastIsAttack) {
            k = kindOf(Asset::CenterAttack);
            centerFirst = true;
        } else if (!in.embark.empty() && in.embark[i]) {
            k = kindOf(Asset::CenterEmbark);
            centerFirst = true;
            withSelected = true;
            threeChevrons = false;
        } else if (nd.turn == 1)
            k = kindOf(Asset::CenterOneTurn);
        else if (nd.turn == 2)
            k = kindOf(Asset::CenterTwoTurns);
        else
            k = kindOf(Asset::CenterThreeTurns);
        const uint16_t node = static_cast<uint16_t>(i);
        if (centerFirst) {
            list.add(k.center, scaledAt(in.pos[i], scale), node);
            if (withSelected) list.add(k.selected, scaledAt(in.pos[i], 1.0f), node);
        } else {
            if (withSelected) list.add(k.selected, scaledAt(in.pos[i], 1.0f), node);
            list.add(k.center, scaledAt(in.pos[i], scale), node);
        }
        // The step's chevrons at a + j (b - a) / 7, j = 1 .. 6, or j = 1 .. 3 into the destination.
        const Vec3& a = in.pos[i - 1];
        const Vec3& b = in.pos[i];
        Mat4 frame;
        if (!connectorFrame(a, b, frame)) {
            ++st.zeroSteps;
            continue;
        }
        const uint32_t count = threeChevrons ? kLastConnectors : kConnectors;
        for (uint32_t j = 1; j <= count; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(kConnectors + 1);
            Mat4 m = frame;
            m.m[12] = a.x + t * (b.x - a.x);
            m.m[13] = a.y + t * (b.y - a.y);
            m.m[14] = a.z + t * (b.z - a.z);
            list.add(k.connector, m, node);
        }
    }
    return Status::Ok();
}

Status nodePositions(const heightfield::HeightGrid& g, const std::vector<PathNode>& nodes,
                     const std::vector<uint8_t>& fogRevealed, const std::vector<uint8_t>& missing, float floorZ,
                     std::vector<Vec3>& out) {
    out.clear();
    if (!fogRevealed.empty() && fogRevealed.size() != nodes.size())
        return Status::fail(Err::BadLayout, "fog flags: " + std::to_string(fogRevealed.size()) + " for " +
                                                std::to_string(nodes.size()) + " nodes");
    if (!missing.empty() && missing.size() != nodes.size())
        return Status::fail(Err::BadLayout, "missing flags: " + std::to_string(missing.size()) + " for " +
                                                std::to_string(nodes.size()) + " nodes");
    if (!std::isfinite(floorZ)) return Status::fail(Err::BadLayout, "the floor height is not finite");
    std::vector<Vec3> v(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        Status s = units::plotCentre(g, nodes[i].x, nodes[i].y, v[i].x, v[i].y);
        if (!s.ok()) return Status::fail(s.code, "node " + std::to_string(i) + ": " + s.msg);
        const bool toFloor = (!fogRevealed.empty() && fogRevealed[i]) || (!missing.empty() && missing[i]);
        const float z = toFloor ? floorZ : units::heightAtWorld(g, v[i].x, v[i].y);
        v[i].z = z < floorZ ? floorZ : z;
    }
    out.swap(v);
    return Status::Ok();
}

}}  // namespace c5r::pathmodels
