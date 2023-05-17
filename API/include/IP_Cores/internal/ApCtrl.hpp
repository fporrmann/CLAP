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

	void Reset()
	{
		getStatus();
		m_done = false;
	}

	bool Start()
	{
		getStatus();

		if (!ap_idle) return false;

		m_done   = false;
		ap_start = true;
		Update(Direction::WRITE);
		return true;
	}

	void SetAutoRestart(const bool& enable = true)
	{
		auto_restart = enable;
		Update(Direction::WRITE);
	}

	bool IsDone()
	{
		if (m_done) return m_done;
		getStatus();
		return m_done;
	}

	bool IsIdle()
	{
		getStatus();
		return ap_idle;
	}

	bool PollDone()
	{
		return IsDone();
	}

private:
	void getStatus()
	{
		Update();

		if (!m_done && ap_done)
			m_done = true;
	}

private:
	bool m_done = false;

	bool ap_start     = false;
	bool ap_done      = false;
	bool ap_idle      = false;
	bool ap_ready     = false;
	bool auto_restart = false;
};
} // namespace clap