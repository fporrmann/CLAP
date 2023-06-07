# Set the minimum required CMake version
cmake_minimum_required(VERSION 3.10)

project("CLAP Examples")

# Include all example projects in this directory
file(GLOB EXAMPLES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)



foreach(EXAMPLE ${EXAMPLES})
	set(dir ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE})

	if(IS_DIRECTORY ${dir} AND EXISTS ${dir}/CMakeLists.txt)
		add_subdirectory(${EXAMPLE})
	endif()
endforeach()
