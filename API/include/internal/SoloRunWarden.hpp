/*
 *  File: SoloRunWarden.hpp
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

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Logger.hpp"

namespace clap
{
namespace internal
{
class SoloRunWarden
{
	static inline const std::string LOCK_FILE = "/tmp/clap.lock";

public:
	static SoloRunWarden& GetInstance()
	{
		static SoloRunWarden instance;
		return instance;
	}

	static bool Cleanup()
	{
		return unlink(LOCK_FILE.c_str()) == 0;
	}

	SoloRunWarden(SoloRunWarden const&)  = delete;
	void operator=(SoloRunWarden const&) = delete;

private:
	SoloRunWarden()
	{
		int lockFile = open(LOCK_FILE.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
		if (lockFile == INVALID_HANDLE)
		{
			// file already exists
			std::ifstream lockFileIn(LOCK_FILE);
			int pid;
			lockFileIn >> pid;
			lockFileIn.close();

			if (kill(pid, 0) == 0)
			{
				// Process is still running
				CLAP_CLASS_LOG_ERROR << "Error: Another instance of this program is already running" << std::endl;
				exit(1);
			}
			else
			{
				CLAP_CLASS_LOG_WARNING << "Warning: Lock file exists but process is not running - deleting lock file and continuing" << std::endl;

				// Process is not running
				if (!Cleanup())
				{
					CLAP_CLASS_LOG_ERROR << "Error: Unable to delete lock file (" << LOCK_FILE << ") - Please delete it manually and restart the application" << std::endl;
					exit(1);
				}
			}
		}

		close(lockFile);

		std::ofstream lockFileOut(LOCK_FILE);

		// Make sure the file was created successfully
		if (!lockFileOut.is_open())
		{
			CLAP_CLASS_LOG_ERROR << "Error: Unable to create lock file" << std::endl;
			exit(1);
		}

		lockFileOut << getpid();
		lockFileOut.close();

#ifndef CLAP_DISABLE_SRW_SIG_HANDLER
		// Catch SIGINT and SIGTERM to delete the lock file before exiting
		signal(SIGINT, signalHandler);
		signal(SIGTERM, signalHandler);
#endif
	}

	~SoloRunWarden()
	{
		Cleanup();
	}

private:
	static void signalHandler([[maybe_unused]] int signal)
	{
		Cleanup();
		exit(0);
	}
};
} // namespace internal
} // namespace clap
