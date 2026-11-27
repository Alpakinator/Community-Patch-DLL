// FMemoryStream: real replacement (FireWorksWin32.obj member FMemoryStream.obj, x86 disassembly
// 2026-11-10). Only the default constructor, the destructor and the virtuals (the vtable) are
// defined; the other constructors, Grow, GetBuf and the 4-argument Init are not referenced by the DLL.
//
// Behaviour kept from x86, including its quirks:
// - default: growable (m_bRealloc = true), m_bReallocMin = false, memory pool 2, tag 0;
// - growth doubles the capacity, or grows to exactly what is needed if that is more (or if
//   m_bReallocMin is set); the read/write position after growth is m_pStart + GetBytesUsed();
// - CopyToMem copies from the current position to the end, not from the start;
// - reading or skipping past the end and writing past a fixed buffer go through RaiseException.
#include "fw_prelude.h"
#include <FireWorks/FMemoryStream.h>
#include <string.h>

FMemoryStream::FMemoryStream()
	: FDataStream()
	, m_pStart(NULL)
	, m_pData(NULL)
	, m_pStop(NULL)
	, m_pEndOfBuffer(NULL)
	, m_bRealloc(true)
	, m_bReallocMin(false)
	, m_uMemoryPool(2)
	, m_uMemoryTag(0)
{
}

FMemoryStream::~FMemoryStream(void)
{
	if(m_bRealloc)
	{
		delete [] m_pStart;
		m_pStart = NULL;
	}
}

unsigned int FMemoryStream::ReadIt(unsigned int byteCount, void* buffer)
{
	if(m_pData + byteCount > m_pStop)
	{
		std::runtime_error kError("FMemoryStream Attempting to read past end of stream");
		RaiseException(kError);
	}
	memcpy(buffer, m_pData, byteCount);
	m_pData += byteCount;
	m_uiBytesRead += byteCount;
	m_uiPosition += byteCount;
	return byteCount;
}

unsigned int FMemoryStream::WriteIt(unsigned int byteCount, const void* buffer)
{
	if(m_pData + byteCount > m_pEndOfBuffer)
	{
		if(!m_bRealloc)
		{
			std::runtime_error kError("FMemoryStream Attempting to write past end of stream");
			RaiseException(kError);
		}
		else
		{
			// x86 does this arithmetic in 32 bits; buffers are far below 4 GB.
			const unsigned int uiCapacity = static_cast<unsigned int>(m_pEndOfBuffer - m_pStart);
			const unsigned int uiNeeded = static_cast<unsigned int>((m_pData + byteCount) - m_pEndOfBuffer);
			unsigned int uiNewSize = uiCapacity * 2;
			if(uiNeeded != 0 && m_bReallocMin)
				uiNewSize = uiCapacity + uiNeeded;
			else if(uiNewSize < uiCapacity + uiNeeded)
				uiNewSize = uiCapacity + uiNeeded;
			if(static_cast<int>(uiNewSize) > 0)
			{
				char* pNew = new char[uiNewSize];
				const unsigned int uiUsed = GetEOF();
				if(uiUsed)
					memcpy(pNew, m_pStart, uiUsed);
				Init(static_cast<int>(uiNewSize), pNew, m_bRealloc);
				m_pStop = m_pStart + uiUsed;
				m_pData = m_pStart + GetBytesUsed();
			}
		}
	}
	memcpy(m_pData, buffer, byteCount);
	m_uiBytesRead += byteCount;
	m_uiPosition += byteCount;
	m_pData += byteCount;
	if(m_pData > m_pStop)
		m_pStop = m_pData;
	return byteCount;
}

void FMemoryStream::Skip(unsigned int deltaByteCount)
{
	m_uiBytesRead += deltaByteCount;
	m_uiPosition += deltaByteCount;
	m_pData += deltaByteCount;
	if(m_pData > m_pStop)
	{
		std::runtime_error kError("FMemoryStream Skip went past end of stream");
		RaiseException(kError);
	}
}

void FMemoryStream::Rewind()
{
	m_uiBytesRead = 0;
	m_uiPosition = 0;
	m_pData = m_pStart;
}

void FMemoryStream::Init(int size, const void* data, bool bRealloc)
{
	Uninit();
	m_bRealloc = bRealloc;
	char* pStart;
	if(bRealloc && data == NULL)
		pStart = (size > 0) ? new char[size] : NULL;
	else
		pStart = static_cast<char*>(const_cast<void*>(data)); // x86 keeps the caller's buffer, even when bRealloc is set
	m_pStart = pStart;
	m_pData = pStart;
	m_pEndOfBuffer = pStart + size;
	m_pStop = pStart + size;
}

void FMemoryStream::Uninit()
{
	if(m_bRealloc)
	{
		delete [] m_pStart;
		m_pStart = NULL;
	}
}

bool FMemoryStream::AtEnd() const
{
	return m_pData >= m_pStop;
}

void FMemoryStream::FastFwd()
{
	m_uiBytesRead = GetEOF();
	m_uiPosition = GetEOF();
	m_pData = m_pStop;
}

void FMemoryStream::Truncate()
{
	m_pStop = m_pStart;
	m_pData = m_pStart;
	m_uiBytesRead = 0;
	m_uiPosition = 0;
}

unsigned int FMemoryStream::GetEOF() const
{
	return static_cast<unsigned int>(m_pStop - m_pStart);
}

void FMemoryStream::CopyToMem(void* mem)
{
	memcpy(mem, m_pData, static_cast<size_t>(m_pStop - m_pData));
}
