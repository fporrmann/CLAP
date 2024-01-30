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
		CLAP_LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
	}

	void Unset()
	{
		CLAP_LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
	}

	bool IsSet() const
	{
		CLAP_LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
		return false;
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true)
	{
		CLAP_LOG_WARNING << CLASS_TAG("BareMetalUserInterrupt") << " Currently not implemented" << std::endl;
		return false;
	}
};

class BareMetalBackend : virtual public CLAPBackend
{
public:
	BareMetalBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0)
	{
		CLAP_LOG_WARNING << CLASS_TAG("BareMetalBackend") << "WARNING: BareMetalBackend is currently untested and therefore, probably not fully functional." << std::endl;
		m_nameRead    = "BareMetal";
		m_nameWrite   = "BareMetal";
		m_backendName = "BareMetal";
		m_valid       = true;
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)
	{
		CLAP_LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t count      = 0;
		uint64_t offset     = addr;
		uint64_t bytes2Read = sizeInByte;
		uint8_t* pByteData  = reinterpret_cast<uint8_t*>(pData);

		const uint64_t unalignedAddr = addr % sizeof(uint64_t); // TODO: Check if this is correct, especially for 32-bit systems

		// Get a uint8_t pointer to the memory
		const uint8_t* pMem = reinterpret_cast<uint8_t*>(offset);

		// If the address is not aligned, read the first x-bytes sequentially
		if (unalignedAddr != 0)
		{
			const uint64_t bytes = bytes2Read > unalignedAddr ? unalignedAddr : bytes2Read;

			readSingle(addr, pData, bytes);

			count += bytes;
			bytes2Read -= bytes;
		}

		/*
		while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(pByteData + count, (void*)(offset), bytes);

			count += bytes;
			offset += bytes;
		}

		*/

		//	T Read(const T& addr, void* pData, const T& sizeInByte) const

		const uint64_t unalignedBytes    = bytes2Read % sizeof(uint64_t);
		const uint64_t sizeInByteAligned = bytes2Read - unalignedBytes;

		while (count < sizeInByteAligned)
		{
			uint64_t bytes = sizeInByteAligned - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(pByteData + count, reinterpret_cast<const void*>(pMem + count), bytes);

			count += bytes;
		}

		if (unalignedBytes != 0)
		{
			readSingle(addr + count, reinterpret_cast<void*>(pByteData + count), unalignedBytes);
			count += unalignedBytes;
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
		CLAP_LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "addr=0x" << std::hex << addr << " pData=0x" << pData << " sizeInByte=0x" << sizeInByte << std::dec << std::endl;

		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalBackend") << "CLAP Instance is not valid, an error probably occurred during device initialization.";
			throw CLAPException(ss.str());
		}

		uint64_t count           = 0;
		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);
		uint64_t offset          = addr;
		uint64_t bytes2Write     = sizeInByte;

		const uint64_t unalignedAddr = addr % sizeof(uint64_t); // TODO: Check if this is correct, especially for 32-bit systems

		/*while (count < sizeInByte)
		{
			uint64_t bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::cout << "Memcpy ... " << std::flush;

			memcpy((void*)(offset), pByteData + count, bytes);

			std::cout << "Done" << std::endl;

			count += bytes;
			offset += bytes;
		}*/

		// Get a uint8_t pointer to the mapped memory of the device
		uint8_t* pMem = reinterpret_cast<uint8_t*>(offset);

		// If the address is not aligned, write the first x-bytes sequentially
		if (unalignedAddr != 0)
		{
			const uint64_t bytes = bytes2Write > unalignedAddr ? unalignedAddr : bytes2Write;

			writeSingle(addr, pData, bytes);

			count += bytes;
			bytes2Write -= bytes;
		}

		const uint64_t unalignedBytes    = bytes2Write % sizeof(uint64_t);
		const uint64_t sizeInByteAligned = bytes2Write - unalignedBytes;

		while (count < sizeInByteAligned)
		{
			uint64_t bytes = sizeInByteAligned - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(reinterpret_cast<void*>(pMem + count), pByteData + count, bytes);

			count += bytes;
		}

		// Write the last unaligned bytes sequentially
		if (unalignedBytes != 0)
		{
			writeSingle(addr + count, reinterpret_cast<const void*>(pByteData + count), unalignedBytes);
			count += unalignedBytes;
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
		CLAP_LOG_DEBUG << CLASS_TAG("BareMetalBackend") << "ReadCtrl is currently not implemented by the BareMetal backend." << std::endl;
	}

	UserInterruptPtr MakeUserInterrupt() const
	{
		return std::make_unique<BareMetalUserInterrupt>();
	}

private:
	void readSingle(const uint64_t& addr, void* pData, const uint64_t& bytes) const
	{
		switch (bytes)
		{
			case 1:
				readSingle<uint8_t>(addr, reinterpret_cast<uint8_t*>(pData));
				break;
			case 2:
				readSingle<uint16_t>(addr, reinterpret_cast<uint16_t*>(pData));
				break;
			case 4:
				readSingle<uint32_t>(addr, reinterpret_cast<uint32_t*>(pData));
				break;
			default:
			{
				std::stringstream ss;
				ss << CLASS_TAG("BareMetalBackend") << "Reading \"" << bytes << "\" unaligned bytes is not supported" << std::endl;
				throw UIOException(ss.str());
			}
			break;
		}
	}

	template<typename T>
	void readSingle(const uint64_t& addr, T* pData) const
	{
		const T* pMem = reinterpret_cast<const T*>(reinterpret_cast<uint8_t*>(addr));
		*pData        = *pMem;
	}

	void writeSingle(const uint64_t& addr, const void* pData, const uint64_t& bytes) const
	{
		switch (bytes)
		{
			case 1:
				writeSingle<uint8_t>(addr, *reinterpret_cast<const uint8_t*>(pData));
				break;
			case 2:
				writeSingle<uint16_t>(addr, *reinterpret_cast<const uint16_t*>(pData));
				break;
			case 4:
				writeSingle<uint32_t>(addr, *reinterpret_cast<const uint32_t*>(pData));
				break;
			default:
			{
				std::stringstream ss;
				ss << CLASS_TAG("BareMetalBackend") << "Writing \"" << bytes << "\" unaligned bytes is not supported" << std::endl;
				throw UIOException(ss.str());
			}
			break;
		}
	}

	template<typename T>
	void writeSingle(const uint64_t& addr, const T data) const
	{
		T* pMem = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(addr));
		*pMem   = data;
	}
};

} // namespace backends
} // namespace internal
} // namespace clap
