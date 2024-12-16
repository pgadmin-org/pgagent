//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgAgent.h - main include
//
//////////////////////////////////////////////////////////////////////////


#ifndef PGAGENT_H
#define PGAGENT_H

#if BOOST_OS_WINDOWS
#include <windows.h>
#endif

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "misc.h"
#include "connection.h"
#include "job.h"

extern long        longWait;
extern long        shortWait;
extern long        minLogLevel;
extern std::string connectString;
extern std::string backendPid;

#if !BOOST_OS_WINDOWS
extern bool        runInForeground;
extern std::string logFile;
#endif

// Log levels
enum
{
	LOG_ERROR = 0,
	LOG_WARNING,
	LOG_DEBUG,
	// NOTE:
	//     "STARTUP" will be used to log messages for any LogLevel
	//     Use it for logging database connection errors which we
	//     don't want to abort the whole shebang.
	LOG_STARTUP = 15
};

// Prototypes
void LogMessage(const std::string &msg, const int &level);
void MainLoop();

#if BOOST_OS_WINDOWS
void CheckForInterrupt();
HANDLE win32_popen_r(const WCHAR *command, HANDLE &handle);
#endif

#endif // PGAGENT_H

