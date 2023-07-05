#include <iostream>
#include <numeric>

#include <CLAP.hpp>
#include <IP_Cores/AxiDMA.hpp>
#include <IP_Cores/AxiInterruptController.hpp>

// The DDR is located at 0x20000000
static const uint64_t DDR_BASE_ADDR = 0x20000000;
// The size of the DDR is 512MB
static const uint64_t DDR_SIZE = 0x20000000;

static const uint64_t AXI_DMA_1_BASE_ADDR                = 0x40010000; // S2MM
static const uint64_t AXI_DMA_2_BASE_ADDR                = 0x40020000; // MM2S
static const uint64_t AXI_INTERRUPT_CONTROLLER_BASE_ADDR = 0x40030000;

int main(int argc, char** argv)
{
	uint32_t testDataSize = 1024;

	if (argc > 1)
		testDataSize = std::atoi(argv[1]);

	// Create host side buffer for the test data to be written to the input memory
	clap::CLAPBuffer<uint32_t> testData(testDataSize, 0);
	// Create host side buffer for the data read from the destination memory
	clap::CLAPBuffer<uint32_t> testDataRB(testDataSize, 0);
	// Create host side buffer to set the destination memory to 0xFFFFFFFF,
	// this way it is easy to observe if the process worked or not
	clap::CLAPBuffer<uint32_t> ff(testDataSize, 0xFFFFFFFF);

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

		// Initialize the test data with increasing values
		std::iota(testData.begin(), testData.end(), 0);

		// Write the test data to the input buffer
		pClap->Write(inBuf, testData);

		clap::AxiDMA<uint32_t> axiDMA1(pClap, AXI_DMA_1_BASE_ADDR, false);
		clap::AxiDMA<uint32_t> axiDMA2(pClap, AXI_DMA_2_BASE_ADDR, true, false);
		// Trigger a reset in the AxiDMA
		axiDMA1.Reset();
		axiDMA2.Reset();

		clap::AxiInterruptController axiInterruptController(pClap, AXI_INTERRUPT_CONTROLLER_BASE_ADDR);
		if(!axiInterruptController.AutoDetectInterruptID())
			axiInterruptController.EnableInterrupt(0);

		axiInterruptController.Start();

		axiDMA1.UseInterruptController(axiInterruptController);
		axiDMA2.UseInterruptController(axiInterruptController);

		if(axiDMA1.AutoDetectInterruptID())
			axiDMA1.EnableInterrupts();
		else
			axiDMA1.EnableInterrupts(1);

		if(axiDMA2.AutoDetectInterruptID())
			axiDMA2.EnableInterrupts();
		else
			axiDMA2.EnableInterrupts(0);

		// clap::logging::SetVerbosity(clap::logging::Verbosity::VB_DEBUG);

		for (int i = 0; i < 8; i++)
		{
			axiDMA2.Start(inBuf);
			axiDMA1.Start(outBuf);

			// Wait until the MM2S channel finishes (an interrupt occures on complete)
			if (axiDMA2.WaitForFinish())
				std::cout << "Channel: MM2S finished successfully - Runtime: " << axiDMA2.GetMM2SRuntime() << " ms" << std::endl;

			// Wait until the S2MM channel finishes (an interrupt occures on complete)
			if (axiDMA1.WaitForFinish())
				std::cout << "Channel: S2MM finished successfully - Runtime: " << axiDMA1.GetS2MMRuntime() << " ms" << std::endl;

			std::cout << " ---------------------- " << std::endl
					  << std::endl;
		}

		// Stop the AxiDMA engine
		axiDMA1.Stop();
		axiDMA2.Stop();

		// Readback the result data from the DDR memory.
		pClap->Read(outBuf, testDataRB);

		// Validate the result data
		bool success = true;
		for (uint32_t i = 0; i < testDataSize; i++)
		{
			if (testDataRB[i] != testData[i])
				success = false;
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
