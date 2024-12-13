//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgAgent.cpp - pgAgent main entry
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

#if !BOOST_OS_WINDOWS
#include <unistd.h>
#endif

std::string connectString;
std::string backendPid;
long        longWait = 30;
long        shortWait = 5;
long        minLogLevel = LOG_ERROR;

using namespace std;

#define MAXATTEMPTS 10

#if !BOOST_OS_WINDOWS
bool        runInForeground = false;
std::string logFile;

#else
// pgAgent Initialized
void        Initialized();
#endif

int MainRestartLoop(DBconn *serviceConn)
{
	// clean up old jobs

	int rc;

	LogMessage("Clearing zombies", LOG_DEBUG);
	rc = serviceConn->ExecuteVoid("CREATE TEMP TABLE pga_tmp_zombies(jagpid int4)");

	if (serviceConn->BackendMinimumVersion(9, 2))
	{
		rc = serviceConn->ExecuteVoid(
			"INSERT INTO pga_tmp_zombies (jagpid) "
			"SELECT jagpid "
			"  FROM pgagent.pga_jobagent AG "
			"  LEFT JOIN pg_stat_activity PA ON jagpid=pid "
			" WHERE pid IS NULL"
		);
	}
	else
	{
		rc = serviceConn->ExecuteVoid(
			"INSERT INTO pga_tmp_zombies (jagpid) "
			"SELECT jagpid "
			"  FROM pgagent.pga_jobagent AG "
			"  LEFT JOIN pg_stat_activity PA ON jagpid=procpid "
			" WHERE procpid IS NULL"
		);
	}

	if (rc > 0)
	{
		// There are orphaned agent entries
		// mark the jobs as aborted
		rc = serviceConn->ExecuteVoid(
			"UPDATE pgagent.pga_joblog SET jlgstatus='d' WHERE jlgid IN ("
			"SELECT jlgid "
			"FROM pga_tmp_zombies z, pgagent.pga_job j, pgagent.pga_joblog l "
			"WHERE z.jagpid=j.jobagentid AND j.jobid = l.jlgjobid AND l.jlgstatus='r');\n"

			"UPDATE pgagent.pga_jobsteplog SET jslstatus='d' WHERE jslid IN ( "
			"SELECT jslid "
			"FROM pga_tmp_zombies z, pgagent.pga_job j, pgagent.pga_joblog l, pgagent.pga_jobsteplog s "
			"WHERE z.jagpid=j.jobagentid AND j.jobid = l.jlgjobid AND l.jlgid = s.jsljlgid AND s.jslstatus='r');\n"

			"UPDATE pgagent.pga_job SET jobagentid=NULL, jobnextrun=NULL "
			"  WHERE jobagentid IN (SELECT jagpid FROM pga_tmp_zombies);\n"

			"DELETE FROM pgagent.pga_jobagent "
			"  WHERE jagpid IN (SELECT jagpid FROM pga_tmp_zombies);\n"
		);
	}

	rc = serviceConn->ExecuteVoid("DROP TABLE pga_tmp_zombies");

	std::string host_name = boost::asio::ip::host_name();

	rc = serviceConn->ExecuteVoid(
		"INSERT INTO pgagent.pga_jobagent (jagpid, jagstation) SELECT pg_backend_pid(), '" +
		host_name + "'"
	);

	if (rc < 0)
		return rc;

	while (1)
	{
		bool foundJobToExecute = false;

		LogMessage("Checking for jobs to run", LOG_DEBUG);
		DBresultPtr res = serviceConn->Execute(
			"SELECT J.jobid "
			"  FROM pgagent.pga_job J "
			" WHERE jobenabled "
			"   AND jobagentid IS NULL "
			"   AND jobnextrun <= now() "
			"   AND (jobhostagent = '' OR jobhostagent = '" + host_name + "')"
			" ORDER BY jobnextrun"
		);

		if (res)
		{
			while (res->HasData())
			{
				std::string jobid = res->GetString("jobid");

				boost::thread job_thread = boost::thread(JobThread(jobid));
				job_thread.detach();
				foundJobToExecute = true;
				res->MoveNext();
			}
			res = NULL;

			LogMessage("Sleeping...", LOG_DEBUG);
			WaitAWhile();
		}
		else
			LogMessage("Failed to query jobs table!", LOG_ERROR);

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
		LogMessage("Creating primary connection", LOG_DEBUG);
		DBconn *serviceConn = DBconn::InitConnection(connectString);

		if (serviceConn)
		{
			// Basic sanity check, and a chance to get the serviceConn's PID
			LogMessage("Database sanity check", LOG_DEBUG);
			DBresultPtr res = serviceConn->Execute(
				"SELECT count(*) As count, pg_backend_pid() AS pid FROM pg_class cl JOIN pg_namespace ns ON ns.oid=relnamespace WHERE relname='pga_job' AND nspname='pgagent'"
			);

			if (res)
			{
				std::string val = res->GetString("count");

				if (val == "0")
					LogMessage(
						"Could not find the table 'pgagent.pga_job'. Have you run pgagent.sql on this database?",
						LOG_ERROR
					);

				backendPid = res->GetString("pid");

				res = NULL;
			}

			// Check for particular version
			bool hasSchemaVerFunc = false;
			std::string sqlCheckSchemaVersion	=
				"SELECT COUNT(*)                                            " \
				"FROM pg_proc                                               " \
				"WHERE proname = 'pgagent_schema_version' AND               " \
				"      pronamespace = (SELECT oid                           " \
				"                      FROM pg_namespace                    " \
				"                      WHERE nspname = 'pgagent') AND       " \
				"      prorettype = (SELECT oid                             " \
				"                    FROM pg_type                           " \
				"                    WHERE typname = 'int2') AND            " \
				"      proargtypes = ''                                     ";

			res = serviceConn->Execute(sqlCheckSchemaVersion);

			if (res)
			{
				if (res->IsValid() && res->GetString(0) == "1")
					hasSchemaVerFunc = true;
				res = NULL;
			}

			if (!hasSchemaVerFunc)
			{
				LogMessage(
					"Couldn't find the function 'pgagent_schema_version' - please run ALTER EXTENSION \"pgagent\" UPDATE;.",
					LOG_ERROR
				);
			}

			std::string strPgAgentSchemaVer = serviceConn->ExecuteScalar(
				"SELECT pgagent.pgagent_schema_version()"
			);
			std::string currentPgAgentVersion = (boost::format("%d") % PGAGENT_VERSION_MAJOR).str();

			if (strPgAgentSchemaVer != currentPgAgentVersion)
			{
				LogMessage(
					"Unsupported schema version: " + strPgAgentSchemaVer +
					". Version " + currentPgAgentVersion +
					" is required - please run ALTER EXTENSION \"pgagent\" UPDATE;.",
					LOG_ERROR
				);
			}

#ifdef WIN32
			Initialized();
#endif
			MainRestartLoop(serviceConn);
		}

		LogMessage((boost::format(
			"Couldn't create the primary connection [Attempt #%d]") % attemptCount
		).str(), LOG_STARTUP);

		DBconn::ClearConnections(true);

		// Try establishing primary connection upto MAXATTEMPTS times
		if (attemptCount++ >= (int)MAXATTEMPTS)
		{
			LogMessage(
				"Stopping pgAgent: Couldn't establish the primary connection with the database server.",
				LOG_ERROR
			);
		}
		WaitAWhile();
	} while (1);
}
