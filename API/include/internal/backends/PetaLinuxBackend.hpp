/*
 *  File: PetaLinuxBackend.hpp
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

/////////////////////////
// Include for mmap(), munmap()
#ifndef _WIN32
#include <sys/mman.h>
#endif
/////////////////////////

#include <cstring>
#include <mutex>
#include <string>

#include "../CLAPBackend.hpp"
#include "../Constants.hpp"
#include "../Defines.hpp"
#include "../Logger.hpp"
#include "../Timer.hpp"
#include "../UserInterruptBase.hpp"
#include "../Utils.hpp"

namespace clap
{
namespace internal
{
namespace backends
{
class PetaLinuxUserInterrupt : virtual public UserInterruptBase
{
	DISABLE_COPY_ASSIGN_MOVE(PetaLinuxUserInterrupt)

public:
	PetaLinuxUserInterrupt() {}

	virtual void Init([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, [[maybe_unused]] HasInterrupt* pReg = nullptr)
	{
		LOG_WARNING << CLASS_TAG("PetaLinuxUserInterrupt") << " Currently not implemented" << std::endl;
	}

	void Unset()
	{
		LOG_WARNING << CLASS_TAG("PetaLinuxUserInterrupt") << " Currently not implemented" << std::endl;
	}

	bool IsSet() const
	{
		LOG_WARNING << CLASS_TAG("PetaLinuxUserInterrupt") << " Currently not implemented" << std::endl;
		return false;
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE)
	{
		LOG_WARNING << CLASS_TAG("PetaLinuxUserInterrupt") << " Currently not implemented" << std::endl;
		return false;
	}
};

class PetaLinuxBackend : virtual public CLAPBackend
{
	DISABLE_COPY_ASSIGN_MOVE(PetaLinuxBackend)

public:
	PetaLinuxBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0) :
		m_memDev("/dev/mem"),
		m_readMutex(),
		m_writeMutex()
	{
		m_nameRead    = m_memDev;
		m_nameWrite   = m_memDev;
		m_backendName = "PetaLinux";

		m_fd    = OpenDevice(m_memDev);
		m_valid = (m_fd >= 0);
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_readMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		// Split the address into a rough base address and the specific offset
		// this is required to prevent alignment problems when performing the mapping with mmap
		uint64_t addrBase   = addr & 0xFFFFFFFFFFFF0000;
		uint64_t addrOffset = addr & 0xFFFF;

		uint64_t count     = 0;
		off_t offset       = addr;
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		Timer timer;

		timer.Start();

		void* pMapBase = mmap(NULL, 0x10000 + sizeInByte, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, addrBase);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(pByteData + count, (void*)(reinterpret_cast<uint8_t*>(pMapBase) + addrOffset + count), bytes);

			count += bytes;
			offset += bytes;
		}

		munmap(pMapBase, 0x10000 + sizeInByte);

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << m_memDev << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}

		LOG_VERBOSE << "Reading " << sizeInByte << " byte (" << utils::SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
					<< " ms (" << utils::SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_writeMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t addrBase   = addr & 0xFFFFFFFFFFFF0000;
		uint64_t addrOffset = addr & 0xFFFF;

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		off_t offset             = addr;

		Timer timer;

		timer.Start();

		void* pMapBase = mmap(NULL, 0x10000 + sizeInByte, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, addrBase);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			memcpy((void*)(reinterpret_cast<uint8_t*>(pMapBase) + addrOffset + count), pByteData + count, bytes);

			count += bytes;
			offset += bytes;
		}

		munmap(pMapBase, 0x10000 + sizeInByte);

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << m_memDev << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}

		LOG_VERBOSE << "Writing " << sizeInByte << " byte (" << utils::SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
					<< " ms (" << utils::SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void ReadCtrl([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] uint64_t& data, [[maybe_unused]] const std::size_t& byteCnt)
	{
		LOG_ERROR << CLASS_TAG("PetaLinuxBackend") << "ReadCtrl is currently not implemented by the PetaLinux backend." << std::endl;
	}

	UserInterruptPtr MakeUserInterrupt() const
	{
		return std::make_unique<PetaLinuxUserInterrupt>();
	}

private:
	std::string m_memDev;
	int32_t m_fd = -1;
	std::mutex m_readMutex;
	std::mutex m_writeMutex;
};

} // namespace backends
} // namespace internal
} // namespace clap