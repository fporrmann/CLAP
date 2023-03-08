/* 
 *  File: VDMA.h
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

#include "internal/IPControl.h"
#include "internal/RegisterInterface.h"
#include "internal/WatchDog.h"

enum VDMAInterrupts
{
	VDMA_INTR_ON_FRAME_COUNT = 1 << 0,
	VDMA_INTR_ON_DELAY       = 1 << 1,
	VDMA_INTR_ON_ERROR       = 1 << 2,
	VDMA_INTR_ALL            = (1 << 3) - 1 // All bits set
};

// The template defines the address width of the VDMA
// required to read and write input/output addresses
template<typename T>
class VDMA : public IPControlBase
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
	VDMA(class XDMA* pXdma, const uint64_t& ctrlOffset) :
		IPControlBase(pXdma, ctrlOffset),
		m_mm2sCtrlReg(),
		m_mm2sStatReg(),
		m_parkPntrReg(),
		m_versionReg(),
		m_s2mmCtrlReg(),
		m_s2mmStatReg(),
		m_s2mmIrqMask(),
		m_mm2sFDelyStrideReg(),
		m_s2mmFDelyStrideReg(),
		m_watchDogMM2S("VDMA_MM2S"),
		m_watchDogS2MM("VDMA_S2MM")
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
				std::cerr << CLASS_TAG("VDMA") << "Trying to start VDMA (MM2S) at: 0x" << std::hex << m_ctrlOffset << " which is still running, stopping startup ..." << std::endl;
				return;
			}

			// Set the RunStop bit
			m_mm2sCtrlReg.Start();
			// Set the source address
			setMM2SSrcAddr(addr);

			// Set the Stride to hSize
			m_mm2sFDelyStrideReg.Stride = hSize;
			m_mm2sFDelyStrideReg.Update(Direction::WRITE);

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
				std::cerr << CLASS_TAG("VDMA") << "Trying to start VDMA (S2MM) at: 0x" << std::hex << m_ctrlOffset << " which is still running, stopping startup ..." << std::endl;
				return;
			}

			// Set the RunStop bit
			m_s2mmCtrlReg.Start();
			// Set the destination address
			setS2MMDestAddr(addr);

			// Set the Stride to hSize
			m_s2mmFDelyStrideReg.Stride = hSize;
			m_s2mmFDelyStrideReg.Update(Direction::WRITE);

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
	struct ControlRegister : public Register<uint32_t>
	{
		ControlRegister(const std::string& name) :
			Register(name),
			RS(0),
			CircularPark(0),
			Reset(0),
			GenlockEn(0),
			FrameCntEn(0),
			GenlockSrc(0),
			FrmCntIrqEn(0),
			DlyCntIrqEn(0),
			ErrIrqEn(0),
			RepeatEn(0),
			IRQFrameCount(0),
			IRQDelayCount(0)
		{
			RegisterElement<bool>(&RS, "RS", 0);
			RegisterElement<bool>(&CircularPark, "CircularPark", 1);
			RegisterElement<bool>(&Reset, "Reset", 2);
			RegisterElement<bool>(&GenlockEn, "GenlockEn", 3);
			RegisterElement<bool>(&FrameCntEn, "FrameCntEn", 4);
			RegisterElement<bool>(&GenlockSrc, "GenlockSrc", 7);
			RegisterElement<bool>(&FrmCntIrqEn, "FrmCntIrqEn", 12);
			RegisterElement<bool>(&DlyCntIrqEn, "DlyCntIrqEn", 13);
			RegisterElement<bool>(&ErrIrqEn, "ErrIrqEn", 14);
			RegisterElement<bool>(&RepeatEn, "RepeatEn", 15);
			RegisterElement<uint8_t>(&IRQFrameCount, "IRQFrameCount", 16, 23);
			RegisterElement<uint8_t>(&IRQDelayCount, "IRQDelayCount", 24, 31);
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
			Reset = 1;
			Update(Direction::WRITE);

			// The Reset bit will be set to 0 once the reset has been completed
			while (Reset)
				Update();
		}

	private:
		void setRunStop(bool run)
		{
			// Update the register
			Update();
			// Set/Unset the Run-Stop bit
			RS = run;
			// Write changes to the register
			Update(Direction::WRITE);
		}

		void setInterrupts(bool enable, const VDMAInterrupts& intr)
		{
			if (intr & VDMA_INTR_ON_FRAME_COUNT)
				FrmCntIrqEn = enable;
			if (intr & VDMA_INTR_ON_DELAY)
				DlyCntIrqEn = enable;
			if (intr & VDMA_INTR_ON_ERROR)
				ErrIrqEn = enable;

			Update(Direction::WRITE);
		}

	public:
		bool RS;
		bool CircularPark;
		bool Reset;
		bool GenlockEn;
		bool FrameCntEn;
		bool GenlockSrc;
		bool FrmCntIrqEn;
		bool DlyCntIrqEn;
		bool ErrIrqEn;
		bool RepeatEn;
		uint8_t IRQFrameCount;
		uint8_t IRQDelayCount;
	};

	struct StatusRegister : public Register<uint32_t>, public HasInterrupt
	{
		StatusRegister(const std::string& name) :
			Register(name),
			Halted(0),
			VDMAIntErr(0),
			VDMASlvErr(0),
			VDMADecErr(0),
			SOFEarlyErr(0),
			FrmCntIrq(0),
			DlyCntIrq(0),
			ErrIrq(0),
			IRQFrameCntSts(0),
			IRQDelayCntSts(0)
		{
			RegisterElement<bool>(&Halted, "Halted", 0);
			RegisterElement<bool>(&VDMAIntErr, "VDMAIntErr", 4);
			RegisterElement<bool>(&VDMASlvErr, "VDMASlvErr", 5);
			RegisterElement<bool>(&VDMADecErr, "VDMADecErr", 6);
			RegisterElement<bool>(&SOFEarlyErr, "SOFEarlyErr", 7);
			RegisterElement<bool>(&FrmCntIrq, "FrmCntIrq", 12);
			RegisterElement<bool>(&DlyCntIrq, "DlyCntIrq", 13);
			RegisterElement<bool>(&ErrIrq, "ErrIrq", 14);
			RegisterElement<uint8_t>(&IRQFrameCntSts, "IRQFrameCntSts", 16, 23);
			RegisterElement<uint8_t>(&IRQDelayCntSts, "IRQDelayCntSts", 24, 31);
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
			intr |= FrmCntIrq << (VDMA_INTR_ON_FRAME_COUNT >> 1);
			intr |= DlyCntIrq << (VDMA_INTR_ON_DELAY >> 1);
			intr |= ErrIrq << (VDMA_INTR_ON_ERROR >> 1);

			return intr;
		}

		void ResetInterrupts(const VDMAInterrupts& intr)
		{
			if (intr & VDMA_INTR_ON_FRAME_COUNT)
				FrmCntIrq = 1;
			if (intr & VDMA_INTR_ON_DELAY)
				DlyCntIrq = 1;
			if (intr & VDMA_INTR_ON_ERROR)
				ErrIrq = 1;

			Update(Direction::WRITE);
		}

		bool Halted;
		bool VDMAIntErr;
		bool VDMASlvErr;
		bool VDMADecErr;
		bool SOFEarlyErr;
		bool FrmCntIrq;
		bool DlyCntIrq;
		bool ErrIrq;
		uint8_t IRQFrameCntSts;
		uint8_t IRQDelayCntSts;
	};

	struct MM2SControlRegister : public ControlRegister
	{
		MM2SControlRegister() :
			ControlRegister("MM2S Control Register"),
			RdPntrNum(0)
		{
			// Reference to base class is require due to template inheritance
			Register<uint32_t>::RegisterElement<uint8_t>(&RdPntrNum, "RdPntrNum", 8, 11);
		}

		uint8_t RdPntrNum;
	};

	struct MM2SStatusRegister : public StatusRegister
	{
		MM2SStatusRegister() :
			StatusRegister("MM2S Status Register")
			{}
	};

	struct S2MMControlRegister : public ControlRegister
	{
		S2MMControlRegister() :
			ControlRegister("S2MM Control Register"),
			WrPntrNum(0)
		{
			Register<uint32_t>::RegisterElement<uint8_t>(&WrPntrNum, "WrPntrNum", 8, 11);
		}

		uint8_t WrPntrNum;
	};

	struct S2MMStatusRegister : public StatusRegister
	{
		S2MMStatusRegister() :
			StatusRegister("S2MM Status Register"),
			EOLEarlyErr(0),
			SOFLateErr(0),
			EOLLateErr(0)
		{
			Register<uint32_t>::RegisterElement<bool>(&EOLEarlyErr, "EOLEarlyErr", 8);
			Register<uint32_t>::RegisterElement<bool>(&SOFLateErr, "SOFLateErr", 11);
			Register<uint32_t>::RegisterElement<bool>(&EOLLateErr, "EOLLateErr", 15);
		}

		bool EOLEarlyErr;
		bool SOFLateErr;
		bool EOLLateErr;
	};

	struct S2MMIrqMask : public Register<uint32_t>
	{
		S2MMIrqMask() :
			Register("S2MM IRQ Mask"),
			IRQMaskSOFEarlyErr(0),
			IRQMaskEOLEarlyErr(0),
			IRQMaskSOFLateErr(0),
			IRQMaskEOLLateErr(0)
		{
			RegisterElement<bool>(&IRQMaskSOFEarlyErr, "IRQMaskSOFEarlyErr", 0);
			RegisterElement<bool>(&IRQMaskEOLEarlyErr, "IRQMaskEOLEarlyErr", 1);
			RegisterElement<bool>(&IRQMaskSOFLateErr, "IRQMaskSOFLateErr", 2);
			RegisterElement<bool>(&IRQMaskEOLLateErr, "IRQMaskEOLLateErr", 3);
		}

		bool IRQMaskSOFEarlyErr;
		bool IRQMaskEOLEarlyErr;
		bool IRQMaskSOFLateErr;
		bool IRQMaskEOLLateErr;
	};

	struct MM2SFrameDelayStrideRegister : public Register<uint32_t>
	{
		MM2SFrameDelayStrideRegister() :
			Register("MM2S FrameDelay & Stride Register"),
			Stride(0), FrameDelay(0)
		{
			RegisterElement<uint16_t>(&Stride, "Stride", 0, 15);
			RegisterElement<uint8_t>(&FrameDelay, "FrameDelay", 24, 28);
		}

		uint16_t Stride;
		uint8_t FrameDelay;
	};

	struct SS2MFrameDelayStrideRegister : public Register<uint32_t>
	{
		SS2MFrameDelayStrideRegister() :
			Register("SS2M FrameDelay & Stride Register"),
			Stride(0), FrameDelay(0)
		{
			RegisterElement<uint16_t>(&Stride, "Stride", 0, 15);
			RegisterElement<uint8_t>(&FrameDelay, "FrameDelay", 24, 28);
		}

		uint16_t Stride;
		uint8_t FrameDelay;
	};

	struct ParkPointerRegister : public Register<uint32_t>
	{
		ParkPointerRegister() :
			Register("Park Pointer Register"),
			RdFrmPtrRef(0), WrFrmPtrRef(0),
			RdFramStore(0), WrFramStore(0)
		{
			RegisterElement<uint8_t>(&RdFrmPtrRef, "RdFrmPtrRef", 0, 4);
			RegisterElement<uint8_t>(&WrFrmPtrRef, "WrFrmPtrRef", 8, 12);
			RegisterElement<uint8_t>(&RdFramStore, "RdFramStore", 16, 20);
			RegisterElement<uint8_t>(&WrFramStore, "WrFramStore", 24, 28);
		}

		uint8_t RdFrmPtrRef;
		uint8_t WrFrmPtrRef;
		uint8_t RdFramStore;
		uint8_t WrFramStore;
	};

	struct VDMAVersionRegister : public Register<uint32_t>
	{
		VDMAVersionRegister() :
			Register("VDMA Version Register"),
			XilinxInternal(0), MinorVersion(0), MajorVersion(0)
		{
			RegisterElement<uint16_t>(&XilinxInternal, "XilinxInternal", 0, 15);
			RegisterElement<uint8_t>(&MinorVersion, "MinorVersion", 20, 27);
			RegisterElement<uint8_t>(&MajorVersion, "MajorVersion", 28, 31);
		}

		uint16_t XilinxInternal;
		uint8_t MinorVersion;
		uint8_t MajorVersion;
	};

public:
	MM2SControlRegister m_mm2sCtrlReg;
	MM2SStatusRegister m_mm2sStatReg;
	ParkPointerRegister m_parkPntrReg;
	VDMAVersionRegister m_versionReg;
	S2MMControlRegister m_s2mmCtrlReg;
	S2MMStatusRegister m_s2mmStatReg;
	S2MMIrqMask m_s2mmIrqMask;
	MM2SFrameDelayStrideRegister m_mm2sFDelyStrideReg;
	SS2MFrameDelayStrideRegister m_s2mmFDelyStrideReg;

private:
	WatchDog m_watchDogMM2S;
	WatchDog m_watchDogS2MM;
};
