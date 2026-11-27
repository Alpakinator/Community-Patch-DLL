// c5r/units/scene_file.h - the unit-scene file (.c5units, format 1): reader and writer of one frame's UnitScene
// (epic 0041, issue 4355). Format and rules: research/05-graphics-composition/unit-renderer.md section 3.
//
//   c5units 1
//   scene   <position_source exe|own> <water_level>
//   player  <player> <colour Type> <art_style Type> <era Type>
//   unit    <player> <id> <unit_type> <art_row|-> <plot_x> <plot_y> <root x|-> <root y|-> <root z|-> <facing> <seed>
//           <damage> <max_hp> <alive|-> <flags> <fade_from> <fade_to> <fade>
//   figure  <figure> <clip> <t>                    (belongs to the last unit line)
//
// One tab between fields, '#' starts a comment line, blank lines ignored, LF or CRLF. Strict: the first refusal stops
// the read with a named code and its line number, and the output scene is left empty (nothing of the file is used).
// The writer refuses a scene the reader would refuse (it checks its own text by reading it back), so every file it
// writes reads back to an equal scene. Floats are written with %.9g (round-trip exact). Nothing throws.
#pragma once
#include <cstddef>
#include <string>
#include "c5r/units/unit_scene.h"

namespace c5r { namespace units {

enum class SceneFileErr {
    Ok = 0,
    Io,          // the file could not be opened, read or written
    BadHeader,   // the first record is not "c5units<TAB>1" (missing, other kind, other version)
    BadRecord,   // an unknown record kind, a wrong field count, or a required record missing (scene)
    BadValue,    // a number that does not parse completely, a value out of range, an empty or control-character string
    Duplicate,   // a second scene record, a second player record or (player, id) or figure index in one unit
    Orphan,      // a figure before any unit, or a unit whose player has no player record
    TooLarge,    // more than 64 players, 4,096 units or 64 figures in a unit, a line over 4,096 bytes, a file over 64 MiB
};
const char* sceneFileErrName(SceneFileErr e);

// Limits (unit-renderer.md section 3).
constexpr size_t kSceneMaxPlayers = 64;
constexpr size_t kSceneMaxUnits = 4096;
constexpr size_t kSceneMaxFigures = 64;
constexpr size_t kSceneMaxLine = 4096;          // bytes, without the line end
constexpr size_t kSceneMaxFile = 64u << 20;     // bytes

struct SceneFileStatus {
    SceneFileErr code = SceneFileErr::Ok;
    size_t line = 0;      // 1-based line of the refusal; 0 = the file as a whole (I/O, size, a missing record)
    std::string msg;      // what was refused, naming the field and value
    bool ok() const { return code == SceneFileErr::Ok; }
    // "<code> at line <n>: <msg>" (or "<code>: <msg>" for line 0)
    std::string str() const;
};

// Text -> scene. On any refusal `out` is reset to an empty UnitScene{} and the status names code and line.
SceneFileStatus parseScene(const std::string& text, UnitScene& out);
// Scene -> text (LF line ends). On a refusal `text` is cleared; the status's line is that of the generated text.
SceneFileStatus writeScene(const UnitScene& scene, std::string& text);

// File forms of the two (path UTF-16, as the rest of render/ on Windows).
SceneFileStatus readSceneFile(const std::wstring& path, UnitScene& out);
SceneFileStatus writeSceneFile(const std::wstring& path, const UnitScene& scene);

}}  // namespace c5r::units
