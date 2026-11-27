// c5r/unitanim/pose_program.h - the controller's per-figure output: a pose program (epic 0374; unit-controller.md 4,
// MC decision 12 of #2000). Interface by the lead at C1; 4409 owns it from now on (additive changes only).
//
// An ordered list of nodes; each node is a sample of one clip at one clip time, or a per-bone blend of two earlier
// nodes with a weight and an optional track mask. The last node is the figure's local pose. The blend follows
// blendfit.py: positions and scale-shear lerped, orientations nlerped after aligning b to a's hemisphere; a bone with
// mask weight m uses w * m (bones the mask does not name use the mask's default weight). A program with one Sample
// node is exactly 0041's (clip, t) and is rendered bit-identical to it (MC condition (1)).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace c5r { namespace unitanim {

struct PoseNode {
    enum Kind : uint8_t { Sample = 0, Blend = 1 };
    Kind kind = Sample;
    // Sample
    std::string clip;            // the animation as the member's .fxsxml names it (0041's clip cache key)
    int32_t ec = 0;              // the ec it was chosen by (diagnosis and the oracle)
    float t = 0;                 // clip time in seconds, already wrapped or clamped by the source's loop rule
    // Blend: out = blend(nodes[a], nodes[b], w) per bone, masked
    int32_t a = -1, b = -1;      // indexes of earlier nodes
    float w = 0;                 // 0 = a, 1 = b
    int32_t mask = -1;           // -1 = none; else an index into PoseProgram::masks
};

struct TrackMaskWeights {
    std::string binding;         // the graph's TrackMasks[k].BindingName
    float default_weight = 1;
    std::vector<std::string> bones;      // bones with an explicit weight
    std::vector<float> weights;          // same length as bones
    // 4409 (additive): the Trackmask node's TargetWeight; the graph leaves `bones` empty (which bones a binding names
    // is not in the .dge: open, unit-controller.md 13.2) and rowsProgram refuses a mask with a binding and no bones.
    float target_weight = 0;
};

struct PoseProgram {
    std::vector<PoseNode> nodes;
    std::vector<TrackMaskWeights> masks;
    bool single() const { return nodes.size() == 1 && nodes[0].kind == PoseNode::Sample; }
    void clear() { nodes.clear(); masks.clear(); }
};

}}  // namespace c5r::unitanim
