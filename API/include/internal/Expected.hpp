/*
 *  File: Expected.hpp
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

#include <type_traits>
#include <utility>

namespace clap
{
class Unexpected final
{
public:
	explicit Unexpected() {}

	const bool m_success = false;
};

inline Unexpected MakeUnexpected()
{
	return Unexpected();
}

template<typename T>
class Expected;

template<typename T>
class Expected final
{
public:
	template<class U>
	friend class Expected;

	explicit Expected(const Expected<T>& other) :
		m_success(other.m_success)
	{
		if (other.HasValue())
			construct(&m_value, other.m_value);
	}

	template<typename U>
	Expected(const Expected<U>& other) :
		m_success(other.m_success)
	{
		if (other.HasValue())
			construct(&m_value, other.m_value);
	}

	Expected(Expected<T>&& other) :
		m_success(other.m_success)
	{
		if (other.HasValue())
			construct(&m_value, std::move(other.m_value));
	}

	Expected(T&& value) :
		m_value(std::move(value)),
		m_success(true)
	{}

	Expected(const bool& success) = delete;

	Expected(const Unexpected& unexpected) :
		m_success(unexpected.m_success)
	{
	}

	Expected<T>& operator=(const Expected<T>& other)     = delete;
	Expected<T>& operator=(Expected<T>&& other) noexcept = delete;
	Expected<T>& operator=(const T& other)               = delete;
	Expected<T>& operator=(T&& other) noexcept           = delete;
	Expected<T>& operator=(const bool& success)          = delete;

	~Expected()
	{
		if (HasValue())
		{
			m_success = false;
			m_value.~T();
		}
	}

	void MakeUnexpected()
	{
		if (HasValue())
			m_value.~T();

		m_success = false;
	}

	bool HasValue() const
	{
		return m_success;
	}

	T& Value() &
	{
		return m_value;
	}

	const T& Value() const&
	{
		return m_value;
	}

	T Release()
	{
		T tmp = std::move(m_value);
		MakeUnexpected();
		return tmp;
	}

	T* operator->()
	{
		return &(Value());
	}

	const T* operator->() const
	{
		return &(Value());
	}

	T& operator*() &
	{
		return Value();
	}

	const T& operator*() const&
	{
		return Value();
	}

	explicit operator bool() const
	{
		return HasValue();
	}

private:
	template<typename... Args>
	static void construct(T* value, Args&&... args)
	{
		new (reinterpret_cast<void*>(value)) T(std::forward<Args>(args)...);
	}

	union
	{
		T m_value = T();
	};
	bool m_success;
};
} // namespace clap
