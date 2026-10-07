if(NOT DEFINED TEST_EXECUTABLE OR NOT DEFINED TEST_LOG)
  message(FATAL_ERROR "TEST_EXECUTABLE and TEST_LOG are required")
endif()
get_filename_component(report_directory "${TEST_LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${report_directory}")
execute_process(
  COMMAND "${TEST_EXECUTABLE}" -o "${TEST_LOG},txt"
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr
  TIMEOUT 80
)
if(EXISTS "${TEST_LOG}")
  file(READ "${TEST_LOG}" test_report)
else()
  set(test_report "No Qt Test report was created.")
endif()
if(NOT test_result STREQUAL "0")
  message(FATAL_ERROR "Qt Test failed (${test_result}).\n${test_report}\n${test_stdout}\n${test_stderr}")
endif()
message(STATUS "${test_report}")
