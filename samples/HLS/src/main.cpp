#include <iostream>

#define XDMA_VERBOSE

#include <IP_Cores/HLSCore.h>
#include <xdmaAccess.h>

// DDR is located at 0x000000000
static const uint64_t DDR_BASE_ADDR = 0x000000000;
// DDR is 4GB in size
static const uint64_t DDR_SIZE = 0x100000000;

// The HLS core control registers are located at 0x100100000
static const uint64_t HLS_TEST_CORE_BASE_ADDR = 0x100100000;

static const uint64_t TEST_CONTROL_ADDR_PDDRIN_DATA   = 0x10;
static const uint64_t TEST_CONTROL_ADDR_PDDROUT_DATA  = 0x1c;
static const uint64_t TEST_CONTROL_ADDR_ELEMENTS_DATA = 0x28;

static const uint64_t TEST_DATA_SIZE = 8;

int main()
{
	// Create host side buffer for the test data to be written to the input memory
	std::vector<uint16_t> testData(TEST_DATA_SIZE, 0);
	// Create host side buffer for the data read from the destination memory
	std::vector<uint32_t> testDataRB(TEST_DATA_SIZE, 0);
	// Create host side buffer to set the destination memory to 0xFF,
	// this way it is easy to observe if the process worked or not
	std::vector<uint32_t> ff(TEST_DATA_SIZE, 0xFFFFFFFF);

	try
	{
		// Create an XDMA object
		XDMA xdma(std::make_shared<PCIeBackend>());
		// Add a DDR memory region to the XDMA
		xdma.AddMemoryRegion(XDMA::DDR, DDR_BASE_ADDR, DDR_SIZE);

		HLSCore hlsTest(&xdma, HLS_TEST_CORE_BASE_ADDR, "HLS_Test");

		testData[0] = 0xDEAD;
		testData[1] = 0xBEEF;
		testData[2] = 0xAFFE;
		testData[3] = 0x1337;
		testData[4] = 0x4242;
		testData[5] = 0x1234;
		testData[6] = 0x5678;
		testData[7] = 0xABCD;

		// Allocate memory for the data on the devices DDR
		Memory inBuf  = xdma.AllocMemoryDDR(TEST_DATA_SIZE, sizeof(uint16_t));
		Memory outBuf = xdma.AllocMemoryDDR(TEST_DATA_SIZE, sizeof(uint32_t));

		hlsTest.SetDataAddr(TEST_CONTROL_ADDR_PDDRIN_DATA, inBuf);
		hlsTest.SetDataAddr(TEST_CONTROL_ADDR_PDDROUT_DATA, outBuf);
		hlsTest.SetDataAddr(TEST_CONTROL_ADDR_ELEMENTS_DATA, TEST_DATA_SIZE);

		// Write 0xFFFFFFFF to the memory
		xdma.Write(outBuf, ff.data());

		// Readback the output data buffer and print it to show that it is indeed all 0xFFFFFFFF
		xdma.Read(outBuf, testDataRB.data());

		std::cout << "Printing Output Memory Before HLS Execution:" << std::endl;
		for(const uint32_t &d : testDataRB)
			std::cout << std::hex << d << std::dec << " " << std::flush;

		std::cout << std::endl
				  << std::endl;

		// Write the input data to the device
		xdma.Write(inBuf, testData.data());

		// hlsTest.EnableInterrupts(0);

		// Start the HLS core
		hlsTest.Start();

		// Wait for the HLS core to finish
		hlsTest.WaitForFinish();

		// Readback the result data and print it, this time it should not be all 0xFF
		xdma.Read(outBuf, testDataRB.data());
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	}

	std::cout << "Printing Output Memory After HLS Execution:" << std::endl;
	for (const uint32_t& d : testDataRB)
		std::cout << std::hex << d << std::dec << " " << std::flush;

	std::cout << std::endl;

	return 0;
}