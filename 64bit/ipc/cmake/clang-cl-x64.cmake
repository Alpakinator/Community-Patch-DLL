# Toolchain file: clang-cl -m64 + lld-link, environment from VS 2022 vcvarsall x64 (MSVC 14.4x STL, Windows 10 SDK).
# Used by ipc/build.py, which captures that environment first. Owner: epic 1200 coordinator.
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
# clang-cl in the VC9 environment reports _MSC_VER 1500, as in the DLL's own build, and CMake then adds
# /INCREMENTAL:YES to Debug links, which lld-link rejects. The compiler check runs in Release; the top-level
# CMakeLists.txt rewrites the flag for the project's own Debug builds.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
