// c5r/unitanim/driver.h - the offline driver of the unit animation controller (epic 0374, issue 4404 part B;
// research/05-graphics-composition/unit-controller.md 4 and 14). The tool render/tools/unitanim/ is driverMain();
// the pieces are here so that the tests run them in-process with the test doubles (anim_testkit.h).
//
//   unitanim play --timeline FILE.c5anim --game INSTALL --db DB --out DIR [--mods "A;B"] [--work-table FILE]
//                 [--no-c5units] [--fade MACHINE:FROM:TO:SECONDS]... [--skip SEQ:PLAYER:ID:FIGURE|*:STEPS]...
//                 [--clock steps|continuous]   (K6's two readings, clock.h; default continuous, MC #2089)
//                 [--run-from]   (4406 r2: GraphOptions::hold_from_continue off, a comparison line; default held,
//                 MC #2117)
//                 [--action-skip none|order|next]   (4406 r2: ActionSkip, clock.h; default none)
//
// Reads the timeline with its start scene (readTimelineFile), opens the controller on the install and the mods
// (gamefs), the database (art rows, EnableActions, Builds) and the ActionBridge, plays every frame, and writes into DIR:
//   programs.csv  frame,seq,present_time,player,id,figure,node,kind,clip,ec,t,a,b,w,mask,state   (one row per node)
//   triggers.csv  frame,seq,present_time,player,id,figure,type,ec,trigger,time,offset,event,bone
//   status.csv    frame,seq,player,id,status,text                                              (every unit, frame)
//   masks.csv     frame,seq,player,id,figure,mask,binding,default_weight,target_weight,bones   (4406 r2: the track
//                 masks programs.csv's `mask` indexes; bones = how many the program names, 0 = resolved from the model)
//   frames/<seq, 10 digits>.c5units  the frames where every unit is Ok and every figure has one source (format 1)
// `frame` is the 0-based frame index in the file, `seq` the frame record's sequence number; floats %.9g; text fields
// quoted when they hold a comma, a quote or a line break. Nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/timeline.h"
#include "c5r/units/pose.h"

namespace c5r { namespace unitanim {

// Exit codes of the tool, one per failure class.
enum class DriverExit : int {
    Ok = 0,
    UnitsFailed = 1,         // every frame played and written, but a unit had a status other than Ok (status.csv)
    Usage = 2,               // arguments
    TimelineRefused = 3,     // the .c5anim or its start scene was refused or is missing (readTimelineFile)
    InputsUnavailable = 4,   // the install, the mods, the database, its tables or the work table
    OutputFailed = 5,        // an output file or folder could not be written
    FrameRefused = 6,        // the controller refused a frame as a whole (Controller::apply)
};
const char* driverExitName(DriverExit e);

// The product's graph factory: bindGraph (4409) with the member's ec map completed by each clip's duration, read from
// the clip file (its animation 0, as 0041's clip cache binds it); a clip that cannot be read keeps duration 0, so the
// graph fails EcNotBound and the text names the clip and why. Durations are cached by lower-case basename.
class ClipGraphFactory : public GraphFactory {
public:
    explicit ClipGraphFactory(units::ClipFileLoader loader) : loader_(std::move(loader)) {}
    AnimStatus bind(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                    std::unique_ptr<GraphInstance>& out) override;
    // 4738: the clip's duration as bind() reads it (the same cache, a lazy read counted the same way)
    float clipDuration(const std::string& clip) override;
    size_t clipsRead() const { return durations_.size(); }
    GraphOptions options;   // 4406 r2 (additive): the evaluator's readings held as options (graph.h)

    // 4468 (additive; unit-controller.md 18): the durations read ahead, off the thread that binds. A clip's entry is
    // (duration, why): duration 0 = it could not be read, why says so. readDurations reads each named clip (any case;
    // keyed by lower-case basename) with `loader` exactly as bind() reads one; seed() takes the entries the cache lacks
    // (a clip read here before is kept) and returns how many it took. lazyReads() counts the clips bind() had to read
    // itself (not seeded).
    using Durations = std::map<std::string, std::pair<float, std::string>>;
    static void readDurations(const units::ClipFileLoader& loader, const std::vector<std::string>& clips, Durations& out);
    size_t seed(const Durations& d);
    size_t lazyReads() const { return lazy_reads_; }
    // 0441 (additive): the durations a pose clip cache already read (units::ClipCache::durations: animation 0 of the
    // same file, the value readOne reads; byte-equal by test, test_clip_durations.cpp) for the named clips `out` lacks,
    // as (duration, ""). A clip the cache lacks (not bound, failed, no positive duration) is left for readDurations,
    // so its refusal text stays readOne's. Returns how many it added.
    static size_t takeFromCache(const units::ClipCache& cache, const std::vector<std::string>& clips, Durations& out);
    // 0470 (unit-controller.md 19.7): bind() binds through this GraphShare, so every figure's instance of one graph
    // file bound to equal sources holds one shared shape. One factory per controller and thread (as AnimRunner and the
    // driver use it).
    const GraphShare& share() const { return share_; }

private:
    static std::pair<float, std::string> readOne(const units::ClipFileLoader& loader, const std::string& key);
    units::ClipFileLoader loader_;
    Durations durations_;   // basename -> duration (0 = failed), why
    size_t lazy_reads_ = 0;
    GraphShare share_;   // 0470
};

struct PlayOptions {
    ControllerOptions controller;    // seed and forced are taken from the timeline
    bool c5units = true;
    std::function<void(Controller&)> prepare;   // optional: called after open(), before the start scene (tests give
                                                // parsed logic files through Controller::logic() here)
};
struct PlaySummary {
    size_t frames = 0, program_rows = 0, trigger_rows = 0, status_rows = 0, c5units_frames = 0;
    size_t units_max = 0;            // units held at once, at most
    size_t failed_units = 0;         // distinct (player, id) with a status other than Ok in some frame
    std::string first_failure;       // "<player>/<id> <code>: <text>" of the first one
    double seconds = 0;              // wall time of the frames (an indication, COMMON 23)
};

// Plays `tl` (start scene `start`) through a controller opened on (files, art, graphs) and writes the outputs into
// out_dir (created if missing, with frames/). Ok, UnitsFailed, OutputFailed or FrameRefused; `msg` names the cause.
DriverExit playTimeline(const Timeline& tl, const units::UnitScene& start, const std::wstring& out_dir,
                        c5m::FileProvider& files, ArtSource& art, GraphFactory& graphs, const PlayOptions& opt,
                        PlaySummary& sum, std::string& msg);

// The tool (render/tools/unitanim/unitanim.cpp calls it): the usage line on stderr for Usage, one line naming the exit
// class and the cause on stderr for any other failure, a summary line on stdout. Returns the DriverExit as an int.
int driverMain(int argc, char** argv);

}}  // namespace c5r::unitanim
