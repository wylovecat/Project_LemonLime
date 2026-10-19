include(${CMAKE_SOURCE_DIR}/tests/common/judging-test-target.cmake)

# Both fixtures are copied because the soak runs the verdict matrix of TestContest1
# and interrupts the never-terminating JudgingStop fixture.
file(COPY ${CMAKE_SOURCE_DIR}/tests/data/TestContest1
          ${CMAKE_SOURCE_DIR}/tests/data/JudgingStop
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)

lemon_add_judging_test(judging-soak)
