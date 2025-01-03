//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
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

Job::Job(DBconn *conn, const std::string &jid)
{
	m_threadConn = conn;
	m_jobid = jid;
	m_status = "";

	LogMessage("Starting job: " + m_jobid, LOG_DEBUG);

	int rc = m_threadConn->ExecuteVoid(
		"UPDATE pgagent.pga_job SET jobagentid=" + backendPid +
		", joblastrun=now() WHERE jobagentid IS NULL AND jobid=" + m_jobid
	);

	if (rc == 1)
	{
		DBresultPtr id = m_threadConn->Execute(
			"SELECT nextval('pgagent.pga_joblog_jlgid_seq') AS id"
		);
		if (id)
		{
			m_logid = id->GetString("id");

			DBresultPtr res = m_threadConn->Execute(
				"INSERT INTO pgagent.pga_joblog(jlgid, jlgjobid, jlgstatus) "
				"VALUES (" + m_logid + ", " + m_jobid + ", 'r')");
			if (res)
			{
				m_status = "r";
			}
		}
	}
}


Job::~Job()
{
	if (!m_status.empty())
	{
		m_threadConn->ExecuteVoid(
			"UPDATE pgagent.pga_joblog "
			"   SET jlgstatus='" + m_status + "', jlgduration=now() - jlgstart " +
			" WHERE jlgid=" + m_logid + ";\n" +

			"UPDATE pgagent.pga_job " +
			"   SET jobagentid=NULL, jobnextrun=NULL " +
			" WHERE jobid=" + m_jobid
		);
	}
	m_threadConn->Return();

	LogMessage("Completed job: " + m_jobid, LOG_DEBUG);
}


int Job::Execute()
{
	int rc = 0;
	bool succeeded = false;
	DBresultPtr steps = m_threadConn->Execute(
		"SELECT * "
		"  FROM pgagent.pga_jobstep "
		" WHERE jstenabled "
		"   AND jstjobid=" + m_jobid +
		" ORDER BY jstname, jstid");

	if (!steps)
	{
		LogMessage("No steps found for jobid " + m_jobid, LOG_WARNING);
		m_status = "i";
		return -1;
	}

	while (steps->HasData())
	{
		DBconn      *stepConn = nullptr;
		std::string  jslid, stepid, jpecode, output;

		stepid = steps->GetString("jstid");

		DBresultPtr id = m_threadConn->Execute(
			"SELECT nextval('pgagent.pga_jobsteplog_jslid_seq') AS id"
		);

		if (id)
		{
			jslid = id->GetString("id");
			DBresultPtr res = m_threadConn->Execute(
				"INSERT INTO pgagent.pga_jobsteplog(jslid, jsljlgid, jsljstid, jslstatus) "
				"SELECT " + jslid + ", " + m_logid + ", " + stepid + ", 'r'" +
				"  FROM pgagent.pga_jobstep WHERE jstid=" + stepid);

			if (res)
			{
				rc = res->RowsAffected();
				LogMessage("Number of rows affected for jobid " + m_jobid, LOG_DEBUG);
			}
			else
				rc = -1;
		}

		if (rc != 1)
		{
			LogMessage("Value of rc is " + std::to_string(rc) + " for job " + m_jobid, LOG_WARNING);
			m_status = "i";
			return -1;
		}

		switch ((int)steps->GetString("jstkind")[0])
		{
			case 's':
			{
				std::string jstdbname = steps->GetString("jstdbname");
				std::string jstconnstr = steps->GetString("jstconnstr");

				stepConn = DBconn::Get(jstconnstr, jstdbname);

				if (stepConn)
				{
					LogMessage(
						"Executing SQL step " + stepid + "(part of job " + m_jobid + ")",
						 LOG_DEBUG
					);
					rc = stepConn->ExecuteVoid(steps->GetString("jstcode"));
					succeeded = stepConn->LastCommandOk();
					output = stepConn->GetLastError();
					stepConn->Return();
				}
				else
				{
					output = "Couldn't get a connection to the database!";
					succeeded = false;
				}


				break;
			}
			case 'b':
			{
				// Batch jobs are more complex thank SQL, for obvious reasons...
				LogMessage(
					"Executing batch step" + stepid + "(part of job " + m_jobid + ")",
          LOG_DEBUG
				);

				namespace fs = boost::filesystem;

				// Generate unique temporary directory
				std::string prefix = (
					boost::format("pga_%s_%s_") % m_jobid % stepid
				).str();

				fs::path jobDir;
				fs::path filepath((
					boost::format("%s_%s.%s") %
					m_jobid % stepid %
#if BOOST_OS_WINDOWS
					".bat"
#else
					".scr"
#endif
				).str());
				fs::path errorFilePath(
					(boost::format("%s_%s_error.txt") % m_jobid % stepid).str()
				);

				if (!createUniqueTemporaryDirectory(prefix, jobDir))
				{
					output = "Couldn't get a temporary filename!";
					LogMessage(output, LOG_WARNING);
					rc = -1;

					break;
				}

				filepath = jobDir / filepath;
				errorFilePath = jobDir / errorFilePath;

				std::string filename = filepath.string();
				std::string errorFile = errorFilePath.string();

				std::string code = steps->GetString("jstcode");

				// Cleanup the code. If we're on Windows, we need to make all line ends \r\n,
				// If we're on Unix, we need \n
				boost::replace_all(code, "\r\n", "\n");
#if BOOST_OS_WINDOWS
				boost::replace_all(code, "\n", "\r\n");
#endif
				std::ofstream out_file;

				out_file.open((const char *)filename.c_str(), std::ios::out);

				if (out_file.fail())
				{
					LogMessage(
						"Couldn't open temporary script file: " + filename,
						LOG_WARNING
					);

					if (boost::filesystem::exists(jobDir))
						boost::filesystem::remove_all(jobDir);

					rc = -1;
					break;
				}
				else
				{
					out_file << code;
					out_file.close();

#if !BOOST_OS_WINDOWS
					// Change file permission to 700 for executable in linux
					try {
						boost::filesystem::permissions(
							filepath, boost::filesystem::owner_all
						);
					} catch (const fs::filesystem_error &ex) {
						LogMessage(
							"Error setting executable permission to file: " +
							filename, LOG_DEBUG
						);
					}
#endif
				}

				LogMessage("Executing script file: " + filename, LOG_DEBUG);

				// freopen function is used to redirect output of stream (stderr in our case)
				// into the specified file.
				FILE *fpError = freopen((const char *)errorFile.c_str(), "w", stderr);

				// Execute the file and capture the output
#if BOOST_OS_WINDOWS
				// The Windows way
				HANDLE h_script, h_process;
				DWORD  dwRead;
				char   chBuf[4098];

				h_script = win32_popen_r(s2ws(filename).c_str(), h_process);

				if (!h_script)
				{
					LogMessage((boost::format(
						"Couldn't execute script: %s, GetLastError() returned %d, errno = %d"
					) % filename.c_str() % GetLastError() % errno).str(), LOG_WARNING);
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
						output += (const char *)chBuf;
					}
				}


				GetExitCodeProcess(h_process, (LPDWORD)&rc);
				CloseHandle(h_process);
				CloseHandle(h_script);

#else
				// The *nix way.
				FILE *fp_script = nullptr;
				char  buf[4098];

				fp_script = popen((const char *)filename.c_str(), "r");

				if (!fp_script)
				{
					LogMessage((boost::format(
						"Couldn't execute script: %s, errno = %d"
					) % filename.c_str() % errno).str(), LOG_WARNING);
					rc = -1;

					if(fpError)
						fclose(fpError);

					break;
				}


				while(!feof(fp_script))
				{
					if (fgets(buf, 4096, fp_script) != NULL)
						output += (const char *)buf;
				}

				rc = pclose(fp_script);

				if (WIFEXITED(rc))
					rc = WEXITSTATUS(rc);
				else
					rc = -1;

#endif

				// set success status for batch runs, be pessimistic by default
				LogMessage(
					(boost::format("Script return code: %d") % rc).str(),
					LOG_DEBUG
				);

				succeeded = ((rc == 0) ? true : false);
				// If output is empty then either script did not return any output
				// or script threw some error into stderr.
				// Check script threw some error into stderr
				if (fpError)
				{
					fclose(fpError);
					FILE* fpErr = fopen((const char *)errorFile.c_str(), "r");

					if (fpErr)
					{
						char buffer[4098];
						std::string errorMsg = "";

						while (!feof(fpErr))
						{
							if (fgets(buffer, 4096, fpErr) != NULL)
								errorMsg += buffer;
						}

						if (errorMsg != "") {
							std::string errmsg = "Script Error: \n" + errorMsg + "\n";
							LogMessage("Script Error: \n" + errorMsg + "\n", LOG_WARNING);
							output += "\n" + errmsg;
						}

						fclose(fpErr);
					}
				}

				// Delete the file/directory. If we fail, don't overwrite the script
				// output in the log, just throw warnings.
				try
				{
					if (boost::filesystem::exists(jobDir))
						boost::filesystem::remove_all(jobDir);
				}
				catch (boost::filesystem::filesystem_error const & e)
				{
					//display error message
					LogMessage((const char *)e.what(), LOG_WARNING);
					break;
				}

				break;
			}
			default:
			{
				output = "Invalid step type!";
				LogMessage("Invalid step type!", LOG_WARNING);
				m_status = "i";
				return -1;
			}
		}

		std::string stepstatus;
		if (succeeded)
			stepstatus = "s";
		else
			stepstatus = steps->GetString("jstonerror");

		rc = m_threadConn->ExecuteVoid(
			"UPDATE pgagent.pga_jobsteplog "
			"   SET jslduration = now() - jslstart, "
			"       jslresult = " + NumToStr(rc) + ", jslstatus = '" + stepstatus + "', " +
			"       jsloutput = " + m_threadConn->qtDbString(output) + " " +
			" WHERE jslid=" + jslid);
		if (rc != 1 || stepstatus == "f")
		{
			m_status = "f";
			return -1;
		}
		steps->MoveNext();
	}

	m_status = "s";
	return 0;
}


JobThread::JobThread(const std::string &jid)
    : m_jobid(jid)
{
	LogMessage("Creating job thread for job " + m_jobid, LOG_DEBUG);
}


JobThread::~JobThread()
{
	LogMessage("Destroying job thread for job " + m_jobid, LOG_DEBUG);
}


void JobThread::operator()()
{
	DBconn *threadConn = DBconn::Get();

	if (threadConn)
	{
		Job job(threadConn, m_jobid);

		if (job.Runnable())
		{
			job.Execute();
		}
		else
		{
			LogMessage("Failed to launch the thread for job " + m_jobid +
			". Inserting an entry to the joblog table with status 'i'", LOG_WARNING);

			// Failed to launch the thread. Insert an entry with
			// "internal error" status in the joblog table, to leave
			// a trace of fact that we tried to launch the job.
			DBresultPtr res = threadConn->Execute(
				"INSERT INTO pgagent.pga_joblog(jlgid, jlgjobid, jlgstatus) "
				"VALUES (nextval('pgagent.pga_joblog_jlgid_seq'), " +
				m_jobid + ", 'i')"
			);
			if (res)
				res = NULL;
		}
	}
}
