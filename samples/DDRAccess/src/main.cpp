#include <iostream>

#include <xdmaAccess.h>

// DDR is located at 0x000000000
static const uint64_t DDR_BASE_ADDR = 0x000000000;
// DDR is 1GB in size
static const uint64_t DDR_SIZE = 0x100000000;

const uint64_t TEST_DATA_SIZE = 8;

int main()
{
	// Create an XDMA object
	XDMA xdma(std::make_shared<PCIeBackend>());
	// Add a DDR memory region to the XDMA
	xdma.AddMemoryRegion(XDMA::DDR, DDR_BASE_ADDR, DDR_SIZE);

	// Create host side buffer for the test data to be written to the input memory
	std::vector<uint8_t> testData(TEST_DATA_SIZE, 0);
	// Create host side buffer for the data read from the destination memory
	std::vector<uint8_t> testDataRB(TEST_DATA_SIZE, 0);
	// Create host side buffer to set the destination memory to 0xFF,
	// this way it is easy to observe if the process worked or not
	std::vector<uint8_t> ff(TEST_DATA_SIZE, 0xFF);

	testData[0] = 0xDE;
	testData[1] = 0xAD;
	testData[2] = 0xBE;
	testData[3] = 0xEF;
	testData[4] = 0xAF;
	testData[5] = 0xFE;
	testData[6] = 0x13;
	testData[7] = 0x37;

	// Allocate memory for the data on the devices DDR
	Memory buf = xdma.AllocMemoryDDR(TEST_DATA_SIZE, sizeof(uint8_t));

	// Write 0xFF to the memory
	xdma.Write(buf, ff.data());

	// Readback the output data and print it to show that it is indeed all 0xFF
	xdma.Read(buf, testDataRB.data(), TEST_DATA_SIZE);

	std::cout << "Printing Memory Before Transfer:" << std::endl;
	for (uint32_t i = 0; i < TEST_DATA_SIZE; i++)
		std::cout << std::hex << (int)testDataRB.at(i) << " " << std::flush;

	std::cout << std::endl
			  << std::endl;

	// Write the input data to the device
	xdma.Write(buf, testData.data());

	// Readback the result data and print it, this time it should not be all 0xFF
	xdma.Read(buf, testDataRB.data(), TEST_DATA_SIZE);

	std::cout << "Printing Memory After Transfer:" << std::endl;
	for (uint32_t i = 0; i < TEST_DATA_SIZE; i++)
		std::cout << std::hex << (int)testDataRB.at(i) << " " << std::flush;

	std::cout << std::endl;

	return 0;
}