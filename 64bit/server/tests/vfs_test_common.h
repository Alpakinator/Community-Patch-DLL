// Stand-in for srv_common.h when server/src/srv_vfs.cpp is compiled alone for its unit test (bug 0100,
// server/tests/test_vfs_resolve.cpp). server/tests/run_vfs_test.py copies srv_vfs.cpp into its build folder with this
// file beside it under the name srv_common.h. Declares only what srv_vfs.cpp uses, so the test needs neither the DLL's
// headers nor the fork's source.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

bool VfsInit(const char* szGameDir, std::string& strWhy);
bool VfsResolve(const char* szName, std::string& strFull, std::string& strVfsPath);
const std::vector<std::string>& VfsLuaFileList();
bool VfsReadFile(const std::string& strFull, std::string& strData);
size_t VfsIndexSize();
