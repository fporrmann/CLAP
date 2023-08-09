/*
 *  File: ApCtrl.hpp
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

#include "../../internal/Logger.hpp"
#include "../../internal/RegisterInterface.hpp"

namespace clap
{
namespace internal
{
class ApCtrl : public Register<uint8_t>, public HasStatus
{
	DISABLE_COPY_ASSIGN_MOVE(ApCtrl)

public:
	ApCtrl() :
		Register("ap_ctrl")
	{
		RegisterElement<bool>(&ap_start, "ap_start", 0);
		RegisterElement<bool>(&ap_done, "ap_done", 1);
		RegisterElement<bool>(&ap_idle, "ap_idle", 2);
		RegisterElement<bool>(&ap_ready, "ap_ready", 3);
		RegisterElement<bool>(&auto_restart, "auto_restart", 7);
	}

	void PrintStatus()
	{
		getStatus();
		LOG_INFO << "---- ap_ctrl: ----" << std::endl
				 << "ap_start    : " << ap_start << std::endl
				 << "ap_done     : " << ap_done << " (" << m_done << ")" << std::endl
				 << "ap_idle     : " << ap_idle << std::endl
				 << "ap_ready    : " << ap_ready << std::endl
				 << "auto_restart: " << auto_restart << std::endl
				 << "------------------" << std::endl;
	}

	bool Start()
	{
		getStatus();

		if (!ap_idle) return false;

		m_done   = false;
		ap_start = true;
		Update(Direction::WRITE);

		m_running = true;

		return true;
	}

	void SetAutoRestart(const bool& enable = true)
	{
		auto_restart = enable;
		Update(Direction::WRITE);
	}

	bool IsIdle()
	{
		getStatus();
		return ap_idle;
	}

private:
	void getStatus()
	{
		Update();

		if (!m_done && ap_done)
		{
			m_done    = true;
			m_running = false;
		}

		// Fallback check for ap_done there might be edge cases where the clear on read state of ap_done
		// is not properly read by the register interface (e.g., when the ap_done flag is set and cleared in the same cycle).
		// In this case we check if the ap_idle flag is set and set the ap_done flag accordingly
		if (!m_done && ap_idle && m_running)
		{
			m_done    = true;
			m_running = false;
		}
	}

private:
	bool ap_start     = false;
	bool ap_done      = false;
	bool ap_idle      = false;
	bool ap_ready     = false;
	bool auto_restart = false;

	bool m_running = false;
};
} // namespace internal
} // namespace clap