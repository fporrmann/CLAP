/*
 *  File: PCIeBackend.hpp
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

#include <cstdint>
#include <fcntl.h>
#include <mutex>
#include <sstream>
#include <string>

#include "../CLAPBackend.hpp"
#include "../Constants.hpp"
#include "../Defines.hpp"
#include "../FileOps.hpp"
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
class PCIeUserInterrupt : virtual public UserInterruptBase
{
	DISABLE_COPY_ASSIGN_MOVE(PCIeUserInterrupt)

public:
	PCIeUserInterrupt()
#ifndef _WIN32
		:
		m_pollFd()
#endif
	{}

	~PCIeUserInterrupt() override
	{
		unset();
	}

	void Init(const uint32_t& devNum, const uint32_t& interruptNum, HasInterrupt* pReg = nullptr) override
	{
		if (DEVICE_HANDLE_VALID(m_fd))
			Unset();

		m_devName = "/dev/xdma" + std::to_string(devNum) + "_events_" + std::to_string(interruptNum);
		m_pReg    = pReg;

		m_fd          = OPEN_DEVICE(m_devName.c_str(), READ_ONLY_FLAG);
		int32_t errsv = errno;

		if (!DEVICE_HANDLE_VALID(m_fd))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeUserInterrupt") << "Unable to open device " << m_devName << "; errno: " << errsv;
			throw UserInterruptException(ss.str());
		}

#ifndef _WIN32
		m_pollFd.fd     = m_fd;
		m_pollFd.events = POLLIN;
#endif
		m_interruptNum = interruptNum;
	}

	void Unset() override
	{
		unset();
	}

	bool IsSet() const override
	{
		return (DEVICE_HANDLE_VALID(m_fd));
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true) override
	{
#ifdef _WIN32
		CLAP_LOG_ERROR << CLASS_TAG("PCIeUserInterrupt") << " Currently not implemented for Windows" << std::endl;
		return false;
#else
		if (!IsSet())
		{
			std::stringstream ss("");
			ss << CLASS_TAG("PCIeUserInterrupt") << "Error: Trying to wait for uninitialized user interrupt";
			throw UserInterruptException(ss.str());
		}

		// Poll checks whether an interrupt was generated.
		uint32_t rd = poll(&m_pollFd, 1, timeout);
		if ((rd > 0) && (m_pollFd.revents & POLLIN))
		{
			uint32_t events;

			if (m_pReg)
				m_pReg->ClearInterrupts();

			// Check how many interrupts were generated, and clear the interrupt so we can detect future interrupts.
			int32_t rc    = pread(m_fd, &events, sizeof(events), 0);
			int32_t errsv = errno;

			if (rc < 0)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeUserInterrupt") << m_devName << ", call to pread failed (rc: " << rc << ") errno: " << errsv;
				throw UserInterruptException(ss.str());
			}

			uint32_t lastIntr = -1;
			if (m_pReg)
				lastIntr = m_pReg->GetLastInterrupt();

			if (runCallbacks)
			{
				for (const auto& callback : m_callbacks)
					callback(lastIntr);
			}

			CLAP_LOG_DEBUG << CLASS_TAG("PCIeUserInterrupt") << "Interrupt present on " << m_devName << ", events: " << events << ", Interrupt Mask: " << (m_pReg ? std::to_string(lastIntr) : "No Status Register Specified") << std::endl;
			return true;
		}
		// else
		// 	CLAP_LOG_DEBUG << CLASS_TAG("PCIeUserInterrupt") << "No Interrupt present on " << m_devName << std::endl;

		return false;
#endif // _WIN32
	}

private:
	void unset()
	{
		if (!DEVICE_HANDLE_VALID(m_fd)) return;

		CLOSE_DEVICE(m_fd);
		m_fd = INVALID_HANDLE;

#ifndef _WIN32
		m_pollFd.fd = -1;
#endif
		m_pReg = nullptr;
	}

private:
	DeviceHandle m_fd = INVALID_HANDLE;
#ifndef _WIN32
	struct pollfd m_pollFd;
#endif
};

class PCIeBackend : virtual public CLAPBackend
{
	DISABLE_COPY_ASSIGN_MOVE(PCIeBackend)

public:
	explicit PCIeBackend(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0) :
		m_h2cDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_h2c_" + std::to_string(channelNum)),
		m_c2hDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_c2h_" + std::to_string(channelNum)),
		m_ctrlDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_control"),
		m_devNum(deviceNum),
		m_readMutex(),
		m_writeMutex(),
		m_ctrlMutex()
	{
		m_nameRead    = m_c2hDeviceName;
		m_nameWrite   = m_h2cDeviceName;
		m_nameCtrl    = m_ctrlDeviceName;
		m_backendName = "XDMA PCIe";

		m_h2cFd  = OpenDevice(m_h2cDeviceName);
		m_c2hFd  = OpenDevice(m_c2hDeviceName);
		m_ctrlFd = OpenDevice(m_ctrlDeviceName, CTRL_OPEN_FLAGS);
		m_valid  = (DEVICE_HANDLE_VALID(m_h2cFd) && DEVICE_HANDLE_VALID(m_c2hFd) && DEVICE_HANDLE_VALID(m_ctrlFd));
	}

	~PCIeBackend() override
	{
		// Try to lock the read, write and ctrl mutex in order to prevent read, write or ctrl access
		// while the XDMA object is being destroyed and also to prevent the destruction
		// of the object while a read, write or ctrl access is still in progress
		std::lock_guard<std::mutex> lockRd(m_readMutex);
		std::lock_guard<std::mutex> lockWd(m_writeMutex);
		std::lock_guard<std::mutex> lockCtrl(m_ctrlMutex);

		CLOSE_DEVICE(m_h2cFd);
		CLOSE_DEVICE(m_c2hFd);
		CLOSE_DEVICE(m_ctrlFd);
	}

	uint32_t GetDevNum() const override
	{
		return m_devNum;
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte) override
	{
		// CLAP_LOG_DEBUG << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_readMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		if (!IS_ALIGNED(pData, ALIGNMENT))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "pData is not aligned to " << ALIGNMENT << " bytes.";
			throw CLAPException(ss.str());
		}

		uint64_t count     = 0;
		OffsetType offset  = static_cast<OffsetType>(addr);
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		FileOpType rc;
		Timer timer;

		timer.Start();

		while (count < sizeInByte)
		{
			ByteCntType bytes = (sizeInByte - count) > RW_MAX_SIZE ? RW_MAX_SIZE : static_cast<ByteCntType>(sizeInByte - count);

			rc = SEEK(m_c2hFd, offset);
			if (SEEK_INVALID(rc, offset))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to seek to offset 0x" << std::hex << offset << " (rc: 0x" << rc << ")" << std::dec;
				throw CLAPException(ss.str());
			}

#if _WIN32
			if (!ReadFile(m_c2hFd, pByteData + count, bytes, &rc, NULL))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << bytes << " byte from offset 0x" << offset << " Error: " << GetLastError() << std::dec;
				throw CLAPException(ss.str());
			}
#else
			rc = ::read(m_c2hFd, pByteData + count, bytes);
#endif

			int32_t errsv = errno;

			if (static_cast<ByteCntType>(rc) != bytes)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << bytes << " byte from offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << std::dec << errsv << " (" << strerror(errsv) << ")";
				throw CLAPException(ss.str());
			}

			count += bytes;
			offset += bytes; // TODO: this might cause problems in streaming mode
		}

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}

		logTransferTime(addr, sizeInByte, timer, true);
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte) override
	{
		// CLAP_LOG_DEBUG << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_writeMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		if (!IS_ALIGNED(pData, ALIGNMENT))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "pData is not aligned to " << ALIGNMENT << " bytes.";
			throw CLAPException(ss.str());
		}

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		OffsetType offset        = static_cast<OffsetType>(addr);

		FileOpType rc;
		Timer timer;

		timer.Start();

		while (count < sizeInByte)
		{
			ByteCntType bytes = (sizeInByte - count) > RW_MAX_SIZE ? RW_MAX_SIZE : static_cast<ByteCntType>(sizeInByte - count);

			rc = SEEK(m_h2cFd, offset);
			if (SEEK_INVALID(rc, offset))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to seek to offset 0x" << std::hex << offset << " (rc: 0x" << rc << ")" << std::dec;
				throw CLAPException(ss.str());
			}

#ifdef _WIN32
			if (!WriteFile(m_h2cFd, pByteData + count, bytes, &rc, NULL))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << bytes << " byte to offset 0x" << offset << " Error: " << GetLastError() << std::dec;
				throw CLAPException(ss.str());
			}
#else
			rc = ::write(m_h2cFd, pByteData + count, bytes);
#endif
			int32_t errsv = errno;

			if (static_cast<ByteCntType>(rc) != bytes)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << bytes << " byte to offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << std::dec << errsv << " (" << strerror(errsv) << ")";
				throw CLAPException(ss.str());
			}

			count += bytes;
			offset += bytes; // TODO: This might casue problems in streaming mode
		}

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)" << std::dec;
			throw CLAPException(ss.str());
		}

		logTransferTime(addr, sizeInByte, timer, false);
	}

	void ReadCtrl(const uint64_t& addr, uint64_t& data, const std::size_t& byteCnt) override
	{
		CLAP_LOG_DEBUG << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " data=0x" << &data << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_ctrlMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		if (byteCnt > 8)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "byteCnt is greater than 8 (64-bit), which is not supported.";
			throw CLAPException(ss.str());
		}

		ByteCntType bytes = static_cast<ByteCntType>(byteCnt);
		OffsetType offset = static_cast<OffsetType>(addr);
		FileOpType rc;

#ifdef _WIN32
		OVERLAPPED ol = { 0 };
		ol.Offset     = addr & 0xFFFFFFFF;
		ol.OffsetHigh = addr >> 32;

		if (!ReadFile(m_ctrlFd, &data, bytes, &rc, NULL))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_ctrlDeviceName << ", failed to read 0x" << std::hex << bytes << " byte from offset 0x" << addr << " Error: " << GetLastError() << std::dec;
			throw CLAPException(ss.str());
		}
#else
		rc = ::pread(m_ctrlFd, &data, bytes, offset);
#endif
		int32_t errsv = errno;

		if (static_cast<ByteCntType>(rc) != bytes)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_ctrlDeviceName << ", failed to read 0x" << std::hex << bytes << " byte to offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << std::dec << errsv << " (" << strerror(errsv) << ")";
			throw CLAPException(ss.str());
		}
	}

	UserInterruptPtr MakeUserInterrupt() const override
	{
		return std::make_unique<PCIeUserInterrupt>();
	}

private:
	std::string m_h2cDeviceName;
	std::string m_c2hDeviceName;
	std::string m_ctrlDeviceName;
	DeviceHandle m_h2cFd  = INVALID_HANDLE;
	DeviceHandle m_c2hFd  = INVALID_HANDLE;
	DeviceHandle m_ctrlFd = INVALID_HANDLE;
	uint32_t m_devNum;
	std::mutex m_readMutex;
	std::mutex m_writeMutex;
	std::mutex m_ctrlMutex;
};

} // namespace backends
} // namespace internal
} // namespace clap
