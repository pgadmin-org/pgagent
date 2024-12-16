//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// job.h - agent job
//
//////////////////////////////////////////////////////////////////////////


#ifndef JOB_H
#define JOB_H

#include <boost/thread.hpp>

class Job
{
public:
	Job(DBconn *conn, const std::string &jid);
	~Job();

	int Execute();
	bool Runnable()
	{
		return m_status == "r";
	}

protected:
	DBconn      *m_threadConn;
	std::string  m_jobid, m_logid;
	std::string  m_status;
};

class JobThread
{
public:
	JobThread(const std::string &jid);
	~JobThread();
	void operator()();

private:
	std::string  m_jobid;
};

#endif // JOB_H

