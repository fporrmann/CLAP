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

#include <xscugic.h>

namespace clap
{
namespace internal
{
namespace backends
{
static inline void interruptHandler(void* p);

class BareMetalGic
{
public:
	static BareMetalGic& GetInstance()
	{
		static BareMetalGic instance;
		return instance;
	}

	BareMetalGic(BareMetalGic const&)   = delete;
	void operator=(BareMetalGic const&) = delete;

	void RegisterInterrupt(const uint32_t& interruptNum, void* pObj)
	{
		CLAP_LOG_DEBUG << CLASS_TAG("BareMetalGic") << "Registering interrupt " << interruptNum << std::endl;
		XScuGic_SetPriorityTriggerType(&m_gic, interruptNum, 0xA0, 0x3);

		// Connect a device driver handler that will be called when an interrupt for the device occurs, the device driver handler performs the specific interrupt processing for the device
		int result = XScuGic_Connect(&m_gic, interruptNum, (Xil_ExceptionHandler)interruptHandler, pObj);
		if (result != XST_SUCCESS)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalGic") << "failed to connect interrupt handler";
			throw CLAPException(ss.str());
		}

		// Enable the interrupt for the device and then cause (simulate) an interrupt so the handlers will be called
		XScuGic_Enable(&m_gic, interruptNum);
	}

	void UnregisterInterrupt(const uint32_t& interruptNum)
	{
		XScuGic_Disable(&m_gic, interruptNum);
		XScuGic_Disconnect(&m_gic, interruptNum);
	}

private:
	BareMetalGic()
	{
		XScuGic_Config* pGICConfig;

		// Initialize the GIC driver so that it is ready to use.
		pGICConfig = XScuGic_LookupConfig(0);
		if (NULL == pGICConfig)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalGic") << "configuration lookup failed";
			throw CLAPException(ss.str());
		}

		int result = XScuGic_CfgInitialize(&m_gic, pGICConfig, pGICConfig->CpuBaseAddress);
		if (result != XST_SUCCESS)
		{
			std::stringstream ss;
			ss << CLASS_TAG("BareMetalGic") << "initialization failed";
			throw CLAPException(ss.str());
		}

		// Initialize the exception table and register the interrupt controller handler with the exception table
		Xil_ExceptionInit();

		Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, &m_gic);

		// Enable non-critical exceptions
		Xil_ExceptionEnable();
	}

private:
	XScuGic m_gic;
};

class BareMetalUserInterrupt : virtual public UserInterruptBase
{
	DISABLE_COPY_ASSIGN_MOVE(BareMetalUserInterrupt)

public:
	BareMetalUserInterrupt()
	{
		m_devName = "BareMetal";
	}

	virtual void Init([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, [[maybe_unused]] HasInterrupt* pReg = nullptr)
	{
		m_intrNum = interruptNum;
		m_pReg    = pReg;

		CLAP_LOG_DEBUG << CLASS_TAG("BareMetalUserInterrupt") << "Registering Interrupt " << interruptNum << std::endl;

		void* tAddr = reinterpret_cast<void*>(this);

		BareMetalGic::GetInstance().RegisterInterrupt(interruptNum, tAddr);
		m_isSet = true;
	}

	void Unset()
	{
		if (m_isSet && m_intrNum != MINUS_ONE)
			BareMetalGic::GetInstance().UnregisterInterrupt(m_intrNum);

		m_isSet   = false;
		m_intrNum = MINUS_ONE;
		m_pReg    = nullptr;
	}

	bool IsSet() const
	{
		return m_isSet;
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true)
	{
		if (m_intrPresent)
		{
			m_intrPresent = false;
			return true;
		}

		return false;
	}

	void interruptHandler()
	{
		if (m_pReg)
			m_pReg->ClearInterrupts();

		uint32_t lastIntr = -1;
		if (m_pReg)
			lastIntr = m_pReg->GetLastInterrupt();

		if (m_runCallbacks)
		{
			for (auto& callback : m_callbacks)
				callback(lastIntr);
		}

		CLAP_LOG_DEBUG << CLASS_TAG("BareMetalUserInterrupt") << "Interrupt present, Interrupt Mask: " << (m_pReg ? std::to_string(lastIntr) : "No Status Register Specified") << std::endl;
		m_intrPresent = true;
	}

private:
	bool m_isSet        = false;
	uint32_t m_intrNum  = -1;
	bool m_runCallbacks = true;
	bool m_intrPresent  = false;
};

class BareMetalBackend : virtual public CLAPBackend
{
public:
	BareMetalBackend([[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0)
	{
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

		const uint64_t unalignedAddr = addr % sizeof(uint64_t);

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

		const uint64_t unalignedAddr = addr % sizeof(uint64_t);

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

static inline void interruptHandler(void* p)
{
	BareMetalUserInterrupt* pObj = reinterpret_cast<BareMetalUserInterrupt*>(p);
	pObj->interruptHandler();
}

} // namespace backends
} // namespace internal
} // namespace clap
