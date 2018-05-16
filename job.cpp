//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2016 The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// job.cpp - pgAgent job
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

#include <sys/types.h>

#if !BOOST_OS_WINDOWS
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

Job::Job(DBconn *conn, const std::wstring &jid)
{
	threadConn = conn;
	jobid = jid;
	status = L"";

	LogMessage((L"Starting job: " + jobid), LOG_DEBUG);

	int rc = threadConn->ExecuteVoid(
		L"UPDATE pgagent.pga_job SET jobagentid=" + backendPid + L", joblastrun=now() " +
		L" WHERE jobagentid IS NULL AND jobid=" + jobid);

	if (rc == 1)
	{
		DBresultPtr id = threadConn->Execute(
			L"SELECT nextval('pgagent.pga_joblog_jlgid_seq') AS id");
		if (id)
		{
			logid = id->GetString(L"id");

			DBresultPtr res = threadConn->Execute(
				L"INSERT INTO pgagent.pga_joblog(jlgid, jlgjobid, jlgstatus) "
				L"VALUES (" + logid + L", " + jobid + L", 'r')");
			if (res)
			{
				status = L"r";
			}
		}
	}
}


Job::~Job()
{
	if (status != L"")
	{
		threadConn->ExecuteVoid(
			L"UPDATE pgagent.pga_joblog "
			L"   SET jlgstatus='" + status + L"', jlgduration=now() - jlgstart " +
			L" WHERE jlgid=" + logid + L";\n" +

			L"UPDATE pgagent.pga_job " +
			L"   SET jobagentid=NULL, jobnextrun=NULL " +
			L" WHERE jobid=" + jobid
			);
	}
	threadConn->Return();

	LogMessage((L"Completed job: " + jobid), LOG_DEBUG);
}


int Job::Execute()
{
	int rc = 0;
	bool succeeded = false;
	DBresultPtr steps = threadConn->Execute(
		L"SELECT * "
		L"  FROM pgagent.pga_jobstep "
		L" WHERE jstenabled "
		L"   AND jstjobid=" + jobid +
		L" ORDER BY jstname, jstid");

	if (!steps)
	{
		status = L"i";
		return -1;
	}

	while (steps->HasData())
	{
		DBconn *stepConn;
		std::wstring jslid, stepid, jpecode, output;

		stepid = steps->GetString(L"jstid");

		DBresultPtr id = threadConn->Execute(
			L"SELECT nextval('pgagent.pga_jobsteplog_jslid_seq') AS id");
		if (id)
		{
			jslid = id->GetString(L"id");
			DBresultPtr res = threadConn->Execute(
				L"INSERT INTO pgagent.pga_jobsteplog(jslid, jsljlgid, jsljstid, jslstatus) "
				L"SELECT " + jslid + L", " + logid + L", " + stepid + L", 'r'" +
				L"  FROM pgagent.pga_jobstep WHERE jstid=" + stepid);

			if (res)
			{
				rc = res->RowsAffected();
			}
			else
				rc = -1;
		}

		if (rc != 1)
		{
			status = L"i";
			return -1;
		}

		switch ((int)steps->GetString(L"jstkind")[0])
		{
			case 's':
			{
				std::wstring jstdbname = steps->GetString(L"jstdbname");
				std::wstring jstconnstr = steps->GetString(L"jstconnstr");

				stepConn = DBconn::Get(jstconnstr, jstdbname);
				if (stepConn)
				{
					LogMessage((L"Executing SQL step " + stepid + L"(part of job " + jobid + L")"), LOG_DEBUG);
					rc = stepConn->ExecuteVoid(steps->GetString(L"jstcode"));
					succeeded = stepConn->LastCommandOk();
					output = stepConn->GetLastError();
					stepConn->Return();
				}
				else
				{
					output = L"Couldn't get a connection to the database!";
					succeeded = false;
				}


				break;
			}
			case 'b':
			{
				// Batch jobs are more complex thank SQL, for obvious reasons...
				LogMessage((L"Executing batch step" + stepid + L"(part of job " + jobid + L")"), LOG_DEBUG);

				// Get a temporary filename, then reuse it to create an empty directory.
                                std::wstring wTmpDir = getTemporaryDirectoryPath();
				std::string sDirectory(wTmpDir.begin(), wTmpDir.end());
				std::string sFilesName = "";
				std::string prefix = "pga_";
				int last_n_char = 7;

                                // Genrate random string of 6 characters long to make unique dir name
                                std::string result = generateRandomString(7);
				sFilesName = prefix + result;
#if BOOST_OS_WINDOWS
				std::string sModel = (boost::format("%s\\%s") % sDirectory % sFilesName).str();
#else
				std::string sModel = (boost::format("%s/%s") % sDirectory % sFilesName).str();
#endif
				std::string dir_name = sModel;
				std::wstring dirname(dir_name.begin(), dir_name.end());

				if (dirname == L"")
				{
					output = L"Couldn't get a temporary filename!";
					LogMessage(output, LOG_WARNING);
					rc = -1;
					break;
				}

				if (!boost::filesystem::create_directory(boost::filesystem::path(dir_name)))
				{
					LogMessage((L"Couldn't create temporary directory: " + dirname), LOG_WARNING);
					rc = -1;
					break;
				}

#if BOOST_OS_WINDOWS
				std::wstring filename = dirname + L"\\" + jobid + L"_" + stepid + L".bat";
				std::wstring errorFile = dirname + L"\\" + jobid + L"_" + stepid + L"_error.txt";
#else
				std::wstring filename = dirname + L"/" + jobid + L"_" + stepid + L".scr";
				std::wstring errorFile = dirname + L"/" + jobid + L"_" + stepid + L"_error.txt";
#endif

				std::wstring code = steps->GetString(L"jstcode");

				// Cleanup the code. If we're on Windows, we need to make all line ends \r\n,
				// If we're on Unix, we need \n
				boost::replace_all(code, "\r\n", "\n");
#if BOOST_OS_WINDOWS
				boost::replace_all(code, "\n", "\r\n");
#endif
				std::ofstream out_file;
				std::string s_code(code.begin(), code.end());
				std::string sfilename(filename.begin(), filename.end());
				std::string serrorFile(errorFile.begin(), errorFile.end());

				out_file.open((const char *)sfilename.c_str(), std::ios::out);
				if (out_file.fail())
				{
					LogMessage((L"Couldn't open temporary script file: " + filename), LOG_WARNING);
					if (boost::filesystem::exists(dirname))
						boost::filesystem::remove_all(dirname);
					rc = -1;
					break;
				}
				else
				{
					out_file << s_code;
					out_file.close();
#if !BOOST_OS_WINDOWS
					// change file permission to 700 for executable in linux
					int ret = chmod((const char *)sfilename.c_str(), S_IRWXU);
					if (ret != 0)
						LogMessage((L"Error setting executable permission to file: " + filename), LOG_DEBUG);
#endif
				}

				LogMessage((L"Executing script file: " + filename), LOG_DEBUG);

				// freopen function is used to redirect output of stream (stderr in our case)
				// into the specified file.
				FILE *fpError = freopen((const char *)serrorFile.c_str(), "w", stderr);

				// Execute the file and capture the output
#if BOOST_OS_WINDOWS
				// The Windows way
				HANDLE h_script, h_process;
				DWORD dwRead;
				char chBuf[4098];

				h_script = win32_popen_r(filename.c_str(), h_process);
				if (!h_script)
				{
					LogMessage((boost::wformat(L"Couldn't execute script: %s, GetLastError() returned %s, errno = %d") % filename.c_str() % GetLastError() % errno).str(), LOG_WARNING);
					CloseHandle(h_process);
					rc = -1;
					if (fpError)
						fclose(fpError);
					break;
				}


				// Read output from the child process
				if (h_script)
				{
					for (;;)
					{
						if (!ReadFile(h_script, chBuf, 4096, &dwRead, NULL) || dwRead == 0)
							break;

						chBuf[dwRead] = 0;
						output += CharToWString((const char *)chBuf);
					}
				}


				GetExitCodeProcess(h_process, (LPDWORD)&rc);
				CloseHandle(h_process);
				CloseHandle(h_script);

#else
				// The *nix way.
				FILE *fp_script;
				char buf[4098];

				fp_script = popen((const char *)sfilename.c_str(), "r");
				if (!fp_script)
				{
					LogMessage((boost::wformat(L"Couldn't execute script: %s, errno = %d") % filename.c_str() % errno).str(), LOG_WARNING);
					rc = -1;
					if(fpError)
						fclose(fpError);
					break;
				}


				while(!feof(fp_script))
				{
					if (fgets(buf, 4096, fp_script) != NULL)
						output += CharToWString((const char *)buf);
				}

				rc = pclose(fp_script);

				if (WIFEXITED(rc))
					rc = WEXITSTATUS(rc);
				else
					rc = -1;

#endif

				// set success status for batch runs, be pessimistic by default
				LogMessage((boost::wformat(L"Script return code: %d") % rc).str(), LOG_DEBUG);
				succeeded = ((rc == 0) ? true : false);
				// If output is empty then either script did not return any output
				// or script threw some error into stderr.
				// Check script threw some error into stderr
				if (fpError)
				{
					fclose(fpError);
					FILE* fpErr = fopen((const char *)serrorFile.c_str(), "r");
					if (fpErr)
					{
						char buffer[4098];
						std::wstring errorMsg = L"";
						while (!feof(fpErr))
						{
							if (fgets(buffer, 4096, fpErr) != NULL)
								errorMsg += CharToWString(buffer);
						}

						if (errorMsg != L"") {
							std::wstring errmsg = L"Script Error: \n" + errorMsg + L"\n",
								LogMessage((L"Script Error: \n" + errorMsg + L"\n"), LOG_WARNING);
							output += L"\n" + errmsg;
						}

						fclose(fpErr);
					}
				}

				// Delete the file/directory. If we fail, don't overwrite the script output in the log, just throw warnings.
				try
				{
					boost::filesystem::path dir_path(dir_name);
					if (boost::filesystem::exists(dir_path))
						boost::filesystem::remove_all(dir_path);
				}
				catch (boost::filesystem::filesystem_error const & e)
				{
					//display error message
					LogMessage(CharToWString((const char *)e.what()), LOG_WARNING);
					break;
				}

				break;
			}
			default:
			{
				output = L"Invalid step type!";
				status = L"i";
				return -1;
			}
		}

		std::wstring stepstatus;
		if (succeeded)
			stepstatus = L"s";
		else
			stepstatus = steps->GetString(L"jstonerror");

		rc = threadConn->ExecuteVoid(
			L"UPDATE pgagent.pga_jobsteplog "
			L"   SET jslduration = now() - jslstart, "
			L"       jslresult = " + NumToStr(rc) + L", jslstatus = '" + stepstatus + L"', " +
			L"       jsloutput = " + threadConn->qtDbString(output) + L" " +
			L" WHERE jslid=" + jslid);
		if (rc != 1 || stepstatus == L"f")
		{
			status = L"f";
			return -1;
		}
		steps->MoveNext();
	}

	status = L"s";
	return 0;
}


JobThread::JobThread(const std::wstring &jid)
    : m_jobid(jid)
{
	LogMessage((L"Creating job thread for job " + m_jobid), LOG_DEBUG);
}


JobThread::~JobThread()
{
	LogMessage(L"Destroying job thread for job " + m_jobid, LOG_DEBUG);
}


void JobThread::operator()()
{
	DBconn *threadConn = DBconn::Get(
		DBconn::GetBasicConnectString(), serviceDBname
		);

	if (threadConn)
	{
		Job job(threadConn, m_jobid);

		if (job.Runnable())
		{
			job.Execute();
		}
		else
		{
			// Failed to launch the thread. Insert an entry with
			// "internal error" status in the joblog table, to leave
			// a trace of fact that we tried to launch the job.
			DBresultPtr res = threadConn->Execute(
				L"INSERT INTO pgagent.pga_joblog(jlgid, jlgjobid, jlgstatus) "
				L"VALUES (nextval('pgagent.pga_joblog_jlgid_seq'), " +
				m_jobid + L", 'i')"
				);
			if (res)
				res = NULL;
		}
	}
}
