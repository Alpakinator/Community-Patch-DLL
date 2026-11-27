// The .Civ5Save container (issue 2001): header, pregame, u32 2, zlib body in pieces. Reader and writer, byte exact
// against the game's own files. No dependency on the DLL or the server: also built into the civ5save self-test tool.
//
// Layout (research/sources/civ5-headless/saves.md section 2; research/03-dll-engine-boundary/server-host.md section 9):
//   header   "CIV5", u32 8, string game version, string build, u32 turn, u8 game type, 7 type-name strings,
//            u32 n + n x (GUID16, u32 1, string name) DLC, u32 n + n x (string ID, u32 version, string name) mods,
//            string leader name, string civ name, string player colour, GUID16 DLL GUID, string DLL version,
//            GUID16 {0d41152c-948e-498f-8cdf-699d04c98a95}, u32 0.   (string = u32 length + bytes)
//   pregame  ICvPreGame1::write, no length prefix
//   u32 2
//   body     one zlib stream (zlib 1.2.3, level 6, Z_NO_FLUSH then one Z_SYNC_FLUSH, no final block, no Adler
//            checksum), cut into 64 KiB pieces, each a u32 length and its bytes, up to the end of the file.
#pragma once
#include <string>
#include <vector>

struct Civ5SaveHeader
{
	struct Dlc
	{
		unsigned char guid[16];	// as in memory
		unsigned int one;		// 1 in every save seen
		std::string name;
	};
	struct Mod
	{
		std::string id;
		unsigned int version;
		std::string name;
	};
	unsigned int saveVersion;	// 8
	std::string gameVersion;	// "1.0.3.279 (403694)"
	std::string build;			// "403694"
	unsigned int turn;
	unsigned char gameType;		// 0 single player
	std::string types[7];		// civilization, handicap, start era, current era, speed, world size, map script
	std::vector<Dlc> dlc;
	std::vector<Mod> mods;
	std::string leaderName, civName, playerColor;
	unsigned char dllGuid[16];
	std::string dllVersion;
	unsigned int tail;			// 0

	Civ5SaveHeader();
};

// The game's constants.
extern const char* const kCiv5GameVersion;	// "1.0.3.279 (403694)"
extern const char* const kCiv5Build;		// "403694"
extern const unsigned char kCiv5ConstGuid[16];

// Parses the header at the start of `data`; `end` gets its length. Returns false with the reason.
bool Civ5ParseHeader(const std::string& data, Civ5SaveHeader& h, size_t& end, std::string& why);
// Serializes a header (the exact inverse of Civ5ParseHeader).
std::string Civ5WriteHeader(const Civ5SaveHeader& h);

// A whole file split into its parts. `pregame` is found as saves.md describes: the first u32 2 after the header that
// is followed by a chain of pieces ending exactly at the end of the file whose first piece starts with a zlib header.
struct Civ5SaveParts
{
	Civ5SaveHeader header;
	std::string headerBytes;
	std::string pregame;
	std::vector<std::pair<size_t, size_t> > pieces;	// offset and length of each compressed piece in the file
};
bool Civ5Split(const std::string& file, Civ5SaveParts& parts, std::string& why);
// Inflates the pieces of `file` (as Civ5Split found them) into `body`.
bool Civ5Inflate(const std::string& file, const Civ5SaveParts& parts, std::string& body, std::string& why);
// Deflates a body as the game does and appends u32 2 and the pieces to `out`.
bool Civ5AppendBody(const std::string& body, std::string& out, std::string& why);
// header bytes + pregame + u32 2 + deflated body.
bool Civ5Pack(const std::string& headerBytes, const std::string& pregame, const std::string& body, std::string& out,
              std::string& why);

// zlib's version string as compiled in (expect "1.2.3").
const char* Civ5ZlibVersion();

// Whole-file I/O helpers (binary).
bool Civ5ReadFile(const char* szPath, std::string& data);
// Writes <path>.tmp and renames it over <path>, so no half file is ever left at <path>.
bool Civ5WriteFile(const char* szPath, const std::string& data, std::string& why);
