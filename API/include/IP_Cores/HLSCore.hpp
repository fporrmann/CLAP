/* 
 *  File: HLSCore.hpp
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

#include "../internal/Logger.hpp"
#include "../internal/RegisterControl.hpp"
#include "../internal/Types.hpp"
#include "internal/ApCtrl.hpp"
#include "internal/WatchDog.hpp"

#include <functional>
#include <string>

namespace clap
{
class HLSCore : public internal::RegisterControlBase
{
	DISABLE_COPY_ASSIGN_MOVE(HLSCore)

	/**
	0x0 : Control signals
		  bit 0  - ap_start (Read/Write/COH)
		  bit 1  - ap_done (Read/COR)
		  bit 2  - ap_idle (Read)
		  bit 3  - ap_ready (Read)
		  bit 7  - auto_restart (Read/Write)
		  others - reserved
	0x4 : Global Interrupt Enable (GIE) Register
		  bit 0  - Global Interrupt Enable (Read/Write)
		  others - reserved
	0x8 : IP Interrupt Enable Register (IER) (Read/Write)
		  bit 0  - Channel 0 (ap_done)
		  bit 1  - Channel 1 (ap_ready)
		  others - reserved
	0xC : IP Interrupt Status Register (ISR) (Read/TOW)
		  bit 0  - Channel 0 (ap_done)
		  bit 1  - Channel 1 (ap_ready)
		  others - reserved
	(SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)
	**/

	enum REGISTER_MAP
	{
		ADDR_AP_CTRL = 0x0,
		ADDR_GIE     = 0x4,
		ADDR_IER     = 0x8,
		ADDR_ISR     = 0xC
	};

public:
	enum APInterrupts
	{
		AP_INTR_DONE  = 1 << 0,
		AP_INTR_READY = 1 << 1,
		AP_INTR_ALL   = (1 << 2) - 1 // All bits set
	};

	enum class AddressType
	{
		BIT_32 = sizeof(uint32_t),
		BIT_64 = sizeof(uint64_t)
	};

public:
	HLSCore(const CLAPPtr& pClap, const uint64_t& ctrlOffset, const std::string& name) :
		RegisterControlBase(pClap, ctrlOffset),
		m_apCtrl(),
		m_intrCtrl(),
		m_intrStat(),
		m_watchDog(name, pClap->MakeUserInterrupt()),
		m_name(name)
	{
		registerReg<uint8_t>(m_apCtrl, ADDR_AP_CTRL);
		registerReg<uint8_t>(m_intrCtrl, ADDR_IER);
		registerReg<uint8_t>(m_intrStat, ADDR_ISR);

		m_watchDog.SetStatusRegister(&m_apCtrl);
	}

	////////////////////////////////////////

	bool Start()
	{
		m_apCtrl.Reset();

		if (!m_watchDog.Start())
		{
			LOG_ERROR << CLASS_TAG("HLSCore") << "Trying to start HLS core at: 0x" << std::hex << m_ctrlOffset << " which is still running, stopping startup ..." << std::endl;
			return false;
		}

		if (!m_apCtrl.Start())
		{
			LOG_ERROR << CLASS_TAG("HLSCore") << "Trying to start HLS core at: 0x" << std::hex << m_ctrlOffset << " which is currently not idle, stopping startup ..." << std::endl;
			return false;
		}

		return true;
	}

	bool WaitForFinish(const int32_t& timeoutMS = WAIT_INFINITE)
	{
		return m_watchDog.WaitForFinish(timeoutMS);
	}

	double GetRuntime() const
	{
		return m_watchDog.GetRuntime();
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void EnableInterrupts(const uint32_t& eventNo, const APInterrupts& intr = AP_INTR_ALL)
	{
		m_watchDog.InitInterrupt(getDevNum(), eventNo, &m_intrStat);
		m_intrCtrl.EnableInterrupts(intr);
		writeRegister<uint8_t>(ADDR_GIE, 1);
	}

	void DisableInterrupts(const APInterrupts& intr = AP_INTR_ALL)
	{
		m_intrCtrl.DisableInterrupts(intr);

		// If all interrupts are disabled also disable the global interrupt enable
		if (!m_intrCtrl.IsInterruptEnabled())
			writeRegister<uint8_t>(ADDR_GIE, 0);

		m_watchDog.UnsetInterrupt();
	}

	////////////////////////////////////////

	////////////////////////////////////////

	template<typename T>
	void SetDataAddr(const uint64_t& offset, const T& addr)
	{
		setDataAddr<T>(offset, addr);
	}

	void SetDataAddr(const uint64_t& offset, const Memory& mem, const AddressType& addrType = AddressType::BIT_64)
	{
		if (addrType == AddressType::BIT_32)
			setDataAddr<uint32_t>(offset, static_cast<uint32_t>(mem.GetBaseAddr()));
		else
			setDataAddr<uint64_t>(offset, mem.GetBaseAddr());
	}

	template<typename T>
	T GetDataAddr(const uint64_t& offset)
	{
		return getDataAddr<T>(offset);
	}

	uint64_t GetDataAddr(const uint64_t& offset, const AddressType& addrType = AddressType::BIT_64)
	{
		if (addrType == AddressType::BIT_32)
			return getDataAddr<uint32_t>(offset);
		else
			return getDataAddr<uint64_t>(offset);
	}

	const std::string& GetName() const
	{
		return m_name;
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void SetAutoRestart(const bool& enable = true)
	{
		m_apCtrl.SetAutoRestart(enable);
	}

	bool IsDone()
	{
		return m_apCtrl.IsDone();
	}

	bool IsIdle()
	{
		return m_apCtrl.IsIdle();
	}

	void PrintApStatus()
	{
		LOG_INFO << "---- " << m_name << " ----" << std::endl;
		m_apCtrl.PrintStatus();
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void RegisterInterruptCallback(const std::function<void(uint32_t)>& callback)
	{
		m_watchDog.RegisterInterruptCallback(callback);
	}

	////////////////////////////////////////

private:
	template<typename T>
	void setDataAddr(const uint64_t& offset, const T addr)
	{
		writeRegister<T>(offset, addr, true);
	}

	template<typename T>
	T getDataAddr(const uint64_t& offset)
	{
		return readRegister<T>(offset);
	}

private:
	class InterruptEnableRegister : public internal::Register<uint8_t>
	{
	public:
		InterruptEnableRegister() :
			Register("Interrupt Enable Register"),
			m_ap_done(false),
			m_ap_ready(false)
		{
			RegisterElement<bool>(&m_ap_done, "ap_done", 0);
			RegisterElement<bool>(&m_ap_ready, "ap_ready", 1);
		}

		void EnableInterrupts(const APInterrupts& intr = AP_INTR_ALL)
		{
			setInterrupts(true, intr);
		}

		void DisableInterrupts(const APInterrupts& intr = AP_INTR_ALL)
		{
			setInterrupts(false, intr);
		}

		bool IsInterruptEnabled() const
		{
			return (m_ap_done || m_ap_ready);
		}

	private:
		void setInterrupts(bool enable, const APInterrupts& intr)
		{
			if (intr & AP_INTR_DONE)
				m_ap_done = enable;
			if (intr & AP_INTR_READY)
				m_ap_ready = enable;

			Update(internal::Direction::WRITE);
		}

	private:
		bool m_ap_done;
		bool m_ap_ready;
	};

	class InterruptStatusRegister : public internal::Register<uint8_t>, public internal::HasInterrupt
	{
	public:
		InterruptStatusRegister() :
			Register("Interrupt Status Register"),
			m_ap_done(false),
			m_ap_ready(false)
		{
			RegisterElement<bool>(&m_ap_done, "ap_done", 0);
			RegisterElement<bool>(&m_ap_ready, "ap_ready", 1);
		}

		void ClearInterrupts()
		{
			m_lastInterrupt = GetInterrupts();
			ResetInterrupts(AP_INTR_ALL);
		}

		uint32_t GetInterrupts()
		{
			Update();
			uint32_t intr = 0;
			intr |= m_ap_done << (AP_INTR_DONE >> 1);
			intr |= m_ap_ready << (AP_INTR_READY >> 1);

			return intr;
		}

		void ResetInterrupts(const APInterrupts& intr)
		{
			if (intr & AP_INTR_DONE)
				m_ap_done = 1;
			if (intr & AP_INTR_READY)
				m_ap_ready = 1;

			Update(internal::Direction::WRITE);
		}

	private:
		bool m_ap_done;
		bool m_ap_ready;
	};

private:
	internal::ApCtrl m_apCtrl;
	InterruptEnableRegister m_intrCtrl;
	InterruptStatusRegister m_intrStat;
	internal::WatchDog m_watchDog;
	std::string m_name;
};
} // namespace clap