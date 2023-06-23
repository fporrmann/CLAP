/*
 *  File: AxiInterruptController.hpp
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

#include "../internal/RegisterControl.hpp"
#include "../internal/UserInterruptBase.hpp"
#include "internal/WatchDog.hpp"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

// TODO: Maybe also implement polling for interrupts

namespace clap
{
class AxiInterruptController;

namespace internal
{
using AxiIntrCtrlCallback    = std::function<void(void)>;
using AxiIntrCtrlCallbacks   = std::vector<AxiIntrCtrlCallback>;
using AxiIntrCtrlCallbackMap = std::map<uint32_t, AxiIntrCtrlCallback>;

class AxiIntrCtrlUserInterrupt : virtual public UserInterruptBase
{
	DISABLE_COPY_ASSIGN_MOVE(AxiIntrCtrlUserInterrupt)

public:
	AxiIntrCtrlUserInterrupt(AxiInterruptController* pAxiIntC) :
		m_pAxiIntC(pAxiIntC)
	{}

	virtual void Init([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, [[maybe_unused]] HasInterrupt* pReg = nullptr);

	void Unset() {}

	bool IsSet() const
	{
		return true;
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true)
	{
		std::mutex mtx;
		std::unique_lock<std::mutex> lck(mtx);

		// The interruptOccured flag is used to make sure that the interrupt has not already occured
		if (!m_interruptOccured)
		{
			if (timeout == WAIT_INFINITE)
				m_cv.wait(lck);
			else
			{
				if (m_cv.wait_for(lck, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
					return false;
			}
		}

		m_interruptOccured = false;

		uint32_t lastIntr = MINUS_ONE;
		if (m_pReg)
			lastIntr = m_pReg->GetLastInterrupt();

		if (runCallbacks)
		{
			for (auto& callback : m_callbacks)
				callback(lastIntr);
		}

		LOG_DEBUG << CLASS_TAG("AxiIntrCtrlUserInterrupt") << "Interrupt present on " << m_devName << ", Interrupt Mask: " << (m_pReg ? std::to_string(lastIntr) : "No Status Register Specified") << std::endl;

		return true;
	}

	void TriggerInterrupt()
	{
		if (m_pReg)
			m_pReg->ClearInterrupts();

		m_interruptOccured = true;
		m_cv.notify_all();
		LOG_DEBUG << CLASS_TAG("AxiIntrCtrlUserInterrupt") << "Interrupt triggered on " << m_devName << std::endl;
	}

private:
	AxiInterruptController* m_pAxiIntC;
	std::condition_variable m_cv = {};
	bool m_interruptOccured      = false;
};
} // namespace internal

class AxiInterruptController : public internal::RegisterControlBase
{
	friend internal::AxiIntrCtrlUserInterrupt;
	DISABLE_COPY_ASSIGN_MOVE(AxiInterruptController)

	enum REGISTER_MAP
	{
		ADDR_ISR = 0x00,
		ADDR_IPR = 0x04,
		ADDR_IER = 0x08,
		ADDR_IAR = 0x0C,
		ADDR_SIE = 0x10,
		ADDR_CIE = 0x14,
		ADDR_IVR = 0x18,
		ADDR_MER = 0x1C,
		ADDR_IMR = 0x20,
		ADDR_ILR = 0x24
	};

	enum class RegUpdate
	{
		Update,
		NoUpdate
	};

public:
	AxiInterruptController(const CLAPPtr& pClap, const uint64_t& ctrlOffset) :
		RegisterControlBase(pClap, ctrlOffset),
		m_watchDog("AxiInterruptController", pClap->MakeUserInterrupt()),
		m_mutex()
	{
		registerReg<uint32_t>(m_intrStatusReg, ADDR_ISR);
		registerReg<uint32_t>(m_intrPendingReg, ADDR_IPR);
		registerReg<uint32_t>(m_intrEnReg, ADDR_IER);
		registerReg<uint32_t>(m_intrAccReg, ADDR_IAR);
		registerReg<uint32_t>(m_setIntrEn, ADDR_SIE);
		registerReg<uint32_t>(m_clearIntrEn, ADDR_CIE);
		registerReg<uint32_t>(m_intrVecReg, ADDR_IVR);
		registerReg<uint32_t>(m_masterEnReg, ADDR_MER);
		registerReg<uint32_t>(m_intrModeReg, ADDR_IMR);
		registerReg<uint32_t>(m_intrLevelReg, ADDR_ILR);

		Reset();

		m_watchDog.RegisterInterruptCallback(std::bind(&AxiInterruptController::CoreInterruptTriggered, this, std::placeholders::_1));
	}

	virtual ~AxiInterruptController()
	{
		Stop();
	}

	void Reset()
	{
		m_intrAccReg.AccnowledgeAllInterrupts();
		m_intrStatusReg.Reset();
		m_intrPendingReg.Reset();
		m_intrEnReg.Reset();
		m_setIntrEn.Reset();
		m_clearIntrEn.Reset();
		m_intrVecReg.Reset();
		m_masterEnReg.Reset();
		m_intrModeReg.Reset();
		m_intrLevelReg.Reset(0xFFFFFFFF);
		m_intrAccReg.AccnowledgeAllInterrupts();
		m_intrAccReg.Reset();
	}

	/// @brief Start the interrupt controller and enable the interrupt line
	/// @param eventNo The event number to use for the interrupt
	///                - for XDMA this is the concat index of the interrupt line
	///                - for PetaLinux this is the ID of the UIO device associated with the core
	/// @return true if the controller was started successfully, false otherwise
	bool Start(const uint32_t& eventNo)
	{
		if (m_running) return true;

		uint32_t intrID = eventNo;

		if(m_detectedInterruptID != -1)
			intrID = static_cast<uint32_t>(m_detectedInterruptID);

		m_watchDog.InitInterrupt(getDevNum(), intrID);

		if (!m_watchDog.Start(true))
		{
			LOG_ERROR << CLASS_TAG("AxiInterruptController") << "Trying to start Controller at: 0x" << std::hex << m_ctrlOffset << " which is already running, stopping startup ..." << std::endl;
			return false;
		}

		return start();
	}

	void Stop()
	{
		if (!m_running) return;
		LOG_INFO << CLASS_TAG("AxiInterruptController") << "Stopping Controller at: 0x" << std::hex << m_ctrlOffset << std::dec << " ... " << std::flush;
		stop();

		m_watchDog.Stop();
		m_watchDog.UnsetInterrupt();

		m_running = false;

		LOG_INFO << "Done" << std::endl;
	}

	void EnableInterrupt(const uint32_t& interruptNum, const bool& enable = true)
	{
		if (interruptNum >= 32)
			throw std::runtime_error("Interrupt number out of range");

		m_intrEnReg.SetBitAt(interruptNum, enable);
	}

	void CoreInterruptTriggered([[maybe_unused]] const uint32_t& mask)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		uint32_t intrs = m_intrStatusReg.GetInterrupts();
		uint32_t idx   = 0;

		LOG_DEBUG << CLASS_TAG("AxiInterruptController") << "CoreInterruptTriggered: " << std::hex << intrs << std::endl;

		try
		{
			while (intrs > 0)
			{
				if (intrs & 1)
				{
					if (m_intrCallbacks.count(idx))
						m_intrCallbacks[idx]();

					m_intrAccReg.AccnowledgeInterrupt(idx);
				}

				idx++;
				intrs >>= 1;
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << std::endl;
		}
	}

	internal::UserInterruptPtr MakeUserInterrupt()
	{
		return std::make_unique<internal::AxiIntrCtrlUserInterrupt>(this);
	}

private:
	bool start()
	{
		if (!m_masterEnReg.GetHardwareInterruptEnable())
			m_masterEnReg.SetHardwareInterruptEnable(true);

		if (!m_masterEnReg.GetMasterIRQEnable())
			m_masterEnReg.SetMasterIRQEnable(true);

		m_running = true;

		return true;
	}

	void stop()
	{
		m_masterEnReg.SetHardwareInterruptEnable(false);
		m_masterEnReg.SetMasterIRQEnable(false);
	}

	void registerIntrCallback(const uint32_t& interruptNum, internal::AxiIntrCtrlCallback callback)
	{
		m_intrCallbacks[interruptNum] = callback;
		EnableInterrupt(interruptNum);
	}

	class Bit32Register : public internal::Register<uint32_t>
	{
	public:
		Bit32Register(const std::string& name) :
			Register(name)
		{
			for (std::size_t i = 0; i < m_bits.size(); i++)
				RegisterElement<bool>(&m_bits[i], "Bit-" + std::to_string(i), static_cast<uint8_t>(i));
		}

		void Reset(const uint32_t& rstVal = 0x0)
		{
			// Initialize each bit with the coresponding bit of the reset value
			for (std::size_t i = 0; i < m_bits.size(); i++)
				m_bits[i] = (rstVal >> i) & 1;

			Update(internal::Direction::WRITE);
		}

		void SetBitAt(const std::size_t& index, const bool& value)
		{
			if (index >= m_bits.size())
				throw std::runtime_error("Index out of range");

			m_bits[index] = value;

			Update(internal::Direction::WRITE);
		}

		bool GetBitAt(const std::size_t& index, const RegUpdate& update = RegUpdate::Update)
		{
			if (index >= m_bits.size())
				throw std::runtime_error("Index out of range");

			if (update == RegUpdate::Update)
				Update(internal::Direction::READ);

			return m_bits[index];
		}

		const std::array<bool, 32>& GetBits(const RegUpdate& update = RegUpdate::Update)
		{
			if (update == RegUpdate::Update)
				Update(internal::Direction::READ);

			return m_bits;
		}

	protected:
		std::array<bool, 32> m_bits = { false };
	};

	class InterruptAcknowledgeRegister : public Bit32Register
	{
	public:
		InterruptAcknowledgeRegister() :
			Bit32Register("Interrupt Acknowledge Register")
		{
		}

		void AccnowledgeInterrupt(const uint32_t& interruptNum)
		{
			SetBitAt(interruptNum, true);

			// Reset the bit to 0 so it does not trigger when a different interrupt is accnowledged
			m_bits[interruptNum] = false;
		}

		void AccnowledgeAllInterrupts()
		{
			for (bool& bit : m_bits)
				bit = true;

			Update(internal::Direction::WRITE);

			// Reset all bits to 0
			for (bool& bit : m_bits)
				bit = false;
		}
	};

	class InterruptStatusRegister : public Bit32Register
	{
		DISABLE_COPY_ASSIGN_MOVE(InterruptStatusRegister)
	public:
		InterruptStatusRegister() :
			Bit32Register("Interrupt Status Register")
		{
		}

		uint32_t GetInterrupts()
		{
			Update();

			uint32_t intr = 0;
			for (std::size_t i = 0; i < m_bits.size(); i++)
				intr |= m_bits[i] << i;

			return intr;
		}
	};

	class MasterEnableRegister : public internal::Register<uint32_t>
	{
	public:
		MasterEnableRegister() :
			Register("Master Enable Register")
		{
			RegisterElement<bool>(&m_me, "Master IRQ Enable", 0);
			RegisterElement<bool>(&m_hie, "Hardware Interrupt Enable", 1);
		}

		void Reset()
		{
			m_me  = false;
			m_hie = false;
			Update(internal::Direction::WRITE);
		}

		void SetHardwareInterruptEnable(const bool& enable)
		{
			m_hie = enable;
			Update(internal::Direction::WRITE);
		}

		void SetMasterIRQEnable(const bool& enable)
		{
			m_me = enable;
			Update(internal::Direction::WRITE);
		}

		bool GetHardwareInterruptEnable() const
		{
			return m_hie;
		}

		bool GetMasterIRQEnable() const
		{
			return m_me;
		}

	private:
		bool m_me  = false;
		bool m_hie = false;
	};

private:
	internal::WatchDog m_watchDog;
	internal::AxiIntrCtrlCallbackMap m_intrCallbacks = {};
	std::mutex m_mutex;
	bool m_running = false;

	InterruptStatusRegister m_intrStatusReg   = InterruptStatusRegister();
	Bit32Register m_intrPendingReg            = Bit32Register("Interrupt Pending Register");
	Bit32Register m_intrEnReg                 = Bit32Register("Interrupt Enable Register");
	InterruptAcknowledgeRegister m_intrAccReg = InterruptAcknowledgeRegister();
	Bit32Register m_setIntrEn                 = Bit32Register("Set Interrupt Enables");
	Bit32Register m_clearIntrEn               = Bit32Register("Clear Interrupt Enables");
	Bit32Register m_intrVecReg                = Bit32Register("Interrupt Vector Register");
	MasterEnableRegister m_masterEnReg        = MasterEnableRegister();
	Bit32Register m_intrModeReg               = Bit32Register("Interrupt Mode Register");
	Bit32Register m_intrLevelReg              = Bit32Register("Interrupt Level Register");
};

namespace internal
{
inline void AxiIntrCtrlUserInterrupt::Init([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, [[maybe_unused]] HasInterrupt* pReg)
{
	m_pReg    = pReg;
	m_devName = "AxiIntrCtrl Intr#" + std::to_string(interruptNum);
	m_pAxiIntC->registerIntrCallback(interruptNum, std::bind(&AxiIntrCtrlUserInterrupt::TriggerInterrupt, this));
}

} // namespace internal

} // namespace clap