//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2018 The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// misc.cpp - misc functions
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"
#include "connection.h"

#if !BOOST_OS_WINDOWS
#include <unistd.h>
#include <stdlib.h>
#endif

#define APPVERSION_STR PGAGENT_VERSION

// In unix.c or win32.c
void usage(const std::wstring &executable);

std::wstring getArg(int &argc, char **&argv)
{
	std::wstring s;
	if (argv[0][2])
		s = CharToWString(argv[0] + 2);
	else
	{
		if (argc >= 1)
		{
			argc--;
			argv++;
			s = CharToWString(argv[0]);
		}
		else
		{
			// very bad!
			LogMessage(L"Invalid command line argument", LOG_ERROR);
		}
	}

	return s;
}

void printVersion()
{
	printf("PostgreSQL Scheduling Agent\n");
	printf("Version: %s\n", APPVERSION_STR);
}

void setOptions(int argc, char **argv, const std::wstring &executable)
{
	while (argc-- > 0)
	{
		if (argv[0][0] == '-')
		{
			switch (argv[0][1])
			{
				case 't':
				{
					int val = atoi((const char*)getArg(argc, argv).c_str());
					if (val > 0)
						shortWait = val;
					break;
				}
				case 'r':
				{
					int val = atoi((const char*)getArg(argc, argv).c_str());
					if (val >= 10)
						longWait = val;
					break;
				}
				case 'l':
				{
					int val = atoi((const char*)getArg(argc, argv).c_str());
					if (val >= 0 && val <= 2)
						minLogLevel = val;
					break;
				}
				case 'v':
				{
					printVersion();
					exit(0);
				}
#if !BOOST_OS_WINDOWS
				case 'f':
				{
					runInForeground = true;
					break;
				}
				case 's':
				{
					logFile = getArg(argc, argv);
					break;
				}
#endif
				default:
				{
					usage(executable);
					exit(1);
				}
			}
		}
		else
		{
			if (connectString != L"")
				connectString += L" ";
			connectString = connectString + CharToWString(*argv);
			if (**argv == '"')
				connectString = connectString.substr(1, connectString.length() - 2);
		}
		argv++;
	}
}


void WaitAWhile(const bool waitLong)
{
	int count;
	if (waitLong)
		count = longWait;
	else
		count = shortWait;

	while (count--)
	{
#ifdef WIN32
		CheckForInterrupt();
		Sleep(1000);
#else
		sleep(1);
#endif
	}
}

std::wstring NumToStr(const long l)
{
	std::wstring buf(boost::lexical_cast<std::wstring>(l));
	return buf;
}

// This function is used to convert char* to std::wstring.
std::wstring CharToWString(const char* cstr)
{
	std::string s = std::string(cstr);
	std::wstring wsTmp(s.begin(), s.end());
	return wsTmp;
}

// This function is used to convert std::wstring to char *.
char * WStringToChar(const std::wstring &wstr)
{
	const wchar_t *wchar_str = wstr.c_str();
	int wstr_length = wcslen(wchar_str);
	char *dst = new char[wstr_length + 10];
	memset(dst, 0x00, (wstr_length + 10));
	wcstombs(dst, wchar_str, wstr_length);
	return dst;
}

// Below function will generate random string of given character.
std::string generateRandomString(size_t length)
{
    char *str = new char[length];
    size_t i = 0;
    int r;

    str[length - 1] = '\0';
    srand(time(NULL));

    for(i = 0; i < length - 1; ++i)
    {
        for(;;)
        {
            // interval between 'A' and 'z'
            r = rand() % 57 + 65;
            if((r >= 65 && r <= 90) || (r >= 97 && r <= 122))
            {
                str[i] = (char)r;
                break;
            }
        }
    }

    std::string result(str);

    if (str != NULL)
    {
        delete []str;
        str = NULL;
    }

    return result;
}

std::wstring getTemporaryDirectoryPath()
{

#if BOOST_OS_WINDOWS
    std::wstring tmp_dir = L"";
    wchar_t wcharPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, wcharPath))
        tmp_dir = wcharPath;
#else
    // Read this environment variable (TMPDIR, TMP, TEMP, TEMPDIR) and if not found then use "/tmp"
    std::wstring tmp_dir = L"/tmp";
    const char *s = getenv("TMPDIR");

    if (s != NULL)
        tmp_dir = CharToWString(s);
    else
    {
        const char *s1 = getenv("TMP");
        if (s1 != NULL)
            tmp_dir = CharToWString(s1);
        else {
            const char *s2 = getenv("TEMP");
            if (s2 != NULL)
                tmp_dir = CharToWString(s2);
            else {
                const char *s3 = getenv("TEMPDIR");
                if (s3 != NULL)
                    tmp_dir = CharToWString(s2);
            }
        }
    }
#endif
    return tmp_dir;
}
