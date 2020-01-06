#pragma once

#include <sstream>
#include <cmath>

#define CLASS_TAG(_C_) "[" << _C_ << "::" << __func__ << "] "

#define DISABLE_COPY_ASSIGN_MOVE(_C_) \
_C_(_C_ const &) = delete; /* disable copy constructor */ \
_C_& operator=(_C_ const &) = delete; /* disable assignment constructor */ \
_C_(_C_ &&) = delete;

#define UNUSED(x) (void)(x)

static const int32_t WAIT_INFINITE = -1;

/// -------------------------------------------------------------------- ///
/// -------------------------------------------------------------------- ///
/// -------------------------------------------------------------------- ///
// Template used for reverse iteration in C++11 range-based for loops.
// Source: https://gist.github.com/arvidsson/7231973
template<typename T>
class ReverseRange
{
	T &x;

	public:
		ReverseRange(T &x) : x(x) {}

		auto begin() const -> decltype(this->x.rbegin())
		{
			return x.rbegin();
		}

		auto end() const -> decltype(this->x.rend())
		{
			return x.rend();
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

template <typename T>
static std::string to_string_with_precision(const T a_value, const uint32_t& n = 6)
{
	std::ostringstream out;
	out.precision(n);
	out << std::fixed << a_value;
	return out.str();
}

static uint32_t CalcOrder(double val)
{
	uint32_t cnt = 0;

	while (val / 1000.0 > 1.0)
	{
		val /= 1000.0;
		cnt++;
	}

	return cnt;
}

static std::string GetPrefix(const uint32_t& order)
{
	switch (order)
	{
		// Byte
		case 0:
			return " B";

		// Kilo
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

static std::string SpeedWidthSuffix(double val)
{
	std::string str = "";
	uint32_t order = CalcOrder(val);

	str = to_string_with_precision(val / (std::pow(1000.0, order)), 2);

	str.append(GetPrefix(order));
	str.append("/s");

	return str;
}

static std::string SizeWithSuffix(uint64_t val)
{
	std::string str = "";
	uint32_t order = CalcOrder(val);

	str = to_string_with_precision(val / (std::pow(1000.0, order)), 2);

	str.append(GetPrefix(order));

	return str;
}
