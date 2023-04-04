# Set the minimum required CMake version
cmake_minimum_required(VERSION 3.10)

# Set the name of the CMake project
set(PROJECT_NAME Streaming)
project(${PROJECT_NAME} LANGUAGES CXX)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
	set(CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "Build Mode: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(cpp_flags "-pedantic") # Enable pedantic warnings
set(cpp_flags "${cpp_flags} -Wall") # Enable all default g++ warning level
set(cpp_flags "${cpp_flags} -Wextra") # Enable additional warnings
set(cpp_flags "${cpp_flags} -Werror") # Treat all warnings as errors
set(cpp_flags "${cpp_flags} -Weffc++") # Enable warnings for violates of the effective C++ guidelines
set(cpp_flags "${cpp_flags} -Wunreachable-code") # Warn on unreachable code
set(cpp_flags "${cpp_flags} -Wunused-result") # Warn when the result of a function is not used
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${cpp_flags}") # Add all previous warning and standard defines to the CMake C++ flags


find_package(XDMA_API PATHS ../../API/cmake/modules REQUIRED)

# Search for all .cpp files in the src folder
file(GLOB src "src/*.cpp")

include_directories(${XDMA_API_INCLUDE_DIRS})

# Specify the name of the executable created by this CMake project
add_executable(${PROJECT_NAME} ${src})
target_link_libraries (${PROJECT_NAME} PRIVATE ${XDMA_API_LIBS})
