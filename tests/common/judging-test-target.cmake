# ==================================================================================
# Shared wiring for the judging test targets.
#
# Mirrors how tests/test1 is built: Qt Test, the lemon libraries, the spdlog
# include path, TEST_DATA_DIR pointing at the per-directory data/ copy, and ctest
# registration.  Include this from a test directory and call
# lemon_add_judging_test(<name>).
# ==================================================================================

# NOTE: deliberately no cmake/enable-asan.cmake here (tests/test1 does include it).
# These targets link the judging core and pull in spdlog's heavy sink templates via
# tests/common/judging_test_util.hpp; building that combination with
# /fsanitize=address makes cl.exe abort with exit code 0xC0000409 on MSVC 14.51
# (ASan was only a warning, C5072).  windows-sandbox and sleep-inhibitor, the other
# tests that drive real judging, do not enable ASan either.

find_package(Qt${LEMON_QT_MAJOR_VERSION} ${LEMON_QT_MIN_VERSION} COMPONENTS Test REQUIRED)

function(lemon_add_judging_test target)
    if (NOT WIN32)
        add_executable(${target} main.cpp ${CMAKE_BINARY_DIR}/watcher.qrc)
        add_dependencies(${target} watcher_unix)
    else()
        add_executable(${target} main.cpp)
    endif()

    target_precompile_headers(${target} REUSE_FROM lemon-core)

    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
        ${spdlog_DIR}/include
    )

    target_link_libraries(${target} PRIVATE
        lemon-core
        lemon-base
        SingleApplication::SingleApplication
        ${LEMON_QT_LIBNAME}::Test
        ${LEMON_QT_LIBS}
        spdlog::spdlog
    )

    target_compile_definitions(${target} PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/data"
    )

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()
