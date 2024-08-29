/*
 *  File: Utils.hpp
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

#include <cmath>
#include <cxxabi.h>
#include <exception>
#include <sstream>

#ifndef CLASS_TAG
#define CLASS_TAG(_C_) "[" << _C_ << "::" << __func__ << "] "
#endif

#ifndef CLASS_TAG_AUTO
#define CLASS_TAG_AUTO "[" << clap::utils::ClassName(*this) << "::" << __func__ << "] "
#endif

#ifndef FUNCTION_TAG
#define FUNCTION_TAG "[" << __func__ << "] "
#endif

#ifndef DISABLE_COPY_ASSIGN_MOVE
#define DISABLE_COPY_ASSIGN_MOVE(_C_)                                          \
	_C_(_C_ const &)            = delete; /* disable copy constructor */       \
	_C_ &operator=(_C_ const &) = delete; /* disable assignment constructor */ \
	_C_(_C_ &&)                 = delete;
#endif

#ifndef DEFINE_EXCEPTION
#define DEFINE_EXCEPTION(__NAME__)                                   \
	class __NAME__ : public std::exception                           \
	{                                                                \
	public:                                                          \
		explicit __NAME__(const std::string &what) : m_what(what) {} \
                                                                     \
		virtual ~__NAME__() throw() {}                               \
                                                                     \
		virtual const char *what() const throw()                     \
		{                                                            \
			return m_what.c_str();                                   \
		}                                                            \
                                                                     \
	private:                                                         \
		std::string m_what;                                          \
	};
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define CHECK_RESULT __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && (_MSC_VER >= 1700)
#define CHECK_RESULT _Check_return_
#else
#define CHECK_RESULT
#endif

namespace clap
{
namespace utils
{
/// -------------------------------------------------------------------- ///
/// -------------------------------------------------------------------- ///
/// -------------------------------------------------------------------- ///
// Template used for reverse iteration in C++11 range-based for loops.
// Source: https://gist.github.com/arvidsson/7231973
template<typename T>
class ReverseRange
{
	T &m_x;

public:
	explicit ReverseRange(T &x) :
		m_x(x) {}

	auto begin() const -> decltype(this->m_x.rbegin())
	{
		return m_x.rbegin();
	}

	auto end() const -> decltype(this->m_x.rend())
	{
		return m_x.rend();
	}
};

template<typename T>
static ReverseRange<T> ReverseIterate(T &x)
{
	return ReverseRange<T>(x);
}
/// -------------------------------------------------------------------- ///
/// -------------------------------------------------------------------- ///
/// -------------------------------------------------------------------- ///

template<typename T>
static inline std::string ToStringWithPrecision(const T val, const uint32_t &n = 6)
{
	std::ostringstream out;
	out.precision(n);
	out << std::fixed << val;
	return out.str();
}

static inline uint32_t CalcOrder(double val)
{
	uint32_t cnt = 0;

	while (val / 1000.0 > 1.0)
	{
		val /= 1000.0;
		cnt++;
	}

	return cnt;
}

static inline std::string GetPrefix(const uint32_t &order)
{
	switch (order)
	{
		// Byte
		case 0:
			return " B";

		// Kilo Byte
		case 1:
			return " KB";

		// Mega Byte
		case 2:
			return " MB";

		// Giga Byte
		case 3:
			return " GB";

		// Tera Byte
		case 4:
			return " TB";
	}

	return "UNKNOWN ORDER: " + std::to_string(order);
}

static inline std::string SpeedWidthSuffix(double val)
{
	std::string str = "";
	uint32_t order  = CalcOrder(val);

	str = ToStringWithPrecision(val / (std::pow(1000.0, order)), 2);

	str.append(GetPrefix(order));
	str.append("/s");

	return str;
}

static inline std::string SizeWithSuffix(double val)
{
	std::string str = "";
	uint32_t order  = CalcOrder(val);

	str = ToStringWithPrecision(val / (std::pow(1000.0, order)), 2);

	str.append(GetPrefix(order));

	return str;
}

static inline std::string SizeWithSuffix(const uint64_t &val)
{
	return SizeWithSuffix(static_cast<double>(val));
}

static inline std::string SizeWithSuffix(const int64_t &val)
{
	return SizeWithSuffix(static_cast<double>(val));
}

static inline std::string Hex2Str(const uint64_t &val)
{
	std::stringstream ss;
	ss << std::hex << val;
	return ss.str();
}

static inline std::vector<std::string> SplitString(const std::string &s, const char &delimiter = ' ')
{
	std::vector<std::string> split;
	std::string item;
	std::istringstream stream(s);

	while (std::getline(stream, item, delimiter))
		split.push_back(item);

	return split;
}

template<typename T>
static inline std::string ClassName(T &ref)
{
	int status;
	char *pName = abi::__cxa_demangle(typeid(ref).name(), NULL, NULL, &status);
	std::string name(pName);
	free(pName);

	// Remove potential namespace prefix
	name = SplitString(name, ':').back();

	return name;
}

} // namespace utils
} // namespace clap