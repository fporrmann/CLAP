/*
 *  File: UserInterruptBase.hpp
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

/////////////////////////
// Includes for open()
#ifdef _WIN32
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
/////////////////////////

#ifndef EMBEDDED_XILINX
/////////////////////////
// Include for poll()
#ifndef _WIN32
#include <poll.h>
#endif
/////////////////////////
#endif

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Constants.hpp"
#include "Defines.hpp"
#include "Exceptions.hpp"
#include "Logger.hpp"
#include "RegisterInterface.hpp"
#include "Utils.hpp"

namespace clap
{
namespace internal
{
class UserInterruptBase
{
	DISABLE_COPY_ASSIGN_MOVE(UserInterruptBase)

public:
	UserInterruptBase() {}

	// Don't call derived methods in the destructor
	virtual ~UserInterruptBase() = default;

	virtual void Init(const uint32_t& devNum, const uint32_t& interruptNum, HasInterrupt* pReg = nullptr) = 0;

	virtual void Unset() {}

	virtual bool IsSet() const = 0;

	virtual bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true) = 0;

	void RegisterCallback([[maybe_unused]] const IntrCallback& callback)
	{
		m_callbacks.push_back(callback);
	}

	void TransferCallbacks(UserInterruptBase* pInterrupt)
	{
		for (const auto& callback : m_callbacks)
			pInterrupt->RegisterCallback(callback);

		m_callbacks.clear();
	}

	void SetInstantForward(const bool& instantForward)
	{
		m_instantForward = instantForward;
	}

	const bool& GetInstantForward() const
	{
		return m_instantForward;
	}

	bool HasDoneIntr() const
	{
		return m_pReg->HasDoneIntr();
	}

	bool HasErrorIntr() const
	{
		return m_pReg->HasErrorIntr();
	}

protected:
	std::string m_devName                 = "";
	HasInterrupt* m_pReg                  = nullptr;
	std::vector<IntrCallback> m_callbacks = {};
	uint32_t m_interruptNum               = 0;
	bool m_instantForward                 = false;
};
} // namespace internal
} // namespace clap