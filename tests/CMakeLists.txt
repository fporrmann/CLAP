cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME "clap_tests")

project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
	nlohmann_json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(nlohmann_json)

add_executable(${PROJECT_NAME}
	test_core.cpp
	test_ip_cores.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../API/include
	${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json pthread)

option(CLAP_ENABLE_COVERAGE "Enable coverage reporting" OFF)

if(CLAP_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	message(STATUS "Coverage enabled: Adding compiler and linker flags for coverage reporting")
	target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g --coverage -fprofile-arcs -ftest-coverage)
	target_link_options(${PROJECT_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)

	add_custom_target(coverage
		COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
		COMMAND lcov --directory ${CMAKE_BINARY_DIR} --capture --ignore-errors empty --include "*/API/include/*" --output-file coverage.info
		COMMAND genhtml coverage.info --output-directory coverage --ignore-errors inconsistent
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "Generating coverage report with lcov"
	)
endif()

enable_testing()
include(Catch)
catch_discover_tests(${PROJECT_NAME})
