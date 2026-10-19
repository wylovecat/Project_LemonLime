include(${CMAKE_SOURCE_DIR}/tests/common/judging-test-target.cmake)

# The fixture is generated at run time, so there is nothing to copy.  The test
# skips itself unless LEMON_JUDGE_BENCH=1: run
#
#   LEMON_JUDGE_BENCH=1 ctest -R judging-benchmark -V
#
# to get the timings.
lemon_add_judging_test(judging-benchmark)
