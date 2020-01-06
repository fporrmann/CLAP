#pragma once


#include <mutex>
#include <thread>
#include <condition_variable>

#include "UserInterrupt.h"
#include "../Timer.h"

static std::exception_ptr g_pExcept = nullptr;

class WatchDogException : public std::exception
{
	public:
		explicit WatchDogException(const std::string& what) : m_what(what) {}

		virtual ~WatchDogException() throw() {}

		virtual const char* what() const throw()
		{
			return m_what.c_str();
		}

	private:
		std::string m_what;
};


static void waitForFinishThread(UserInterrupt* pUserIntr, HasStatus* pStatus, Timer* pTimer, std::condition_variable* pCv, const std::string& name)
{
	UNUSED(name);
	pTimer->Start();

	try
	{
		if(pUserIntr->IsSet())
		{
#ifdef XDMA_VERBOSE
			std::cout << "[" << name << "] Interrupt Mode ... " << std::endl;
#endif
			pUserIntr->WaitForInterrupt(WAIT_INFINITE);
		}
		else if(pStatus)
		{
#ifdef XDMA_VERBOSE
			std::cout << "[" << name << "] Polling Mode ... " << std::endl;
#endif
			while(!pStatus->PollDone())
				usleep(1);
		}
	}
	catch(...)
	{
		//Set the global exception pointer in case of an exception
		g_pExcept = std::current_exception();
	}

	pTimer->Stop();

#ifdef XDMA_VERBOSE
	std::cout << "[" << name << "] Finished" << std::endl;
#endif

	pCv->notify_one();
}





class WatchDog
{
	DISABLE_COPY_ASSIGN_MOVE(WatchDog)

	public:
		WatchDog(const std::string& name) :
			m_name(name),
			m_interrupt(),
			m_waitThread(),
			m_cv(),
			m_threadRunning(false),
			m_pStatus(nullptr),
			m_timer()
		{
		}

		void InitInterrupt(const uint32_t& devNum, const uint32_t& interruptNum, HasInterrupt* pReg = nullptr)
		{
			m_interrupt.Init(devNum, interruptNum, pReg);
		}

		void UnsetInterrupt()
		{
			m_interrupt.Unset();
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
			if(m_threadRunning) return false;

			if(!m_interrupt.IsSet() && m_pStatus == nullptr)
			{
				std::stringstream ss("");
				ss << CLASS_TAG("WatchDog") << "Error: Trying to start WatchDog thread with neither the interrupt nor the status register set.";
				throw WatchDogException(ss.str());
			}

			g_pExcept = nullptr;
			m_waitThread = std::thread(waitForFinishThread, &m_interrupt, m_pStatus, &m_timer, &m_cv, m_name);
			m_threadRunning = true;

			return true;
		}

		bool WaitForFinish(const int32_t& timeoutMS = WAIT_INFINITE)
		{
			if(m_waitThread.joinable())
			{
				m_waitThread.join();
				m_threadRunning = false;
				checkException();
				return true;
			}

			std::mutex mtx;
			std::unique_lock<std::mutex> lck(mtx);

			if(timeoutMS == WAIT_INFINITE)
				m_cv.wait(lck);
			else if(m_cv.wait_for(lck, std::chrono::milliseconds(timeoutMS)) == std::cv_status::timeout)
				return false;

			m_waitThread.join();
			m_threadRunning = false;
			checkException();

			return true;
		}

		double GetRuntime() const
		{
			return m_timer.GetElapsedTimeInMilliSec();
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
				catch (const std::exception &ex)
				{
				}
			}
		}

	private:
		std::string m_name;
		UserInterrupt m_interrupt;
		std::thread m_waitThread;
		std::condition_variable m_cv;
		bool m_threadRunning;
		HasStatus* m_pStatus;
		Timer m_timer;
};