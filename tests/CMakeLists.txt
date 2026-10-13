add_subdirectory(test1)

# Judging regression targets: pure logic, parallel equivalence, case-level
# parallelism and stop latency.
add_subdirectory(judging-unit)
add_subdirectory(judging-parallel)
add_subdirectory(judging-cases)
add_subdirectory(judging-stop)

# Which directory the contest dialogs start in.  Registered as two ctest slots, one
# per layout, because portable mode is decided once per process.
add_subdirectory(portable-layout)

if (ENABLE_ONLINE_SERVER)
    add_subdirectory(server-smoke)
endif()

if(WIN32)
    add_subdirectory(windows-sandbox)
    add_subdirectory(sleep-inhibitor)
endif()
