/* 
 *  File: xdmaBackend.h
 *  Copyright (c) 2021 Florian Porrmann
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

#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef EMBEDDED_XILINX
/////////////////////////
// Include for mmap(), munmap()
#ifndef _WIN32
#include <sys/mman.h>
#endif
/////////////////////////
#endif

#include "Constants.h"
#include "Defines.h"
#include "Logger.h"
#include "Utils.h"

#ifndef EMBEDDED_XILINX
#include "Timer.h"
#endif

/*
 * man 2 write:
 * On Linux, write() (and similar system calls) will transfer at most
 * 	0x7ffff000 (2,147,479,552) bytes, returning the number of bytes
 *	actually transferred.  (This is true on both 32-bit and 64-bit
 *	systems.)
 */
static const uint32_t RW_MAX_SIZE = 0x7ffff000;

DEFINE_EXCEPTION(XDMAException)

class XDMABackend
{
public:
	enum class TYPE
	{
		READ,
		WRITE,
		CONTROL
	};

public:
	XDMABackend() {}

	virtual ~XDMABackend() {}

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
			throw XDMAException(ss.str());
		}

		return fd;
	}

protected:
	bool m_valid            = false;
	std::string m_nameRead  = "";
	std::string m_nameWrite = "";
	std::string m_nameCtrl  = "";
	std::string m_backendName = "XDMA";
};

#ifndef EMBEDDED_XILINX
class PCIeBackend : virtual public XDMABackend
{
	DISABLE_COPY_ASSIGN_MOVE(PCIeBackend)

public:
	PCIeBackend(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0) :
		m_h2cDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_h2c_" + std::to_string(channelNum)),
		m_c2hDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_c2h_" + std::to_string(channelNum)),
		m_ctrlDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_control"),
		m_devNum(deviceNum),
		m_readMutex(),
		m_writeMutex(),
		m_ctrlMutex()
	{
		m_nameRead  = m_c2hDeviceName;
		m_nameWrite = m_h2cDeviceName;
		m_nameCtrl  = m_ctrlDeviceName;
		m_backendName = "XDMA PCIe";

		m_h2cFd     = OpenDevice(m_h2cDeviceName);
		m_c2hFd     = OpenDevice(m_c2hDeviceName);
		m_ctrlFd    = OpenDevice(m_ctrlDeviceName, CTRL_OPEN_FLAGS);
		m_valid     = (DEVICE_HANDLE_VALID(m_h2cFd) && DEVICE_HANDLE_VALID(m_c2hFd) && DEVICE_HANDLE_VALID(m_ctrlFd));
	}

	virtual ~PCIeBackend()
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

	uint32_t GetDevNum() const
	{
		return m_devNum;
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_readMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		if (!IS_ALIGNED(pData, XDMA_ALIGNMENT))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "pData is not aligned to " << XDMA_ALIGNMENT << " bytes.";
			throw XDMAException(ss.str());
		}

		uint64_t count     = 0;
		OffsetType offset  = static_cast<OffsetType>(addr);
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		FileOpType rc;
		xdma::Timer timer;

		timer.Start();

		while (count < sizeInByte)
		{
			ByteCntType bytes = (sizeInByte - count) > RW_MAX_SIZE ? RW_MAX_SIZE : static_cast<ByteCntType>(sizeInByte - count);

			rc = SEEK(m_c2hFd, offset);
			if (SEEK_INVALID(rc, offset))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to seek to offset 0x" << std::hex << offset << " (rc: 0x" << rc << ")" << std::dec;
				throw XDMAException(ss.str());
			}

#if _WIN32
			if (!ReadFile(m_c2hFd, pByteData + count, bytes, &rc, NULL))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << bytes << " byte from offset 0x" << offset << " Error: " << GetLastError() << std::dec;
				throw XDMAException(ss.str());
			}
#else
			rc = ::read(m_c2hFd, pByteData + count, bytes);
#endif

			int32_t errsv = errno;

			if (static_cast<ByteCntType>(rc) != bytes)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << bytes << " byte from offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << std::dec << errsv << " (" << strerror(errsv) << ")";
				throw XDMAException(ss.str());
			}

			count += bytes;
			offset += bytes; // TODO: this might cause problems in streaming mode
		}

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)" << std::dec;
			throw XDMAException(ss.str());
		}

		LOG_VERBOSE << "Reading " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
					<< " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_writeMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		if (!IS_ALIGNED(pData, XDMA_ALIGNMENT))
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "pData is not aligned to " << XDMA_ALIGNMENT << " bytes.";
			throw XDMAException(ss.str());
		}

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		OffsetType offset        = static_cast<OffsetType>(addr);

		FileOpType rc;
		xdma::Timer timer;

		timer.Start();

		while (count < sizeInByte)
		{
			ByteCntType bytes = (sizeInByte - count) > RW_MAX_SIZE ? RW_MAX_SIZE : static_cast<ByteCntType>(sizeInByte - count);

			rc = SEEK(m_h2cFd, offset);
			if (SEEK_INVALID(rc, offset))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to seek to offset 0x" << std::hex << offset << " (rc: 0x" << rc << ")" << std::dec;
				throw XDMAException(ss.str());
			}

#ifdef _WIN32
			if (!WriteFile(m_h2cFd, pByteData + count, bytes, &rc, NULL))
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << bytes << " byte to offset 0x" << offset << " Error: " << GetLastError() << std::dec;
				throw XDMAException(ss.str());
			}
#else
			rc = ::write(m_h2cFd, pByteData + count, bytes);
#endif
			int32_t errsv = errno;

			if (static_cast<ByteCntType>(rc) != bytes)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << bytes << " byte to offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << std::dec << errsv << " (" << strerror(errsv) << ")";
				throw XDMAException(ss.str());
			}

			count += bytes;
			offset += bytes; // TODO: This might casue problems in streaming mode
		}

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)" << std::dec;
			throw XDMAException(ss.str());
		}

		LOG_VERBOSE << "Writing " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
					<< " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void ReadCtrl(const uint64_t& addr, uint64_t& data, const std::size_t& byteCnt)
	{
		LOG_DEBUG << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " data=0x" << &data << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_ctrlMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		if(byteCnt > 8)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << "byteCnt is greater than 8 (64-bit), which is not supported.";
			throw XDMAException(ss.str());
		}

		ByteCntType bytes = byteCnt;
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
			throw XDMAException(ss.str());
		}
#else
		rc = ::pread(m_ctrlFd, &data, bytes, offset);
#endif
		int32_t errsv = errno;

		if (static_cast<ByteCntType>(rc) != bytes)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PCIeBackend") << m_ctrlDeviceName << ", failed to read 0x" << std::hex << bytes << " byte to offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << std::dec << errsv << " (" << strerror(errsv) << ")";
			throw XDMAException(ss.str());
		}
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

#ifndef _WIN32

class PetaLinuxBackend : virtual public XDMABackend
{
	DISABLE_COPY_ASSIGN_MOVE(PetaLinuxBackend)

public:
	PetaLinuxBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0) :
		m_memDev("/dev/mem"),
		m_readMutex(),
		m_writeMutex()
	{
		m_nameRead  = m_memDev;
		m_nameWrite = m_memDev;
		m_backendName = "PetaLinux";

		
		m_fd        = OpenDevice(m_memDev);
		m_valid     = (m_fd >= 0);
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_readMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		// Split the address into a rough base address and the specific offset
		// this is required to prevent alignment problems when performing the mapping with mmap
		uint64_t addrBase   = addr & 0xFFFFFFFFFFFF0000;
		uint64_t addrOffset = addr & 0xFFFF;

		uint64_t count     = 0;
		off_t offset       = addr;
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		xdma::Timer timer;

		timer.Start();

		void* pMapBase = mmap(NULL, 0x10000 + sizeInByte, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, addrBase);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			memcpy(pByteData + count, (void*)(reinterpret_cast<uint8_t*>(pMapBase) + addrOffset + count), bytes);

			count += bytes;
			offset += bytes;
		}

		munmap(pMapBase, 0x10000 + sizeInByte);

		timer.Stop();

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << m_memDev << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)" << std::dec;
			throw XDMAException(ss.str());
		}

		LOG_VERBOSE << "Reading " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
					<< " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		std::lock_guard<std::mutex> lock(m_writeMutex);

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("PetaLinuxBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		uint64_t addrBase   = addr & 0xFFFFFFFFFFFF0000;
		uint64_t addrOffset = addr & 0xFFFF;

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		off_t offset             = addr;

		xdma::Timer timer;

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
			throw XDMAException(ss.str());
		}

		LOG_VERBOSE << "Writing " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
					<< " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
	}

	void ReadCtrl([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] uint64_t& data, [[maybe_unused]] const std::size_t& byteCnt)
	{
		LOG_ERROR << CLASS_TAG("PetaLinuxBackend") << "ReadCtrl is currently not implemented by the PetaLinux backend." << std::endl;
	}

private:
	std::string m_memDev;
	int32_t m_fd = -1;
	std::mutex m_readMutex;
	std::mutex m_writeMutex;
};

#endif // _WIN32
#endif // EMBEDDED_XILINX

#ifndef _WIN32

class BareMetalBackend : virtual public XDMABackend
{
public:
	BareMetalBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0)
	{
		m_nameRead  = "BareMetal";
		m_nameWrite = "BareMetal";
		m_backendName = "BareMetal";
		m_valid     = true;
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		uint64_t count     = 0;
		off_t offset       = addr;
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			memcpy(pByteData + count, (void*)(offset), bytes);

			count += bytes;
			offset += bytes;
		}

		if (count != sizeInByte)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)" << std::dec;
			throw XDMAException(ss.str());
		}
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
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
			throw XDMAException(ss.str());
		}
	}

	void ReadCtrl([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] uint64_t& data, [[maybe_unused]] const std::size_t& byteCnt)
	{
		LOG_ERROR << CLASS_TAG("PetaLinuxBackend") << "ReadCtrl is currently not implemented by the PetaLinux backend." << std::endl;
	}
};

#endif // _WIN32