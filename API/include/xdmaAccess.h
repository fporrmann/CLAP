/* 
 *  File: xdmaAccess.h
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

////// TODO:
// - Proper logging
// - Function to create a new XDMA instance (so user does not have to create the shared pointer)
// - Maybe change the way IP core objects are created, possible to create from an XDMA object?

/////////////////////////
// Includes for open()
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
/////////////////////////

/////////////////////////
// Include for read(), write(), lseek()
#include <unistd.h>
/////////////////////////

#ifndef EMBEDDED_XILINX
/////////////////////////
// Include for mmap(), munmap()
#include <sys/mman.h>
/////////////////////////
#endif

#include <algorithm>
#include <cstring> // required for std::memcpy
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

#include "Memory.h"
#include "Utils.h"

#ifndef EMBEDDED_XILINX
#include "Timer.h"
#include "xdmaAlignmentAllocator.h"
#endif

#include "xdmaBackend.h"

static const std::size_t XDMA_ALIGNMENT = 4096;

#ifdef EMBEDDED_XILINX
using DMABuffer = std::vector<uint8_t>;
#else
using DMABuffer = std::vector<uint8_t, xdma::AlignmentAllocator<uint8_t, XDMA_ALIGNMENT>>;
#endif
using XDMAManagedShr   = std::shared_ptr<class XDMAManaged>;
using XDMABackendShr   = std::shared_ptr<class XDMABackend>;
using MemoryManagerShr = std::shared_ptr<MemoryManager>;
using MemoryManagerVec = std::vector<MemoryManagerShr>;

class XDMAManaged
{
	friend class XDMABase;
	DISABLE_COPY_ASSIGN_MOVE(XDMAManaged)

protected:
	XDMAManaged(class XDMABase* pXdma);

	~XDMAManaged();

	class XDMABase* XDMA()
	{
		checkXDMAValid();
		return m_pXdma;
	}

private:
	void markXDMAInvalid()
	{
		m_pXdma = nullptr;
	}

	void checkXDMAValid() const
	{
		if (m_pXdma == nullptr)
		{
			std::stringstream ss;
			ss << CLASS_TAG("ApCtrl") << "XDMA/XDMAPio instance is not valid or has been destroyed";
			throw XDMAException(ss.str());
		}
	}

private:
	class XDMABase* m_pXdma;
};

class XDMABase
{
	friend class XDMAManaged;

public:
	virtual uint8_t Read8(const uint64_t& addr)   = 0;
	virtual uint16_t Read16(const uint64_t& addr) = 0;
	virtual uint32_t Read32(const uint64_t& addr) = 0;
	virtual uint64_t Read64(const uint64_t& addr) = 0;

	virtual void Write8(const uint64_t& addr, const uint8_t& data)   = 0;
	virtual void Write16(const uint64_t& addr, const uint16_t& data) = 0;
	virtual void Write32(const uint64_t& addr, const uint32_t& data) = 0;
	virtual void Write64(const uint64_t& addr, const uint64_t& data) = 0;

	uint32_t GetDevNum() const
	{
		return m_devNum;
	}

protected:
	XDMABase(const uint32_t& devNum) :
		m_devNum(devNum),
		m_managedObjects()
	{
	}

	virtual ~XDMABase()
	{
		for (XDMAManaged* pM : m_managedObjects)
			pM->markXDMAInvalid();
	}

private:
	void registerObject(XDMAManaged* pObj)
	{
		m_managedObjects.push_back(pObj);
	}

	void unregisterObject(XDMAManaged* pObj)
	{
		m_managedObjects.erase(std::remove(m_managedObjects.begin(), m_managedObjects.end(), pObj), m_managedObjects.end());
	}

protected:
	uint32_t m_devNum;
	std::vector<XDMAManaged*> m_managedObjects;
};

class XDMA : virtual public XDMABase
{
public:
	enum class MemoryType
	{
		DDR,
		BRAM
	};

private:
	using MemoryPair = std::pair<MemoryType, MemoryManagerVec>;

public:
	XDMA(XDMABackendShr pBackend) :
		XDMABase(pBackend->GetDevNum()),
		m_pBackend(pBackend),
		m_memories()
#ifndef EMBEDDED_XILINX
		,
		m_mutex()
#endif
	{
		m_memories.insert(MemoryPair(MemoryType::DDR, MemoryManagerVec()));
		m_memories.insert(MemoryPair(MemoryType::BRAM, MemoryManagerVec()));
	}

	~XDMA()
	{
	}

	void AddMemoryRegion(const MemoryType& type, const uint64_t& baseAddr, const uint64_t& size)
	{
		m_memories[type].push_back(std::make_shared<MemoryManager>(baseAddr, size));
	}

	Memory AllocMemory(const MemoryType& type, const uint64_t& byteSize, const int32_t& memIdx = -1)
	{
#ifndef EMBEDDED_XILINX
		std::lock_guard<std::mutex> lock(m_mutex);
#endif

		if (memIdx == -1)
		{
			for (MemoryManagerShr& mem : m_memories[type])
			{
				if (mem->GetAvailableSpace() >= byteSize)
					return mem->AllocMemory(byteSize);
			}
		}
		else
		{
			if (m_memories[type].size() <= static_cast<uint32_t>(memIdx))
			{
				std::stringstream ss;
				ss << CLASS_TAG("XDMA") << "Specified memory region " << std::dec << memIdx << " does not exist.";
				throw XDMAException(ss.str());
			}

			return m_memories[type][memIdx]->AllocMemory(byteSize);
		}

		std::stringstream ss;
		ss << CLASS_TAG("XDMA") << "No memory region found with enough space left to allocate " << std::dec << byteSize << " byte.";
		throw XDMAException(ss.str());
	}

	Memory AllocMemory(const MemoryType& type, const uint64_t& elements, const uint64_t& sizeOfElement, const int32_t& memIdx = -1)
	{
		return AllocMemory(type, elements * sizeOfElement, memIdx);
	}

	Memory AllocMemoryDDR(const uint64_t& elements, const uint64_t& sizeOfElement, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::DDR, elements, sizeOfElement, memIdx);
	}

	Memory AllocMemoryBRAM(const uint64_t& elements, const uint64_t& sizeOfElement, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::BRAM, elements, sizeOfElement, memIdx);
	}

	Memory AllocMemoryDDR(const uint64_t& byteSize, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::DDR, byteSize, memIdx);
	}

	Memory AllocMemoryBRAM(const uint64_t& byteSize, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::BRAM, byteSize, memIdx);
	}

	////////////////////////////////////////////////////////////////////////////
	///                      Read Functions                                  ///
	////////////////////////////////////////////////////////////////////////////

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
	{
		m_pBackend->Read(addr, pData, sizeInByte, verbose);
	}

	void Read(const Memory& mem, void* pData, const uint64_t& sizeInByte = USE_MEMORY_SIZE, const bool& verbose = false)
	{
		uint64_t size = (sizeInByte == USE_MEMORY_SIZE ? mem.GetSize() : sizeInByte);
		if (size > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMA") << m_pBackend->GetName(XDMABackend::TYPE::READ) << ", specified size (0x" << std::hex << size << ") exceeds size of the given buffer (0x" << std::hex << mem.GetSize() << ")";
			throw XDMAException(ss.str());
		}

		Read(mem.GetBaseAddr(), pData, size, verbose);
	}

	void Read(const uint64_t& addr, DMABuffer& buffer, const uint64_t& sizeInByte, const bool& verbose = false)
	{
		if (sizeInByte > buffer.size())
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMA") << "Size of buffer provided (" << std::dec << buffer.size() << ") is smaller than the desired read size (" << sizeInByte << ")";
			throw XDMAException(ss.str());
		}

		Read(addr, buffer.data(), sizeInByte, verbose);
	}

	void Read(const uint64_t& addr, DMABuffer& buffer, const bool& verbose = false)
	{
		Read(addr, buffer.data(), buffer.size(), verbose);
	}

	DMABuffer Read(const uint64_t& addr, const uint64_t& sizeInByte, const bool& verbose = false) __attribute__((warn_unused_result))
	{
		DMABuffer buffer = DMABuffer(sizeInByte, 0);
		Read(addr, buffer, sizeInByte, verbose);
		return buffer;
	}

	template<typename T>
	T Read(const uint64_t& addr, const bool& verbose = false)
	{
		const uint64_t size = static_cast<uint64_t>(sizeof(T));
		DMABuffer data      = Read(addr, size, verbose);
		T res;
		std::memcpy(&res, data.data(), size);
		return res;
	}

	template<typename T>
	void Read(const uint64_t& addr, T& buffer, const bool& verbose = false)
	{
		const uint64_t size = static_cast<uint64_t>(sizeof(T));
		DMABuffer data      = Read(addr, size, verbose);
		std::memcpy(&buffer, data.data(), size);
	}

	template<class T, class A = std::allocator<T>>
	void Read(const uint64_t& addr, std::vector<T, A>& data, const bool& verbose = false)
	{
		std::size_t size = sizeof(T);
		Read(addr, data.data(), data.size() * size, verbose);
	}

	uint8_t Read8(const uint64_t& addr)
	{
		return Read<uint8_t>(addr);
	}

	uint16_t Read16(const uint64_t& addr)
	{
		return Read<uint16_t>(addr);
	}

	uint32_t Read32(const uint64_t& addr)
	{
		return Read<uint32_t>(addr);
	}

	uint64_t Read64(const uint64_t& addr)
	{
		return Read<uint64_t>(addr);
	}

	uint8_t Read8(const Memory& mem)
	{
		return read<uint8_t>(mem);
	}

	uint16_t Read16(const Memory& mem)
	{
		return read<uint16_t>(mem);
	}

	uint32_t Read32(const Memory& mem)
	{
		return read<uint32_t>(mem);
	}

	uint64_t Read64(const Memory& mem)
	{
		return read<uint64_t>(mem);
	}

	////////////////////////////////////////////////////////////////////////////
	///                      Write Functions                                 ///
	////////////////////////////////////////////////////////////////////////////

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte, const bool& verbose = false)
	{
		m_pBackend->Write(addr, pData, sizeInByte, verbose);
	}

	void Write(const Memory& mem, const void* pData, const uint64_t& sizeInByte = USE_MEMORY_SIZE, const bool& verbose = false)
	{
		uint64_t size = (sizeInByte == USE_MEMORY_SIZE ? mem.GetSize() : sizeInByte);
		if (size > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMA") << m_pBackend->GetName(XDMABackend::TYPE::WRITE) << ", specified size (0x" << std::hex << size << ") exceeds size of the given buffer (0x" << std::hex << mem.GetSize() << ")";
			throw XDMAException(ss.str());
		}

		Write(mem.GetBaseAddr(), pData, size, verbose);
	}

	void Write(const uint64_t& addr, const DMABuffer& buffer, const uint64_t& sizeInByte, const bool& verbose = false)
	{
		if (sizeInByte > buffer.size())
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMA") << "Size of buffer provided (" << std::dec << buffer.size() << ") is smaller than the desired write size (" << sizeInByte << ")";
			throw XDMAException(ss.str());
		}

		Write(addr, buffer.data(), sizeInByte, verbose);
	}

	void Write(const uint64_t& addr, const DMABuffer& buffer, const bool& verbose = false)
	{
		Write(addr, buffer.data(), buffer.size(), verbose);
	}

	template<typename T>
	void Write(const uint64_t& addr, const T& data, const bool& verbose = false)
	{
		//  === Ugly Workaround ===
		// If a global const variable was passed as data argument
		// the write call fails, to circumvent this a local copy
		// is created and passed to the underlying function
		const T tmp = data;
		//  === Ugly Workaround ===
		Write(addr, reinterpret_cast<const void*>(&tmp), sizeof(T), verbose);
	}

	template<class T, class A = std::allocator<T>>
	void Write(const uint64_t& addr, const std::vector<T, A>& data, const bool& verbose = false)
	{
		Write(addr, data.data(), data.size() * sizeof(T), verbose);
	}

	void Write8(const uint64_t& addr, const uint8_t& data)
	{
		Write<uint8_t>(addr, data);
	}

	void Write16(const uint64_t& addr, const uint16_t& data)
	{
		Write<uint16_t>(addr, data);
	}

	void Write32(const uint64_t& addr, const uint32_t& data)
	{
		Write<uint32_t>(addr, data);
	}

	void Write64(const uint64_t& addr, const uint64_t& data)
	{
		Write<uint64_t>(addr, data);
	}

	void Write8(const Memory& mem, const uint8_t& data)
	{
		write<uint8_t>(mem, data);
	}

	void Write16(const Memory& mem, const uint16_t& data)
	{
		write<uint16_t>(mem, data);
	}

	void Write32(const Memory& mem, const uint32_t& data)
	{
		write<uint32_t>(mem, data);
	}

	void Write64(const Memory& mem, const uint64_t& data)
	{
		write<uint64_t>(mem, data);
	}

private:
	template<typename T>
	T read(const Memory& mem)
	{
		if (sizeof(T) > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMA") << "Size of provided memory (" << std::dec << mem.GetSize() << ") is smaller than the desired read size (" << sizeof(T) << ")";
			throw XDMAException(ss.str());
		}

		return Read<T>(mem.GetBaseAddr());
	}

	template<typename T>
	void write(const Memory& mem, const T& data)
	{
		if (sizeof(T) > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMA") << "Size of provided memory (" << std::dec << mem.GetSize() << ") is smaller than the desired write size (" << sizeof(T) << ")";
			throw XDMAException(ss.str());
		}

		return Write<T>(mem.GetBaseAddr(), data);
	}

private:
	XDMABackendShr m_pBackend;
	std::map<MemoryType, MemoryManagerVec> m_memories;
#ifndef EMBEDDED_XILINX
	std::mutex m_mutex;
#endif
};

// TODO: Add backend classes for Pio
class XDMAPio : virtual public XDMABase
{
public:
	XDMAPio(const uint32_t& deviceNum, const std::size_t& pioSize, const std::size_t& pioOffset = 0) :
		XDMABase(deviceNum),
		m_pioDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_user"),
		m_pioSize(pioSize),
		m_pioOffset(pioOffset),
		m_fd(-1),
		m_mapBase(nullptr),
		m_valid(false)
#ifndef EMBEDDED_XILINX
		,
		m_mutex()
#endif
	{
		// m_fd = OpenDevice(m_pioDeviceName);
		m_fd    = open(m_pioDeviceName.c_str(), O_RDWR | O_NONBLOCK);
		int err = errno;

		if (m_fd < 0)
		{
			std::stringstream ss;
			ss << CLASS_TAG("") << "Unable to open device " << m_pioDeviceName << "; errno: " << err;
			throw XDMAException(ss.str());
		}

#ifndef EMBEDDED_XILINX
		m_mapBase = mmap(0, m_pioSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, m_pioOffset);
		err       = errno;

		if (m_mapBase == MAP_FAILED)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Failed to map memory into userspace, errno: " << err;
			throw XDMAException(ss.str());
		}
#endif

		m_valid = true;
	}

	DISABLE_COPY_ASSIGN_MOVE(XDMAPio)

	~XDMAPio()
	{
#ifndef EMBEDDED_XILINX
		std::lock_guard<std::mutex> lock(m_mutex);

		munmap(m_mapBase, m_pioSize);
#endif
		close(m_fd);
	}

	uint8_t Read8(const uint64_t& addr)
	{
		return read<uint8_t>(addr);
	}

	uint16_t Read16(const uint64_t& addr)
	{
		return read<uint16_t>(addr);
	}

	uint32_t Read32(const uint64_t& addr)
	{
		return read<uint32_t>(addr);
	}

	uint64_t Read64(const uint64_t& addr)
	{
		return read<uint64_t>(addr);
	}

	void Write8(const uint64_t& addr, const uint8_t& data)
	{
		write<uint8_t>(addr, data);
	}

	void Write16(const uint64_t& addr, const uint16_t& data)
	{
		write<uint16_t>(addr, data);
	}

	void Write32(const uint64_t& addr, const uint32_t& data)
	{
		write<uint32_t>(addr, data);
	}

	void Write64(const uint64_t& addr, const uint64_t& data)
	{
		write<uint64_t>(addr, data);
	}

private:
	template<typename T>
	T read(const uint64_t& addr)
	{
#ifndef EMBEDDED_XILINX
		std::lock_guard<std::mutex> lock(m_mutex);
#endif

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "XDMAPio Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		const std::size_t size = sizeof(T);
		if (size > MAX_PIO_ACCESS_SIZE)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Type size (" << std::dec << size << " byte) exceeds maximal allowed Pio size (" << MAX_PIO_ACCESS_SIZE << " byte)";
			throw XDMAException(ss.str());
		}

		if (addr >= m_pioSize + m_pioOffset)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Address: (0x" << std::hex << addr << ") exceeds Pio address range (0x" << m_pioOffset << "-0x" << m_pioSize + m_pioOffset << ")";
			throw XDMAException(ss.str());
		}

		uint8_t* vAddr = reinterpret_cast<uint8_t*>(m_mapBase) + addr;
		T result       = *(reinterpret_cast<T*>(vAddr));
		return result;
	}

	template<typename T>
	void write(const uint64_t& addr, const T& data)
	{
#ifndef EMBEDDED_XILINX
		std::lock_guard<std::mutex> lock(m_mutex);
#endif

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "XDMAPio Instance is not valid, an error probably occurred during device initialization.";
			throw XDMAException(ss.str());
		}

		const std::size_t size = sizeof(T);
		if (size > MAX_PIO_ACCESS_SIZE)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Type size (" << std::dec << size << " byte) exceeds maximal allowed Pio size (" << MAX_PIO_ACCESS_SIZE << " byte)";
			throw XDMAException(ss.str());
		}

		if (addr >= m_pioSize + m_pioOffset)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Address (0x" << std::hex << addr << ") exceeds Pio address range (0x" << m_pioOffset << "-0x" << m_pioSize + m_pioOffset << ")";
			throw XDMAException(ss.str());
		}

		uint8_t* vAddr                 = reinterpret_cast<uint8_t*>(m_mapBase) + addr;
		*(reinterpret_cast<T*>(vAddr)) = data;
	}

private:
	std::string m_pioDeviceName;
	std::size_t m_pioSize;
	std::size_t m_pioOffset;
	int m_fd;
	void* m_mapBase;
	bool m_valid;
#ifndef EMBEDDED_XILINX
	std::mutex m_mutex;
#endif

	static const std::size_t MAX_PIO_ACCESS_SIZE = sizeof(uint64_t);
};

inline XDMAManaged::XDMAManaged(XDMABase* pXdma) :
	m_pXdma(pXdma)
{
	if (m_pXdma)
		m_pXdma->registerObject(this);
}

inline XDMAManaged::~XDMAManaged()
{
	if (m_pXdma)
		m_pXdma->unregisterObject(this);
}
