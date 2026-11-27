// wbhost_fs.cpp - the FIFileSystem for wbdump (tickets 0343, 0326): what the 32-bit closed library opens a .Civ5Map
// through (FFileInstance -> FIFileSystem::GetInstance() -> the EXE export GetFileSystemMgr). Create opens any path
// with Win32: for reading, or for writing (modeWrite: created or truncated, the writer's Save); Delete(wchar) removes
// a file (Save deletes its file after a failed write); every other method answers "no".
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "fw_prelude.h"
#include <FireWorks/FIFile.h>

namespace
{
class WbFile : public FIFile
{
public:
	WbFile() : m_h(INVALID_HANDLE_VALUE) {}
	~WbFile() { Close(); }
	FileErr Open(const char* sz, OpenMode e, dword, uint)
	{
		wchar_t w[MAX_PATH];
		MultiByteToWideChar(CP_UTF8, 0, sz ? sz : "", -1, w, MAX_PATH);
		return Open(w, e, 0, 0);
	}
	FileErr Open(const wchar* wsz, OpenMode e, dword, uint)
	{
		Close();
		if(!wsz || (e != modeRead && e != modeWrite))
			return FILE_BADNAME;
		if(e == modeWrite)
			m_h = CreateFileW(wsz, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		else
			m_h = CreateFileW(wsz, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		return m_h == INVALID_HANDLE_VALUE ? FILE_NOTFOUND : FILE_OK;
	}
	FileErr Close()
	{
		if(m_h != INVALID_HANDLE_VALUE)
			CloseHandle(m_h);
		m_h = INVALID_HANDLE_VALUE;
		return FILE_OK;
	}
	const wchar_t* GetFileName() { return L""; }
	byte* GetViewCaret() const { return NULL; }
	byte* GetViewEnd() const { return NULL; }
	byte* GetViewPtr() const { return NULL; }
	uint GetLength() const { return m_h == INVALID_HANDLE_VALUE ? 0 : (uint)GetFileSize(m_h, NULL); }
	void SetLength(uint) {}
	bool IsOpen() const { return m_h != INVALID_HANDLE_VALUE; }
	bool IsMapped() const { return false; }
	uint Seek(long l, SeekMode e) { return SetFilePointer(m_h, l, NULL, e == begin ? FILE_BEGIN : e == current ? FILE_CURRENT : FILE_END); }
	uint SeekToBegin() { return Seek(0, begin); }
	uint SeekToEnd() { return Seek(0, end); }
	uint GetPosition() { return SetFilePointer(m_h, 0, NULL, FILE_CURRENT); }
	void Rewind() { Seek(0, begin); }
	bool IsEOF() { return GetPosition() >= GetLength(); }
	uint Read(void* p, uint n)
	{
		DWORD got = 0;
		return ReadFile(m_h, p, n, &got, NULL) ? got : 0;
	}
	uint Write(const void* p, uint n)
	{
		DWORD put = 0;
		return WriteFile(m_h, p, n, &put, NULL) ? put : 0;
	}
	uint Flush() { return 0; }

private:
	HANDLE m_h;
};

class WbFileSystem : public FIFileSystem
{
public:
	FIFile* Create(const char* sz, FIFile::OpenMode e, dword f, uint u)
	{
		WbFile* p = new WbFile();
		if(sz) p->Open(sz, e, f, u);
		return p;
	}
	FIFile* Create(const wchar* wsz, FIFile::OpenMode e, dword f, uint u)
	{
		WbFile* p = new WbFile();
		if(wsz) p->Open(wsz, e, f, u);
		return p;
	}
	FileErr Delete(const char*) { return FILE_BADNAME; }
	FileErr Delete(const wchar* wsz) { return wsz && DeleteFileW(wsz) ? FILE_OK : FILE_NOTFOUND; }
	FileErr ReadAll(const char*, byte**, size_t*, dword) { return FILE_BADNAME; }
	FileErr ReadAll(const wchar*, byte**, size_t*, dword) { return FILE_BADNAME; }
	FileErr ReadAll(const char*, byte**, size_t*, dword, FIFile::Allocator&) { return FILE_BADNAME; }
	FileErr ReadAll(const wchar*, byte**, size_t*, dword, FIFile::Allocator&) { return FILE_BADNAME; }
	bool Exist(const char*) { return false; }
	bool Exist(const wchar*) { return false; }
	uint GetLength(const char*) { return 0; }
	uint GetLength(const wchar*) { return 0; }
	FFileTimestamp GetLastWriteTime(const char*) { return FFileTimestamp(); }
	bool IsFileCatalogEnabled() { return false; }
	FFileErrorHandler* GetErrorHandler() { return NULL; }
};

WbFileSystem s_kFileSystem;
} // namespace

void* WbHostFileSystem()
{
	return static_cast<FIFileSystem*>(&s_kFileSystem);
}
