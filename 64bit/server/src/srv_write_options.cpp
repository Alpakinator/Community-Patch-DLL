// srv_write_options.cpp - server.exe --write-options / --write-options-schema (one-off 9081). Hand-written.
// Interface and formats: srv_write_options.h. Self-contained on purpose (no srv_common.h, no DLL header): the
// hermetic installer tests link it with options/src/vp64_options.cpp alone.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_write_options.h"
#include "vp64_options.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace
{
const char kVerbIni[] = "--write-options";
const char kVerbSchema[] = "--write-options-schema";

// Writes the bytes to path (replacing a file there); deletes what it wrote on a failure. False with why.
bool WriteAll(const char* path, const std::string& bytes, std::string& why)
{
	HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		char e[160];
		_snprintf_s(e, sizeof(e), _TRUNCATE, "cannot create %s (error %lu)", path, GetLastError());
		why = e;
		return false;
	}
	DWORD put = 0;
	const BOOL ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &put, NULL) && put == (DWORD)bytes.size();
	const DWORD err = ok ? 0 : GetLastError();
	const BOOL closed = CloseHandle(h);
	if (!ok || !closed)
	{
		DeleteFileA(path);
		char e[160];
		_snprintf_s(e, sizeof(e), _TRUNCATE, "cannot write %s (error %lu)", path, err);
		why = e;
		return false;
	}
	return true;
}

void PutEscaped(std::string& out, const char* s)
{
	for (; s && *s; ++s)
	{
		switch (*s)
		{
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out += *s; break;
		}
	}
}

void PutInt(std::string& out, int v)
{
	char b[16];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%d", v);
	out += b;
}

std::string SchemaText()
{
	std::string t;
	for (int i = 0; i < Vp64Opt::kCount; ++i)
	{
		const Vp64Opt::Def& d = Vp64Opt::Definition((Vp64Opt::Id)i);
		if (!d.key || !d.key[0] || d.testOnly) continue;   // a reserved id; a test value, not offered (ticket 4983, D243)
		PutInt(t, i);
		t += '\t';
		t += d.section;
		t += '\t';
		t += d.key;
		t += '\t';
		PutInt(t, d.defaultValue);
		t += '\t';
		PutInt(t, d.minValue);
		t += '\t';
		PutInt(t, d.maxValue);
		t += '\t';
		PutInt(t, (int)d.category);
		t += '\t';
		t += (d.needsKey && d.needsKey[0]) ? d.needsKey : "-";
		t += '\t';
		PutEscaped(t, d.title ? d.title : "");   // one-off 9081: the row's label on the installer's options page
		t += '\t';
		PutEscaped(t, d.help);
		t += '\n';
	}
	// Ticket 0745 (D255 G1): after the whole-number rows of srv_write_options.h, one row per text option (Vp64Opt::TextDef,
	// ticket 0680), so that the installer's options page can offer it (tools/installer/options_schema.py):
	//   t<text id> <TAB> section <TAB> key <TAB> default text <TAB> - <TAB> - <TAB> - <TAB> - <TAB> title <TAB> help
	// the default text, the title and the help escaped as above.
	for (int i = 0; i < Vp64Opt::kTextCount; ++i)
	{
		const Vp64Opt::TextDef& d = Vp64Opt::TextDefinition((Vp64Opt::TextId)i);
		t += 't';
		PutInt(t, i);
		t += '\t';
		t += d.section;
		t += '\t';
		t += d.key;
		t += '\t';
		PutEscaped(t, d.defaultValue);
		t += "\t-\t-\t-\t-\t";
		PutEscaped(t, d.title ? d.title : "");
		t += '\t';
		PutEscaped(t, d.help);
		t += '\n';
	}
	return t;
}

std::string IniText()
{
	const size_t n = Vp64Opt::DefaultFileText(NULL, 0);
	std::string t(n + 1, '\0');
	Vp64Opt::DefaultFileText(&t[0], n + 1);
	t.resize(n);
	return t;
}

int Usage(const char* why)
{
	fprintf(stderr, "server: %s\nusage: server.exe --write-options <ini path> --write-options-schema <tsv path> "
	                "(either or both)\n", why);
	return 2;
}
}  // namespace

int SrvWriteOptionsVerb(int argc, char** argv)
{
	bool any = false;
	for (int i = 1; i < argc; ++i)
		if (argv[i] && (!strcmp(argv[i], kVerbIni) || !strcmp(argv[i], kVerbSchema))) any = true;
	if (!any) return -1;
	const char* ini = NULL;
	const char* schema = NULL;
	for (int i = 1; i < argc; ++i)
	{
		const bool isIni = !strcmp(argv[i], kVerbIni);
		const bool isSchema = !strcmp(argv[i], kVerbSchema);
		if (!isIni && !isSchema) return Usage("the options verbs take no other argument");
		if (i + 1 >= argc || !argv[i + 1][0]) return Usage("a verb without its path");
		const char*& slot = isIni ? ini : schema;
		if (slot) return Usage("a verb given twice");
		slot = argv[++i];
	}
	std::string why;
	if (ini && !WriteAll(ini, IniText(), why))
	{
		fprintf(stderr, "server: --write-options: %s\n", why.c_str());
		return 1;
	}
	if (schema && !WriteAll(schema, SchemaText(), why))
	{
		fprintf(stderr, "server: --write-options-schema: %s\n", why.c_str());
		return 1;
	}
	return 0;
}
