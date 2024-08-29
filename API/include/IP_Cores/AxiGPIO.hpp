/*
 *  File: AxiGPIO.hpp
 *  Copyright (c) 2024 Florian Porrmann
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
#include "internal/WatchDog.hpp"

#include "AxiInterruptController.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

// TODO: Implement non-interrupt status check

namespace clap
{
class AxiGPIO : public internal::RegisterControlBase
{
public:
	enum GPIOInterrupts
	{
		INTR_ON_CH1 = 1 << 0,
		INTR_ON_CH2 = 1 << 1,
		INTR_ALL    = (1 << 2) - 1 // All bits set
	};

	enum Channel
	{
		CHANNEL_1 = 0,
		CHANNEL_2 = 1
	};

	enum PortStates
	{
		OUTPUT = 0,
		INPUT  = 1
	};

	enum class DualChannel : bool
	{
		Yes = true,
		No  = false
	};

	enum class ResetOnInit : bool
	{
		Yes = true,
		No  = false
	};

	using IntrCallback = std::function<void(const Channel&, const uint32_t&, const bool&)>;

private:
	DISABLE_COPY_ASSIGN_MOVE(AxiGPIO)

	enum REGISTER_MAP
	{
		ADDR_GPIO_DATA  = 0x00,
		ADDR_GPIO_TRI   = 0x04,
		ADDR_GPIO2_DATA = 0x08,
		ADDR_GPIO2_TRI  = 0x0C,
		ADDR_GIER       = 0x11C,
		ADDR_IP_IER     = 0x128,
		ADDR_IP_ISR     = 0x120
	};

public:
	AxiGPIO(const CLAPPtr& pClap, const uint64_t& ctrlOffset, const ResetOnInit& resetOnInit) :
		AxiGPIO(pClap, ctrlOffset, DualChannel::No, resetOnInit)
	{
	}

	AxiGPIO(const CLAPPtr& pClap, const uint64_t& ctrlOffset, const DualChannel& dualChannel = DualChannel::No, const ResetOnInit& resetOnInit = ResetOnInit::Yes) :
		RegisterControlBase(pClap, ctrlOffset),
		m_watchDog("AxiGPIO", pClap->MakeUserInterrupt()),
		m_isDualChannel((dualChannel == DualChannel::Yes))
	{
		registerReg<uint32_t>(m_gpio1Data, ADDR_GPIO_DATA);
		registerReg<uint32_t>(m_gpio1Tri, ADDR_GPIO_TRI);
		registerReg<uint32_t>(m_gpio2Data, ADDR_GPIO2_DATA);
		registerReg<uint32_t>(m_gpio2Tri, ADDR_GPIO2_TRI);
		registerReg<uint32_t>(m_globalIntrEn, ADDR_GIER);

		registerReg<uint32_t>(m_ipIntrEn, ADDR_IP_IER);
		registerReg<uint32_t>(m_ipIntrStatus, ADDR_IP_ISR);

		m_watchDog.SetFinishCallback(std::bind(&AxiGPIO::OnFinished, this));
		m_watchDog.RegisterInterruptCallback(std::bind(&AxiGPIO::InterruptTriggered, this, std::placeholders::_1));

		detectDualChannel();
		detectGPIOWidth();
		detectDataDefaultValues();
		detectTriDefaultValues();

		if (resetOnInit == ResetOnInit::Yes)
			Reset();
	}

	virtual ~AxiGPIO() override
	{
		Stop();
	}

	void Reset()
	{
		m_gpio1Data.Reset(m_dataDefaultValues[0]);
		m_gpio1Tri.Reset(m_triDefaultValues[0]);
		m_gpio2Data.Reset(m_dataDefaultValues[1]);
		m_gpio2Tri.Reset(m_triDefaultValues[1]);
		m_globalIntrEn.Reset();
		m_ipIntrEn.Reset();
		m_ipIntrStatus.Reset();
	}

	void SetDualChannel(const bool& dualChannel)
	{
		m_isDualChannel = dualChannel;
	}

	void SetGPIOWidth(const Channel& channel, const uint32_t& width)
	{
		if (channel == CHANNEL_1)
			m_gpioWidth[0] = width;
		else if (channel == CHANNEL_2)
			m_gpioWidth[1] = width;
	}

	void SetGPIOWidths(const std::array<uint32_t, 2>& widths)
	{
		m_gpioWidth = widths;
	}

	void SetTriStateDefaultValue(const Channel& channel, const uint32_t& value)
	{
		if (channel == CHANNEL_1)
			m_triDefaultValues[0] = value;
		else if (channel == CHANNEL_2)
			m_triDefaultValues[1] = value;
	}

	void SetTriStateDefaultValues(const std::array<uint32_t, 2>& values)
	{
		m_triDefaultValues = values;
	}

	void SetDataDefaultValue(const Channel& channel, const uint32_t& value)
	{
		if (channel == CHANNEL_1)
			m_dataDefaultValues[0] = value;
		else if (channel == CHANNEL_2)
			m_dataDefaultValues[1] = value;
	}

	void SetDataDefaultValues(const std::array<uint32_t, 2>& values)
	{
		m_dataDefaultValues = values;
	}

	void EnableInterrupts(const uint32_t& eventNo = USE_AUTO_DETECT, const GPIOInterrupts& intr = INTR_ALL)
	{
		uint32_t intrID = eventNo;

		if (m_detectedInterruptID != -1)
			intrID = static_cast<uint32_t>(m_detectedInterruptID);

		m_globalIntrEn.SetGlobalInterruptEnable(true);

		m_watchDog.InitInterrupt(getDevNum(), intrID, &m_ipIntrStatus);
		m_ipIntrEn.EnableInterrupt(intr);
	}

	void UseInterruptController(AxiInterruptController& axiIntC)
	{
		m_watchDog.SetUserInterrupt(axiIntC.MakeUserInterrupt());
	}

	void RegisterInterruptCallback(const IntrCallback& callback)
	{
		m_callbacks.push_back(callback);
	}

	bool OnFinished()
	{
		if (m_running)
			return false;
		else
			return true;
	}

	bool Start()
	{
		if (m_running) return true;

		if (m_watchDog && !m_watchDog.Start(true))
		{
			CLAP_CLASS_LOG_ERROR << "Trying to start GPIO at: 0x" << std::hex << m_ctrlOffset << " which is already running, stopping startup ..." << std::endl;
			return false;
		}

		m_running = true;

		return true;
	}

	void Stop()
	{
		if (!m_running) return;
		CLAP_CLASS_LOG_INFO << "Stopping GPIO at: 0x" << std::hex << m_ctrlOffset << std::dec << " ... " << std::flush;

		m_watchDog.Stop();
		m_watchDog.UnsetInterrupt();

		m_running = false;

		CLAP_LOG_INFO << "Done" << std::endl;
	}

	void SetGPIOState(const Channel& channel, const uint32_t& port, const PortStates& state)
	{
		if (port > m_gpioWidth[channel]) return;

		if (channel == CHANNEL_1)
			m_gpio1Tri.SetBitAt(port, state);
		else if (channel == CHANNEL_2 && m_isDualChannel)
			m_gpio2Tri.SetBitAt(port, state);
	}

	uint8_t GetGPIOBit(const Channel& channel, const uint32_t& port)
	{
		if (port > m_gpioWidth[channel]) return -1;

		if (channel == CHANNEL_1)
			return m_gpio1Data.GetBitAt(port);
		else if (channel == CHANNEL_2 && m_isDualChannel)
			return m_gpio2Data.GetBitAt(port);

		return -1;
	}

	uint32_t GetGPIOBits(const Channel& channel)
	{
		if (channel == CHANNEL_1)
			return m_gpio1Data.ToUint32();
		else if (channel == CHANNEL_2 && m_isDualChannel)
			return m_gpio2Data.ToUint32();

		return 0;
	}

	void SetGPIOBit(const Channel& channel, const uint32_t& port, const bool& value)
	{
		if (port > m_gpioWidth[channel]) return;

		if (channel == CHANNEL_1)
			m_gpio1Data.SetBitAt(port, value);
		else if (channel == CHANNEL_2 && m_isDualChannel)
			m_gpio2Data.SetBitAt(port, value);
	}

	void SetGPIOBits(const Channel& channel, const uint32_t& value)
	{
		if (channel == CHANNEL_1)
			m_gpio1Data.SetBits(value);
		else if (channel == CHANNEL_2 && m_isDualChannel)
			m_gpio2Data.SetBits(value);
	}

	void InterruptTriggered([[maybe_unused]] const uint32_t& mask)
	{
		if (mask & INTR_ON_CH1)
			runCallbacks(CHANNEL_1);
		if (mask & INTR_ON_CH2 && m_isDualChannel)
			runCallbacks(CHANNEL_2);
	}

private:
	void runCallbacks(const Channel& channel)
	{
		Bit32Arr changes = getGPIOChanges(channel);

		for (const auto& callback : m_callbacks)
		{
			for (uint32_t i = 0; i < changes.size(); i++)
			{
				if (changes[i])
					callback(channel, i, GetGPIOBit(channel, i));
			}
		}
	}

	Bit32Arr getGPIOChanges(const Channel& channel)
	{
		Bit32Arr changes = {};
		Bit32Arr oldValue;
		Bit32Arr newValue;

		if (channel == CHANNEL_1)
		{
			oldValue = m_gpio1Data.GetBits(internal::Bit32Register::RegUpdate::NoUpdate);
			newValue = m_gpio1Data.GetBits();
		}
		else if (channel == CHANNEL_2)
		{
			oldValue = m_gpio2Data.GetBits(internal::Bit32Register::RegUpdate::NoUpdate);
			newValue = m_gpio2Data.GetBits();
		}

		std::transform(std::begin(oldValue), std::end(oldValue), std::begin(newValue), std::begin(changes), std::bit_xor<bool>());

		return changes;
	}

	void detectDualChannel()
	{
		Expected<uint64_t> res = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,is-dual");
		if (res)
		{
			m_isDualChannel = (static_cast<uint32_t>(res.Value()) != 0);
			CLAP_CLASS_LOG_INFO << "Detected dual channel mode: " << (m_isDualChannel ? "ON" : "OFF") << std::endl;
		}
	}

	void detectGPIOWidth()
	{
		Expected<uint64_t> res = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,gpio-width");
		if (res)
		{
			m_gpioWidth[0] = static_cast<uint32_t>(res.Value());
			CLAP_CLASS_LOG_INFO << "Detected GPIO width for channel 1: " << m_gpioWidth[0] << std::endl;
		}

		if (m_isDualChannel)
		{
			// The shadowing here is intentional, as Expected currently does not allow overwriting the value
			Expected<uint64_t> res2 = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,gpio2-width");
			if (res2)
			{
				m_gpioWidth[1] = static_cast<uint32_t>(res2.Value());
				CLAP_CLASS_LOG_INFO << "Detected GPIO width for channel 2: " << m_gpioWidth[1] << std::endl;
			}
		}
	}

	void detectTriDefaultValues()
	{
		Expected<uint64_t> res = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,tri-default");
		if (res)
		{
			m_triDefaultValues[0] = static_cast<uint32_t>(res.Value());
			CLAP_CLASS_LOG_INFO << "Detected GPIO tri state default for channel 1: 0x" << std::hex << m_triDefaultValues[0] << std::dec << std::endl;
		}

		if (m_isDualChannel)
		{
			// The shadowing here is intentional, as Expected currently does not allow overwriting the value
			Expected<uint64_t> res2 = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,tri-default-2");
			if (res2)
			{
				m_triDefaultValues[1] = static_cast<uint32_t>(res2.Value());
				CLAP_CLASS_LOG_INFO << "Detected GPIO tri state default for channel 2: 0x" << std::hex << m_triDefaultValues[1] << std::dec << std::endl;
			}
		}
	}

	void detectDataDefaultValues()
	{
		Expected<uint64_t> res = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,dout-default");
		if (res)
		{
			m_dataDefaultValues[0] = static_cast<uint32_t>(res.Value());
			CLAP_CLASS_LOG_INFO << "Detected GPIO data default for channel 1: 0x" << std::hex << m_dataDefaultValues[0] << std::dec << std::endl;
		}

		if (m_isDualChannel)
		{
			// The shadowing here is intentional, as Expected currently does not allow overwriting the value
			Expected<uint64_t> res2 = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,dout-default-2");
			if (res2)
			{
				m_dataDefaultValues[1] = static_cast<uint32_t>(res2.Value());
				CLAP_CLASS_LOG_INFO << "Detected GPIO data default for channel 2: 0x" << std::hex << m_dataDefaultValues[1] << std::dec << std::endl;
			}
		}
	}

	class IPInterruptEnableRegister : public internal::Bit32Register
	{
		DISABLE_COPY_ASSIGN_MOVE(IPInterruptEnableRegister)
	public:
		IPInterruptEnableRegister() :
			Bit32Register("IP Interrupt Enable Register")
		{
		}

		void EnableInterrupt(const GPIOInterrupts& intr = INTR_ALL)
		{
			if (intr == 0) return;

			if (intr & INTR_ON_CH1)
				m_bits[0] = true;

			if (intr & INTR_ON_CH2)
				m_bits[1] = true;

			Update(internal::Direction::WRITE);
		}

		void DisableInterrupt(const GPIOInterrupts& intr = INTR_ALL)
		{
			if (intr == 0) return;

			if (intr & INTR_ON_CH1)
				m_bits[0] = false;

			if (intr & INTR_ON_CH2)
				m_bits[1] = false;

			Update(internal::Direction::WRITE);
		}
	};

	class IPInterruptStatusRegister : public internal::Bit32Register, public internal::HasInterrupt
	{
		DISABLE_COPY_ASSIGN_MOVE(IPInterruptStatusRegister)
	public:
		IPInterruptStatusRegister() :
			Bit32Register("IP Interrupt Status Register")
		{
			// Do an initial clear to discard old interrupts
			clearInterrupts();
			m_lastInterrupt = 0;
		}

		void ClearInterrupts() override
		{
			clearInterrupts();
		}

		uint32_t GetInterrupts() override
		{
			return getInterrupts();
		}

		void ResetInterrupts(const GPIOInterrupts& intr)
		{
			if (intr & INTR_ON_CH1)
				m_bits[0] = true;
			if (intr & INTR_ON_CH2)
				m_bits[1] = true;

			Update(internal::Direction::WRITE);
		}

	private:
		void clearInterrupts()
		{
			m_lastInterrupt = getInterrupts();
			ResetInterrupts(INTR_ALL);
		}

		uint32_t getInterrupts()
		{
			Update();
			uint32_t intr = 0;
			intr |= m_bits[0] << (INTR_ON_CH1 >> 1);
			intr |= m_bits[1] << (INTR_ON_CH2 >> 1);

			return intr;
		}
	};

	class GlobalInterruptEnableRegister : public internal::Register<uint32_t>
	{
	public:
		GlobalInterruptEnableRegister() :
			Register("Global Interrupt Enable Register")
		{
			RegisterElement<bool>(&m_gie, "Master IRQ Enable", 31);
		}

		void Reset()
		{
			m_gie = false;
			Update(internal::Direction::WRITE);
		}

		void SetGlobalInterruptEnable(const bool& enable)
		{
			m_gie = enable;
			Update(internal::Direction::WRITE);
		}

		bool GetGlobalInterruptEnable() const
		{
			return m_gie;
		}

	private:
		bool m_gie = false;
	};

private:
	internal::WatchDog m_watchDog;
	bool m_running = false;

	internal::Bit32Register m_gpio1Data = internal::Bit32Register("GPIO 1 Data Register");
	internal::Bit32Register m_gpio1Tri  = internal::Bit32Register("GPIO 1 Tri Register");
	internal::Bit32Register m_gpio2Data = internal::Bit32Register("GPIO 2 Data Register");
	internal::Bit32Register m_gpio2Tri  = internal::Bit32Register("GPIO 2 Tri Register");

	GlobalInterruptEnableRegister m_globalIntrEn = GlobalInterruptEnableRegister();

	IPInterruptEnableRegister m_ipIntrEn     = IPInterruptEnableRegister();
	IPInterruptStatusRegister m_ipIntrStatus = IPInterruptStatusRegister();

	std::vector<IntrCallback> m_callbacks = {};
	bool m_isDualChannel;
	std::array<uint32_t, 2> m_gpioWidth         = { 32, 32 };
	std::array<uint32_t, 2> m_triDefaultValues  = { 0xFFFFFFFF, 0xFFFFFFFF };
	std::array<uint32_t, 2> m_dataDefaultValues = { 0x0, 0x0 };
};
} // namespace clap
