#pragma once
#include <chrono>
#include <iomanip>
#include <string>
#include <cmath>
#include <ostream>

#ifdef PRINT_MU_SEC
#define TIME_FUNC  GetElapsedTimeInMicroSec
#define MOD_FACTOR 1000000
#define FILL_CNT   6
#else
#define TIME_FUNC  GetElapsedTimeInMilliSec
#define MOD_FACTOR 1000
#define FILL_CNT   3
#endif

class Timer
{
	public:
		Timer() :
			m_stopped(false),
			m_StartTime(std::chrono::high_resolution_clock::now()),
			m_EndTime(std::chrono::high_resolution_clock::now())
		{
		}

		~Timer() = default;

		void Start()
		{
			m_stopped = false;
			m_StartTime = std::chrono::high_resolution_clock::now();
		}
		
		void Stop()
		{
			m_stopped = true;
			m_EndTime = std::chrono::high_resolution_clock::now();
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

		friend std::ostream& operator<<(std::ostream& stream, const Timer &t)
		{
			std::chrono::high_resolution_clock::time_point diff = t.getTimeDiffTimePoint();
	
			std::time_t tTime = std::chrono::high_resolution_clock::to_time_t(diff);
			std::tm *bt = std::gmtime(&tTime);

			stream << std::put_time(bt, "%T");
			stream << "." << std::setfill('0') << std::setw(FILL_CNT) << static_cast<uint64_t>(std::round(t.TIME_FUNC())) % MOD_FACTOR;

			return stream;
		}

	private:
		Timer(const Timer &) = delete; // disable copy constructor
		Timer &operator=(const Timer &) = delete; //  disable assignment constructor

		std::chrono::high_resolution_clock::duration getTimeDiff() const
		{
			if(!m_stopped)
				return (std::chrono::high_resolution_clock::now() - m_StartTime);
			else
				return (m_EndTime - m_StartTime);
		}

		std::chrono::high_resolution_clock::time_point getTimeDiffTimePoint() const
		{
			return std::chrono::high_resolution_clock::time_point(getTimeDiff());
		}

		template<typename T>
		T getElapsedTime() const
		{
			return std::chrono::duration_cast<T>(getTimeDiff());
		}
		
	private:
		bool m_stopped;

		std::chrono::high_resolution_clock::time_point m_StartTime;
		std::chrono::high_resolution_clock::time_point m_EndTime;
};
