#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "CLAP.hpp"
#include "IP_Cores/AxiDMA.hpp"
#include "IP_Cores/AxiGPIO.hpp"
#include "IP_Cores/AxiInterruptController.hpp"
#include "IP_Cores/AxiQuadSPI.hpp"
#include "IP_Cores/HLSCore.hpp"
#include "IP_Cores/VDMA.hpp"

#include "support/DummyBackend.hpp"
#include "support/DummyConfig.hpp"

namespace offsets
{
constexpr uint64_t GPIO_CTRL_OFFSET = 0x2000;
constexpr uint64_t HLS_CTRL_OFFSET  = 0x3000;
constexpr uint64_t INTC_CTRL_OFFSET = 0x4000;
constexpr uint64_t SPI_CTRL_OFFSET  = 0x5000;
constexpr uint64_t VDMA_CTRL_OFFSET = 0x6000;
constexpr uint64_t DMA_CTRL_OFFSET  = 0x7000;
} // namespace offsets

TEST_CASE("AxiGpioDefaultsAndAccess")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x40000",
	"uio": {
		"scalars": [
			{"addr": "0x2000", "name": "xlnx,is-dual", "value": "1"},
			{"addr": "0x2000", "name": "xlnx,gpio-width", "value": "8"},
			{"addr": "0x2000", "name": "xlnx,gpio2-width", "value": "4"},
			{"addr": "0x2000", "name": "xlnx,tri-default", "value": "0xff"},
			{"addr": "0x2000", "name": "xlnx,tri-default-2", "value": "0x0f"},
			{"addr": "0x2000", "name": "xlnx,dout-default", "value": "0xaa"},
			{"addr": "0x2000", "name": "xlnx,dout-default-2", "value": "0x55"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	clap::AxiGPIO gpio(pClap, offsets::GPIO_CTRL_OFFSET, clap::AxiGPIO::DualChannel::Yes, clap::AxiGPIO::ResetOnInit::Yes, "gpio");

	REQUIRE(gpio.GetGPIOBits(clap::AxiGPIO::CHANNEL_1) == 0xAA);
	REQUIRE(gpio.GetGPIOBits(clap::AxiGPIO::CHANNEL_2) == 0x55);

	gpio.SetGPIOBit(clap::AxiGPIO::CHANNEL_1, 0, true);
	REQUIRE(gpio.GetGPIOBit(clap::AxiGPIO::CHANNEL_1, 0) == 1);

	gpio.SetGPIOState(clap::AxiGPIO::CHANNEL_1, 1, clap::AxiGPIO::INPUT);
	const uint64_t triReg = pClap->Read32(offsets::GPIO_CTRL_OFFSET + 0x04);
	REQUIRE((triReg & (1u << 1)) != 0);

	bool threw = false;

	try
	{
		gpio.SetGPIOBit(clap::AxiGPIO::CHANNEL_1, 31, true);
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}

	REQUIRE(threw);
}

TEST_CASE("AxiGpioInterruptCallbacks")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x40000",
	"uio": {
		"scalars": [
			{"addr": "0x2000", "name": "xlnx,is-dual", "value": "1"},
			{"addr": "0x2000", "name": "xlnx,gpio-width", "value": "8"},
			{"addr": "0x2000", "name": "xlnx,gpio2-width", "value": "4"},
			{"addr": "0x2000", "name": "xlnx,dout-default", "value": "0xaa"},
			{"addr": "0x2000", "name": "xlnx,dout-default-2", "value": "0x55"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	clap::AxiGPIO gpio(pClap, offsets::GPIO_CTRL_OFFSET, clap::AxiGPIO::DualChannel::Yes, clap::AxiGPIO::ResetOnInit::Yes, "gpio");

	uint32_t callbackPort = 0;
	bool callbackHit      = false;
	bool callbackValue    = false;

	gpio.RegisterInterruptCallback([&](const clap::AxiGPIO::Channel& channel, const uint32_t& port, const bool& value) {
		if (channel == clap::AxiGPIO::CHANNEL_1)
		{
			callbackHit   = true;
			callbackPort  = port;
			callbackValue = value;
		}
	});

	pClap->Write32(offsets::GPIO_CTRL_OFFSET + 0x00, 0xAB);
	gpio.InterruptTriggered(clap::AxiGPIO::INTR_ON_CH1);

	REQUIRE(callbackHit);
	REQUIRE(callbackPort == 0);
	REQUIRE(callbackValue == true);

	gpio.EnableInterrupts(0, clap::AxiGPIO::INTR_ON_CH1);
	REQUIRE(gpio.Start());
	gpio.Stop();
	REQUIRE(gpio.OnFinished());
}

TEST_CASE("HlsCoreControlAndDataAddr")
{
	const uint64_t apCtrlAddr = offsets::HLS_CTRL_OFFSET + 0x0;

	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x40000",
	"mem_bytes": [
		{"addr": "0x3000", "value": "0x4"}
	],
	"apctrl_autocomplete": ["0x3000"],
	"uio": {
		"ids": [
			{"addr": "0x3000", "id": "3"}
		],
		"vectors": [
			{"addr": "0x3000", "name": "interrupts", "values": ["3", "0"]}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	clap::HLSCore core(pClap, offsets::HLS_CTRL_OFFSET, "hls");

	core.SetDataAddr<uint32_t>(0x10, 0x1234);
	REQUIRE(core.GetDataAddr<uint32_t>(0x10) == 0x1234);

	REQUIRE(core.Start());
	REQUIRE(core.WaitForFinish(100));

	const uint8_t status = pClap->Read8(apCtrlAddr);
	REQUIRE((status & 0x2) != 0);

	core.EnableInterrupts();
	const uint64_t gieReg = pClap->Read8(offsets::HLS_CTRL_OFFSET + 0x4);
	REQUIRE((gieReg & 0x1) != 0);

	core.Stop();
}

TEST_CASE("AxiInterruptControllerCallbacks")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x40000",
	"reg": {
		"values": [
			{"addr": "0x4000", "width": "4", "value": "0x1"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPCIeBackend>(0, 0, true);
	clap::AxiInterruptController intc(pClap, offsets::INTC_CTRL_OFFSET, "intc");

	auto intr        = intc.MakeUserInterrupt();
	bool callbackHit = false;

	intr->RegisterCallback([&callbackHit](uint32_t) { callbackHit = true; });
	intr->Init(pClap->GetDevNum(), 0, nullptr);

	// Set ISR bit 0 after controller reset to simulate an active interrupt
	pClap->Write32(offsets::INTC_CTRL_OFFSET + 0x00, 0x1);

	intc.CoreInterruptTriggered(0);

	REQUIRE(callbackHit);
	const uint64_t ackReg = pClap->Read32(offsets::INTC_CTRL_OFFSET + 0x0C);
	REQUIRE((ackReg & 0x1) != 0);
}

TEST_CASE("VDMAStartStopAndReset")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x40000",
	"reg": {
		"hooks": [
			{"addr": "0x6000", "width": "4", "set_on_write": "0", "clear_on_write": "0x4", "set_on_read": "0", "clear_on_read": "0"},
			{"addr": "0x6030", "width": "4", "set_on_write": "0", "clear_on_write": "0x4", "set_on_read": "0", "clear_on_read": "0"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	clap::VDMA<uint32_t> vdma(pClap, offsets::VDMA_CTRL_OFFSET, "vdma");

	vdma.Reset();
	vdma.EnableInterrupts(0, 1);
	vdma.Start(DMAChannel::MM2S, 0x1000, 128, 4);
	vdma.Start(DMAChannel::S2MM, 0x2000, 128, 4);

	REQUIRE(vdma.GetMM2SSrcAddr() == 0x1000);
	REQUIRE(vdma.GetS2MMDestAddr() == 0x2000);
	REQUIRE(vdma.GetMM2SHSize() == 128);
	REQUIRE(vdma.GetS2MMHSize() == 128);

	vdma.Stop();
}

TEST_CASE("AxiDMABasicTransferSetup")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x80000",
	"uio": {
		"scalars": [
			{"addr": "0x7000", "name": "xlnx,sg-length-width", "value": "14"},
			{"addr": "0x7000", "name": "/dma-channel@7000/xlnx,datawidth", "value": "32"},
			{"addr": "0x7000", "name": "/dma-channel@7030/xlnx,datawidth", "value": "32"},
			{"addr": "0x7000", "name": "/dma-channel@7000/xlnx,include-dre", "value": "1"},
			{"addr": "0x7000", "name": "/dma-channel@7030/xlnx,include-dre", "value": "1"}
		]
	},
	"reg": {
		"values": [
			{"addr": "0x7004", "width": "4", "value": "0x2"},
			{"addr": "0x7034", "width": "4", "value": "0x2"}
		],
		"hooks": [
			{"addr": "0x7000", "width": "4", "set_on_write": "0", "clear_on_write": "0x4", "set_on_read": "0", "clear_on_read": "0"},
			{"addr": "0x7030", "width": "4", "set_on_write": "0", "clear_on_write": "0x4", "set_on_read": "0", "clear_on_read": "0"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	clap::AxiDMA<uint32_t> dma(pClap, offsets::DMA_CTRL_OFFSET, true, true, "dma");

	REQUIRE(dma.GetMaxTransferLength(DMAChannel::MM2S) > 0);
	REQUIRE(dma.GetMaxTransferLength(DMAChannel::MM2S) == 0x1000); // 1 < 14 (length-width) / 32 (datawidth)
	REQUIRE(dma.GetMaxTransferLength(DMAChannel::S2MM) > 0);
	REQUIRE(dma.GetMaxTransferLength(DMAChannel::S2MM) == 0x1000); // 1 < 14 (length-width) / 32 (datawidth)

	dma.Start(0x1000, 64, 0x2000, 64);
	const uint64_t mm2sAddr = pClap->Read32(offsets::DMA_CTRL_OFFSET + 0x18);
	const uint64_t s2mmAddr = pClap->Read32(offsets::DMA_CTRL_OFFSET + 0x48);
	REQUIRE(mm2sAddr == 0x1000);
	REQUIRE(s2mmAddr == 0x2000);

	const uint64_t mm2sLen = pClap->Read32(offsets::DMA_CTRL_OFFSET + 0x28);
	const uint64_t s2mmLen = pClap->Read32(offsets::DMA_CTRL_OFFSET + 0x58);
	REQUIRE(mm2sLen == 64);
	REQUIRE(s2mmLen == 64);
}

TEST_CASE("AxiDMASGDescriptorInit")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x80000",
	"uio": {
		"scalars": [
			{"addr": "0x7000", "name": "xlnx,sg-length-width", "value": "14"},
			{"addr": "0x7000", "name": "/dma-channel@7000/xlnx,datawidth", "value": "32"},
			{"addr": "0x7000", "name": "/dma-channel@7030/xlnx,datawidth", "value": "32"},
			{"addr": "0x7000", "name": "/dma-channel@7000/xlnx,include-dre", "value": "1"},
			{"addr": "0x7000", "name": "/dma-channel@7030/xlnx,include-dre", "value": "1"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, 0x10000, 0x4000);

	clap::Memory bdMem   = pClap->AllocMemory(clap::CLAP::MemoryType::DDR, 0x400);
	clap::Memory dataMem = pClap->AllocMemory(clap::CLAP::MemoryType::DDR, 0x800);

	clap::AxiDMA<uint32_t> dma(pClap, offsets::DMA_CTRL_OFFSET, true, true, "dma");
	auto container = dma.PreInitSGDescs(DMAChannel::MM2S, bdMem, dataMem, 128, 1, 1);

	REQUIRE(!container.GetDescriptors().empty());
	REQUIRE(container.GetDescriptors().size() == 16);
	REQUIRE(container.GetNumPkts() == 1);
}

TEST_CASE("AxiDMARxDescriptorsAndInterruptDetect")
{
	auto guard = dummyConfig::SetBackendConfig(
		R"json({
	"memory_size": "0x80000",
	"uio": {
		"scalars": [
			{"addr": "0x7000", "name": "xlnx,sg-length-width", "value": "14"},
			{"addr": "0x7000", "name": "/dma-channel@7000/xlnx,datawidth", "value": "32"},
			{"addr": "0x7000", "name": "/dma-channel@7030/xlnx,datawidth", "value": "32"},
			{"addr": "0x7000", "name": "/dma-channel@7000/xlnx,include-dre", "value": "1"},
			{"addr": "0x7000", "name": "/dma-channel@7030/xlnx,include-dre", "value": "1"}
		],
		"vectors": [
			{"addr": "0x7000", "name": "interrupts", "values": ["5", "0", "6", "0"]}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, 0x10000, 0x8000);

	clap::Memory bdMem   = pClap->AllocMemory(clap::CLAP::MemoryType::DDR, 0x800);
	clap::Memory dataMem = pClap->AllocMemory(clap::CLAP::MemoryType::DDR, 0x1000);

	clap::AxiDMA<uint32_t> dma(pClap, offsets::DMA_CTRL_OFFSET, true, true, "dma");
	REQUIRE(dma.AutoDetectInterruptID());

	auto container = dma.PreInitSGDescs(DMAChannel::S2MM, bdMem, dataMem, 256, 1, 1);
	REQUIRE(!container.GetDescriptors().empty());
	REQUIRE(container.GetDescriptors().size() == 32);
}
