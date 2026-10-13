include(${CMAKE_SOURCE_DIR}/tests/common/judging-test-target.cmake)

# No judging fixture is needed and no tool chain is required: every assertion in
# this target is pure logic, so it must run everywhere.
lemon_add_judging_test(judging-unit)
