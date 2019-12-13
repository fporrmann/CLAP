#pragma once
#include <mm_malloc.h>

// Alignment allocator for std::vector
// based on boost::alignment::aligned_allocator

static inline void* alignedMalloc(size_t alignment, size_t size) noexcept
{
	return _mm_malloc(size, alignment);
}

static inline void alignedFree(void* ptr) noexcept
{
	_mm_free(ptr);
}

template <typename T, std::size_t Alignment = sizeof(void*)>
class AlignmentAllocator
{
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	using void_pointer = void*;
	using const_void_pointer = const void*;

	using pointer = T * ;
	using const_pointer = const T*;

	using reference = T & ;
	using const_reference = const T&;


	template <typename U>
	struct rebind
	{
		using other = AlignmentAllocator<U, Alignment>;
	};

	AlignmentAllocator() = default;

	template <typename U>
	explicit AlignmentAllocator(const AlignmentAllocator<U, Alignment> &) noexcept
	{
	}

	pointer address(reference value) noexcept
	{
		return std::addressof(value);
	}

	const_pointer address(const_reference value) const noexcept
	{
		return std::addressof(value);
	}

	pointer allocate(size_type size, const_void_pointer = 0)
	{
		if(size == 0)
			return nullptr;

		void* p = alignedMalloc(Alignment, size * sizeof(T));

		if(!p)
			throw std::bad_alloc();

		return static_cast<T*>(p);
	}

	void deallocate(pointer ptr, size_type)
	{
		alignedFree(ptr);
	}

	size_type  max_size() const noexcept
	{
		return (~static_cast<std::size_t>(0) / sizeof(T));
	}


	template <class U, class ...Args>
	void construct(U* ptr, Args&&... args)
	{
		::new(static_cast<void*>(ptr)) U(std::forward<Args>(args)...);
	}

	template<class U>
	void construct(U* ptr)
	{
		::new(static_cast<void*>(ptr)) U();
	}

	template<class U>
	void destroy(U* ptr)
	{
		ptr->~U();
	}
};

template <std::size_t Alignment>
class AlignmentAllocator<void, Alignment>
{
public:
	using pointer = void*;
	using const_pointer = const void*;
	using value_type = void;

	template <class U>
	struct rebind
	{
		using other = AlignmentAllocator<U, Alignment>;
	};
};

template<class T, class U, std::size_t Alignment>
inline bool operator==(const AlignmentAllocator<T, Alignment>&, const AlignmentAllocator<U, Alignment>&) noexcept
{
	return true;
}

template<class T, class U, std::size_t Alignment>
inline bool operator!=(const AlignmentAllocator<T, Alignment>&, const AlignmentAllocator<U, Alignment>&) noexcept
{
	return false;
}