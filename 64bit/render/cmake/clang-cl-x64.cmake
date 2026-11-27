# Toolchain file for render/ (epic 2900): clang-cl -m64 + lld-link, environment from VS 2022 vcvarsall x64
# (MSVC 14.4x STL, Windows 10 SDK) - the same toolchain as server/ (decision D15). A copy of
# ipc/cmake/clang-cl-x64.cmake so that render/ stays a standalone project with no dependency on ipc/.
set(CMAKE_SYSTEM_NAME Windows)
set(C5_LLVM_BIN "C:/Program Files/LLVM/bin")
if(DEFINED ENV{VP_LLVM_BIN})
    file(TO_CMAKE_PATH "$ENV{VP_LLVM_BIN}" C5_LLVM_BIN)
endif()
set(CMAKE_CXX_COMPILER "${C5_LLVM_BIN}/clang-cl.exe")
set(CMAKE_LINKER "${C5_LLVM_BIN}/lld-link.exe" CACHE FILEPATH "")
set(CMAKE_AR "${C5_LLVM_BIN}/llvm-lib.exe" CACHE FILEPATH "")
set(CMAKE_MT "${C5_LLVM_BIN}/llvm-mt.exe" CACHE FILEPATH "")
set(CMAKE_CXX_FLAGS_INIT "-m64")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
