// Two-thread test of Database::Connection::Count (issue 0165).
//
// The server's crash of queue ticket 0153: two threads Count the same table on one Connection; one resets or steps the
// kept "select count(*)" statement while the other reads its column, and sqlite3_column_int dereferences a NULL result
// row (read of 0x24). This program does exactly that, without the game: one Connection on a scratch database with a
// 12-row table "Months", one UPDATE that changes a row (so sqlite3_changes() is not 0 and every Count steps the kept
// statement, as in the game after its post-processing writes), then N threads call Count("Months") in a loop.
//
// Usage: count_threads <scratch database file> <threads> <milliseconds>
// Output: one line "count_threads: threads N, calls C, wrong W, ms M" and exit 0 when every call returned 12; exit 1
// when a call returned anything else; the process dies with 0xC0000005 when the race hits the column read.
// Layout-agnostic like ab/dbquery.cpp: the Connection is built with placement new into a large buffer.
#define CvGameDatabaseAPI __declspec(dllimport)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "CvGameDatabase.h"

using namespace Database;

namespace
{
struct alignas(64) Buf
{
	unsigned char b[4096];
};

Buf g_buf;
Connection* g_pConn = NULL;
volatile LONG g_lStop = 0;
volatile LONG g_lCalls = 0;
volatile LONG g_lWrong = 0;
volatile LONG g_lFirstWrong = 0;

DWORD WINAPI Worker(void*)
{
	while (!g_lStop)
	{
		const int n = g_pConn->Count("Months", true);
		InterlockedIncrement(&g_lCalls);
		if (n != 12)
		{
			if (InterlockedIncrement(&g_lWrong) == 1)
				g_lFirstWrong = n;
		}
	}
	return 0;
}
}  // namespace

int main(int argc, char** argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "usage: count_threads <scratch database file> <threads> <milliseconds>\n");
		return 2;
	}
	const char* szFile = argv[1];
	const int nThreads = atoi(argv[2]);
	const int nMs = atoi(argv[3]);
	if (nThreads < 1 || nThreads > 64 || nMs < 1)
	{
		fprintf(stderr, "count_threads: threads must be 1..64 and milliseconds > 0\n");
		return 2;
	}
	DeleteFileA(szFile);

	memset(&g_buf, 0xCD, sizeof(g_buf));
	g_pConn = ::new (static_cast<void*>(&g_buf)) Connection();
	// SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX: serialized mode, as the game's connection
	// (the library is built THREADSAFE=1; the crash dump showed the connection mutex in use).
	if (!g_pConn->Open(szFile, 0x00000002 | 0x00000004 | 0x00010000))
	{
		fprintf(stderr, "count_threads: cannot open %s\n", szFile);
		return 2;
	}
	bool bOk = g_pConn->Execute("CREATE TABLE Months (ID integer primary key, Type text)");
	for (int i = 0; i < 12 && bOk; ++i)
	{
		char szSQL[128];
		_snprintf_s(szSQL, sizeof(szSQL), _TRUNCATE, "INSERT INTO Months (ID, Type) VALUES (%d, 'MONTH_%d')", i, i);
		bOk = g_pConn->Execute(szSQL);
	}
	bOk = bOk && g_pConn->Execute("UPDATE Months SET Type = 'MONTH_0X' WHERE ID = 0");
	if (!bOk || g_pConn->TotalChanges() != 1 || g_pConn->Count("Months", true) != 12)
	{
		fprintf(stderr, "count_threads: set-up failed (changes %d)\n", g_pConn->TotalChanges());
		return 2;
	}

	HANDLE ah[64];
	for (int i = 0; i < nThreads; ++i)
	{
		ah[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
		if (ah[i] == NULL)
		{
			fprintf(stderr, "count_threads: CreateThread failed (%lu)\n", GetLastError());
			return 2;
		}
	}
	Sleep((DWORD)nMs);
	InterlockedExchange(&g_lStop, 1);
	WaitForMultipleObjects((DWORD)nThreads, ah, TRUE, INFINITE);
	for (int i = 0; i < nThreads; ++i)
		CloseHandle(ah[i]);

	printf("count_threads: threads %d, calls %ld, wrong %ld (first %ld), ms %d\n", nThreads, g_lCalls, g_lWrong,
	       g_lFirstWrong, nMs);
	fflush(stdout);
	g_pConn->~Connection();
	DeleteFileA(szFile);
	return g_lWrong == 0 ? 0 : 1;
}
