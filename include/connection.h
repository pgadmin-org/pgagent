//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2016, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// connection.h - database connection
//
//////////////////////////////////////////////////////////////////////////


#ifndef CONNECTION_H
#define CONNECTION_H

#include <libpq-fe.h>

class DBresult;


class DBconn
{
protected:
	DBconn(const std::wstring &, const std::wstring &);
	~DBconn();

public:
	std::wstring qtDbString(const std::wstring &value);

	bool BackendMinimumVersion(int major, int minor);

	static DBconn *Get(const std::wstring &connStr, const std::wstring &db);
	static DBconn *InitConnection(const std::wstring &connectString);

	static void ClearConnections(bool allIncludingPrimary = false);
	static void SetBasicConnectString(const std::wstring &bcs)
	{
		ms_basicConnectString = bcs;
	}
	static const std::wstring &GetBasicConnectString()
	{
		return ms_basicConnectString;
	}

	std::wstring GetLastError();
	std::wstring GetDBname()
	{
		return dbname;
	}

	bool IsValid()
	{
		return conn != 0;
	}

	bool LastCommandOk()
	{
		return IsCommandOk((ExecStatusType)lastResult);
	}

	bool IsCommandOk(ExecStatusType ret)
	{
		switch (ret)
		{
			case PGRES_COMMAND_OK:
			case PGRES_TUPLES_OK:
			case PGRES_COPY_OUT:
			case PGRES_COPY_IN:
#if (PG_VERSION_NUM >= 90100)
			case PGRES_COPY_BOTH:
#endif
				return true;
			default:
				return false;
		};
	}

	void SetLastResult(int res)
	{
		lastResult = res;
	}

	int GetLastResult()
	{
		return lastResult;
	}

	DBresult *Execute(const std::wstring &query);
	std::wstring ExecuteScalar(const std::wstring &query);
	int ExecuteVoid(const std::wstring &query);
	void Return();

private:
	bool Connect(const std::wstring &connectString);

	int minorVersion, majorVersion;

protected:
	static std::wstring ms_basicConnectString;
	static DBconn *ms_primaryConn;

	std::wstring dbname, lastError, connStr;
	PGconn *conn;
	DBconn *next, *prev;
	bool inUse;
	int lastResult;
	friend class DBresult;

};


class DBresult
{
protected:
	DBresult(DBconn *conn, const std::wstring &query);

public:
	~DBresult();

	std::wstring GetString(int col) const;
	std::wstring GetString(const std::wstring &colname) const;

	bool IsValid() const
	{
		return result != NULL;
	}
	bool HasData() const
	{
		return currentRow < maxRows;
	}
	void MoveNext()
	{
		if (currentRow < maxRows) currentRow++;
	}

	long RowsAffected() const
	{
		return atol(PQcmdTuples(result));
	}

protected:
	PGresult *result;
	int currentRow, maxRows;

	friend class DBconn;
};


class DBresultPtr
{
public:
	DBresultPtr(DBresult* in_ptr)
		: m_ptr(in_ptr)
	{}
	~DBresultPtr()
	{
		if (m_ptr) {
			delete m_ptr;
			m_ptr = NULL;
		}
	}
	DBresultPtr& operator=(DBresult *other)
	{
		if (m_ptr) {
			delete m_ptr;
		}
		m_ptr = other;
		return *this;
	}
	const DBresult& operator*() const
	{
		return (*(const DBresult *)m_ptr);
	}
	const DBresult* operator->() const
	{
		return (const DBresult*)(m_ptr);
	}
	DBresult& operator*()
	{
		return (*(DBresult *)m_ptr);
	}
	DBresult* operator->()
	{
		return (DBresult *)m_ptr;
	}
	operator void*() const
	{
		return (DBresult *)m_ptr;
	}
	operator bool() const { return (m_ptr != NULL); }

protected:
	DBresult* m_ptr;
};


class connInfo
{
public:
	connInfo()
	{
		isValid = false;
		connection_timeout = 0;
		port = 0;
	}

private:
	std::wstring  user;
	unsigned long port;
	std::wstring  host;
	std::wstring  dbname;
	unsigned long connection_timeout;
	std::wstring  password;
	bool          isValid;

	std::wstring getConnectionString();
	static connInfo getConnectionInfo(const std::wstring &connStr);

protected:
	bool IsValidIP();
	friend class DBconn;
};

#endif // CONNECTION_H

