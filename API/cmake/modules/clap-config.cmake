if(CLAP_FOUND)
	return()
endif()

if(WIN32)
	# When building with MSVC, disable the warning about using the "unsafe" version of the CRT functions.
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	# Disable the default min/max macros in Windows.h
	add_definitions(-DNOMINMAX)
endif()

include(FindPackageHandleStandardArgs)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(CLAP_INCLUDE_DIRS xdmaAccess.h
	HINTS "${CMAKE_CURRENT_LIST_DIR}/../../"
	PATH_SUFFIXES include)

set(CLAP_LIBS Threads::Threads)

find_package_handle_standard_args(CLAP DEFAULT_MSG CLAP_INCLUDE_DIRS CLAP_LIBS)
