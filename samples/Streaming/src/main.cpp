#include <iostream>
#include <numeric>
#include <vector>

#include <xdmaAccess.h>

using Buffer = std::vector<uint32_t, xdma::AlignmentAllocator<uint32_t, XDMA_ALIGNMENT>>;

int main(int argc, char** argv)
{
	uint64_t testDataSize = 1024;

	if (argc > 1)
		testDataSize = std::atoi(argv[1]);

	// Create an XDMA object
	XDMAShr pXdma = XDMA::Create<PCIeBackend>();

	// Create host side buffer for the test data to be written to the input memory
	Buffer testData(testDataSize, 0);
	// Create host side buffer for the data read from the destination memory
	Buffer testDataRB(testDataSize, 0);

	// Initialize the test data with increasing values
	std::iota(testData.begin(), testData.end(), 0);

	// Start the XDMA read stream
	pXdma->StartReadStream(testDataRB);

	// Start the XDMA write stream
	pXdma->StartWriteStream(testData);

	// Wait for the read and write stream to finish
	pXdma->WaitForStreams();

	// Print the runtime of the read and write stream
	std::cout << "Read Stream Runtime: " << pXdma->GetReadStreamRuntime() << " ms" << std::endl;
	std::cout << "Write Stream Runtime: " << pXdma->GetWriteStreamRuntime() << " ms" << std::endl;

	// Print the performance of the read and write stream
	std::cout << "Read Performance: " << (testDataSize * sizeof(uint32_t) / 1024.0 / 1024.0) / (pXdma->GetReadStreamRuntime() / 1000.0) << " MB/s" << std::endl;
	std::cout << "Write Performance: " << (testDataSize * sizeof(uint32_t) / 1024.0 / 1024.0) / (pXdma->GetWriteStreamRuntime() / 1000.0) << " MB/s" << std::endl;

	// Validate the result data
	bool success = true;
	for (uint64_t i = 0; i < testDataSize; i++)
	{
		// +10 here due to the HLS core used in the example, remove incase of standard circular XDMA example
		if (testDataRB[i] != (testData[i] + 10))
			success = false;
	}

	if (success)
		std::cout << "Test successful" << std::endl;
	else
		std::cout << "Test failed" << std::endl;

	return 0;
}
