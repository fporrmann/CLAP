/*
 *  File: CLAPBackend.hpp
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

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "Defines.hpp"
#include "Exceptions.hpp"
#include "Expected.hpp"
#include "Timer.hpp"
#include "Types.hpp"
#include "UserInterruptBase.hpp"

namespace clap
{
namespace internal
{
class CLAPBackend
{
public:
	enum class TYPE
	{
		READ,
		WRITE,
		CONTROL
	};

public:
	CLAPBackend() {}

	virtual ~CLAPBackend() {}

	virtual void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte)        = 0;
	virtual void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte) = 0;
	virtual void ReadCtrl(const uint64_t& addr, uint64_t& data, const std::size_t& byteCnt) = 0;

	virtual Expected<uint64_t> ReadUIOProperty([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] const std::string& propName) const
	{
		return MakeUnexpected();
	}

	virtual Expected<std::string> ReadUIOStringProperty([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] const std::string& propName) const
	{
		return MakeUnexpected();
	}

	virtual Expected<std::vector<uint64_t>> ReadUIOPropertyVec([[maybe_unused]] const uint64_t& addr, [[maybe_unused]] const std::string& propName) const
	{
		return MakeUnexpected();
	}

	virtual Expected<int32_t> GetUIOID([[maybe_unused]] const uint64_t& addr) const
	{
		return MakeUnexpected();
	}

	virtual UserInterruptPtr MakeUserInterrupt() const = 0;

	virtual uint32_t GetDevNum() const
	{
		return 0;
	}

	const std::string& GetName(const TYPE& type) const
	{
		if (type == TYPE::READ)
			return m_nameRead;
		else if (type == TYPE::WRITE)
			return m_nameWrite;
		else
			return m_nameCtrl;
	}

	const std::string& GetBackendName() const
	{
		return m_backendName;
	}

	void AddPollAddr(const uint64_t& addr)
	{
		m_pollAddrs.push_back(addr);
	}

	void RemovePollAddr(const uint64_t& addr)
	{
		for (auto it = m_pollAddrs.begin(); it != m_pollAddrs.end(); it++)
		{
			if (*it == addr)
			{
				m_pollAddrs.erase(it);
				break;
			}
		}
	}

protected:
	void logTransferTime(const uint64_t& addr, const uint64_t& sizeInByte, const Timer& timer, const bool& reading)
	{
		// Get the time in seconds, if the time is 0.0, set it to 1ns to avoid division by 0
		const double tSec = (timer.GetElapsedTime() == 0.0 ? 1.0e-9 : timer.GetElapsedTime());

		// Only log if the address is not in the poll list
		if (std::find(m_pollAddrs.begin(), m_pollAddrs.end(), addr) == m_pollAddrs.end())
		{
			if (reading)
			{
				LOG_VERBOSE << "Reading " << sizeInByte << " byte (" << utils::SizeWithSuffix(sizeInByte) << ") from the device took " << timer.GetElapsedTimeInMilliSec()
							<< " ms (" << utils::SpeedWidthSuffix(sizeInByte / tSec) << ")" << std::endl;
			}
			else
			{
				LOG_VERBOSE << "Writing " << sizeInByte << " byte (" << utils::SizeWithSuffix(sizeInByte) << ") to the device took " << timer.GetElapsedTimeInMilliSec()
							<< " ms (" << utils::SpeedWidthSuffix(sizeInByte / tSec) << ")" << std::endl;
			}
		}
	}

protected:
	bool m_valid              = false;
	std::string m_nameRead    = "";
	std::string m_nameWrite   = "";
	std::string m_nameCtrl    = "";
	std::string m_backendName = "CLAP";

	std::vector<uint64_t> m_pollAddrs = {};
};
} // namespace internal
} // namespace clap