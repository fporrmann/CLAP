# XDMA API

A simple, header-only C++ API for the Xilinx XDMA IP-Core.<br>
NOTE: Currently only linux is supported.


## Requirements

	CMake >= 3.10.0
	g++ >= 9 or comparable compiler supporting C++17

### XDMA Driver

Either use the latest version from the [official git](https://github.com/Xilinx/dma_ip_drivers) or the version (v2020.2.0) included here.

#### Using the official git:

**Checkout and build**

	git clone https://github.com/Xilinx/dma_ip_drivers
	cd dma_ip_drivers/XDMA/linux-kernel/xdma
	make
	sudo make install

**Test load the driver**

	cd ../tests
	sudo ./load_driver.sh


#### Using the included driver:

**Build**

	cd driver/xdma
	make
	sudo make install

**Test load the driver**

	cd ..
	sudo ./load_driver.sh

#### Additional settings for both:

**Allow users to access the XDMA devices**

	echo "SUBSYSTEM==\"xdma\", GROUP=\"users\", MODE=\"0666\"" | sudo tee /etc/udev/rules.d/60-xdma.rules
	sudo udevadm trigger

**Check the permissions**

The following command should output several xdma0_xxxx devices, e.g., xdma0_c2h_0 and xdma0_h2c_0, the number of devices and which are available depends on the configuration of the XDMA endpoint. If no devices are displayed make sure the FPGA is plugged in and has been programmed.

	ls -la /dev/xdma*

**Automatically load the driver on boot**

	echo "xdma" | sudo tee /etc/modules
	echo "options xdma poll_mode=1" | sudo tee /etc/modprobe.d/xdma_options.conf 

## Usage

For a fully working example please refer to the examples provided in the samples folder.

### Include the API into a project

Clone the git into your project or add it as a submodule

	git clone https://github.com/fporrmann/XDMA_API.git

When using CMake simply add the XDMA API as follows:

	# Initialize the XDMA CMake variables
	add_subdirectory(XDMA_API/API)

	# Add the XDMA API to the include directories
	include_directories(${XDMA_API_INCLUDE_DIRS})

	# Link against the libraries required by the XDMA API
	target_link_libraries (<YOUR_PROJECT_NAME> PRIVATE ${XDMA_API_LIBS})

When not using CMake, add XDMA_API/API/include to the include search path of your environment and link against pthread (or which ever threading library is used by your compiler in combination with std::thread).

### Use the API

At the moment please refer to the [DDRAccess example](samples/DDRAccess/src/main.cpp).
