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
			m_outStream << message.str();

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

#ifdef DISABLE_LOGGING
static Logger g_none(Verbosity::VB_DEBUG, Verbosity::VB_NONE);
#else
static Logger g_debug(Verbosity::VB_DEBUG);
static Logger g_verbose(Verbosity::VB_VERBOSE);
static Logger g_info(Verbosity::VB_INFO);
static Logger g_warning(Verbosity::VB_WARNING, Verbosity::VB_INFO, std::cerr);
static Logger g_error(Verbosity::VB_ERROR, Verbosity::VB_INFO, std::cerr);
#endif

template<typename T>
constexpr typename std::underlying_type<T>::type ToUnderlying(const T& t) noexcept
{
	return static_cast<typename std::underlying_type<T>::type>(t);
}

static inline Verbosity ToVerbosity(const int32_t& val)
{
	if (val < ToUnderlying(Verbosity::VB_DEBUG) || val > ToUnderlying(Verbosity::VB_ERROR))
		return Verbosity::VB_INFO;

	return static_cast<Verbosity>(val);
}

static inline void SetVerbosity([[maybe_unused]] const Verbosity& v)
{
#ifndef DISABLE_LOGGING
	g_debug.SetVerbosity(v);
	g_verbose.SetVerbosity(v);
	g_info.SetVerbosity(v);
	g_warning.SetVerbosity(v);
	g_error.SetVerbosity(v);
#endif
}

} // namespace logging

#ifndef DISABLE_LOGGING
#define CLAP_LOG_DEBUG   logging::g_debug
#define CLAP_LOG_VERBOSE logging::g_verbose
#define CLAP_LOG_INFO    logging::g_info
#define CLAP_LOG_WARNING logging::g_warning
#define CLAP_LOG_ERROR   logging::g_error
#else
#define CLAP_LOG_DEBUG   logging::g_none
#define CLAP_LOG_VERBOSE logging::g_none
#define CLAP_LOG_INFO    logging::g_none
#define CLAP_LOG_WARNING logging::g_none
#define CLAP_LOG_ERROR   logging::g_none
#endif

#define CLAP_CLASS_LOG_DEBUG   CLAP_LOG_DEBUG << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_VERBOSE CLAP_LOG_VERBOSE << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_INFO    CLAP_LOG_INFO << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_WARNING CLAP_LOG_WARNING << CLASS_TAG_AUTO
#define CLAP_CLASS_LOG_ERROR   CLAP_LOG_ERROR << CLASS_TAG_AUTO

/// Info log messages that will not be disabled by the DISABLE_LOGGING macro
#define CLAP_LOG_INFO_ALWAYS logging::g_info

} // namespace clap
