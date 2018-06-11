//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2018, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgAgent.cpp - pgAgent main entry
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

#if !BOOST_OS_WINDOWS
#include <unistd.h>
#endif

std::wstring connectString;
std::wstring backendPid;
long longWait = 30;
long shortWait = 5;
long minLogLevel = LOG_ERROR;

using namespace std;

#define MAXATTEMPTS 10

#if !BOOST_OS_WINDOWS
bool runInForeground = false;
std::wstring logFile = L"";

#else
//pgAgent Initialized
void Initialized();
#endif

int MainRestartLoop(DBconn *serviceConn)
{
	// clean up old jobs

	int rc;

	LogMessage(L"Clearing zombies", LOG_DEBUG);
	rc = serviceConn->ExecuteVoid(L"CREATE TEMP TABLE pga_tmp_zombies(jagpid int4)");

	if (serviceConn->BackendMinimumVersion(9, 2))
	{
		rc = serviceConn->ExecuteVoid(
			L"INSERT INTO pga_tmp_zombies (jagpid) "
			L"SELECT jagpid "
			L"  FROM pgagent.pga_jobagent AG "
			L"  LEFT JOIN pg_stat_activity PA ON jagpid=pid "
			L" WHERE pid IS NULL"
			);
	}
	else
	{
		rc = serviceConn->ExecuteVoid(
			L"INSERT INTO pga_tmp_zombies (jagpid) "
			L"SELECT jagpid "
			L"  FROM pgagent.pga_jobagent AG "
			L"  LEFT JOIN pg_stat_activity PA ON jagpid=procpid "
			L" WHERE procpid IS NULL"
			);
	}

	if (rc > 0)
	{
		// There are orphaned agent entries
		// mark the jobs as aborted
		rc = serviceConn->ExecuteVoid(
			L"UPDATE pgagent.pga_joblog SET jlgstatus='d' WHERE jlgid IN ("
			L"SELECT jlgid "
			L"FROM pga_tmp_zombies z, pgagent.pga_job j, pgagent.pga_joblog l "
			L"WHERE z.jagpid=j.jobagentid AND j.jobid = l.jlgjobid AND l.jlgstatus='r');\n"

			L"UPDATE pgagent.pga_jobsteplog SET jslstatus='d' WHERE jslid IN ( "
			L"SELECT jslid "
			L"FROM pga_tmp_zombies z, pgagent.pga_job j, pgagent.pga_joblog l, pgagent.pga_jobsteplog s "
			L"WHERE z.jagpid=j.jobagentid AND j.jobid = l.jlgjobid AND l.jlgid = s.jsljlgid AND s.jslstatus='r');\n"

			L"UPDATE pgagent.pga_job SET jobagentid=NULL, jobnextrun=NULL "
			L"  WHERE jobagentid IN (SELECT jagpid FROM pga_tmp_zombies);\n"

			L"DELETE FROM pgagent.pga_jobagent "
			L"  WHERE jagpid IN (SELECT jagpid FROM pga_tmp_zombies);\n"
			);
	}

	rc = serviceConn->ExecuteVoid(L"DROP TABLE pga_tmp_zombies");

	std::string host_name = boost::asio::ip::host_name();
	std::wstring hostname(host_name.begin(), host_name.end());

	rc = serviceConn->ExecuteVoid(
		L"INSERT INTO pgagent.pga_jobagent (jagpid, jagstation) SELECT pg_backend_pid(), '" + hostname + L"'");
	if (rc < 0)
		return rc;

	while (1)
	{
		bool foundJobToExecute = false;

		LogMessage(L"Checking for jobs to run", LOG_DEBUG);
		DBresultPtr res = serviceConn->Execute(
			L"SELECT J.jobid "
			L"  FROM pgagent.pga_job J "
			L" WHERE jobenabled "
			L"   AND jobagentid IS NULL "
			L"   AND jobnextrun <= now() "
			L"   AND (jobhostagent = '' OR jobhostagent = '" + hostname + L"')"
			L" ORDER BY jobnextrun");

		if (res)
		{
			while (res->HasData())
			{
				std::wstring jobid = res->GetString(L"jobid");

				boost::thread job_thread = boost::thread(JobThread(jobid));
				job_thread.detach();
				foundJobToExecute = true;
				res->MoveNext();
			}
			res = NULL;

			LogMessage(L"Sleeping...", LOG_DEBUG);
			WaitAWhile();
		}
		else
		{
			LogMessage(L"Failed to query jobs table!", LOG_ERROR);
		}
		if (!foundJobToExecute)
			DBconn::ClearConnections();
	}
	return 0;
}


void MainLoop()
{
	int attemptCount = 1;

	// OK, let's get down to business
	do
	{
		LogMessage(L"Creating primary connection", LOG_DEBUG);
		DBconn *serviceConn = DBconn::InitConnection(connectString);

		if (serviceConn)
		{
			// Basic sanity check, and a chance to get the serviceConn's PID
			LogMessage(L"Database sanity check", LOG_DEBUG);
			DBresultPtr res = serviceConn->Execute(L"SELECT count(*) As count, pg_backend_pid() AS pid FROM pg_class cl JOIN pg_namespace ns ON ns.oid=relnamespace WHERE relname='pga_job' AND nspname='pgagent'");
			if (res)
			{
				std::wstring val = res->GetString(L"count");

				if (val == L"0")
					LogMessage(L"Could not find the table 'pgagent.pga_job'. Have you run pgagent.sql on this database?", LOG_ERROR);

				backendPid = res->GetString(L"pid");

				res = NULL;
			}

			// Check for particular version

			bool hasSchemaVerFunc = false;
			std::wstring sqlCheckSchemaVersion
				= L"SELECT COUNT(*)                                            "\
				L"FROM pg_proc                                               "\
				L"WHERE proname = 'pgagent_schema_version' AND               "\
				L"      pronamespace = (SELECT oid                           "\
				L"                      FROM pg_namespace                    "\
				L"                      WHERE nspname = 'pgagent') AND       "\
				L"      prorettype = (SELECT oid                             "\
				L"                    FROM pg_type                           "\
				L"                    WHERE typname = 'int2') AND            "\
				L"      proargtypes = ''                                     ";

			res = serviceConn->Execute(sqlCheckSchemaVersion);

			if (res)
			{
				if (res->IsValid() && res->GetString(0) == L"1")
					hasSchemaVerFunc = true;
				res = NULL;
			}

			if (!hasSchemaVerFunc)
			{
				LogMessage(L"Couldn't find the function 'pgagent_schema_version' - please run pgagent_upgrade.sql.", LOG_ERROR);
			}

			std::wstring strPgAgentSchemaVer = serviceConn->ExecuteScalar(L"SELECT pgagent.pgagent_schema_version()");
			std::wstring currentPgAgentVersion = (boost::wformat(L"%d") % PGAGENT_VERSION_MAJOR).str();
			if (strPgAgentSchemaVer != currentPgAgentVersion)
			{
				LogMessage((L"Unsupported schema version: " + strPgAgentSchemaVer + L". Version " + currentPgAgentVersion + L" is required - please run pgagent_upgrade.sql."), LOG_ERROR);
			}

#ifdef WIN32
			Initialized();
#endif
			MainRestartLoop(serviceConn);
		}

		LogMessage((boost::wformat(L"Couldn't create the primary connection [Attempt #%d]") % attemptCount).str(), LOG_STARTUP);

		DBconn::ClearConnections(true);

		// Try establishing primary connection upto MAXATTEMPTS times
		if (attemptCount++ >= (int)MAXATTEMPTS)
		{
			LogMessage(L"Stopping pgAgent: Couldn't establish the primary connection with the database server.", LOG_ERROR);
		}
		WaitAWhile();
	} while (1);
}

