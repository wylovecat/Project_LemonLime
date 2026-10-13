include(${CMAKE_SOURCE_DIR}/tests/common/judging-test-target.cmake)

# The zoo fixture is the same one test1 asserts serially, so the parallel verdict
# matrix has a reference to compare against.
file(COPY ${CMAKE_SOURCE_DIR}/tests/data/TestContest1
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)

lemon_add_judging_test(judging-cases)
