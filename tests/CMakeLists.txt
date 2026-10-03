add_subdirectory(test1)

if (ENABLE_ONLINE_SERVER)
    add_subdirectory(server-smoke)
endif()

if(WIN32)
    add_subdirectory(windows-sandbox)
endif()
