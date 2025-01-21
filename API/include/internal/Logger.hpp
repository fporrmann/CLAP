/*
 *  File: Logger.hpp
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

#include <iostream>
#include <mutex>
#include <sstream>

#include "StdStub.hpp"
#include "Utils.hpp"

#ifdef CLAP_USE_XIL_PRINTF
#include <xil_printf.h>
#endif

namespace clap
{
namespace logging
{
enum class Verbosity
{
	VB_DEBUG   = 0,
	VB_VERBOSE = 1,
	VB_INFO    = 2,
	VB_WARNING = 3,
	VB_ERROR   = 4,
	VB_NONE    = 255
};

// Partially based on: https://stackoverflow.com/a/57554337

// Required for ostream manipulators (e.g. std::endl or std::flush)
using ManipType = std::ostream&(std::ostream&);

class Logger
{
public:
	explicit Logger(const Verbosity& lvl, const Verbosity& verbosity = Verbosity::VB_INFO, std::ostream& outStream = std::cout) :
		m_lvl(lvl),
		m_verbosity(verbosity),
		m_outStream(outStream)
	{}

	void SetVerbosity(const Verbosity& v)
	{
		m_verbosity = v;
	}

	// Required for pure std::endl / std::flush calls (e.g. Logger << std::endl)
	Logger& operator<<(ManipType manip)
	{
		log(manip);
		return *this;
	}

	template<typename T>
	void log(T& message)
	{
		std::lock_guard<std::mutex> lock(s_logMutex);

		if (m_lvl >= m_verbosity)
		{
#ifdef CLAP_USE_XIL_PRINTF
			xil_printf("%s\r", message.str().c_str());
#else
			m_outStream << message.str();
#endif
		}

		message.flush();
	}

	// Specialization for ostream manipulators (e.g. std::endl or std::flush)
	void log(ManipType manip)
	{
		std::lock_guard<std::mutex> lock(s_logMutex);

		if (m_lvl >= m_verbosity)
			m_outStream << manip;
	}

private:
	Verbosity m_lvl;
	Verbosity m_verbosity;
	std::ostream& m_outStream;
	static inline std::mutex s_logMutex;
};

class LoggerBuffer
{
public:
	LoggerBuffer(const LoggerBuffer&)            = delete;
	LoggerBuffer& operator=(const LoggerBuffer&) = delete;
	LoggerBuffer& operator=(LoggerBuffer&&)      = delete;

	explicit LoggerBuffer(Logger* pLogger) :
		m_stream(),
		m_pLogger(pLogger)
	{}

	LoggerBuffer(LoggerBuffer&& buf) :
		m_stream(std::move(buf.m_stream)),
		m_pLogger(buf.m_pLogger)
	{}

	template<typename T>
	LoggerBuffer& operator<<(T&& message)
	{
		m_stream << std::forward<T>(message);
		return *this;
	}

	LoggerBuffer& operator<<(ManipType manip)
	{
		m_stream << manip;
		return *this;
	}

	~LoggerBuffer()
	{
		if (m_pLogger)
			m_pLogger->log(m_stream);
	}

private:
	std::stringstream m_stream;
	Logger* m_pLogger;
};

template<typename T>
LoggerBuffer operator<<(Logger& logger, T&& message)
{
	LoggerBuffer buf(&logger);
	buf << std::forward<T>(message);
	return buf;
}

class LoggerContainer
{
public:
	LoggerContainer()  = default;
	~LoggerContainer() = default;

	Logger& GetLogger([[maybe_unused]] const Verbosity& v)
	{
#ifdef CLAP_DISABLE_LOGGING
		return m_none;
#else
		switch (v)
		{
			case Verbosity::VB_DEBUG:
				return m_debug;
			case Verbosity::VB_VERBOSE:
				return m_verbose;
			case Verbosity::VB_INFO:
				return m_info;
			case Verbosity::VB_WARNING:
				return m_warning;
			case Verbosity::VB_ERROR:
				return m_error;
			default:
				return m_info;
		}
#endif
	}

	void SetVerbosity([[maybe_unused]] const Verbosity& v)
	{
#ifndef CLAP_DISABLE_LOGGING
		m_debug.SetVerbosity(v);
		m_verbose.SetVerbosity(v);
		m_info.SetVerbosity(v);
		m_warning.SetVerbosity(v);
		m_error.SetVerbosity(v);
#endif
	}

private:
#ifdef CLAP_DISABLE_LOGGING
	Logger m_none = Logger(Verbosity::VB_DEBUG, Verbosity::VB_NONE);
#else
	Logger m_debug = Logger(Verbosity::VB_DEBUG);
	Logger m_verbose = Logger(Verbosity::VB_VERBOSE);
	Logger m_info = Logger(Verbosity::VB_INFO);
	Logger m_warning = Logger(Verbosity::VB_WARNING, Verbosity::VB_INFO, std::cerr);
	Logger m_error = Logger(Verbosity::VB_ERROR, Verbosity::VB_INFO, std::cerr);
#endif
};

inline LoggerContainer& GetLoggers()
{
	static LoggerContainer loggers;
	return loggers;
}

inline Logger& GetLogger(const Verbosity& v)
{
	return GetLoggers().GetLogger(v);
}

template<typename T>
constexpr typename std::underlying_type<T>::type ToUnderlying(const T& t) noexcept
{
	return static_cast<typename std::underlying_type<T>::type>(t);
}

inline Verbosity ToVerbosity(const int32_t& val)
{
	if (val < ToUnderlying(Verbosity::VB_DEBUG) || val > ToUnderlying(Verbosity::VB_ERROR))
		return Verbosity::VB_INFO;

	return static_cast<Verbosity>(val);
}

inline void SetVerbosity([[maybe_unused]] const Verbosity& v)
{
#ifndef CLAP_DISABLE_LOGGING
	GetLoggers().SetVerbosity(v);
#endif
}

} // namespace logging

#ifdef CLAP_DISABLE_LOGGING
#define CLAP_LOG_FLAG if (false)
#else
#define CLAP_LOG_FLAG
#endif


#define CLAP_LOG_DEBUG   CLAP_LOG_FLAG clap::logging::GetLogger(clap::logging::Verbosity::VB_DEBUG)
#define CLAP_LOG_VERBOSE CLAP_LOG_FLAG clap::logging::GetLogger(clap::logging::Verbosity::VB_VERBOSE)
#define CLAP_LOG_INFO    CLAP_LOG_FLAG clap::logging::GetLogger(clap::logging::Verbosity::VB_INFO)
#define CLAP_LOG_WARNING CLAP_LOG_FLAG clap::logging::GetLogger(clap::logging::Verbosity::VB_WARNING)
#define CLAP_LOG_ERROR   CLAP_LOG_FLAG clap::logging::GetLogger(clap::logging::Verbosity::VB_ERROR)

#define CLAP_CLASS_LOG_DEBUG   CLAP_LOG_DEBUG << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_VERBOSE CLAP_LOG_VERBOSE << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_INFO    CLAP_LOG_INFO << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_WARNING CLAP_LOG_WARNING << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_ERROR   CLAP_LOG_ERROR << CLASS_TAG_AUTO

#define CLAP_CLASS_LOG_WITH_NAME_DEBUG(_N_)   CLAP_LOG_DEBUG << CLASS_TAG_AUTO_WITH_NAME(_N_)
#define CLAP_CLASS_LOG_WITH_NAME_VERBOSE(_N_) CLAP_LOG_VERBOSE << CLASS_TAG_AUTO_WITH_NAME(_N_)
#define CLAP_CLASS_LOG_WITH_NAME_INFO(_N_)    CLAP_LOG_INFO << CLASS_TAG_AUTO_WITH_NAME(_N_)
#define CLAP_CLASS_LOG_WITH_NAME_WARNING(_N_) CLAP_LOG_WARNING << CLASS_TAG_AUTO_WITH_NAME(_N_)
#define CLAP_CLASS_LOG_WITH_NAME_ERROR(_N_)   CLAP_LOG_ERROR << CLASS_TAG_AUTO_WITH_NAME(_N_)

} // namespace clap
