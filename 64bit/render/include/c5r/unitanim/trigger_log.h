// c5r/unitanim/trigger_log.h - the offline trigger log (ticket 4721, route S, D137): the trigger stream's records
// (shell/common/vp_trigger.h, VPTrigRecord) and its name table as text, one line per name and per record, written by
// the server in offline runs and read back by the parser below, so that sound and effect timing is testable on
// recorded fights without a client or an audio device (the Architect's route S note 1.3;
// research/05-graphics-composition/unit-combat-server.md section 7).
//
// The file:
//   # c5r trigger log 1                         the first line: the format's version (= VPTRIG_VERSION)
//   # any text                                  comments, anywhere after the first line; blank lines are skipped
//   N <index> <kind> <text>                     a name (R5 of vp_trigger.h): index 1, 2, ... in order, kind sound3d /
//                                               effect / triggerfile, the text to the end of the line (no control
//                                               characters, at most 119 bytes)
//   R v=1 seq=.. F=.. kind=.. reason=.. flags=.. owner=.. fseq=.. player=.. unit=.. figure=.. handle=.. inst=..
//     name=.. file=.. pos=x,y,z scale=.. orient=x,y,z,w vel=x,y,z ec=.. trig=.. t=.. late=..
//                                               a record (one line; written in this order, read in any order, every
//                                               key exactly once). kind and reason by name (reason "-" outside OwnerEnd),
//                                               flags as names joined by '|' ("-" for none; an unknown bit as 0x..),
//                                               floats with 9 significant digits (exact for a float).
// A record's name and file indexes must be declared by an N line before it (R5). Names in a log come from the
// player's install at run time: logs stay outside git (D83).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "c5r/status.h"
#include "vp_trigger.h"

namespace c5r { namespace unitanim {

const char* triggerKindName(uint8_t kind);        // "SoundStart", ...; "?" when unknown
const char* triggerReasonName(uint8_t reason);    // "Natural", ...; "?" when unknown
const char* triggerNameKindName(uint32_t kind);   // "sound3d", "effect", "triggerfile"; "?" when unknown

// The contract's checks of one record (vp_trigger.h's field comments and R7, R8), each failure named.
Status checkTriggerRecord(const VPTrigRecord& r);

struct TriggerName {
    uint32_t index = 0;
    uint32_t kind = VPTRIG_NAME_NONE;
    std::string text;
};

struct TriggerLog {
    std::vector<TriggerName> names;      // index i + 1 at names[i]
    std::vector<VPTrigRecord> records;   // in file order
};

std::string formatTriggerRecord(const VPTrigRecord& r);   // the R line, no newline
Status parseTriggerRecord(const std::string& line, VPTrigRecord& out);
std::string formatTriggerName(const TriggerName& n);      // the N line, no newline
Status parseTriggerName(const std::string& line, TriggerName& out);

std::string formatTriggerLog(const TriggerLog& log);      // the whole file
Status parseTriggerLog(const std::string& text, TriggerLog& out);
Status readTriggerLog(const std::string& path, TriggerLog& out);

// The server's writer in offline runs: names as they are published, records as they are written to the ring.
class TriggerLogWriter {
public:
    TriggerLogWriter() = default;
    ~TriggerLogWriter();
    TriggerLogWriter(const TriggerLogWriter&) = delete;
    TriggerLogWriter& operator=(const TriggerLogWriter&) = delete;
    Status open(const std::string& path);
    // R5: the next name; index must be the previous one + 1 (refused otherwise, nothing written)
    Status name(const TriggerName& n);
    // a record checked by checkTriggerRecord, its name and file declared (refused otherwise, nothing written)
    Status record(const VPTrigRecord& r);
    Status close();
    bool isOpen() const { return f_ != nullptr; }
    uint64_t records() const { return records_; }

private:
    std::FILE* f_ = nullptr;
    uint32_t names_ = 0;
    uint64_t records_ = 0;
    std::string path_;
};

}} // namespace c5r::unitanim
