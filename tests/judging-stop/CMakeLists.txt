include(${CMAKE_SOURCE_DIR}/tests/common/judging-test-target.cmake)

# One task, one test case, a deliberately huge time limit and a source that never
# terminates: the only way this round can finish quickly is by being stopped.
file(COPY ${CMAKE_SOURCE_DIR}/tests/data/JudgingStop
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)

lemon_add_judging_test(judging-stop)
