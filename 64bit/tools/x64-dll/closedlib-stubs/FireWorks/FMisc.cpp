// FCRC + g_CRC32, FCriticalSection, FTimer: real replacements. FDebugHelper: stub.
// x86 evidence: FireWorksWin32.obj members FCrc32.obj, FCriticalSection.obj, FTimer.obj,
// FDebugHelper.obj, disassembled 2026-11-10.
#include "fw_prelude.h"
#include <FireWorks/FCrc32.h>
#include <FireWorks/FCriticalSection.h>
#include <FireWorks/FTimer.h>
#include <FireWorks/Win32/FDebugHelper.h>

// ---------------------------------------------------------------------------------------------
// FCRC: reflected table CRC (polynomial 0xEDB88320 for g_CRC32), caller-supplied initial value,
// no final XOR. The DLL hashes strings with g_CRC32.Calc(p, n) (CvGlobals.cpp:73,78), so the
// values must match x86 exactly; this is the standard CRC-32 table and update step.

FCRC g_CRC32;

FCRC::FCRC(dword polynomial)
{
	m_polynomial = polynomial;
	InitTable(polynomial);
}

FCRC::~FCRC()
{
}

void FCRC::InitTable(dword polynomial)
{
	for(int i = 0; i < 256; ++i)
	{
		dword crc = static_cast<dword>(i);
		for(int bit = 0; bit < 8; ++bit)
			crc = (crc & 1) ? ((crc >> 1) ^ polynomial) : (crc >> 1);
		m_table[i] = crc;
	}
	// x86 stores the CRC of four zero bytes, starting from ~0, as m_magic.
	const dword dwZero = 0;
	m_magic = Calc(&dwZero, 4, CRC_INIT);
}

dword FCRC::Calc(const void* pBuf, int nLen, dword crc) const
{
	// x86 loops "while(nLen != 0)": a negative length is not guarded there either.
	const byte* p = static_cast<const byte*>(pBuf);
	for(; nLen != 0; --nLen)
		crc = (crc >> 8) ^ m_table[(crc ^ *p++) & 0xFF];
	return crc;
}

// ---------------------------------------------------------------------------------------------
// FCriticalSection: thin wrapper over a Win32 CRITICAL_SECTION.

FCriticalSection::FCriticalSection(void) { InitializeCriticalSection(&m_kCriticalSection); }
FCriticalSection::~FCriticalSection(void) { DeleteCriticalSection(&m_kCriticalSection); }
void FCriticalSection::Enter(void) { EnterCriticalSection(&m_kCriticalSection); }
void FCriticalSection::Leave(void) { LeaveCriticalSection(&m_kCriticalSection); }

// ---------------------------------------------------------------------------------------------
// FTimer: QueryPerformanceCounter seconds. The first Read() records m_qMark (time zero) and
// m_qRate; Read(0) measures from m_qMark, Read(t) from t.

__int64 FTimer::m_qMark = 0;
__int64 FTimer::m_qRate = 0;
double FTimer::m_fFreq = 0.0;

double FTimer::Read(__int64 offset)
{
	if(m_fFreq == 0.0)
	{
		QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&m_qRate));
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&m_qMark));
		m_fFreq = (m_qRate != 0) ? 1.0 / static_cast<double>(m_qRate) : 1.0;
	}
	__int64 qNow;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&qNow));
	if(offset == 0)
		offset = m_qMark;
	return static_cast<double>(qNow - offset) * m_fFreq;
}

// ---------------------------------------------------------------------------------------------
// FDebugHelper: STUB. On x86 GetInstance() creates the singleton and runs SymInitialize, and
// LoadSymbols(hModule) loads the DLL's symbols for call-stack logging (DllMain calls it). The x64
// replacement creates the singleton without touching dbghelp and LoadSymbols does nothing.

FDebugHelper* FDebugHelper::pInstance = NULL;

FDebugHelper::FDebugHelper()
	: hCurrentProcess(GetCurrentProcess())
	, m_pExceptionTextCB(NULL)
	, m_pkCaptureStackBackTrace(NULL)
{
	HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
	if(hKernel32 != NULL)
		m_pkCaptureStackBackTrace = reinterpret_cast<CaptureStackBackTraceFuncType>(GetProcAddress(hKernel32, "RtlCaptureStackBackTrace"));
}

FDebugHelper& FDebugHelper::GetInstance()
{
	STUB_CALL("FireWorks", "public: static class FDebugHelper & __cdecl FDebugHelper::GetInstance(void) [partial: no SymInitialize]");
	if(pInstance == NULL)
		pInstance = new FDebugHelper();
	return *pInstance;
}

bool FDebugHelper::LoadSymbols(HMODULE /*hModule*/)
{
	STUB_CALL("FireWorks", "public: bool __cdecl FDebugHelper::LoadSymbols(struct HINSTANCE__ *)");
	return false;
}
