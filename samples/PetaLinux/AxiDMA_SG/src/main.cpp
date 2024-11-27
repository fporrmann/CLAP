#include <iostream>
#include <numeric>
#include <random>

#include <CLAP.hpp>
#include <IP_Cores/AxiDMA.hpp>
#include <IP_Cores/AxiInterruptController.hpp>

// The DDR is located at 0x20000000
static constexpr uint64_t DDR_BASE_ADDR = 0x20000000;
// The size of the DDR is 512MB
static constexpr uint64_t DDR_SIZE = 0x20000000;

static constexpr uint64_t AXI_INTERRUPT_CONTROLLER_BASE_ADDR = 0x41800000;

static constexpr uint64_t AXI_DMA_BASE_ADDR      = 0x40400000;
static constexpr uint64_t AXI_MM2S_DMA_BASE_ADDR = 0x40410000;
static constexpr uint64_t AXI_S2MM_DMA_BASE_ADDR = 0x40420000;

#define MAX_PKT_BYTE_LEN           0x400
#define NUMBER_OF_BDS_PER_PKT      2
#define NUMBER_OF_PKTS_TO_TRANSFER 11
#define NUMBER_OF_BDS_TO_TRANSFER  (NUMBER_OF_PKTS_TO_TRANSFER * NUMBER_OF_BDS_PER_PKT)

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
	uint32_t testDataSize = 0x100 * NUMBER_OF_BDS_TO_TRANSFER;

	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_int_distribution<std::mt19937::result_type> dist(0, 0xFFFFFFFF);

	// Create host side buffer for the test data to be written to the input memory
	clap::CLAPBuffer<uint32_t> testData(testDataSize, 0);
	// Create host side buffer for the data read from the destination memory
	clap::CLAPBuffer<uint32_t> testDataRB(testDataSize, 0);

	clap::CLAPBuffer<uint32_t> zeroData(testDataSize, 0);

	//clap::logging::SetVerbosity(clap::logging::Verbosity::VB_DEBUG);
	try
	{
		// Create an XDMA object
		clap::CLAPPtr pClap = clap::CLAP::Create<clap::backends::PetaLinuxBackend>();
		// Add a DDR memory region to the XDMA
		pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, DDR_BASE_ADDR, DDR_SIZE);

		// Allocate memory for the data on the devices DDR
		clap::Memory inBuf  = pClap->AllocMemoryDDR(testDataSize, sizeof(uint32_t));
		clap::Memory outBuf = pClap->AllocMemoryDDR(testDataSize, sizeof(uint32_t));

		std::cout << "Input Buffer : " << inBuf << std::endl;
		std::cout << "Output Buffer: " << outBuf << std::endl;

		// Fill the test data buffer with random data
		std::generate(testData.begin(), testData.end(), [&]() { return dist(rng); });

		// Write the test data to the input buffer
		pClap->Write(inBuf, testData);
		clap::AxiDMA<uint32_t> axiDMA(pClap, AXI_DMA_BASE_ADDR);

		clap::AxiDMA<uint32_t> axiDMAmm2s(pClap, AXI_MM2S_DMA_BASE_ADDR);
		clap::AxiDMA<uint32_t> axiDMAs2mm(pClap, AXI_S2MM_DMA_BASE_ADDR);
		// Trigger a reset in the AxiDMA
		axiDMA.Reset();

		axiDMAmm2s.Reset();
		axiDMAs2mm.Reset();

		clap::AxiInterruptController axiInterruptController(pClap, AXI_INTERRUPT_CONTROLLER_BASE_ADDR);

		if (axiInterruptController.AutoDetectInterruptID())
			axiInterruptController.Start();
		else
			axiInterruptController.Start(0);

		// Setup Interrupts
		axiDMA.UseInterruptController(axiInterruptController);

		if (axiDMA.AutoDetectInterruptID())
			axiDMA.EnableInterrupts(clap::USE_AUTO_DETECT, clap::USE_AUTO_DETECT);
		else
			axiDMA.EnableInterrupts(0, 1);

		if (axiDMAmm2s.AutoDetectInterruptID())
			axiDMAmm2s.EnableInterrupts(DMAChannel::MM2S, clap::USE_AUTO_DETECT);
		else
			axiDMAmm2s.EnableInterrupts(DMAChannel::MM2S, 2);
		if (axiDMAs2mm.AutoDetectInterruptID())
			axiDMAs2mm.EnableInterrupts(DMAChannel::S2MM, clap::USE_AUTO_DETECT);
		else
			axiDMAs2mm.EnableInterrupts(DMAChannel::S2MM, 3);

		if (!axiDMA.IsSGEnabled())
		{
			std::cout << "SG Mode is not enabled" << std::endl;
			return -1;
		}

		clap::Memory txBdMem = pClap->AllocMemoryDDR(NUMBER_OF_BDS_TO_TRANSFER, std::size_t(0x40));
		clap::Memory rxBdMem = pClap->AllocMemoryDDR(NUMBER_OF_BDS_TO_TRANSFER, std::size_t(0x40));

		std::cout << "Tx BD Memory: " << txBdMem << std::endl;
		std::cout << "Rx BD Memory: " << rxBdMem << std::endl;

		for (uint32_t i = 0; i < 100; i++)
		{
			pClap->Write(outBuf, zeroData);
			axiDMA.StartSG(txBdMem, rxBdMem, inBuf, outBuf, MAX_PKT_BYTE_LEN, NUMBER_OF_PKTS_TO_TRANSFER, NUMBER_OF_BDS_PER_PKT);

			// Wait until the MM2S channel finishes (an interrupt occures on complete)
			if (axiDMA.WaitForFinish(DMAChannel::MM2S))
				std::cout << "Channel: MM2S finished successfully - Runtime: " << axiDMA.GetMM2SRuntime() << " ms" << std::endl;

			// Wait until the S2MM channel finishes (an interrupt occures on complete)
			if (axiDMA.WaitForFinish(DMAChannel::S2MM))
				std::cout << "Channel: S2MM finished successfully - Runtime: " << axiDMA.GetS2MMRuntime() << " ms" << std::endl;

			// Readback the result data from the DDR memory.
			pClap->Read(outBuf, testDataRB);

			// Validate the result data
			bool success = true;
			for (uint32_t j = 0; j < testDataSize; j++)
			{
				if (testDataRB[j] != testData[j])
				{
					if (success)
						std::cout << "Test failed at index: " << j << std::endl;
					success = false;
				}
			}

			if (success)
				std::cout << "Test successful" << std::endl;
			else
				std::cout << "Test failed" << std::endl;
		}

		std::cout << "---------------------------- Testing MM2S and S2MM channels separately ----------------------------" << std::endl;

		pClap->Write(outBuf, zeroData);
		axiDMAmm2s.StartSG(DMAChannel::MM2S, txBdMem, inBuf, MAX_PKT_BYTE_LEN, NUMBER_OF_PKTS_TO_TRANSFER, NUMBER_OF_BDS_PER_PKT);
		axiDMAs2mm.StartSG(DMAChannel::S2MM, rxBdMem, outBuf, MAX_PKT_BYTE_LEN, NUMBER_OF_PKTS_TO_TRANSFER, NUMBER_OF_BDS_PER_PKT);

		// Wait until the MM2S channel finishes (an interrupt occures on complete)
		if (axiDMAmm2s.WaitForFinish(DMAChannel::MM2S))
			std::cout << "Channel: MM2S finished successfully - Runtime: " << axiDMAmm2s.GetMM2SRuntime() << " ms" << std::endl;

		// Wait until the S2MM channel finishes (an interrupt occures on complete)
		if (axiDMAs2mm.WaitForFinish(DMAChannel::S2MM))
			std::cout << "Channel: S2MM finished successfully - Runtime: " << axiDMAs2mm.GetS2MMRuntime() << " ms" << std::endl;

		// Readback the result data from the DDR memory.
		pClap->Read(outBuf, testDataRB);

		// Validate the result data
		bool success = true;
		for (uint32_t j = 0; j < testDataSize; j++)
		{
			if (testDataRB[j] != testData[j])
			{
				if (success)
					std::cout << "Test failed at index: " << j << std::endl;
				success = false;
			}
		}

		if (success)
			std::cout << "Test successful" << std::endl;
		else
			std::cout << "Test failed" << std::endl;

		axiInterruptController.Stop();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}
