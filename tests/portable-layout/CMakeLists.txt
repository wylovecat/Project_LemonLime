find_package(Qt${LEMON_QT_MAJOR_VERSION} ${LEMON_QT_MIN_VERSION} COMPONENTS Test REQUIRED)

# Deliberately not lemon_add_judging_test(): this target needs neither the judging
# core nor a fixture - LemonPortable.hpp is header-only - and it has to register two
# ctest entries instead of one.
add_executable(portable-layout main.cpp)

target_include_directories(portable-layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(portable-layout PRIVATE
    ${LEMON_QT_LIBNAME}::Test
    ${LEMON_QT_LIBS}
)

# Portable mode is a process-wide decision, so each layout is pinned by a separate
# run of the same binary with its own environment.  The trailing argument selects the
# matching test slot (QTest treats a bare positional argument as a test function
# name).
add_test(NAME portable-layout-on COMMAND portable-layout testPortableLayout)
add_test(NAME portable-layout-off COMMAND portable-layout testDefaultLayout)

set_tests_properties(portable-layout-on PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ENVIRONMENT "LEMON_PORTABLE=1"
)
set_tests_properties(portable-layout-off PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ENVIRONMENT "LEMON_PORTABLE=0"
)
