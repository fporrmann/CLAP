if(XDMA_API_FOUND)
	return()
endif()

include(FindPackageHandleStandardArgs)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(XDMA_API_INCLUDE_DIRS xdmaAccess.h
	HINTS "${CMAKE_CURRENT_LIST_DIR}/../../"
	PATH_SUFFIXES include)

set(XDMA_API_LIBS Threads::Threads)

find_package_handle_standard_args(XDMA_API DEFAULT_MSG XDMA_API_INCLUDE_DIRS XDMA_API_LIBS)
