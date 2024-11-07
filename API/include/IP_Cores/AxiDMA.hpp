/*
 *  File: AxiDMA.hpp
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
#include "internal/WatchDog.hpp"

#include "AxiInterruptController.hpp"

#include <array>
#include <cstdint>
#include <queue>

// TODO: Implement SG multi-channel support

namespace clap
{
template<typename T>
class AxiDMA : public internal::RegisterControlBase
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

	struct TransferChunk
	{
		const DMAChannel channel;
		const T addr;
		const uint32_t length;
	};

	static inline const std::string MM2S_INTR_NAME = "mm2s_introut";
	static inline const std::string S2MM_INTR_NAME = "s2mm_introut";

	static inline const uint32_t SG_IRQ_DELAY = 100;

public:
	enum DMAInterrupts
	{
		INTR_ON_COMPLETE = 1 << 0,
		INTR_ON_DELAY    = 1 << 1,
		INTR_ON_ERROR    = 1 << 2,
		INTR_ALL         = (1 << 3) - 1 // All bits set
	};

public:
	AxiDMA(const CLAPPtr& pClap, const uint64_t& ctrlOffset, const bool& mm2sPresent = true, const bool& s2mmPresent = true, const std::string& name = "") :
		RegisterControlBase(pClap, ctrlOffset, name),
		m_pClap(pClap),
		m_watchDogMM2S("AxiDMA_MM2S", pClap->MakeUserInterrupt()),
		m_watchDogS2MM("AxiDMA_S2MM", pClap->MakeUserInterrupt()),
		m_mm2sPresent(mm2sPresent),
		m_s2mmPresent(s2mmPresent)
	{
		if (!m_mm2sPresent && !m_s2mmPresent)
			BUILD_IP_EXCEPTION(CLAPException, "At least one channel must be present");

		if (m_mm2sPresent)
		{
			registerReg<uint32_t>(m_mm2sCtrlReg, MM2S_DMACR);
			registerReg<uint32_t>(m_mm2sStatReg, MM2S_DMASR);

			m_watchDogMM2S.SetStatusRegister(&m_mm2sStatReg);
			m_watchDogMM2S.SetFinishCallback(std::bind(&AxiDMA::OnMM2SFinished, this));
		}

		if (m_s2mmPresent)
		{
			registerReg<uint32_t>(m_s2mmCtrlReg, S2MM_DMACR);
			registerReg<uint32_t>(m_s2mmStatReg, S2MM_DMASR);

			m_watchDogS2MM.SetStatusRegister(&m_s2mmStatReg);
			m_watchDogS2MM.SetFinishCallback(std::bind(&AxiDMA::OnS2MMFinished, this));
		}

		detectBufferLengthRegWidth();
		detectDataWidth();
		detectHasDRE();
	}

	////////////////////////////////////////

	bool OnMM2SFinished()
	{
		CLAP_IP_CORE_LOG_DEBUG << "MM2S Chunk finished" << std::endl;

		// Check if there are more chunks to transfer
		if (!m_mm2sChunks.empty())
		{
			startMM2STransfer();
			return false;
		}

		return true;
	}

	bool OnS2MMFinished()
	{
		CLAP_IP_CORE_LOG_DEBUG << "S2MM Chunk finished" << std::endl;

		// Check if there are more chunks to transfer
		if (!m_s2mmChunks.empty())
		{
			startS2MMTransfer();
			return false;
		}

		return true;
	}

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

	void Start(const Memory& mem)
	{
		if (m_mm2sPresent && m_s2mmPresent)
			CLAP_IP_CORE_LOG_ERROR << "Channel unspecific start with single memory object is not supported when both channels are present, please use the dual memory method" << std::endl;

		if (m_mm2sPresent)
			Start(DMAChannel::MM2S, mem);
		if (m_s2mmPresent)
			Start(DMAChannel::S2MM, mem);
	}

	void Start(const DMAChannel& channel, const Memory& mem)
	{
		Start(channel, static_cast<T>(mem.GetBaseAddr()), static_cast<uint32_t>(mem.GetSize()));
	}

	// Starts the specified channel
	void Start(const DMAChannel& channel, const T& addr, const uint32_t& length)
	{
		CLAP_IP_CORE_LOG_DEBUG << "Starting DMA transfer on channel " << channel << " with address 0x" << std::hex << addr << std::dec << " and length " << length << " byte" << std::endl;

		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			uint32_t remainingLength = length;
			T currentAddr            = addr;

			do
			{
				uint32_t currentLength = std::min(remainingLength, m_maxTransferLengths[ch2Id(channel)]);

				m_mm2sChunks.push({ channel, currentAddr, currentLength });

				currentAddr += currentLength;
				remainingLength -= currentLength;

			} while (remainingLength > 0);

			CLAP_IP_CORE_LOG_VERBOSE << "MM2S chunks: " << m_mm2sChunks.size() << std::endl;

			if (!m_watchDogMM2S.Start(true))
			{
				CLAP_IP_CORE_LOG_ERROR << "Watchdog for MM2S already running!" << std::endl;
				return;
			}

			startMM2STransfer();
		}

		if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			uint32_t remainingLength = length;
			T currentAddr            = addr;

			do
			{
				uint32_t currentLength = std::min(remainingLength, m_maxTransferLengths[ch2Id(channel)]);

				m_s2mmChunks.push({ channel, currentAddr, currentLength });

				currentAddr += currentLength;
				remainingLength -= currentLength;

			} while (remainingLength > 0);

			CLAP_IP_CORE_LOG_VERBOSE << "S2MM chunks: " << m_s2mmChunks.size() << std::endl;

			if (!m_watchDogS2MM.Start(true))
			{
				CLAP_IP_CORE_LOG_ERROR << "Watchdog for S2MM already running!" << std::endl;
				return;
			}

			startS2MMTransfer();
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
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			m_mm2sCtrlReg.Stop();
			m_watchDogMM2S.Stop();
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			m_s2mmCtrlReg.Stop();
			m_watchDogS2MM.Stop();
		}
	}

	bool WaitForFinish(const int32_t& timeoutMS = WAIT_INFINITE)
	{
		if (!WaitForFinish(DMAChannel::MM2S, timeoutMS))
			return false;

		if (!WaitForFinish(DMAChannel::S2MM, timeoutMS))
			return false;

		return true;
	}

	bool WaitForFinish(const DMAChannel& channel, const int32_t& timeoutMS = WAIT_INFINITE)
	{
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			if (!m_watchDogMM2S.WaitForFinish(timeoutMS))
				return false;

			return true;
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			if (!m_watchDogS2MM.WaitForFinish(timeoutMS))
				return false;

			return true;
		}

		std::stringstream ss;
		ss << CLAP_IP_EXCEPTION_TAG << "Channel " << channel << " not present, cannot wait for finish";
		throw CLAPException(ss.str());
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
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			Stop(DMAChannel::MM2S);
			m_mm2sCtrlReg.DoReset();
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			Stop(DMAChannel::S2MM);
			m_s2mmCtrlReg.DoReset();
		}
	}

	////////////////////////////////////////

	////////////////////////////////////////

	void UseInterruptController(AxiInterruptController& axiIntC)
	{
		UseInterruptController(DMAChannel::MM2S, axiIntC);
		UseInterruptController(DMAChannel::S2MM, axiIntC);
	}

	void UseInterruptController(const DMAChannel& channel, AxiInterruptController& axiIntC)
	{
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
			m_watchDogMM2S.SetUserInterrupt(axiIntC.MakeUserInterrupt());
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
			m_watchDogS2MM.SetUserInterrupt(axiIntC.MakeUserInterrupt());
	}

	void EnableInterrupts(const uint32_t& eventNoMM2S, const uint32_t& eventNoS2MM, const DMAInterrupts& intr = INTR_ALL)
	{
		EnableInterrupts(DMAChannel::MM2S, eventNoMM2S, intr);
		EnableInterrupts(DMAChannel::S2MM, eventNoS2MM, intr);
	}

	void EnableInterrupts(const DMAChannel& channel, const uint32_t& eventNo = USE_AUTO_DETECT, const DMAInterrupts& intr = INTR_ALL)
	{
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			uint32_t intrID = eventNo;
			if (m_mm2sIntrDetected != -1)
				intrID = static_cast<uint32_t>(m_mm2sIntrDetected);

			if (intrID == internal::MINUS_ONE)
			{
				CLAP_IP_CORE_LOG_ERROR << "Interrupt ID was not automatically detected and no interrupt ID specified for MM2S channel - Unable to setup interrupts for channel MM2S" << std::endl;
				return;
			}

			m_mm2sCtrlReg.Update();
			m_watchDogMM2S.InitInterrupt(getDevNum(), intrID, &m_mm2sStatReg);
			m_mm2sCtrlReg.EnableInterrupts(intr);
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			uint32_t intrID = eventNo;
			if (m_s2mmIntrDetected != -1)
				intrID = static_cast<uint32_t>(m_s2mmIntrDetected);

			if (intrID == internal::MINUS_ONE)
			{
				CLAP_IP_CORE_LOG_ERROR << "Interrupt ID was not automatically detected and no interrupt ID specified for S2MM channel - Unable to setup interrupts for channel S2MM" << std::endl;
				return;
			}

			m_s2mmCtrlReg.Update();
			m_watchDogS2MM.InitInterrupt(getDevNum(), intrID, &m_s2mmStatReg);
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
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			m_watchDogMM2S.UnsetInterrupt();
			m_mm2sCtrlReg.DisableInterrupts(intr);
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			m_watchDogS2MM.UnsetInterrupt();
			m_s2mmCtrlReg.DisableInterrupts(intr);
		}
	}

	////////////////////////////////////////

	////////////////////////////////////////

	/// @brief Sets the buffer length register width
	/// @param width The buffer length register width in bits
	void SetBufferLengthRegWidth(const uint32_t& width)
	{
		m_bufLenRegWidth = width;
		updateMaxTransferLength();
	}

	// TODO: This should support multiple channels

	/// @brief Sets the data width of the Axi DMA in bytes
	/// @param width The data width in bytes
	void SetDataWidth(const uint32_t& width, const DMAChannel& channel = DMAChannel::MM2S)
	{
		m_dataWidths[ch2Id(channel)] = width;
		updateMaxTransferLength();
	}

	void SetDataWidth(const std::array<uint32_t, 2>& widths)
	{
		SetDataWidth(widths[0], DMAChannel::MM2S);
		SetDataWidth(widths[1], DMAChannel::S2MM);
	}

	/// @brief Sets the data width of the Axi DMA in bits
	/// @param width The data width in bits
	void SetDataWidthBits(const uint32_t& width, const DMAChannel& channel = DMAChannel::MM2S)
	{
		SetDataWidth(width / 8, channel);
	}

	void SetDataWidthBits(const std::array<uint32_t, 2>& widths)
	{
		SetDataWidthBits(widths[0], DMAChannel::MM2S);
		SetDataWidthBits(widths[1], DMAChannel::S2MM);
	}

	const uint32_t& GetDataWidth(const DMAChannel& channel) const
	{
		return m_dataWidths[ch2Id(channel)];
	}

	void SetHasDRE(const bool& dre, const DMAChannel& channel = DMAChannel::MM2S)
	{
		m_dreSupport[ch2Id(channel)] = dre;
	}

	const bool& GetHasDRE(const DMAChannel& channel) const
	{
		return m_dreSupport[ch2Id(channel)];
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

	////////////////////////////////////////

	double GetMM2SRuntime() const
	{
		return m_watchDogMM2S.GetRuntime();
	}

	double GetS2MMRuntime() const
	{
		return m_watchDogS2MM.GetRuntime();
	}

	////////////////////////////////////////
	// SG
	////////////////////////////////////////

	bool IsSGEnabled()
	{
		if (m_mm2sPresent)
			return m_mm2sStatReg.IsSGEnabled();
		else
			return m_s2mmStatReg.IsSGEnabled();

		return false;
	}

	void StartSG(const Memory& memBDTx, const Memory& memBDRx, const Memory& memDataIn, const Memory& memDataOut, const uint32_t maxPktByteLen, const uint8_t& numPkts = 1, const uint32_t& bdsPerPkt = 1)
	{
		StartSG(DMAChannel::MM2S, memBDTx, memDataIn, maxPktByteLen, numPkts, bdsPerPkt);
		StartSG(DMAChannel::S2MM, memBDRx, memDataOut, maxPktByteLen, numPkts, bdsPerPkt);
	}

	void StartSG(const DMAChannel& channel, const Memory& memBD, const Memory& memData, const uint32_t maxPktByteLen, const uint8_t& numPkts = 1, const uint32_t& bdsPerPkt = 1)
	{
		if (channel == DMAChannel::MM2S && m_mm2sPresent)
		{
			if (!m_watchDogMM2S.Start(true))
				BUILD_IP_EXCEPTION(CLAPException, "Watchdog for MM2S already running!");

			startSGTransferMM2S(memBD, memData, maxPktByteLen, numPkts, bdsPerPkt);
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			if (!m_watchDogS2MM.Start(true))
				BUILD_IP_EXCEPTION(CLAPException, "Watchdog for S2MM already running!");

			startSGTransferS2MM(memBD, memData, maxPktByteLen, numPkts);
		}
	}

private:
	static const uint32_t AXI_DMA_BD_MINIMUM_ALIGNMENT = 0x40;
	static const uint32_t AXI_DMA_BD_HAS_DRE_SHIFT     = 8;
	static const uint32_t AXI_DMA_BD_MAX_LENGTH_MASK   = 0x3FFFFFF;

#define XAXIDMA_BD_CTRL_TXSOF_MASK   0x08000000
#define XAXIDMA_BD_CTRL_TXEOF_MASK   0x04000000
#define XAXIDMA_BD_CTRL_ALL_MASK     0x0C000000
#define XAXIDMA_BD_WORDLEN_MASK      0xFF
#define XAXIDMA_BD_HAS_DRE_MASK      0xF00
#define XAXIDMA_BD_STS_COMPLETE_MASK 0x80000000

#define AXIDMA_CHANNEL_NOT_HALTED 1
#define AXIDMA_CHANNEL_HALTED     2

	class SGDescriptor : public internal::RegisterControlBase
	{
	public:
		SGDescriptor(const CLAPPtr& pClap, const uint64_t& ctrlOffset, const std::string& name = "") :
			RegisterControlBase(pClap, ctrlOffset, name)
		{
			Reset();
		}

		DISABLE_COPY_ASSIGN_MOVE(SGDescriptor)

		~SGDescriptor() {}

		void Reset()
		{
			m_nextDescAddr   = 0;
			m_bufferAddr     = 0;
			m_control        = 0;
			m_status         = 0;
			m_app0           = 0;
			m_app1           = 0;
			m_app2           = 0;
			m_app3           = 0;
			m_app4           = 0;
			m_id             = 0;
			m_hasStsCtrlStrm = 0;
			m_hasDRE         = 0;

			writeRegister(0x00, m_nextDescAddr);
			writeRegister(0x08, m_bufferAddr);
			writeRegister(0x18, m_control);
			writeRegister(0x1C, m_status);
			writeRegister(0x20, m_app0);
			writeRegister(0x24, m_app1);
			writeRegister(0x28, m_app2);
			writeRegister(0x2C, m_app3);
			writeRegister(0x30, m_app4);
			writeRegister(0x34, m_id);
			writeRegister(0x38, m_hasStsCtrlStrm);
			writeRegister(0x3C, m_hasDRE);
		}

		void Print() const
		{
			std::cout << "NextDescAddr: 0x" << std::hex << m_nextDescAddr << std::dec << std::endl;
			std::cout << "BufferAddr: 0x" << std::hex << m_bufferAddr << std::dec << std::endl;
			std::cout << "Control: 0x" << std::hex << m_control << std::dec << std::endl;
			std::cout << "Status: 0x" << std::hex << m_status << std::dec << std::endl;
			std::cout << "App0: 0x" << std::hex << m_app0 << std::dec << std::endl;
			std::cout << "App1: 0x" << std::hex << m_app1 << std::dec << std::endl;
			std::cout << "App2: 0x" << std::hex << m_app2 << std::dec << std::endl;
			std::cout << "App3: 0x" << std::hex << m_app3 << std::dec << std::endl;
			std::cout << "App4: 0x" << std::hex << m_app4 << std::dec << std::endl;
			std::cout << "ID: 0x" << std::hex << m_id << std::dec << std::endl;
			std::cout << "HasStsCtrlStrm: 0x" << std::hex << m_hasStsCtrlStrm << std::dec << std::endl;
			std::cout << "HasDRE: 0x" << std::hex << m_hasDRE << std::dec << std::endl;
		}

		const uint64_t& Addr() const
		{
			return m_ctrlOffset;
		}

		void SetNextDescAddr(const uint64_t& addr)
		{
			m_nextDescAddr = addr;
			writeRegister(0, m_nextDescAddr);
		}

		bool SetBufferAddr(const uint64_t& addr)
		{
			uint32_t hasDRE = GetHasDRE();
			uint8_t wordLen = hasDRE & XAXIDMA_BD_WORDLEN_MASK;

			if (addr & (wordLen - 1))
			{
				if ((hasDRE & XAXIDMA_BD_HAS_DRE_MASK) == 0)
				{
					CLAP_IP_CORE_LOG_ERROR << "Error set buf addr 0x" << std::hex << addr << std::dec << " with hasDRE=" << hasDRE << " and wordLen=" << (wordLen - 1) << std::endl;
					return false;
				}
			}

			m_bufferAddr = addr;
			writeRegister(8, m_bufferAddr);

			return true;
		}

		void SetControl(const uint32_t& ctrl)
		{
			m_control = ctrl;
			writeRegister(0x18, m_control);
		}

		void SetControlBits(const uint32_t& bits)
		{
			m_control = readRegister<uint32_t>(0x18);

			m_control &= ~XAXIDMA_BD_CTRL_ALL_MASK;
			m_control |= (bits & XAXIDMA_BD_CTRL_ALL_MASK);

			SetControl(m_control);
		}

		bool SetLength(const uint32_t& lenBytes, const uint32_t& maxLen)
		{
			if (lenBytes > maxLen)
			{
				CLAP_IP_CORE_LOG_ERROR << "Length (" << lenBytes << ") exceeds maximum length (" << maxLen << ")" << std::endl;
				return false;
			}

			m_control = readRegister<uint32_t>(0x18);
			m_control &= ~AXI_DMA_BD_MAX_LENGTH_MASK;
			m_control |= lenBytes;

			SetControl(m_control);

			return true;
		}

		void SetStatus(const uint32_t& sts)
		{
			m_status = sts;
			writeRegister(0x1C, m_status);
		}

		void SetApp0(const uint32_t& app)
		{
			m_app0 = app;
			writeRegister(0x20, m_app0);
		}

		void SetApp1(const uint32_t& app)
		{
			m_app1 = app;
			writeRegister(0x24, m_app1);
		}

		void SetApp2(const uint32_t& app)
		{
			m_app2 = app;
			writeRegister(0x28, m_app2);
		}

		void SetApp3(const uint32_t& app)
		{
			m_app3 = app;
			writeRegister(0x2C, m_app3);
		}

		void SetApp4(const uint32_t& app)
		{
			m_app4 = app;
			writeRegister(0x30, m_app4);
		}

		void SetId(const uint32_t& id)
		{
			m_id = id;
			writeRegister(0x34, m_id);
		}

		void SetHasStsCtrlStrm(const uint32_t& sts)
		{
			m_hasStsCtrlStrm = sts;
			writeRegister(0x38, m_hasStsCtrlStrm);
		}

		void SetHasDRE(const uint32_t& dre)
		{
			m_hasDRE = dre;
			writeRegister(0x3C, m_hasDRE);
		}

		uint32_t GetControl()
		{
			m_control = readRegister<uint32_t>(0x18);
			return m_control;
		}

		uint32_t GetLength()
		{
			m_control = readRegister<uint32_t>(0x18);
			return m_control & AXI_DMA_BD_MAX_LENGTH_MASK;
		}

		uint32_t GetStatus()
		{
			m_status = readRegister<uint32_t>(0x1C);
			return m_status;
		}

		uint32_t GetHasDRE()
		{
			m_hasDRE = readRegister<uint32_t>(0x3C);
			return m_hasDRE;
		}

		class SGDescriptor* GetNextDesc()
		{
			return m_pNextDesc;
		}

		void SetNextDesc(SGDescriptor* pDesc)
		{
			m_pNextDesc = pDesc;
		}

		bool IsComplete()
		{
			return GetStatus() & XAXIDMA_BD_STS_COMPLETE_MASK;
		}

	private:
		uint64_t m_nextDescAddr   = 0; // 0-7
		uint64_t m_bufferAddr     = 0; // 8-F
		uint32_t m_reserved1      = 0; // 10-13
		uint32_t m_reserved2      = 0; // 14-17
		uint32_t m_control        = 0; // 18-1B
		uint32_t m_status         = 0; // 1C-1F
		uint32_t m_app0           = 0; // 20-23
		uint32_t m_app1           = 0; // 24-27
		uint32_t m_app2           = 0; // 28-2B
		uint32_t m_app3           = 0; // 2C-2F
		uint32_t m_app4           = 0; // 30-33
		uint32_t m_id             = 0; // 34-37
		uint32_t m_hasStsCtrlStrm = 0; // 38-3B -- Whether the BD has status/control stream
		uint32_t m_hasDRE         = 0; // 3C-3F -- Whether the BD has DRE (allows unaligned transfers)

		class SGDescriptor* m_pNextDesc = nullptr;
	};

	using SGDescriptors = std::vector<SGDescriptor*>;

	struct BdRing
	{
		BdRing(const DMAChannel& ch) :
			channel(ch)
		{}

		DISABLE_COPY_ASSIGN_MOVE(BdRing)

		~BdRing()
		{
			for (SGDescriptor* d : descriptors)
				delete d;

			if (cyclicBd) delete cyclicBd;
		}

		void Reset()
		{
			if (cyclicBd) delete cyclicBd;

			for (SGDescriptor* d : descriptors)
				delete d;

			descriptors.clear();

			runState        = 0;
			hasStsCntrlStrm = 0;
			hasDRE          = 0;
			dataWidth       = 0;
			maxTransferLen  = 0;

			freeHead  = nullptr;
			preHead   = nullptr;
			hwTail    = nullptr;
			bdRestart = nullptr;
			cyclicBd  = nullptr;

			freeCnt   = 0;
			preCnt    = 0;
			hwCnt     = 0;
			allCnt    = 0;
			ringIndex = 0;
			cyclic    = 0;
		}

		SGDescriptors descriptors = {};

		DMAChannel channel = DMAChannel::MM2S;

		int32_t runState        = 0;
		int32_t hasStsCntrlStrm = 0;
		int32_t hasDRE          = 0;
		int32_t dataWidth       = 0;
		uint32_t maxTransferLen = 0;

		SGDescriptor* freeHead  = nullptr;
		SGDescriptor* preHead   = nullptr;
		SGDescriptor* hwTail    = nullptr;
		SGDescriptor* bdRestart = nullptr;
		SGDescriptor* cyclicBd  = nullptr;

		uint32_t freeCnt   = 0;
		uint32_t preCnt    = 0;
		int32_t hwCnt      = 0;
		int32_t allCnt     = 0;
		uint32_t ringIndex = 0;
		uint32_t cyclic    = 0;

		const uint64_t separation = AXI_DMA_BD_MINIMUM_ALIGNMENT;

		bool IsRxChannel() const
		{
			return channel == DMAChannel::S2MM;
		}
	};

	void startSGTransferMM2S(const Memory& memBD, const Memory& memData, const uint32_t& maxPktByteLen, const uint8_t& numPkts, const uint32_t& bdsPerPkt)
	{
		if (!txSetup(memBD, numPkts, SG_IRQ_DELAY))
			BUILD_IP_EXCEPTION(CLAPException, "TXSetup failed");

		if (!sendPacket(numPkts, maxPktByteLen, bdsPerPkt, memData))
			BUILD_IP_EXCEPTION(CLAPException, "SendPacket failed");
	}

	void startSGTransferS2MM(const Memory& memBD, const Memory& memData, const uint32_t& maxPktByteLen, const uint8_t& numPkts)
	{
		if (!rxSetup(memBD, memData, numPkts, maxPktByteLen, SG_IRQ_DELAY))
			BUILD_IP_EXCEPTION(CLAPException, "RXSetup failed");
	}

	bool initBdRing(BdRing& bdRing, const uint64_t& addr, const uint32_t& bdCount)
	{
		CLAP_IP_CORE_LOG_DEBUG << "Creating BD ring for channel " << bdRing.channel << " with " << bdCount << " BDs" << std::endl;

		if (bdCount <= 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "initBdRing: non-positive BD  number " << bdCount << std::endl;
			return false;
		}

		bdRing.Reset();

		if (bdRing.channel == DMAChannel::MM2S)
		{
			bdRing.hasDRE    = GetHasDRE(DMAChannel::MM2S);
			bdRing.dataWidth = GetDataWidth(DMAChannel::MM2S);
		}
		else
		{
			bdRing.hasDRE    = GetHasDRE(DMAChannel::S2MM);
			bdRing.dataWidth = GetDataWidth(DMAChannel::S2MM);
		}

		Expected<uint32_t> maxTransferLen = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,sg-length-width");

		// -1 to get a 0xXFFFFF value
		bdRing.maxTransferLen = (1 << (maxTransferLen ? maxTransferLen.Value() : 23)) - 1;

		if (addr % AXI_DMA_BD_MINIMUM_ALIGNMENT)
		{
			CLAP_IP_CORE_LOG_ERROR << "initBdRing: Physical address  0x" << std::hex << addr << " is not aligned to 0x" << AXI_DMA_BD_MINIMUM_ALIGNMENT << std::dec << std::endl;
			return false;
		}

		std::vector<SGDescriptor*> desc;

		for (uint32_t i = 0; i < bdCount; i++)
		{
			SGDescriptor* d = new SGDescriptor(m_pClap, addr + (i * bdRing.separation), "SGDescriptor #" + std::to_string(i));

			if (i < bdCount - 1)
				d->SetNextDescAddr(addr + ((i + 1) * bdRing.separation));
			else
				d->SetNextDescAddr(addr);

			d->SetHasStsCtrlStrm(bdRing.hasStsCntrlStrm);
			d->SetHasDRE((bdRing.hasDRE << AXI_DMA_BD_HAS_DRE_SHIFT) | bdRing.dataWidth);

			desc.push_back(d);
		}

		for (std::size_t i = 0; i < desc.size(); i++)
			desc[i]->SetNextDesc(desc[(i + 1) % desc.size()]);

		bdRing.descriptors = desc;
		bdRing.runState    = AXIDMA_CHANNEL_HALTED;

		bdRing.allCnt   = bdCount;
		bdRing.freeCnt  = bdCount;
		bdRing.freeHead = desc.front();
		bdRing.preHead  = desc.front();
		bdRing.hwTail   = desc.front();

		bdRing.bdRestart = desc.front();
		bdRing.cyclicBd  = nullptr;

		return true;
	}

	bool setCoalesce(BdRing& bdRing, const uint8_t& counter, const uint8_t& timer)
	{
		if (counter == 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "setCoalesce: invalid coalescing threshold " << static_cast<uint32_t>(counter) << std::endl;
			return false;
		}

		if (bdRing.channel == DMAChannel::MM2S)
		{
			m_mm2sCtrlReg.SetIrqThreshold(counter);
			m_mm2sCtrlReg.SetIrqDelay(timer);
		}
		else
		{
			m_s2mmCtrlReg.SetIrqThreshold(counter);
			m_s2mmCtrlReg.SetIrqDelay(timer);
		}

		return true;
	}

	bool updateCDesc(BdRing& bdRing)
	{
		uint32_t ringIndex = bdRing.ringIndex;

		StatusRegister* pStatusReg = nullptr;
		uint64_t descPtrOffset     = 0;

		if (bdRing.channel == DMAChannel::MM2S)
		{
			pStatusReg    = &m_mm2sStatReg;
			descPtrOffset = MM2S_CURDESC;
		}
		else
		{
			pStatusReg    = &m_s2mmStatReg;
			descPtrOffset = S2MM_CURDESC;
		}

		if (bdRing.allCnt == 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "updateCDesc: no bds" << std::endl;
			return false;
		}

		if (bdRing.runState == AXIDMA_CHANNEL_NOT_HALTED)
			return true;

		if (!pStatusReg->IsStarted())
		{
			SGDescriptor* pDesc = bdRing.bdRestart;

			if (!pDesc->IsComplete())
			{
				if (bdRing.IsRxChannel())
				{
					if (!ringIndex)
						writeRegister(descPtrOffset, pDesc->Addr());
					else
						BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
				}
				else
					writeRegister(descPtrOffset, pDesc->Addr());
			}
			else
			{
				while (pDesc->IsComplete())
				{
					pDesc = pDesc->GetNextDesc();

					if (pDesc == bdRing.bdRestart)
					{
						CLAP_IP_CORE_LOG_ERROR << "updateCDesc: Cannot find valid cdesc" << std::endl;
						return false;
					}

					if (!pDesc->IsComplete())
					{
						if (bdRing.IsRxChannel())
						{
							if (!ringIndex)
								writeRegister(descPtrOffset, pDesc->Addr());
							else
								BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
						}
						else
							writeRegister(descPtrOffset, pDesc->Addr());
						break;
					}
				}
			}
		}

		return true;
	}

	bool startBdRingHW(BdRing& bdRing)
	{
		uint32_t ringIndex = bdRing.ringIndex;

		StatusRegister* pStatusReg = nullptr;
		ControlRegister* pCtrlReg  = nullptr;
		uint64_t tailDescOffset    = 0;

		if (bdRing.channel == DMAChannel::MM2S)
		{
			pStatusReg     = &m_mm2sStatReg;
			pCtrlReg       = &m_mm2sCtrlReg;
			tailDescOffset = MM2S_TAILDESC;
		}
		else
		{
			pStatusReg     = &m_s2mmStatReg;
			pCtrlReg       = &m_s2mmCtrlReg;
			tailDescOffset = S2MM_TAILDESC;
		}

		if (!pStatusReg->IsStarted())
			pCtrlReg->Start();

		if (pStatusReg->IsStarted())
		{
			bdRing.runState = AXIDMA_CHANNEL_NOT_HALTED;

			if (bdRing.hwCnt > 0)
			{
				if (bdRing.cyclic)
				{
					writeRegister(tailDescOffset, bdRing.cyclicBd->Addr());
					return true;
				}

				if (!bdRing.hwTail->IsComplete())
				{
					if (bdRing.IsRxChannel())
					{
						if (!ringIndex)
							writeRegister(tailDescOffset, bdRing.hwTail->Addr());
						else
							BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
					}
					else
						writeRegister(tailDescOffset, bdRing.hwTail->Addr());
				}
			}

			return true;
		}

		CLAP_IP_CORE_LOG_ERROR << "startBdRingHW: Failed to start hardware -- Try resetting the AxiDMA IP before starting" << std::endl;
		pStatusReg->Print();

		return false;
	}

	bool startBdRing(BdRing& bdRing)
	{
		if (!updateCDesc(bdRing))
		{
			CLAP_IP_CORE_LOG_ERROR << "startBdRing: Updating Current Descriptor Failed" << std::endl;
			return false;
		}

		if (!startBdRingHW(bdRing))
		{
			CLAP_IP_CORE_LOG_ERROR << "startBdRing: Starting Hardware Failed" << std::endl;
			return false;
		}

		return true;
	}

	bool bdRingAlloc(BdRing& bdRing, const uint32_t& numBd, SGDescriptor** ppBdSet)
	{
		CLAP_IP_CORE_LOG_DEBUG << "Allocating " << numBd << " BDs" << std::endl;

		if (bdRing.freeCnt < numBd)
		{
			CLAP_IP_CORE_LOG_ERROR << "Not enough BDs to alloc " << numBd << "/" << bdRing.freeCnt << std::endl;
			return false;
		}

		*ppBdSet = bdRing.freeHead;

		for (uint32_t i = 0; i < numBd; i++)
			bdRing.freeHead = bdRing.freeHead->GetNextDesc();

		bdRing.freeCnt -= numBd;
		bdRing.preCnt += numBd;

		return true;
	}

	bool bdRingToHw(BdRing& bdRing, const uint32_t& numBd, SGDescriptor* pBdSet)
	{
		uint64_t tailDescOffset = 0;

		if (bdRing.channel == DMAChannel::MM2S)
			tailDescOffset = MM2S_TAILDESC;
		else
			tailDescOffset = S2MM_TAILDESC;

		uint32_t ringIndex = bdRing.ringIndex;

		if (numBd == 0) return true;

		if ((bdRing.preCnt < numBd) || (bdRing.preHead != pBdSet))
		{
			CLAP_IP_CORE_LOG_ERROR << "BD ring has problems:" << std::endl;
			if (bdRing.preCnt < numBd)
				CLAP_IP_CORE_LOG_ERROR << "preCnt (" << bdRing.preCnt << ") < numBd (" << numBd << ")" << std::endl;

			if (bdRing.preHead != pBdSet)
				CLAP_IP_CORE_LOG_ERROR << std::hex << "preHead (0x" << bdRing.preHead << ") != pBdSet (0x" << pBdSet << ")" << std::dec << std::endl;

			return false;
		}

		SGDescriptor* pCurBd = pBdSet;

		uint32_t bdCr  = pCurBd->GetControl();
		uint32_t bdSts = pCurBd->GetStatus();

		if (!bdRing.IsRxChannel() && !(bdCr & XAXIDMA_BD_CTRL_TXSOF_MASK))
		{
			CLAP_IP_CORE_LOG_ERROR << "Tx first BD does not have SOF" << std::endl;
			return false;
		}

		for (uint32_t i = 0; i < numBd - 1; i++)
		{
			if ((pCurBd->GetLength() & bdRing.maxTransferLen) == 0)
			{
				CLAP_IP_CORE_LOG_ERROR << "0 length BD" << std::endl;
				return false;
			}

			bdSts &= ~XAXIDMA_BD_STS_COMPLETE_MASK;
			pCurBd->SetStatus(bdSts);

			pCurBd = pCurBd->GetNextDesc();
			bdCr   = pCurBd->GetControl();
			bdSts  = pCurBd->GetStatus();
		}

		if (!bdRing.IsRxChannel() && !(bdCr & XAXIDMA_BD_CTRL_TXEOF_MASK))
		{
			CLAP_IP_CORE_LOG_ERROR << "Tx last BD does not have EOF" << std::endl;
			return false;
		}

		if ((bdCr & bdRing.maxTransferLen) == 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "0 length BD" << std::endl;
			return false;
		}

		bdSts &= ~XAXIDMA_BD_STS_COMPLETE_MASK;
		pCurBd->SetStatus(bdSts);

		for (uint32_t i = 0; i < numBd; i++)
			bdRing.preHead = bdRing.preHead->GetNextDesc();

		bdRing.preCnt -= numBd;
		bdRing.hwTail = pCurBd;
		bdRing.hwCnt += numBd;

		if (bdRing.runState == AXIDMA_CHANNEL_NOT_HALTED)
		{
			if (bdRing.cyclic)
			{
				writeRegister(tailDescOffset, bdRing.cyclicBd->Addr());
				return true;
			}

			if (bdRing.IsRxChannel())
			{
				if (!ringIndex)
					writeRegister(tailDescOffset, bdRing.hwTail->Addr());
				else
					BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
			}
			else
				writeRegister(tailDescOffset, bdRing.hwTail->Addr());
		}

		return true;
	}

	bool txSetup(const Memory& mem, const uint8_t& numPkts, const uint8_t& irqDelay)
	{
		uint32_t bdCount = ROUND_UP_DIV(mem.GetSize(), AXI_DMA_BD_MINIMUM_ALIGNMENT);

		if (!initBdRing(m_bdRingTx, mem.GetBaseAddr(), bdCount))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed to initialize BD ring" << std::endl;
			return false;
		}

		if (!setCoalesce(m_bdRingTx, numPkts, irqDelay))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed set coalescing " << static_cast<uint32_t>(numPkts) << "/" << static_cast<uint32_t>(irqDelay) << std::endl;
			return false;
		}

		if (!startBdRing(m_bdRingTx))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed start BD ring" << std::endl;
			return false;
		}

		return true;
	}

	bool rxSetup(const Memory& memBd, const Memory& memRx, const uint8_t& numPkts, const uint32_t& maxPktByteLen, const uint8_t& irqDelay)
	{
		uint32_t bdCount = ROUND_UP_DIV(memBd.GetSize(), AXI_DMA_BD_MINIMUM_ALIGNMENT);

		if (!initBdRing(m_bdRingRx, memBd.GetBaseAddr(), bdCount))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed to initialize BD ring" << std::endl;
			return false;
		}

		int32_t freeBdCount = m_bdRingRx.freeCnt;
		SGDescriptor* pBd;

		if (!bdRingAlloc(m_bdRingRx, freeBdCount, &pBd))
		{
			CLAP_IP_CORE_LOG_ERROR << "Rx BD alloc failed" << std::endl;
			return false;
		}

		SGDescriptor* pBdCur = pBd;
		uint64_t pRxBuffer   = memRx.GetBaseAddr();

		for (int32_t i = 0; i < freeBdCount; i++)
		{
			if (!pBdCur->SetBufferAddr(pRxBuffer))
			{
				CLAP_IP_CORE_LOG_ERROR << "Rx set buffer addr 0x" << std::hex << pRxBuffer << std::dec << " on BD " << pBdCur->GetName() << " failed" << std::endl;
				return false;
			}

			if (!pBdCur->SetLength(maxPktByteLen, m_bdRingRx.maxTransferLen))
			{
				CLAP_IP_CORE_LOG_ERROR << "Rx set length " << maxPktByteLen << " on BD " << pBdCur->GetName() << " failed" << std::endl;
				return false;
			}

			pBdCur->SetControlBits(0);

			pBdCur->SetId(i); // Arbitrary ID

			pRxBuffer += maxPktByteLen;
			pBdCur = pBdCur->GetNextDesc();
		}

		if (!setCoalesce(m_bdRingRx, numPkts, irqDelay))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed set coalescing " << static_cast<uint32_t>(numPkts) << "/" << static_cast<uint32_t>(irqDelay) << std::endl;
			return false;
		}

		if (!bdRingToHw(m_bdRingRx, freeBdCount, pBd))
		{
			CLAP_IP_CORE_LOG_ERROR << "Rx ToHw failed" << std::endl;
			return false;
		}

		if (!startBdRing(m_bdRingRx))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed start BD ring" << std::endl;
			return false;
		}

		return true;
	}

	bool readPackets()
	{
		return true;
	}

	bool sendPacket(const uint8_t& numPkts, const uint32_t maxPktByteLen, const uint32_t& bdsPerPkt, const Memory& mem)
	{
		const uint32_t numBDs = numPkts * bdsPerPkt;

		CLAP_IP_CORE_LOG_DEBUG << "Sending " << numPkts << " packets of " << maxPktByteLen << " bytes, with " << bdsPerPkt << " BDs per packet" << std::endl;

		if (static_cast<uint32_t>(maxPktByteLen) * bdsPerPkt > m_bdRingTx.maxTransferLen)
		{
			CLAP_IP_CORE_LOG_ERROR << "Invalid total per packet transfer length for the packet " << maxPktByteLen * bdsPerPkt << "/" << m_bdRingTx.maxTransferLen << std::endl;
			return false;
		}

		SGDescriptor* pBd;

		if (!bdRingAlloc(m_bdRingTx, numBDs, &pBd))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed BD alloc" << std::endl;
			return false;
		}

		uint64_t bufferAddr  = mem.GetBaseAddr();
		SGDescriptor* pBdCur = pBd;

		for (uint32_t i = 0; i < numPkts; i++)
		{
			for (uint32_t pkt = 0; pkt < bdsPerPkt; pkt++)
			{
				uint32_t crBits = 0;

				if (!pBdCur->SetBufferAddr(bufferAddr))
				{
					CLAP_IP_CORE_LOG_ERROR << "Tx set buffer addr " << bufferAddr << " on BD " << pBdCur << " failed" << std::endl;
					return false;
				}

				if (!pBdCur->SetLength(maxPktByteLen, m_bdRingTx.maxTransferLen))
				{
					CLAP_IP_CORE_LOG_ERROR << "Tx set length " << maxPktByteLen << " on BD " << pBdCur << " failed" << std::endl;
					return false;
				}

				if (pkt == 0)
					crBits |= XAXIDMA_BD_CTRL_TXSOF_MASK;

				if (pkt == (bdsPerPkt - 1))
					crBits |= XAXIDMA_BD_CTRL_TXEOF_MASK;

				pBdCur->SetControlBits(crBits);
				pBdCur->SetId(i);

				bufferAddr += maxPktByteLen;
				pBdCur = pBdCur->GetNextDesc();
			}
		}

		if (!bdRingToHw(m_bdRingTx, numBDs, pBd))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed to send packet, length: " << pBd->GetLength() << std::endl;
			return false;
		}

		return true;
	}

	////////////////////////////////////////
	// SG
	////////////////////////////////////////

protected:
	bool detectInterruptID() override
	{
		Expected<std::vector<uint64_t>> res = CLAP()->ReadUIOPropertyVec(m_ctrlOffset, "interrupts");
		Expected<uint32_t> intrParent       = CLAP()->ReadUIOProperty(m_ctrlOffset, "interrupt-parent");

		if (res)
		{
			const std::vector<uint64_t>& intrs = res.Value();
			if (intrs.empty()) return false;

			// Both channels are active
			if (intrs.size() >= 4)
			{
				if (intrParent)
					CLAP_IP_CORE_LOG_WARNING << "Interrupt parent is set while both channels are active - This might cause problems" << std::endl;

				m_mm2sIntrDetected = static_cast<uint32_t>(intrs[0]);
				m_s2mmIntrDetected = static_cast<uint32_t>(intrs[2]);
				CLAP_IP_CORE_LOG_INFO << "Detected interrupts: MM2S=" << m_mm2sIntrDetected << ", S2MM=" << m_s2mmIntrDetected << std::endl;
				return true;
			}
			else if (intrs.size() >= 2)
			{
				// Only one channel is active, check which one
				Expected<std::string> intrName = CLAP()->ReadUIOStringProperty(m_ctrlOffset, "interrupt-names");
				if (!intrName) return false;

				Expected<uint32_t> devID = CLAP()->GetUIOID(m_ctrlOffset);

				if (intrName.Value() == MM2S_INTR_NAME)
				{
					if (intrParent && devID)
						m_mm2sIntrDetected = devID.Value();
					else
						m_mm2sIntrDetected = static_cast<uint32_t>(intrs[0]);
					CLAP_IP_CORE_LOG_INFO << "Detected interrupt: MM2S=" << m_mm2sIntrDetected << std::endl;
				}
				else if (intrName.Value() == S2MM_INTR_NAME)
				{
					if (intrParent && devID)
						m_s2mmIntrDetected = devID.Value();
					else
						m_s2mmIntrDetected = static_cast<uint32_t>(intrs[0]);
					CLAP_IP_CORE_LOG_INFO << "Detected interrupt: S2MM=" << m_s2mmIntrDetected << std::endl;
				}
				else
				{
					CLAP_IP_CORE_LOG_ERROR << "Unable to detect interrupt ID for channel: \"" << intrName.Value() << "\"" << std::endl;
					return false;
				}

				return true;
			}
		}

		return false;
	}

private:
	////////////////////////////////////////

	void startMM2STransfer()
	{
		if (m_mm2sChunks.empty())
		{
			CLAP_IP_CORE_LOG_ERROR << "No MM2S chunks available!" << std::endl;
			return;
		}

		const TransferChunk chunk = m_mm2sChunks.front();
		m_mm2sChunks.pop();

		m_mm2sStatReg.Reset();

		// Set the RunStop bit
		m_mm2sCtrlReg.Start();
		// Set the source address
		setMM2SSrcAddr(chunk.addr);
		// Set the amount of bytes to transfer
		setMM2SByteLength(chunk.length);
	}

	void startS2MMTransfer()
	{
		if (m_s2mmChunks.empty())
		{
			CLAP_IP_CORE_LOG_ERROR << "No S2MM chunks available!" << std::endl;
			return;
		}

		const TransferChunk chunk = m_s2mmChunks.front();
		m_s2mmChunks.pop();

		m_s2mmStatReg.Reset();

		// Set the RunStop bit
		m_s2mmCtrlReg.Start();
		// Set the destination address
		setS2MMDestAddr(chunk.addr);
		// Set the amount of bytes to transfer
		setS2MMByteLength(chunk.length);
	}

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

	void detectBufferLengthRegWidth()
	{
		Expected<uint64_t> res = CLAP()->ReadUIOProperty(m_ctrlOffset, "xlnx,sg-length-width");
		if (res)
		{
			m_bufLenRegWidth = static_cast<uint32_t>(res.Value());
			updateMaxTransferLength();
			CLAP_IP_CORE_LOG_INFO << "Detected buffer length register width: " << m_bufLenRegWidth << " bit" << std::endl;
		}
	}

	void detectDataWidth()
	{
		if (m_mm2sPresent)
			detectDataWidth(DMAChannel::MM2S);
		if (m_s2mmPresent)
			detectDataWidth(DMAChannel::S2MM);
	}

	void detectDataWidth(const DMAChannel& channel)
	{
		Expected<uint64_t> res = CLAP()->ReadUIOProperty(m_ctrlOffset, buildPropertyString(channel, "xlnx,datawidth"));
		if (res)
		{
			SetDataWidthBits(static_cast<uint32_t>(res.Value()), channel);
			CLAP_IP_CORE_LOG_INFO << "Detected data width: " << m_dataWidths[ch2Id(channel)] << " byte for channel " << channel << std::endl;
		}
	}

	void detectHasDRE()
	{
		if (m_mm2sPresent)
			detectHasDRE(DMAChannel::MM2S);
		if (m_s2mmPresent)
			detectHasDRE(DMAChannel::S2MM);
	}

	void detectHasDRE(const DMAChannel& channel)
	{
		const bool exists = CLAP()->CheckUIOPropertyExists(m_ctrlOffset, buildPropertyString(channel, "xlnx,include-dre"));
		if (exists)
		{
			SetHasDRE(exists, channel);
			CLAP_IP_CORE_LOG_INFO << "Detected DRE: " << m_dreSupport[ch2Id(channel)] << " for channel " << channel << std::endl;
		}
	}

	std::string buildPropertyString(const DMAChannel& channel, const std::string& propName)
	{
		const uint64_t offset = (channel == DMAChannel::MM2S ? REGISTER_MAP::MM2S_DMACR : REGISTER_MAP::S2MM_DMACR);

		const std::string addr = utils::Hex2Str(m_ctrlOffset + offset);

		return std::string("/dma-channel@" + addr + "/" + propName);
	}

	void updateMaxTransferLength()
	{
		m_maxTransferLengths[ch2Id(DMAChannel::MM2S)] = (1 << m_bufLenRegWidth) - m_dataWidths[ch2Id(DMAChannel::MM2S)];
		m_maxTransferLengths[ch2Id(DMAChannel::S2MM)] = (1 << m_bufLenRegWidth) - m_dataWidths[ch2Id(DMAChannel::S2MM)];
	}

	uint32_t ch2Id(const DMAChannel& channel) const
	{
		return (channel == DMAChannel::MM2S ? 0 : 1);
	}

	////////////////////////////////////////

	class ControlRegister : public internal::Register<uint32_t>
	{
	public:
		explicit ControlRegister(const std::string& name) :
			Register(name)
		{
			RegisterElement<bool>(&m_rs, "RS", 0);
			RegisterElement<bool>(&m_reset, "Reset", 2);
			RegisterElement<bool>(&m_keyhole, "Keyhole", 3);
			RegisterElement<bool>(&m_cyclicBDEnable, "CyclicBDEnable", 4);
			RegisterElement<bool>(&m_ioCIrqEn, "IOCIrqEn", 12);
			RegisterElement<bool>(&m_dlyIrqEn, "DlyIrqEn", 13);
			RegisterElement<bool>(&m_errIrqEn, "ErrIrqEn", 14);
			RegisterElement<uint8_t>(&m_irqThreshold, "IRQThreshold", 16, 23);
			RegisterElement<uint8_t>(&m_irqDelay, "IRQDelay", 24, 31);
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
			m_reset = 1;
			Update(internal::Direction::WRITE);

			// The Reset bit will be set to 0 once the reset has been completed
			while (m_reset)
				Update();
		}

		void SetIrqThreshold(const uint8_t& threshold)
		{
			m_irqThreshold = threshold;
			Update(internal::Direction::WRITE);
		}

		void SetIrqDelay(const uint8_t& delay)
		{
			m_irqDelay = delay;
			Update(internal::Direction::WRITE);
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

		void setInterrupts(bool enable, const DMAInterrupts& intr)
		{
			if (intr & INTR_ON_COMPLETE)
				m_ioCIrqEn = enable;
			if (intr & INTR_ON_DELAY)
				m_dlyIrqEn = enable;
			if (intr & INTR_ON_ERROR)
				m_errIrqEn = enable;

			Update(internal::Direction::WRITE);
		}

	private:
		bool m_rs              = false;
		bool m_reset           = false;
		bool m_keyhole         = false;
		bool m_cyclicBDEnable  = false;
		bool m_ioCIrqEn        = false;
		bool m_dlyIrqEn        = false;
		bool m_errIrqEn        = false;
		uint8_t m_irqThreshold = 0;
		uint8_t m_irqDelay     = 0;
	};

	class StatusRegister : public internal::Register<uint32_t>, public internal::HasInterrupt, public internal::HasStatus
	{
	public:
		explicit StatusRegister(const std::string& name) :
			Register(name)
		{
			RegisterElement<bool>(&m_halted, "Halted", 0);
			RegisterElement<bool>(&m_idle, "Idle", 1);
			RegisterElement<bool>(&m_sgIncld, "SGIncld", 3);
			RegisterElement<bool>(&m_dmaIntErr, "DMAIntErr", 4);
			RegisterElement<bool>(&m_dmaSlvErr, "DMASlvErr", 5);
			RegisterElement<bool>(&m_dmaDecErr, "DMADecErr", 6);
			RegisterElement<bool>(&m_sgIntErr, "SGIntErr", 8);
			RegisterElement<bool>(&m_sgSlvErr, "SGSlvErr", 9);
			RegisterElement<bool>(&m_sgDecErr, "SGDecErr", 10);
			RegisterElement<bool>(&m_ioCIrq, "IOCIrq", 12);
			RegisterElement<bool>(&m_dlyIrq, "DlyIrq", 13);
			RegisterElement<bool>(&m_errIrq, "ErrIrq", 14);
			RegisterElement<uint8_t>(&m_irqThresholdSts, "IRQThresholdSts", 16, 23);
			RegisterElement<uint8_t>(&m_irqDelaySts, "IRQDelaySts", 24, 31);
		}

		void ClearInterrupts() override
		{
			m_lastInterrupt = GetInterrupts();
			ResetInterrupts(INTR_ALL);
		}

		uint32_t GetInterrupts() override
		{
			Update();
			uint32_t intr = 0;
			intr |= m_ioCIrq << (INTR_ON_COMPLETE >> 1);
			intr |= m_dlyIrq << (INTR_ON_DELAY >> 1);
			intr |= m_errIrq << (INTR_ON_ERROR >> 1);

			return intr;
		}

		void ResetInterrupts(const DMAInterrupts& intr)
		{
			if (intr & INTR_ON_COMPLETE)
				m_ioCIrq = 1;
			if (intr & INTR_ON_DELAY)
				m_dlyIrq = 1;
			if (intr & INTR_ON_ERROR)
				m_errIrq = 1;

			Update(internal::Direction::WRITE);
		}

		bool IsStarted()
		{
			Update();
			return !m_halted;
		}

		bool IsSGEnabled()
		{
			Update();
			return m_sgIncld;
		}

	protected:
		void getStatus() override
		{
			Update();
			if (!m_done && m_idle)
				m_done = true;
		}

	private:
		bool m_halted             = false;
		bool m_idle               = false;
		bool m_sgIncld            = false;
		bool m_dmaIntErr          = false;
		bool m_dmaSlvErr          = false;
		bool m_dmaDecErr          = false;
		bool m_sgIntErr           = false;
		bool m_sgSlvErr           = false;
		bool m_sgDecErr           = false;
		bool m_ioCIrq             = false;
		bool m_dlyIrq             = false;
		bool m_errIrq             = false;
		uint8_t m_irqThresholdSts = 0;
		uint8_t m_irqDelaySts     = 0;
	};

	class MM2SControlRegister : public ControlRegister
	{
	public:
		MM2SControlRegister() :
			ControlRegister("MM2S DMA Control Register")
		{
		}
	};

	class MM2SStatusRegister : public StatusRegister
	{
	public:
		MM2SStatusRegister() :
			StatusRegister("MM2S DMA Status Register")
		{
		}
	};

	class S2MMControlRegister : public ControlRegister
	{
	public:
		S2MMControlRegister() :
			ControlRegister("S2MM DMA Control Register")
		{
		}
	};

	class S2MMStatusRegister : public StatusRegister
	{
	public:
		S2MMStatusRegister() :
			StatusRegister("S2MM DMA Status Register")
		{
		}
	};

private:
	CLAPPtr m_pClap = nullptr;

	BdRing m_bdRingTx = BdRing(DMAChannel::MM2S);
	BdRing m_bdRingRx = BdRing(DMAChannel::S2MM);

	MM2SControlRegister m_mm2sCtrlReg = MM2SControlRegister();
	MM2SStatusRegister m_mm2sStatReg  = MM2SStatusRegister();
	S2MMControlRegister m_s2mmCtrlReg = S2MMControlRegister();
	S2MMStatusRegister m_s2mmStatReg  = S2MMStatusRegister();

	internal::WatchDog m_watchDogMM2S;
	internal::WatchDog m_watchDogS2MM;

	uint32_t m_bufLenRegWidth = 14; // Default AXI DMA width of the buffer length register is 14 bits

	std::array<uint32_t, 2> m_maxTransferLengths = { 0x3FFC, 0x3FFC }; // Default AXI DMA max transfer length is 16K

	std::array<uint32_t, 2> m_dataWidths = { 4, 4 }; // Default AXI DMA data width is 32 bits

	std::array<bool, 2> m_dreSupport = { false, false }; // By default unaligned transfers are not enabled

	std::queue<TransferChunk> m_mm2sChunks = {};
	std::queue<TransferChunk> m_s2mmChunks = {};

	bool m_mm2sPresent = false;
	bool m_s2mmPresent = false;

	int32_t m_mm2sIntrDetected = -1;
	int32_t m_s2mmIntrDetected = -1;
};
} // namespace clap