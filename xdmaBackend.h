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

#include "Utils.h"

/*
 * man 2 write:
 * On Linux, write() (and similar system calls) will transfer at most
 * 	0x7ffff000 (2,147,479,552) bytes, returning the number of bytes
 *	actually transferred.  (This is true on both 32-bit and 64-bit
 *	systems.)
 */
static const uint32_t RW_MAX_SIZE = 0x7ffff000;

class XDMAException : public std::exception
{
	public:
		explicit XDMAException(const std::string& what) : m_what(what) {}

		virtual ~XDMAException() throw() {}

		virtual const char* what() const throw()
		{
			return m_what.c_str();
		}

	private:
		std::string m_what;
};

class XDMABackend
{
	public:
		enum TYPE
		{
			READ,
			WRITE
		};

	public:
		XDMABackend() :
			m_valid(false),
			m_nameRead(""),
			m_nameWrite("")
		{}
		virtual ~XDMABackend() {}

		virtual void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte, const bool& verbose = false) = 0;
		virtual void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte, const bool& verbose = false) = 0;

		virtual uint32_t GetDevNum() const
		{
			return 0;
		}

		const std::string& GetName(const TYPE& type) const
		{
			if(type == READ)
				return m_nameRead;
			else
				return m_nameWrite;
		}

		int OpenDevice(const std::string& name, int flags = O_RDWR | O_NONBLOCK) const
		{
			int fd = open(name.c_str(), flags);
			int err = errno;

			if (fd < 0)
			{
				std::stringstream ss;
				ss << CLASS_TAG("") << "Unable to open device " << name << "; errno: " << err;
				throw XDMAException(ss.str());
			}

			return fd;
		}

	protected:
		bool m_valid;
		std::string m_nameRead;
		std::string m_nameWrite;
};

#ifndef EMBEDDED_XILINX
class PCIeBackend : virtual public XDMABackend
{
	DISABLE_COPY_ASSIGN_MOVE(PCIeBackend)

	public:
		PCIeBackend(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0) :
			m_h2cDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_h2c_" + std::to_string(channelNum)),
			m_c2hDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_c2h_" + std::to_string(channelNum)),
			m_h2cFd(-1),
			m_c2hFd(-1),
			m_mapBase(nullptr),
			m_devNum(deviceNum),
			m_readMutex(),
			m_writeMutex()
		{
			m_nameRead  = m_c2hDeviceName;
			m_nameWrite = m_h2cDeviceName;
			m_h2cFd = OpenDevice(m_h2cDeviceName);
			m_c2hFd = OpenDevice(m_c2hDeviceName);
			m_valid = (m_h2cFd >= 0 && m_c2hFd >= 0);
		}

		virtual ~PCIeBackend()
		{
			// Try to lock the read and write mutex in order to prevent read/write access
			// while the XDMA object is being destroyed and also to prevent the destruction
			// of the object while a read/write access is still in progress
			std::lock_guard<std::mutex> lockRd(m_readMutex);
			std::lock_guard<std::mutex> lockWd(m_writeMutex);

			close(m_h2cFd);
			close(m_c2hFd);
		}

		uint32_t GetDevNum() const
		{
			return m_devNum;
		}


		void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
		{
#ifdef XDMA_VERBOSE
			std::cout << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::endl;
#endif

			std::lock_guard<std::mutex> lock(m_readMutex);

			if(!m_valid)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
				throw XDMAException(ss.str());
			}
			
			uint64_t count = 0;
			off_t offset = addr;
			uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

			ssize_t rc;
			Timer timer;

			if(verbose) timer.Start();

			while (count < sizeInByte)
			{
				uint64_t bytes = sizeInByte - count;

				if (bytes > RW_MAX_SIZE)
					bytes = RW_MAX_SIZE;

				rc = lseek(m_c2hFd, offset, SEEK_SET);
				if (rc != offset)
				{
					std::stringstream ss;
					ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to seek to offset 0x" << std::hex << offset << " (rc: 0x" << rc << ")";
					throw XDMAException(ss.str());
				}

				rc = ::read(m_c2hFd, pByteData + count, bytes);
				int errsv = errno;
				if (static_cast<uint64_t>(rc) != bytes)
				{
					std::stringstream ss;
					ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << bytes << " byte from offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << errsv << " (" << strerror(errsv) << ")";
					throw XDMAException(ss.str());
				}

				count += bytes;
				offset += bytes;
			}

			if(verbose) timer.Stop();

			if (count != sizeInByte)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_c2hDeviceName << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)";
				throw XDMAException(ss.str());
			}

			if(verbose)
			{
				std::cout << "Reading " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
				          << " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
			}
		}

		void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
		{
#ifdef XDMA_VERBOSE

			std::cout << CLASS_TAG("PCIeBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::endl;
#endif

			std::lock_guard<std::mutex> lock(m_writeMutex);

			if(!m_valid)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
				throw XDMAException(ss.str());
			}

			uint64_t count = 0;
			const uint8_t *pByteData = reinterpret_cast<const uint8_t*>(pData);
			off_t offset = addr;

			ssize_t rc;
			Timer timer;

			if(verbose) timer.Start();

			while (count < sizeInByte)
			{
				uint64_t bytes = sizeInByte - count;

				if (bytes > RW_MAX_SIZE)
					bytes = RW_MAX_SIZE;

				rc = lseek(m_h2cFd, offset, SEEK_SET);
				if (rc != offset)
				{
					std::stringstream ss;
					ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to seek to offset 0x" << std::hex << offset << " (rc: 0x" << rc << ")";
					throw XDMAException(ss.str());
				}

				rc = ::write(m_h2cFd, pByteData + count, bytes);
				int errsv = errno;
				if (static_cast<uint64_t>(rc) != bytes)
				{
					std::stringstream ss;
					ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << bytes << " byte to offset 0x" << offset << " (rc: 0x" << rc << ") errno: " << errsv << " (" << strerror(errsv) << ")";
					throw XDMAException(ss.str());
				}

				count += bytes;
				offset += bytes;
			}

			if(verbose) timer.Stop();

			if (count != sizeInByte)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PCIeBackend") << m_h2cDeviceName << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)";
				throw XDMAException(ss.str());
			}

			if(verbose)
			{
				std::cout << "Writing " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
				          << " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
			}
		}

	private:
		std::string m_h2cDeviceName;
		std::string m_c2hDeviceName;
		int m_h2cFd;
		int m_c2hFd;
		void* m_mapBase;
		uint32_t m_devNum;
		std::mutex m_readMutex;
		std::mutex m_writeMutex;
};

class PetaLinuxBackend : virtual public XDMABackend
{
	DISABLE_COPY_ASSIGN_MOVE(PetaLinuxBackend)

	public:
		PetaLinuxBackend() :
			m_memDev("/dev/mem"),
			m_fd(-1),
			m_mapBase(nullptr),
			m_readMutex(),
			m_writeMutex()
		{
			m_nameRead  = m_memDev;
			m_nameWrite = m_memDev;
			m_fd = OpenDevice(m_memDev);
			m_valid = (m_fd >= 0);
		}

		void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
		{
#ifdef XDMA_VERBOSE
			std::cout << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::endl;
#endif

			std::lock_guard<std::mutex> lock(m_readMutex);

			if(!m_valid)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PetaLinuxBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
				throw XDMAException(ss.str());
			}

			// Split the address into a rough base address and the specific offset
			// this is required to prevent alignment problems when performing the mapping with mmap
			uint64_t addrBase = addr & 0xFFFFFFFFFFFF0000;
			uint64_t addrOffset = addr & 0xFFFF;

			uint64_t count = 0;
			off_t offset = addr;
			uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

			Timer timer;

			if(verbose) timer.Start();

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

			if(verbose) timer.Stop();

			if (count != sizeInByte)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PetaLinuxBackend") << m_memDev << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)";
				throw XDMAException(ss.str());
			}

			if(verbose)
			{
				std::cout << "Reading " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
				          << " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
			}
		}

		void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
		{
#ifdef XDMA_VERBOSE

			std::cout << CLASS_TAG("PetaLinuxBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::endl;
#endif

			std::lock_guard<std::mutex> lock(m_writeMutex);

			if(!m_valid)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PetaLinuxBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
				throw XDMAException(ss.str());
			}

			uint64_t addrBase = addr & 0xFFFFFFFFFFFF0000;
			uint64_t addrOffset = addr & 0xFFFF;

			uint64_t count = 0;
			const uint8_t *pByteData = reinterpret_cast<const uint8_t*>(pData);
			off_t offset = addr;

			Timer timer;

			if(verbose) timer.Start();

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

			if(verbose) timer.Stop();

			if (count != sizeInByte)
			{
				std::stringstream ss;
				ss << CLASS_TAG("PetaLinuxBackend") << m_memDev << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)";
				throw XDMAException(ss.str());
			}

			if(verbose)
			{
				std::cout << "Writing " << sizeInByte << " byte (" << SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
				          << " ms (" << SpeedWidthSuffix(sizeInByte / timer.GetElapsedTime()) << ")" << std::endl;
			}
		}

	private:
		std::string m_memDev;
		int m_fd;
		void* m_mapBase;
		std::mutex m_readMutex;
		std::mutex m_writeMutex;
};
#endif

class BareMetalBackend : virtual public XDMABackend
{
	public:
		BareMetalBackend()
		{
			m_nameRead  = "BareMetal";
			m_nameWrite = "BareMetal";
			m_valid = true;
		}


		void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
		{
			UNUSED(verbose);
#ifdef XDMA_VERBOSE
			std::cout << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::endl;
#endif

			if(!m_valid)
			{
				std::stringstream ss;
				ss << CLASS_TAG("BareMetalBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
				throw XDMAException(ss.str());
			}

			uint64_t count = 0;
			off_t offset = addr;
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
				ss << CLASS_TAG("BareMetalBackend") << ", failed to read 0x" << std::hex << sizeInByte << " byte from offset 0x" << offset << " (read: 0x" << count << " byte)";
				throw XDMAException(ss.str());
			}
		}

		void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
		{
			UNUSED(verbose);
#ifdef XDMA_VERBOSE
			std::cout << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::endl;
#endif

			if(!m_valid)
			{
				std::stringstream ss;
				ss << CLASS_TAG("BareMetalBackend") << "XDMA Instance is not valid, an error probably occurred during device initialization.";
				throw XDMAException(ss.str());
			}

			uint64_t count = 0;
			const uint8_t *pByteData = reinterpret_cast<const uint8_t*>(pData);
			off_t offset = addr;

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
				ss << CLASS_TAG("BareMetalBackend") << ", failed to write 0x" << std::hex << sizeInByte << " byte to offset 0x" << offset << " (wrote: 0x" << count << " byte)";
				throw XDMAException(ss.str());
			}
		}
};

