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

// Notes:
// - The BD pointer can only be updated when the DMA engine is halted (stopped)
//   - This means that when another BD region, e.g., pre initialized should be used, the DMA engine has to be stopped before updating the BD pointer, i.e., starting the next transfer
// TODOs:
// - Implement SG multi-channel support
// - Turn BdRing struct into a class

#pragma once

#include "../internal/RegisterControl.hpp"
#include "internal/WatchDog.hpp"

#include "AxiInterruptController.hpp"

#include <array>
#include <cstdint>
#include <queue>

namespace clap
{

class SGDescriptor : public internal::RegisterControlBase
{
	static inline const uint32_t CTRL_ALL_MASK   = 0x0C000000;
	static inline const uint32_t MAX_LENGTH_MASK = 0x3FFFFFF;
	static inline const uint32_t HAS_DRE_MASK    = 0xF00;
	static inline const uint8_t WORDLEN_MASK     = 0xFF;

	enum REGISTER_MAP
	{
		SG_DESC_NXTDESC           = 0x00,
		SG_DESC_BUFFER_ADDRESS    = 0x08,
		SG_DESC_CONTROL           = 0x18,
		SG_DESC_STATUS            = 0x1C,
		SG_DESC_APP0              = 0x20,
		SG_DESC_APP1              = 0x24,
		SG_DESC_APP2              = 0x28,
		SG_DESC_APP3              = 0x2C,
		SG_DESC_APP4              = 0x30,
		SG_DESC_ID                = 0x34,
		SG_DESC_HAS_STS_CTRL_STRM = 0x38,
		SG_DESC_HAS_DRE           = 0x3C
	};

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

		writeRegister(SG_DESC_NXTDESC, m_nextDescAddr);
		writeRegister(SG_DESC_BUFFER_ADDRESS, m_bufferAddr);
		writeRegister(SG_DESC_CONTROL, m_control);
		writeRegister(SG_DESC_STATUS, m_status);
		writeRegister(SG_DESC_APP0, m_app0);
		writeRegister(SG_DESC_APP1, m_app1);
		writeRegister(SG_DESC_APP2, m_app2);
		writeRegister(SG_DESC_APP3, m_app3);
		writeRegister(SG_DESC_APP4, m_app4);
		writeRegister(SG_DESC_ID, m_id);
		writeRegister(SG_DESC_HAS_STS_CTRL_STRM, m_hasStsCtrlStrm);
		writeRegister(SG_DESC_HAS_DRE, m_hasDRE);
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
		writeRegister(SG_DESC_NXTDESC, m_nextDescAddr);
	}

	bool SetBufferAddr(const uint64_t& addr)
	{
		GetHasDRE();
		const uint8_t wordLen = m_hasDRE & WORDLEN_MASK;

		if (addr & (wordLen - 1))
		{
			if ((m_hasDRE & HAS_DRE_MASK) == 0)
			{
				CLAP_IP_CORE_LOG_ERROR << "Error set buf addr 0x" << std::hex << addr << std::dec << " with hasDRE=" << m_hasDRE << " and wordLen=" << (wordLen - 1) << std::endl;
				return false;
			}
		}

		m_bufferAddr = addr;
		writeRegister(SG_DESC_BUFFER_ADDRESS, m_bufferAddr);

		return true;
	}

	void SetControl(const uint32_t& ctrl)
	{
		m_control = ctrl;
		writeRegister(SG_DESC_CONTROL, m_control);
	}

	void SetControlBits(const uint32_t& bits)
	{
		GetControl();

		m_control &= ~CTRL_ALL_MASK;
		m_control |= (bits & CTRL_ALL_MASK);

		SetControl(m_control);
	}

	bool SetLength(const uint32_t& lenBytes, const uint32_t& maxLen)
	{
		if (lenBytes > maxLen)
		{
			CLAP_IP_CORE_LOG_ERROR << "Length (" << lenBytes << ") exceeds maximum length (" << maxLen << ")" << std::endl;
			return false;
		}

		GetControl();
		m_control &= ~MAX_LENGTH_MASK;
		m_control |= lenBytes;

		SetControl(m_control);

		return true;
	}

	void ClearComplete()
	{
		GetStatus();
		m_status &= ~COMPLETE_MASK;
		writeRegister(SG_DESC_STATUS, m_status);
	}

	void SetStatus(const uint32_t& sts)
	{
		m_status = sts;
		writeRegister(SG_DESC_STATUS, m_status);
	}

	void SetApp0(const uint32_t& app)
	{
		m_app0 = app;
		writeRegister(SG_DESC_APP0, m_app0);
	}

	void SetApp1(const uint32_t& app)
	{
		m_app1 = app;
		writeRegister(SG_DESC_APP1, m_app1);
	}

	void SetApp2(const uint32_t& app)
	{
		m_app2 = app;
		writeRegister(SG_DESC_APP2, m_app2);
	}

	void SetApp3(const uint32_t& app)
	{
		m_app3 = app;
		writeRegister(SG_DESC_APP3, m_app3);
	}

	void SetApp4(const uint32_t& app)
	{
		m_app4 = app;
		writeRegister(SG_DESC_APP4, m_app4);
	}

	void SetId(const uint32_t& id)
	{
		m_id = id;
		writeRegister(SG_DESC_ID, m_id);
	}

	void SetHasStsCtrlStrm(const uint32_t& sts)
	{
		m_hasStsCtrlStrm = sts;
		writeRegister(SG_DESC_HAS_STS_CTRL_STRM, m_hasStsCtrlStrm);
	}

	void SetHasDRE(const uint32_t& dre)
	{
		m_hasDRE = dre;
		writeRegister(SG_DESC_HAS_DRE, m_hasDRE);
	}

	const uint32_t& GetControl()
	{
		m_control = readRegister<uint32_t>(SG_DESC_CONTROL);
		return m_control;
	}

	uint32_t GetLength()
	{
		GetControl();
		return m_control & MAX_LENGTH_MASK;
	}

	const uint32_t& GetStatus()
	{
		m_status = readRegister<uint32_t>(SG_DESC_STATUS);
		return m_status;
	}

	const uint32_t& GetHasDRE()
	{
		m_hasDRE = readRegister<uint32_t>(SG_DESC_HAS_DRE);
		return m_hasDRE;
	}

	class SGDescriptor* GetNextDesc() const
	{
		return m_pNextDesc;
	}

	void SetNextDesc(SGDescriptor* pDesc)
	{
		m_pNextDesc = pDesc;
	}

	bool IsComplete()
	{
		return GetStatus() & COMPLETE_MASK;
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

public:
	static inline const uint32_t COMPLETE_MASK = 0x80000000;
};

using SGDescriptors = std::vector<SGDescriptor*>;

class SGDescriptorContainer
{
public:
	SGDescriptorContainer(const SGDescriptors& descs, const uint8_t& numPkts) :
		m_descriptors(descs),
		m_numPkts(numPkts)
	{}

	~SGDescriptorContainer()
	{
		for (SGDescriptor* pDesc : m_descriptors)
			delete pDesc;
	}

	SGDescriptorContainer(const SGDescriptorContainer& other)            = delete;
	SGDescriptorContainer& operator=(const SGDescriptorContainer& other) = delete;

	SGDescriptorContainer(SGDescriptorContainer&& other) noexcept :
		m_descriptors(std::move(other.m_descriptors)),
		m_completeClearDone(other.m_completeClearDone),
		m_numPkts(other.m_numPkts)
	{
		other.m_descriptors.clear();
		other.m_completeClearDone = false;
		other.m_numPkts           = 0;
	}

	SGDescriptorContainer& operator=(SGDescriptorContainer&& other) noexcept
	{
		if (this != &other)
		{
			for (SGDescriptor* pDesc : m_descriptors)
				delete pDesc;

			m_descriptors       = std::move(other.m_descriptors);
			m_completeClearDone = other.m_completeClearDone;
			m_numPkts           = other.m_numPkts;

			other.m_descriptors.clear();
			other.m_completeClearDone = false;
			other.m_numPkts           = 0;
		}
		return *this;
	}

	void ResetCompleteState()
	{
		for (SGDescriptor* pDesc : m_descriptors)
			pDesc->ClearComplete();

		m_completeClearDone = true;
	}

	void SetCompleteClearDone(const bool& done)
	{
		m_completeClearDone = done;
	}

	const bool& GetCompleteClearDone() const
	{
		return m_completeClearDone;
	}

	const SGDescriptors& GetDescriptors() const
	{
		return m_descriptors;
	}

	void SetDescriptors(const SGDescriptors& descs)
	{
		m_descriptors = descs;
	}

	const uint8_t& GetNumPkts() const
	{
		return m_numPkts;
	}

	void SetNumPkts(const uint8_t& numPkts)
	{
		m_numPkts = numPkts;
	}

private:
	SGDescriptors m_descriptors = {};
	bool m_completeClearDone    = false;
	uint8_t m_numPkts           = 0;
};

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

		// Assignment operator
		TransferChunk& operator=(const TransferChunk& other)
		{
			const_cast<DMAChannel&>(channel) = other.channel;
			const_cast<T&>(addr)             = other.addr;
			const_cast<uint32_t&>(length)    = other.length;
			return *this;
		}
	};

	enum class SGState
	{
		Idle = 0,
		Running
	};

	static inline const std::string MM2S_INTR_NAME = "mm2s_introut";
	static inline const std::string S2MM_INTR_NAME = "s2mm_introut";

	static inline const uint32_t SG_IRQ_DELAY = 0;

	static inline const uint32_t CTRL_TXSOF_MASK = 0x08000000;
	static inline const uint32_t CTRL_TXEOF_MASK = 0x04000000;

	static inline const uint32_t MINIMUM_ALIGNMENT = 0x40;
	static inline const uint32_t HAS_DRE_SHIFT     = 8;

public:
	enum DMAInterrupts
	{
		INTR_ON_COMPLETE = 1 << 0,
		INTR_ON_DELAY    = 1 << 1,
		INTR_ON_ERROR    = 1 << 2,
		INTR_ALL         = (1 << 3) - 1 // All bits set
	};

	struct ChunkResult
	{
		const uint32_t expectedLength;
		const uint32_t actualLength;
	};

	using ChunkResults = std::vector<ChunkResult>;

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

		initBDRings();
	}

	////////////////////////////////////////

	bool OnMM2SFinished()
	{
		CLAP_IP_CORE_LOG_DEBUG << "MM2S Transfer Finished" << std::endl;

		// Check if there are more chunks to transfer
		if (!m_mm2sChunks.empty())
		{
			startMM2STransfer();
			return false;
		}

		m_bdRingTx.SetRunState(SGState::Idle);

		return true;
	}

	bool OnS2MMFinished()
	{
		CLAP_IP_CORE_LOG_DEBUG << "S2MM Transfer Finished" << std::endl;

		m_s2mmChunkResults.push_back({ m_s2mmCurChunk.length, GetS2MMByteLength() });

		// Check if there are more chunks to transfer
		if (!m_s2mmChunks.empty())
		{
			startS2MMTransfer();
			return false;
		}

		m_bdRingRx.SetRunState(SGState::Idle);

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
		if (IsSGEnabled())
			BUILD_IP_EXCEPTION(CLAPException, "SG mode is enabled, normal transfers not possible, use the StartSG method instead");

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

			m_s2mmChunkResults.clear();

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

			if (m_mm2sStatReg.IsSGEnabled())
				m_bdRingTx.Reset();
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			m_s2mmCtrlReg.Stop();
			m_watchDogS2MM.Stop();

			if (m_s2mmStatReg.IsSGEnabled())
				m_bdRingRx.Reset();
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

			if (m_mm2sStatReg.IsSGEnabled())
				m_bdRingTx.Reset();
		}
		else if (channel == DMAChannel::S2MM && m_s2mmPresent)
		{
			Stop(DMAChannel::S2MM);
			m_s2mmCtrlReg.DoReset();

			if (m_s2mmStatReg.IsSGEnabled())
				m_bdRingRx.Reset();
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
			if (m_mm2sIntrDetected != INTR_UNDEFINED)
				intrID = static_cast<uint32_t>(m_mm2sIntrDetected);

			if (intrID == USE_AUTO_DETECT)
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
			if (m_s2mmIntrDetected != INTR_UNDEFINED)
				intrID = static_cast<uint32_t>(m_s2mmIntrDetected);

			if (intrID == USE_AUTO_DETECT)
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

	const uint32_t& GetMaxTransferLength(const DMAChannel& channel) const
	{
		return m_maxTransferLengths[ch2Id(channel)];
	}

	////////////////////////////////////////

	////////////////////////////////////////

	const ChunkResults& GetS2MMChunkResults() const
	{
		return m_s2mmChunkResults;
	}

	uint64_t GetS2MMTotalTransferredBytes() const
	{
		uint64_t total = 0;
		for (const auto& res : m_s2mmChunkResults)
			total += res.actualLength;

		return total;
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

	void StartSG(const Memory& memBDTx, const Memory& memBDRx, const Memory& memDataIn, const Memory& memDataOut, const uint32_t& maxPktByteLen, const uint8_t& numPkts = 1, const uint32_t& bdsPerPkt = 1)
	{
		StartSG(DMAChannel::MM2S, memBDTx, memDataIn, maxPktByteLen, numPkts, bdsPerPkt);
		StartSG(DMAChannel::S2MM, memBDRx, memDataOut, maxPktByteLen, numPkts, bdsPerPkt);
	}

	void StartSG(const DMAChannel& channel, const Memory& memBD, const Memory& memData, const uint32_t& maxPktByteLen, const uint8_t& numPkts = 1, const uint32_t& bdsPerPkt = 1)
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

	void StartSGExtDescs(const DMAChannel& channel, SGDescriptorContainer& descContainer)
	{
		const SGDescriptors& descs = descContainer.GetDescriptors();
		SGDescriptor* pBd          = descs.front();
		const uint32_t numBDs      = static_cast<uint32_t>(descs.size());
		const uint8_t& numPkts     = descContainer.GetNumPkts();

		if (channel == DMAChannel::MM2S)
		{
			if (m_bdRingTx.GetRunState() != SGState::Idle)
				BUILD_IP_EXCEPTION(CLAPException, "DMA channel MM2S is still active");

			m_bdRingTx.Init(descs, true);

			if (!setCoalesce(m_bdRingTx, numPkts, SG_IRQ_DELAY))
				BUILD_IP_EXCEPTION(CLAPException, "Failed set coalescing " << static_cast<uint32_t>(numPkts) << "/" << static_cast<uint32_t>(SG_IRQ_DELAY));

			if (!m_watchDogMM2S.Start(true))
				BUILD_IP_EXCEPTION(CLAPException, "Watchdog for MM2S already running!");

			if (!startBdRing(m_bdRingTx))
				BUILD_IP_EXCEPTION(CLAPException, "Failed to start BD ring");

			if (!bdRingToHw(m_bdRingTx, numBDs, pBd, descContainer.GetCompleteClearDone()))
				BUILD_IP_EXCEPTION(CLAPException, "Failed to send packet, length: " << pBd->GetLength());
		}
		else if (channel == DMAChannel::S2MM)
		{
			if (m_bdRingRx.GetRunState() != SGState::Idle)
				BUILD_IP_EXCEPTION(CLAPException, "DMA channel S2MM is still active");

			m_bdRingRx.Init(descs, true);

			if (!setCoalesce(m_bdRingRx, numPkts, SG_IRQ_DELAY))
				BUILD_IP_EXCEPTION(CLAPException, "Failed set coalescing " << static_cast<uint32_t>(numPkts) << "/" << static_cast<uint32_t>(SG_IRQ_DELAY));

			if (!m_watchDogS2MM.Start(true))
				BUILD_IP_EXCEPTION(CLAPException, "Watchdog for S2MM already running!");

			if (!bdRingToHw(m_bdRingRx, numBDs, pBd, descContainer.GetCompleteClearDone()))
				BUILD_IP_EXCEPTION(CLAPException, "Failed to read packet, length: " << pBd->GetLength());

			if (!startBdRing(m_bdRingRx))
				BUILD_IP_EXCEPTION(CLAPException, "Failed to start BD ring");
		}

		descContainer.SetCompleteClearDone(false);
	}

	SGDescriptorContainer PreInitSGDescs(const DMAChannel& channel, const Memory& memBD, const Memory& memData, const uint32_t& maxPktByteLen, const uint8_t& numPkts = 1, const uint32_t& bdsPerPkt = 1)
	{
		BdRing* pRing = nullptr;

		if (channel == DMAChannel::MM2S)
			pRing = &m_bdRingTx;
		else if (channel == DMAChannel::S2MM)
			pRing = &m_bdRingRx;
		else
			BUILD_IP_EXCEPTION(CLAPException, "PreInitSGDescs failed, invalid channel");

		const uint32_t bdCount = ROUND_UP_DIV(memBD.GetSize(), MINIMUM_ALIGNMENT);
		SGDescriptors descs    = initDescs(*pRing, memBD.GetBaseAddr(), bdCount);

		if (configDescs(channel, numPkts, maxPktByteLen, bdsPerPkt, memData, descs.front()))
			return SGDescriptorContainer(descs, numPkts);
		else
		{
			for (SGDescriptor* d : descs)
				delete d;
			descs.clear();
			BUILD_IP_EXCEPTION(CLAPException, "PreInitSGDescs failed");
		}
	}

private:
	class BdRing
	{
	public:
		static const uint64_t SEPARATION = MINIMUM_ALIGNMENT;

	public:
		BdRing(const DMAChannel& ch) :
			m_channel(ch)
		{}

		DISABLE_COPY_ASSIGN_MOVE(BdRing)

		~BdRing()
		{
			Reset();
		}

		void Reset()
		{
			if (!m_extDescs)
			{
				for (SGDescriptor* d : m_descriptors)
					delete d;

				if (m_pCyclicBd) delete m_pCyclicBd;

				m_descriptors.clear();
			}

			m_runState = SGState::Idle;

			m_pFreeHead  = nullptr;
			m_pHwTail    = nullptr;
			m_pBdRestart = nullptr;
			m_pCyclicBd  = nullptr;

			m_freeCnt   = 0;
			m_hwCnt     = 0;
			m_allCnt    = 0;
			m_ringIndex = 0;
			m_cyclic    = 0;
		}

		void Init(const SGDescriptors& descs, const bool& useExtDescs = false)
		{
			CheckBdMemAddr(descs.front()->Addr());

			m_descriptors = descs;
			m_runState    = SGState::Idle;

			m_freeCnt = m_allCnt = static_cast<uint32_t>(descs.size());

			m_pCyclicBd = nullptr;

			ReInit(useExtDescs);
		}

		void ReInit(const bool& useExtDescs = false)
		{
			m_extDescs = useExtDescs;

			m_pFreeHead = m_descriptors.front();
			m_pHwTail   = m_descriptors.front();

			m_pBdRestart = m_descriptors.front();

			m_freeCnt = (m_extDescs ? 0 : m_allCnt);
			m_hwCnt   = 0;
		}

		bool IsRxChannel() const
		{
			return m_channel == DMAChannel::S2MM;
		}

		bool CheckBdMemAddr(const uint64_t& addr)
		{
			if (m_pBdRestart == nullptr) return true;
			if (!m_pStatusReg->IsStarted()) return true;
			if (m_pBdRestart->Addr() == addr) return true;

			BUILD_EXCEPTION(CLAPException, "The BD memory location cannot be changed while the DMA is running, please stop the DMA first");
		}

		bool CheckBdMemAddr(const Memory& mem)
		{
			return CheckBdMemAddr(mem.GetBaseAddr());
		}

		void SetHasDRE(const bool& dre)
		{
			m_hasDRE = dre;
		}

		void SetDataWidth(const uint32_t& width)
		{
			m_dataWidth = width;
		}

		void SetMaxTransferLen(const uint32_t& len)
		{
			m_maxTransferLen = len;
		}

		void SetStatusRegister(class AxiDMA<T>::StatusRegister* pReg)
		{
			m_pStatusReg = pReg;
		}

		void SetControlRegister(class AxiDMA<T>::ControlRegister* pReg)
		{
			m_pCtrlReg = pReg;
		}

		void SetDescPtrOffset(const uint64_t& offset)
		{
			m_descPtrOffset = offset;
		}

		void SetTailDescOffset(const uint64_t& offset)
		{
			m_tailDescOffset = offset;
		}

		void SetRunState(const SGState& state)
		{
			m_runState = state;
		}

		void SetAllCnt(const uint32_t& cnt)
		{
			m_allCnt = cnt;
		}

		void SetCyclicBd(SGDescriptor* pBd)
		{
			m_pCyclicBd = pBd;
		}

		void SetFreeHead(SGDescriptor* pDesc)
		{
			m_pFreeHead = pDesc;
		}

		void SetHwTail(SGDescriptor* pDesc)
		{
			m_pHwTail = pDesc;
		}

		void SetBdRestart(SGDescriptor* pDesc)
		{
			m_pBdRestart = pDesc;
		}

		void SetFreeCnt(const uint32_t& cnt)
		{
			m_freeCnt = cnt;
		}

		void SetHwCnt(const uint32_t& cnt)
		{
			m_hwCnt = cnt;
		}

		void UpdateHwTail(const uint32_t& numBd)
		{
			if (m_descriptors.size() < static_cast<std::size_t>(numBd))
				BUILD_EXCEPTION(CLAPException, "Invalid number of BDs, provided number: " << numBd << " but only " << m_descriptors.size() << " descriptors available");

			if (numBd > 0)
			{
				// Minus 1, because the tail indicates the last used BD in the chain, i.e., if 1 BD were to be used the tail would be the same as the head (first BD)
				m_pHwTail = m_descriptors[numBd - 1];
			}
		}

		const DMAChannel& GetChannel() const
		{
			return m_channel;
		}

		const uint32_t& HasDRE() const
		{
			return m_hasDRE;
		}

		const uint32_t& GetDataWidth() const
		{
			return m_dataWidth;
		}

		const uint32_t& GetMaxTransferLen() const
		{
			return m_maxTransferLen;
		}

		// For some reason the following getter break the clang-format, therefore, they are not formatted
		// clang-format off

		class AxiDMA<T>::StatusRegister* GetStatusRegister() const
		{
			return m_pStatusReg;
		}

		class AxiDMA<T>::ControlRegister* GetControlRegister() const
		{
			return m_pCtrlReg;
		}

		const uint64_t& GetDescPtrOffset() const
		{
			return m_descPtrOffset;
		}

		// clang-format on

		const uint64_t& GetTailDescOffset() const
		{
			return m_tailDescOffset;
		}

		const SGState& GetRunState() const
		{
			return m_runState;
		}

		const uint32_t& GetAllCnt() const
		{
			return m_allCnt;
		}

		SGDescriptor* GetFreeHead() const
		{
			return m_pFreeHead;
		}

		SGDescriptor* GetHwTail() const
		{
			return m_pHwTail;
		}

		SGDescriptor* GetBdRestart() const
		{
			return m_pBdRestart;
		}

		SGDescriptor* GetCyclicBd() const
		{
			return m_pCyclicBd;
		}

		const uint32_t& GetFreeCnt() const
		{
			return m_freeCnt;
		}

		const uint32_t& GetHwCnt() const
		{
			return m_hwCnt;
		}

		const uint32_t& GetRingIndex() const
		{
			return m_ringIndex;
		}

		const uint32_t& IsCyclic() const
		{
			return m_cyclic;
		}

		const SGDescriptors& GetDescriptors() const
		{
			return m_descriptors;
		}

		const bool& HasExtDescs() const
		{
			return m_extDescs;
		}

		const uint32_t& HasStsCntrlStrm() const
		{
			return m_hasStsCntrlStrm;
		}

	private:
		SGDescriptors m_descriptors = {};

		DMAChannel m_channel = DMAChannel::MM2S;

		SGState m_runState         = SGState::Idle;
		uint32_t m_hasStsCntrlStrm = 0;
		uint32_t m_hasDRE          = 0;
		uint32_t m_dataWidth       = 0;
		uint32_t m_maxTransferLen  = 0;

		SGDescriptor* m_pFreeHead  = nullptr;
		SGDescriptor* m_pHwTail    = nullptr;
		SGDescriptor* m_pBdRestart = nullptr;
		SGDescriptor* m_pCyclicBd  = nullptr;

		uint32_t m_freeCnt   = 0;
		uint32_t m_hwCnt     = 0;
		uint32_t m_allCnt    = 0;
		uint32_t m_ringIndex = 0;
		uint32_t m_cyclic    = 0;

		bool m_extDescs = false;

		class AxiDMA<T>::StatusRegister* m_pStatusReg = nullptr;
		class AxiDMA<T>::ControlRegister* m_pCtrlReg  = nullptr;

		uint64_t m_descPtrOffset  = 0;
		uint64_t m_tailDescOffset = 0;
	};

	void initBDRings()
	{
		m_bdRingTx.SetHasDRE(GetHasDRE(DMAChannel::MM2S));
		m_bdRingTx.SetDataWidth(GetDataWidth(DMAChannel::MM2S));
		m_bdRingTx.SetStatusRegister(&m_mm2sStatReg);
		m_bdRingTx.SetControlRegister(&m_mm2sCtrlReg);
		m_bdRingTx.SetDescPtrOffset(MM2S_CURDESC);
		m_bdRingTx.SetTailDescOffset(MM2S_TAILDESC);

		m_bdRingRx.SetHasDRE(GetHasDRE(DMAChannel::S2MM));
		m_bdRingRx.SetDataWidth(GetDataWidth(DMAChannel::S2MM));
		m_bdRingRx.SetStatusRegister(&m_s2mmStatReg);
		m_bdRingRx.SetControlRegister(&m_s2mmCtrlReg);
		m_bdRingRx.SetDescPtrOffset(S2MM_CURDESC);
		m_bdRingRx.SetTailDescOffset(S2MM_TAILDESC);

		// -1 to get a 0xXFFFFF value
		m_bdRingTx.SetMaxTransferLen((1 << m_bufLenRegWidth) - 1);
		m_bdRingRx.SetMaxTransferLen((1 << m_bufLenRegWidth) - 1);
	}

	void startSGTransferMM2S(const Memory& memBD, const Memory& memData, const uint32_t& maxPktByteLen, const uint8_t& numPkts, const uint32_t& bdsPerPkt)
	{
		if (m_bdRingTx.GetRunState() != SGState::Idle)
			BUILD_IP_EXCEPTION(CLAPException, "DMA channel MM2S is still active");

		if (!bdSetup(m_bdRingTx, memBD, numPkts, SG_IRQ_DELAY))
			BUILD_IP_EXCEPTION(CLAPException, "TXSetup failed");

		if (!sendPackets(numPkts, maxPktByteLen, bdsPerPkt, memData))
			BUILD_IP_EXCEPTION(CLAPException, "SendPackets failed");
	}

	void startSGTransferS2MM(const Memory& memBD, const Memory& memData, const uint32_t& maxPktByteLen, const uint8_t& numPkts)
	{
		if (m_bdRingRx.GetRunState() != SGState::Idle)
			BUILD_IP_EXCEPTION(CLAPException, "DMA channel S2MM is still active");

		if (!bdSetup(m_bdRingRx, memBD, numPkts, SG_IRQ_DELAY))
			BUILD_IP_EXCEPTION(CLAPException, "RXSetup failed");

		if (!readPackets(maxPktByteLen, memData))
			BUILD_IP_EXCEPTION(CLAPException, "ReadPackets failed");
	}

	bool initBdRing(BdRing& bdRing, const uint64_t& addr, const uint32_t& bdCount)
	{
		CLAP_IP_CORE_LOG_DEBUG << "Creating BD ring for channel " << bdRing.GetChannel() << " with " << bdCount << " BDs" << std::endl;

		if (bdCount <= 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "initBdRing: non-positive BD number " << bdCount << std::endl;
			return false;
		}

		bdRing.Reset();

		SGDescriptors descs = initDescs(bdRing, addr, bdCount);

		if (descs.empty())
			return false;

		bdRing.Init(descs);

		return true;
	}

	bool setCoalesce(BdRing& bdRing, const uint8_t& counter, const uint8_t& timer)
	{
		if (counter == 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "setCoalesce: invalid coalescing threshold " << static_cast<uint32_t>(counter) << std::endl;
			return false;
		}

		bdRing.GetControlRegister()->SetIrqThreshold(counter);
		bdRing.GetControlRegister()->SetIrqDelay(timer);

		return true;
	}

	bool updateCDesc(BdRing& bdRing)
	{
		if (bdRing.GetAllCnt() == 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "updateCDesc: no bds" << std::endl;
			return false;
		}

		if (bdRing.GetRunState() == SGState::Running)
			return true;

		if (!bdRing.GetStatusRegister()->IsStarted())
		{
			SGDescriptor* pDesc = bdRing.GetBdRestart();

			if (bdRing.HasExtDescs())
			{
				writeRegister(bdRing.GetDescPtrOffset(), pDesc->Addr());
				return true;
			}

			if (!pDesc->IsComplete())
			{
				if (bdRing.IsRxChannel())
				{
					if (!bdRing.GetRingIndex())
						writeRegister(bdRing.GetDescPtrOffset(), pDesc->Addr());
					else
						BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
				}
				else
					writeRegister(bdRing.GetDescPtrOffset(), pDesc->Addr());
			}
			else
			{
				while (pDesc->IsComplete())
				{
					pDesc = pDesc->GetNextDesc();

					if (pDesc == bdRing.GetBdRestart())
					{
						CLAP_IP_CORE_LOG_ERROR << "updateCDesc: Cannot find valid cdesc" << std::endl;
						return false;
					}

					if (!pDesc->IsComplete())
					{
						if (bdRing.IsRxChannel())
						{
							if (!bdRing.GetRingIndex())
								writeRegister(bdRing.GetDescPtrOffset(), pDesc->Addr());
							else
								BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
						}
						else
							writeRegister(bdRing.GetDescPtrOffset(), pDesc->Addr());
						break;
					}
				}
			}
		}

		return true;
	}

	bool startBdRingHW(BdRing& bdRing)
	{
		if (!bdRing.GetStatusRegister()->IsStarted())
			bdRing.GetControlRegister()->Start();

		if (bdRing.GetStatusRegister()->IsStarted())
		{
			bdRing.SetRunState(SGState::Running);

			if (bdRing.GetHwCnt() > 0)
			{
				if (bdRing.IsCyclic())
				{
					writeRegister(bdRing.GetTailDescOffset(), bdRing.GetCyclicBd()->Addr());
					return true;
				}

				if (!bdRing.GetHwTail()->IsComplete())
				{
					if (bdRing.IsRxChannel())
					{
						if (!bdRing.GetRingIndex())
							writeRegister(bdRing.GetTailDescOffset(), bdRing.GetHwTail()->Addr());
						else
							BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
					}
					else
						writeRegister(bdRing.GetTailDescOffset(), bdRing.GetHwTail()->Addr());
				}
			}

			return true;
		}

		CLAP_IP_CORE_LOG_ERROR << "startBdRingHW: Failed to start hardware -- Try resetting the AxiDMA IP before starting" << std::endl;
		bdRing.GetStatusRegister()->Print();

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

		if (bdRing.GetFreeCnt() < numBd)
		{
			CLAP_IP_CORE_LOG_ERROR << "Not enough BDs to alloc " << numBd << "/" << bdRing.GetFreeCnt() << std::endl;
			return false;
		}

		*ppBdSet = bdRing.GetFreeHead();

		for (uint32_t i = 0; i < numBd; i++)
			bdRing.SetFreeHead(bdRing.GetFreeHead()->GetNextDesc());

		bdRing.SetFreeCnt(bdRing.GetFreeCnt() - numBd);

		return true;
	}

	bool bdRingToHw(BdRing& bdRing, const uint32_t& numBd, SGDescriptor* pBdSet, const bool& skipBdReset = false)
	{
		if (numBd == 0) return true;

		if (!skipBdReset)
			resetDescs(!bdRing.IsRxChannel(), bdRing.GetMaxTransferLen(), numBd, pBdSet);
		else
			CLAP_IP_CORE_LOG_DEBUG << "Skipping BD reset, as it has already been performed beforehand" << std::endl;

		bdRing.UpdateHwTail(numBd);
		bdRing.SetHwCnt(bdRing.GetHwCnt() + numBd);

		if (bdRing.GetRunState() == SGState::Running)
		{
			if (bdRing.IsCyclic())
			{
				writeRegister(bdRing.GetTailDescOffset(), bdRing.GetCyclicBd()->Addr());
				return true;
			}

			if (bdRing.IsRxChannel())
			{
				if (!bdRing.GetRingIndex())
					writeRegister(bdRing.GetTailDescOffset(), bdRing.GetHwTail()->Addr());
				else
					BUILD_IP_EXCEPTION(CLAPException, "Multi channel support is currently not implemented");
			}
			else
				writeRegister(bdRing.GetTailDescOffset(), bdRing.GetHwTail()->Addr());
		}

		return true;
	}

	bool bdSetup(BdRing& bdRing, const Memory& mem, const uint8_t& numPkts, const uint8_t& irqDelay)
	{
		bdRing.CheckBdMemAddr(mem);

		const uint32_t bdCount = ROUND_UP_DIV(mem.GetSize(), MINIMUM_ALIGNMENT);

		// If the BD ring count is the same as the previous one, we can reuse the BD ring
		if (bdRing.GetAllCnt() == bdCount)
		{
			CLAP_IP_CORE_LOG_DEBUG << "Reusing BD ring" << std::endl;
			bdRing.ReInit();
			return true;
		}

		if (!initBdRing(bdRing, mem.GetBaseAddr(), bdCount))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed to initialize BD ring" << std::endl;
			return false;
		}

		if (!setCoalesce(bdRing, numPkts, irqDelay))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed set coalescing " << static_cast<uint32_t>(numPkts) << "/" << static_cast<uint32_t>(irqDelay) << std::endl;
			return false;
		}

		return true;
	}

	bool readPackets(const uint32_t& maxPktByteLen, const Memory& mem)
	{
		int32_t freeBdCount = m_bdRingRx.GetFreeCnt();
		SGDescriptor* pBd;

		if (!bdRingAlloc(m_bdRingRx, freeBdCount, &pBd))
		{
			CLAP_IP_CORE_LOG_ERROR << "Rx BD alloc failed" << std::endl;
			return false;
		}

		SGDescriptor* pBdCur = pBd;
		configRxDescs(maxPktByteLen, mem, freeBdCount, pBdCur);

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

	bool sendPackets(const uint8_t& numPkts, const uint32_t maxPktByteLen, const uint32_t& bdsPerPkt, const Memory& mem)
	{
		if (!startBdRing(m_bdRingTx))
		{
			CLAP_IP_CORE_LOG_ERROR << "Failed start BD ring" << std::endl;
			return false;
		}

		const uint32_t numBDs = numPkts * bdsPerPkt;

		CLAP_IP_CORE_LOG_DEBUG << "Sending " << static_cast<uint32_t>(numPkts) << " packets of " << maxPktByteLen << " bytes, with " << bdsPerPkt << " BDs per packet" << std::endl;

		if (static_cast<uint32_t>(maxPktByteLen) * bdsPerPkt > m_bdRingTx.GetMaxTransferLen())
		{
			CLAP_IP_CORE_LOG_ERROR << "Invalid total per packet transfer length for the packet " << maxPktByteLen * bdsPerPkt << "/" << m_bdRingTx.GetMaxTransferLen() << std::endl;
			return false;
		}

		SGDescriptor* pBd;

		if (!m_bdRingTx.HasExtDescs())
		{
			if (!bdRingAlloc(m_bdRingTx, numBDs, &pBd))
			{
				CLAP_IP_CORE_LOG_ERROR << "Failed BD alloc" << std::endl;
				return false;
			}

			configTxDescs(numPkts, maxPktByteLen, bdsPerPkt, mem, pBd);
		}
		else
			pBd = m_bdRingTx.GetDescriptors().front();

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

	SGDescriptors initDescs(BdRing& bdRing, const uint64_t& addr, const uint32_t& bdCount)
	{
		if (bdCount <= 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "initDescs: non-positive BD number " << bdCount << std::endl;
			return {};
		}

		if (addr % MINIMUM_ALIGNMENT)
		{
			CLAP_IP_CORE_LOG_ERROR << "initDescs: Physical address  0x" << std::hex << addr << " is not aligned to 0x" << MINIMUM_ALIGNMENT << std::dec << std::endl;
			return {};
		}

		SGDescriptors descs(bdCount, nullptr);

		for (uint32_t i = 0; i < bdCount; i++)
		{
			SGDescriptor* d = new SGDescriptor(m_pClap, addr + (i * BdRing::SEPARATION), "SGDescriptor #" + std::to_string(i));

			if (i < bdCount - 1)
				d->SetNextDescAddr(addr + ((i + 1) * BdRing::SEPARATION));
			else
				d->SetNextDescAddr(addr);

			d->SetHasStsCtrlStrm(bdRing.HasStsCntrlStrm());
			d->SetHasDRE((bdRing.HasDRE() << HAS_DRE_SHIFT) | bdRing.GetDataWidth());

			descs[i] = d;
		}

		for (std::size_t i = 0; i < descs.size(); i++)
			descs[i]->SetNextDesc(descs[(i + 1) % descs.size()]);

		return descs;
	}

	bool configDescs(const DMAChannel& channel, const uint8_t& numPkts, const uint32_t maxPktByteLen, const uint32_t& bdsPerPkt, const Memory& mem, SGDescriptor* pBd)
	{
		if (channel == DMAChannel::MM2S)
			return configTxDescs(numPkts, maxPktByteLen, bdsPerPkt, mem, pBd);
		else if (channel == DMAChannel::S2MM)
			return configRxDescs(maxPktByteLen, mem, bdsPerPkt * numPkts, pBd);
		else
			return false;
	}

	bool configTxDescs(const uint8_t& numPkts, const uint32_t maxPktByteLen, const uint32_t& bdsPerPkt, const Memory& mem, SGDescriptor* pBd)
	{
		uint64_t bufferAddr    = mem.GetBaseAddr();
		uint64_t remainingSize = mem.GetSize();

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

				const uint64_t bdLength = std::min(remainingSize, static_cast<uint64_t>(maxPktByteLen));
				remainingSize -= bdLength;

				if (!pBdCur->SetLength(bdLength, m_bdRingTx.GetMaxTransferLen()))
				{
					CLAP_IP_CORE_LOG_ERROR << "Tx set length " << bdLength << " on BD " << pBdCur << " failed" << std::endl;
					return false;
				}

				if (pkt == 0)
					crBits |= CTRL_TXSOF_MASK;

				if (pkt == (bdsPerPkt - 1))
					crBits |= CTRL_TXEOF_MASK;

				pBdCur->SetControlBits(crBits);
				pBdCur->SetId(i);

				bufferAddr += bdLength;
				pBdCur = pBdCur->GetNextDesc();
			}
		}

		return true;
	}

	bool configRxDescs(const uint32_t& maxPktByteLen, const Memory& mem, const int32_t& bdCount, SGDescriptor* pBd)
	{
		uint64_t pRxBuffer     = mem.GetBaseAddr();
		uint64_t remainingSize = mem.GetSize();

		SGDescriptor* pBdCur = pBd;

		if (bdCount <= 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "non-positive BD number " << bdCount << std::endl;
			return false;
		}

		for (int32_t i = 0; i < bdCount; i++)
		{
			if (!pBdCur->SetBufferAddr(pRxBuffer))
			{
				CLAP_IP_CORE_LOG_ERROR << "Rx set buffer addr 0x" << std::hex << pRxBuffer << std::dec << " on BD " << pBdCur->GetName() << " failed" << std::endl;
				return false;
			}

			const uint64_t bdLength = std::min(remainingSize, static_cast<uint64_t>(maxPktByteLen));
			remainingSize -= bdLength;

			if (!pBdCur->SetLength(bdLength, m_bdRingRx.GetMaxTransferLen()))
			{
				CLAP_IP_CORE_LOG_ERROR << "Rx set length " << bdLength << " on BD " << pBdCur->GetName() << " failed" << std::endl;
				return false;
			}

			pBdCur->SetControlBits(0);
			pBdCur->SetId(i); // Arbitrary ID

			pRxBuffer += bdLength;
			pBdCur = pBdCur->GetNextDesc();
		}

		return true;
	}

	bool resetDescs(const bool& isTx, const uint32_t& maxTransLen, const uint32_t& numBd, SGDescriptor* pBdSet)
	{
		SGDescriptor* pCurBd = pBdSet;

		if (isTx && !(pCurBd->GetControl() & CTRL_TXSOF_MASK))
		{
			CLAP_IP_CORE_LOG_ERROR << "Tx first BD does not have SOF" << std::endl;
			return false;
		}

		for (uint32_t i = 0; i < numBd - 1; i++)
		{
			if ((pCurBd->GetLength() & maxTransLen) == 0)
			{
				CLAP_IP_CORE_LOG_ERROR << "0 length BD at index: " << i << std::endl;
				return false;
			}

			pCurBd->ClearComplete();
			pCurBd = pCurBd->GetNextDesc();
		}

		if (isTx && !(pCurBd->GetControl() & CTRL_TXEOF_MASK))
		{
			CLAP_IP_CORE_LOG_ERROR << "Tx last BD does not have EOF" << std::endl;
			return false;
		}

		if ((pCurBd->GetLength() & maxTransLen) == 0)
		{
			CLAP_IP_CORE_LOG_ERROR << "0 length BD" << std::endl;
			return false;
		}

		pCurBd->ClearComplete();

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

		m_mm2sCurChunk = m_mm2sChunks.front();
		m_mm2sChunks.pop();

		m_mm2sStatReg.Reset();

		// Set the RunStop bit
		m_mm2sCtrlReg.Start();
		// Set the source address
		setMM2SSrcAddr(m_mm2sCurChunk.addr);
		// Set the amount of bytes to transfer
		setMM2SByteLength(m_mm2sCurChunk.length);
	}

	void startS2MMTransfer()
	{
		if (m_s2mmChunks.empty())
		{
			CLAP_IP_CORE_LOG_ERROR << "No S2MM chunks available!" << std::endl;
			return;
		}

		m_s2mmCurChunk = m_s2mmChunks.front();
		m_s2mmChunks.pop();

		m_s2mmStatReg.Reset();

		// Set the RunStop bit
		m_s2mmCtrlReg.Start();
		// Set the destination address
		setS2MMDestAddr(m_s2mmCurChunk.addr);
		// Set the amount of bytes to transfer
		setS2MMByteLength(m_s2mmCurChunk.length);
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
		m_maxTransferLengths[ch2Id(DMAChannel::MM2S)] = (1 << m_bufLenRegWidth) / m_dataWidths[ch2Id(DMAChannel::MM2S)];
		m_maxTransferLengths[ch2Id(DMAChannel::S2MM)] = (1 << m_bufLenRegWidth) / m_dataWidths[ch2Id(DMAChannel::S2MM)];

		m_bdRingTx.SetMaxTransferLen((1 << m_bufLenRegWidth) - 1);
		m_bdRingRx.SetMaxTransferLen((1 << m_bufLenRegWidth) - 1);

		CLAP_LOG_DEBUG << "Max transfer length: MM2S=" << m_maxTransferLengths[ch2Id(DMAChannel::MM2S)] << ", S2MM=" << m_maxTransferLengths[ch2Id(DMAChannel::S2MM)] << std::endl;
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

	TransferChunk m_mm2sCurChunk = {};
	TransferChunk m_s2mmCurChunk = {};

	ChunkResults m_s2mmChunkResults = {};

	bool m_mm2sPresent = false;
	bool m_s2mmPresent = false;

	int32_t m_mm2sIntrDetected = INTR_UNDEFINED;
	int32_t m_s2mmIntrDetected = INTR_UNDEFINED;
};
} // namespace clap