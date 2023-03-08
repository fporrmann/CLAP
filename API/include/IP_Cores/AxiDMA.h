/* 
 *  File: AxiDMA.h
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

#include "IPControl.h"
#include "RegisterInterface.h"
#include "WatchDog.h"

enum DMAInterrupts
{
	INTR_ON_COMPLETE = 1 << 0,
	INTR_ON_DELAY    = 1 << 1,
	INTR_ON_ERROR    = 1 << 2,
	INTR_ALL         = (1 << 3) - 1 // All bits set
};


template<typename T>
class AxiDMA : public IPControlBase
{
	DISABLE_COPY_ASSIGN_MOVE(AxiDMA)
	enum REGISTER_MAP
	{
		MM2S_DMACR        = 0x00,
		MM2S_DMASR        = 0x04,
		MM2S_CURDESC      = 0x08,
		MM2S_CURDESC_MSB  = 0x0C,
		MM2S_TAILDESC     = 0x10,
		MM2S_TAILDESC_MSB = 0x14,
		MM2S_SA           = 0x18,
		MM2S_SA_MSB       = 0x1C,
		MM2S_LENGTH       = 0x28,
		SG_CTL            = 0x2C,
		S2MM_DMACR        = 0x30,
		S2MM_DMASR        = 0x34,
		S2MM_CURDESC      = 0x38,
		S2MM_CURDESC_MSB  = 0x3C,
		S2MM_TAILDESC     = 0x40,
		S2MM_TAILDESC_MSB = 0x44,
		S2MM_DA           = 0x48,
		S2MM_DA_MSB       = 0x4C,
		S2MM_LENGTH       = 0x58
	};

	public:
		AxiDMA(class XDMA* pXdma, const uint64_t& ctrlOffset) :
			IPControlBase(pXdma, ctrlOffset),
			m_mm2sCtrlReg(),
			m_mm2sStatReg(),
			m_s2mmCtrlReg(),
			m_s2mmStatReg(),
			m_watchDogMM2S("AxiDMA_MM2S"),
			m_watchDogS2MM("AxiDMA_S2MM")
		{
			registerReg<uint32_t>(m_mm2sCtrlReg, MM2S_DMACR);
			registerReg<uint32_t>(m_mm2sStatReg, MM2S_DMASR);
			registerReg<uint32_t>(m_s2mmCtrlReg, S2MM_DMACR);
			registerReg<uint32_t>(m_s2mmStatReg, S2MM_DMASR);

			UpdateAllRegisters();
		}

		////////////////////////////////////////

		// Starts both channels
		void Start(const T& srcAddr, const uint32_t& srcLength, const T& dstAddr, const uint32_t& dstLength)
		{
			Start(DMAChannel::MM2S, srcAddr, srcLength);
			Start(DMAChannel::S2MM, dstAddr, dstLength);
		}

		void Start(const Memory& srcMem, const Memory& dstMem)
		{
			Start(static_cast<T>(srcMem.GetBaseAddr()), static_cast<uint32_t>(srcMem.GetSize()),
			      static_cast<T>(dstMem.GetBaseAddr()), static_cast<uint32_t>(dstMem.GetSize()));
		}

		void Start(const DMAChannel& channel, const Memory& mem)
		{
			Start(channel, static_cast<T>(mem.GetBaseAddr()), static_cast<uint32_t>(mem.GetSize()));
		}


		// Starts the specified channel
		void Start(const DMAChannel& channel, const T& addr, const uint32_t& length)
		{
			if (channel == DMAChannel::MM2S)
			{
				// Set the RunStop bit
				m_mm2sCtrlReg.Start();
				// Set the source address
				setMM2SSrcAddr(addr);
				// Set the amount of bytes to transfer
				setMM2SByteLength(length);
			}

			if (channel == DMAChannel::S2MM)
			{
				// Set the RunStop bit
				m_s2mmCtrlReg.Start();
				// Set the destination address
				setS2MMDestAddr(addr);
				// Set the amount of bytes to transfer
				setS2MMByteLength(length);
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
				return m_watchDogMM2S.WaitForFinish(timeoutMS);
			else
				return m_watchDogS2MM.WaitForFinish(timeoutMS);
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

		void EnableInterrupts(const uint32_t& eventNoMM2S, const uint32_t& eventNoS2MM, const DMAInterrupts& intr = INTR_ALL)
		{
			EnableInterrupts(DMAChannel::MM2S, eventNoMM2S, intr);
			EnableInterrupts(DMAChannel::S2MM, eventNoS2MM, intr);
		}

		void EnableInterrupts(const DMAChannel& channel, const uint32_t& eventNo, const DMAInterrupts& intr = INTR_ALL)
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

		void DisableInterrupts(const DMAInterrupts& intr = INTR_ALL)
		{
			DisableInterrupts(DMAChannel::MM2S, intr);
			DisableInterrupts(DMAChannel::S2MM, intr);
		}

		void DisableInterrupts(const DMAChannel& channel, const DMAInterrupts& intr = INTR_ALL)
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
			return readRegister<T>(MM2S_SA);
		}

		T GetS2MMDestAddr()
		{
			return readRegister<T>(S2MM_DA);
		}

		////////////////////////////////////////

		////////////////////////////////////////

		uint32_t GetMM2SByteLength()
		{
			return readRegister<uint32_t>(MM2S_LENGTH);
		}

		uint32_t GetS2MMByteLength()
		{
			return readRegister<uint32_t>(S2MM_LENGTH);
		}

		////////////////////////////////////////

	private:

		////////////////////////////////////////

		void setMM2SSrcAddr(const T& addr)
		{
			writeRegister<T>(MM2S_SA, addr);
		}

		void setS2MMDestAddr(const T& addr)
		{
			writeRegister<T>(S2MM_DA, addr);
		}

		////////////////////////////////////////

		////////////////////////////////////////

		void setMM2SByteLength(const uint32_t& length)
		{
			writeRegister<uint32_t>(MM2S_LENGTH, length);
		}

		void setS2MMByteLength(const uint32_t& length)
		{
			writeRegister<uint32_t>(S2MM_LENGTH, length);
		}

		////////////////////////////////////////

	public:
		struct ControlRegister : public Register<uint32_t>
		{
			ControlRegister(const std::string& name) :
				Register(name),
				RS(0), 
				Reset(0),
				Keyhole(0),
				CyclicBDEnable(0),
				IOCIrqEn(0),
				DlyIrqEn(0),
				ErrIrqEn(0),
				IRQThreshold(0),
				IRQDelay(0)
			{
				RegisterElement<bool>(&RS, "RS", 0);
				RegisterElement<bool>(&Reset, "Reset", 2);
				RegisterElement<bool>(&Keyhole, "Keyhole", 3);
				RegisterElement<bool>(&CyclicBDEnable, "CyclicBDEnable", 4);
				RegisterElement<bool>(&IOCIrqEn, "IOCIrqEn", 12);
				RegisterElement<bool>(&DlyIrqEn, "DlyIrqEn", 13);
				RegisterElement<bool>(&ErrIrqEn, "ErrIrqEn", 14);
				RegisterElement<uint8_t>(&IRQThreshold, "IRQThreshold", 16, 23);
				RegisterElement<uint8_t>(&IRQDelay, "IRQDelay", 24, 31);
			}

			void EnableInterrupts(const DMAInterrupts& intr = INTR_ALL)
			{
				setInterrupts(true, intr);
			}

			void DisableInterrupts(const DMAInterrupts& intr = INTR_ALL)
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
				while(Reset)
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

			void setInterrupts(bool enable, const DMAInterrupts& intr)
			{
				if(intr & INTR_ON_COMPLETE)
					IOCIrqEn = enable;
				if(intr & INTR_ON_DELAY)
					DlyIrqEn = enable;
				if(intr & INTR_ON_ERROR)
					ErrIrqEn = enable;

				Update(Direction::WRITE);
			}

		public:
			bool    RS;
			bool    Reset;
			bool    Keyhole;
			bool    CyclicBDEnable;
			bool    IOCIrqEn;
			bool    DlyIrqEn;
			bool    ErrIrqEn;
			uint8_t IRQThreshold;
			uint8_t IRQDelay;
		};

		struct StatusRegister : public Register<uint32_t>, public HasInterrupt
		{
			StatusRegister(const std::string& name) :
				Register(name),
				Halted(0),
				Idle(0),
				SGIncld(0),
				DMAIntErr(0),
				DMASlvErr(0),
				DMADecErr(0),
				SGIntErr(0),
				SGSlvErr(0),
				SGDecErr(0),
				IOCIrq(0),
				DlyIrq(0),
				ErrIrq(0),
				IRQThresholdSts(0),
				IRQDelaySts(0)
			{
				RegisterElement<bool>(&Halted, "Halted", 0);
				RegisterElement<bool>(&Idle, "Idle", 1);
				RegisterElement<bool>(&SGIncld, "SGIncld", 3);
				RegisterElement<bool>(&DMAIntErr, "DMAIntErr", 4);
				RegisterElement<bool>(&DMASlvErr, "DMASlvErr", 5);
				RegisterElement<bool>(&DMADecErr, "DMADecErr", 6);
				RegisterElement<bool>(&SGIntErr, "SGIntErr", 8);
				RegisterElement<bool>(&SGSlvErr, "SGSlvErr", 9);
				RegisterElement<bool>(&SGDecErr, "SGDecErr", 10);
				RegisterElement<bool>(&IOCIrq, "IOCIrq", 12);
				RegisterElement<bool>(&DlyIrq, "DlyIrq", 13);
				RegisterElement<bool>(&ErrIrq, "ErrIrq", 14);
				RegisterElement<uint8_t>(&IRQThresholdSts, "IRQThresholdSts", 16, 23);
				RegisterElement<uint8_t>(&IRQDelaySts, "IRQDelaySts", 24, 31);
			}

			void ClearInterrupts()
			{
				m_lastInterrupt = GetInterrupts();
				ResetInterrupts(INTR_ALL);
			}

			uint32_t GetInterrupts()
			{
				Update();
				uint32_t intr = 0;
				intr |= IOCIrq << (INTR_ON_COMPLETE >> 1);
				intr |= DlyIrq << (INTR_ON_DELAY >> 1);
				intr |= ErrIrq << (INTR_ON_ERROR >> 1);

				return intr;
			}

			void ResetInterrupts(const DMAInterrupts& intr)
			{
				if(intr & INTR_ON_COMPLETE)
					IOCIrq = 1;
				if(intr & INTR_ON_DELAY)
					DlyIrq = 1;
				if(intr & INTR_ON_ERROR)
					ErrIrq = 1;

				Update(Direction::WRITE);
			}

			bool    Halted;
			bool    Idle;
			bool    SGIncld;
			bool    DMAIntErr;
			bool    DMASlvErr;
			bool    DMADecErr;
			bool    SGIntErr;
			bool    SGSlvErr;
			bool    SGDecErr;
			bool    IOCIrq;
			bool    DlyIrq;
			bool    ErrIrq;
			uint8_t IRQThresholdSts;
			uint8_t IRQDelaySts;
		};

		struct MM2SControlRegister : public ControlRegister
		{
			MM2SControlRegister() :
				ControlRegister("MM2S DMA Control Register")
			{
			}
		};

		struct MM2SStatusRegister : public StatusRegister
		{
			MM2SStatusRegister() :
				StatusRegister("MM2S DMA Status Register")
			{
			}
		};


		struct S2MMControlRegister : public ControlRegister
		{
			S2MMControlRegister() :
				ControlRegister("S2MM DMA Control Register")
			{
			}
		};

		struct S2MMStatusRegister : public StatusRegister
		{
			S2MMStatusRegister() :
				StatusRegister("S2MM DMA Status Register")
			{
			}
		};

	public:
		MM2SControlRegister m_mm2sCtrlReg;
		MM2SStatusRegister  m_mm2sStatReg;
		S2MMControlRegister m_s2mmCtrlReg;
		S2MMStatusRegister  m_s2mmStatReg;

	private:
		WatchDog m_watchDogMM2S;
		WatchDog m_watchDogS2MM;
};