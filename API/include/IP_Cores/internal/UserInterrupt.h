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
#ifdef _WIN32
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
/////////////////////////

#ifndef EMBEDDED_XILINX
/////////////////////////
// Include for poll()
#ifndef _WIN32
#include <poll.h>
#endif
/////////////////////////
#include <functional>
#endif

#include <iostream>

#include "../../internal/Defines.h"
#include "../../internal/Logger.h"
#include "../../internal/Utils.h"
#include "RegisterInterface.h"

DEFINE_EXCEPTION(UserIntrruptException)

class UserInterrupt
{
	DISABLE_COPY_ASSIGN_MOVE(UserInterrupt)

public:
	UserInterrupt()
#ifndef EMBEDDED_XILINX
#ifndef _WIN32
		:
		m_pollFd()
#endif
#endif
	{}

	~UserInterrupt()
	{
		Unset();
	}

	void Init(const uint32_t& devNum, const uint32_t& interruptNum, HasInterrupt* pReg = nullptr)
	{
		if (!DEVICE_HANDLE_VALID(m_fd))
			Unset();

		m_devName = "/dev/xdma" + std::to_string(devNum) + "_events_" + std::to_string(interruptNum);
		m_pReg    = pReg;

		m_fd          = OPEN_DEVICE(m_devName.c_str(), READ_ONLY_FLAG);
		int32_t errsv = errno;

		if (!DEVICE_HANDLE_VALID(m_fd))
		{
			std::stringstream ss;
			ss << CLASS_TAG("UserInterrupt") << "Unable to open device " << m_devName << "; errno: " << errsv;
			throw UserIntrruptException(ss.str());
		}

#ifndef EMBEDDED_XILINX
#ifndef _WIN32
		m_pollFd.fd     = m_fd;
		m_pollFd.events = POLLIN;
#endif
#endif
		m_interruptNum = interruptNum;
	}

	void Unset()
	{
		if (!DEVICE_HANDLE_VALID(m_fd)) return;

		CLOSE_DEVICE(m_fd);
		m_fd = INVALID_HANDLE;
#ifndef EMBEDDED_XILINX
#ifndef _WIN32
		m_pollFd.fd = -1;
#endif
#endif
		m_pReg = nullptr;
	}

	bool IsSet() const
	{
		return (DEVICE_HANDLE_VALID(m_fd));
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE)
	{
#ifdef _WIN32
		LOG_ERROR << CLASS_TAG("UserInterrupt") << " Currently not implemented for Windows" << std::endl;
		return false;
#else
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

			LOG_DEBUG << CLASS_TAG("UserInterrupt") << "Interrupt present on " << m_devName << ", events: " << events << ", Interrupt Mask: " << (m_pReg ? std::to_string(m_pReg->GetLastInterrupt()) : "No Status Register Specified") << std::endl;
			return true;
		}
		else
			LOG_DEBUG << CLASS_TAG("UserInterrupt") << "No Interrupt present on " << m_devName << std::endl;
#endif // EMBEDDED_XILINX
		return false;
#endif // _WIN32
	}

	void RegisterCallback([[maybe_unused]] const std::function<void(uint32_t)>& callback)
	{
#ifndef EMBEDDED_XILINX
		m_callbacks.push_back(callback);
#endif
	}

private:
	std::string m_devName = "";
	HasInterrupt* m_pReg  = nullptr;
	DeviceHandle m_fd     = INVALID_HANDLE;
#ifndef EMBEDDED_XILINX
#ifndef _WIN32
	struct pollfd m_pollFd;
#endif
	std::vector<std::function<void(uint32_t)>> m_callbacks = {};
#endif
	uint32_t m_interruptNum = 0;
};
