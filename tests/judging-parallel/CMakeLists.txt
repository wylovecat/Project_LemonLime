include(${CMAKE_SOURCE_DIR}/tests/common/judging-test-target.cmake)

# Same fixture as test1, so the parallel verdict matrix can be compared against the
# serial one.
file(COPY ${CMAKE_SOURCE_DIR}/tests/data/TestContest1
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)

lemon_add_judging_test(judging-parallel)
