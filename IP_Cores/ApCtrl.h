#pragma once

#include "RegisterInterface.h"

class ApCtrl : public Register<uint8_t>, public HasStatus
{
	DISABLE_COPY_ASSIGN_MOVE(ApCtrl)

	public:
		ApCtrl() :
			Register("ap_ctrl"),
			m_done(false),
			ap_start(false),
			ap_done(false),
			ap_idle(false),
			ap_ready(false),
			auto_restart(false)
		{
			RegisterElement<bool>(&ap_start, "ap_start",         0);
			RegisterElement<bool>(&ap_done, "ap_done",           1);
			RegisterElement<bool>(&ap_idle, "ap_idle",           2);
			RegisterElement<bool>(&ap_ready, "ap_ready",         3);
			RegisterElement<bool>(&auto_restart, "auto_restart", 7);
		}

		void PrintStatus()
		{
			getStatus();
			std::cout << "---- ap_ctrl: ----" << std::endl
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

			if(!ap_idle) return false;

			m_done = false;
			ap_start = true;
			Update(WRITE);
			return true;

		}

		void SetAutoRestart(const bool& enable = true)
		{
			auto_restart = enable;
			Update(WRITE);
		}

		bool IsDone()
		{
			if(m_done) return m_done;
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

			if(!m_done && ap_done)
				m_done = true;
		}

	private:
		bool m_done;

		bool ap_start;
		bool ap_done;
		bool ap_idle;
		bool ap_ready;
		bool auto_restart;
};
