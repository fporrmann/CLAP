/*
 *  File: CLAP.hpp
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

////// TODO:
// - Maybe change the way IP core objects are created, possible to create from an XDMA object?
// --------------------------------------------------------------------------------------------
// - Look into 32-bit AXI interfaces, although this is disabled by default it might still be used
//   - Would require some edits to the memory manager
//   - Would require that the DDR address is below 4GB
// --------------------------------------------------------------------------------------------
// - Try to force the use of aligned memory, e.g., by using the CLAPBuffer type, alternative force vector types to be aligned with the AlignmentAllocator
//   - Maybe prevent passing for custom memory addresses altogether
// --------------------------------------------------------------------------------------------
// - Replace boolean flags with enums for better readability
// --------------------------------------------------------------------------------------------
// - Redesign some of the methods to remove the need for explicit casts
// --------------------------------------------------------------------------------------------
// - Replace pointers, e.g., in XDMAManaged with smart pointers
// --------------------------------------------------------------------------------------------
// - Detect if the control address of an IP Core is within the range of a memory block / other IP Core
// --------------------------------------------------------------------------------------------
// - Write examples using AxiDMA and VDMA
// --------------------------------------------------------------------------------------------
// - Address of memory can differ between internal and external
// --------------------------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////
// Includes for open()
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
/////////////////////////

#ifndef EMBEDDED_XILINX
#ifndef _WIN32
/////////////////////////
// Include for mmap(), munmap()
#include <sys/mman.h>
/////////////////////////
#endif
#endif

#include <algorithm>
#include <cstring> // required for std::memcpy
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

#include "internal/Backends.hpp"
#include "internal/CLAPBackend.hpp"
#include "internal/Constants.hpp"
#include "internal/Exceptions.hpp"
#include "internal/Expected.hpp"
#include "internal/Memory.hpp"
#include "internal/SoloRunWarden.hpp"
#include "internal/Types.hpp"
#include "internal/Utils.hpp"

#ifndef EMBEDDED_XILINX
#include "internal/AlignmentAllocator.hpp"
#include "internal/Timer.hpp"
#endif

namespace clap
{
namespace internal
{
class CLAPManaged
{
	friend class CLAPBase;
	DISABLE_COPY_ASSIGN_MOVE(CLAPManaged)

protected:
	CLAPManaged(internal::CLAPBasePtr pClap);

	virtual ~CLAPManaged();

	internal::CLAPBasePtr CLAP()
	{
		checkCLAPValid();
		return m_pClap;
	}

	const internal::CLAPBasePtr& CLAP() const
	{
		checkCLAPValid();
		return m_pClap;
	}

private:
	void markCLAPInvalid()
	{
		m_pClap = nullptr;
	}

	void checkCLAPValid() const
	{
		if (m_pClap == nullptr)
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAPManaged") << "CLAP/CLAPPio instance is not valid or has been destroyed";
			throw CLAPException(ss.str());
		}
	}

private:
	internal::CLAPBasePtr m_pClap;
};

class CLAPBase
{
	friend class CLAPManaged;

public:
	virtual uint8_t Read8(const uint64_t& addr)   = 0;
	virtual uint16_t Read16(const uint64_t& addr) = 0;
	virtual uint32_t Read32(const uint64_t& addr) = 0;
	virtual uint64_t Read64(const uint64_t& addr) = 0;

	virtual void Write8(const uint64_t& addr, const uint8_t& data)   = 0;
	virtual void Write16(const uint64_t& addr, const uint16_t& data) = 0;
	virtual void Write32(const uint64_t& addr, const uint32_t& data) = 0;
	virtual void Write64(const uint64_t& addr, const uint64_t& data) = 0;

	virtual Expected<uint64_t> ReadUIOProperty(const uint64_t& addr, const std::string& propName) const                 = 0;
	virtual Expected<std::string> ReadUIOStringProperty(const uint64_t& addr, const std::string& propName) const        = 0;
	virtual Expected<std::vector<uint64_t>> ReadUIOPropertyVec(const uint64_t& addr, const std::string& propName) const = 0;
	virtual Expected<int32_t> GetUIOID(const uint64_t& addr) const                                                      = 0;

	virtual void AddPollAddress(const uint64_t& addr) = 0;

	uint32_t GetDevNum() const
	{
		return m_devNum;
	}

protected:
	CLAPBase(const uint32_t& devNum) :
		m_devNum(devNum),
		m_managedObjects()
	{
	}

	virtual ~CLAPBase()
	{
		for (CLAPManaged* pM : m_managedObjects)
			pM->markCLAPInvalid();
	}

private:
	void registerObject(CLAPManaged* pObj)
	{
		m_managedObjects.push_back(pObj);
	}

	void unregisterObject(CLAPManaged* pObj)
	{
		m_managedObjects.erase(std::remove(m_managedObjects.begin(), m_managedObjects.end(), pObj), m_managedObjects.end());
	}

protected:
	uint32_t m_devNum;
	std::vector<CLAPManaged*> m_managedObjects;
};
} // namespace internal

class CLAP : virtual public internal::CLAPBase
{
public:
	enum class MemoryType
	{
		DDR,
		BRAM
	};

	struct MemoryRegion
	{
		MemoryType type;
		uint64_t baseAddress;
		uint64_t size;
	};

	using MemoryRegions = std::vector<MemoryRegion>;

private:
	using MemoryPair = std::pair<MemoryType, internal::MemoryManagerVec>;

	class XDMAInfo
	{
	public:
		XDMAInfo() = default;

		XDMAInfo(const uint32_t& reg0x0, const uint32_t& reg0x4)
		{
			m_channelID = (reg0x0 >> 8) & 0xF;
			m_version   = (reg0x0 >> 0) & 0xF;
			m_streaming = (reg0x0 >> 15) & 0x1;
			m_polling   = (reg0x4 >> 26) & 0x1;
		}

		const bool& IsStreaming() const
		{
			return m_streaming;
		}

		friend std::ostream& operator<<(std::ostream& os, const XDMAInfo& info)
		{
			os << "Channel ID: " << static_cast<uint32_t>(info.m_channelID) << std::endl;
			os << "Version: " << static_cast<uint32_t>(info.m_version) << std::endl;
			os << "Streaming: " << (info.m_streaming ? "true" : "false") << std::endl;
			os << "Polling: " << (info.m_polling ? "true" : "false");
			return os;
		}

	private:
		uint8_t m_channelID = 0;
		uint8_t m_version   = 0;
		bool m_streaming    = false;
		bool m_polling      = false;
	};

	CLAP(internal::CLAPBackendPtr pBackend) :
		CLAPBase(pBackend->GetDevNum()),
		m_pBackend(std::move(pBackend)),
		m_memories()
#ifndef EMBEDDED_XILINX
		,
		m_mutex()
#endif
	{
		internal::SoloRunWarden::GetInstance();
		m_memories.insert(MemoryPair(MemoryType::DDR, internal::MemoryManagerVec()));
		m_memories.insert(MemoryPair(MemoryType::BRAM, internal::MemoryManagerVec()));

		readInfo();

		LOG_VERBOSE << "CLAP instance created" << std::endl;
		LOG_VERBOSE << "Device number: " << m_devNum << std::endl;
		LOG_VERBOSE << "Backend: " << m_pBackend->GetBackendName() << std::endl;
		LOG_VERBOSE << m_info << std::endl;
	}

public:
	/// @brief Creates a new CLAP instance
	/// @tparam T Type of the backend to use
	/// @return A shared pointer to the new CLAP instance
	/// @param deviceNum Device number of the CLAP device
	/// @param channelNum Channel number of the CLAP device
	template<typename T>
	static CLAPPtr Create(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0)
	{
		// We have to use the result of new here, because the constructor is private
		// and can therefore, not be called from make_shared
		return CLAPPtr(new CLAP(std::make_shared<T>(deviceNum, channelNum)));
	}

	~CLAP()
	{
	}

	internal::UserInterruptPtr MakeUserInterrupt()
	{
		return m_pBackend->MakeUserInterrupt();
	}

	void AddPollAddress(const uint64_t& addr)
	{
		m_pBackend->AddPollAddr(addr);
	}

	/// @brief Adds a memory region to the CLAP instance
	/// @param type Type of memory
	/// @param baseAddr Base address of the memory region
	/// @param size Size of the memory region in bytes
	void AddMemoryRegion(const MemoryType& type, const uint64_t& baseAddr, const uint64_t& size)
	{
		m_memories[type].push_back(std::make_shared<internal::MemoryManager>(baseAddr, size));
	}

	void AddMemoryRegion(const MemoryRegion& region)
	{
		AddMemoryRegion(region.type, region.baseAddress, region.size);
	}

	/// @brief Allocates a memory block of the specified size and type
	/// @param type	Type of memory to allocate
	/// @param byteSize	Size of the memory block in bytes
	/// @param memIdx Index of the memory region to allocate from
	/// @return Allocated memory block
	Memory AllocMemory(const MemoryType& type, const uint64_t& byteSize, const int32_t& memIdx = -1)
	{
#ifndef EMBEDDED_XILINX
		std::lock_guard<std::mutex> lock(m_mutex);
#endif

		if (memIdx == -1)
		{
			for (internal::MemoryManagerPtr& mem : m_memories[type])
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
				ss << CLASS_TAG("CLAP") << "Specified memory region " << std::dec << memIdx << " does not exist.";
				throw CLAPException(ss.str());
			}

			return m_memories[type][memIdx]->AllocMemory(byteSize);
		}

		std::stringstream ss;
		ss << CLASS_TAG("CLAP") << "No memory region found with enough space left to allocate " << std::dec << byteSize << " byte.";
		throw CLAPException(ss.str());
	}

	/// @brief Allocates a memory block for n-elements
	/// @param type Type of memory to allocate
	/// @param elements Number of elements to allocate
	/// @param sizeOfElement Size of one element in bytes
	/// @param memIdx Index of the memory region to allocate from
	/// @return Allocated memory block
	Memory AllocMemory(const MemoryType& type, const uint64_t& elements, const std::size_t& sizeOfElement, const int32_t& memIdx = -1)
	{
		return AllocMemory(type, elements * sizeOfElement, memIdx);
	}

	/// @brief Allocates a DDR memory block for n-elements
	/// @param elements Number of elements to allocate
	/// @param sizeOfElement Size of one element in bytes
	/// @param memIdx Index of the memory region to allocate from
	/// @return Allocated DDR memory block
	Memory AllocMemoryDDR(const uint64_t& elements, const std::size_t& sizeOfElement, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::DDR, elements, sizeOfElement, memIdx);
	}

	/// @brief Allocates a BRAM memory block for n-elements
	/// @param elements Number of elements to allocate
	/// @param sizeOfElement Size of one element in bytes
	/// @param memIdx Index of the memory region to allocate from
	/// @return Allocated BRAM memory block
	Memory AllocMemoryBRAM(const uint64_t& elements, const std::size_t& sizeOfElement, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::BRAM, elements, sizeOfElement, memIdx);
	}

	/// @brief Allocates a DDR memory block of the specified byte size
	/// @param byteSize Size of the memory block in bytes
	/// @param memIdx Index of the memory region to allocate from
	/// @return Allocated DDR memory block
	Memory AllocMemoryDDR(const uint64_t& byteSize, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::DDR, byteSize, memIdx);
	}

	/// @brief Allocates a BRAM memory block of the specified byte size
	/// @param byteSize Size of the memory block in bytes
	/// @param memIdx Index of the memory region to allocate from
	/// @return Allocated BRAM memory block
	Memory AllocMemoryBRAM(const uint64_t& byteSize, const int32_t& memIdx = -1)
	{
		return AllocMemory(MemoryType::BRAM, byteSize, memIdx);
	}

	/// @brief Frees the specified memory block
	/// @param mem Memory block to free
	void FreeMemory(Memory& mem)
	{
		for (auto& [type, memories] : m_memories)
		{
			for (auto& memManager : memories)
			{
				if (memManager->FreeMemory(mem))
					return;
			}
		}
	}

	/// @brief Resets the specified memory region
	/// @param type Type of memory
	/// @param memIdx Index of the memory region to reset
	void ResetMemory(const MemoryType& type, const int32_t& memIdx = -1)
	{
		if (memIdx == -1)
		{
			for (auto& memManager : m_memories[type])
				memManager->Reset();
		}
		else
		{
			if (m_memories[type].size() <= static_cast<uint32_t>(memIdx))
			{
				std::stringstream ss;
				ss << CLASS_TAG("CLAP") << "Specified memory region " << std::dec << memIdx << " does not exist.";
				throw CLAPException(ss.str());
			}

			m_memories[type][memIdx]->Reset();
		}
	}

	/// @brief Resets all memory regions
	void ResetMemory()
	{
		for (auto& [type, memories] : m_memories)
		{
			for (auto& memManager : memories)
				memManager->Reset();
		}
	}

	////////////////////////////////////////////////////////////////////////////
	///                      Read Methods                                    ///
	////////////////////////////////////////////////////////////////////////////

	/// @brief Reads data from the specified address into a data buffer
	/// @param addr Address to read from
	/// @param pData Pointer to the data buffer
	/// @param sizeInByte Size of the data buffer in bytes
	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		m_pBackend->Read(addr, pData, sizeInByte);
	}

	/// @brief Reads data from the specified memory object into a data buffer
	/// @param mem Memory object to read from
	/// @param pData Pointer to the data buffer
	/// @param sizeInByte Size of the data buffer in bytes
	void Read(const Memory& mem, void* pData, const uint64_t& sizeInByte = USE_MEMORY_SIZE)
	{
		uint64_t size = (sizeInByte == USE_MEMORY_SIZE ? mem.GetSize() : sizeInByte);
		if (size > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << m_pBackend->GetName(internal::CLAPBackend::TYPE::READ) << ", specified size (0x" << std::hex << size << ") exceeds size of the given memory (0x" << std::hex << mem.GetSize() << ")";
			throw CLAPException(ss.str());
		}

		Read(mem.GetBaseAddr(), pData, size);
	}

	/// @brief Reads data from the given memory object into the given XDMA buffer
	/// @tparam T Type of the data to read
	/// @param mem Memory object to read from
	/// @param buffer XDMA buffer to read into
	/// @param sizeInByte Size of the XDMA buffer in bytes
	template<typename T>
	void Read(const Memory& mem, CLAPBuffer<T>& buffer, const uint64_t& sizeInByte = USE_MEMORY_SIZE)
	{
		uint64_t size = (sizeInByte == USE_MEMORY_SIZE ? mem.GetSize() : sizeInByte);
		if (size > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << m_pBackend->GetName(internal::CLAPBackend::TYPE::READ) << ", specified size (0x" << std::hex << size << ") exceeds size of the given memory (0x" << std::hex << mem.GetSize() << ")";
			throw CLAPException(ss.str());
		}

		if (size > (buffer.size() * sizeof(T)))
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Byte size of buffer provided (" << std::dec << buffer.size() * sizeof(T) << ") is smaller than the desired read size (" << size << ")";
			throw CLAPException(ss.str());
		}

		Read(mem, buffer.data(), buffer.size() * sizeof(T));
	}

	/// @brief Reads data from the specified address into the given XDMA buffer
	/// @tparam T Type of the data to read
	/// @param addr Address to read from
	/// @param buffer XDMA buffer to read into
	/// @param sizeInByte Size of the XDMA buffer in bytes
	template<typename T>
	void Read(const uint64_t& addr, CLAPBuffer<T>& buffer, const uint64_t& sizeInByte)
	{
		if (sizeInByte > (buffer.size() * sizeof(T)))
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Byte size of buffer provided (" << std::dec << buffer.size() * sizeof(T) << ") is smaller than the desired read size (" << sizeInByte << ")";
			throw CLAPException(ss.str());
		}

		Read(addr, buffer.data(), sizeInByte);
	}

	/// @brief Reads data from the specified address into the given XDMA buffer
	/// @tparam T Type of the data to read
	/// @param addr Address to read from
	/// @param buffer XDMA buffer to read into
	template<typename T>
	void Read(const uint64_t& addr, CLAPBuffer<T>& buffer)
	{
		Read(addr, buffer.data(), buffer.size() * sizeof(T));
	}

	/// @brief Reads data from the specified address and returns it as an XDMA buffer
	/// @tparam T Type of the data to read
	/// @param addr Address to read from
	/// @param sizeInByte Size of the data buffer in bytes
	/// @return XDMA buffer containing the read data
	template<typename T>
	CLAPBuffer<T> CHECK_RESULT Read(const uint64_t& addr, const uint32_t& sizeInByte)
	{
		CLAPBuffer<T> buffer = CLAPBuffer<T>(ROUND_UP_DIV(sizeInByte, sizeof(T)), 0);
		Read<T>(addr, buffer, sizeInByte);
		return buffer;
	}

	/// @brief Reads data from the specified address and returns it as an object of the template type T
	/// @tparam T Type of the object into which the data will be read
	/// @param addr Address to read from
	/// @return Object of type T containing the read data
	template<typename T>
	T Read(const uint64_t& addr)
	{
		const uint32_t size = static_cast<uint32_t>(sizeof(T));

		CLAPBuffer<uint8_t> data = Read<uint8_t>(addr, size);
		T res;
		std::memcpy(&res, data.data(), size);
		return res;
	}

	/// @brief Reads a datum of type T from the specified address and writes it into the given buffer
	/// @tparam T Type of the object into which the datum will be read
	/// @param addr Address to read from
	/// @param buffer Object into which the datum will be read
	template<typename T>
	void Read(const uint64_t& addr, T& buffer)
	{
		const uint32_t size      = static_cast<uint32_t>(sizeof(T));
		CLAPBuffer<uint8_t> data = Read<uint8_t>(addr, size);
		std::memcpy(&buffer, data.data(), size);
	}

	/// @brief Reads data from the specified address and writes it into the given vector
	/// @tparam T Type of the vector into which the data will be read
	/// @tparam A The allocator used for the vector
	/// @param addr Address to read from
	/// @param data Vector into which the data will be read
	template<class T, class A = clap::internal::AlignmentAllocator<T, ALIGNMENT>>
	void Read(const uint64_t& addr, std::vector<T, A>& data)
	{
		std::size_t size = sizeof(T);
		Read(addr, data.data(), data.size() * size);
	}

	/// @brief Reads a single unsigned byte from the specified address
	/// @param addr Address to read from
	/// @return Unsigned byte read from the specified address
	uint8_t Read8(const uint64_t& addr)
	{
		return Read<uint8_t>(addr);
	}

	/// @brief Reads a single unsigned word from the specified address
	/// @param addr Address to read from
	/// @return Unsigned word read from the specified address
	uint16_t Read16(const uint64_t& addr)
	{
		return Read<uint16_t>(addr);
	}

	/// @brief Reads a single unsigned double word from the specified address
	/// @param addr Address to read from
	/// @return Unsigned double word read from the specified address
	uint32_t Read32(const uint64_t& addr)
	{
		return Read<uint32_t>(addr);
	}

	/// @brief Reads a single unsigned quad word from the specified address
	/// @param addr Address to read from
	/// @return Unsigned quad word read from the specified address
	uint64_t Read64(const uint64_t& addr)
	{
		return Read<uint64_t>(addr);
	}

	/// @brief Reads a single unsigned byte from the specified memory object
	/// @param mem Memory object to read from
	/// @return Unsigned byte read from the specified memory object
	uint8_t Read8(const Memory& mem)
	{
		return read<uint8_t>(mem);
	}

	/// @brief Reads a single unsigned word from the specified memory object
	/// @param mem Memory object to read from
	/// @return Unsigned word read from the specified memory object
	uint16_t Read16(const Memory& mem)
	{
		return read<uint16_t>(mem);
	}

	/// @brief Reads a single unsigned double word from the specified memory object
	/// @param mem Memory object to read from
	/// @return Unsigned double word read from the specified memory object
	uint32_t Read32(const Memory& mem)
	{
		return read<uint32_t>(mem);
	}

	/// @brief Reads a single unsigned quad word from the specified memory object
	/// @param mem Memory object to read from
	/// @return Unsigned quad word read from the specified memory object
	uint64_t Read64(const Memory& mem)
	{
		return read<uint64_t>(mem);
	}

	////////////////////////////////////////////////////////////////////////////
	///                      Write Methods                                   ///
	////////////////////////////////////////////////////////////////////////////

	/// @brief Writes data to the specified address
	/// @param addr Address to write to
	/// @param pData Pointer to the data buffer
	/// @param sizeInByte Size of the data buffer in bytes
	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte)
	{
		m_pBackend->Write(addr, pData, sizeInByte);
	}

	/// @brief Writes data to the specified memory object
	/// @param mem Memory object to write to
	/// @param pData Pointer to the data buffer
	/// @param sizeInByte Size of the data buffer in bytes
	void Write(const Memory& mem, const void* pData, const uint64_t& sizeInByte = USE_MEMORY_SIZE)
	{
		uint64_t size = (sizeInByte == USE_MEMORY_SIZE ? mem.GetSize() : sizeInByte);
		if (size > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << m_pBackend->GetName(internal::CLAPBackend::TYPE::WRITE) << ", specified size (0x" << std::hex << size << ") exceeds size of the given memory (0x" << std::hex << mem.GetSize() << ")";
			throw CLAPException(ss.str());
		}

		Write(mem.GetBaseAddr(), pData, size);
	}

	/// @brief Writes data to the specified memory object
	/// @tparam T Type of the data to write
	/// @param mem Memory object to write to
	/// @param buffer XDMA Buffer containing the data to write
	/// @param sizeInByte Size of the data buffer in bytes
	template<typename T>
	void Write(const Memory& mem, const CLAPBuffer<T>& buffer, const uint64_t& sizeInByte = USE_MEMORY_SIZE)
	{
		uint64_t size = (sizeInByte == USE_MEMORY_SIZE ? mem.GetSize() : sizeInByte);
		if (size > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << m_pBackend->GetName(internal::CLAPBackend::TYPE::WRITE) << ", specified size (0x" << std::hex << size << ") exceeds size of the given memory (0x" << std::hex << mem.GetSize() << ")";
			throw CLAPException(ss.str());
		}

		if (size > (buffer.size() * sizeof(T)))
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Byte size of buffer provided (" << buffer.size() * sizeof(T) << ") is smaller than the desired write size (" << size << ")";
			throw CLAPException(ss.str());
		}

		Write(mem, buffer.data(), size);
	}

	/// @brief Writes data to the specified address
	/// @tparam T Type of the data to write
	/// @param addr Address to write to
	/// @param buffer XDMA Buffer containing the data to write
	/// @param sizeInByte Size of the data buffer in bytes
	template<typename T>
	void Write(const uint64_t& addr, const CLAPBuffer<T>& buffer, const uint64_t& sizeInByte)
	{
		if (sizeInByte > (buffer.size() * sizeof(T)))
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Byte size of buffer provided (" << std::dec << buffer.size() * sizeof(T) << ") is smaller than the desired write size (" << sizeInByte << ")";
			throw CLAPException(ss.str());
		}

		Write(addr, buffer.data(), sizeInByte);
	}

	/// @brief Writes data to the specified address
	/// @tparam T Type of the data to write
	/// @param addr Address to write to
	/// @param buffer XDMA Buffer containing the data to write
	template<typename T>
	void Write(const uint64_t& addr, const CLAPBuffer<T>& buffer)
	{
		Write(addr, buffer.data(), buffer.size() * sizeof(T));
	}

	/// @brief Writes a datum of type T to the specified address
	/// @tparam T Type of the datum to write
	/// @param addr Address to write to
	/// @param data Datum to write to the specified address
	template<typename T>
	void Write(const uint64_t& addr, const T& data)
	{
		// Create a temporary CLAPBuffer containing the data, in order to properly align the data
		const CLAPBuffer<T> tmp = CLAPBuffer<T>(1, data);
		Write<T>(addr, tmp);
	}

	/// @brief Writes data from a vector to the specified address
	/// @tparam T Type of the vector from which the data will be written
	/// @tparam A The allocator used for the vector
	/// @param addr Address to write to
	/// @param data Vector containing the data to write to the specified address
	template<class T, class A = clap::internal::AlignmentAllocator<T, ALIGNMENT>>
	void Write(const uint64_t& addr, const std::vector<T, A>& data)
	{
		Write(addr, data.data(), data.size() * sizeof(T));
	}

	/// @brief Writes a single unsigned byte to the specified address
	/// @param addr Address to write to
	/// @param data Unsigned byte to write to the specified address
	void Write8(const uint64_t& addr, const uint8_t& data)
	{
		Write<uint8_t>(addr, data);
	}

	/// @brief Writes a single unsigned word to the specified address
	/// @param addr Address to write to
	/// @param data Unsigned word to write to the specified address
	void Write16(const uint64_t& addr, const uint16_t& data)
	{
		Write<uint16_t>(addr, data);
	}

	/// @brief Writes a single unsigned double word to the specified address
	/// @param addr Address to write to
	/// @param data Unsigned double word to write to the specified address
	void Write32(const uint64_t& addr, const uint32_t& data)
	{
		Write<uint32_t>(addr, data);
	}

	/// @brief Writes a single unsigned quad word to the specified address
	/// @param addr Address to write to
	/// @param data Unsigned quad word to write to the specified address
	void Write64(const uint64_t& addr, const uint64_t& data)
	{
		Write<uint64_t>(addr, data);
	}

	/// @brief Writes a single unsigned byte to the specified memory object
	/// @param mem Memory object to write to
	/// @param data Unsigned byte to write to the specified memory object
	void Write8(const Memory& mem, const uint8_t& data)
	{
		write<uint8_t>(mem, data);
	}

	/// @brief Writes a single unsigned word to the specified memory object
	/// @param mem Memory object to write to
	/// @param data Unsigned word to write to the specified memory object
	void Write16(const Memory& mem, const uint16_t& data)
	{
		write<uint16_t>(mem, data);
	}

	/// @brief Writes a single unsigned double word to the specified memory object
	/// @param mem Memory object to write to
	/// @param data Unsigned double word to write to the specified memory object
	void Write32(const Memory& mem, const uint32_t& data)
	{
		write<uint32_t>(mem, data);
	}

	/// @brief Writes a single unsigned quad word to the specified memory object
	/// @param mem Memory object to write to
	/// @param data Unsigned quad word to write to the specified memory object
	void Write64(const Memory& mem, const uint64_t& data)
	{
		write<uint64_t>(mem, data);
	}

	////////////////////////////////////////////////////////////////////////////
	///                      UIO Property Methods                            ///
	////////////////////////////////////////////////////////////////////////////

	Expected<uint64_t> ReadUIOProperty(const uint64_t& addr, const std::string& propName) const
	{
		return m_pBackend->ReadUIOProperty(addr, propName);
	}

	Expected<std::string> ReadUIOStringProperty(const uint64_t& addr, const std::string& propName) const
	{
		return m_pBackend->ReadUIOStringProperty(addr, propName);
	}

	Expected<std::vector<uint64_t>> ReadUIOPropertyVec(const uint64_t& addr, const std::string& propName) const
	{
		return m_pBackend->ReadUIOPropertyVec(addr, propName);
	}

	Expected<int32_t> GetUIOID(const uint64_t& addr) const
	{
		return m_pBackend->GetUIOID(addr);
	}

	////////////////////////////////////////////////////////////////////////////
	///                      Streaming Methods                               ///
	////////////////////////////////////////////////////////////////////////////

	/// @brief Starts a streaming read, reading sizeInByte bytes into the specified XDMA buffer
	/// @tparam T Type of the XDMA buffer into which the data will be read
	/// @param buffer XDMA buffer to read into
	/// @param sizeInByte Number of bytes to read
	template<typename T>
	void StartReadStream(CLAPBuffer<T>& buffer, const uint64_t& sizeInByte = USE_VECTOR_SIZE)
	{
		uint64_t size = (sizeInByte == USE_VECTOR_SIZE ? buffer.size() * sizeof(T) : sizeInByte);
		startReadStream(buffer.data(), size);
	}

	/// @brief Starts a streaming read, reading sizeInByte bytes into the specified vector
	/// @tparam T Type of the vector into which the data will be read
	/// @tparam A The allocator used for the vector
	/// @param buffer Vector to read into
	/// @param sizeInByte Number of bytes to read
	template<class T, class A = clap::internal::AlignmentAllocator<T, ALIGNMENT>>
	void StartReadStream(std::vector<T, A>& buffer, const uint64_t& sizeInByte = USE_VECTOR_SIZE)
	{
		uint64_t size = (sizeInByte == USE_VECTOR_SIZE ? buffer.size() * sizeof(T) : sizeInByte);
		startReadStream(buffer.data(), size);
	}

	/// @brief Starts a streaming write, writing sizeInByte bytes from the specified XDMA buffer
	/// @tparam T Type of the XDMA buffer from which the data will be written
	/// @param buffer XDMA buffer containing the data to write
	/// @param sizeInByte Number of bytes to write
	template<typename T>
	void StartWriteStream(const CLAPBuffer<T>& buffer, const uint64_t& sizeInByte = USE_VECTOR_SIZE)
	{
		uint64_t size = (sizeInByte == USE_VECTOR_SIZE ? buffer.size() * sizeof(T) : sizeInByte);
		startWriteStream(buffer.data(), size);
	}

	/// @brief Starts a streaming write, writing sizeInByte bytes from the specified vector
	/// @tparam T Type of the vector from which the data will be written
	/// @tparam A The allocator used for the vector
	/// @param buffer Vector containing the data to write
	/// @param sizeInByte Number of bytes to write
	template<class T, class A = clap::internal::AlignmentAllocator<T, ALIGNMENT>>
	void StartWriteStream(const std::vector<T, A>& buffer, const uint64_t& sizeInByte = USE_VECTOR_SIZE)
	{
		uint64_t size = (sizeInByte == USE_VECTOR_SIZE ? buffer.size() * sizeof(T) : sizeInByte);
		startWriteStream(buffer.data(), size);
	}

	/// @brief Waits for the read stream operation to finish
	void WaitForReadStream()
	{
		if (m_readFuture.valid())
		{
			m_readFuture.wait();
			m_readFuture.get();
		}
	}

	/// @brief Waits for the write stream operation to finish
	void WaitForWriteStream()
	{
		if (m_writeFuture.valid())
		{
			m_writeFuture.wait();
			m_writeFuture.get();
		}
	}

	/// @brief Waits for the read and write stream operations to finish
	void WaitForStreams()
	{
		WaitForReadStream();
		WaitForWriteStream();
	}

	/// @brief Returns the runtime of the last read stream operation in milliseconds
	/// @return Runtime in milliseconds
	double GetReadStreamRuntime() const
	{
#ifndef EMBEDDED_XILINX
		return m_readStreamTimer.GetElapsedTimeInMilliSec();
#else
		return 0.0; // TODO: Add embedded runtime measurement
#endif
	}

	/// @brief Returns the runtime of the last write stream operation in milliseconds
	/// @return Runtime in milliseconds
	double GetWriteStreamRuntime() const
	{
#ifndef EMBEDDED_XILINX
		return m_writeStreamTimer.GetElapsedTimeInMilliSec();
#else
		return 0.0; // TODO: Add embedded runtime measurement
#endif
	}

private:
	template<typename T>
	T read(const Memory& mem)
	{
		if (sizeof(T) > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Size of provided memory (" << std::dec << mem.GetSize() << ") is smaller than the desired read size (" << sizeof(T) << ")";
			throw CLAPException(ss.str());
		}

		return Read<T>(mem.GetBaseAddr());
	}

	template<typename T>
	void write(const Memory& mem, const T& data)
	{
		if (sizeof(T) > mem.GetSize())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Size of provided memory (" << std::dec << mem.GetSize() << ") is smaller than the desired write size (" << sizeof(T) << ")";
			throw CLAPException(ss.str());
		}

		return Write<T>(mem.GetBaseAddr(), data);
	}

	void startReadStream(void* pData, const uint64_t& sizeInByte)
	{
		if (!m_info.IsStreaming())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "The XDMA endpoint is not in streaming mode";
			throw CLAPException(ss.str());
		}

		if (m_readFuture.valid())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Read stream is already running";
			throw CLAPException(ss.str());
		}

		m_readFuture = std::async(&CLAP::readStream, this, pData, sizeInByte);
	}

	void startWriteStream(const void* pData, const uint64_t& sizeInByte)
	{
		if (!m_info.IsStreaming())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "The XDMA endpoint is not in streaming mode";
			throw CLAPException(ss.str());
		}

		if (m_writeFuture.valid())
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Write stream is already running";
			throw CLAPException(ss.str());
		}

		m_writeFuture = std::async(&CLAP::writeStream, this, pData, sizeInByte);
	}

	void writeStream(const void* pData, const uint64_t& size)
	{
		// Due to the AXI data width of the XDMA write size has to be a multiple of 512-Bit (64-Byte)
		if (size % internal::XDMA_AXI_DATA_WIDTH != 0)
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Size (" << size << ") is not a multiple of the XDMA AXI data width (" << internal::XDMA_AXI_DATA_WIDTH << ").";
			throw CLAPException(ss.str());
		}

		uint64_t curSize = 0;
		m_writeStreamTimer.Start();

		while (curSize < size)
		{
			uint64_t writeSize = std::min(size - curSize, static_cast<uint64_t>(ALIGNMENT));
			Write(internal::XDMA_STREAM_OFFSET, reinterpret_cast<const uint8_t*>(pData) + curSize, writeSize);
			curSize += writeSize;
		}

		m_writeStreamTimer.Stop();
	}

	void readStream(void* pData, const uint64_t& size)
	{
		// Due to the AXI data width of the XDMA read size has to be a multiple of 512-Bit (64-Byte)
		if (size % internal::XDMA_AXI_DATA_WIDTH != 0)
		{
			std::stringstream ss;
			ss << CLASS_TAG("CLAP") << "Size (" << size << ") is not a multiple of the XDMA AXI data width (" << internal::XDMA_AXI_DATA_WIDTH << ").";
			throw CLAPException(ss.str());
		}

		uint64_t curSize = 0;
		m_readStreamTimer.Start();

		while (curSize < size)
		{
			uint64_t readSize = std::min(size - curSize, static_cast<uint64_t>(ALIGNMENT));
			Read(internal::XDMA_STREAM_OFFSET, reinterpret_cast<uint8_t*>(pData) + curSize, readSize);
			curSize += readSize;
		}

		m_readStreamTimer.Stop();
	}

	// --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	uint8_t readCtrl8(const uint64_t& addr)
	{
		return readCtrl<uint8_t>(addr);
	}

	uint16_t readCtrl16(const uint64_t& addr)
	{
		return readCtrl<uint16_t>(addr);
	}

	uint32_t readCtrl32(const uint64_t& addr)
	{
		return readCtrl<uint32_t>(addr);
	}

	uint64_t readCtrl64(const uint64_t& addr)
	{
		return readCtrl<uint64_t>(addr);
	}

	template<typename T>
	T readCtrl(const uint64_t& addr)
	{
		uint64_t tmp;
		m_pBackend->ReadCtrl(addr, tmp, sizeof(T));
		return static_cast<T>(tmp);
	}

	void readInfo()
	{
		uint32_t reg0 = readCtrl32(internal::XDMA_CTRL_BASE + m_devNum * internal::XDMA_CTRL_SIZE + 0x0);
		uint32_t reg4 = readCtrl32(internal::XDMA_CTRL_BASE + m_devNum * internal::XDMA_CTRL_SIZE + 0x4);
		m_info        = XDMAInfo(reg0, reg4);
	}
	// --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

private:
	internal::CLAPBackendPtr m_pBackend;
	std::map<MemoryType, internal::MemoryManagerVec> m_memories;
	std::future<void> m_readFuture  = {};
	std::future<void> m_writeFuture = {};
	Timer m_readStreamTimer         = {};
	Timer m_writeStreamTimer        = {};

	XDMAInfo m_info = {};

#ifndef EMBEDDED_XILINX
	std::mutex m_mutex;
#endif
};

#ifndef _WIN32
// TODO: Add backend classes for Pio
// NOTE: PIO is used for the AXI-Lite interface of the XDMA -- But currently not fully supported by this API
class XDMAPio : virtual public internal::CLAPBase
{
public:
	XDMAPio(const uint32_t& deviceNum, const std::size_t& pioSize, const std::size_t& pioOffset = 0) :
		CLAPBase(deviceNum),
		m_pioDeviceName("/dev/xdma" + std::to_string(deviceNum) + "_user"),
		m_pioSize(pioSize),
		m_pioOffset(pioOffset)
#ifndef EMBEDDED_XILINX
		,
		m_mutex()
#endif
	{
		m_fd        = open(m_pioDeviceName.c_str(), O_RDWR | O_NONBLOCK);
		int32_t err = errno;

		if (m_fd < 0)
		{
			std::stringstream ss;
			ss << CLASS_TAG("") << "Unable to open device " << m_pioDeviceName << "; errno: " << err;
			throw CLAPException(ss.str());
		}

#ifndef EMBEDDED_XILINX
		m_pMapBase = mmap(0, m_pioSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, m_pioOffset);
		err        = errno;

		if (m_pMapBase == MAP_FAILED)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Failed to map memory into userspace, errno: " << err;
			throw CLAPException(ss.str());
		}
#endif

		m_valid = true;
	}

	DISABLE_COPY_ASSIGN_MOVE(XDMAPio)

	~XDMAPio()
	{
#ifndef EMBEDDED_XILINX
		std::lock_guard<std::mutex> lock(m_mutex);

		munmap(m_pMapBase, m_pioSize);
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
			throw CLAPException(ss.str());
		}

		const std::size_t size = sizeof(T);
		if (size > MAX_PIO_ACCESS_SIZE)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Type size (" << std::dec << size << " byte) exceeds maximal allowed Pio size (" << MAX_PIO_ACCESS_SIZE << " byte)";
			throw CLAPException(ss.str());
		}

		if (addr >= m_pioSize + m_pioOffset)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Address: (0x" << std::hex << addr << ") exceeds Pio address range (0x" << m_pioOffset << "-0x" << m_pioSize + m_pioOffset << ")";
			throw CLAPException(ss.str());
		}

		uint8_t* vAddr = reinterpret_cast<uint8_t*>(m_pMapBase) + addr;
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
			throw CLAPException(ss.str());
		}

		const std::size_t size = sizeof(T);
		if (size > MAX_PIO_ACCESS_SIZE)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Type size (" << std::dec << size << " byte) exceeds maximal allowed Pio size (" << MAX_PIO_ACCESS_SIZE << " byte)";
			throw CLAPException(ss.str());
		}

		if (addr >= m_pioSize + m_pioOffset)
		{
			std::stringstream ss;
			ss << CLASS_TAG("XDMAPio") << "Address (0x" << std::hex << addr << ") exceeds Pio address range (0x" << m_pioOffset << "-0x" << m_pioSize + m_pioOffset << ")";
			throw CLAPException(ss.str());
		}

		uint8_t* vAddr                 = reinterpret_cast<uint8_t*>(m_pMapBase) + addr;
		*(reinterpret_cast<T*>(vAddr)) = data;
	}

private:
	std::string m_pioDeviceName;
	std::size_t m_pioSize;
	std::size_t m_pioOffset;
	int32_t m_fd     = -1;
	void* m_pMapBase = nullptr;
	bool m_valid     = false;
#ifndef EMBEDDED_XILINX
	std::mutex m_mutex;
#endif

	static const std::size_t MAX_PIO_ACCESS_SIZE = sizeof(uint64_t);
};
#endif // XDMAPio

namespace internal
{
inline CLAPManaged::CLAPManaged(internal::CLAPBasePtr pClap) :
	m_pClap(std::move(pClap))
{
	if (m_pClap)
		m_pClap->registerObject(this);
}

inline CLAPManaged::~CLAPManaged()
{
	if (m_pClap)
		m_pClap->unregisterObject(this);
}
} // namespace internal

} // namespace clap