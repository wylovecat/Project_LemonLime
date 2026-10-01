# ==================================================================================
# Test: Online Submission Service Smoke Test
#
# End-to-end HTTP test: start SubmissionServer, batch-generate users, log in,
# submit code, and assert what lands in the contest directory. Requires
# ENABLE_ONLINE_SERVER=ON (the server sources and web assets).
# ==================================================================================

find_package(Qt${LEMON_QT_MAJOR_VERSION} ${LEMON_QT_MIN_VERSION}
             COMPONENTS Test Network HttpServer WebSockets Concurrent REQUIRED)

set(SERVER_SMOKE_SOURCES
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/server/SubmissionServer.cpp
    ${CMAKE_SOURCE_DIR}/src/server/UserStore.cpp
    ${CMAKE_SOURCE_DIR}/src/server/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/server/server-assets.qrc
)

add_executable(server-smoke ${SERVER_SMOKE_SOURCES})

include(${CMAKE_SOURCE_DIR}/cmake/enable-asan.cmake)

target_precompile_headers(server-smoke REUSE_FROM lemon-core)

target_include_directories(server-smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
    ${spdlog_DIR}/include
)

target_link_libraries(server-smoke PRIVATE
    lemon-core
    lemon-base
    SingleApplication::SingleApplication
    ${LEMON_QT_LIBNAME}::Test
    ${LEMON_QT_LIBNAME}::Network
    ${LEMON_QT_LIBNAME}::HttpServer
    ${LEMON_QT_LIBNAME}::WebSockets
    ${LEMON_QT_LIBNAME}::Concurrent
    ${LEMON_QT_LIBS}
    spdlog::spdlog
)

add_test(NAME server-smoke COMMAND server-smoke)
set_tests_properties(server-smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
