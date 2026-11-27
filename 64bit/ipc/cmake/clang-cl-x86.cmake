# Toolchain file: clang-cl -m32 + lld-link, environment from VS 2008 vsvars32.bat (VC9 headers and libraries, msvcr90).
# Used by ipc/build.py, which captures that environment first. Owner: epic 1200 coordinator.
set(CMAKE_SYSTEM_NAME Windows)
set(C5_LLVM_BIN "C:/Program Files/LLVM/bin")
if(DEFINED ENV{VP_LLVM_BIN})
    file(TO_CMAKE_PATH "$ENV{VP_LLVM_BIN}" C5_LLVM_BIN)
endif()
set(CMAKE_CXX_COMPILER "${C5_LLVM_BIN}/clang-cl.exe")
set(CMAKE_LINKER "${C5_LLVM_BIN}/lld-link.exe" CACHE FILEPATH "")
set(CMAKE_AR "${C5_LLVM_BIN}/llvm-lib.exe" CACHE FILEPATH "")
set(CMAKE_CXX_FLAGS_INIT "-m32")
# clang-cl in the VC9 environment reports _MSC_VER 1500, as in the DLL's own build, and CMake then adds
# /INCREMENTAL:YES to Debug links, which lld-link rejects. The compiler check runs in Release; the top-level
# CMakeLists.txt rewrites the flag for the project's own Debug builds.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
# Manifests: CMake embeds the linker's manifest (msvcr90 is a side-by-side assembly) with mt.exe -outputresource,
# which llvm-mt does not implement. Use the VC9 environment's SDK mt.exe.
find_program(C5_SDK_MT mt.exe PATHS "C:/Program Files/Microsoft SDKs/Windows/v6.0A/bin"
             "C:/Program Files (x86)/Microsoft SDKs/Windows/v7.0A/bin" NO_DEFAULT_PATH)
set(CMAKE_MT "${C5_SDK_MT}" CACHE FILEPATH "")
