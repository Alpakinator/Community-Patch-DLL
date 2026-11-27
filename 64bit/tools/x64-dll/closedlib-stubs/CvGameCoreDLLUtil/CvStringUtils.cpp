// CvStringUtils::FromUTF8ToUTF16: real replacement (CvGameCoreDLLUtilWin32.lib member
// CvStringUtils.obj, x86 disassembly 2026-11-10): empty input gives an empty string; otherwise
// MultiByteToWideChar(CP_UTF8) through a malloc'ed buffer; a conversion failure gives an empty string.
#include "../FireWorks/fw_prelude.h"
#include "CvStringUtils.h"
#include <stdlib.h>

namespace CvStringUtils
{
	std::wstring FromUTF8ToUTF16(const std::string& strUTF8String)
	{
		std::wstring strResult;
		if(strUTF8String.size() > 0)
		{
			const char* szSource = strUTF8String.c_str();
			const int iChars = MultiByteToWideChar(CP_UTF8, 0, szSource, -1, NULL, 0);
			if(iChars > 0)
			{
				wchar_t* pBuffer = static_cast<wchar_t*>(malloc(iChars * sizeof(wchar_t)));
				if(MultiByteToWideChar(CP_UTF8, 0, szSource, -1, pBuffer, iChars) > 0)
					strResult = pBuffer;
				free(pBuffer);
			}
		}
		return strResult;
	}
}
