#include <iostream>

#include <CLAP.hpp>

#include "testCore.hpp"

// The DDR is located at 0x000000000
static const uint64_t DDR_BASE_ADDR = 0x000000000;
// The size of the DDR is 4GB
static const uint64_t DDR_SIZE = 0x100000000;

// The HLS core control registers are located at 0x100100000
static const uint64_t HLS_TEST_CORE_BASE_ADDR = 0x100100000;

static const uint32_t TEST_DATA_SIZE = 8;

void InterruptCallBack(const uint32_t& mask)
{
	std::cout << "Interrupt Triggered - Mask: " << mask << std::endl;
}

int main()
{
	// Create host side buffer for the test data to be written to the input memory
	clap::XDMABuffer<uint16_t> testData(TEST_DATA_SIZE, 0);
	// Create host side buffer for the data read from the destination memory
	clap::XDMABuffer<uint32_t> testDataRB(TEST_DATA_SIZE, 0);
	// Create host side buffer to set the destination memory to 0xFFFFFFFF,
	// this way it is easy to observe if the process worked or not
	clap::XDMABuffer<uint32_t> ff(TEST_DATA_SIZE, 0xFFFFFFFF);

	try
	{
		// Create a CLAP object
		clap::CLAPPtr pClap = clap::CLAP::Create<clap::backends::PCIeBackend>();
		// Add a DDR memory region
		pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, DDR_BASE_ADDR, DDR_SIZE);

		// Create a testCore object, whose control registers are located at HLS_TEST_CORE_BASE_ADDR
		// with the name "HLS_Test".
		testCore hlsTest(pClap, HLS_TEST_CORE_BASE_ADDR, "HLS_Test");

		testData[0] = 0xDEAD;
		testData[1] = 0xBEEF;
		testData[2] = 0xAFFE;
		testData[3] = 0x1337;
		testData[4] = 0x4242;
		testData[5] = 0x1234;
		testData[6] = 0x5678;
		testData[7] = 0xABCD;

		// Allocate memory for the data on the devices DDR
		clap::Memory inBuf  = pClap->AllocMemoryDDR(TEST_DATA_SIZE, static_cast<uint64_t>(sizeof(uint16_t)));
		clap::Memory outBuf = pClap->AllocMemoryDDR(TEST_DATA_SIZE, static_cast<uint64_t>(sizeof(uint32_t)));

#if 0
		// Set the addresses of the input and output memory used in the HLS core.
		hlsTest.SetPDDRInAddr(inBuf);
		hlsTest.SetPDDROutAddr(outBuf);
		// Set the number of elements to process, as this is a plain value, instead of a memory
		// address the actual value is passed.
		hlsTest.SetElementsAddr(TEST_DATA_SIZE);
#else
		hlsTest.Init(inBuf, outBuf, TEST_DATA_SIZE);
#endif

		// Write 0xFFFFFFFF to the memory, in this case, this operation writes data directly into the DDR
		// attached to the FPGA. The data is written to the address specified by the outBuf object.
		pClap->Write(outBuf, ff);

		// Readback the output data buffer and print it to show that it is indeed all 0xFFFFFFFF.
		// This is done to show that the data is actually being written to the DDR memory.
		pClap->Read(outBuf, testDataRB);

		std::cout << "Printing Output Memory Before HLS Execution:" << std::endl;
		for (const uint32_t& d : testDataRB)
			std::cout << std::hex << d << std::dec << " " << std::flush;

		std::cout << std::endl
				  << std::endl;

		// Write the actual input data to the DDR memory.
		pClap->Write(inBuf, testData);

		// Configure the HLS core to use its interrupt signal to determine when the core is finished.
		// The number (0) specifies which of the up to 16 interrupt events the HLS core is connected to.
		// If only one interrupt is connected to the XDMA core, the number is always 0. When multiple
		// interrupts are connected, the ordering of the concat used to combine the signals determines
		// the mapping of the cores to the interrupt events.
		// If the interrupt is not configure, polling is used to determine when the core is finished.
		hlsTest.EnableInterrupts(0);

		// Register a callback function to be called when the interrupt is triggered.
		// The callback function is called with the interrupt mask as an argument.
		hlsTest.RegisterInterruptCallback(InterruptCallBack);

		// Start the HLS core
		hlsTest.Start();

		// Wait for the HLS core to finish
		hlsTest.WaitForFinish();

		// Print the runtime of the HLS core in milliseconds
		std::cout << "HLS core finished after: " << hlsTest.GetRuntime() << "ms" << std::endl
				  << std::endl;

		// Readback the result data from the DDR memory.
		pClap->Read(outBuf, testDataRB.data());
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	}

	// Print the result data
	std::cout << "Printing Output Memory After HLS Execution:" << std::endl;
	for (const uint32_t& d : testDataRB)
		std::cout << std::hex << d << std::dec << " " << std::flush;

	std::cout << std::endl;

	return 0;
}