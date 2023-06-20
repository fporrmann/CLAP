
#pragma once

#include <cstdint>
#include <sstream>
#include <string>

/////////////////////////
// Includes for open()
#ifdef _WIN32
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
/////////////////////////

#include "Defines.hpp"
#include "Exceptions.hpp"

namespace clap
{
namespace internal
{
DeviceHandle OpenDevice(const std::string &name, FlagType flags = DEFAULT_OPEN_FLAGS)
{
	DeviceHandle fd = OPEN_DEVICE(name.c_str(), flags);
	int32_t err     = errno;

	if (!DEVICE_HANDLE_VALID(fd))
	{
		std::stringstream ss;
		ss << CLASS_TAG("") << "Unable to open device " << name << "; errno: " << err;
		throw CLAPException(ss.str());
	}

	return fd;
}

void CloseDevice(DeviceHandle &fd)
{
	if (!DEVICE_HANDLE_VALID(fd)) return;

	CLOSE_DEVICE(fd);
	fd = INVALID_HANDLE;
}

} // namespace internal
} // namespace clap