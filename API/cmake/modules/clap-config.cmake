if(CLAP_FOUND)
	return()
endif()

if(WIN32)
	# When building with MSVC, disable the warning about using the "unsafe" version of the CRT functions.
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	# Disable the default min/max macros in Windows.h
	add_definitions(-DNOMINMAX)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	add_definitions(-DCLAP_32BIT)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	add_definitions(-DCLAP_64BIT)
endif()

include(FindPackageHandleStandardArgs)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_path(CLAP_INCLUDE_DIR
	NAMES CLAP.hpp # Look for this file
	PATH_SUFFIXES clap # Tries /usr/include/clap
	HINTS
		"${CMAKE_CURRENT_LIST_DIR}/../../" # API
		"${CMAKE_CURRENT_LIST_DIR}/../../include" # API/include
		"/usr/include"
		"/usr/local/include"
	)

# Add plural form of CLAP_INCLUDE_DIR (--> DIRS) for backwards compatibility
if (CLAP_INCLUDE_DIR)
	set(CLAP_INCLUDE_DIRS "${CLAP_INCLUDE_DIR}")
endif()

set(CLAP_LIBS Threads::Threads)

# Provide an imported target, e.g., target_link_libraries(${PROJECT_NAME} PRIVATE CLAP::CLAP)
if(NOT TARGET CLAP::CLAP)
	add_library(CLAP::CLAP INTERFACE IMPORTED)
	set_target_properties(
		CLAP::CLAP PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${CLAP_INCLUDE_DIR}"
		INTERFACE_LINK_LIBRARIES "${CLAP_LIBS}"
	)
endif()

# Validate the CLAP find results and set CLAP_FOUND.
find_package_handle_standard_args(CLAP DEFAULT_MSG CLAP_INCLUDE_DIR CLAP_LIBS)
