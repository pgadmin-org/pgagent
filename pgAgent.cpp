//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2012, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgAgent.cpp - pgAgent main entry
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

#ifndef __WXMSW__
#include <unistd.h>
#endif

wxString connectString;
wxString serviceDBname;
wxString backendPid;
long longWait = 30;
long shortWait = 5;
long minLogLevel = LOG_ERROR;

#define MAXATTEMPTS 10

#ifndef __WXMSW__
bool runInForeground = false;
wxString logFile = wxEmptyString;
wxString pidFile = wxEmptyString;
#else
//pgAgent Initialized
void Initialized();
#endif

int MainRestartLoop(DBconn *serviceConn)
{
	// clean up old jobs

	int rc;

	LogMessage(_("Clearing zombies"), LOG_DEBUG);
	rc = serviceConn->ExecuteVoid(wxT("CREATE TEMP TABLE pga_tmp_zombies(jagpid int4)"));

	if (serviceConn->BackendMinimumVersion(9, 2))
	{
		rc = serviceConn->ExecuteVoid(
		         wxT("INSERT INTO pga_tmp_zombies (jagpid) ")
		         wxT("SELECT jagpid ")
		         wxT("  FROM pgagent.pga_jobagent AG ")
		         wxT("  LEFT JOIN pg_stat_activity PA ON jagpid=pid ")
		         wxT(" WHERE pid IS NULL")
		     );
	}
	else
	{
		rc = serviceConn->ExecuteVoid(
		         wxT("INSERT INTO pga_tmp_zombies (jagpid) ")
		         wxT("SELECT jagpid ")
		         wxT("  FROM pgagent.pga_jobagent AG ")
		         wxT("  LEFT JOIN pg_stat_activity PA ON jagpid=procpid ")
		         wxT(" WHERE procpid IS NULL")
		     );
	}

	if (rc > 0)
	{
		// There are orphaned agent entries
		// mark the jobs as aborted
		rc = serviceConn->ExecuteVoid(
		         wxT("UPDATE pgagent.pga_joblog SET jlgstatus='d' WHERE jlgid IN (")
		         wxT("SELECT jlgid ")
		         wxT("FROM pga_tmp_zombies z, pgagent.pga_job j, pgagent.pga_joblog l ")
		         wxT("WHERE z.jagpid=j.jobagentid AND j.jobid = l.jlgjobid AND l.jlgstatus='r');\n")

		         wxT("UPDATE pgagent.pga_jobsteplog SET jslstatus='d' WHERE jslid IN ( ")
		         wxT("SELECT jslid ")
		         wxT("FROM pga_tmp_zombies z, pgagent.pga_job j, pgagent.pga_joblog l, pgagent.pga_jobsteplog s ")
		         wxT("WHERE z.jagpid=j.jobagentid AND j.jobid = l.jlgjobid AND l.jlgid = s.jsljlgid AND s.jslstatus='r');\n")

		         wxT("UPDATE pgagent.pga_job SET jobagentid=NULL, jobnextrun=NULL ")
		         wxT("  WHERE jobagentid IN (SELECT jagpid FROM pga_tmp_zombies);\n")

		         wxT("DELETE FROM pgagent.pga_jobagent ")
		         wxT("  WHERE jagpid IN (SELECT jagpid FROM pga_tmp_zombies);\n")
		     );
	}

	rc = serviceConn->ExecuteVoid(wxT("DROP TABLE pga_tmp_zombies"));

	wxString hostname = wxGetFullHostName();

	rc = serviceConn->ExecuteVoid(
	         wxT("INSERT INTO pgagent.pga_jobagent (jagpid, jagstation) SELECT pg_backend_pid(), '") + hostname + wxT("'"));
	if (rc < 0)
		return rc;

	while (1)
	{
		bool foundJobToExecute = false;

		LogMessage(_("Checking for jobs to run"), LOG_DEBUG);
		DBresult *res = serviceConn->Execute(
		                    wxT("SELECT J.jobid ")
		                    wxT("  FROM pgagent.pga_job J ")
		                    wxT(" WHERE jobenabled ")
		                    wxT("   AND jobagentid IS NULL ")
		                    wxT("   AND jobnextrun <= now() ")
		                    wxT("   AND (jobhostagent = '' OR jobhostagent = '") + hostname + wxT("')")
		                    wxT(" ORDER BY jobnextrun"));

		if (res)
		{
			while(res->HasData())
			{
				wxString jobid = res->GetString(wxT("jobid"));

				JobThread *jt = new JobThread(jobid);

				if (jt->Runnable())
				{
					jt->Create();
					jt->Run();
					foundJobToExecute = true;
				}
				else
				{
					// Failed to launch the thread. Insert an entry with
					// "internal error" status in the joblog table, to leave
					// a trace of fact that we tried to launch the job.
					DBresult *res = serviceConn->Execute(
						wxT("INSERT INTO pgagent.pga_joblog(jlgid, jlgjobid, jlgstatus) ")
						wxT("VALUES (nextval('pgagent.pga_joblog_jlgid_seq'), ") + jobid + wxT(", 'i')"));
					if (res)
						delete res;

					// A thread object that's started will destroy itself when
					// it's finished, but one that never starts we'll have to
					// destory ourselves.
					delete jt;
				}
				res->MoveNext();
			}

			delete res;
			LogMessage(_("Sleeping..."), LOG_DEBUG);
			WaitAWhile();
		}
		else
		{
			LogMessage(_("Failed to query jobs table!"), LOG_ERROR);
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
		LogMessage(_("Creating primary connection"), LOG_DEBUG);
		DBconn *serviceConn = DBconn::InitConnection(connectString);

		if (serviceConn && serviceConn->IsValid())
		{
			serviceDBname = serviceConn->GetDBname();

			// Basic sanity check, and a chance to get the serviceConn's PID
			LogMessage(_("Database sanity check"), LOG_DEBUG);
			DBresult *res = serviceConn->Execute(wxT("SELECT count(*) As count, pg_backend_pid() AS pid FROM pg_class cl JOIN pg_namespace ns ON ns.oid=relnamespace WHERE relname='pga_job' AND nspname='pgagent'"));
			if (res)
			{
				wxString val = res->GetString(wxT("count"));

				if (val == wxT("0"))
					LogMessage(_("Could not find the table 'pgagent.pga_job'. Have you run pgagent.sql on this database?"), LOG_ERROR);

				backendPid = res->GetString(wxT("pid"));

				delete res;
				res = NULL;
			}

			// Check for particular version

			bool hasSchemaVerFunc = false;
			wxString sqlCheckSchemaVersion
			= wxT("SELECT COUNT(*)                                            ")\
			  wxT("FROM pg_proc                                               ")\
			  wxT("WHERE proname = 'pgagent_schema_version' AND               ")\
			  wxT("      pronamespace = (SELECT oid                           ")\
			  wxT("                      FROM pg_namespace                    ")\
			  wxT("                      WHERE nspname = 'pgagent') AND       ")\
			  wxT("      prorettype = (SELECT oid                             ")\
			  wxT("                    FROM pg_type                           ")\
			  wxT("                    WHERE typname = 'int2') AND            ")\
			  wxT("      proargtypes = ''                                     ");

			res = serviceConn->Execute(sqlCheckSchemaVersion);

			if (res)
			{
				if (res->IsValid() && res->GetString(0) == wxT("1"))
					hasSchemaVerFunc = true;
				delete res;
				res = NULL;
			}

			if (!hasSchemaVerFunc)
			{
				LogMessage(_("Couldn't find the function 'pgagent_schema_version' - please run pgagent_upgrade.sql."), LOG_ERROR);
			}

			wxString strPgAgentSchemaVer = serviceConn->ExecuteScalar(wxT("SELECT pgagent.pgagent_schema_version()"));
			wxString currentPgAgentVersion;
			currentPgAgentVersion.Printf(_("%d"), PGAGENT_VERSION_MAJOR);
			if (strPgAgentSchemaVer != currentPgAgentVersion)
			{
				wxString strSchemaVerMisMatch;
				strSchemaVerMisMatch.Printf(_("Unsupported schema version: %s. Version %s is required - please run pgagent_upgrade.sql."), strPgAgentSchemaVer.c_str(), currentPgAgentVersion.c_str());
				LogMessage(strSchemaVerMisMatch, LOG_ERROR);
			}

#ifdef WIN32
			Initialized();
#endif
			MainRestartLoop(serviceConn);
		}

		LogMessage(wxString::Format(_("Couldn't create the primary connection (attempt %d): %s"), attemptCount, serviceConn->GetLastError().c_str()), LOG_STARTUP);
		DBconn::ClearConnections(true);

		// Try establishing primary connection upto MAXATTEMPTS times
		if (attemptCount++ >= (int)MAXATTEMPTS)
		{
			LogMessage(wxString::Format(_("Stopping pgAgent: Couldn't establish the primary connection with the database server.")), LOG_ERROR);
		}
		WaitAWhile();
	}
	while (1);
}

