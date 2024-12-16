//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// win32.cpp - pgAgent win32 specific functions
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

// This is for Win32 only!!
#ifdef WIN32

#include <process.h>

using namespace std;

// for debugging purposes, we can start the service paused

#define START_SUSPENDED 0


static SERVICE_STATUS serviceStatus;
static SERVICE_STATUS_HANDLE serviceStatusHandle;
static std::wstring serviceName;
static std::wstring user = L".\\Administrator", password;
static HANDLE threadHandle = 0;


static bool serviceIsRunning;
static bool pgagentInitialized;
static HANDLE serviceSync;
static HANDLE eventHandle;

bool stopService();
void printVersion();

// This will be called from MainLoop, if pgagent is initialized properly
void Initialized()
{
	pgagentInitialized = true;
}

// This will be called periodically to check if the service is to be paused.
void CheckForInterrupt()
{
	serviceIsRunning = false;
	long prevCount;
	ReleaseSemaphore(serviceSync, 1, &prevCount);

	// if prevCount is zero, the service should be paused.
	// We're waiting for the semaphore to get signaled again.
	if (!prevCount)
		WaitForSingleObject(serviceSync, INFINITE);
	serviceIsRunning = true;
}

void LogMessage(const std::string &_msg, const int &level)
{
  const std::wstring msg = s2ws(_msg);

	if (eventHandle)
	{
		LPCWSTR tmp;
		tmp = _wcsdup(msg.c_str());

		switch (level)
		{
			case LOG_DEBUG:
				if (minLogLevel >= LOG_DEBUG)
					ReportEventW(eventHandle, EVENTLOG_INFORMATION_TYPE, 0, 0, NULL, 1, 0, &tmp, NULL);
				break;

			case LOG_WARNING:
				if (minLogLevel >= LOG_WARNING)
					ReportEventW(eventHandle, EVENTLOG_WARNING_TYPE, 0, 0, NULL, 1, 0, &tmp, NULL);
				break;

			case LOG_ERROR:
				ReportEventW(eventHandle, EVENTLOG_ERROR_TYPE, 0, 0, NULL, 1, 0, &tmp, NULL);
				stopService();

				// Set pgagent initialized to true, as initService
				// is waiting for it to be intialized
				pgagentInitialized = true;

				// Change service status
				serviceStatus.dwCheckPoint = 0;
				serviceStatus.dwCurrentState = SERVICE_STOPPED;
				SetServiceStatus(serviceStatusHandle, &serviceStatus);

				break;

				// Log startup/connection warnings (valid for any log level)
			case LOG_STARTUP:
				ReportEventW(eventHandle, EVENTLOG_WARNING_TYPE, 0, 0, NULL, 1, 0, &tmp, NULL);
				break;
		}

		if (tmp)
		{
			free((void *)tmp);
			tmp = NULL;
		}
	}
	else
	{
		switch (level)
		{
			case LOG_DEBUG:
				if (minLogLevel >= LOG_DEBUG)
					wprintf(L"DEBUG: %s\n", msg.c_str());
				break;
			case LOG_WARNING:
				if (minLogLevel >= LOG_WARNING)
					wprintf(L"WARNING: %s\n", msg.c_str());
				break;
			case LOG_ERROR:
				wprintf(L"ERROR: %s\n", msg.c_str());
				pgagentInitialized = true;
				exit(1);
				break;
				// Log startup/connection warnings (valid for any log level)
			case LOG_STARTUP:
				wprintf(L"WARNING: %s\n", msg.c_str());
				break;
		}
	}
}

// The main working thread of the service

unsigned int __stdcall threadProcedure(void *unused)
{
	MainLoop();
	return 0;
}


////////////////////////////////////////////////////////////
// a replacement popen for windows.
//
// _popen doesn't work in Win2K from a service so we have to
// do it the fun way :-)

HANDLE win32_popen_r(const WCHAR *command, HANDLE &handle)
{
	HANDLE hWrite, hRead;
	SECURITY_ATTRIBUTES saAttr;
	BOOL ret = FALSE;

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOW siStartInfo;
	WCHAR *cmd;

	cmd = _wcsdup(command);

	// Set the bInheritHandle flag so pipe handles are inherited.
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Create a pipe for the child process's STDOUT.
	if (!CreatePipe(&hRead, &hWrite, &saAttr, 0))
		return NULL;

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	// Now create the child process.
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = hWrite;
	siStartInfo.hStdOutput = hWrite;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	ret = CreateProcessW(NULL,
		cmd,           // command line
		NULL,          // process security attributes
		NULL,          // primary thread security attributes
		TRUE,          // handles are inherited
		0,             // creation flags
		NULL,          // use parent's environment
		NULL,          // use parent's current directory
		&siStartInfo,  // STARTUPINFO pointer
		&piProcInfo);  // receives PROCESS_INFORMATION

	if (!ret)
		return NULL;
	else
		CloseHandle(piProcInfo.hThread);

	// Close the write end of the pipe and return the read end.
	if (!CloseHandle(hWrite))
		return NULL;

	handle = piProcInfo.hProcess;
	return hRead;
}

////////////////////////////////////////////////////////////
// service control functions
bool pauseService()
{
	WaitForSingleObject(serviceSync, shortWait * 1000 - 30);

	if (!serviceIsRunning)
	{
		SuspendThread(threadHandle);
		return true;
	}
	return false;
}


bool continueService()
{
	ReleaseSemaphore(serviceSync, 1, 0);
	ResumeThread(threadHandle);

	return true;
}

bool stopService()
{
	pauseService();
	CloseHandle(threadHandle);
	threadHandle = 0;
	return true;
}

bool initService()
{
	serviceSync = CreateSemaphore(0, 1, 1, 0);

	unsigned int tid;
	pgagentInitialized = false;

	threadHandle = (HANDLE)_beginthreadex(0, 0, threadProcedure, 0, 0, &tid);

	while (!pgagentInitialized)
	{
		if (eventHandle)
		{
			serviceStatus.dwWaitHint += 1000;
			serviceStatus.dwCheckPoint++;
			SetServiceStatus(serviceStatusHandle, (LPSERVICE_STATUS)&serviceStatus);
		}
		Sleep(1000);
	}

	return (threadHandle != 0);
}


void CALLBACK serviceHandler(DWORD ctl)
{
	switch (ctl)
	{
		case SERVICE_CONTROL_STOP:
		{
			serviceStatus.dwCheckPoint++;
			serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
			SetServiceStatus(serviceStatusHandle, &serviceStatus);

			stopService();

			serviceStatus.dwCheckPoint = 0;
			serviceStatus.dwCurrentState = SERVICE_STOPPED;
			SetServiceStatus(serviceStatusHandle, &serviceStatus);
			break;
		}
		case SERVICE_CONTROL_PAUSE:
		{
			pauseService();

			serviceStatus.dwCurrentState = SERVICE_PAUSED;
			SetServiceStatus(serviceStatusHandle, &serviceStatus);

			break;
		}
		case SERVICE_CONTROL_CONTINUE:
		{
			continueService();
			serviceStatus.dwCurrentState = SERVICE_RUNNING;
			SetServiceStatus(serviceStatusHandle, &serviceStatus);
			break;
		}
		default:
		{
			break;
		}
	}
}


void CALLBACK serviceMain(DWORD argc, LPTSTR *argv)
{
	serviceName = s2ws((const char *)argv[0]);
	serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	serviceStatus.dwCurrentState = SERVICE_START_PENDING;
	serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE;
	serviceStatus.dwWin32ExitCode = 0;
	serviceStatus.dwCheckPoint = 0;
	serviceStatus.dwWaitHint = 15000;
	serviceStatusHandle = RegisterServiceCtrlHandlerW(serviceName.c_str(), serviceHandler);

	if (serviceStatusHandle)
	{
		SetServiceStatus(serviceStatusHandle, &serviceStatus);

		if (initService())
		{
			serviceStatus.dwCurrentState = SERVICE_RUNNING;
			serviceStatus.dwWaitHint = 1000;
		}
		else
			serviceStatus.dwCurrentState = SERVICE_STOPPED;

		SetServiceStatus(serviceStatusHandle, &serviceStatus);
	}
}


////////////////////////////////////////////////////////////
// installation and removal
bool installService(const std::wstring &serviceName, const std::wstring &executable, const std::wstring &args, const std::wstring &displayname, const std::wstring &user, const std::wstring &password)
{
	bool done = false;

	SC_HANDLE manager = OpenSCManager(0, 0, SC_MANAGER_ALL_ACCESS);
	if (manager)
	{
		std::wstring cmd = executable + L" " + args;

		std::wstring quser;
		if (user.find(L"\\") == std::string::npos)
			quser = L".\\" + user;
		else
			quser = user;

		SC_HANDLE service = CreateServiceW(manager, serviceName.c_str(), displayname.c_str(), SERVICE_ALL_ACCESS,
			SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
			cmd.c_str(), 0, 0, 0, quser.c_str(), password.c_str());

		if (service)
		{
			done = true;
			CloseServiceHandle(service);
		}
		else
		{
			LPVOID lpMsgBuf;
			DWORD dw = GetLastError();

			FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,
				dw,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpMsgBuf,
				0, NULL
				);
			LogMessage(ws2s((boost::wformat(L"%s") % lpMsgBuf).str()), LOG_ERROR);
		}

		CloseServiceHandle(manager);
	}

	// Setup the event message DLL
	const std::wstring key_path(L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" + serviceName);
	HKEY key;
	DWORD last_error = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
		key_path.c_str(),
		0,
		0,
		REG_OPTION_NON_VOLATILE,
		KEY_SET_VALUE,
		0,
		&key,
		0);

	if (ERROR_SUCCESS == last_error)
	{
		std::size_t found = executable.find_last_of(L"/\\");
		std::wstring path = executable.substr(0, found) + L"\\pgaevent.dll";

		DWORD last_error;
		const DWORD types_supported = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;

		last_error = RegSetValueExW(key,
			L"EventMessageFile",
			0,
			REG_SZ,
			(unsigned char *)(path.c_str()),
			(path.length() + 1)*sizeof(std::wstring));

		if (ERROR_SUCCESS == last_error)
		{
			last_error = RegSetValueExW(key,
				L"TypesSupported",
				0,
				REG_DWORD,
				(LPBYTE)&types_supported,
				sizeof(types_supported));
		}

		if (ERROR_SUCCESS != last_error)
		{
			LogMessage(
				"Could not set the event message file registry value.", LOG_WARNING
			);
		}

		RegCloseKey(key);
	}
	else
	{
		LogMessage(
			"Could not open the message source registry key.", LOG_WARNING
		);
	}

	return done;
}


bool removeService(const std::wstring &serviceName)
{
	bool done = false;

	SC_HANDLE manager = OpenSCManager(0, 0, SC_MANAGER_ALL_ACCESS);
	if (manager)
	{
		SC_HANDLE service = OpenServiceW(manager, serviceName.c_str(), SERVICE_ALL_ACCESS);
		if (service)
		{
			SERVICE_STATUS serviceStatus;
			ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);

			int retries;
			for (retries = 0; retries < 5; retries++)
			{
				if (QueryServiceStatus(service, &serviceStatus))
				{
					if (serviceStatus.dwCurrentState == SERVICE_STOPPED)
					{
						DeleteService(service);
						done = true;
						break;
					}
					Sleep(1000L);
				}
			}
			CloseServiceHandle(service);
		}
		CloseServiceHandle(manager);
	}

	// Remove the event message DLL
	const std::wstring key_path(L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" + serviceName);
	DWORD last_error = RegDeleteKeyW(HKEY_LOCAL_MACHINE, key_path.c_str());
	if (ERROR_SUCCESS != last_error)
	{
		LogMessage("Failed to uninstall source", LOG_ERROR);
	}

	return done;
}

void usage(const std::string &executable)
{
	printVersion();

	printf("Usage:\n");
	printf("%s REMOVE <serviceName>\n", executable.c_str());
	printf("%s INSTALL <serviceName> [options] <connect-string>\n", executable.c_str());
	printf("%s DEBUG [options] <connect-string>\n", executable.c_str());
	printf("options:\n");
	printf("-v (display version info and then exit)\n");
	printf("-u <user or DOMAIN\\user>\n");
	printf("-p <password>\n");
	printf("-d <displayname>\n");
	printf("-t <poll time interval in seconds (default 10)>\n");
	printf("-r <retry period after connection abort in seconds (>=10, default 30)>\n");
	printf("-l <logging verbosity (ERROR=0, WARNING=1, DEBUG=2, default 0)>\n");
}



////////////////////////////////////////////////////////////

void setupForRun(int argc, char **argv, bool debug, const std::wstring &executable)
{
	if (!debug)
	{
		eventHandle = RegisterEventSourceW(0, serviceName.c_str());
		if (!eventHandle)
			LogMessage("Couldn't register event handle.", LOG_ERROR);
	}

	setOptions(argc, argv, ws2s(executable));
}


void main(int argc, char **argv)
{
	std::string executable;
	executable.assign(*argv++);

	if (argc < 3)
	{
		usage(executable);
		return;
	}

	std::wstring command;
	command.assign(s2ws(*argv++));

	if (command != L"DEBUG")
	{
		serviceName.assign(s2ws(*argv++));
		argc -= 3;
	}
	else
		argc -= 2;

	if (command == L"INSTALL")
	{
		std::wstring displayname = L"PostgreSQL Scheduling Agent - " + serviceName;
		std::wstring args = L"RUN " + serviceName;

		while (argc-- > 0)
		{
			if (argv[0][0] == '-')
			{
				switch (argv[0][1])
				{
					case 'u':
					{
						user = s2ws(getArg(argc, argv));
						break;
					}
					case 'p':
					{
						password = s2ws(getArg(argc, argv));
						break;
					}
					case 'd':
					{
						displayname = s2ws(getArg(argc, argv));
						break;
					}
					default:
					{
						args += L" " + s2ws(*argv);
						break;
					}
				}
			}
			else
			{
				args += L" " + s2ws(*argv);
			}

			argv++;
		}

		bool rc = installService(serviceName, s2ws(executable), args, displayname, user, password);
	}
	else if (command == L"REMOVE")
	{
		bool rc = removeService(serviceName);
	}
	else if (command == L"DEBUG")
	{
		setupForRun(argc, argv, true, s2ws(executable));

		initService();
#if START_SUSPENDED
		continueService();
#endif

		WaitForSingleObject(threadHandle, INFINITE);
	}
	else if (command == L"RUN")
	{
		std::string app = "pgAgent Service";

		SERVICE_TABLE_ENTRY serviceTable[] =
		{ (LPSTR)app.c_str(), serviceMain, 0, 0 };

		setupForRun(argc, argv, false, s2ws(executable));
		if (!StartServiceCtrlDispatcher(serviceTable))
		{
			DWORD rc = GetLastError();
			if (rc)
			{
			}
		}
	}
	else
	{
		usage(executable);
	}

	return;
}

#endif // WIN32
