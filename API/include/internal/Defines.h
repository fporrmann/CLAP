/* 
 *  File: Defines.h
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

#ifdef _WIN32
#include <windows.h>
using DeviceHandle = HANDLE;
using FlagType     = DWORD;
using FileOpType   = DWORD;
using OffsetType   = LONG;
using ByteCntType  = DWORD;

#define INVALID_HANDLE              INVALID_HANDLE_VALUE
#define DEFAULT_OPEN_FLAGS          (GENERIC_READ | GENERIC_WRITE)
#define READ_ONLY_FLAG              GENERIC_READ
#define OPEN_DEVICE(NAME, FLAGS)    CreateFile(NAME, FLAGS, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
#define DEVICE_HANDLE_VALID(HANDLE) (HANDLE != INVALID_HANDLE_VALUE)
#define CLOSE_DEVICE(HANDLE)        CloseHandle(HANDLE)
#define SEEK(HANDLE, OFFSET)        SetFilePointer(HANDLE, OFFSET, NULL, FILE_BEGIN)
#define SEEK_INVALID(RC, OFFSET)    (RC == INVALID_SET_FILE_POINTER)
#else
#include <stdint.h>
#include <sys/types.h>

using DeviceHandle = int32_t;
using FlagType     = int32_t;
using FileOpType   = ssize_t;
using OffsetType   = off_t;
using ByteCntType  = uint64_t;

#define INVALID_HANDLE              -1
#define DEFAULT_OPEN_FLAGS          (O_RDWR | O_NONBLOCK)
#define READ_ONLY_FLAG              O_RDONLY
#define OPEN_DEVICE(NAME, FLAGS)    open(NAME, FLAGS)
#define DEVICE_HANDLE_VALID(HANDLE) (HANDLE >= 0)
#define CLOSE_DEVICE(HANDLE)        close(HANDLE)
#define SEEK(HANDLE, OFFSET)        lseek(HANDLE, OFFSET, SEEK_SET)
#define SEEK_INVALID(RC, OFFSET)    (RC != OFFSET)
#endif

#define IS_ALIGNED(POINTER, ALIGNMENT) ((reinterpret_cast<uintptr_t>(reinterpret_cast<const void*>(POINTER)) % (ALIGNMENT)) == 0)
