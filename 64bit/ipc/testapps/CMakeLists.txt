# ipc/testapps - robustness test programs (task 1204). Run them with ipc/tools/run_robustness.py.
#   vpipc_test_server: both trees (x64 and x86)
#   vpipc_test_client: x86 tree only (the client side lives in the 32-bit EXE)
add_executable(vpipc_test_server vpipc_test_server.cpp testapp_common.h)
target_link_libraries(vpipc_test_server PRIVATE vpipc)
target_compile_options(vpipc_test_server PRIVATE ${VPIPC_EXE_OPTIONS})

if(C5_ARCH STREQUAL "x86")
    add_executable(vpipc_test_client vpipc_test_client.cpp testapp_common.h)
    target_link_libraries(vpipc_test_client PRIVATE vpipc)
    target_compile_options(vpipc_test_client PRIVATE ${VPIPC_EXE_OPTIONS})
    # Three concurrent 50 MB transfers need more than 2 GB of address space headroom; the EXE is
    # large-address-aware as well.
    if(MSVC)
        target_link_options(vpipc_test_client PRIVATE /LARGEADDRESSAWARE)
    endif()
endif()
