//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2020, The pgAdmin Development Team
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
	Job(DBconn *conn, const std::wstring &jid);
	~Job();

	int Execute();
	bool Runnable()
	{
		return status == L"r";
	}

protected:
	DBconn *threadConn;
	std::wstring jobid, logid;
	std::wstring status;
};

class JobThread
{
public:
	JobThread(const std::wstring &jid);
	~JobThread();
	void operator()();

private:
	std::wstring  m_jobid;
};

#endif // JOB_H

