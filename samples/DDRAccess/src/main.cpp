#include <iostream>

#include <xdmaAccess.h>

// The DDR is located at 0x000000000
static const uint64_t DDR_BASE_ADDR = 0x000000000;
// The size of the DDR is 4GB
static const uint64_t DDR_SIZE = 0x100000000;

const uint64_t TEST_DATA_SIZE = 8;

int main()
{
	// Create an XDMA object
	XDMAShr pXdma = XDMA::Create<PCIeBackend>();
	// Add a DDR memory region to the XDMA
	pXdma->AddMemoryRegion(XDMA::MemoryType::DDR, DDR_BASE_ADDR, DDR_SIZE);

	// Create host side buffer for the test data to be written to the input memory
	XDMABuffer<uint8_t> testData(TEST_DATA_SIZE, 0);
	// Create host side buffer for the data read from the destination memory
	XDMABuffer<uint8_t> testDataRB(TEST_DATA_SIZE, 0);
	// Create host side buffer to set the destination memory to 0xFF,
	// this way it is easy to observe if the process worked or not
	XDMABuffer<uint8_t> ff(TEST_DATA_SIZE, 0xFF);

	testData[0] = 0xDE;
	testData[1] = 0xAD;
	testData[2] = 0xBE;
	testData[3] = 0xEF;
	testData[4] = 0xAF;
	testData[5] = 0xFE;
	testData[6] = 0x13;
	testData[7] = 0x37;

	// Allocate memory for the data on the devices DDR
	Memory buf = pXdma->AllocMemoryDDR(TEST_DATA_SIZE, static_cast<uint64_t>(sizeof(uint8_t)));

	// Write 0xFF to the memory, in this case, this operation writes data directly into the DDR
	// attached to the FPGA. The data is written to the address specified by the buf object.
	pXdma->Write(buf, ff);

	// Readback the output data buffer and print it to show that it is indeed all 0xFF.
	// This is done to show that the data is actually being written to the DDR memory.
	pXdma->Read(buf, testDataRB);

	std::cout << "Printing Memory Before Transfer:" << std::endl;
	for (const uint32_t& d : testDataRB)
		std::cout << std::hex << static_cast<int32_t>(d) << " " << std::flush;

	std::cout << std::endl
			  << std::endl;

	// Write the actual input data to the DDR memory.
	pXdma->Write(buf, testData);

	// Readback the result data from the DDR memory.
	pXdma->Read(buf, testDataRB);

	// Print the result data
	std::cout << "Printing Memory After Transfer:" << std::endl;
	for (const uint32_t& d : testDataRB)
		std::cout << std::hex << static_cast<int32_t>(d) << " " << std::flush;

	std::cout << std::endl;

	return 0;
}