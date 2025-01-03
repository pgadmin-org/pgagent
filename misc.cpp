//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// misc.cpp - misc functions
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"
#include "connection.h"

#include <boost/locale/encoding_utf.hpp>

#if !BOOST_OS_WINDOWS
#include <unistd.h>
#include <stdlib.h>
#endif

#define APPVERSION_STR PGAGENT_VERSION

namespace fs = boost::filesystem;

// In unix.c or win32.c
void usage(const std::string &executable);

std::string getArg(int &argc, char **&argv)
{
	std::string res;

	if (argv[0][2])
		return (argv[0] + 2);

	if (argc >= 1)
	{
		argc--;
		argv++;

		return argv[0];
	}

	// very bad!
	LogMessage("Invalid command line argument", LOG_ERROR);

	return res;
}

void printVersion()
{
	printf("PostgreSQL Scheduling Agent\n");
	printf("Version: %s\n", APPVERSION_STR);
};

void setOptions(int argc, char **argv, const std::string &executable)
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
			if (!connectString.empty())
				connectString += " ";
			connectString += *argv;
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

std::string NumToStr(const long l)
{
	return boost::lexical_cast<std::string>(l);
}

#if BOOST_OS_WINDOWS
// This function is used to convert const std::str to std::wstring.
std::wstring s2ws(const std::string &str)
{
	using boost::locale::conv::utf_to_utf;
	return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

// This function is used to convert std::wstring to std::str
std::string ws2s(const std::wstring &wstr)
{
	using boost::locale::conv::utf_to_utf;
	return utf_to_utf<char>(wstr.c_str(), wstr.c_str() + wstr.size());
}
#endif

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

bool createUniqueTemporaryDirectory(const std::string &prefix, fs::path &uniqueDir)
{
	const unsigned short MAX_ATTEMPTS = 100;
	unsigned short attempts = 0;

	try {
		fs::path tempDir = fs::temp_directory_path();

		do {
			if (attempts++ >= MAX_ATTEMPTS)
				return false;

			uniqueDir = tempDir / fs::unique_path(
				prefix + "%%%%%%%%-%%%%-%%%%-%%%%-%%%%%%%%%%%%"
			);

			// Check if exists
			if (boost::filesystem::is_directory(uniqueDir))
				continue;

			// Create the directory securely
			if (!fs::create_directory(uniqueDir)) {
				return false;
			}

			// Set appropriate permissions (example: owner read/write/execute only)
			fs::permissions(uniqueDir, boost::filesystem::owner_all);

			return true;
		} while (true);
	} catch (const fs::filesystem_error &ex) {
		return false;
	}
}
