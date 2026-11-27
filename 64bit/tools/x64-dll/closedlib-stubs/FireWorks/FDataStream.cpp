// FDataStream and FEndian: real replacements for the FireWorksWin32 members the DLL uses.
//
// Source of truth: disassembly of FirePlace/lib/FireWorksWin32.obj (archive members FDataStream.obj,
// FEndian.obj), x86, taken 2026-11-10 with llvm-objdump 19.1.7. Every function below reproduces the
// x86 code path for path; see SYMBOLS-fireworks-dllutil-worldbuilder-flua.md for the evidence.
//
// Byte format written by these members (identical on x86 and x64, since every width here is fixed):
//   bool, char, unsigned char      1 byte, raw
//   short, unsigned short          2 bytes, little-endian
//   int, unsigned int, unsigned long, float   4 bytes, little-endian
//   __int64, unsigned __int64      8 bytes, little-endian
//   GUID                           16 bytes, raw struct
//   std::string                    4-byte length, then the characters, no terminator
// A stream whose m_eEndianness differs from the machine's would swap 2/4/8-byte values, except
// unsigned __int64, which the x86 library never swaps (it falls into FEndian's generic template).
// Every FDataStream is constructed ENDIAN_LITTLE and nothing in the DLL changes it.
#include "fw_prelude.h"

// ---------------------------------------------------------------------------------------------
// FEndian

FEndian::EEndianness FEndian::ms_eTargetEndianness = FEndian::ENDIAN_UNKNOWN;

FEndian::EEndianness FEndian::GetEndianness()
{
	// x86: lazily sets and returns ENDIAN_LITTLE (1). x64 Windows is little-endian too.
	if(ms_eTargetEndianness == ENDIAN_UNKNOWN)
		ms_eTargetEndianness = ENDIAN_LITTLE;
	return ms_eTargetEndianness;
}

namespace
{
	template<int N> inline void ReverseBytes(void* p)
	{
		char* b = static_cast<char*>(p);
		for(int i = 0; i < N / 2; ++i)
			std::swap(b[i], b[N - 1 - i]);
	}
}

// ---------------------------------------------------------------------------------------------
// FDataStream base: constructor, destructor and the non-pure virtuals (FMemoryStream needs them).

FDataStream::FDataStream(void)
	: m_bEnableCB(true)
	, m_eEndianness(FEndian::ENDIAN_LITTLE)
	, m_uiBytesRead(0)
	, m_uiPosition(0)
{
}

FDataStream::~FDataStream(void)
{
}

unsigned int FDataStream::GetBytesUsed() const { return m_uiBytesRead; }
unsigned int FDataStream::GetPosition() const { return m_uiPosition; }
void FDataStream::SetPosition(unsigned int position) { Rewind(); Skip(position); }
void FDataStream::Truncate() {}
void FDataStream::Flush() {}
unsigned int FDataStream::GetEOF() const { return 0; }
unsigned int FDataStream::GetSizeLeft() const
{
	const unsigned int uiEOF = GetEOF();
	return uiEOF - GetPosition();
}
void FDataStream::CopyToMem(void* /*mem*/) {}
void FDataStream::RaiseException(std::exception& kException) const
{
	// x86: copy-constructs the (sliced) exception base object and throws it.
	throw kException;
}

// ---------------------------------------------------------------------------------------------
// Scalar writers

void FDataStream::Write(const bool& value) { WriteIt(1, &value); }
void FDataStream::Write(const unsigned char& value) { WriteIt(1, &value); }
void FDataStream::Write(const char& value) { WriteIt(1, &value); }
void FDataStream::Write(const GUID& value) { WriteIt(16, &value); }

#define FDS_WRITE_SWAPPED(TYPE, N) \
	void FDataStream::Write(const TYPE& value) \
	{ \
		if(m_eEndianness != FEndian::GetEndianness()) \
		{ \
			TYPE kCopy = value; \
			ReverseBytes<N>(&kCopy); \
			WriteIt(N, &kCopy); \
		} \
		else \
			WriteIt(N, &value); \
	}

FDS_WRITE_SWAPPED(INT32, 4)
FDS_WRITE_SWAPPED(UINT32, 4)
FDS_WRITE_SWAPPED(unsigned long, 4)
FDS_WRITE_SWAPPED(float, 4)
FDS_WRITE_SWAPPED(INT16, 2)
FDS_WRITE_SWAPPED(UINT16, 2)
FDS_WRITE_SWAPPED(INT64, 8)
#undef FDS_WRITE_SWAPPED

void FDataStream::Write(const UINT64& value)
{
	// x86 quirk kept: an unsigned 64-bit value is copied but never byte-swapped.
	if(m_eEndianness != FEndian::GetEndianness())
	{
		UINT64 kCopy = value;
		WriteIt(8, &kCopy);
	}
	else
		WriteIt(8, &value);
}

// ---------------------------------------------------------------------------------------------
// Scalar readers

void FDataStream::Read(bool& value) { ReadIt(1, &value); }
void FDataStream::Read(unsigned char& value) { ReadIt(1, &value); }
void FDataStream::Read(char& value) { ReadIt(1, &value); }
void FDataStream::Read(GUID& value) { ReadIt(16, &value); }

#define FDS_READ_SWAPPED(TYPE, N) \
	void FDataStream::Read(TYPE& value) \
	{ \
		ReadIt(N, &value); \
		if(m_eEndianness != FEndian::GetEndianness()) \
			ReverseBytes<N>(&value); \
	}

FDS_READ_SWAPPED(INT32, 4)
FDS_READ_SWAPPED(UINT32, 4)
FDS_READ_SWAPPED(unsigned long, 4)
FDS_READ_SWAPPED(float, 4)
FDS_READ_SWAPPED(INT16, 2)
FDS_READ_SWAPPED(UINT16, 2)
FDS_READ_SWAPPED(INT64, 8)
#undef FDS_READ_SWAPPED

void FDataStream::Read(UINT64& value)
{
	// x86 quirk kept: never byte-swapped (see Write(const UINT64&)).
	ReadIt(8, &value);
}

// ---------------------------------------------------------------------------------------------
// std::string members: 4-byte signed length, then the raw characters.

void FDataStream::Write(const std::string& szName)
{
	const int iLength = static_cast<int>(szName.size());
	Write(iLength);
	if(iLength > 0)
		WriteIt(static_cast<unsigned int>(iLength), szName.data());
}

unsigned int FDataStream::ReadString(std::string& szName)
{
	int iLength = 0;
	ReadIt(4, &iLength);
	if(m_eEndianness != FEndian::GetEndianness())
		ReverseBytes<4>(&iLength);
	szName.clear();
	if(iLength > 0 && iLength <= static_cast<int>(GetSizeLeft()))
	{
		char* pBuffer = new char[iLength];
		ReadIt(static_cast<unsigned int>(iLength), pBuffer);
		szName.assign(pBuffer, static_cast<size_t>(iLength));
		delete [] pBuffer;
		return static_cast<unsigned int>(iLength);
	}
	if(iLength < 0 || iLength > static_cast<int>(GetSizeLeft()))
	{
		std::runtime_error kError("FDataStream ReadString error");
		RaiseException(kError);
	}
	return 0;
}

void FDataStream::Read(std::string& szName)
{
	ReadString(szName);
}

// ---------------------------------------------------------------------------------------------
// Free std::string operators (FStlContainerSerialization.obj): 4-byte unsigned length, then one
// 1-byte Write/Read per character. Same bytes as FDataStream::Write(const std::string&).

FDataStream& operator<<(FDataStream& saveTo, const std::string& readFrom)
{
	const unsigned int uiSize = static_cast<unsigned int>(readFrom.size());
	saveTo << uiSize;
	for(std::string::const_iterator it = readFrom.begin(); it != readFrom.end(); ++it)
		saveTo << *it;
	return saveTo;
}

FDataStream& operator>>(FDataStream& loadFrom, std::string& writeTo)
{
	unsigned int uiSize = 0;
	loadFrom >> uiSize;
	writeTo.clear();
	for(unsigned int i = 0; i < uiSize; ++i)
	{
		char ch;
		loadFrom >> ch;
		writeTo.push_back(ch);
	}
	return loadFrom;
}
