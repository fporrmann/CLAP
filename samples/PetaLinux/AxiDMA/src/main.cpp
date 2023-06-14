#include <iostream>

#include <CLAP.hpp>
#include <IP_Cores/AxiDMA.hpp>
#include <IP_Cores/AxiInterruptController.hpp>

// The DDR is located at 0x20000000
static const uint64_t DDR_BASE_ADDR = 0x20000000;
// The size of the DDR is 512MB
static const uint64_t DDR_SIZE = 0x20000000;

static const uint64_t AXI_DMA_BASE_ADDR                  = 0x40010000;
static const uint64_t AXI_INTERRUPT_CONTROLLER_BASE_ADDR = 0x40020000;

int main(int argc, char** argv)
{
	uint32_t testDataSize = 1024;

	if (argc > 1)
		testDataSize = std::atoi(argv[1]);

	// Create host side buffer for the test data to be written to the input memory
	clap::XDMABuffer<uint32_t> testData(testDataSize, 0);
	// Create host side buffer for the data read from the destination memory
	clap::XDMABuffer<uint32_t> testDataRB(testDataSize, 0);
	// Create host side buffer to set the destination memory to 0xFFFFFFFF,
	// this way it is easy to observe if the process worked or not
	clap::XDMABuffer<uint32_t> ff(testDataSize, 0xFFFFFFFF);

	try
	{
		// Create an XDMA object
		clap::CLAPPtr pClap = clap::CLAP::Create<clap::backends::PetaLinuxBackend>();
		// Add a DDR memory region to the XDMA
		pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, DDR_BASE_ADDR, DDR_SIZE);

		// Allocate memory for the data on the devices DDR
		clap::Memory inBuf  = pClap->AllocMemoryDDR(testDataSize, static_cast<uint64_t>(sizeof(uint32_t)));
		clap::Memory outBuf = pClap->AllocMemoryDDR(testDataSize, static_cast<uint64_t>(sizeof(uint32_t)));

		std::cout << "Input Buffer : " << inBuf << std::endl;
		std::cout << "Output Buffer: " << outBuf << std::endl;

		clap::AxiDMA<uint32_t> axiDMA(pClap, AXI_DMA_BASE_ADDR);
		// Trigger a reset in the AxiDMA
		axiDMA.Reset();

		clap::AxiInterruptController axiInterruptController(pClap, AXI_INTERRUPT_CONTROLLER_BASE_ADDR);
		axiInterruptController.Start(0);

		axiDMA.UseInterruptController(axiInterruptController);

		axiDMA.EnableInterrupts(0, 1);

		// clap::logging::SetVerbosity(clap::logging::Verbosity::VB_DEBUG);

		for (int i = 0; i < 8; i++)
		{
			axiDMA.Start(inBuf, outBuf);

			// Wait until the MM2S channel finishes (an interrupt occures on complete)
			if (axiDMA.WaitForFinish(DMAChannel::MM2S))
				std::cout << "Channel: MM2S finished successfully" << std::endl;

			// Wait until the S2MM channel finishes (an interrupt occures on complete)
			if (axiDMA.WaitForFinish(DMAChannel::S2MM))
				std::cout << "Channel: S2MM finished successfully" << std::endl;

			std::cout << " ---------------------- " << std::endl << std::endl;
		}

		// Stop the AxiDMA engine
		axiDMA.Stop();
		axiInterruptController.Stop();

		// Readback the result data from the DDR memory.
		pClap->Read(outBuf, testDataRB.data());
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}
