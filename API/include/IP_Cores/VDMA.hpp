/* 
 *  File: VDMA.hpp
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

#include "../internal/RegisterControl.hpp"
#include "internal/WatchDog.hpp"

#include <cstdint>

namespace clap
{
// The template defines the address width of the VDMA
// required to read and write input/output addresses
template<typename T>
class VDMA : public internal::RegisterControlBase
{
	DISABLE_COPY_ASSIGN_MOVE(VDMA)

	enum REGISTER_MAP
	{
		MM2S_VDMACR          = 0x00,
		MM2S_VDMASR          = 0x04,
		MM2S_REG_INDEX       = 0x14,
		PARK_PTR_REG         = 0x28,
		VDMA_VERSION         = 0x2C,
		S2MM_VDMACR          = 0x30,
		S2MM_VDMASR          = 0x34,
		S2MM_VDMA_IRQ_MASK   = 0x3C,
		S2MM_REG_INDEX       = 0x44,
		MM2S_VSIZE           = 0x50,
		MM2S_HSIZE           = 0x54,
		MM2S_FRMDLY_STRIDE   = 0x58,
		MM2S_START_ADDRESS   = 0x5C,
		S2MM_VSIZE           = 0xA0,
		S2MM_HSIZE           = 0xA4,
		S2MM_FRMDLY_STRIDE   = 0xA8,
		S2MM_START_ADDRESS   = 0xAC,
		ENABLE_VERTICAL_FLIP = 0xEC
	};

public:
	enum VDMAInterrupts
	{
		VDMA_INTR_ON_FRAME_COUNT = 1 << 0,
		VDMA_INTR_ON_DELAY       = 1 << 1,
		VDMA_INTR_ON_ERROR       = 1 << 2,
		VDMA_INTR_ALL            = (1 << 3) - 1 // All bits set
	};

public:
	VDMA(const CLAPPtr& pClap, const uint64_t& ctrlOffset) :
		RegisterControlBase(pClap, ctrlOffset),
		m_watchDogMM2S("VDMA_MM2S", pClap->MakeUserInterrupt()),
		m_watchDogS2MM("VDMA_S2MM", pClap->MakeUserInterrupt())
	{
		registerReg<uint32_t>(m_mm2sCtrlReg, MM2S_VDMACR);
		registerReg<uint32_t>(m_mm2sStatReg, MM2S_VDMASR);
		registerReg<uint32_t>(m_parkPntrReg, PARK_PTR_REG);
		registerReg<uint32_t>(m_versionReg, VDMA_VERSION);
		registerReg<uint32_t>(m_s2mmCtrlReg, S2MM_VDMACR);
		registerReg<uint32_t>(m_s2mmStatReg, S2MM_VDMASR);
		registerReg<uint32_t>(m_s2mmIrqMask, S2MM_VDMA_IRQ_MASK);
		registerReg<uint32_t>(m_mm2sFDelyStrideReg, MM2S_FRMDLY_STRIDE);
		registerReg<uint32_t>(m_s2mmFDelyStrideReg, S2MM_FRMDLY_STRIDE);

		UpdateAllRegisters();
	}

	////////////////////////////////////////

	// Starts both channels - If the destination [H|V]size is 0 use the respective src size
	void Start(const T& srcAddr, const uint32_t& srcHSize, const uint32_t& srcVSize,
			   const T& dstAddr, const uint32_t& dstHSize = 0, const uint32_t& dstVSize = 0)
	{
		Start(DMAChannel::MM2S, srcAddr, srcHSize, srcVSize);
		Start(DMAChannel::S2MM, dstAddr, (dstHSize == 0 ? srcHSize : dstHSize), (dstVSize == 0 ? srcVSize : dstVSize));
	}

	void Start(const Memory& srcMem, const uint32_t& srcHSize, const uint32_t& srcVSize,
			   const Memory& dstMem, const uint32_t& dstHSize = 0, const uint32_t& dstVSize = 0)
	{
		Start(static_cast<T>(srcMem.GetBaseAddr()), srcHSize, srcVSize,
			  static_cast<T>(dstMem.GetBaseAddr()), dstHSize, dstVSize);
	}

	void Start(const DMAChannel& channel, const Memory& mem, const uint32_t& hSize, const uint32_t& vSize)
	{
		Start(static_cast<T>(mem.GetBaseAddr()), hSize, vSize);
	}

	// Starts the specified channel
	void Start(const DMAChannel& channel, const T& addr, const uint32_t& hSize, const uint32_t& vSize)
	{
		if (channel == DMAChannel::MM2S)
		{
			// Start the watchdog
			if (!m_watchDogMM2S.Start())
			{
				LOG_ERROR << CLASS_TAG("VDMA") << "Trying to start VDMA (MM2S) at: 0x" << std::hex << m_ctrlOffset << " which is still running, stopping startup ..." << std::endl;
				return;
			}

			// Set the RunStop bit
			m_mm2sCtrlReg.Start();
			// Set the source address
			setMM2SSrcAddr(addr);

			// Set the Stride to hSize
			m_mm2sFDelyStrideReg.Stride = hSize;
			m_mm2sFDelyStrideReg.Update(internal::Direction::WRITE);

			// Set the amount of bytes in horizontal direction
			setMM2SHSize(hSize);
			// Set the number of rows
			setMM2SVSize(vSize);
		}

		if (channel == DMAChannel::S2MM)
		{
			// Start the watchdog
			if (!m_watchDogS2MM.Start())
			{
				LOG_ERROR << CLASS_TAG("VDMA") << "Trying to start VDMA (S2MM) at: 0x" << std::hex << m_ctrlOffset << " which is still running, stopping startup ..." << std::endl;
				return;
			}

			// Set the RunStop bit
			m_s2mmCtrlReg.Start();
			// Set the destination address
			setS2MMDestAddr(addr);

			// Set the Stride to hSize
			m_s2mmFDelyStrideReg.Stride = hSize;
			m_s2mmFDelyStrideReg.Update(internal::Direction::WRITE);

			// Set the amount of bytes in horizontal direction
			setS2MMHSize(hSize);
			// Set the number of rows
			setS2MMVSize(vSize);
		}
	}

	// Stops both channel
	void Stop()
	{
		Stop(DMAChannel::MM2S);
		Stop(DMAChannel::S2MM);
	}

	// Stops the specified channel
	void Stop(const DMAChannel& channel)
	{
		// Unset the RunStop bit
		if (channel == DMAChannel::MM2S)
			m_mm2sCtrlReg.Stop();
		else
			m_s2mmCtrlReg.Stop();
	}

	bool WaitForFinish(const DMAChannel& channel, const int32_t& timeoutMS = WAIT_INFINITE)
	{
		if (channel == DMAChannel::MM2S)
		{
			bool state = m_watchDogMM2S.WaitForFinish(timeoutMS);
			// The VDMA automatically restarts as long as it's not stopped so also restart the watchdog
			if (state)
				m_watchDogMM2S.Start();
			return state;
		}
		else
		{
			bool state = m_watchDogS2MM.WaitForFinish(timeoutMS);
			// The VDMA automatically restarts as long as it's not stopped so also restart the watchdog
			if (state)
				m_watchDogS2MM.Start();
			return state;
		}
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void Reset()
	{
		Reset(DMAChannel::MM2S);
		Reset(DMAChannel::S2MM);
	}

	void Reset(const DMAChannel& channel)
	{
		if (channel == DMAChannel::MM2S)
			m_mm2sCtrlReg.DoReset();
		else
			m_s2mmCtrlReg.DoReset();
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void EnableInterrupts(const uint32_t& eventNoMM2S, const uint32_t& eventNoS2MM, const VDMAInterrupts& intr = VDMA_INTR_ALL)
	{
		EnableInterrupts(DMAChannel::MM2S, eventNoMM2S, intr);
		EnableInterrupts(DMAChannel::S2MM, eventNoS2MM, intr);
	}

	void EnableInterrupts(const DMAChannel& channel, const uint32_t& eventNo, const VDMAInterrupts& intr = VDMA_INTR_ALL)
	{
		if (channel == DMAChannel::MM2S)
		{
			m_mm2sCtrlReg.Update();
			m_watchDogMM2S.InitInterrupt(getDevNum(), eventNo, &m_mm2sStatReg);
			m_mm2sCtrlReg.EnableInterrupts(intr);
		}
		else
		{
			m_s2mmCtrlReg.Update();
			m_watchDogS2MM.InitInterrupt(getDevNum(), eventNo, &m_s2mmStatReg);
			m_s2mmCtrlReg.EnableInterrupts(intr);
		}
	}

	void DisableInterrupts(const VDMAInterrupts& intr = VDMA_INTR_ALL)
	{
		DisableInterrupts(DMAChannel::MM2S, intr);
		DisableInterrupts(DMAChannel::S2MM, intr);
	}

	void DisableInterrupts(const DMAChannel& channel, const VDMAInterrupts& intr = VDMA_INTR_ALL)
	{
		if (channel == DMAChannel::MM2S)
		{
			m_watchDogMM2S.UnsetInterrupt();
			m_mm2sCtrlReg.DisableInterrupts(intr);
		}
		else
		{
			m_watchDogS2MM.UnsetInterrupt();
			m_s2mmCtrlReg.DisableInterrupts(intr);
		}
	}

	////////////////////////////////////////

	////////////////////////////////////////

	T GetMM2SSrcAddr()
	{
		return readRegister<T>(MM2S_START_ADDRESS);
	}

	T GetS2MMDestAddr()
	{
		return readRegister<T>(S2MM_START_ADDRESS);
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void SetMM2SStartAddress(const T& addr)
	{
		writeRegister<T>(MM2S_START_ADDRESS, addr);
	}

	void SetS2MMStartAddress(const T& addr)
	{
		writeRegister<T>(S2MM_START_ADDRESS, addr);
	}

	////////////////////////////////////////

	uint16_t GetMM2SVSize()
	{
		return readRegister<uint32_t>(MM2S_VSIZE);
	}

	uint16_t GetS2MMVSize()
	{
		return readRegister<uint32_t>(S2MM_VSIZE);
	}

	////////////////////////////////////////

	////////////////////////////////////////

	uint16_t GetMM2SHSize()
	{
		return readRegister<uint32_t>(MM2S_HSIZE);
	}

	uint16_t GetS2MMHSize()
	{
		return readRegister<uint32_t>(S2MM_HSIZE);
	}

	////////////////////////////////////////

private:
	////////////////////////////////////////

	void setMM2SSrcAddr(const T& addr)
	{
		writeRegister<T>(MM2S_START_ADDRESS, addr);
	}

	void setS2MMDestAddr(const T& addr)
	{
		writeRegister<T>(S2MM_START_ADDRESS, addr);
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void setMM2SVSize(const uint16_t& size)
	{
		writeRegister<uint32_t>(MM2S_VSIZE, size);
	}

	void setS2MMVSize(const uint16_t& size)
	{
		writeRegister<uint32_t>(S2MM_VSIZE, size);
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void setMM2SHSize(const uint16_t& size)
	{
		writeRegister<uint32_t>(MM2S_HSIZE, size);
	}

	void setS2MMHSize(const uint16_t& size)
	{
		writeRegister<uint32_t>(S2MM_HSIZE, size);
	}

	////////////////////////////////////////

public:
	class ControlRegister : public internal::Register<uint32_t>
	{
	public:
		ControlRegister(const std::string& name) :
			Register(name)
		{
			RegisterElement<bool>(&m_rs, "RS", 0);
			RegisterElement<bool>(&m_circularPark, "CircularPark", 1);
			RegisterElement<bool>(&m_reset, "Reset", 2);
			RegisterElement<bool>(&m_genlockEn, "GenlockEn", 3);
			RegisterElement<bool>(&m_frameCntEn, "FrameCntEn", 4);
			RegisterElement<bool>(&m_genlockSrc, "GenlockSrc", 7);
			RegisterElement<bool>(&m_frmCntIrqEn, "FrmCntIrqEn", 12);
			RegisterElement<bool>(&m_dlyCntIrqEn, "DlyCntIrqEn", 13);
			RegisterElement<bool>(&m_errIrqEn, "ErrIrqEn", 14);
			RegisterElement<bool>(&m_repeatEn, "RepeatEn", 15);
			RegisterElement<uint8_t>(&m_irqFrameCount, "IRQFrameCount", 16, 23);
			RegisterElement<uint8_t>(&m_irqDelayCount, "IRQDelayCount", 24, 31);
		}

		void EnableInterrupts(const VDMAInterrupts& intr = VDMA_INTR_ALL)
		{
			setInterrupts(true, intr);
		}

		void DisableInterrupts(const VDMAInterrupts& intr = VDMA_INTR_ALL)
		{
			setInterrupts(false, intr);
		}

		void Start()
		{
			setRunStop(true);
		}

		void Stop()
		{
			setRunStop(false);
		}

		void DoReset()
		{
			Update();
			m_reset = 1;
			Update(internal::Direction::WRITE);

			// The Reset bit will be set to 0 once the reset has been completed
			while (m_reset)
				Update();
		}

	private:
		void setRunStop(bool run)
		{
			// Update the register
			Update();
			// Set/Unset the Run-Stop bit
			m_rs = run;
			// Write changes to the register
			Update(internal::Direction::WRITE);
		}

		void setInterrupts(bool enable, const VDMAInterrupts& intr)
		{
			if (intr & VDMA_INTR_ON_FRAME_COUNT)
				m_frmCntIrqEn = enable;
			if (intr & VDMA_INTR_ON_DELAY)
				m_dlyCntIrqEn = enable;
			if (intr & VDMA_INTR_ON_ERROR)
				m_errIrqEn = enable;

			Update(internal::Direction::WRITE);
		}

	private:
		bool m_rs               = false;
		bool m_circularPark     = false;
		bool m_reset            = false;
		bool m_genlockEn        = false;
		bool m_frameCntEn       = false;
		bool m_genlockSrc       = false;
		bool m_frmCntIrqEn      = false;
		bool m_dlyCntIrqEn      = false;
		bool m_errIrqEn         = false;
		bool m_repeatEn         = false;
		uint8_t m_irqFrameCount = 0;
		uint8_t m_irqDelayCount = 0;
	};

	// TODO: Currently finish is only detected using interrupts, add a way to poll for it
	class StatusRegister : public internal::Register<uint32_t>, public internal::HasInterrupt
	{
	public:
		StatusRegister(const std::string& name) :
			Register(name)
		{
			RegisterElement<bool>(&m_halted, "Halted", 0);
			RegisterElement<bool>(&m_vdmaIntErr, "VDMAIntErr", 4);
			RegisterElement<bool>(&m_vdmaSlvErr, "VDMASlvErr", 5);
			RegisterElement<bool>(&m_vdmaDecErr, "VDMADecErr", 6);
			RegisterElement<bool>(&m_sofEarlyErr, "SOFEarlyErr", 7);
			RegisterElement<bool>(&m_frmCntIrq, "FrmCntIrq", 12);
			RegisterElement<bool>(&m_dlyCntIrq, "DlyCntIrq", 13);
			RegisterElement<bool>(&m_errIrq, "ErrIrq", 14);
			RegisterElement<uint8_t>(&m_irqFrameCntSts, "IRQFrameCntSts", 16, 23);
			RegisterElement<uint8_t>(&m_irqDelayCntSts, "IRQDelayCntSts", 24, 31);
		}

		void ClearInterrupts()
		{
			m_lastInterrupt = GetInterrupts();
			ResetInterrupts(VDMA_INTR_ALL);
		}

		uint32_t GetInterrupts()
		{
			Update();
			uint32_t intr = 0;
			intr |= m_frmCntIrq << (VDMA_INTR_ON_FRAME_COUNT >> 1);
			intr |= m_dlyCntIrq << (VDMA_INTR_ON_DELAY >> 1);
			intr |= m_errIrq << (VDMA_INTR_ON_ERROR >> 1);

			return intr;
		}

		void ResetInterrupts(const VDMAInterrupts& intr)
		{
			if (intr & VDMA_INTR_ON_FRAME_COUNT)
				m_frmCntIrq = 1;
			if (intr & VDMA_INTR_ON_DELAY)
				m_dlyCntIrq = 1;
			if (intr & VDMA_INTR_ON_ERROR)
				m_errIrq = 1;

			Update(internal::Direction::WRITE);
		}

	private:
		bool m_halted            = false;
		bool m_vdmaIntErr        = false;
		bool m_vdmaSlvErr        = false;
		bool m_vdmaDecErr        = false;
		bool m_sofEarlyErr       = false;
		bool m_frmCntIrq         = false;
		bool m_dlyCntIrq         = false;
		bool m_errIrq            = false;
		uint8_t m_irqFrameCntSts = 0;
		uint8_t m_irqDelayCntSts = 0;
	};

	class MM2SControlRegister : public ControlRegister
	{
	public:
		MM2SControlRegister() :
			ControlRegister("MM2S Control Register")
		{
			// Reference to base class is require due to template inheritance
			internal::Register<uint32_t>::RegisterElement<uint8_t>(&m_rdPntrNum, "RdPntrNum", 8, 11);
		}

	private:
		uint8_t m_rdPntrNum = 0;
	};

	class MM2SStatusRegister : public StatusRegister
	{
	public:
		MM2SStatusRegister() :
			StatusRegister("MM2S Status Register")
		{}
	};

	class S2MMControlRegister : public ControlRegister
	{
	public:
		S2MMControlRegister() :
			ControlRegister("S2MM Control Register")
		{
			internal::Register<uint32_t>::RegisterElement<uint8_t>(&m_wrPntrNum, "WrPntrNum", 8, 11);
		}

	private:
		uint8_t m_wrPntrNum = 0;
	};

	class S2MMStatusRegister : public StatusRegister
	{
	public:
		S2MMStatusRegister() :
			StatusRegister("S2MM Status Register")
		{
			internal::Register<uint32_t>::RegisterElement<bool>(&m_eolEarlyErr, "EOLEarlyErr", 8);
			internal::Register<uint32_t>::RegisterElement<bool>(&m_sofLateErr, "SOFLateErr", 11);
			internal::Register<uint32_t>::RegisterElement<bool>(&m_eolLateErr, "EOLLateErr", 15);
		}

	private:
		bool m_eolEarlyErr = false;
		bool m_sofLateErr  = false;
		bool m_eolLateErr  = false;
	};

	class S2MMIrqMask : public internal::Register<uint32_t>
	{
	public:
		S2MMIrqMask() :
			Register("S2MM IRQ Mask")
		{
			RegisterElement<bool>(&m_irqMaskSOFEarlyErr, "IRQMaskSOFEarlyErr", 0);
			RegisterElement<bool>(&m_irqMaskEOLEarlyErr, "IRQMaskEOLEarlyErr", 1);
			RegisterElement<bool>(&m_irqMaskSOFLateErr, "IRQMaskSOFLateErr", 2);
			RegisterElement<bool>(&m_irqMaskEOLLateErr, "IRQMaskEOLLateErr", 3);
		}

	private:
		bool m_irqMaskSOFEarlyErr = false;
		bool m_irqMaskEOLEarlyErr = false;
		bool m_irqMaskSOFLateErr  = false;
		bool m_irqMaskEOLLateErr  = false;
	};

	class MM2SFrameDelayStrideRegister : public internal::Register<uint32_t>
	{
	public:
		MM2SFrameDelayStrideRegister() :
			Register("MM2S FrameDelay & Stride Register")
		{
			RegisterElement<uint16_t>(&m_stride, "Stride", 0, 15);
			RegisterElement<uint8_t>(&m_frameDelay, "FrameDelay", 24, 28);
		}

	private:
		uint16_t m_stride    = 0;
		uint8_t m_frameDelay = 0;
	};

	class SS2MFrameDelayStrideRegister : public internal::Register<uint32_t>
	{
	public:
		SS2MFrameDelayStrideRegister() :
			Register("SS2M FrameDelay & Stride Register")
		{
			RegisterElement<uint16_t>(&m_stride, "Stride", 0, 15);
			RegisterElement<uint8_t>(&m_frameDelay, "FrameDelay", 24, 28);
		}

	private:
		uint16_t m_stride    = 0;
		uint8_t m_frameDelay = 0;
	};

	class ParkPointerRegister : public internal::Register<uint32_t>
	{
	public:
		ParkPointerRegister() :
			Register("Park Pointer Register")
		{
			RegisterElement<uint8_t>(&m_rdFrmPtrRef, "RdFrmPtrRef", 0, 4);
			RegisterElement<uint8_t>(&m_wrFrmPtrRef, "WrFrmPtrRef", 8, 12);
			RegisterElement<uint8_t>(&m_rdFramStore, "RdFramStore", 16, 20);
			RegisterElement<uint8_t>(&m_wrFramStore, "WrFramStore", 24, 28);
		}

	private:
		uint8_t m_rdFrmPtrRef = 0;
		uint8_t m_wrFrmPtrRef = 0;
		uint8_t m_rdFramStore = 0;
		uint8_t m_wrFramStore = 0;
	};

	class VDMAVersionRegister : public internal::Register<uint32_t>
	{
	public:
		VDMAVersionRegister() :
			Register("VDMA Version Register")
		{
			RegisterElement<uint16_t>(&m_xilinxInternal, "XilinxInternal", 0, 15);
			RegisterElement<uint8_t>(&m_minorVersion, "MinorVersion", 20, 27);
			RegisterElement<uint8_t>(&m_majorVersion, "MajorVersion", 28, 31);
		}

	private:
		uint16_t m_xilinxInternal = 0;
		uint8_t m_minorVersion    = 0;
		uint8_t m_majorVersion    = 0;
	};

public:
	MM2SControlRegister m_mm2sCtrlReg                 = MM2SControlRegister();
	MM2SStatusRegister m_mm2sStatReg                  = MM2SStatusRegister();
	ParkPointerRegister m_parkPntrReg                 = ParkPointerRegister();
	VDMAVersionRegister m_versionReg                  = VDMAVersionRegister();
	S2MMControlRegister m_s2mmCtrlReg                 = S2MMControlRegister();
	S2MMStatusRegister m_s2mmStatReg                  = S2MMStatusRegister();
	S2MMIrqMask m_s2mmIrqMask                         = S2MMIrqMask();
	MM2SFrameDelayStrideRegister m_mm2sFDelyStrideReg = MM2SFrameDelayStrideRegister();
	SS2MFrameDelayStrideRegister m_s2mmFDelyStrideReg = SS2MFrameDelayStrideRegister();

private:
	internal::WatchDog m_watchDogMM2S;
	internal::WatchDog m_watchDogS2MM;
};
} // namespace clap