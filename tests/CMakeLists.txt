add_subdirectory(test1)

if (ENABLE_ONLINE_SERVER)
    add_subdirectory(server-smoke)
endif()
