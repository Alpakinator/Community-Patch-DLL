// formations.cpp - the formation table of the unit module (epic 0041, U3, issue 4354): UnitFormations.xml read from
// the install, the built-in Default record, GlobalUnitScale, and the two database columns placement needs.
// Rule source: research/05-graphics-composition/unit-formations.md sections 1, 2 (own words, D83). Own code.
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <windows.h>
#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/placement.h"

namespace c5r { namespace units {

namespace {

struct Field {
    const char* name;
    int32_t FormationRecord::*i;   // one of the two is set
    float FormationRecord::*f;
};

const Field kFields[] = {
    {"SpacingX", &FormationRecord::spacingX, nullptr},
    {"SpacingY", &FormationRecord::spacingY, nullptr},
    {"SpacingMultiplier", nullptr, &FormationRecord::spacingMultiplier},
    {"UnitRandomness", nullptr, &FormationRecord::unitRandomness},
    {"BlockRandomness", nullptr, &FormationRecord::blockRandomness},
    {"PerRowInc", &FormationRecord::perRowInc, nullptr},
    {"YOffsetInc", nullptr, &FormationRecord::yOffsetInc},
    {"YOffsetCurved", &FormationRecord::yOffsetCurved, nullptr},
    {"Stagger", &FormationRecord::stagger, nullptr},
    {"TotalWidth", &FormationRecord::totalWidth, nullptr},
    {"XBlockSize", &FormationRecord::xBlockSize, nullptr},
    {"XBlockSpacingCount", &FormationRecord::xBlockSpacingCount, nullptr},
    {"YBlockSize", &FormationRecord::yBlockSize, nullptr},
    {"YBlockSpacingCount", &FormationRecord::yBlockSpacingCount, nullptr},
    {"CenterWidth", &FormationRecord::centerWidth, nullptr},
};

// The whole text must be one finite number (the stock file writes integers for the integer fields; a fractional
// value there is truncated, as an integer reader does [I]).
bool parseNumber(const std::string& t, double& v)
{
    if (t.empty()) return false;
    errno = 0;
    char* end = nullptr;
    v = std::strtod(t.c_str(), &end);
    return end && *end == '\0' && errno == 0 && std::isfinite(v);
}

FormationRecord defaultRecord()
{
    FormationRecord r;
    r.name = "Default";
    return r;
}

}  // namespace

Status FormationTable::parse(const std::string& xml)
{
    records_.clear();
    warnings_.clear();
    c5m::XmlElement root;
    c5m::Error err;
    if (!c5m::parse_xml(xml.data(), xml.size(), root, &err))
        return Status::fail(Err::BadLayout, "UnitFormations.xml does not parse: " + err.what);
    if (root.name != "Formations")
        return Status::fail(Err::BadLayout, "UnitFormations.xml: root element is <" + root.name + ">, not <Formations>");
    std::vector<FormationRecord> recs;
    recs.push_back(defaultRecord());
    for (const c5m::XmlElement& f : root.children) {
        if (f.name != "Formation") {
            warnings_.push_back("element <" + f.name + "> under <Formations> ignored (line " + std::to_string(f.line) + ")");
            continue;
        }
        const c5m::XmlElement* nameEl = f.child("Name");
        if (!nameEl || nameEl->text.empty())
            return Status::fail(Err::BadLayout, "UnitFormations.xml: a <Formation> without <Name> (line " +
                                                                    std::to_string(f.line) + ")");
        FormationRecord rec = defaultRecord();
        rec.name = nameEl->text;
        for (const c5m::XmlElement& c : f.children) {
            if (c.name == "Name") continue;
            const Field* fd = nullptr;
            for (const Field& k : kFields)
                if (c.name == k.name) fd = &k;
            if (!fd) {
                warnings_.push_back("formation " + rec.name + ": unknown field <" + c.name + "> ignored");
                continue;
            }
            double v = 0;
            if (!parseNumber(c.text, v))
                return Status::fail(Err::BadLayout, "UnitFormations.xml: field " + c.name + " of " +
                                                                        rec.name + " is not a number: '" + c.text + "'");
            if (fd->i) {
                if (v > 2147483647.0 || v < -2147483648.0)
                    return Status::fail(Err::BadLayout, "UnitFormations.xml: field " + c.name + " of " +
                                                                            rec.name + " out of range: '" + c.text + "'");
                rec.*(fd->i) = static_cast<int32_t>(v);   // truncation toward zero
            } else {
                rec.*(fd->f) = static_cast<float>(v);
            }
        }
        bool replaced = false;
        for (FormationRecord& r : recs)
            if (r.name == rec.name && &r != &recs[0]) {
                r = rec;   // a later entry with the same name replaces the earlier one [I]
                replaced = true;
                warnings_.push_back("formation " + rec.name + " given twice; the later entry is used");
            }
        if (!replaced) recs.push_back(rec);
    }
    if (recs.size() < 2) return Status::fail(Err::BadLayout, "UnitFormations.xml: no <Formation>");
    records_ = std::move(recs);
    return Status::Ok();
}

namespace {

// A loose file under the install root (the EXE's own path open), else the file system's name lookup.
// found false and Ok: neither has it.
Status readInstallFile(const gamefs::GameFiles& files, const std::wstring& installRoot, const wchar_t* relPath,
                       const char* name, std::vector<uint8_t>& out, std::string& origin, bool& found)
{
    found = false;
    out.clear();
    if (!installRoot.empty()) {
        std::wstring path = installRoot;
        while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
        path += L"\\";
        path += relPath;
        const DWORD attr = GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            std::ifstream f(path.c_str(), std::ios::binary);
            if (!f) return Status::fail(Err::Io, "cannot open " + gamefs::toUtf8(path));
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            if (f.bad()) return Status::fail(Err::Io, "cannot read " + gamefs::toUtf8(path));
            origin = "loose:" + gamefs::toUtf8(path);
            found = true;
            return Status::Ok();
        }
    }
    if (!files.exists(name)) return Status::Ok();
    C5R_TRY(files.read(name, out, &origin));
    found = true;
    return Status::Ok();
}

}  // namespace

Status FormationTable::load(const gamefs::GameFiles& files, const std::wstring& installRoot)
{
    records_.clear();
    origin_.clear();
    std::vector<uint8_t> bytes;
    std::string origin;
    bool found = false;
    C5R_TRY(readInstallFile(files, installRoot, L"Assets\\Units\\UnitFormations.xml", "unitformations.xml", bytes,
                            origin, found));
    if (!found) return Status::fail(Err::NotFound, "UnitFormations.xml is neither loose in the install nor in its file system");
    C5R_TRY(parse(std::string(bytes.begin(), bytes.end())));
    origin_ = origin;
    // GlobalUnitScale (unit-formations.md 2): Assets\Units\UnitVisConfig.ini, [Debugging], percent, default 100.
    unitScale_ = 1.0f;
    std::vector<uint8_t> ini;
    std::string iniOrigin;
    C5R_TRY(readInstallFile(files, installRoot, L"Assets\\Units\\UnitVisConfig.ini", "unitvisconfig.ini", ini, iniOrigin,
                            found));
    if (found) {
        std::string text(ini.begin(), ini.end()), section;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t e = text.find('\n', pos);
            if (e == std::string::npos) e = text.size();
            std::string line = text.substr(pos, e - pos);
            pos = e + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos) continue;
            line = line.substr(b);
            if (line[0] == '[') {
                section = line;
                continue;
            }
            if (c5m::ascii_lower(section) != "[debugging]") continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            if (c5m::ascii_lower(key) != "globalunitscale") continue;
            // an integer reader: leading digits count, anything else gives 0 [I: GetPrivateProfileInt-like]
            long pct = std::strtol(line.c_str() + eq + 1, nullptr, 10);
            unitScale_ = static_cast<float>(pct) * 0.01f;
            warnings_.push_back(iniOrigin + ": GlobalUnitScale " + std::to_string(pct));
        }
    }
    return Status::Ok();
}

const FormationRecord* FormationTable::find(const std::string& name) const
{
    if (records_.empty()) return nullptr;
    if (name.empty()) return &records_[0];
    for (size_t i = 1; i < records_.size(); ++i)
        if (records_[i].name == name) return &records_[i];
    return nullptr;
}

Status loadPlacementColumns(c5m::TableSource& db, PlacementColumns& out)
{
    out = PlacementColumns();
    PlacementColumns cols;
    std::vector<c5m::DbRow> rows;
    c5m::Error err;
    if (!db.rows("ArtDefine_UnitInfos", {"Type", "Formation"}, rows, &err))
        return Status::fail(Err::NotFound, "ArtDefine_UnitInfos.Type/Formation: " + err.what);
    for (const c5m::DbRow& r : rows)
        if (r.size() == 2 && !r[0].null) cols.formation[r[0].text] = r[1].null ? std::string() : r[1].text;
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberInfos", {"Type", "ZOffset"}, rows, &err))
        return Status::fail(Err::NotFound, "ArtDefine_UnitMemberInfos.Type/ZOffset: " + err.what);
    for (const c5m::DbRow& r : rows) {
        if (r.size() != 2 || r[0].null) continue;
        double v = 0;
        if (!r[1].null && !r[1].text.empty() && !parseNumber(r[1].text, v))
            return Status::fail(Err::BadLayout, "ArtDefine_UnitMemberInfos.ZOffset of " + r[0].text +
                                                    " is not a number: '" + r[1].text + "'");
        cols.zOffset[r[0].text] = static_cast<float>(v);
    }
    out = std::move(cols);
    return Status::Ok();
}

}}  // namespace c5r::units
