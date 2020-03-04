//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2020, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// unix.cpp - pgAgent unix specific functions
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

// *nix only!!
#ifndef WIN32

#include <iostream>
#include <fcntl.h>
#include <fstream>

static boost::mutex s_loggerLock;

using namespace std;

void printVersion();

void usage(const std::wstring &executable)
{
	char *appName = WStringToChar(executable);
	printVersion();

	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "%s [options] <connect-string>\n", appName);
	fprintf(stdout, "options:\n");
	fprintf(stdout, "-v (display version info and then exit)\n");
	fprintf(stdout, "-f run in the foreground (do not detach from the terminal)\n");
	fprintf(stdout, "-t <poll time interval in seconds (default 10)>\n");
	fprintf(stdout, "-r <retry period after connection abort in seconds (>=10, default 30)>\n");
	fprintf(stdout, "-s <log file (messages are logged to STDOUT if not specified>\n");
	fprintf(stdout, "-l <logging verbosity (ERROR=0, WARNING=1, DEBUG=2, default 0)>\n");

	if (appName)
		delete []appName;
}

void LogMessage(const std::wstring &msg, const int &level)
{
	std::wofstream out;
	bool writeToStdOut = false;
	MutexLocker locker(&s_loggerLock);

	if (!logFile.empty())
	{
		std::string log_file(logFile.begin(), logFile.end());
		out.open((const char *)log_file.c_str(), ios::out | ios::app);
		if (!out.is_open())
		{
			fprintf(stderr, "Can not open the logfile!");
			return;
		}
	}
	else
		writeToStdOut = true;

	boost::gregorian::date current_date(boost::gregorian::day_clock::local_day());

	std::wstring day_week = boost::lexical_cast<std::wstring>(current_date.day_of_week());
	std::wstring year = boost::lexical_cast<std::wstring>(current_date.year());
	std::wstring month = boost::lexical_cast<std::wstring>(current_date.month());
	std::wstring day = boost::lexical_cast<std::wstring>(current_date.day());

	boost::posix_time::ptime pt = boost::posix_time::second_clock::local_time();
	std::wstring time_day = boost::lexical_cast<std::wstring>(pt.time_of_day());

	std::wstring logTimeString = L"";
	logTimeString = day_week + L" " + month + L" " + day + L" " + time_day + L" " + year + L" ";

	switch (level)
	{
		case LOG_DEBUG:
			if (minLogLevel >= LOG_DEBUG)
			{
				logTimeString = logTimeString + L"DEBUG: " + msg + L"\n";
				if (writeToStdOut)
					std::wcout << logTimeString;
				else
					out.write(logTimeString.c_str(), logTimeString.size());
			}
			break;
		case LOG_WARNING:
			if (minLogLevel >= LOG_WARNING)
			{
				logTimeString = logTimeString + L"WARNING: " + msg + L"\n";
				if (writeToStdOut)
					std::wcout << logTimeString;
				else
					out.write(logTimeString.c_str(), logTimeString.size());
			}
			break;
		case LOG_ERROR:
			logTimeString = logTimeString + L"ERROR: " + msg + L"\n";
			if (writeToStdOut)
				std::wcout << logTimeString;
			else
				out.write(logTimeString.c_str(), logTimeString.size());
			// On system exit, boost::mutex object calls
			// pthread_mutex_destroy(...) on the underlying native mutex object.
			// But - it returns errorcode 'BUSY' instead of 0. Because - it was
			// still keeping the lock on the resource. And, that results into
			// an assertion in debug mode.
			//
			// Hence - we need to unlock the mutex before calling system exit.
			locker = (boost::mutex *)NULL;
			exit(1);
			break;
		case LOG_STARTUP:
			logTimeString = logTimeString + L"WARNING: " + msg + L"\n";
			if (writeToStdOut)
				std::wcout << logTimeString;
			else
				out.write(logTimeString.c_str(), logTimeString.size());
			break;
	}

	if (!logFile.empty())
	{
		out.close();
	}
}


// Shamelessly lifted from pg_autovacuum...
static void daemonize(void)
{
	pid_t pid;

	pid = fork();
	if (pid == (pid_t)-1)
	{
		LogMessage(L"Cannot disassociate from controlling TTY", LOG_ERROR);
		exit(1);
	}
	else if (pid)
		exit(0);

#ifdef HAVE_SETSID
	if (setsid() < 0)
	{
		LogMessage(L"Cannot disassociate from controlling TTY", LOG_ERROR);
		exit(1);
	}
#endif

}

int main(int argc, char **argv)
{
	std::wstring executable;
	executable.assign(CharToWString(argv[0]));

	if (argc < 2)
	{
		usage(executable);
		return 1;
	}

	argc--;
	argv++;

	setOptions(argc, argv, executable);

	if (!runInForeground)
		daemonize();

	MainLoop();

	return 0;
}

#endif // !WIN32
