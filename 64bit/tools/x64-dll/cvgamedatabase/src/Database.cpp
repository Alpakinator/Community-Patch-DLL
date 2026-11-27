// CvGameDatabase replacement: Database::Connection, Results and SingleResult over SQLite 3.7.17
// (c5-64bit-patch issues 1402 and 1403). Written from the fork's headers (CvGameDatabase/include/Database.h,
// DatabaseResults.h) and from the behaviour of the closed CvGameDatabaseWin32Final Release.dll as read in a Ghidra
// decompile of a copy (SHA-256 f95637398ce10012..., 2026-11-10). No code from any other implementation.
// Behaviour notes per member are in ../README.md; the x86 A/B (ab/run_ab.py) compares this file with the closed
// library on a full gameplay database.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "CvGameDatabase.h"
#include "sqlite3.h"

namespace Database
{

bool Connection::ms_bMemoryManagerInitialized = false;

namespace
{
// The closed library formats several messages with vsprintf_s into fixed buffers; same sizes here, but truncating.
void Format(char* buf, size_t size, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, size, _TRUNCATE, fmt, ap);
	va_end(ap);
}

// Issue 0165 (2026-11-14): the count caches (m_hshCountStatements, m_hshCountValues and the statements they keep) are
// shared by every thread that uses a Connection. The server runs the game DLL on one thread per IPC channel (ui, core,
// aux), and the EXE lets its UI thread call the DLL while the game-core thread is inside Update, so two threads can
// Count the same table at once: one reset or stepped the kept statement while the other read its column, and the
// server crashed in sqlite3_column_int (dump of queue ticket 0153). SQLite's own mutex protects each API call, not the
// prepare/step/read/reset sequence nor the maps. One process-wide recursive lock serialises every use of the kept
// statements and the maps: Count, ClearCountCache and Close (no member is added: the layout of Connection is fixed by the fork's header). Lock
// order: this lock, then SQLite's connection mutex; SQLite never calls back into this file under its mutex.
class CountLock
{
public:
	CountLock() { InitializeCriticalSection(&m_cs); }
	~CountLock() { DeleteCriticalSection(&m_cs); }
	void Enter() { EnterCriticalSection(&m_cs); }
	void Leave() { LeaveCriticalSection(&m_cs); }

private:
	CRITICAL_SECTION m_cs;
};
CountLock g_countLock;  // constructed when the DLL loads, before any Connection exists

class CountLockScope
{
public:
	CountLockScope() { g_countLock.Enter(); }
	~CountLockScope() { g_countLock.Leave(); }

private:
	CountLockScope(const CountLockScope&);
	CountLockScope& operator=(const CountLockScope&);
};

// Busy handler installed by Open(): log, sleep 2 ms, give up after 30 attempts.
int BusyHandler(void* pConnection, int iAttempts)
{
	char szMsg[128];
	Format(szMsg, sizeof(szMsg), "Database is busy. Sleeping for 2 ms... (%i) attempt(s).", iAttempts);
	static_cast<const Connection*>(pConnection)->LogWarning(szMsg);
	Sleep(2);
	return iAttempts < 30 ? 1 : 0;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------
// Connection
//------------------------------------------------------------------------------------------------------------------
void Connection::InitMemoryManagement()
{
	// The closed library also installs its own allocator, a static page cache (5000 x 1168 B), a scratch buffer and
	// turns lookaside off (sqlite3_config 4, 7, 6, 13). None of that changes a query result; only
	// CalculateMemoryStats would show it, so it is not reproduced. SQLITE_OMIT_AUTOINIT (the closed library's
	// option) makes the explicit initialize necessary.
	if (!ms_bMemoryManagerInitialized)
	{
		sqlite3_initialize();
		ms_bMemoryManagerInitialized = true;
	}
}

Connection::Connection() : m_db(NULL)
{
	InitMemoryManagement();
}

Connection::Connection(const char* filename, int flags) : m_db(NULL)
{
	InitMemoryManagement();
	Open(filename, flags);
}

Connection::~Connection()
{
	if (m_db != NULL)
		Close();
	// m_pkDatabaseLogger (auto_ptr) deletes the logger.
}

void* Connection::operator new(size_t tSize)
{
	return malloc(tSize);
}

void Connection::operator delete(void* pMem)
{
	free(pMem);
}

sqlite3* Connection::GetSQLite3() const
{
	return m_db;
}

bool Connection::Report(int status) const
{
	if (status == SQLITE_OK)
		return true;
	LogError(ErrorMessage());
	return false;
}

bool Connection::Open(const char* filename, int flags)
{
	if (filename == NULL)
		filename = ":memory:";
	// The closed library neither closes a previous handle nor clears m_db on failure (sqlite3_open_v2 returns a
	// handle carrying the error message for most failures).
	if (sqlite3_open_v2(filename, &m_db, flags, NULL) != SQLITE_OK)
	{
		LogError(ErrorMessage());
		return false;
	}
	sqlite3_busy_handler(m_db, BusyHandler, this);
	return true;
}

void Connection::Close()
{
	// Issue 0165: the whole of Close under the count lock, because the loop below also finalizes the kept count
	// statements (no Count of another thread may hold one meanwhile).
	const CountLockScope lock;
	// Forget the count caches (their statements are finalized below with every other statement).
	m_hshCountStatements.clear();
	m_hshCountValues.clear();
	if (m_db == NULL)
		return;
	// Finalizes every statement of the connection, including those still attached to live Results objects (as the
	// closed library does; such Results must not be used afterwards).
	sqlite3_stmt* pStmt = sqlite3_next_stmt(m_db, NULL);
	while (pStmt != NULL)
	{
		if (sqlite3_finalize(pStmt) != SQLITE_OK)
			LogError(ErrorMessage());
		pStmt = sqlite3_next_stmt(m_db, NULL);
	}
	if (sqlite3_close(m_db) == SQLITE_OK)
		m_db = NULL;
	else
		LogError(ErrorMessage());
}

void Connection::BeginTransaction()
{
	Execute("SAVEPOINT _;");
}

void Connection::EndTransaction()
{
	Execute("RELEASE _;");
}

void Connection::RollbackTransaction()
{
	Execute("ROLLBACK;");
}

void Connection::BeginDeferredTransaction()
{
	Execute("BEGIN DEFERRED TRANSACTION");
}

void Connection::BeginImmediateTransaction()
{
	Execute("BEGIN IMMEDIATE TRANSACTION");
}

void Connection::BeginExclusiveTransaction()
{
	Execute("BEGIN EXCLUSIVE TRANSACTION");
}

void Connection::CommitTransaction()
{
	Execute("COMMIT");
}

bool Connection::SetSavePoint(const char* savePoint)
{
	char szSQL[256];
	Format(szSQL, sizeof(szSQL), "SAVEPOINT %s", savePoint);
	return Execute(szSQL);
}

bool Connection::RollbackToSavePoint(const char* savePoint)
{
	char szSQL[256];
	Format(szSQL, sizeof(szSQL), "ROLLBACK TO %s", savePoint);
	return Execute(szSQL);
}

int Connection::Count(const char* tableName, bool bCache)
{
	const std::string strTable(tableName);
	const CountLockScope lock;  // issue 0165: the maps and the kept statement are shared between threads

	// A remembered value is used (whatever bCache says) while the most recent INSERT/UPDATE/DELETE changed no row:
	// the closed library tests the field that sqlite3_changes() returns (see TotalChanges).
	std::tr1::unordered_map<std::string, int>::const_iterator itValue = m_hshCountValues.find(strTable);
	if (itValue != m_hshCountValues.end() && sqlite3_changes(m_db) == 0)
		return itValue->second;

	sqlite3_stmt* pStmt = NULL;
	bool bNewStatement = false;
	std::tr1::unordered_map<std::string, sqlite3_stmt*>::const_iterator itStmt = m_hshCountStatements.find(strTable);
	if (itStmt == m_hshCountStatements.end())
	{
		bNewStatement = true;
		const std::string strSQL = "select count(*) from " + strTable;  // not quoted, as the closed library
		if (sqlite3_prepare_v2(m_db, strSQL.c_str(), -1, &pStmt, NULL) != SQLITE_OK)
		{
			LogError(ErrorMessage());
			return -1;
		}
		if (bCache)
			m_hshCountStatements[strTable] = pStmt;
	}
	else
	{
		pStmt = itStmt->second;
		if (sqlite3_reset(pStmt) != SQLITE_OK)
			LogError(ErrorMessage());
	}

	if (sqlite3_step(pStmt) != SQLITE_ROW)
		return -1;  // the closed library leaves an uncached statement to Close() here; so do we

	const int iCount = sqlite3_column_int(pStmt, 0);
	m_hshCountValues[strTable] = iCount;
	int rc;
	if (bCache || !bNewStatement)
	{
		Report(sqlite3_reset(pStmt));
		rc = sqlite3_clear_bindings(pStmt);
	}
	else
	{
		rc = sqlite3_finalize(pStmt);
	}
	Report(rc);
	return iCount;
}

void Connection::ClearCountCache()
{
	const CountLockScope lock;  // issue 0165
	for (std::tr1::unordered_map<std::string, sqlite3_stmt*>::const_iterator it = m_hshCountStatements.begin();
	     it != m_hshCountStatements.end(); ++it)
	{
		if (sqlite3_finalize(it->second) != SQLITE_OK)
			LogError(ErrorMessage());
	}
	m_hshCountStatements.clear();
	m_hshCountValues.clear();
}

namespace
{
// Shared tail of the Select* members: prepare the formatted SQL into a fresh command and attach it to kResults.
bool SelectInto(const Connection* pConnection, Results& kResults, const char* szSQL)
{
	Command command(pConnection, NULL);
	if (Connection::AcquireCommand(command, szSQL))
		return kResults.SetCommand(command);
	Connection::ReleaseCommand(command);
	return false;
}

const char* LimitClause(const Results& kResults)
{
	return kResults.SingleQuery() ? "LIMIT 1" : "";
}
} // namespace

bool Connection::SelectAt(Results& kResults, const char* tableName, int rowID)
{
	char szSQL[1024];
	memset(szSQL, 0, sizeof(szSQL));
	sqlite3_snprintf(sizeof(szSQL), szSQL, "select %s from %q where ROWID = %i LIMIT 1", kResults.GetColumns(),
	                 tableName, rowID);
	return SelectInto(this, kResults, szSQL);
}

bool Connection::SelectAt(Results& kResults, const char* tableName, const char* columnName, const char* value)
{
	char szSQL[1024];
	memset(szSQL, 0, sizeof(szSQL));
	sqlite3_snprintf(sizeof(szSQL), szSQL, "select %s from %q where %s = %Q %s", kResults.GetColumns(), tableName,
	                 columnName, value, LimitClause(kResults));
	return SelectInto(this, kResults, szSQL);
}

bool Connection::SelectAt(Results& kResults, const char* tableName, const char* columnName, int value)
{
	char szSQL[1024];
	memset(szSQL, 0, sizeof(szSQL));
	sqlite3_snprintf(sizeof(szSQL), szSQL, "select %s from %q where %s = %i %s", kResults.GetColumns(), tableName,
	                 columnName, value, LimitClause(kResults));
	return SelectInto(this, kResults, szSQL);
}

bool Connection::SelectAt(Results& kResults, const char* tableName, const char* columnName, double value)
{
	// Deliberate difference: the closed library passes the double to a "%d" conversion, which reads its low
	// 32 bits as the integer and its high 32 bits as the next ("%s") argument (a crash for most values). The value
	// is formatted as a real number here. No caller in the game DLL.
	char szSQL[1024];
	memset(szSQL, 0, sizeof(szSQL));
	sqlite3_snprintf(sizeof(szSQL), szSQL, "select %s from %q where %s = %!.15g %s", kResults.GetColumns(),
	                 tableName, columnName, value, LimitClause(kResults));
	return SelectInto(this, kResults, szSQL);
}

bool Connection::SelectAll(Results& kResults, const char* tableName)
{
	char szSQL[1024];
	memset(szSQL, 0, sizeof(szSQL));
	sqlite3_snprintf(sizeof(szSQL), szSQL, "select %s from %q %s", kResults.GetColumns(), tableName,
	                 LimitClause(kResults));
	return SelectInto(this, kResults, szSQL);
}

bool Connection::SelectWhere(Results& kResults, const char* tableName, const char* condition)
{
	char szSQL[1024];
	memset(szSQL, 0, sizeof(szSQL));
	sqlite3_snprintf(sizeof(szSQL), szSQL, "select %s from %q where %s %s", kResults.GetColumns(), tableName,
	                 condition, LimitClause(kResults));
	return SelectInto(this, kResults, szSQL);
}

bool Connection::Execute(Results& kResults, const char* szCommand, int lenCommand) const
{
	Command command(this, NULL);
	if (!AcquireCommand(command, szCommand, lenCommand))
		return false;
	return kResults.SetCommand(command);
}

bool Connection::Execute(const char* szCommand, int lenCommand) const
{
	sqlite3_stmt* pStmt = NULL;
	if (sqlite3_prepare_v2(m_db, szCommand, lenCommand, &pStmt, NULL) != SQLITE_OK)
	{
		LogError(ErrorMessage());
		return false;
	}
	if (pStmt == NULL)
		return false;  // empty statement: no message, as the closed library
	int rc;
	do
	{
		rc = sqlite3_step(pStmt);
	} while (rc == SQLITE_OK || rc == SQLITE_ROW);
	Report(sqlite3_finalize(pStmt));
	if (rc != SQLITE_DONE)
		LogError(ErrorMessage());
	return rc == SQLITE_DONE;
}

bool Connection::ExecuteMultiple(const char* szCommands, int lenCommands) const
{
	int iRemaining = lenCommands >= 0 ? lenCommands : (int)strlen(szCommands);
	const char* szTail = NULL;
	sqlite3_stmt* pStmt = NULL;
	int rc = sqlite3_prepare_v2(m_db, szCommands, iRemaining, &pStmt, &szTail);
	while (pStmt != NULL)
	{
		iRemaining -= (int)(szTail - szCommands);
		szCommands = szTail;
		do
		{
			rc = sqlite3_step(pStmt);
		} while (rc == SQLITE_OK || rc == SQLITE_ROW);
		if (sqlite3_finalize(pStmt) != SQLITE_OK)
			LogError(ErrorMessage());
		if (rc != SQLITE_DONE)
		{
			LogError(ErrorMessage());
			return false;
		}
		pStmt = NULL;
		rc = sqlite3_prepare_v2(m_db, szCommands, iRemaining, &pStmt, &szTail);
	}
	// A statement that prepares to nothing (end of text, or a lone ';') ends the loop.
	if (rc == SQLITE_OK || rc == SQLITE_DONE)
		return true;
	LogError(ErrorMessage());
	return false;
}

int Connection::TotalChanges() const
{
	// Deliberately sqlite3_changes(), not sqlite3_total_changes(): the closed library reads sqlite3::nChange
	// (offset 0x50 of the 3.7.17 x86 struct, decompile 2026-11-10; A/B confirmed), i.e. the rows changed by the
	// most recent INSERT/UPDATE/DELETE, despite the name.
	return m_db != NULL ? sqlite3_changes(m_db) : 0;
}

void Connection::Analyze() const
{
	if (m_db != NULL && sqlite3_exec(m_db, "ANALYZE", NULL, NULL, NULL) != SQLITE_OK)
		LogError(ErrorMessage());
}

void Connection::Vacuum() const
{
	if (m_db != NULL && sqlite3_exec(m_db, "VACUUM", NULL, NULL, NULL) != SQLITE_OK)
		LogError(ErrorMessage());
}

int Connection::StatementCount() const
{
	int iCount = 0;
	for (sqlite3_stmt* p = sqlite3_next_stmt(m_db, NULL); p != NULL; p = sqlite3_next_stmt(m_db, p))
		++iCount;
	return iCount;
}

bool Connection::SetBusyTimeout(int ms)
{
	if (sqlite3_busy_timeout(m_db, ms) == SQLITE_OK)
		return true;
	LogError(ErrorMessage());
	return false;
}

const char* Connection::StatementSQL(int index) const
{
	for (sqlite3_stmt* p = sqlite3_next_stmt(m_db, NULL); p != NULL; p = sqlite3_next_stmt(m_db, p))
	{
		if (index == 0)
			return sqlite3_sql(p);
		--index;
	}
	return NULL;
}

void Connection::SetLogger(IDatabaseLogger* pkLogger)
{
	m_pkDatabaseLogger.reset(pkLogger);
}

void Connection::LogMessage(const char* szMessage) const
{
	if (m_pkDatabaseLogger.get() != NULL)
		m_pkDatabaseLogger->Log(szMessage, IDatabaseLogger::TYPE_MESSAGE);
}

void Connection::LogWarning(const char* szWarning) const
{
	if (m_pkDatabaseLogger.get() != NULL)
		m_pkDatabaseLogger->Log(szWarning, IDatabaseLogger::TYPE_WARNING);
}

void Connection::LogError(const char* szError) const
{
	if (m_pkDatabaseLogger.get() != NULL)
		m_pkDatabaseLogger->Log(szError, IDatabaseLogger::TYPE_ERROR);
}

const char* Connection::CalculateMemoryStats()
{
	// Same headings as the closed library; the numbers are this process's SQLite counters (the closed library's
	// come from its static page cache and scratch buffers, which are not reproduced), so the text differs.
	int iMemCur = 0, iMemMax = 0, iPageCur = 0, iPageMax = 0, iScratchCur = 0, iScratchMax = 0;
	int iPageOverCur = 0, iPageOverMax = 0, iScratchOverCur = 0, iScratchOverMax = 0;
	int iMallocSize = 0, iPageSize = 0, iScratchSize = 0, iDummy = 0, iLookCur = 0, iLookMax = 0;
	sqlite3_status(SQLITE_STATUS_MEMORY_USED, &iMemCur, &iMemMax, 0);
	sqlite3_status(SQLITE_STATUS_PAGECACHE_USED, &iPageCur, &iPageMax, 0);
	sqlite3_status(SQLITE_STATUS_SCRATCH_USED, &iScratchCur, &iScratchMax, 0);
	sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &iDummy, &iMallocSize, 0);
	sqlite3_status(SQLITE_STATUS_PAGECACHE_SIZE, &iDummy, &iPageSize, 0);
	sqlite3_status(SQLITE_STATUS_SCRATCH_SIZE, &iDummy, &iScratchSize, 0);
	sqlite3_status(SQLITE_STATUS_PAGECACHE_OVERFLOW, &iPageOverCur, &iPageOverMax, 0);
	sqlite3_status(SQLITE_STATUS_SCRATCH_OVERFLOW, &iScratchOverCur, &iScratchOverMax, 0);
	if (m_db != NULL)
		sqlite3_db_status(m_db, SQLITE_DBSTATUS_LOOKASIDE_USED, &iLookCur, &iLookMax, 0);
	const int iStatements = m_db != NULL ? StatementCount() : 0;

	char szBuf[2048];
	Format(szBuf, sizeof(szBuf),
	       "\n-- SQLite Memory Statistics --\nMemory Usage:\n\t\t[Cur]\t\t[Max]\n"
	       "Malloc:\t\t%d\t\t%d\nPageCache:\t%d\t\t%d\nLookAside:\t%d\t\t%d\nScratch:\t%d\t\t%d\n\n"
	       "Static Buffer Overflows:\n\t\t[TooLarge]\t[NoSpace]\nPageCache:\t%d\t\t%d\nScratch:\t%d\t\t%d\n\n"
	       "Largest Allocations:\nMalloc:\t\t%d\nPageCache:\t%d\nScratch:\t%d\n\n"
	       "Prepared Statements:\nCurrent:\t\t%d\n------------------------------",
	       iMemCur, iMemMax, iPageCur, iPageMax, iLookCur, iLookMax, iScratchCur, iScratchMax, iPageOverCur,
	       iPageOverMax, iScratchOverCur, iScratchOverMax, iMallocSize, iPageSize, iScratchSize, iStatements);
	m_strMemoryStats = szBuf;
	LogMessage(m_strMemoryStats.c_str());
	return m_strMemoryStats.c_str();
}

bool Connection::ValidateFKConstraints(bool /*bAllowNULL: ignored by the closed library too*/) const
{
	bool bValid = true;
	LogMessage("Validating Foreign Key Constraints...");
	sqlite3_stmt* pTables = NULL;
	if (sqlite3_prepare_v2(m_db, "select name from sqlite_master where type = 'table'", -1, &pTables, NULL) !=
	    SQLITE_OK)
		return false;
	char szPragma[512];
	char szCheck[512];
	char szMsg[1024];
	while (sqlite3_step(pTables) == SQLITE_ROW)
	{
		const char* szTable = (const char*)sqlite3_column_text(pTables, 0);
		Format(szPragma, sizeof(szPragma), "pragma foreign_key_list(%s);", szTable);
		sqlite3_stmt* pKeys = NULL;
		if (sqlite3_prepare_v2(m_db, szPragma, -1, &pKeys, NULL) != SQLITE_OK)
		{
			sqlite3_finalize(pTables);
			LogMessage("Failed Validation.");
			return false;
		}
		while (sqlite3_step(pKeys) == SQLITE_ROW)
		{
			const char* szParent = (const char*)sqlite3_column_text(pKeys, 2);
			const char* szFrom = (const char*)sqlite3_column_text(pKeys, 3);
			const char* szTo = (const char*)sqlite3_column_text(pKeys, 4);
			Format(szCheck, sizeof(szCheck),
			       "select \"%s\" from %s where %s not null and %s not in (select \"%s\" from %s);", szFrom,
			       szTable, szFrom, szFrom, szTo, szParent);
			sqlite3_stmt* pCheck = NULL;
			if (sqlite3_prepare_v2(m_db, szCheck, -1, &pCheck, NULL) != SQLITE_OK)
			{
				Format(szMsg, sizeof(szMsg), "Failure to check references for foreign key %s(%s).", szTable, szFrom);
				LogError(szMsg);
				LogError(ErrorMessage());
				sqlite3_finalize(pTables);
				sqlite3_finalize(pKeys);
				LogMessage("Failed Validation.");
				return false;
			}
			while (sqlite3_step(pCheck) == SQLITE_ROW)
			{
				bValid = false;
				Format(szMsg, sizeof(szMsg), "Invalid Reference on %s.%s - \"%s\" does not exist in %s", szTable,
				       szFrom, (const char*)sqlite3_column_text(pCheck, 0), szParent);
				LogError(szMsg);
			}
			sqlite3_finalize(pCheck);
		}
		sqlite3_finalize(pKeys);
	}
	sqlite3_finalize(pTables);
	LogMessage(bValid ? "Passed Validation." : "Failed Validation.");
	return bValid;
}

int Connection::ErrorCode() const
{
	return m_db != NULL ? sqlite3_errcode(m_db) : -1;
}

const char* Connection::ErrorMessage() const
{
	return m_db != NULL ? sqlite3_errmsg(m_db) : NULL;
}

bool Connection::AcquireCommand(Command& command, const char* szSQL, int lenSQL)
{
	const Connection* pConnection = command.connection;
	if (pConnection == NULL)
		return false;
	if (command.stmt != NULL)
	{
		pConnection->LogError("Command must be released prior to reuse.");
		return false;
	}
	if (pConnection->Report(sqlite3_prepare_v2(pConnection->m_db, szSQL, lenSQL, &command.stmt, NULL)) &&
	    command.stmt != NULL)
		return true;
	char szMsg[512];
	Format(szMsg, sizeof(szMsg), "In Query - %s", szSQL);
	pConnection->LogMessage(szMsg);
	return false;
}

void Connection::ReleaseCommand(Command& command)
{
	if (command.stmt == NULL)
		return;
	if (sqlite3_finalize(command.stmt) != SQLITE_OK)
		command.connection->LogError(command.connection->ErrorMessage());
	command.stmt = NULL;
}

//------------------------------------------------------------------------------------------------------------------
// Results
//------------------------------------------------------------------------------------------------------------------
Results::Results(const char* szColumns) : m_szColumns(NULL), m_bSingleQuery(false)
{
	SetColumns(szColumns);
}

Results::Results(const Results& copy)
    : m_szColumns(NULL), m_bSingleQuery(copy.m_bSingleQuery), m_hshColumnPositions(copy.m_hshColumnPositions)
{
	if (copy.m_szColumns != NULL)
		SetColumns(copy.GetColumns());
	m_Command.connection = copy.m_Command.connection;
	m_Command.stmt = NULL;
	if (copy.m_Command.stmt != NULL)
		Connection::AcquireCommand(m_Command, sqlite3_sql(copy.m_Command.stmt));  // a new statement, same SQL
}

Results::~Results()
{
	Release();
}

const char* Results::GetColumns() const
{
	return m_szColumns != NULL ? m_szColumns : "*";
}

void Results::SetColumns(const char* szColumns)
{
	if (m_szColumns != NULL)
		sqlite3_free(const_cast<char*>(m_szColumns));
	m_szColumns = NULL;
	if (szColumns != NULL)
		m_szColumns = sqlite3_mprintf("%s", szColumns);
}

bool Results::SetCommand(Command& command)
{
	if (m_Command.stmt != NULL)
		Connection::ReleaseCommand(m_Command);
	m_Command = command;
	m_hshColumnPositions.clear();
	if (m_bSingleQuery)
		return Step();
	return true;
}

bool Results::Execute()
{
	if (m_Command.connection == NULL || m_Command.stmt == NULL)
		return false;
	const int rc = sqlite3_step(m_Command.stmt);
	if (rc != SQLITE_DONE)
	{
		if (rc == SQLITE_ROW)
			m_Command.connection->LogWarning("A row was returned during an Execute command.");
		else
			m_Command.connection->LogError(m_Command.connection->ErrorMessage());
		char szMsg[512];
		Format(szMsg, sizeof(szMsg), "While executing - '%s'", sqlite3_sql(m_Command.stmt));
		m_Command.connection->LogMessage(szMsg);
	}
	return rc == SQLITE_DONE;
}

bool Results::TryExecute()
{
	if (m_Command.connection == NULL || m_Command.stmt == NULL)
		return false;
	const int rc = sqlite3_step(m_Command.stmt);
	if (rc == SQLITE_DONE || rc == SQLITE_ABORT || rc == SQLITE_CONSTRAINT)
		return true;
	if (rc == SQLITE_ROW)
		m_Command.connection->LogWarning("A row was returned during an Execute command.");
	else
		m_Command.connection->LogError(m_Command.connection->ErrorMessage());
	char szMsg[512];
	Format(szMsg, sizeof(szMsg), "While executing - '%s'", sqlite3_sql(m_Command.stmt));
	m_Command.connection->LogMessage(szMsg);
	return false;
}

bool Results::Step()
{
	// sqlite3_step(NULL) is a harmless SQLITE_MISUSE; the closed library calls it the same way.
	const int rc = sqlite3_step(m_Command.stmt);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE && m_Command.stmt != NULL && m_Command.connection != NULL)
	{
		m_Command.connection->LogError(m_Command.connection->ErrorMessage());
		char szMsg[512];
		Format(szMsg, sizeof(szMsg), "While executing - '%s'", sqlite3_sql(m_Command.stmt));
		m_Command.connection->LogMessage(szMsg);
	}
	return rc == SQLITE_ROW;
}

bool Results::Reset()
{
	if (m_Command.stmt == NULL || sqlite3_reset(m_Command.stmt) != SQLITE_OK)
		return false;
	return sqlite3_clear_bindings(m_Command.stmt) == SQLITE_OK;
}

bool Results::TryReset()
{
	if (m_Command.stmt == NULL)
		return false;
	sqlite3_reset(m_Command.stmt);
	return sqlite3_clear_bindings(m_Command.stmt) == SQLITE_OK;
}

void Results::Release()
{
	if (m_Command.stmt != NULL)
		Connection::ReleaseCommand(m_Command);
	// The closed library also forgets the column list here, so a released Results selects "*" afterwards.
	SetColumns(NULL);
}

bool Results::Bind(int idx, const char* szValue, int lenValue, bool bMakeCopy)
{
	return sqlite3_bind_text(m_Command.stmt, idx, szValue, lenValue, bMakeCopy ? SQLITE_TRANSIENT : SQLITE_STATIC) ==
	       SQLITE_OK;
}

bool Results::Bind(int idx, const char* szValue, bool bMakeCopy)
{
	return Bind(idx, szValue, -1, bMakeCopy);
}

bool Results::Bind(int idx, const wchar_t* wszValue, int lenValue, bool bMakeCopy)
{
	const int nBytes = lenValue == -1 ? -1 : lenValue * 2;
	return sqlite3_bind_text16(m_Command.stmt, idx, wszValue, nBytes, bMakeCopy ? SQLITE_TRANSIENT : SQLITE_STATIC) ==
	       SQLITE_OK;
}

bool Results::Bind(int idx, const wchar_t* wszValue, bool bMakeCopy)
{
	return Bind(idx, wszValue, -1, bMakeCopy);
}

bool Results::Bind(int idx, double dValue)
{
	return sqlite3_bind_double(m_Command.stmt, idx, dValue) == SQLITE_OK;
}

bool Results::Bind(int idx, float fValue)
{
	return sqlite3_bind_double(m_Command.stmt, idx, (double)fValue) == SQLITE_OK;
}

bool Results::Bind(int idx, int iValue)
{
	return sqlite3_bind_int(m_Command.stmt, idx, iValue) == SQLITE_OK;
}

bool Results::Bind(int idx, __int64 iValue)
{
	return sqlite3_bind_int64(m_Command.stmt, idx, iValue) == SQLITE_OK;
}

bool Results::BindNULL(int idx)
{
	return sqlite3_bind_null(m_Command.stmt, idx) == SQLITE_OK;
}

int Results::ColumnCount() const
{
	return m_Command.stmt != NULL ? sqlite3_column_count(m_Command.stmt) : 0;
}

const char* Results::ColumnName(int iColumn)
{
	return m_Command.stmt != NULL ? sqlite3_column_name(m_Command.stmt, iColumn) : NULL;
}

const char* Results::ColumnTypeName(int iColumn)
{
	switch (ColumnType(iColumn))
	{
	case COLTYPE_INT:
		return "integer";
	case COLTYPE_FLOAT:
		return "float";
	case COLTYPE_TEXT:
		return "text";
	case COLTYPE_NULL:
		return "null";
	case COLTYPE_BOOL:
		return "boolean";
	default:
		return NULL;  // SQLITE_BLOB (4)
	}
}

ColumnTypes Results::ColumnType(int iColumn)
{
	if (m_Command.stmt == NULL)
		return COLTYPE_NULL;
	const int iType = sqlite3_column_type(m_Command.stmt, iColumn);
	if (iType == SQLITE_INTEGER)
	{
		// An integer value in a column declared exactly "boolean" (case-sensitive) is a bool.
		const char* szDecl = sqlite3_column_decltype(m_Command.stmt, iColumn);
		if (szDecl != NULL && strcmp(szDecl, "boolean") == 0)
			return COLTYPE_BOOL;
	}
	return (ColumnTypes)iType;
}

void Results::HashColumnPositions()
{
	m_hshColumnPositions.clear();
	for (int i = 0; i < ColumnCount(); ++i)
		m_hshColumnPositions[std::string(ColumnName(i))] = i;  // a repeated name keeps its last position
}

bool Results::HasColumn(const char* szColumnName)
{
	return ColumnPosition(szColumnName) != -1;
}

int Results::ColumnPosition(const char* szColumnName)
{
	if (m_hshColumnPositions.empty())
		HashColumnPositions();
	stdext::hash_map<std::string, int>::const_iterator it = m_hshColumnPositions.find(std::string(szColumnName));
	return it != m_hshColumnPositions.end() ? it->second : -1;
}

const char* Results::GetText(int iColumn)
{
	return m_Command.stmt != NULL ? (const char*)sqlite3_column_text(m_Command.stmt, iColumn) : NULL;
}

const wchar_t* Results::GetText16(int iColumn)
{
	return m_Command.stmt != NULL ? (const wchar_t*)sqlite3_column_text16(m_Command.stmt, iColumn) : NULL;
}

bool Results::GetBool(int iColumn)
{
	switch (ColumnType(iColumn))
	{
	case COLTYPE_INT:
	case COLTYPE_BOOL:
		return m_Command.stmt != NULL && sqlite3_column_int(m_Command.stmt, iColumn) != 0;
	case COLTYPE_TEXT:
	{
		const char* szText = GetText(iColumn);
		return szText != NULL && (szText[0] == 't' || szText[0] == 'T');
	}
	default:
		return false;  // NULL, float and blob values are false
	}
}

int Results::GetInt(int iColumn)
{
	return m_Command.stmt != NULL ? sqlite3_column_int(m_Command.stmt, iColumn) : 0;
}

__int64 Results::GetInt64(int iColumn)
{
	return m_Command.stmt != NULL ? sqlite3_column_int64(m_Command.stmt, iColumn) : 0;
}

float Results::GetFloat(int iColumn)
{
	return m_Command.stmt != NULL ? (float)sqlite3_column_double(m_Command.stmt, iColumn) : 0.0f;
}

double Results::GetDouble(int iColumn)
{
	return m_Command.stmt != NULL ? sqlite3_column_double(m_Command.stmt, iColumn) : 0.0;
}

// GetValue(bool&) is column_int != 0, unlike GetBool (the closed library does the same).
void Results::GetValue(int iColumn, bool& bValue)
{
	bValue = GetInt(iColumn) != 0;
}

void Results::GetValue(int iColumn, int& iValue)
{
	iValue = GetInt(iColumn);
}

void Results::GetValue(int iColumn, __int64& iValue)
{
	iValue = GetInt64(iColumn);
}

void Results::GetValue(int iColumn, float& fValue)
{
	fValue = GetFloat(iColumn);
}

void Results::GetValue(int iColumn, double& dValue)
{
	dValue = GetDouble(iColumn);
}

void Results::GetValue(int iColumn, const char*& zValue)
{
	zValue = GetText(iColumn);
}

void Results::GetValue(int iColumn, const wchar_t*& wszValue)
{
	wszValue = GetText16(iColumn);
}

// By name: an unknown name gives position -1, i.e. SQLite's out-of-range result (NULL / 0).
const char* Results::GetText(const char* szColumn)
{
	return GetText(ColumnPosition(szColumn));
}

const wchar_t* Results::GetText16(const char* szColumn)
{
	return GetText16(ColumnPosition(szColumn));
}

bool Results::GetBool(const char* szColumn)
{
	return GetBool(ColumnPosition(szColumn));
}

int Results::GetInt(const char* szColumn)
{
	return GetInt(ColumnPosition(szColumn));
}

__int64 Results::GetInt64(const char* szColumn)
{
	return GetInt64(ColumnPosition(szColumn));
}

float Results::GetFloat(const char* szColumn)
{
	return GetFloat(ColumnPosition(szColumn));
}

double Results::GetDouble(const char* szColumn)
{
	return GetDouble(ColumnPosition(szColumn));
}

void Results::GetValue(const char* szColumn, bool& bValue)
{
	GetValue(ColumnPosition(szColumn), bValue);
}

void Results::GetValue(const char* szColumn, int& iValue)
{
	GetValue(ColumnPosition(szColumn), iValue);
}

void Results::GetValue(const char* szColumn, __int64& iValue)
{
	GetValue(ColumnPosition(szColumn), iValue);
}

void Results::GetValue(const char* szColumn, float& fValue)
{
	GetValue(ColumnPosition(szColumn), fValue);
}

void Results::GetValue(const char* szColumn, double& dValue)
{
	GetValue(ColumnPosition(szColumn), dValue);
}

void Results::GetValue(const char* szColumn, const char*& zValue)
{
	GetValue(ColumnPosition(szColumn), zValue);
}

void Results::GetValue(const char* szColumn, const wchar_t*& wszValue)
{
	GetValue(ColumnPosition(szColumn), wszValue);
}

bool Results::SingleQuery() const
{
	return m_bSingleQuery;
}

void Results::SingleQuery(bool bSingle)
{
	m_bSingleQuery = bSingle;
}

void* Results::operator new(size_t tSize)
{
	return malloc(tSize);
}

void Results::operator delete(void* pMem)
{
	free(pMem);
}

SingleResult::SingleResult(const char* szColumns) : Results(szColumns)
{
	SingleQuery(true);
}

} // namespace Database
