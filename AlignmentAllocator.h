/* 
 *  File: AlignmentAllocator.h
 *  Copyright (c) 2021 Florian Porrmann
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

#ifdef __arm__
/// Copied from https://github.com/gcc-mirror/gcc/blob/master/gcc/config/rs6000/mm_malloc.h
#include <stdlib.h>

/* We can't depend on <stdlib.h> since the prototype of posix_memalign
   may not be visible.  */
#ifndef __cplusplus
extern int posix_memalign (void **, size_t, size_t);
#else
extern "C" int posix_memalign (void **, size_t, size_t) throw ();
#endif

static __inline void *
_mm_malloc (size_t size, size_t alignment)
{
  void *ptr;
  if (alignment == 1)
    return malloc (size);
  if (alignment == 2 || (sizeof (void *) == 8 && alignment == 4))
    alignment = sizeof (void *);
  if (posix_memalign (&ptr, alignment, size) == 0)
    return ptr;
  else
    return NULL;
}

static __inline void
_mm_free (void * ptr)
{
  free (ptr);
}
#else
#include <mm_malloc.h>
#endif

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
