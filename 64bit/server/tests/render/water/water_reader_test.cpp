// water_reader_test.cpp - the water mesh tap's reader (server/src/render/srv_water_tap.cpp) as a command, for the tests
// of issue 3603 (shell/tests/test_water_tap.py): it maps a main section by name, runs srvrender::WaterTapReader::poll()
// until it has read and acked one copy (or the time is up), and reports what the reader decided.
// Written 2026-11-13 for this repository (GPL-3.0-or-later).
//
//   water_reader_test.exe --section NAME [--seconds S] [--json OUT] [--copies N] [--keep-polling S]
//
// --copies N: wait until N copies were acked (default 1). --keep-polling S: poll S more seconds after that (the
// requestSeq step). JSON: {"result", "why", "accepted", "refused", "requests", "loadSeq", "image": {...}, "records":
// [{system, role, index, flags, dataBytes, fnv1a64, stride, count, dxgiFormat, exeAddress, exeOffset, topology,
// vbRecord, ibRecord, vertexCount, indexCount, material, pass, dataFnvCheck}]}.
// Exit: 0 the last copy was accepted; 1 it was refused (ackResult in the JSON and on stdout); 2 usage; 3 the main
// section does not open; 4 no READY copy within the time.
#include "srv_water_tap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

using namespace srvrender;

static void HostLog(const char* line) { printf("%s\n", line); fflush(stdout); }

static uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

static std::string JsonEsc(const std::string& s)
{
	std::string o;
	for(char c : s)
	{
		if(c == '"' || c == '\\')
			o += '\\';
		if((unsigned char)c < 0x20)
			continue;
		o += c;
	}
	return o;
}

int wmain(int argc, wchar_t** argv)
{
	const wchar_t* section = nullptr;
	const wchar_t* json = nullptr;
	double seconds = 10, keep = 0;
	int copies = 1;
	for(int i = 1; i < argc; ++i)
	{
		if(!wcscmp(argv[i], L"--section") && i + 1 < argc)
			section = argv[++i];
		else if(!wcscmp(argv[i], L"--json") && i + 1 < argc)
			json = argv[++i];
		else if(!wcscmp(argv[i], L"--seconds") && i + 1 < argc)
			seconds = _wtof(argv[++i]);
		else if(!wcscmp(argv[i], L"--keep-polling") && i + 1 < argc)
			keep = _wtof(argv[++i]);
		else if(!wcscmp(argv[i], L"--copies") && i + 1 < argc)
			copies = _wtoi(argv[++i]);
		else
		{
			printf("usage: water_reader_test --section NAME [--seconds S] [--json OUT] [--copies N] [--keep-polling S] (unknown '%ls')\n", argv[i]);
			return 2;
		}
	}
	if(!section || seconds <= 0 || copies < 1)
	{
		printf("usage: water_reader_test --section NAME [--seconds S] [--json OUT] [--copies N] [--keep-polling S]: --section required\n");
		return 2;
	}
	LogOpen(nullptr, HostLog);
	HANDLE m = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, section);
	VPShellHeader* hdr = m ? (VPShellHeader*)MapViewOfFile(m, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_SECTION_BYTES) : nullptr;
	if(!hdr)
	{
		printf("water_reader_test: main section '%ls' does not open (error %lu)\n", section, GetLastError());
		return 3;
	}
	WaterTapReader r(hdr);
	const int64_t end = Qpc() + (int64_t)(seconds * QpcFreq());
	int64_t keepEnd = 0;
	while(Qpc() < end || (keepEnd && Qpc() < keepEnd))
	{
		r.poll();
		if(!keepEnd && r.accepted + r.refused >= copies)
		{
			if(keep <= 0)
				break;
			keepEnd = Qpc() + (int64_t)(keep * QpcFreq());
		}
		Sleep(5);
	}
	const bool any = r.accepted + r.refused > 0;
	printf("water_reader_test: accepted %lld refused %lld requests %lld last result %d%s%s\n", (long long)r.accepted,
	       (long long)r.refused, (long long)r.requests, r.lastResult(), r.lastWhy().empty() ? "" : ": ", r.lastWhy().c_str());
	if(json)
	{
		FILE* f = _wfopen(json, L"w");
		if(f)
		{
			const VPShellWaterImage& im = r.image();
			fprintf(f, "{\"result\": %d, \"why\": \"%s\", \"accepted\": %lld, \"refused\": %lld, \"requests\": %lld, \"loadSeq\": %u,\n",
			        r.lastResult(), JsonEsc(r.lastWhy()).c_str(), (long long)r.accepted, (long long)r.refused, (long long)r.requests, r.loadSeq());
			fprintf(f, " \"image\": {\"recordCount\": %u, \"loadSeq\": %u, \"exePid\": %u, \"mapW\": %u, \"mapH\": %u, \"waterZ\": %.6f, \"imageBytes\": %llu},\n",
			        im.recordCount, im.loadSeq, im.exePid, im.mapW, im.mapH, im.waterZ, (unsigned long long)im.imageBytes);
			fprintf(f, " \"records\": [");
			const auto& recs = r.records();
			for(size_t i = 0; i < recs.size(); ++i)
			{
				const VPShellWaterRecord& x = recs[i].rec;
				fprintf(f, "%s\n  {\"system\": %u, \"role\": %u, \"index\": %u, \"flags\": %u, \"dataBytes\": %llu, \"fnv1a64\": \"%016llX\", "
				           "\"stride\": %u, \"count\": %u, \"dxgiFormat\": %u, \"exeAddress\": %u, \"exeOffset\": %u, \"topology\": %u, "
				           "\"vbRecord\": %d, \"ibRecord\": %d, \"vertexCount\": %u, \"indexCount\": %u, \"material\": %u, \"pass\": %u, "
				           "\"dataFnvCheck\": \"%016llX\"}",
				        i ? "," : "", x.system, x.role, x.index, x.flags, (unsigned long long)x.dataBytes, (unsigned long long)x.fnv1a64,
				        x.stride, x.count, x.dxgiFormat, x.exeAddress, x.exeOffset, x.topology, (int)x.vbRecord, (int)x.ibRecord,
				        x.vertexCount, x.indexCount, x.material, x.pass,
				        (unsigned long long)(recs[i].data.empty() ? 0 : Fnv(recs[i].data.data(), recs[i].data.size())));
			}
			fprintf(f, "\n ]}\n");
			fclose(f);
		}
	}
	UnmapViewOfFile(hdr);
	CloseHandle(m);
	if(!any)
		return 4;
	return r.lastResult() == WTACK_OK ? 0 : 1;
}
