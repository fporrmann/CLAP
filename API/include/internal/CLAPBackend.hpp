/*
 *  File: CLAPBackend.hpp
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
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "Defines.hpp"
#include "Exceptions.hpp"

namespace clap
{
namespace internal
{
class CLAPBackend
{
public:
	enum class TYPE
	{
		READ,
		WRITE,
		CONTROL
	};

public:
	CLAPBackend() {}

	virtual ~CLAPBackend() {}

	virtual void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)        = 0;
	virtual void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte) = 0;
	virtual void ReadCtrl(const uint64_t& addr, uint64_t& data, const std::size_t& byteCnt) = 0;

	virtual uint32_t GetDevNum() const
	{
		return 0;
	}

	const std::string& GetName(const TYPE& type) const
	{
		if (type == TYPE::READ)
			return m_nameRead;
		else if (type == TYPE::WRITE)
			return m_nameWrite;
		else
			return m_nameCtrl;
	}

	const std::string& GetBackendName() const
	{
		return m_backendName;
	}

	DeviceHandle OpenDevice(const std::string& name, FlagType flags = DEFAULT_OPEN_FLAGS) const
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

protected:
	bool m_valid              = false;
	std::string m_nameRead    = "";
	std::string m_nameWrite   = "";
	std::string m_nameCtrl    = "";
	std::string m_backendName = "CLAP";
};
} // namespace internal
} // namespace clap