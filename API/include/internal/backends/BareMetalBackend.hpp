/*
 *  File: BareMetalBackend.hpp
 *   Copyright (c) 2023 Florian Porrmann
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
 */

#pragma once

#include <cstring>
#include <iostream>
#include <sstream>

#include "../CLAPBackend.hpp"
#include "../Constants.hpp"
#include "../Logger.hpp"
#include "../UserInterruptBase.hpp"
#include "../Utils.hpp"

namespace clap
{
namespace internal
{
namespace backends
{
class BareMetalUserInterrupt : virtual public UserInterruptBase
{
	DISABLE_COPY_ASSIGN_MOVE(BareMetalUserInterrupt)

public:
	BareMetalUserInterrupt() {}

	virtual void Init([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, [[maybe_unused]] HasInterrupt* pReg = nullptr)
	{
		LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
	}

	void Unset()
	{
		LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
	}

	bool IsSet() const
	{
		LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
		return false;
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE)
	{
		LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
		return false;
	}
};

class BareMetalBackend : virtual public CLAPBackend
{
public:
	BareMetalBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0)
	{
		LOG_WARNING << CLASS_TAG("BareMetalBackend") << "WARNING: BareMetalBackend is currently untested and therefore, probably not fully functional." << std::endl;
		m_nameRead    = "BareMetal";
		m_nameWrite   = "BareMetal";
		m_backendName = "BareMetal";
		m_valid       = true;
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t count     = 0;
		off_t offset       = addr;
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(pByteData + count, (void*)(offset), bytes);

			count += bytes;
			offset += bytes;
		}

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		off_t offset             = addr;

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			memcpy((void*)(offset), pByteData + count, bytes);

			count += bytes;
			offset += bytes;
		}

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}
	}

	void ReadCtrl([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] uint64_t& data, [[maybe_unused]] const std::size_t& byteCnt)
	{
		LOG_ERROR << CLASS_TAG("PetaLinuxBackend") << "ReadCtrl is currently not implemented by the PetaLinux backend." << std::endl;
	}

	UserInterruptPtr MakeUserInterrupt() const
	{
		return std::make_unique<BareMetalUserInterrupt>();
	}
};

} // namespace backends
} // namespace internal
} // namespace clap
