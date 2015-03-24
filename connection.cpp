//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2014, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// connection.cpp - database connection
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"

#include <wx/regex.h>
#include <wx/tokenzr.h>

DBconn *DBconn::primaryConn = NULL;
wxString DBconn::basicConnectString;
static wxMutex s_PoolLock;


DBconn::DBconn(const wxString &connectString, const wxString &db)
{
	inUse = false;
	next = 0;
	prev = 0;
	majorVersion = 0;
	minorVersion = 0;
	dbname = db;
	connStr = connectString;

	if (connectString.IsEmpty())
	{
		// This is a sql call to a local database.
		// No connection string found. Use basicConnectString.
		Connect(basicConnectString  + wxT(" dbname=") + dbname);
	}
	else
	{
		Connect(connectString);
	}
}


bool DBconn::Connect(const wxString &connectString)
{
	LogMessage(wxString::Format(_("Creating DB connection: %s"), connectString.c_str()), LOG_DEBUG);
	wxCharBuffer cstrUTF = connectString.mb_str(wxConvUTF8);
	conn = PQconnectdb(cstrUTF);
	if (PQstatus(conn) != CONNECTION_OK)
	{
		lastError = wxString::FromAscii(PQerrorMessage(conn));
		PQfinish(conn);
		conn = 0;
	}
	return IsValid();
}


DBconn::~DBconn()
{
	// clear a single connection
	if (conn)
	{
		PQfinish(conn);
		conn = 0;
	}
}


wxString DBconn::qtDbString(const wxString &value)
{
	wxString result = value;

	result.Replace(wxT("\\"), wxT("\\\\"));
	result.Replace(wxT("'"), wxT("''"));
	result.Append(wxT("'"));

	if (BackendMinimumVersion(8, 1))
	{
		if (result.Contains(wxT("\\")))
			result.Prepend(wxT("E'"));
		else
			result.Prepend(wxT("'"));
	}
	else
		result.Prepend(wxT("'"));

	return result;
}


bool DBconn::BackendMinimumVersion(int major, int minor)
{
	if (!majorVersion)
	{
		wxString version = ExecuteScalar(wxT("SELECT version();")) ;
		sscanf(version.ToAscii(), "%*s %d.%d", &majorVersion, &minorVersion);
	}
	return majorVersion > major || (majorVersion == major && minorVersion >= minor);
}


DBconn *DBconn::InitConnection(const wxString &connectString)
{
	wxMutexLocker lock(s_PoolLock);

	basicConnectString = connectString;
	wxString dbname;

	connInfo cnInfo = connInfo::getConnectionInfo(connectString);
	if (cnInfo.isValid)
	{
		dbname = cnInfo.dbname;
		basicConnectString = cnInfo.getConnectionString();
		primaryConn = new DBconn(cnInfo.getConnectionString(), dbname);

		if (!primaryConn)
			LogMessage(_("Failed to create primary connection!"), LOG_ERROR);
		primaryConn->dbname = dbname;
		primaryConn->inUse = true;
	}
	else
	{
		primaryConn = NULL;
		LogMessage(wxT("Primary connection string is not valid!"), LOG_ERROR);
	}

	return primaryConn;
}


DBconn *DBconn::Get(const wxString &connStr, const wxString &db)
{
	if (db.IsEmpty() && connStr.IsEmpty())
	{
		LogMessage(_("Cannot allocate connection - no database or connection string specified!"), LOG_WARNING);
		return NULL;
	}

	wxMutexLocker lock(s_PoolLock);

	DBconn *thisConn = primaryConn, *lastConn;

	// find an existing connection
	do
	{
		if (thisConn && ((!db.IsEmpty() && db == thisConn->dbname && connStr.IsEmpty()) || (!connStr.IsEmpty() && connStr == thisConn->connStr)) && !thisConn->inUse)
		{
			LogMessage(wxString::Format(_("Allocating existing connection to database %s"), thisConn->dbname.c_str()), LOG_DEBUG);
			thisConn->inUse = true;
			return thisConn;
		}

		lastConn = thisConn;
		thisConn = thisConn->next;

	}
	while (thisConn != 0);


	// No suitable connection was found, so create a new one.
	DBconn *newConn = NULL;
	newConn = new DBconn(connStr, db);

	if (newConn->conn)
	{
		LogMessage(wxString::Format(_("Allocating new connection to database %s"), newConn->dbname.c_str()), LOG_DEBUG);
		newConn->inUse = true;
		newConn->prev = lastConn;
		lastConn->next = newConn;
	}
	else
	{
		wxString warnMsg;
		if (connStr.IsEmpty())
			warnMsg = wxString::Format(_("Failed to create new connection to database '%s':'%s'"),
			                           db.c_str(), newConn->GetLastError().c_str());
		else
			warnMsg = wxString::Format(_("Failed to create new connection for connection string '%s':%s"),
			                           connStr.c_str(), newConn->GetLastError().c_str());
		LogMessage(warnMsg, LOG_STARTUP);
		return NULL;
	}

	return newConn;
}


void DBconn::Return()
{
	wxMutexLocker lock(s_PoolLock);

	// Cleanup
	this->ExecuteVoid(wxT("RESET ALL"));
	this->lastError.Empty();

	LogMessage(wxString::Format(_("Returning connection to database %s"), dbname.c_str()), LOG_DEBUG);
	inUse = false;
}


void DBconn::ClearConnections(bool all)
{
	wxMutexLocker lock(s_PoolLock);

	if (all)
		LogMessage(_("Clearing all connections"), LOG_DEBUG);
	else
		LogMessage(_("Clearing inactive connections"), LOG_DEBUG);

	DBconn *thisConn = primaryConn, *deleteConn;
	int total = 0, free = 0, deleted = 0;

	if (thisConn)
	{

		total++;

		// Find the last connection
		while (thisConn->next != 0)
		{
			total++;

			if (!thisConn->inUse)
				free++;

			thisConn = thisConn->next;
		}
		if (!thisConn->inUse)
			free++;

		// Delete connections as required
		// If a connection is not in use, delete it, and reset the next and previous
		// pointers appropriately. If it is in use, don't touch it.
		while (thisConn->prev != 0)
		{
			if ((!thisConn->inUse) || all)
			{
				deleteConn = thisConn;
				thisConn = deleteConn->prev;
				thisConn->next = deleteConn->next;
				if (deleteConn->next)
					deleteConn->next->prev = deleteConn->prev;
				delete deleteConn;
				deleted++;
			}
			else
			{
				thisConn = thisConn->prev;
			}
		}

		if (all)
		{
			delete thisConn;
			deleted++;
		}

		wxString tmp;
		tmp.Printf(_("Connection stats: total - %d, free - %d, deleted - %d"), total, free, deleted);
		LogMessage(tmp, LOG_DEBUG);

	}
	else
		LogMessage(_("No connections found!"), LOG_DEBUG);

}


DBresult *DBconn::Execute(const wxString &query)
{
	DBresult *res = new DBresult(this, query);
	if (!res->IsValid())
	{
		// error handling here

		delete res;
		return 0;
	}
	return res;
}


wxString DBconn::ExecuteScalar(const wxString &query)
{
	int rows = -1;
	DBresult *res = Execute(query);
	wxString data;
	if (res)
	{
		data = res->GetString(0);
		rows = res->RowsAffected();
		delete res;
		return data;
	}
	return wxEmptyString;
}


int DBconn::ExecuteVoid(const wxString &query)
{
	int rows = -1;
	DBresult *res = Execute(query);
	if (res)
	{
		rows = res->RowsAffected();
		delete res;
	}
	return rows;
}


wxString DBconn::GetLastError()
{
	size_t len = lastError.length();

	// Return the last error message, minus any trailing line ends
	if (len >= 2 && lastError.substr(len - 2, 2) == wxT("\r\n")) // DOS
		return lastError.substr(0, len - 2);
	else if (len >= 1 && lastError.substr(len - 1, 1) == wxT("\n")) // Unix
		return lastError.substr(0, len - 1);
	else if (len >= 1 && lastError.substr(len - 1, 1) == wxT("\r")) // Mac
		return lastError.substr(0, len - 1);
	else
		return lastError;
}

///////////////////////////////////////////////////////7

DBresult::DBresult(DBconn *conn, const wxString &query)
{
	wxCharBuffer cstrUTF = query.mb_str(wxConvUTF8);
	result = PQexec(conn->conn, cstrUTF);
	currentRow = 0;
	maxRows = 0;

	if (result)
	{
		int rc = PQresultStatus(result);
		conn->SetLastResult(rc);
		if (rc == PGRES_TUPLES_OK)
			maxRows = PQntuples(result);
		else if (rc != PGRES_COMMAND_OK)
		{
			conn->lastError = wxString::FromAscii(PQerrorMessage(conn->conn));
			LogMessage(wxT("Query error: ") + conn->lastError, LOG_WARNING);
			PQclear(result);
			result = 0;
		}
	}
	else
		conn->lastError = wxString::FromAscii(PQerrorMessage(conn->conn));

}


DBresult::~DBresult()
{
	if (result)
		PQclear(result);
}


wxString DBresult::GetString(int col) const
{
	wxString str;

	if (result && currentRow < maxRows && col >= 0)
	{
		str = wxString::FromAscii(PQgetvalue(result, currentRow, col));
	}
	return str;
}


wxString DBresult::GetString(const wxString &colname) const
{
	wxCharBuffer cstrUTF = colname.mb_str(wxConvUTF8);
	int col = PQfnumber(result, cstrUTF);
	if (col < 0)
	{
		// fatal: not found
		return wxT("");
	}
	return GetString(col);
}

///////////////////////////////////////////////////////7

bool connInfo::IsValidIP()
{
	if (host.IsEmpty())
		return false;

	// check for IPv4 format
	wxStringTokenizer tkip4(host, wxT("."));
	int count = 0;

	while (tkip4.HasMoreTokens())
	{
		long val = 0;
		if (!tkip4.GetNextToken().ToLong(&val))
			break;
		if (count == 0 || count == 3)
			if (val > 0 && val < 255)
				count++;
			else
				break;
		else if (val >= 0 && val < 255)
			count++;
		else
			break;
	}

	if (count == 4)
		return true;

	// check for IPv6 format
	wxStringTokenizer tkip6(host, wxT(":"));
	count = 0;

	while (tkip6.HasMoreTokens())
	{
		unsigned long val = 0;
		wxString strVal = tkip6.GetNextToken();
		if (strVal.Length() > 4 || !strVal.ToULong(&val, 16))
			return false;
		count++;
	}
	if (count <= 8)
		return true;

	// TODO:: We're not supporting mix mode (x:x:x:x:x:x:d.d.d.d)
	//        i.e. ::ffff:12.34.56.78
	return false;
}


wxString connInfo::getConnectionString()
{
	wxString connStr;

	// Check if it has valid connection info
	if (!isValid)
		return connStr;

	// User
	connStr = wxT("user=") + user;

	// Port
	if (port != 0)
	{
		wxString portStr;
		portStr.Printf(wxT("%ld"), port);
		connStr += wxT(" port=") + portStr;
	}

	// host or hostaddr
	if (!host.IsEmpty())
	{
		if (IsValidIP())
			connStr += wxT(" hostaddr=") + host;
		else
			connStr += wxT(" host=") + host;
	}

	// connection timeout
	if (connection_timeout != 0)
	{
		wxString val;
		val.Printf(wxT("%ld"), connection_timeout);
		connStr += wxT(" connection_timeout=") + val;
	}

	// password
	if (!password.IsEmpty())
		connStr += wxT(" password=") + password;

	if (!dbname.IsEmpty())
		connStr += wxT(" dbname=") + dbname;

	LogMessage(wxString::Format(_("Connection Information:")), LOG_DEBUG);
	LogMessage(wxString::Format(_("     user         : %s"), user.c_str()), LOG_DEBUG);
	LogMessage(wxString::Format(_("     port         : %ld"), port), LOG_DEBUG);
	LogMessage(wxString::Format(_("     host         : %s"), host.c_str()), LOG_DEBUG);
	LogMessage(wxString::Format(_("     dbname       : %s"), dbname.c_str()), LOG_DEBUG);
	LogMessage(wxString::Format(_("     password     : %s"), password.c_str()), LOG_DEBUG);
	LogMessage(wxString::Format(_("     conn timeout : %ld"), connection_timeout), LOG_DEBUG);

	return connStr;
}


connInfo connInfo::getConnectionInfo(wxString connStr)
{
	connInfo cnInfo;

	wxRegEx propertyExp;

	// Remove the white-space(s) to match the following format
	// i.e. prop=value
	bool res = propertyExp.Compile(wxT("(([ ]*[\t]*)+)="));

	propertyExp.ReplaceAll(&connStr, wxT("="));

	res = propertyExp.Compile(wxT("=(([ ]*[\t]*)+)"));
	propertyExp.ReplaceAll(&connStr, wxT("="));

	// Seperate all the prop=value patterns
	wxArrayString tokens = wxStringTokenize(connStr, wxT("\t \n\r"));

	unsigned int index = 0;
	while (index < tokens.Count())
	{
		wxString prop, value;

		wxArrayString pairs = wxStringTokenize(tokens[index++], wxT("="));

		if (pairs.GetCount() != 2)
			return cnInfo;

		prop = pairs[0];
		value = pairs[1];

		if (prop.CmpNoCase(wxT("user")) == 0)
			cnInfo.user = value;
		else if (prop.CmpNoCase(wxT("host")) == 0 || prop.CmpNoCase(wxT("hostAddr")) == 0)
			cnInfo.host = value;
		else if (prop.CmpNoCase(wxT("port")) == 0)
		{
			if (!value.ToULong(&cnInfo.port))
				// port must be an unsigned integer
				return cnInfo;
		}
		else if (prop.CmpNoCase(wxT("password")) == 0)
			cnInfo.password = value;
		else if (prop.CmpNoCase(wxT("connection_timeout")) == 0)
		{
			if (!value.ToULong(&cnInfo.connection_timeout))
				// connection timeout must be an unsigned interger
				return cnInfo;
		}
		else if (prop.CmpNoCase(wxT("dbname")) == 0)
			cnInfo.dbname = value;
		else
			// Not valid property found
			return cnInfo;
	}

	// If user, dbname & host all are blank than we will consider this an invalid connection string
	if (cnInfo.user.IsEmpty() && cnInfo.dbname.IsEmpty() && cnInfo.host.IsEmpty())
		cnInfo.isValid = false;
	else
		cnInfo.isValid = true;

	return cnInfo;
}

