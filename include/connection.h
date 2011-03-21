//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2011, The pgAdmin Development Team
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
	DBconn(const wxString &, const wxString &);
	~DBconn();

public:
	wxString qtDbString(const wxString &value);

	bool BackendMinimumVersion(int major, int minor);

	static DBconn *Get(const wxString &connStr, const wxString &db);
	static DBconn *InitConnection(const wxString &connectString);

	static void ClearConnections(bool allIncludingPrimary = false);
	static void SetBasicConnectString(const wxString &bcs)
	{
		basicConnectString = bcs;
	}
	static const wxString &GetBasicConnectString()
	{
		return basicConnectString;
	}

	wxString GetLastError();
	wxString GetDBname()
	{
		return dbname;
	}
	bool IsValid()
	{
		return conn != 0;
	}

	DBresult *Execute(const wxString &query);
	wxString ExecuteScalar(const wxString &query);
	int ExecuteVoid(const wxString &query);
	void Return();

private:
	bool Connect(const wxString &connectString);

	int minorVersion, majorVersion;

protected:
	static wxString basicConnectString;
	static DBconn *primaryConn;

	wxString dbname, lastError, connStr;
	PGconn *conn;
	DBconn *next, *prev;
	bool inUse;

	friend class DBresult;

};


class DBresult
{
protected:
	DBresult(DBconn *conn, const wxString &query);

public:
	~DBresult();

	wxString GetString(int col) const;
	wxString GetString(const wxString &colname) const;

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
	wxString      user;
	unsigned long port;
	wxString      host;
	wxString      dbname;
	unsigned long connection_timeout;
	wxString      password;
	bool          isValid;

	wxString getConnectionString();
	static connInfo getConnectionInfo(wxString connStr);

protected:
	bool IsValidIP();
	friend class DBconn;
};

#endif // CONNECTION_H

