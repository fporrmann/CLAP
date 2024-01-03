
/*
 *  File: FileOps.hpp
 *  Copyright (c) 2023 Florian Porrmann
 *
 *  MIT License
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

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
static inline DeviceHandle OpenDevice(const std::string &name, FlagType flags = DEFAULT_OPEN_FLAGS)
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

static inline void CloseDevice(DeviceHandle &fd)
{
	if (!DEVICE_HANDLE_VALID(fd)) return;

	CLOSE_DEVICE(fd);
	fd = INVALID_HANDLE;
}

} // namespace internal
} // namespace clap