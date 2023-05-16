# Set the minimum required CMake version
cmake_minimum_required(VERSION 3.10)

# Set the name of the CMake project
set(PROJECT_NAME HLS_CustomInterface)
project(${PROJECT_NAME} LANGUAGES CXX)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
	set(CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "[${PROJECT_NAME}] Build Mode: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CLAP PATHS ../../API/cmake/modules REQUIRED)

# Search for all .cpp files in the src folder
file(GLOB src "src/*.cpp")

include_directories(${CLAP_INCLUDE_DIRS})

# Specify the name of the executable created by this CMake project
add_executable(${PROJECT_NAME} ${src})
target_link_libraries (${PROJECT_NAME} PRIVATE ${CLAP_LIBS})

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE -pedantic -Wall -Wextra -Weffc++ -Wunreachable-code -Wunused-result -Werror)
endif()
