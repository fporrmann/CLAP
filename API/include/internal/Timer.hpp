/*
 *  File: Timer.hpp
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

#ifndef EMBEDDED_XILINX
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#endif

#include <ostream>
#include <string>

#include "Logger.hpp"

namespace clap
{
#ifdef PRINT_MU_SEC
#define TIME_FUNC  GetElapsedTimeInMicroSec
#define MOD_FACTOR 1000000
#define FILL_CNT   6
#else
#define TIME_FUNC  GetElapsedTimeInMilliSec
#define MOD_FACTOR 1000
#define FILL_CNT   3
#endif

#if !defined(__PRETTY_FUNCTION__) && !defined(__GNUC__)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

#ifndef EMBEDDED_XILINX
#ifdef _MSC_VER
using Clock = std::chrono::system_clock;
#else
using Clock = std::chrono::high_resolution_clock;
#endif
#endif

#ifdef EMBEDDED_XILINX
// TODO: Implement proper Timer for embedded Xilinx, currently only a stub
class Timer
{
public:
	Timer()  = default;
	~Timer() = default;

	void Start() {}
	void Stop() {}
	void Restart() {}

	uint64_t GetElapsedTimeInMicroSec() const
	{
		return 0;
	}
	double GetElapsedTime() const
	{
		return 0.0;
	}
	double GetElapsedTimeInSec() const
	{
		return 0.0;
	}
	double GetElapsedTimeInMilliSec() const
	{
		return 0.0;
	}

	friend Timer operator+(const Timer& t1, const Timer& t2)
	{
		return Timer();
	}
	Timer& operator+=(const Timer& t1)
	{
		return *this;
	}

	friend std::ostream& operator<<(std::ostream& stream, const Timer& t)
	{
		return stream;
	}
};

#else
class Timer
{
public:
	Timer() :
		m_stopped(false),
		m_StartTime(Clock::now()),
		m_EndTime(Clock::now())
	{
	}

	~Timer() = default;

	void Start()
	{
		m_stopped   = false;
		m_StartTime = Clock::now();
	}

	void Stop()
	{
		m_stopped = true;
		m_EndTime = Clock::now();
	}

	void Restart()
	{
		Stop();
		Start();
	}

	uint64_t GetElapsedTimeInMicroSec() const
	{
		return getElapsedTime<std::chrono::microseconds>().count();
	}

	double GetElapsedTime() const
	{
		return GetElapsedTimeInSec();
	}

	double GetElapsedTimeInSec() const
	{
		return GetElapsedTimeInMicroSec() * 1.0e-6;
	}

	double GetElapsedTimeInMilliSec() const
	{
		return GetElapsedTimeInMicroSec() * 1.0e-3;
	}

	friend Timer operator+(const Timer& t1, const Timer& t2)
	{
		Timer res;
		res.m_stopped   = true;
		res.m_StartTime = t1.m_StartTime;
		res.m_EndTime   = t1.m_EndTime + t2.getTimeDiff();
		return res;
	}

	Timer& operator+=(const Timer& t1)
	{
		this->m_stopped = true;
		this->m_EndTime += t1.getTimeDiff();
		return *this;
	}

	friend std::ostream& operator<<(std::ostream& stream, const Timer& t)
	{
		Clock::time_point diff = t.getTimeDiffTimePoint();

		std::time_t tTime = Clock::to_time_t(diff);
		std::tm bt;
#if defined(_MSC_VER) || (__MINGW32__)
		errno_t err = gmtime_s(&bt, &tTime);
		if (err) throw std::runtime_error("Invalid Argument to gmtime_s");
		stream << std::put_time(&bt, "%T");
#else
		gmtime_r(&tTime, &bt);
		//		stream << std::put_time(&bt, "%T"); // Does not work with MinGW
		stream << std::put_time(&bt, "%H:%M:%S");
#endif

		stream << "." << std::setfill('0') << std::setw(FILL_CNT) << static_cast<uint64_t>(std::round(t.TIME_FUNC())) % MOD_FACTOR;

		return stream;
	}

private:
	Timer& operator=(const Timer&) = delete; //  disable assignment constructor

	Clock::duration getTimeDiff() const
	{
		if (!m_stopped)
			return (Clock::now() - m_StartTime);
		else
			return (m_EndTime - m_StartTime);
	}

	Clock::time_point getTimeDiffTimePoint() const
	{
		return Clock::time_point(getTimeDiff());
	}

	template<typename T>
	T getElapsedTime() const
	{
		return std::chrono::duration_cast<T>(getTimeDiff());
	}

private:
	bool m_stopped;

	Clock::time_point m_StartTime;
	Clock::time_point m_EndTime;
};
#endif

#define FuncTime() funcTime __ft(__PRETTY_FUNCTION__)

class funcTime
{
public:
	explicit funcTime(const std::string& funcName = "") :
		m_timer(),
		m_funcName(funcName)
	{
		m_timer.Start();
	}

	~funcTime()
	{
		m_timer.Stop();
		if (!m_funcName.empty())
			CLAP_LOG_INFO << m_funcName << " - " << std::flush;
		CLAP_LOG_INFO << "Execution Time: " << m_timer << std::endl;
	}

private:
	Timer m_timer;
	std::string m_funcName;
};
} // namespace clap
