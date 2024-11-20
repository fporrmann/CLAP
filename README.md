# CLAP

## Overview

CLAP is a C++ API aiming to simplify the usage of IP Cores in Xilinx FPGAs: 
- C++ 17, header-only (The entire source code is located in `API/include` and its subfolders)
- Unified API to access IP Cores via PCIe (Xilinx XDMA), PetaLinux, or Bare Metal
- Contains quickly learned interfaces to Xilinx DMA, VDMA, GPIO, or user-created HLS cores (AP_intf)
- It makes the time-consuming familiarization with Linux driver development superfluous. Write easy-to-debug code running in user space without caring about low-level device access.


![Architecture](doc/CLAP_Overview.drawio.svg)


NOTE: This is a work in progress. Current limitations:
- XDMA: Only Linux is supported
- Bare Metal is under development


## Requirements

	CMake >= 3.10.0
	g++ >= 9 or comparable compiler supporting C++17



### XDMA Driver [optional]

Only required when using an FPGA board, connected to a host system via PCIe.

Use the latest version from the [official git](https://github.com/Xilinx/dma_ip_drivers).

**Checkout and build**

```bash
git clone https://github.com/Xilinx/dma_ip_drivers
cd dma_ip_drivers/XDMA/linux-kernel/xdma
make
sudo make install
```

**Test load the driver**

```bash
cd ../tests
sudo ./load_driver.sh
```

#### Additional settings for both:

**Allow users to access the XDMA devices**

```bash
echo "SUBSYSTEM==\"xdma\", GROUP=\"users\", MODE=\"0666\"" | sudo tee /etc/udev/rules.d/60-xdma.rules
sudo udevadm trigger
```

**Check permissions**

The following command should output several xdma0_xxxx devices, e.g., xdma0_c2h_0 and xdma0_h2c_0, the number of devices and which are available depends on the configuration of the XDMA endpoint. If no devices are displayed make sure the FPGA is plugged in and has been programmed.

```bash
ls -la /dev/xdma*
```

**Automatically load the driver on boot**

```bash
echo "xdma" | sudo tee /etc/modules
echo "options xdma poll_mode=1" | sudo tee /etc/modprobe.d/xdma_options.conf 
```

## API Installation / Usage

For a fully working example please refer to the examples provided in the samples folder.

### Include the API into a project

Clone the git into your project or add it as a submodule

	git clone https://github.com/fporrmann/CLAP.git

When using CMake simply add CLAP as follows:

```cmake
# Find CLAP and initialize its variables
find_package(CLAP PATHS CLAP/API/cmake/modules REQUIRED)

# Add CLAP to the include directories
include_directories(${CLAP_INCLUDE_DIRS})

# Link against the libraries required by the CLAP
target_link_libraries (<YOUR_PROJECT_NAME> PRIVATE ${CLAP_LIBS})
```

When not using CMake, add `CLAP/API/include` to the include search path of your environment and link against pthread (or whichever threading library is used by your compiler in combination with std::thread).

### Use the API

Please refer to the [DDRAccess example](samples/XDMA/DDRAccess/src/main.cpp).

## PetaLinux

### Add the UIO driver to the kernel

1. Open the kernel configuration
```bash
petalinux-config -c kernel
```
2. Enable the UIO driver
```bash
Device Drivers -> Userspace I/O drivers -> Userspace platform driver with generic irq and dynamic memory
```
3. Save the configuration and exit
4. Open the PetaLinux configuration
```bash
petalinux-config
```
5. Modify the boot arguments to include the UIO driver
```bash
DTG-settings -> Kernel bootargs -> Add extra boot args
```
6. Add the following
```bash
uio_pdrv_genirq.of_id=generic-uio
```
6. Save the configuration and exit
7. Build PetaLinux, using `petalinux-build`

### Setup IP core in the device tree to use the UIO driver

1. Find the IP core object name in the device tree, the object name is usually the same as the unique name of the IP block in Vivado. An `AXI DMA` core for example by default is called `axi_dma_ID`, where `ID` is the instance id of the block, starting at zero. In the device tree, the start of the `AXI DMA` object would look similar to this `axi_dma_0: dma@40010000 {`, with `axi_dma_0` being the object name and `dma@40010000` being the name and address.
```bash
cat components/plnx_workspace/device-tree/device-tree/pl.dtsi
```
2. Open the user overlay dtsi file:
```bash
nano project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi 
```
3. After `};` add the following (replace <OBJ_NAME> with the object name from step 1, e.g., `axi_dma_0`):
```c
&<OBJ_NAME> {
    compatible = "generic-uio";
};
```
4. Build PetaLinux, e.g., using `petalinux-build` or if the project has already been built and only changes to the device tree have been made using `petalinux-build -c kernel`
5. After booting the new kernel a UIO device should be listed under `/dev/` with the name `uioX`, where `X` is the number of the device, starting at zero.

### Add access to memory

The way to make memory accessible differs, depending on the target device.

#### A dedicated memory for the PL exists

In this case, the memory, e.g., a DDR4 module, simply has to be added to the block design, connected to the PL, and assigned an address.
When building PetaLinux, the memory instance then has to be configured to use the UIO driver.
Afterward, the memory will be accessible from within the host system.

---

#### PL and PS share a single memory

This is the often case on low-cost embedded devices such as the Zybo Z7. Here, some of the memory has to be marked as exclusive memory for the FPGA design, this can be done as follows:

1. Open the device tree overlay
```bash
nano project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi 
```
2. Add the following after `/ {` and before `};`
```c
    // Restrict the memory available to the kernel to the first 512MB
    memory {
        device_type = "memory";
        reg = <0x00000000 0x20000000>;
    };

    // Create a new memory region (shm) for the FPGA design, starting at address 0x20000000 with a size of 512MB.
    // The name of the region is shm0 and it can be accessed using the UIO driver.
    shm: shm0@20000000 {
        compatible = "generic-uio";
        reg = <0x20000000 0x20000000>;
    };
```
3. Build PetaLinux, e.g., using `petalinux-build` or if the project has already been built and only changes to the device tree have been made using `petalinux-build -c kernel`

### Using an IP core with multiple interrupt lines in combination with the UIO driver

By default, the UIO driver only supports a single interrupt line. To use an IP core with multiple interrupt lines either the driver needs to be modified or the IP core needs to be interconnected with an `AXI Interrupt Controller`. The latter is the easier approach and is described here.

1. In the Vivado block design add an `AXI Interrupt Controller` to the design
2. Connect the `clk` and `reset` signals of the `AXI Interrupt Controller` to the `clk` and `reset` signals of the IP core
3. Change the `Interrupt Output Connection` of the `AXI Interrupt Controller` to `Single`
4. Add a `Concat` IP core to the design
5. Set the `Number of Ports` of the `Concat` IP core to the number of interrupt lines of the IP core
6. Connect the interrupt lines of the IP core to the input ports of the `Concat` IP core
7. Connect the output port of the `Concat` IP core to the `Interrupt Request (intr)` port of the `AXI Interrupt Controller`
8. Connect the `Interrupt (irq)` port of the `AXI Interrupt Controller` to the `IRQ_F2P` port of the PS

Next, generate a new bitstream, export the hardware, and update the hardware description of the PetaLinux project using:

```bash
petalinux-config --get-hw-description=<PATH_TO_VIVADO_PROJECT>
```

Afterward, the device tree needs to be updated to reflect the changes made to the hardware description, it is especially important to configure the `AXI Interrupt Controller` to use the UIO driver. This can be done by following the steps described in the section [Setup IP core in the device tree to use the UIO driver](#setup-ip-core-in-the-device-tree-to-use-the-uio-driver).

For an example of how to use an `AXI DMA` together with an `AXI Interrupt Controller` please refer to the [PetaLinux AxiDMA Example](samples/PetaLinux/AxiDMA/src/main.cpp).

### Modifications required when trying to use UIO together with an IP core whose interrupt line is connected to an AXI Interrupt Controller

In setups where an `AXI Interrupt Controller` acts as an intermediary between the IP core and the PS by default no UIO device will be created for the IP core. It is currently not entirely clear why this happens, but by removing the `interrupt-parent` property from the device tree object of the IP core this problem can be circumvented.

1. Open the device tree overlay
```bash
nano project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi 
```
2. Change the override for the IP core to the following (replace <OBJ_NAME> with the name of the object, e.g., `axi_dma_0`):
```c
&<OBJ_NAME> {
    compatible = "generic-uio";
    /delete-property/ interrupt-parent;
};
```
4. Build PetaLinux, e.g., using `petalinux-build` or if the project has already been built and only changes to the device tree have been made using `petalinux-build -c kernel`


### Allow users to access the UIO devices without root permissions

#### Variant A (online):
```bash
echo "SUBSYSTEM==\"uio\", GROUP=\"users\", MODE=\"0666\"" | sudo tee /etc/udev/rules.d/uio.rules
sudo udevadm trigger
```


#### Variant B (integrate as bitbake recipe in petalinux-build):

1. Create folder for udev rules recipes in your petalinux folder: 
```
mkdir -p project-spec/meta-user/recipes-core/user-udev-rules/files
```

2. Copy content of [doc/project_spec/...](doc/project-spec/meta-user/recipes-core) to your **project-spec** folder

- place ```user-udev-rules.bb``` into ```project-spec/meta-user/recipes-core/user-udev-rules/```
- place ```99-uio-device.rules``` into ```project-spec/meta-user/recipes-core/user-udev-rules/files```

3. Append to project-spec/meta-user/conf/petalinuxbsp.conf
```
IMAGE_INSTALL:pn-petalinux-image-minimal:append = " user-udev-rules"
```

4. Re-build and deploy. 


## Baremetal

For Baremetal it is currently required to increase the size of the **STACK**, **HEAP**, and **IRQ_STACK** in the linker script `script.ld`. The default values are too small for the CLAP API. The following values are recommended:

```c
STACK_SIZE     = 0x10000;
HEAP_SIZE      = 0x20000;
// Only present on 32-bit systems -- Might need to be increased further, depending on the application
IRQ_STACK_SIZE =  0x2000;
```

Furthermore, the `EMBEDDED_XILINX` define has to be set before including the CLAP API. This can be done by either adding the following to the compiler flags:

```c
-D EMBEDDED_XILINX
```

Or by adding the following to the source code before the first CLAP include:

```c
#define EMBEDDED_XILINX
```

Using the `CLAP_USE_XIL_PRINTF` define, the logging system can be forced to use the Xilinx `xil_printf` function instead of `std::cout`. This can be useful when `std::cout` is not available in the Baremetal environment. Similar to the `EMBEDDED_XILINX` define, it can be set either via the compiler flags:

```c
-D CLAP_USE_XIL_PRINTF
```

Or by adding the following to the source code before the first CLAP include:

```c
#define CLAP_USE_XIL_PRINTF
```


## List of CLAP Specific Defines

- `EMBEDDED_XILINX`: When defined, the API is compiled for a Baremetal environment on a Xilinx FPGA.
- `CLAP_USE_XIL_PRINTF`: When defined, the API uses `xil_printf` instead of `std::cout` for logging.
- `CLAP_DISABLE_SRW_SIG_HANDLER`: When defined, the SoloRunWarden does not install signal handlers for the SIGINT and SIGTERM signals. This can be useful when the application already has signal handlers installed for these signals.
- `CLAP_DISABLE_LOGGING`: When defined, all logging targets, except for `CLAP_LOG_INFO_ALWAYS` are disabled. This can be useful when the application does not require any of the internal logging, except for very specific messages.
- `CLAP_IP_CORE_LOG_ALT_STYLE`: When defined, the logging style of the IP core is changed to a more compact style, integrating the IP core name into the log message. This can be useful when the application requires a more compact log output.