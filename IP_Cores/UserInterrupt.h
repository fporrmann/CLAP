#pragma once

/////////////////////////
// Includes for open()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/////////////////////////

#ifndef EMBEDDED_XILINX
/////////////////////////
// Include for poll()
#include <poll.h>
/////////////////////////
#endif

#include <iostream>

#include "RegisterInterface.h"
#include "../Utils.h"

class UserIntrruptException : public std::exception
{
	public:
		explicit UserIntrruptException(const std::string& what) : m_what(what) {}

		virtual ~UserIntrruptException() throw() {}

		virtual const char* what() const throw()
		{
			return m_what.c_str();
		}

	private:
		std::string m_what;
};


class UserInterrupt
{
	DISABLE_COPY_ASSIGN_MOVE(UserInterrupt)

	public:
		UserInterrupt() :
			m_devName(""),
			m_pReg(nullptr),
			m_fd(-1),
#ifndef EMBEDDED_XILINX
			m_pollFd(),
#endif
			m_interruptNum(0)
		{}

		~UserInterrupt()
		{
			Unset();
		}

		void Init(const uint32_t& devNum, const uint32_t& interruptNum, HasInterrupt* pReg = nullptr)
		{
			if(m_fd != -1)
				Unset();

			m_devName = "/dev/xdma" + std::to_string(devNum) + "_events_" + std::to_string(interruptNum);
			m_pReg = pReg;

			m_fd = open(m_devName.c_str(), O_RDONLY);
			int errsv = errno;

			if (m_fd < 0)
			{
				std::stringstream ss;
				ss << CLASS_TAG("UserInterrupt") << "Unable to open device " << m_devName << "; errno: " << errsv;
				throw UserIntrruptException(ss.str());
			}

#ifndef EMBEDDED_XILINX
			m_pollFd.fd = m_fd;
			m_pollFd.events = POLLIN;
#endif
			m_interruptNum = interruptNum;
		}

		void Unset()
		{
			if(m_fd == -1) return;

			close(m_fd);
			m_fd = -1;
#ifndef EMBEDDED_XILINX
			m_pollFd.fd = -1;
#endif
			m_pReg = nullptr;
		}

		bool IsSet() const
		{
			return (m_fd != -1);
		}

		bool WaitForInterrupt(const int32_t& timeout = WAIT_INFINITE)
		{
			if(!IsSet())
			{
				std::stringstream ss("");
				ss << CLASS_TAG("UserInterrupt") << "Error: Trying to wait for uninitialized user interrupt";
				throw UserIntrruptException(ss.str());
			}

#ifndef EMBEDDED_XILINX
			// Poll checks whether an interrupt was generated. 
			uint32_t rd = poll(&m_pollFd, 1, timeout);
			if((rd > 0) && (m_pollFd.revents & POLLIN))
			{
				uint32_t events;

				if(m_pReg)
					m_pReg->ClearInterrupts();

				// Check how many interrupts were generated, and clear the interrupt so we can detect future interrupts.
				int rc = pread(m_fd, &events, sizeof(events), 0);
				int errsv = errno;
				if(rc < 0)
				{
					std::stringstream ss;
					ss << CLASS_TAG("UserInterrupt") << m_devName << ", call to pread failed (rc: " << rc << ") errno: " << errsv;
					throw UserIntrruptException(ss.str());
				}

#ifdef XDMA_VERBOSE
				std::cout << "Interrupt present on " << m_devName << ", events: " << events << ", Interrupt Mask: " << (m_pReg ? std::to_string(m_pReg->GetLastInterrupt()) : "No Status Register Specified") << std::endl;
#endif
				return true;
			}
#ifdef XDMA_VERBOSE
			else
				std::cout << "No Interrupt present on " << m_devName << std::endl;
#endif
#endif
			return false;
		}


	private:
		std::string m_devName;
		HasInterrupt* m_pReg;
		int m_fd;
#ifndef EMBEDDED_XILINX
		struct pollfd m_pollFd;
#endif
		uint32_t m_interruptNum;
};
