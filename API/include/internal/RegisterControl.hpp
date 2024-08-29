/*
 *  File: RegisterControl.hpp
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

#include "../CLAP.hpp"
#include "RegisterInterface.hpp"
#include "Utils.hpp"

#include "Types.hpp"

#include <cstdint>
#include <cxxabi.h>
#include <vector>

#define CLAP_IP_CORE_LOG_DEBUG   CLAP_CLASS_LOG_DEBUG   << nameTag()
#define CLAP_IP_CORE_LOG_VERBOSE CLAP_CLASS_LOG_VERBOSE << nameTag()
#define CLAP_IP_CORE_LOG_INFO    CLAP_CLASS_LOG_INFO    << nameTag()
#define CLAP_IP_CORE_LOG_WARNING CLAP_CLASS_LOG_WARNING << nameTag()
#define CLAP_IP_CORE_LOG_ERROR   CLAP_CLASS_LOG_ERROR   << nameTag()

// TODO: Move this to a more appropriate place
enum class DMAChannel
{
	MM2S,
	S2MM
};

static inline std::ostream& operator<<(std::ostream& os, const DMAChannel& channel)
{
	switch (channel)
	{
		case DMAChannel::MM2S:
			os << "MM2S";
			break;
		case DMAChannel::S2MM:
			os << "S2MM";
			break;
		default:
			os << "Unknown";
			break;
	}

	return os;
}

namespace clap
{
namespace internal
{
class RegisterControlBase : public CLAPManaged
{
	DISABLE_COPY_ASSIGN_MOVE(RegisterControlBase)

public:
	RegisterControlBase(CLAPBasePtr pClap, const uint64_t& ctrlOffset, const std::string& name = "") :
		CLAPManaged(std::move(pClap)),
		m_name(name),
		m_ctrlOffset(ctrlOffset),
		m_registers()
	{
		// Register the control address as a polling address, causing it to be ignored when printing transfer times
		// this is done to prevent log flooding when the control register is polled
		CLAP()->AddPollAddress(ctrlOffset);
	}

	virtual ~RegisterControlBase() override = default;

	// Method used by the static update callback function to update the given register
	template<typename T>
	void UpdateRegister(Register<T>* pReg, const uint64_t& offset, const Direction& dir)
	{
		if (dir == Direction::READ)
			pReg->Update(readRegister<T>(offset));
		else
			writeRegister<T>(offset, pReg->GetValue());
	}

	// Updates all registered registers
	void UpdateAllRegisters()
	{
		for (RegisterIntf* pReg : m_registers)
			pReg->Update(Direction::READ);
	}

	/// @brief Tries to automatically detect the interrupt ID of the IP core, currently only available for PetaLinux
	bool AutoDetectInterruptID()
	{
		return detectInterruptID();
	}

	void SetName(const std::string& name)
	{
		m_name = name;
	}

	const std::string& GetName() const
	{
		return m_name;
	}

	// Callback function, called by the register when the Update() method is called
	template<typename T>
	static void UpdateCallBack(Register<T>* pReg, const uint64_t& offset, const Direction& dir, void* pObj)
	{
		// Make sure that the given object pointer is in fact an RegisterControlBase object
		RegisterControlBase* pIPCtrl = reinterpret_cast<RegisterControlBase*>(pObj);
		if (!pIPCtrl) return;

		pIPCtrl->UpdateRegister(pReg, offset, dir);
	}

protected:
	// Register a register to the list of known registers and
	// setup its update callback function
	template<typename T>
	void registerReg(Register<T>& reg, const uint64_t& offset = 0x0)
	{
		if constexpr (sizeof(T) > sizeof(uint64_t))
		{
			std::stringstream ss("");
			ss << CLASS_TAG_AUTO << nameTag() << "Registers with a size > " << sizeof(uint64_t) << " byte are currently not supported";
			throw std::runtime_error(ss.str());
		}

		CLAP()->AddPollAddress(m_ctrlOffset + offset);

		reg.SetupCallBackBasedUpdate(reinterpret_cast<void*>(this), offset, UpdateCallBack<T>);
		m_registers.push_back(&reg);
	}

	void registerPollOffset(const uint64_t& offset)
	{
		CLAP()->AddPollAddress(m_ctrlOffset + offset);
	}

	template<typename T>
	T readRegister(const uint64_t& regOffset)
	{
		switch (sizeof(T))
		{
			case 8:
				return static_cast<T>(CLAP()->Read64(m_ctrlOffset + regOffset));
			case 4:
				return static_cast<T>(CLAP()->Read32(m_ctrlOffset + regOffset));
			case 2:
				return static_cast<T>(CLAP()->Read16(m_ctrlOffset + regOffset));
			case 1:
				return static_cast<T>(CLAP()->Read8(m_ctrlOffset + regOffset));
			default:
				std::stringstream ss("");
				ss << CLASS_TAG_AUTO << nameTag() << "Registers with a size > " << sizeof(uint64_t) << " byte are currently not supported";
				throw std::runtime_error(ss.str());
		}
	}

	template<typename T>
	void writeRegister(const uint64_t& regOffset, const T& regData, const bool& validate = false)
	{
		switch (sizeof(T))
		{
			case 8:
				CLAP()->Write64(m_ctrlOffset + regOffset, static_cast<uint64_t>(regData));
				break;
			case 4:
				CLAP()->Write32(m_ctrlOffset + regOffset, static_cast<uint32_t>(regData));
				break;
			case 2:
				CLAP()->Write16(m_ctrlOffset + regOffset, static_cast<uint16_t>(regData));
				break;
			case 1:
				CLAP()->Write8(m_ctrlOffset + regOffset, static_cast<uint8_t>(regData));
				break;
			default:
				std::stringstream ss("");
				ss << CLASS_TAG_AUTO << nameTag() << "Registers with a size > " << sizeof(uint64_t) << " byte are currently not supported";
				throw std::runtime_error(ss.str());
		}

		if (validate)
		{
			const T readData = readRegister<T>(regOffset);
			if (readData != regData)
			{
				std::stringstream ss("");
				ss << CLASS_TAG_AUTO << nameTag() << "Register write validation failed. Expected: 0x" << std::hex << regData << ", Read: 0x" << readData << std::dec;
				throw std::runtime_error(ss.str());
			}
		}
	}

	virtual bool detectInterruptID()
	{
		Expected<uint32_t> res = CLAP()->GetUIOID(m_ctrlOffset);
		if (res)
		{
			m_detectedInterruptID = static_cast<int32_t>(res.Value());
			CLAP_IP_CORE_LOG_INFO << "Detected interrupt ID: " << m_detectedInterruptID << std::endl;
			return true;
		}

		return false;
	}

	/// @brief Returns the id of the device, for XDMA devices this is an idx starting at 0 for the first device, for all other backends this is 0
	/// @return The id of the device
	uint32_t getDevNum()
	{
		return CLAP()->GetDevNum();
	}

	std::string nameTag() const
	{
		if(m_name.empty())
			return "";

		return "[" + m_name + "] ";
	}

protected:
	std::string m_name;
	uint64_t m_ctrlOffset;
	std::vector<RegisterIntf*> m_registers;
	int32_t m_detectedInterruptID = -1;
};
} // namespace internal
} // namespace clap
