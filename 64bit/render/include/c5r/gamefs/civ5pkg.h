// c5r/gamefs/civ5pkg.h - the parts of a *.Civ5Pkg package description that decide which files the game mounts
// (task 2901). A Civ5Pkg is a small XML file in Assets/DLC/<folder>/; the EXE reads it in cvContentPackageManager and
// cvUISkinManager (research/05-graphics-composition/asset-pipeline.md 1.2).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "c5r/status.h"

namespace c5r { namespace gamefs {

// Minimal XML element tree: elements, attributes, text. Comments, processing instructions and DOCTYPE are skipped,
// CDATA is taken as text, the five predefined entities are decoded.
struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::string text;  // concatenated, trimmed character data
    std::vector<std::unique_ptr<XmlNode>> children;
    const XmlNode* child(const char* n) const;                  // first child with this name, or null
    std::vector<const XmlNode*> all(const char* n) const;       // all children with this name
    std::vector<std::string> texts(const char* path) const;     // texts of the nodes at "A/B/C" below this one
};

// Checks: input ends inside a tag, comment or element [Truncated]; a closing tag that does not match [BadLayout];
// no root element or text after the root [BadLayout]; nesting deeper than 64 [BadLayout]; an attribute without a
// quoted value [BadLayout].
Status parseXml(const char* data, size_t size, std::unique_ptr<XmlNode>& root);

struct Civ5PkgSkin {
    std::string name, set, platform;                 // UISkin attributes
    std::vector<std::string> skinDirs, gameplaySkinDirs, skinPaks, gameplaySkinPaks;
};

struct Civ5Pkg {
    std::wstring folder;             // the package folder (absolute), e.g. .../Assets/DLC/Expansion2
    std::wstring file;               // the .Civ5Pkg file
    std::string guid;
    int priority = 0;                // <Priority>, 0 when absent
    bool hasSteamApp = false;
    bool hasKey = false;
    std::vector<std::string> paks;               // <Paks>/File and <Common>/Paks/File
    std::vector<std::string> gameplayDirs;       // <Gameplay>/Directory
    std::vector<std::string> gameplayMapDirs;    // <Gameplay>/MapDirectory (issue 3404)
    std::vector<Civ5PkgSkin> skins;
};

// Root element must be <Civ5Package> [BadLayout]; a <Priority> that is not an integer [BadLayout].
Status parseCiv5Pkg(const char* data, size_t size, Civ5Pkg& out);

}} // namespace c5r::gamefs
