/* 
 *  File: UserInterrupt.h
 *  Copyright (c) 2021 Florian Porrmann
 *  
 *  MIT License
 *  
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *  
 */

#pragma once

/////////////////////////
// Includes for open()
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/////////////////////////

#ifndef EMBEDDED_XILINX
/////////////////////////
// Include for poll()
#include <poll.h>
/////////////////////////
#include <functional>
#endif

#include <iostream>

#include "../../internal/Utils.h"
#include "RegisterInterface.h"

DEFINE_EXCEPTION(UserIntrruptException)

class UserInterrupt
{
	DISABLE_COPY_ASSIGN_MOVE(UserInterrupt)

public:
	UserInterrupt() :
#ifndef EMBEDDED_XILINX
		m_pollFd()
#endif
	{}

	~UserInterrupt()
	{
		Unset();
	}

	void Init(const uint32_t& devNum, const uint32_t& interruptNum, HasInterrupt* pReg = nullptr)
	{
		if (m_fd != -1)
			Unset();

		m_devName = "/dev/xdma" + std::to_string(devNum) + "_events_" + std::to_string(interruptNum);
		m_pReg    = pReg;

		m_fd          = open(m_devName.c_str(), O_RDONLY);
		int32_t errsv = errno;

		if (m_fd < 0)
		{
			std::stringstream ss;
			ss << CLASS_TAG("UserInterrupt") << "Unable to open device " << m_devName << "; errno: " << errsv;
			throw UserIntrruptException(ss.str());
		}

#ifndef EMBEDDED_XILINX
		m_pollFd.fd     = m_fd;
		m_pollFd.events = POLLIN;
#endif
		m_interruptNum = interruptNum;
	}

	void Unset()
	{
		if (m_fd == -1) return;

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
		if (!IsSet())
		{
			std::stringstream ss("");
			ss << CLASS_TAG("UserInterrupt") << "Error: Trying to wait for uninitialized user interrupt";
			throw UserIntrruptException(ss.str());
		}

#ifndef EMBEDDED_XILINX
		// Poll checks whether an interrupt was generated.
		uint32_t rd = poll(&m_pollFd, 1, timeout);
		if ((rd > 0) && (m_pollFd.revents & POLLIN))
		{
			uint32_t events;

			if (m_pReg)
				m_pReg->ClearInterrupts();

			// Check how many interrupts were generated, and clear the interrupt so we can detect future interrupts.
			int32_t rc    = pread(m_fd, &events, sizeof(events), 0);
			int32_t errsv = errno;

			if (rc < 0)
			{
				std::stringstream ss;
				ss << CLASS_TAG("UserInterrupt") << m_devName << ", call to pread failed (rc: " << rc << ") errno: " << errsv;
				throw UserIntrruptException(ss.str());
			}

			for (auto& callback : m_callbacks)
				callback(m_pReg->GetLastInterrupt());

#ifdef XDMA_VERBOSE
			std::cout << CLASS_TAG("UserInterrupt") << "Interrupt present on " << m_devName << ", events: " << events << ", Interrupt Mask: " << (m_pReg ? std::to_string(m_pReg->GetLastInterrupt()) : "No Status Register Specified") << std::endl;
#endif
			return true;
		}
#ifdef XDMA_VERBOSE
		else
			std::cout << CLASS_TAG("UserInterrupt") << "No Interrupt present on " << m_devName << std::endl;
#endif
#endif
		return false;
	}

	void RegisterCallback(const std::function<void(uint32_t)>& callback)
	{
		m_callbacks.push_back(callback);
	}

private:
	std::string m_devName = "";
	HasInterrupt* m_pReg  = nullptr;
	int32_t m_fd          = -1;
#ifndef EMBEDDED_XILINX
	struct pollfd m_pollFd;
	std::vector<std::function<void(uint32_t)>> m_callbacks = {};
#endif
	uint32_t m_interruptNum = 0;
};
