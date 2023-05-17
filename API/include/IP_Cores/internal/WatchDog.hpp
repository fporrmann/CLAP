/*
 *  File: WatchDog.hpp
 *  Copyright (c) 2023 Florian Porrmann
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

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "UserInterrupt.hpp"

#include "../../internal/Constants.hpp"
#include "../../internal/Logger.hpp"

#ifndef EMBEDDED_XILINX
#include "../../internal/Timer.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#endif

namespace clap
{
namespace internal
{
static std::exception_ptr g_pExcept = nullptr;

DEFINE_EXCEPTION(WatchDogException)

#ifndef EMBEDDED_XILINX
static void waitForFinishThread(UserInterrupt* pUserIntr, HasStatus* pStatus, Timer* pTimer, std::condition_variable* pCv, [[maybe_unused]] const std::string& name, std::atomic<bool>* pThreadDone)
{
	pThreadDone->store(false, std::memory_order_release);
	pTimer->Start();

	try
	{
		if (pUserIntr->IsSet())
		{
			LOG_DEBUG << "[" << name << "] Interrupt Mode ... " << std::endl;
			pUserIntr->WaitForInterrupt(WAIT_INFINITE);
		}
		else if (pStatus)
		{
			LOG_DEBUG << "[" << name << "] Polling Mode ... " << std::endl;
			while (!pStatus->PollDone())
				std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}
	catch (...)
	{
		// Set the global exception pointer in case of an exception
		g_pExcept = std::current_exception();
	}

	pTimer->Stop();

	pCv->notify_one();
	pThreadDone->store(true, std::memory_order_release);

	LOG_DEBUG << "[" << name << "] Finished" << std::endl;
}
#endif

class WatchDog
{
	DISABLE_COPY_ASSIGN_MOVE(WatchDog)

public:
	WatchDog(const std::string& name) :
		m_name(name),
		m_interrupt()
#ifndef EMBEDDED_XILINX
		,
		m_waitThread(),
		m_cv(),
		m_threadDone(false),
		m_timer()
#endif
	{
	}

	void InitInterrupt([[maybe_unused]] const uint32_t& devNum, [[maybe_unused]] const uint32_t& interruptNum, [[maybe_unused]] HasInterrupt* pReg = nullptr)
	{
#ifdef _WIN32
		LOG_ERROR << CLASS_TAG("WatchDog") << "Error: Interrupts are not supported on Windows." << std::endl;
#else
		m_interrupt.Init(devNum, interruptNum, pReg);
#endif
	}

	void UnsetInterrupt()
	{
#ifdef _WIN32
		LOG_ERROR << CLASS_TAG("WatchDog") << "Error: Interrupts are not supported on Windows." << std::endl;
#else
		m_interrupt.Unset();
#endif
	}

	void SetStatusRegister(HasStatus* pStatus)
	{
		m_pStatus = pStatus;
	}

	void UnsetStatusRegister()
	{
		m_pStatus = nullptr;
	}

	bool Start()
	{
#ifndef EMBEDDED_XILINX
		if (m_threadRunning) return false;

		if (!m_interrupt.IsSet() && m_pStatus == nullptr)
		{
			std::stringstream ss("");
			ss << CLASS_TAG("WatchDog") << "Error: Trying to start WatchDog thread with neither the interrupt nor the status register set.";
			throw WatchDogException(ss.str());
		}

		g_pExcept = nullptr;
		m_threadDone.store(false, std::memory_order_release);
		m_waitThread    = std::thread(waitForFinishThread, &m_interrupt, m_pStatus, &m_timer, &m_cv, m_name, &m_threadDone);
		m_threadRunning = true;
#endif

		return true;
	}

	bool WaitForFinish(const int32_t& timeoutMS = WAIT_INFINITE)
	{
#ifndef EMBEDDED_XILINX
		using namespace std::chrono_literals;

		LOG_DEBUG << CLASS_TAG("WatchDog") << "Core=" << m_name << " timeoutMS=" << (timeoutMS == WAIT_INFINITE ? "Infinite" : std::to_string(timeoutMS)) << std::endl;

		if (!m_threadRunning) return false;

		if (m_threadDone.load(std::memory_order_acquire))
		{
			m_waitThread.join();
			m_threadRunning = false;
			checkException();
			return true;
		}

		std::mutex mtx;
		std::unique_lock<std::mutex> lck(mtx);

		if (timeoutMS == WAIT_INFINITE)
			m_cv.wait_for(lck, 1ms, [this] { return m_threadDone.load(std::memory_order_acquire); });
		else if (m_cv.wait_for(lck, std::chrono::milliseconds(timeoutMS)) == std::cv_status::timeout)
			return false;

		m_waitThread.join();
		m_threadRunning = false;
		checkException();
#else
		while (!m_pStatus->PollDone())
			usleep(1);
#endif

		return true;
	}

	double GetRuntime() const
	{
#ifndef EMBEDDED_XILINX
		return m_timer.GetElapsedTimeInMilliSec();
#else
		return 0.0; // TODO: Add embedded runtime measurement
#endif
	}

	void RegisterInterruptCallback(const std::function<void(uint32_t)>& callback)
	{
		m_interrupt.RegisterCallback(callback);
	}

private:
	void checkException()
	{
		if (g_pExcept)
		{
			try
			{
				std::rethrow_exception(g_pExcept);
			}
			catch ([[maybe_unused]] const std::exception& ex)
			{
			}
		}
	}

private:
	std::string m_name;
	UserInterrupt m_interrupt;
#ifndef EMBEDDED_XILINX
	std::thread m_waitThread;
	std::condition_variable m_cv;
	bool m_threadRunning = false;
	std::atomic<bool> m_threadDone;
	Timer m_timer;
#endif
	HasStatus* m_pStatus = nullptr;
};
} // namespace internal
} // namespace clap