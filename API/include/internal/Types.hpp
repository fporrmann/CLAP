/* 
 *  File: Types.hpp
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

#include <memory>
#include <vector>

#include "Constants.hpp"

namespace clap
{
#ifdef EMBEDDED_XILINX
template<class T>
using CLAPBufferAllocator = std::allocator<T>;
#else
#include "AlignmentAllocator.hpp"
template<class T>
using CLAPBufferAllocator = clap::internal::AlignmentAllocator<T, ALIGNMENT>;
#endif

template<class T>
using CLAPBuffer = std::vector<T, CLAPBufferAllocator<T>>;

using CLAPPtr = std::shared_ptr<class CLAP>;
using Bit32Arr = std::array<bool, 32>;

namespace internal
{
using CLAPBasePtr      = std::shared_ptr<class CLAPBase>;
using CLAPBackendPtr   = std::shared_ptr<class CLAPBackend>;
using CLAPManagedPtr   = std::shared_ptr<class CLAPManaged>;
using MemoryManagerPtr = std::shared_ptr<class MemoryManager>;
using UserInterruptPtr = std::unique_ptr<class UserInterruptBase> ;

using MemoryManagerVec = std::vector<MemoryManagerPtr>;
} // namespace internal
} // namespace clap