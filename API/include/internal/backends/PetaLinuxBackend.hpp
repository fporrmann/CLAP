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
#include <sys/mman.h>
/////////////////////////

#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

#include "../CLAPBackend.hpp"
#include "../Constants.hpp"
#include "../Defines.hpp"
#include "../FileOps.hpp"
#include "../Logger.hpp"
#include "../Timer.hpp"
#include "../Uio.hpp"
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
	PetaLinuxUserInterrupt() :
		m_pollFd()
	{}

	virtual void Init([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, HasInterrupt* pReg = nullptr)
	{
		if (DEVICE_HANDLE_VALID(m_fd))
			Unset();

		m_devName = "/dev/uio" + std::to_string(interruptNum);
		m_pReg    = pReg;

		m_fd          = OPEN_DEVICE(m_devName.c_str(), READ_WRITE_FLAG);
		int32_t errsv = errno;

		if (!DEVICE_HANDLE_VALID(m_fd))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxUserInterrupt") << "Unable to open device " << m_devName << "; errno: " << errsv;
			throw UserInterruptException(ss.str());
		}

		m_pollFd.fd     = m_fd;
		m_pollFd.events = POLLIN;
		m_interruptNum  = interruptNum;

		unmask();
	}

	void Unset()
	{
		CloseDevice(m_fd);

		m_pollFd.fd = -1;
		m_pReg      = nullptr;
	}

	bool IsSet() const
	{
		return (DEVICE_HANDLE_VALID(m_fd));
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true)
	{
		if (!IsSet())
		{
			std::stringstream ss("");
			ss << CLASS_TAG("PetaLinuxUserInterrupt") << "Error: Trying to wait for uninitialized user interrupt";
			throw UserInterruptException(ss.str());
		}

		unmask();
		// Poll checks whether an interrupt was generated.
		uint32_t rd = poll(&m_pollFd, 1, timeout);
		if ((rd > 0) && (m_pollFd.revents & POLLIN))
		{
			uint32_t events;

			if (m_pReg)
				m_pReg->ClearInterrupts();

			// Check how many interrupts were generated, and clear the interrupt so we can detect future interrupts.
			int32_t rc    = ::read(m_fd, &events, sizeof(events));
			int32_t errsv = errno;

			if (rc < 0)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PetaLinuxUserInterrupt") << m_devName << ", call to read failed (rc: " << rc << ") errno: " << errsv;
				throw UserInterruptException(ss.str());
			}

			uint32_t lastIntr = -1;
			if (m_pReg)
				lastIntr = m_pReg->GetLastInterrupt();

			if (runCallbacks)
			{
				for (auto& callback : m_callbacks)
					callback(lastIntr);
			}

			LOG_DEBUG << CLASS_TAG("PetaLinuxUserInterrupt") << "Interrupt present on " << m_devName << ", events: " << events << ", Interrupt Mask: " << (m_pReg ? std::to_string(m_pReg->GetLastInterrupt()) : "No Status Register Specified") << std::endl;
			return true;
		}
		else
			LOG_DEBUG << CLASS_TAG("PetaLinuxUserInterrupt") << "No Interrupt present on " << m_devName << std::endl;

		return false;
	}

private:
	void unmask()
	{
		const uint32_t unmask = 1;

		ssize_t nb = write(m_fd, &unmask, sizeof(unmask));
		if (nb != static_cast<ssize_t>(sizeof(unmask)))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxUserInterrupt") << "Error: Unable to unmask interrupt on " << m_devName;
			throw UserInterruptException(ss.str());
		}
	}

private:
	DeviceHandle m_fd = INVALID_HANDLE;
	struct pollfd m_pollFd;
};

class PetaLinuxBackend : virtual public CLAPBackend
{
	DISABLE_COPY_ASSIGN_MOVE(PetaLinuxBackend)

	enum class Mode
	{
		DevMem,
		UIO
	};

public:
	PetaLinuxBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0) :
		m_readMutex(),
		m_writeMutex()
	{
		m_backendName = "PetaLinux";

		if (!initUIO())
		{
			LOG_INFO << CLASS_TAG("PetaLinuxBackend") << "UIO not available, falling back to /dev/mem" << std::endl;
			m_nameRead  = m_devMem;
			m_nameWrite = m_devMem;
			m_fd        = OpenDevice(m_devMem);
			m_valid     = (m_fd >= 0);
		}
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		// LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_readMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t count = 0;

		Timer timer;
		timer.Start();

		if (m_mode == Mode::DevMem)
			count = readDevMem(addr, pData, sizeInByte);
		else
			count = readUIO(addr, pData, sizeInByte);

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << m_nameRead << ", failed to read 0x" << std::hex << sizeInByte << " byte from addr 0x" << addr << " (read: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}

		// LOG_VERBOSE << "Reading " << sizeInByte << " byte (" << utils::SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
		// 			<< " ms (" << utils::SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		// LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_writeMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t count = 0;

		Timer timer;
		timer.Start();

		if (m_mode == Mode::DevMem)
			count = writeDevMem(addr, pData, sizeInByte);
		else
			count = writeUIO(addr, pData, sizeInByte);

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << m_nameWrite << ", failed to write 0x" << std::hex << sizeInByte << " byte to addr 0x" << addr << " (wrote: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}

		// LOG_VERBOSE << "Writing " << sizeInByte << " byte (" << utils::SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
		// 			<< " ms (" << utils::SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void ReadCtrl([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] uint64_t& data, [[maybe_unused]] const std::size_t& byteCnt)
	{
		LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "ReadCtrl is currently not implemented by the PetaLinux backend." << std::endl;
	}

	UserInterruptPtr MakeUserInterrupt() const
	{
		return std::make_unique<PetaLinuxUserInterrupt>();
	}

private:
	bool initUIO()
	{
		if (m_uioManager.Init())
		{
			m_mode      = Mode::UIO;
			m_nameRead  = "/dev/uio";
			m_nameWrite = "/dev/uio";
			m_valid     = true;
			return true;
		}

		return false;
	}

	uint64_t readUIO(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		const UioDev<uint32_t>& dev = m_uioManager.FindUioDevByAddr(addr);
		if (!dev)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "Failed to find UIO device for address 0x" << std::hex << addr << std::dec;
			throw CLAPException(ss.str());
		}

		return dev.Read(addr, pData, sizeInByte);
	}

	uint64_t writeUIO(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		const UioDev<uint32_t>& dev = m_uioManager.FindUioDevByAddr(addr);
		if (!dev)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "Failed to find UIO device for address 0x" << std::hex << addr << std::dec;
			throw CLAPException(ss.str());
		}

		return dev.Write(addr, pData, sizeInByte);
	}

	uint64_t readDevMem(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		// Split the address into a rough base address and the specific offset
		// this is required to prevent alignment problems when performing the mapping with mmap
		uint64_t addrBase   = addr & 0xFFFFFFFFFFFF0000;
		uint64_t addrOffset = addr & 0xFFFF;

		uint64_t count     = 0;
		off_t offset       = addr;
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		void* pMapBase = mmap(NULL, 0x10000 + sizeInByte, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, addrBase);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(pByteData + count, reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(pMapBase) + addrOffset + count), bytes);

			count += bytes;
			offset += bytes;
		}

		munmap(pMapBase, 0x10000 + sizeInByte);

		return count;
	}

	uint64_t writeDevMem(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		uint64_t addrBase   = addr & 0xFFFFFFFFFFFF0000;
		uint64_t addrOffset = addr & 0xFFFF;

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		off_t offset             = addr;

		void* pMapBase = mmap(NULL, 0x10000 + sizeInByte, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, addrBase);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(pMapBase) + addrOffset + count), pByteData + count, bytes);

			count += bytes;
			offset += bytes;
		}

		munmap(pMapBase, 0x10000 + sizeInByte);

		return count;
	}

	Expected<uint64_t> ReadUIOProperty(const uint64_t& addr, const std::string& propName)
	{
		const UioDev<uint32_t>& dev = m_uioManager.FindUioDevByAddr(addr);
		if (!dev)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "Failed to find UIO device for address 0x" << std::hex << addr << std::dec;
			throw CLAPException(ss.str());
		}

		return dev.ReadBinaryProperty<uint32_t>(propName);
	}

private:
	const std::string m_devMem = "/dev/mem";
	int32_t m_fd               = -1;
	std::mutex m_readMutex;
	std::mutex m_writeMutex;
	Mode m_mode                       = Mode::DevMem;
	UioManager<uint32_t> m_uioManager = {};
};

} // namespace backends
} // namespace internal
} // namespace clap
