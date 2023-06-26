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
