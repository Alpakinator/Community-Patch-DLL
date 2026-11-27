// FStringA / FStringW: real replacements for the members the DLL uses (FireWorksWin32.obj member
// FString.obj, x86 disassembly 2026-11-10), plus SetLength, which they all call.
//
// Storage (FString.h, unchanged on x64): m_pszString points just past a 12-byte FStringAData /
// FStringWData header {int m_iAllocLength; int m_iDataLength; USHORT m_bLocked; USHORT m_bFixed;}.
// The empty string shares the header returned by FStringA/W_GetStringInitData (an EXE export,
// see FExeExports.cpp); it is never written except with zeros and never freed.
// Buffers are allocated with new byte[] because the inline FString::Release frees them with
// delete [] (byte*).
#include "fw_prelude.h"
#include <FireWorks/FString.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------------------------
// FStringA

void FStringA::SetLength(size_t iLength)
{
	const int iNewLength = static_cast<int>(iLength);
	FStringAData* pkOld = GetData();
	if(iNewLength == 0)
	{
		// Leaves the shared empty-string header untouched.
		if(pkOld->m_iDataLength != 0)
		{
			m_pszString[0] = '\0';
			GetData()->m_iDataLength = 0;
		}
		return;
	}
	if(iNewLength > pkOld->m_iAllocLength)
	{
		// x86: capacity rounded so that capacity + 1 is a multiple of 32 characters.
		const int iAlloc = ((iNewLength + 0x20) & ~0x1F) - 1;
		if(iAlloc != pkOld->m_iAllocLength)
		{
			FStringAData* pkNew = reinterpret_cast<FStringAData*>(new byte[sizeof(FStringAData) + iAlloc + 1]);
			pkNew->m_iAllocLength = iAlloc;
			pkNew->m_iDataLength = 0;
			pkNew->m_bLocked = 0;
			pkNew->m_bFixed = 0;
			LPSTR pszOld = m_pszString;
			m_pszString = pkNew->StringData();
			pkNew->m_iDataLength = pkOld->m_iDataLength;
			if(pkOld->m_iDataLength > 0)
				memcpy(m_pszString, pszOld, pkOld->m_iDataLength + 1);
			else
				m_pszString[0] = '\0';
			if(pkOld != FStringA_GetStringInitData() && !pkOld->m_bFixed)
				delete [] reinterpret_cast<byte*>(pkOld);
		}
	}
	GetData()->m_iDataLength = iNewLength;
	m_pszString[iNewLength] = '\0';
}

void FStringA::Copy(int iLength, LPCSTR lpszSource, int iFirst)
{
	if(iLength < 0 || iFirst < 0)
		return;
	if(lpszSource == m_pszString)
		return;
	if(iLength == 0 || lpszSource == NULL)
	{
		SetLength(0);
		return;
	}
	SetLength(iLength);
	memmove(m_pszString, lpszSource + iFirst, iLength);
	m_pszString[GetData()->m_iDataLength] = '\0';
}

void FStringA::SetAt(int iIndex, int iLength, LPCSTR lpszSource, int iFirst)
{
	if(iLength <= 0 || iFirst < 0)
		return;
	const int iDataLength = GetData()->m_iDataLength;
	if(static_cast<unsigned int>(iIndex) > static_cast<unsigned int>(iDataLength))
		return;
	if(lpszSource == NULL)
		return;
	if(iIndex + iLength > iDataLength)
	{
		SetLength(iIndex + iLength);
		m_pszString[GetData()->m_iDataLength] = '\0';
	}
	memmove(m_pszString + iIndex, lpszSource + iFirst, iLength);
}

int FStringA::Replace(char chOld, char chNew)
{
	int iCount = 0;
	if(chOld == chNew)
		return 0;
	char* p = m_pszString;
	char* const pEnd = p + GetData()->m_iDataLength;
	for(; p < pEnd; ++p)
	{
		if(*p == chOld)
		{
			*p = chNew;
			++iCount;
		}
	}
	return iCount;
}

// ---------------------------------------------------------------------------------------------
// FStringW

void FStringW::SetLength(size_t iLength)
{
	const int iNewLength = static_cast<int>(iLength);
	FStringWData* pkOld = GetData();
	if(iNewLength == 0)
	{
		if(pkOld->m_iDataLength != 0)
		{
			m_pszString[0] = L'\0';
			GetData()->m_iDataLength = 0;
		}
		return;
	}
	if(iNewLength > pkOld->m_iAllocLength)
	{
		// x86: (((2n + 0x21) >> 1) & 0x7FFFFFF0) - 1, i.e. capacity + 1 a multiple of 16 characters.
		const int iAlloc = static_cast<int>((((static_cast<unsigned int>(iNewLength) * 2u + 0x21u) >> 1) & 0x7FFFFFF0u) - 1u);
		if(iAlloc != pkOld->m_iAllocLength)
		{
			FStringWData* pkNew = reinterpret_cast<FStringWData*>(new byte[sizeof(FStringWData) + (iAlloc + 1) * sizeof(wchar)]);
			pkNew->m_iAllocLength = iAlloc;
			pkNew->m_iDataLength = 0;
			pkNew->m_bLocked = 0;
			pkNew->m_bFixed = 0;
			LPWSTR pszOld = m_pszString;
			m_pszString = pkNew->StringData();
			pkNew->m_iDataLength = pkOld->m_iDataLength;
			if(pkOld->m_iDataLength > 0)
				memcpy(m_pszString, pszOld, (pkOld->m_iDataLength + 1) * sizeof(wchar));
			else
				m_pszString[0] = L'\0';
			if(pkOld != FStringW_GetStringInitData() && !pkOld->m_bFixed)
				delete [] reinterpret_cast<byte*>(pkOld);
		}
	}
	GetData()->m_iDataLength = iNewLength;
	m_pszString[iNewLength] = L'\0';
}

void FStringW::Copy(int iLength, LPCWSTR lpszSource, int iFirst)
{
	if(iLength < 0 || iFirst < 0)
		return;
	if(lpszSource == m_pszString)
		return;
	if(iLength == 0 || lpszSource == NULL)
	{
		SetLength(0);
		return;
	}
	SetLength(iLength);
	memmove(m_pszString, lpszSource + iFirst, iLength * sizeof(wchar));
	m_pszString[GetData()->m_iDataLength] = L'\0';
}

void FStringW::ExtractFileTitle(FStringW& szDest, wchar chPathDelimiter) const
{
	// x86: szDest = ExtractFileName(chPathDelimiter); then it looks for the last '.' and calls
	// szDest.Mid(szDest, 0, iDot). FStringW::Mid returns without doing anything when source and
	// destination share a buffer, so the extension is NOT stripped: the x86 result is the file
	// name with its extension. Kept as is (the DLL stores it as the world builder map name).
	const int iLength = GetData()->m_iDataLength;
	int iNameStart = -1;
	if(iLength >= 0)
	{
		// ExtractFileName: last delimiter; if none, a drive colon.
		LPWSTR psz = m_pszString;
		const wchar chSaved = psz[iLength];
		psz[iLength] = L'\0';
		const wchar* pDelim = wcsrchr(psz, chPathDelimiter);
		psz[iLength] = chSaved;
		if(pDelim)
			iNameStart = static_cast<int>(pDelim - psz);
		else if(iLength > 0)
		{
			const wchar* pColon = wcschr(psz, L':');
			if(pColon)
				iNameStart = static_cast<int>(pColon - psz);
		}
	}
	if(iNameStart == -1)
		szDest.Copy(iLength, m_pszString, 0);
	else
		szDest.Copy(iLength - (iNameStart + 1), m_pszString + iNameStart + 1, 0);
}

void FStringW::CopyToUTF8(FStringA* pkOutput) const
{
	if(pkOutput == NULL)
		return;
	LPCWSTR pszSource = m_pszString;
	if(GetData()->m_iDataLength <= 0 || pszSource == NULL)
	{
		pkOutput->SetLength(0);
		return;
	}
	const int iBytes = WideCharToMultiByte(CP_UTF8, 0, pszSource, -1, NULL, 0, NULL, NULL);
	if(iBytes <= 1)
	{
		pkOutput->SetLength(0);
		return;
	}
	pkOutput->SetLength(iBytes - 1);
	if(pkOutput->m_pszString != NULL)
		WideCharToMultiByte(CP_UTF8, 0, pszSource, -1, pkOutput->m_pszString, iBytes, NULL, NULL);
}
