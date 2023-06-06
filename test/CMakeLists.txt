function (make_test NAME)
	set (BOOLEAN_ARGS "")
	set (ONEVALUE_ARGS "")
	set (MULTIVALUE_ARGS "SOURCES")
	cmake_parse_arguments(
		MK_TEST
		"${BOOLEAN_ARGS}"
		"${ONEVALUE_ARGS}"
		"${MULTIVALUE_ARGS}"
		${ARGN}
	)

	add_executable (${NAME} ${MK_TEST_SOURCES})

	add_dependencies (${NAME} gtest gmock gcpp) # Ensure this target is compiled after gtest and gmock
	# target_include_directories(${NAME} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_options(${NAME} PRIVATE ${COMPILE_FLAGS})
	target_link_libraries (${NAME} PRIVATE GTest::gtest_main PRIVATE GTest::gmock PRIVATE gcpp)
	add_test (NAME ${NAME} COMMAND ${NAME} WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/test")
endfunction()

make_test (basic_test SOURCES basic_test.cpp)