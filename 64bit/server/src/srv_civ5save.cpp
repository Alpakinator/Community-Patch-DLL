// The .Civ5Save container (issue 2001). See srv_civ5save.h for the layout.
#include "srv_civ5save.h"
#include <string.h>
#include <stdio.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <zlib.h>

const char* const kCiv5GameVersion = "1.0.3.279 (403694)";
const char* const kCiv5Build = "403694";
// {0d41152c-948e-498f-8cdf-699d04c98a95} in memory order
const unsigned char kCiv5ConstGuid[16] = {0x2c, 0x15, 0x41, 0x0d, 0x8e, 0x94, 0x8f, 0x49,
                                          0x8c, 0xdf, 0x69, 0x9d, 0x04, 0xc9, 0x8a, 0x95};

namespace
{
	const size_t kPiece = 65536;	// the game's piece size (bytes of compressed data per piece)
	const unsigned int kMaxString = 1u << 20;

	struct Rd
	{
		const std::string& b;
		size_t p;
		bool ok;
		Rd(const std::string& s) : b(s), p(0), ok(true) {}
		bool Need(size_t n)
		{
			if(!ok || b.size() - p < n || p > b.size())
				ok = false;
			return ok;
		}
		unsigned int U32()
		{
			if(!Need(4))
				return 0;
			const unsigned char* q = (const unsigned char*)b.data() + p;
			p += 4;
			return q[0] | (q[1] << 8) | (q[2] << 16) | ((unsigned int)q[3] << 24);
		}
		unsigned char U8()
		{
			if(!Need(1))
				return 0;
			return (unsigned char)b[p++];
		}
		void Raw(void* dst, size_t n)
		{
			if(!Need(n))
				return;
			memcpy(dst, b.data() + p, n);
			p += n;
		}
		std::string Str()
		{
			const unsigned int n = U32();
			if(n > kMaxString || !Need(n))
			{
				ok = false;
				return std::string();
			}
			std::string s(b.data() + p, n);
			p += n;
			return s;
		}
	};

	void PutU32(std::string& o, unsigned int v)
	{
		const char c[4] = {(char)(v & 0xff), (char)((v >> 8) & 0xff), (char)((v >> 16) & 0xff), (char)(v >> 24)};
		o.append(c, 4);
	}
	void PutStr(std::string& o, const std::string& s)
	{
		PutU32(o, (unsigned int)s.size());
		o += s;
	}
	unsigned int GetU32(const std::string& b, size_t p)
	{
		const unsigned char* q = (const unsigned char*)b.data() + p;
		return q[0] | (q[1] << 8) | (q[2] << 16) | ((unsigned int)q[3] << 24);
	}
}

Civ5SaveHeader::Civ5SaveHeader() : saveVersion(8), gameVersion(kCiv5GameVersion), build(kCiv5Build), turn(0), gameType(0),
                                   tail(0)
{
	memset(dllGuid, 0, sizeof(dllGuid));
}

const char* Civ5ZlibVersion() { return zlibVersion(); }

bool Civ5ParseHeader(const std::string& data, Civ5SaveHeader& h, size_t& end, std::string& why)
{
	Rd r(data);
	char magic[4] = {0};
	r.Raw(magic, 4);
	if(!r.ok || memcmp(magic, "CIV5", 4))
	{
		why = "not a .Civ5Save (no CIV5 magic)";
		return false;
	}
	h.saveVersion = r.U32();
	h.gameVersion = r.Str();
	h.build = r.Str();
	h.turn = r.U32();
	h.gameType = r.U8();
	for(int i = 0; i < 7; ++i)
		h.types[i] = r.Str();
	const unsigned int nDlc = r.U32();
	h.dlc.clear();
	for(unsigned int i = 0; r.ok && i < nDlc; ++i)
	{
		Civ5SaveHeader::Dlc d;
		r.Raw(d.guid, 16);
		d.one = r.U32();
		d.name = r.Str();
		h.dlc.push_back(d);
	}
	const unsigned int nMods = r.U32();
	h.mods.clear();
	for(unsigned int i = 0; r.ok && i < nMods; ++i)
	{
		Civ5SaveHeader::Mod m;
		m.id = r.Str();
		m.version = r.U32();
		m.name = r.Str();
		h.mods.push_back(m);
	}
	h.leaderName = r.Str();
	h.civName = r.Str();
	h.playerColor = r.Str();
	r.Raw(h.dllGuid, 16);
	h.dllVersion = r.Str();
	unsigned char g[16] = {0};
	r.Raw(g, 16);
	h.tail = r.U32();
	if(!r.ok)
	{
		why = "header truncated or malformed";
		return false;
	}
	if(memcmp(g, kCiv5ConstGuid, 16))
	{
		why = "constant GUID {0d41152c-948e-498f-8cdf-699d04c98a95} not found at the end of the header";
		return false;
	}
	end = r.p;
	return true;
}

std::string Civ5WriteHeader(const Civ5SaveHeader& h)
{
	std::string o("CIV5", 4);
	PutU32(o, h.saveVersion);
	PutStr(o, h.gameVersion);
	PutStr(o, h.build);
	PutU32(o, h.turn);
	o += (char)h.gameType;
	for(int i = 0; i < 7; ++i)
		PutStr(o, h.types[i]);
	PutU32(o, (unsigned int)h.dlc.size());
	for(size_t i = 0; i < h.dlc.size(); ++i)
	{
		o.append((const char*)h.dlc[i].guid, 16);
		PutU32(o, h.dlc[i].one);
		PutStr(o, h.dlc[i].name);
	}
	PutU32(o, (unsigned int)h.mods.size());
	for(size_t i = 0; i < h.mods.size(); ++i)
	{
		PutStr(o, h.mods[i].id);
		PutU32(o, h.mods[i].version);
		PutStr(o, h.mods[i].name);
	}
	PutStr(o, h.leaderName);
	PutStr(o, h.civName);
	PutStr(o, h.playerColor);
	o.append((const char*)h.dllGuid, 16);
	PutStr(o, h.dllVersion);
	o.append((const char*)kCiv5ConstGuid, 16);
	PutU32(o, h.tail);
	return o;
}

bool Civ5Split(const std::string& file, Civ5SaveParts& parts, std::string& why)
{
	size_t end = 0;
	if(!Civ5ParseHeader(file, parts.header, end, why))
		return false;
	parts.headerBytes.assign(file.data(), end);
	static const char kMarker[4] = {2, 0, 0, 0};
	const size_t n = file.size();
	for(size_t p = end; p + 8 <= n; ++p)
	{
		if(memcmp(file.data() + p, kMarker, 4))
			continue;
		size_t q = p + 4;
		bool ok = true;
		std::vector<std::pair<size_t, size_t> > pieces;
		while(q < n)
		{
			if(n - q < 4)
			{
				ok = false;
				break;
			}
			const size_t len = GetU32(file, q);
			if(len == 0 || len > n - q - 4)
			{
				ok = false;
				break;
			}
			pieces.push_back(std::make_pair(q + 4, len));
			q += 4 + len;
		}
		if(ok && !pieces.empty() && (unsigned char)file[pieces[0].first] == 0x78)
		{
			parts.pregame.assign(file.data() + end, p - end);
			parts.pieces.swap(pieces);
			return true;
		}
	}
	why = "body marker (u32 2 followed by a piece chain to the end of the file) not found";
	return false;
}

bool Civ5Inflate(const std::string& file, const Civ5SaveParts& parts, std::string& body, std::string& why)
{
	z_stream z;
	memset(&z, 0, sizeof(z));
	if(inflateInit(&z) != Z_OK)
	{
		why = "inflateInit failed";
		return false;
	}
	body.clear();
	std::vector<unsigned char> buf(1 << 20);
	bool ok = true;
	for(size_t i = 0; ok && i < parts.pieces.size(); ++i)
	{
		z.next_in = (Bytef*)(file.data() + parts.pieces[i].first);
		z.avail_in = (uInt)parts.pieces[i].second;
		const int flush = i + 1 == parts.pieces.size() ? Z_SYNC_FLUSH : Z_NO_FLUSH;
		do
		{
			z.next_out = &buf[0];
			z.avail_out = (uInt)buf.size();
			const int rc = inflate(&z, flush);
			if(rc != Z_OK && rc != Z_BUF_ERROR && rc != Z_STREAM_END)
			{
				char sz[160];
				sprintf_s(sz, "inflate error %d (%s) in piece %zu", rc, z.msg ? z.msg : "", i);
				why = sz;
				ok = false;
				break;
			}
			body.append((const char*)&buf[0], buf.size() - z.avail_out);
			if(rc == Z_STREAM_END)
				break;
		} while(z.avail_in > 0 || z.avail_out == 0);
	}
	inflateEnd(&z);
	return ok;
}

bool Civ5AppendBody(const std::string& body, std::string& out, std::string& why)
{
	// As the game: deflateInit at the default level (6), windowBits 15, memLevel 8; all input with Z_NO_FLUSH, then
	// Z_SYNC_FLUSH; no deflateEnd output (no final block, no Adler-32). One 64 KiB output buffer that is written out as a
	// piece only when it is full (and at the end): the Z_SYNC_FLUSH calls continue in the buffer the Z_NO_FLUSH calls
	// left partly filled. That matters (issue 2001): when the last data block does not fit into what is left of the
	// buffer, zlib 1.2.3 returns before the sync marker, and the next Z_SYNC_FLUSH call emits an empty static block
	// (10 bits) before the empty stored block. The game's files show exactly that in 30 of 155 saves.
	z_stream z;
	memset(&z, 0, sizeof(z));
	if(deflateInit(&z, Z_DEFAULT_COMPRESSION) != Z_OK)
	{
		why = "deflateInit failed";
		return false;
	}
	std::string comp;
	std::vector<unsigned char> buf(kPiece);
	z.next_in = (Bytef*)body.data();
	z.avail_in = (uInt)body.size();
	z.next_out = &buf[0];
	z.avail_out = (uInt)buf.size();
	int flush = Z_NO_FLUSH;
	for(;;)
	{
		const int rc = deflate(&z, flush);
		if(rc != Z_OK && rc != Z_BUF_ERROR)
		{
			why = "deflate failed";
			deflateEnd(&z);
			return false;
		}
		if(z.avail_out == 0)
		{
			comp.append((const char*)&buf[0], buf.size());
			z.next_out = &buf[0];
			z.avail_out = (uInt)buf.size();
			continue;
		}
		if(z.avail_in != 0)
			continue;
		if(flush == Z_SYNC_FLUSH)
			break;
		flush = Z_SYNC_FLUSH;
	}
	comp.append((const char*)&buf[0], buf.size() - z.avail_out);
	deflateEnd(&z);
	PutU32(out, 2);
	for(size_t i = 0; i < comp.size(); i += kPiece)
	{
		const size_t len = comp.size() - i < kPiece ? comp.size() - i : kPiece;
		PutU32(out, (unsigned int)len);
		out.append(comp, i, len);
	}
	return true;
}

bool Civ5Pack(const std::string& headerBytes, const std::string& pregame, const std::string& body, std::string& out,
              std::string& why)
{
	out = headerBytes;
	out += pregame;
	return Civ5AppendBody(body, out, why);
}

bool Civ5ReadFile(const char* szPath, std::string& data)
{
	FILE* f = NULL;
	if(fopen_s(&f, szPath, "rb") || !f)
		return false;
	_fseeki64(f, 0, SEEK_END);
	const long long n = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	data.resize((size_t)(n > 0 ? n : 0));
	const size_t r = n > 0 ? fread(&data[0], 1, (size_t)n, f) : 0;
	fclose(f);
	return r == data.size();
}

bool Civ5WriteFile(const char* szPath, const std::string& data, std::string& why)
{
	const std::string tmp = std::string(szPath) + ".tmp";
	FILE* f = NULL;
	if(fopen_s(&f, tmp.c_str(), "wb") || !f)
	{
		why = "cannot write " + tmp;
		return false;
	}
	const size_t w = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
	const bool closed = fclose(f) == 0;
	if(w != data.size() || !closed)
	{
		why = "short write " + tmp;
		DeleteFileA(tmp.c_str());
		return false;
	}
	if(!MoveFileExA(tmp.c_str(), szPath, MOVEFILE_REPLACE_EXISTING))
	{
		why = "cannot rename " + tmp + " to " + szPath;
		return false;
	}
	return true;
}
