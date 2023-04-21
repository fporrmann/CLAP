#include <iostream>

#include <IP_Cores/HLSCore.h>
#include <xdmaAccess.h>

// The DDR is located at 0x000000000
static const uint64_t DDR_BASE_ADDR = 0x000000000;
// The size of the DDR is 4GB
static const uint64_t DDR_SIZE = 0x100000000;

// The HLS core control registers are located at 0x100100000
static const uint64_t HLS_TEST_CORE_BASE_ADDR = 0x100100000;

// The HLS core control register ofsets for the different paramters
static const uint64_t TEST_CONTROL_ADDR_PDDRIN_DATA   = 0x10;
static const uint64_t TEST_CONTROL_ADDR_PDDROUT_DATA  = 0x1c;
static const uint64_t TEST_CONTROL_ADDR_ELEMENTS_DATA = 0x28;

static const uint32_t TEST_DATA_SIZE = 8;

int main()
{
	// Create host side buffer for the test data to be written to the input memory
	XDMABuffer<uint16_t> testData(TEST_DATA_SIZE, 0);
	// Create host side buffer for the data read from the destination memory
	XDMABuffer<uint32_t> testDataRB(TEST_DATA_SIZE, 0);
	// Create host side buffer to set the destination memory to 0xFFFFFFFF,
	// this way it is easy to observe if the process worked or not
	XDMABuffer<uint32_t> ff(TEST_DATA_SIZE, 0xFFFFFFFF);

	try
	{
		// Create an XDMA object
		XDMAPtr pXdma = XDMA::Create<PCIeBackend>();
		// Add a DDR memory region to the XDMA
		pXdma->AddMemoryRegion(XDMA::MemoryType::DDR, DDR_BASE_ADDR, DDR_SIZE);

		// Create an HLS core object, whose control registers are located at HLS_TEST_CORE_BASE_ADDR
		// with the name "HLS_Test".
		HLSCore hlsTest(pXdma, HLS_TEST_CORE_BASE_ADDR, "HLS_Test");

		testData[0] = 0xDEAD;
		testData[1] = 0xBEEF;
		testData[2] = 0xAFFE;
		testData[3] = 0x1337;
		testData[4] = 0x4242;
		testData[5] = 0x1234;
		testData[6] = 0x5678;
		testData[7] = 0xABCD;

		// Allocate memory for the data on the devices DDR
		Memory inBuf  = pXdma->AllocMemoryDDR(TEST_DATA_SIZE, static_cast<uint64_t>(sizeof(uint16_t)));
		Memory outBuf = pXdma->AllocMemoryDDR(TEST_DATA_SIZE, static_cast<uint64_t>(sizeof(uint32_t)));

		// Set the addresses of the input and output memory used in the HLS core.
		hlsTest.SetDataAddr(TEST_CONTROL_ADDR_PDDRIN_DATA, inBuf);
		hlsTest.SetDataAddr(TEST_CONTROL_ADDR_PDDROUT_DATA, outBuf);
		// Set the number of elements to process, as this is a plain value, instead of a memory
		// address the actual value is passed.
		hlsTest.SetDataAddr(TEST_CONTROL_ADDR_ELEMENTS_DATA, TEST_DATA_SIZE);

		// Write 0xFFFFFFFF to the memory, in this case, this operation writes data directly into the DDR
		// attached to the FPGA. The data is written to the address specified by the outBuf object.
		pXdma->Write(outBuf, ff);

		// Readback the output data buffer and print it to show that it is indeed all 0xFFFFFFFF.
		// This is done to show that the data is actually being written to the DDR memory.
		pXdma->Read(outBuf, testDataRB);

		std::cout << "Printing Output Memory Before HLS Execution:" << std::endl;
		for (const uint32_t& d : testDataRB)
			std::cout << std::hex << d << std::dec << " " << std::flush;

		std::cout << std::endl
				  << std::endl;

		// Write the actual input data to the DDR memory.
		pXdma->Write(inBuf, testData);

		// Configure the HLS core to use its interrupt signal to determine when the core is finished.
		// The number (0) specifies which of the up to 16 interrupt events the HLS core is connected to.
		// If only one interrupt is connected to the XDMA core, the number is always 0. When multiple
		// interrupts are connected, the ordering of the concat used to combine the signals determines
		// the mapping of the cores to the interrupt events.
		// If the interrupt is not configure, polling is used to determine when the core is finished.
		hlsTest.EnableInterrupts(0);

		// Start the HLS core
		hlsTest.Start();

		// Wait for the HLS core to finish
		hlsTest.WaitForFinish();

		// Print the runtime of the HLS core in milliseconds
		std::cout << "HLS core finished after: " << hlsTest.GetRuntime() << "ms" << std::endl
				  << std::endl;

		// Readback the result data from the DDR memory.
		pXdma->Read(outBuf, testDataRB.data());
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