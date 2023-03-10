/* 
 *  File: HLSCore.h
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

#include "internal/ApCtrl.h"
#include "internal/IPControl.h"
#include "internal/WatchDog.h"

enum APInterrupts
{
	AP_INTR_DONE  = 1 << 0,
	AP_INTR_READY = 1 << 1,
	AP_INTR_ALL   = (1 << 2) - 1 // All bits set
};

enum AddressType
{
	BIT_32 = sizeof(uint32_t),
	BIT_64 = sizeof(uint64_t)
};

class HLSCore : public IPControlBase
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
	HLSCore(std::shared_ptr<class XDMA> pXdma, const uint64_t& ctrlOffset, const std::string& name) :
		IPControlBase(pXdma, ctrlOffset),
		m_apCtrl(),
		m_intrCtrl(),
		m_intrStat(),
		m_watchDog(name),
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
			std::cerr << CLASS_TAG("HLSCore") << "Trying to start HLS core at: 0x" << std::hex << m_ctrlOffset << " which is still running, stopping startup ..." << std::endl;
			return false;
		}

		if (!m_apCtrl.Start())
		{
			std::cerr << CLASS_TAG("HLSCore") << "Trying to start HLS core at: 0x" << std::hex << m_ctrlOffset << " which is currently not idle, stopping startup ..." << std::endl;
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

	void SetDataAddr(const uint64_t& offset, const Memory& mem, const AddressType& addrType = BIT_64)
	{
		if (addrType == BIT_32)
			setDataAddr<uint32_t>(offset, mem.GetBaseAddr());
		else
			setDataAddr<uint64_t>(offset, mem.GetBaseAddr());
	}

	template<typename T>
	T GetDataAddr(const uint64_t& offset)
	{
		return getDataAddr<T>(offset);
	}

	uint64_t GetDataAddr(const uint64_t& offset, const AddressType& addrType = BIT_64)
	{
		if (addrType == BIT_32)
			return getDataAddr<uint32_t>(offset);
		else
			return getDataAddr<uint64_t>(offset);
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
		std::cout << "---- " << m_name << " ----" << std::endl;
		m_apCtrl.PrintStatus();
	}

	////////////////////////////////////////

private:
	template<typename T>
	void setDataAddr(const uint64_t& offset, const T& addr)
	{
		writeRegister<T>(offset, addr);
	}

	template<typename T>
	T getDataAddr(const uint64_t& offset)
	{
		return readRegister<T>(offset);
	}

private:
	struct InterruptEnableRegister : public Register<uint8_t>
	{
		InterruptEnableRegister() :
			Register("Interrupt Enable Register"),
			ap_done(false),
			ap_ready(false)
		{
			RegisterElement<bool>(&ap_done, "ap_done", 0);
			RegisterElement<bool>(&ap_ready, "ap_ready", 1);
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
			return (ap_done || ap_ready);
		}

	private:
		void setInterrupts(bool enable, const APInterrupts& intr)
		{
			if (intr & AP_INTR_DONE)
				ap_done = enable;
			if (intr & AP_INTR_READY)
				ap_ready = enable;

			Update(Direction::WRITE);
		}

	public:
		bool ap_done;
		bool ap_ready;
	};

	struct InterruptStatusRegister : public Register<uint8_t>, public HasInterrupt
	{
		InterruptStatusRegister() :
			Register("Interrupt Status Register"),
			ap_done(false),
			ap_ready(false)
		{
			RegisterElement<bool>(&ap_done, "ap_done", 0);
			RegisterElement<bool>(&ap_ready, "ap_ready", 1);
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
			intr |= ap_done << (AP_INTR_DONE >> 1);
			intr |= ap_ready << (AP_INTR_READY >> 1);

			return intr;
		}

		void ResetInterrupts(const APInterrupts& intr)
		{
			if (intr & AP_INTR_DONE)
				ap_done = 1;
			if (intr & AP_INTR_READY)
				ap_ready = 1;

			Update(Direction::WRITE);
		}

		bool ap_done;
		bool ap_ready;
	};

private:
	ApCtrl m_apCtrl;
	InterruptEnableRegister m_intrCtrl;
	InterruptStatusRegister m_intrStat;
	WatchDog m_watchDog;
	std::string m_name;
};
