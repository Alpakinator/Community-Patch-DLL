// civ5save.exe: the server's native .Civ5Save container code (srv_civ5save.cpp, zlib 1.2.3) as a command-line tool,
// built by server/build_server.py next to server.exe (issue 2001). No DLL needed. Reads saves; writes only the named
// outputs.
//
//   civ5save.exe info SAVE                     header fields, part sizes
//   civ5save.exe unpack SAVE OUTBASE           OUTBASE.header.bin, .pregame.bin, .body.bin
//   civ5save.exe pack HEADER PREGAME BODY OUT  a .Civ5Save
//   civ5save.exe repack SAVE OUT               unpack and pack again natively; says whether OUT equals SAVE
//   civ5save.exe corpus [--json FILE] DIR...   every *.Civ5Save under DIR (recursive), unpacked and repacked in memory
//                                              and compared with the original; header re-serialized and compared
// Exit: 0 success (repack/corpus: all identical), 1 a difference, 2 error.
#include "../srv_civ5save.h"
#include <stdio.h>
#include <string.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
	std::string Hex(const unsigned char* p, size_t n)
	{
		std::string s;
		char sz[3];
		for(size_t i = 0; i < n; ++i)
		{
			sprintf_s(sz, "%02x", p[i]);
			s += sz;
		}
		return s;
	}
	std::string Json(const std::string& s)
	{
		std::string o = "\"";
		for(size_t i = 0; i < s.size(); ++i)
		{
			const unsigned char c = (unsigned char)s[i];
			if(c == '"' || c == '\\')
				o += '\\', o += (char)c;
			else if(c < 0x20)
			{
				char sz[8];
				sprintf_s(sz, "\\u%04x", c);
				o += sz;
			}
			else
				o += (char)c;
		}
		return o + "\"";
	}
	size_t FirstDiff(const std::string& a, const std::string& b)
	{
		const size_t n = a.size() < b.size() ? a.size() : b.size();
		for(size_t i = 0; i < n; ++i)
			if(a[i] != b[i])
				return i;
		return n;
	}

	int Info(const char* szSave)
	{
		std::string file, why;
		Civ5SaveParts parts;
		if(!Civ5ReadFile(szSave, file) || !Civ5Split(file, parts, why))
		{
			fprintf(stderr, "civ5save: %s: %s\n", szSave, why.empty() ? "cannot read" : why.c_str());
			return 2;
		}
		const Civ5SaveHeader& h = parts.header;
		printf("{\n \"saveVersion\": %u,\n \"gameVersion\": %s,\n \"build\": %s,\n \"turn\": %u,\n \"gameType\": %u,\n \"types\": [",
		       h.saveVersion, Json(h.gameVersion).c_str(), Json(h.build).c_str(), h.turn, h.gameType);
		for(int i = 0; i < 7; ++i)
			printf("%s%s", i ? ", " : "", Json(h.types[i]).c_str());
		printf("],\n \"dlc\": [");
		for(size_t i = 0; i < h.dlc.size(); ++i)
			printf("%s\n  [\"%s\", %u, %s]", i ? "," : "", Hex(h.dlc[i].guid, 16).c_str(), h.dlc[i].one, Json(h.dlc[i].name).c_str());
		printf("],\n \"mods\": [");
		for(size_t i = 0; i < h.mods.size(); ++i)
			printf("%s\n  [%s, %u, %s]", i ? "," : "", Json(h.mods[i].id).c_str(), h.mods[i].version, Json(h.mods[i].name).c_str());
		printf("],\n \"names\": [%s, %s, %s],\n \"dllGuid\": \"%s\",\n \"dllVersion\": %s,\n \"zero\": %u,\n",
		       Json(h.leaderName).c_str(), Json(h.civName).c_str(), Json(h.playerColor).c_str(), Hex(h.dllGuid, 16).c_str(),
		       Json(h.dllVersion).c_str(), h.tail);
		printf(" \"headerBytes\": %zu,\n \"pregameBytes\": %zu,\n \"bodyPieces\": %zu,\n \"zlib\": \"%s\"\n}\n",
		       parts.headerBytes.size(), parts.pregame.size(), parts.pieces.size(), Civ5ZlibVersion());
		return 0;
	}

	// Unpacks and repacks one file in memory. Returns 0 identical, 1 different, 2 error; `detail` says how.
	int Repack(const std::string& file, std::string& out, std::string& detail)
	{
		Civ5SaveParts parts;
		std::string why, body;
		if(!Civ5Split(file, parts, why) || !Civ5Inflate(file, parts, body, why) ||
		   !Civ5Pack(Civ5WriteHeader(parts.header), parts.pregame, body, out, why))
		{
			detail = why;
			return 2;
		}
		char sz[256];
		if(Civ5WriteHeader(parts.header) != parts.headerBytes)
		{
			detail = "header re-serialization differs";
			return 1;
		}
		if(out == file)
		{
			sprintf_s(sz, "header %zu, pregame %zu, body %zu inflated from %zu pieces", parts.headerBytes.size(),
			          parts.pregame.size(), body.size(), parts.pieces.size());
			detail = sz;
			return 0;
		}
		const size_t d = FirstDiff(file, out);
		sprintf_s(sz, "size %zu vs repacked %zu, first difference at %zu (body starts at %zu)", file.size(), out.size(), d,
		          parts.headerBytes.size() + parts.pregame.size() + 4);
		detail = sz;
		std::string ctx = " original ";
		for(size_t i = d > 8 ? d - 8 : 0; i < file.size() && i < d + 8; ++i)
		{
			sprintf_s(sz, "%02x", (unsigned char)file[i]);
			ctx += sz;
		}
		ctx += " repacked ";
		for(size_t i = d > 8 ? d - 8 : 0; i < out.size() && i < d + 8; ++i)
		{
			sprintf_s(sz, "%02x", (unsigned char)out[i]);
			ctx += sz;
		}
		detail += ctx;
		return 1;
	}

	void Walk(const std::string& dir, std::vector<std::string>& files)
	{
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			return;
		std::vector<std::string> subdirs;
		do
		{
			const std::string n = fd.cFileName;
			if(n == "." || n == "..")
				continue;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				subdirs.push_back(dir + "\\" + n);
			else if(n.size() > 9 && _stricmp(n.c_str() + n.size() - 9, ".Civ5Save") == 0)
				files.push_back(dir + "\\" + n);
		} while(FindNextFileA(h, &fd));
		FindClose(h);
		for(size_t i = 0; i < subdirs.size(); ++i)
			Walk(subdirs[i], files);
	}

	int Corpus(int argc, char** argv)
	{
		const char* szJson = NULL;
		std::vector<std::string> files;
		for(int i = 0; i < argc; ++i)
		{
			if(!strcmp(argv[i], "--json") && i + 1 < argc)
				szJson = argv[++i];
			else
				Walk(argv[i], files);
		}
		int nSame = 0, nDiff = 0, nErr = 0;
		std::string js = "{\n \"zlib\": \"" + std::string(Civ5ZlibVersion()) + "\",\n \"files\": [";
		for(size_t i = 0; i < files.size(); ++i)
		{
			std::string file, out, detail;
			int rc = 2;
			if(!Civ5ReadFile(files[i].c_str(), file))
				detail = "cannot read";
			else
				rc = Repack(file, out, detail);
			(rc == 0 ? nSame : rc == 1 ? nDiff : nErr)++;
			const char* szRes = rc == 0 ? "identical" : rc == 1 ? "DIFFERENT" : "ERROR";
			printf("%-9s %10zu  %s  %s\n", szRes, file.size(), files[i].c_str(), rc ? detail.c_str() : "");
			char sz[64];
			sprintf_s(sz, "%zu", file.size());
			js += std::string(i ? "," : "") + "\n  {\"file\": " + Json(files[i]) + ", \"bytes\": " + sz + ", \"result\": \"" +
			      szRes + "\", \"detail\": " + Json(detail) + "}";
		}
		char sz[256];
		sprintf_s(sz, "files %zu, identical %d, different %d, errors %d", files.size(), nSame, nDiff, nErr);
		printf("%s (zlib %s)\n", sz, Civ5ZlibVersion());
		js += "\n ],\n \"summary\": " + Json(sz) + "\n}\n";
		if(szJson)
		{
			std::string why;
			if(!Civ5WriteFile(szJson, js, why))
				fprintf(stderr, "civ5save: %s\n", why.c_str());
		}
		return nErr ? 2 : nDiff ? 1 : 0;
	}

	bool Write(const std::string& path, const std::string& data)
	{
		std::string why;
		if(Civ5WriteFile(path.c_str(), data, why))
			return true;
		fprintf(stderr, "civ5save: %s\n", why.c_str());
		return false;
	}
}

int main(int argc, char** argv)
{
	if(argc >= 3 && !strcmp(argv[1], "info"))
		return Info(argv[2]);
	if(argc >= 2 && !strcmp(argv[1], "corpus"))
		return Corpus(argc - 2, argv + 2);
	if(argc >= 4 && !strcmp(argv[1], "unpack"))
	{
		std::string file, why, body;
		Civ5SaveParts parts;
		if(!Civ5ReadFile(argv[2], file) || !Civ5Split(file, parts, why) || !Civ5Inflate(file, parts, body, why))
		{
			fprintf(stderr, "civ5save: %s: %s\n", argv[2], why.empty() ? "cannot read" : why.c_str());
			return 2;
		}
		const std::string base = argv[3];
		if(!Write(base + ".header.bin", parts.headerBytes) || !Write(base + ".pregame.bin", parts.pregame) ||
		   !Write(base + ".body.bin", body))
			return 2;
		printf("header %zu B, pregame %zu B, body %zu B inflated from %zu pieces; turn %u\n", parts.headerBytes.size(),
		       parts.pregame.size(), body.size(), parts.pieces.size(), parts.header.turn);
		return 0;
	}
	if(argc >= 6 && !strcmp(argv[1], "pack"))
	{
		std::string hb, pre, body, out, why;
		if(!Civ5ReadFile(argv[2], hb) || !Civ5ReadFile(argv[3], pre) || !Civ5ReadFile(argv[4], body))
		{
			fprintf(stderr, "civ5save: cannot read an input\n");
			return 2;
		}
		if(!Civ5Pack(hb, pre, body, out, why))
		{
			fprintf(stderr, "civ5save: %s\n", why.c_str());
			return 2;
		}
		if(!Write(argv[5], out))
			return 2;
		printf("%s: %zu B\n", argv[5], out.size());
		return 0;
	}
	if(argc >= 4 && !strcmp(argv[1], "repack"))
	{
		std::string file, out, detail;
		if(!Civ5ReadFile(argv[2], file))
		{
			fprintf(stderr, "civ5save: cannot read %s\n", argv[2]);
			return 2;
		}
		const int rc = Repack(file, out, detail);
		if(rc != 2 && !Write(argv[3], out))
			return 2;
		printf("%s: %s\n", rc == 0 ? "identical" : rc == 1 ? "DIFFERENT" : "ERROR", detail.c_str());
		return rc;
	}
	fprintf(stderr, "usage: civ5save.exe info SAVE | unpack SAVE OUTBASE | pack HEADER PREGAME BODY OUT | repack SAVE OUT |"
	                " corpus [--json FILE] DIR...\n");
	return 2;
}
