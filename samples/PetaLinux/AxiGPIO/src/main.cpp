#include <iostream>

// Needs to be defined before CLAP.hpp is included
//#define EMBEDDED_XILINX
#include <CLAP.hpp>
#include <IP_Cores/AxiGPIO.hpp>
#include <IP_Cores/AxiInterruptController.hpp>

static constexpr uint64_t AXI_GPIO_1_BASE_ADDR           = 0x41200000;
static constexpr uint64_t AXI_GPIO_2_BASE_ADDR           = 0x41210000;
static constexpr uint64_t AXI_INTERRUPT_CONTROLLER_BASE_ADDR = 0x41800000;

volatile bool intrDone = false;

void InterruptCallBack(const clap::AxiGPIO::Channel& channel, const uint32_t& port, const bool& value)
{
	std::cout << "Interrupt Triggered - Channel: " << channel << " Port: " << port << " Value: " << value << std::endl;
	intrDone = true;
}

int main()
{
		clap::logging::SetVerbosity(clap::logging::Verbosity::VB_DEBUG);
		clap::CLAPPtr pClap = clap::CLAP::Create<clap::backends::PetaLinuxBackend>();

		// AxiGPIO 1 is connected as input to 4 buttons on channel 1 and as an output to 4 LEDs on channel 2
		// AxigPIO 2 is connected as an input to 4 switches on channel 1
//		clap::AxiGPIO axiGPIO1(pClap, AXI_GPIO_1_BASE_ADDR);
		clap::AxiGPIO axiGPIO2(pClap, AXI_GPIO_2_BASE_ADDR);

		// Trigger a reset in the axiGPIO
//		axiGPIO1.Reset();
		axiGPIO2.Reset();

/*		clap::AxiInterruptController axiInterruptController(pClap, AXI_INTERRUPT_CONTROLLER_BASE_ADDR);

		if (axiInterruptController.AutoDetectInterruptID())
			axiInterruptController.Start();
		else
			axiInterruptController.Start(0);

		// Setup Interrupts
		axiGPIO1.UseInterruptController(axiInterruptController);

		if (axiGPIO1.AutoDetectInterruptID())
			axiGPIO1.EnableInterrupts();
		else
			axiGPIO1.EnableInterrupts(0);
*/
		axiGPIO2.EnableInterrupts(1);

//		axiGPIO1.RegisterInterruptCallback(InterruptCallBack);
		axiGPIO2.RegisterInterruptCallback(InterruptCallBack);

//		axiGPIO1.Start();
		axiGPIO2.Start();

//		axiGPIO1.SetGPIOBit(clap::AxiGPIO::CHANNEL_2, 0, true);

		// Wait for 10 seconds
		//std::this_thread::sleep_for(std::chrono::seconds(10));
		while(!intrDone);

		std::cout << "-------AFTER INTR-------" << std::endl;
//		axiGPIO1.SetGPIOBit(clap::AxiGPIO::CHANNEL_2, 0, false);

		// Stop axiGPIO 1 & 2
//		axiGPIO1.Stop();
		axiGPIO2.Stop();

		// Stop the axiInterruptController
//		axiInterruptController.Stop();
	std::cout << "END" << std::endl;

	return 0;
}
