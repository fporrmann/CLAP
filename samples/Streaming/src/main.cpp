#include <iostream>
#include <numeric>
#include <vector>

#include <CLAP.hpp>

int main(int argc, char** argv)
{
	uint32_t testDataSize = 1024;

	if (argc > 1)
		testDataSize = std::atoi(argv[1]);

	// Create a CLAP object
	clap::CLAPPtr pClap = clap::CLAP::Create<clap::backends::PCIeBackend>();

	// Create host side buffer for the test data to be written to the input memory
	clap::XDMABuffer<uint32_t> testData(testDataSize, 0);
	// Create host side buffer for the data read from the destination memory
	clap::XDMABuffer<uint32_t> testDataRB(testDataSize, 0);

	// Initialize the test data with increasing values
	std::iota(testData.begin(), testData.end(), 0);

	// Start the XDMA read stream
	pClap->StartReadStream(testDataRB);

	// Start the XDMA write stream
	pClap->StartWriteStream(testData);

	// Wait for the read and write stream to finish
	pClap->WaitForStreams();

	// Print the runtime of the read and write stream
	std::cout << "Read Stream Runtime: " << pClap->GetReadStreamRuntime() << " ms" << std::endl;
	std::cout << "Write Stream Runtime: " << pClap->GetWriteStreamRuntime() << " ms" << std::endl;

	// Print the performance of the read and write stream
	std::cout << "Read Performance: " << (testDataSize * sizeof(uint32_t) / 1024.0 / 1024.0) / (pClap->GetReadStreamRuntime() / 1000.0) << " MB/s" << std::endl;
	std::cout << "Write Performance: " << (testDataSize * sizeof(uint32_t) / 1024.0 / 1024.0) / (pClap->GetWriteStreamRuntime() / 1000.0) << " MB/s" << std::endl;

	// Validate the result data
	bool success = true;
	for (uint32_t i = 0; i < testDataSize; i++)
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
