// anim_groups.cpp - bind_clip_all_groups (c5models/anim.h): a clip bound over every track group, first track of a
// bone name winning. Epic 0041, issue 4353, 2026-11-15. Own code (unit-formations.md 12.2, unit-renderer.md 11).
#include "c5models/anim.h"

#include <unordered_map>

namespace c5m {

bool bind_clip_all_groups(const FileInfo& f, size_t anim, const Skeleton& skel, BoundClip& out, AnimError* err) {
    out = BoundClip();
    auto fail = [&](AnimErr code, const std::string& what) {
        out = BoundClip();
        if (err) {
            err->code = code;
            err->what = what;
        }
        return false;
    };
    if (anim >= f.animations.size()) return fail(AnimErr::NoTrackGroup, "no animation " + std::to_string(anim));
    const Animation& a = f.animations[anim];
    out.name = a.name;
    out.duration = a.duration;
    bool any_group = false;
    std::unordered_map<std::string, int32_t> bone_index;
    for (size_t i = 0; i < skel.bones.size(); ++i) bone_index.emplace(skel.bones[i].name, (int32_t)i);
    out.bone_track.assign(skel.bones.size(), -1);
    for (int32_t gi : a.track_groups) {
        if (gi < 0 || (size_t)gi >= f.track_groups.size()) continue;
        any_group = true;
        const TrackGroup& tg = f.track_groups[(size_t)gi];
        for (const TransformTrack& tt : tg.transform_tracks) {
            auto it = bone_index.find(tt.name);
            if (it == bone_index.end() || out.bone_track[(size_t)it->second] >= 0) {
                ++out.unbound_tracks;   // no such bone, or a later track of a bone already bound: not used
                continue;
            }
            ClipTrack ct;
            AnimError e;
            if (!decode_curve(tt.position, CurveSlot::Position, ct.position, &e) ||
                !decode_curve(tt.orientation, CurveSlot::Orientation, ct.orientation, &e) ||
                !decode_curve(tt.scale_shear, CurveSlot::ScaleShear, ct.scale_shear, &e))
                return fail(e.code, a.name + ": group " + tg.name + ": track " + tt.name + ": " + e.what);
            out.bone_track[(size_t)it->second] = (int32_t)out.tracks.size();
            out.tracks.push_back(ct);
        }
    }
    if (!any_group) return fail(AnimErr::NoTrackGroup, a.name + ": no track group");
    return true;
}

}  // namespace c5m
