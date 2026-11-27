// render/src/units/clipcache.cpp - the clip cache of the pose layer (epic 0041, issue 4353; c5r/units/pose.h).
// Decoded control points kept per (model, clip, skeleton) on first use; a failed bind is kept as its status.
#include <chrono>
#include <cmath>
#include <utility>
#include "c5r/units/pose.h"

namespace c5r { namespace units {

namespace {

PoseStatus poseFail(PoseErr c, std::string msg) {
    PoseStatus s;
    s.code = c;
    s.msg = std::move(msg);
    return s;
}

size_t curveBytes(const c5m::DecodedCurve& c) { return (c.knots.size() + c.controls.size()) * sizeof(float); }

}  // namespace

ClipFileLoader clipLoaderFromProvider(c5m::FileProvider& files, const c5m::StringDatabase* strings) {
    c5m::FileProvider* fp = &files;
    return [fp, strings](const std::string& name, c5m::FileInfo& out, bool& missing, std::string& why) {
        c5m::Bytes b;
        std::string origin;
        missing = false;
        if (!fp->read(name, b, &origin)) {
            missing = true;
            why = name + ": in no install folder, pack or mod";
            return false;
        }
        c5m::Error e;
        if (!c5m::read_gr2(b.data(), b.size(), strings, out, &e)) {
            why = origin + ": " + e.what;
            return false;
        }
        return true;
    };
}

ClipCache::ClipCache(ClipFileLoader loader) : loader_(std::move(loader)) {}

size_t ClipCache::durations(std::map<std::string, float>& out) const {
    size_t n = 0;
    for (const auto& kv : map_) {
        const CachedClip& cc = kv.second;
        if (!cc.status.ok() || cc.file.empty() || !std::isfinite(cc.duration) || !(cc.duration > 0)) continue;
        if (out.emplace(cc.file, cc.duration).second) ++n;
    }
    return n;
}

void ClipCache::clear() {
    map_.clear();
    stats_ = Stats();
}

PoseStatus ClipCache::get(const PoseModel& pm, const std::string& clip, uint32_t skeleton, const CachedClip*& out) {
    out = nullptr;
    if (skeleton >= pm.skeletons.size())
        return poseFail(PoseErr::BadInput, pm.key + ": skeleton " + std::to_string(skeleton) + " of " +
                                               std::to_string(pm.skeletons.size()));
    const int32_t ai = findAnimation(pm, clip);
    if (ai < 0) return poseFail(PoseErr::ClipNotFound, pm.key + ": the .fxsxml names no clip \"" + clip + "\"");
    const std::string key = pm.key + "|" + std::to_string(ai) + "|" + std::to_string(skeleton);
    auto it = map_.find(key);
    if (it != map_.end()) {
        ++stats_.hits;
        out = &it->second;
        return it->second.status;
    }
    const auto t0 = std::chrono::steady_clock::now();
    CachedClip cc;
    cc.file = c5m::basename_lower(pm.animations[(size_t)ai].file);
    c5m::FileInfo fi;
    bool missing = false;
    std::string why;
    if (!loader_ || !loader_(cc.file, fi, missing, why)) {
        cc.status = poseFail(missing ? PoseErr::ModelNotFound : PoseErr::BadModel,
                             pm.key + ": clip " + cc.file + ": " + (loader_ ? why : std::string("no loader")));
    } else if (fi.animations.empty()) {
        cc.status = poseFail(PoseErr::BadModel, pm.key + ": clip " + cc.file + ": no animation in the file");
    } else {
        PoseStatus bs;
        cc.basis = fileBasis(fi, &bs);
        if (!bs.ok()) {
            cc.status = poseFail(PoseErr::BadModel, pm.key + ": clip " + cc.file + ": " + bs.msg);
        } else {
            c5m::AnimError e;
            if (!c5m::bind_clip_all_groups(fi, 0, pm.skeletons[skeleton], cc.bound, &e)) {
                const bool unsupported = e.code == c5m::AnimErr::UnsupportedFormat || e.code == c5m::AnimErr::BadDegree;
                cc.status = poseFail(unsupported ? PoseErr::CurveUnsupported : PoseErr::BadModel,
                                     pm.key + ": clip " + cc.file + ": " + c5m::anim_err_name(e.code) + ": " + e.what);
            } else if (!std::isfinite(cc.bound.duration) || cc.bound.duration < 0) {
                cc.status = poseFail(PoseErr::BadModel, pm.key + ": clip " + cc.file + ": duration not finite");
                cc.bound = c5m::BoundClip();
            } else {
                cc.duration = cc.bound.duration;
                size_t bytes = cc.bound.bone_track.size() * sizeof(int32_t) + cc.bound.tracks.size() * sizeof(c5m::ClipTrack);
                for (const c5m::ClipTrack& tr : cc.bound.tracks)
                    bytes += curveBytes(tr.position) + curveBytes(tr.orientation) + curveBytes(tr.scale_shear);
                cc.bytes = bytes;
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    stats_.bind_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    ++stats_.binds;
    ++stats_.entries;
    if (!cc.status.ok()) ++stats_.failed_entries;
    stats_.bytes += cc.bytes;
    auto ins = map_.emplace(key, std::move(cc));
    out = &ins.first->second;
    return out->status;
}

}}  // namespace c5r::units
