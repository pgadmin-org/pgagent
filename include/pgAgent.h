//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2012, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgAgent.h - main include
//
//////////////////////////////////////////////////////////////////////////


#ifndef PGAGENT_H
#define PGAGENT_H

// Disable all the GUI classes that might get pulled in through the headers
#define wxUSE_GUI 0

#include <wx/wx.h>

#include "misc.h"
#include "connection.h"
#include "job.h"

extern long longWait;
extern long shortWait;
extern long minLogLevel;
extern wxString connectString;
extern wxString serviceDBname;
extern wxString backendPid;

#ifndef __WXMSW__
extern bool runInForeground;
extern wxString logFile;
extern wxString pidFile;
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
void LogMessage(wxString msg, int level);
void MainLoop();

#ifdef __WIN32__
#include <windows.h>
void CheckForInterrupt();
HANDLE win32_popen_r(const TCHAR *command, HANDLE &handle);
#endif

#endif // PGAGENT_H

