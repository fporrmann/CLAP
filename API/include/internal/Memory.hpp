/*
 *  File: Memory.hpp
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

#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>

#include "Exceptions.hpp"
#include "Logger.hpp"
#include "StdStub.hpp"

namespace clap
{
// Forward declaration is required here to allow for the friend declaration
// TODO: Find a better way to do this -- maybe a separate header file?
// TODO: Add a destructor to free the memory on destruction -- This will need to be propagated to the MemoryManager -- MM check for invalid memory objs on alloc and cleanup?
namespace internal
{
class MemoryManager;
}

class Memory
{
	friend class internal::MemoryManager;

public:
	Memory() = default;

	template<typename T>
	static T Create(const internal::MemoryManagerPtr& manager, const uint64_t& baseAddr, const uint64_t& size)
	{
		return T(new Memory(baseAddr, size, manager));
	}

	~Memory()
	{
		free();
	}

	const uint64_t& GetBaseAddr() const
	{
		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "ERROR: Memory Buffer is Invalid.";
			throw MemoryException(ss.str());
		}

		return m_baseAddr;
	}

	const uint64_t& GetSize() const
	{
		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "ERROR: Memory Buffer is Invalid.";
			throw MemoryException(ss.str());
		}

		return m_size;
	}

	const bool& IsValid() const
	{
		return m_valid;
	}

	operator bool() const
	{
		return m_valid;
	}

	friend std::ostream& operator<<(std::ostream& stream, const Memory& m)
	{
		stream << std::showbase << std::hex
			   << "Address=" << m.m_baseAddr
			   << "; Size=" << m.m_size
			   << std::dec << std::noshowbase;
		return stream;
	}

private:
	Memory(const uint64_t& baseAddr, const uint64_t& size, internal::MemoryManagerPtr manager = nullptr) :
		m_manager(std::move(manager)),
		m_baseAddr(baseAddr),
		m_size(size),
		m_valid(true)
	{}

	// This should never be called directly, instead it should be called by the MemoryManager to ensure proper cleanup
	void invalidate()
	{
		m_baseAddr = 0;
		m_size     = 0;
		m_valid    = false;
	}

	void free();

private:
	internal::MemoryManagerPtr m_manager = nullptr;

	uint64_t m_baseAddr = 0;
	uint64_t m_size     = 0;
	bool m_valid        = false;
};

namespace internal
{
class MemoryManager : public std::enable_shared_from_this<MemoryManager>
{
	static constexpr uint32_t ALIGNMENT          = 0x40;
	static constexpr uint32_t COALESCE_THRESHOLD = 4;
	static constexpr uint64_t INV_NULL           = ~static_cast<uint64_t>(0);
	using MemList                                = std::list<std::pair<uint64_t, uint64_t>>;

	template<typename T>
	struct always_false : std::false_type {};

public:
	MemoryManager(const uint64_t& baseAddr, const uint64_t& size) :
		m_baseAddr(baseAddr),
		m_size(size),
		m_spaceLeft(size),
		m_mutex(),
		m_freeMemory(),
		m_usedMemory()
	{
		m_freeMemory.push_back(std::make_pair(m_baseAddr, m_size));
	}

	DISABLE_COPY_ASSIGN_MOVE(MemoryManager)

	template<typename T>
	T AllocMemory(const uint64_t& size)
	{
		auto [addr, alignedSize] = allocate(size);
		if constexpr (std::is_same<T, MemoryPtr>::value || std::is_same<T, MemoryUPtr>::value)
			return Memory::Create<T>(shared_from_this(), addr, alignedSize);
		else if constexpr (std::is_same<T, Memory>::value)
			return Memory(addr, alignedSize, shared_from_this());
		else
		{
			static_assert(always_false<T>::value, "Unsupported memory type. Use Memory, MemoryPtr, or MemoryUPtr.");
			return T(); // This line will never be reached, but is required to satisfy the compiler
		}
	}

	bool FreeMemory(Memory& mem)
	{
		CLAP_CLASS_LOG_DEBUG << "Waiting for memory mutex" << std::endl;
		std::lock_guard<std::mutex> lock(m_mutex);
		CLAP_CLASS_LOG_DEBUG << "Freeing memory: " << mem << std::endl;

		if (!mem.IsValid())
		{
			CLAP_CLASS_LOG_DEBUG << "Memory is already invalidated, nothing to free." << std::endl;
			return true;
		}

		// Search for the given address in the list of used memory regions
		MemList::iterator it = std::find_if(m_usedMemory.begin(), m_usedMemory.end(), [&mem](const MemList::value_type& p) { return p.first == mem.m_baseAddr; });
		// Check if the given address was found
		if (it == m_usedMemory.end())
		{
			CLAP_CLASS_LOG_DEBUG << "Memory not found: " << mem << std::endl;
			return false;
		}

		m_spaceLeft += it->second;
		m_freeMemory.push_front(std::make_pair(it->first, it->second));
		m_usedMemory.erase(it);

		CLAP_CLASS_LOG_DEBUG << "Freed memory at address 0x" << std::hex << mem.m_baseAddr << std::endl;
		mem.invalidate();
		CLAP_CLASS_LOG_DEBUG << "Memory Invalidated" << std::endl;

		if (m_freeMemory.size() > COALESCE_THRESHOLD)
			coalesce();

		return true;
	}

	uint64_t GetAvailableSpace() const
	{
		return m_spaceLeft;
	}

	void Reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_freeMemory.clear();
		m_usedMemory.clear();
		m_freeMemory.push_back(std::make_pair(m_baseAddr, m_size));
		m_spaceLeft = m_size;
	}

	void SetCustomAlignment(const int32_t& alignment)
	{
		m_alignment = alignment;
	}

	const int32_t& GetCustomAlignment() const
	{
		return m_alignment;
	}

private:
	std::pair<uint64_t, uint64_t> allocate(const uint64_t& size)
	{
		if (size == 0)
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Tried to allocate zero size memory.";
			throw MemoryException(ss.str());
		}

		if (size > m_spaceLeft)
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Not enough memory left to allocate " << std::dec << size << " byte.";
			throw MemoryException(ss.str());
		}

		uint32_t alignment = (m_alignment == -1) ? ALIGNMENT : static_cast<uint32_t>(m_alignment);

		// Due to the internal memory structure the memory needs to be properly aligned
		const uint64_t modSize     = size % alignment;
		const uint64_t pad         = (modSize == 0) ? 0 : (alignment - modSize);
		const uint64_t alignedSize = size + pad;

		std::lock_guard<std::mutex> lock(m_mutex);

		uint64_t addr = INV_NULL;

		for (auto it = m_freeMemory.begin(); it != m_freeMemory.end(); it++)
		{
			// Check if the current element has enough free space
			if (it->second < alignedSize) continue;

			addr = it->first;

			// If the region is bigger than the amount requested
			// Update the memory left in the current region
			if (it->second > alignedSize)
			{
				it->first += alignedSize;
				it->second -= alignedSize;
			}
			// Else remove the region from the list and update the iterator
			else
			{
				m_freeMemory.erase(it++);
				if (it == m_freeMemory.end()) break;
			}
		}

		if (addr == INV_NULL)
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Not enough contiguous memory available to allocate " << std::dec << size << " byte.";
			throw MemoryException(ss.str());
		}

		m_usedMemory.push_back(std::make_pair(addr, alignedSize));
		m_spaceLeft -= alignedSize;

		CLAP_CLASS_LOG_DEBUG << "Allocated 0x" << std::hex << size << " byte of memory at address 0x" << addr
							 << " (Alignment: " << alignment << ", Space left: 0x" << m_spaceLeft << " byte)" << std::dec << std::endl;
		return std::make_pair(addr, size);
	}

	void coalesce()
	{
		m_freeMemory.sort();

		MemList::iterator cur  = m_freeMemory.begin();
		MemList::iterator next = cur;
		++next;

		MemList::iterator end = m_freeMemory.end();

		while (next != end)
		{
			// Check if the current region is directly infront of the next region
			if ((cur->first + cur->second) == next->first)
			{
				// Merge the size of both regions
				cur->second += next->second;
				// Remove the second region
				m_freeMemory.erase(next);
				next = cur;
			}
			// Regions are not adjacent do nothing
			else
				cur = next;

			++next;
		}
	}

private:
	uint64_t m_baseAddr;
	uint64_t m_size;
	uint64_t m_spaceLeft;
	std::mutex m_mutex;
	MemList m_freeMemory;
	MemList m_usedMemory;
	int32_t m_alignment = -1;
};
} // namespace internal

void Memory::free()
{
	if (m_manager && m_valid)
	{
		try
		{
			CLAP_CLASS_LOG_DEBUG << "Freeing memory at address 0x" << std::hex << m_baseAddr << " of size 0x" << std::hex << m_size << " bytes." << std::dec << std::endl;
			m_manager->FreeMemory(*this);
		}
		catch (const MemoryException& e)
		{
			CLAP_CLASS_LOG_ERROR << "Failed to free memory: " << e.what() << std::endl;
		}
	}
} // namespace internal
} // namespace clap
