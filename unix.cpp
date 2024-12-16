//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
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
bool printFileErrorMsg = true;

using namespace std;

void printVersion();

void usage(const std::string &appName)
{
	printVersion();

	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "%s [options] <connect-string>\n", appName.c_str());
	fprintf(stdout, "options:\n");
	fprintf(stdout, "-v (display version info and then exit)\n");
	fprintf(stdout, "-f run in the foreground (do not detach from the terminal)\n");
	fprintf(stdout, "-t <poll time interval in seconds (default 10)>\n");
	fprintf(stdout, "-r <retry period after connection abort in seconds (>=10, default 30)>\n");
	fprintf(stdout, "-s <log file (messages are logged to STDOUT if not specified>\n");
	fprintf(stdout, "-l <logging verbosity (ERROR=0, WARNING=1, DEBUG=2, default 0)>\n");
}

void LogMessage(const std::string &msg, const int &level)
{
	std::ofstream out;
	bool writeToStdOut = false;
	MutexLocker locker(&s_loggerLock);

	if (!logFile.empty())
	{
		std::string log_file(logFile.begin(), logFile.end());
		out.open((const char *)log_file.c_str(), ios::out | ios::app);
		if (!out.is_open())
		{
			if (printFileErrorMsg)
			{
				fprintf(stderr, "Can not open the logfile '%s'", log_file.c_str());
				printFileErrorMsg = false;
			}
			return;
		} else {
			printFileErrorMsg = true;
		}
	}
	else
		writeToStdOut = true;

	boost::gregorian::date current_date(boost::gregorian::day_clock::local_day());

	std::string day_week = boost::lexical_cast<std::string>(current_date.day_of_week());
	std::string year = boost::lexical_cast<std::string>(current_date.year());
	std::string month = boost::lexical_cast<std::string>(current_date.month());
	std::string day = boost::lexical_cast<std::string>(current_date.day());

	boost::posix_time::ptime pt = boost::posix_time::second_clock::local_time();
	std::string time_day = boost::lexical_cast<std::string>(pt.time_of_day());

	std::string logTimeString = "";
	logTimeString = day_week + " " + month + " " + day + " " + time_day + " " + year + " ";

	switch (level)
	{
		case LOG_DEBUG:
			if (minLogLevel >= LOG_DEBUG)
				(writeToStdOut ? std::cout : out) << logTimeString <<
					"DEBUG: " << msg << std::endl;
			break;
		case LOG_WARNING:
			if (minLogLevel >= LOG_WARNING)
				(writeToStdOut ? std::cout : out) << logTimeString <<
					"WARNING: " << msg << std::endl;
			break;
		case LOG_ERROR:
			(writeToStdOut ? std::cout : out) << logTimeString <<
				"ERROR: " << msg << std::endl;

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
			(writeToStdOut ? std::cout : out) << logTimeString <<
				"WARNING: " << msg << std::endl;
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
		LogMessage("Cannot disassociate from controlling TTY", LOG_ERROR);
		exit(1);
	}
	else if (pid)
		exit(0);

#ifdef HAVE_SETSID
	if (setsid() < 0)
	{
		LogMessage("Cannot disassociate from controlling TTY", LOG_ERROR);
		exit(1);
	}
#endif

}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");

	std::string executable;
	executable.assign(argv[0]);

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
