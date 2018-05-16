//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2016, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// connection.cpp - database connection
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"
#include <string>

namespace ip = boost::asio::ip;

DBconn *DBconn::ms_primaryConn = NULL;
std::wstring DBconn::ms_basicConnectString;
static boost::mutex s_poolLock;

class MutexLocker
{
public:
	MutexLocker(boost::mutex *lock): m_lock(lock)
	{
		if (m_lock)
			m_lock->lock();
	}

	~MutexLocker()
	{
		if (m_lock)
			m_lock->unlock();
	}

	// When the exit(code) is being called, static object is being released
	// earlier. Hence - it is a good idea to set the current mutex object to
	// NULL to avoid ASSERTION in debug mode (specifically on OSX).
	MutexLocker& operator =(boost::mutex *lock)
	{
		if (m_lock)
			m_lock->unlock();
		m_lock = lock;

		if (m_lock)
			m_lock->lock();

		return *this;
	}

private:
	boost::mutex *m_lock;
	bool is_locked;
};

DBconn::DBconn(const std::wstring &connectString, const std::wstring &db)
{
	inUse = false;
	next = 0;
	prev = 0;
	majorVersion = 0;
	minorVersion = 0;
	dbname = db;
	connStr = connectString;

	if (connectString.empty())
	{
		// This is a sql call to a local database.
		// No connection string found. Use basicConnectString.
		Connect(ms_basicConnectString + L" dbname=" + dbname);
	}
	else
	{
		Connect(connectString);
	}
}


bool DBconn::Connect(const std::wstring &connectString)
{
	LogMessage((L"Creating DB connection: " + connectString), LOG_DEBUG);
	char *cstrUTF = WStringToChar(connectString);
	if (cstrUTF != NULL)
	{
		conn = PQconnectdb(cstrUTF);
		if (PQstatus(conn) != CONNECTION_OK)
		{
			lastError = CharToWString((const char *)PQerrorMessage(conn));
			PQfinish(conn);
			conn = 0;
		}

		delete [] cstrUTF;
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


std::wstring DBconn::qtDbString(const std::wstring &value)
{
	std::wstring result = value;

	boost::replace_all(result, L"\\", L"\\\\");
	boost::replace_all(result, L"'", L"''");
	result.append(L"'");

	if (BackendMinimumVersion(8, 1))
	{
		if (result.find(L"\\") != std::wstring::npos)
			result.replace(0, 0, L"E'");
		else
			result.replace(0, 0, L"'");
	}
	else
		result.replace(0, 0, L"'");

	return result;
}

bool DBconn::BackendMinimumVersion(int major, int minor)
{
	char *ver = NULL;
	if (!majorVersion)
	{
		std::wstring version = ExecuteScalar(L"SELECT version();");
		ver = WStringToChar(version);
		if (ver != NULL)
			sscanf(ver, "%*s %d.%d", &majorVersion, &minorVersion);
	}

	if (ver != NULL)
		delete []ver;

	return majorVersion > major || (majorVersion == major && minorVersion >= minor);
}


DBconn *DBconn::InitConnection(const std::wstring &connectString)
{
	MutexLocker locker(&s_poolLock);

	ms_basicConnectString = connectString;
	std::wstring dbname;

	connInfo cnInfo = connInfo::getConnectionInfo(connectString);
	if (cnInfo.isValid)
	{
		dbname = cnInfo.dbname;
		ms_basicConnectString = cnInfo.getConnectionString();
		ms_primaryConn = new DBconn(cnInfo.getConnectionString(), dbname);

		if (!ms_primaryConn)
		{
			// Unlock the mutex before logging error.
			locker = (boost::mutex *)NULL;
			LogMessage(L"Failed to create primary connection!", LOG_ERROR);
		}
		if (!ms_primaryConn->IsValid())
		{
			std::wstring error = ms_primaryConn->GetLastError();
			delete ms_primaryConn;
			ms_primaryConn = NULL;
			// Unlock the mutex before logging error.
			locker = (boost::mutex *)NULL;
			LogMessage(
				L"Failed to create primary connection: " + error, LOG_ERROR
				);
		}
		ms_primaryConn->dbname = dbname;
		ms_primaryConn->inUse = true;
	}
	else
	{
		ms_primaryConn = NULL;
		// Unlock the mutex before logging error.
		locker = (boost::mutex *)NULL;
		LogMessage(L"Primary connection string is not valid!", LOG_ERROR);
	}

	return ms_primaryConn;
}


DBconn *DBconn::Get(const std::wstring &connStr, const std::wstring &db)
{
	if (db.empty() && connStr.empty())
	{
		LogMessage(L"Cannot allocate connection - no database or connection string specified!", LOG_WARNING);
		return NULL;
	}
	MutexLocker locker(&s_poolLock);

	DBconn *thisConn = ms_primaryConn, *lastConn;

	// find an existing connection
	do
	{
		if (thisConn && ((!db.empty() && db == thisConn->dbname && connStr.empty()) || (!connStr.empty() && connStr == thisConn->connStr)) && !thisConn->inUse)
		{
			LogMessage((L"Allocating existing connection to database " + thisConn->dbname), LOG_DEBUG);
			thisConn->inUse = true;

			return thisConn;
		}

		lastConn = thisConn;
		thisConn = thisConn->next;

	} while (thisConn != 0);


	// No suitable connection was found, so create a new one.
	DBconn *newConn = NULL;
	newConn = new DBconn(connStr, db);

	if (newConn && newConn->conn)
	{
		LogMessage((L"Allocating new connection to database " + newConn->dbname), LOG_DEBUG);
		newConn->inUse = true;
		newConn->prev = lastConn;
		lastConn->next = newConn;
	}
	else
	{
		std::wstring warnMsg;
		if (connStr.empty())
			warnMsg = (L"Failed to create new connection to database '" + db + L"':'" + newConn->GetLastError() + L"'");
		else
			warnMsg = (L"Failed to create new connection for connection string '" + connStr + L"':'" + newConn->GetLastError() + L"'");
		LogMessage(warnMsg, LOG_STARTUP);

		return NULL;
	}

	return newConn;
}


void DBconn::Return()
{
	MutexLocker locker(&s_poolLock);

	// Cleanup
	this->ExecuteVoid(L"RESET ALL");
	this->lastError.empty();

	LogMessage((L"Returning connection to database " + dbname), LOG_DEBUG);
	inUse = false;
}


void DBconn::ClearConnections(bool all)
{
	MutexLocker locker(&s_poolLock);

	if (all)
		LogMessage(L"Clearing all connections", LOG_DEBUG);
	else
		LogMessage(L"Clearing inactive connections", LOG_DEBUG);

	DBconn *thisConn = ms_primaryConn, *deleteConn;
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
			ms_primaryConn = NULL;
			deleted++;
		}

		LogMessage((boost::wformat(L"Connection stats: total - %d, free - %d, deleted - %d") % total % free % deleted).str(), LOG_DEBUG);

	}
	else
		LogMessage(L"No connections found!", LOG_DEBUG);
}


DBresult *DBconn::Execute(const std::wstring &query)
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


std::wstring DBconn::ExecuteScalar(const std::wstring &query)
{
	int rows = -1;
	DBresultPtr res = Execute(query);
	std::wstring data;
	if (res)
	{
		data = res->GetString(0);
		rows = res->RowsAffected();
		return data;
	}
	return L"";
}


int DBconn::ExecuteVoid(const std::wstring &query)
{
	int rows = -1;
	DBresultPtr res = Execute(query);
	if (res)
	{
		rows = res->RowsAffected();
	}
	return rows;
}


std::wstring DBconn::GetLastError()
{
	boost::algorithm::trim(lastError);
	return lastError;
}

///////////////////////////////////////////////////////7

DBresult::DBresult(DBconn *conn, const std::wstring &query)
{
	char *cstrUTF = WStringToChar(query);
	currentRow = 0;
	maxRows = 0;
	if (cstrUTF != NULL)
	{
		result = PQexec(conn->conn, cstrUTF);

		if (result)
		{
			int rc = PQresultStatus(result);
			conn->SetLastResult(rc);
			if (rc == PGRES_TUPLES_OK)
				maxRows = PQntuples(result);
			else if (rc != PGRES_COMMAND_OK)
			{
				const char *last_error = PQerrorMessage(conn->conn);
				conn->lastError = CharToWString(last_error);
				LogMessage((L"Query error: " + conn->lastError), LOG_WARNING);
				PQclear(result);
				result = 0;
			}
		}
		else
			conn->lastError = CharToWString(PQerrorMessage(conn->conn));

		delete [] cstrUTF;
	}
}


DBresult::~DBresult()
{
	if (result)
		PQclear(result);
}


std::wstring DBresult::GetString(int col) const
{
	std::wstring str;

	if (result && currentRow < maxRows && col >= 0)
	{
		str = CharToWString((const char *)PQgetvalue(result, currentRow, col));
	}
	return str;
}


std::wstring DBresult::GetString(const std::wstring &colname) const
{
	char *cstrUTF = WStringToChar(colname);

	// Below function will return -1 if string is NULL or does not match.
	int col = PQfnumber(result, cstrUTF);
	if (cstrUTF != NULL)
		delete [] cstrUTF;

	if (col < 0)
	{
		// fatal: not found
		return L"";
	}
	return GetString(col);
}

///////////////////////////////////////////////////////7

bool connInfo::IsValidIP()
{
	if (host.empty())
		return false;

	const std::string hostAddr(host.begin(), host.end());
	try
	{
		ip::address ip = ip::address::from_string(hostAddr);
		if (ip.is_v4() || ip.is_v6())
			return true;
	}
	catch (const boost::system::system_error& e)
	{
		LogMessage(CharToWString((const char *)e.what()), LOG_WARNING);
		return false;
	}

	return false;
}

std::wstring connInfo::getConnectionString()
{
	std::wstring connStr;

	// Check if it has valid connection info
	if (!isValid)
		return connStr;

	// User
	connStr = L"user=" + user;

	// Port
	if (port != 0)
	{
		std::wstring portStr(boost::lexical_cast<std::wstring>(port));
		connStr += L" port=" + portStr;
	}

	// host or hostaddr
	if (!host.empty())
	{
		if (IsValidIP())
			connStr += L" hostaddr=" + host;
		else
			connStr += L" host=" + host;
	}

	// connection timeout
	if (connection_timeout != 0)
	{
		std::wstring val(boost::lexical_cast<std::wstring>(connection_timeout));
		connStr += L" connection_timeout=" + val;
	}

	// password
	if (!password.empty())
		connStr += L" password=" + password;

	if (!dbname.empty())
		connStr += L" dbname=" + dbname;

	LogMessage(L"Connection Information:", LOG_DEBUG);

	LogMessage((L"     user         : " + user), LOG_DEBUG);
	LogMessage((boost::wformat(L"     port         : %d") % port).str(), LOG_DEBUG);
	LogMessage((L"     host         : " + host), LOG_DEBUG);
	LogMessage((L"     dbname       : " + dbname), LOG_DEBUG);
	LogMessage((L"     password     : " + password), LOG_DEBUG);
	LogMessage((boost::wformat(L"     conn timeout : %d") % connection_timeout).str(), LOG_DEBUG);

	return connStr;
}

connInfo connInfo::getConnectionInfo(const std::wstring &conn_str)
{
	connInfo cnInfo;

	std::string connStr(conn_str.begin(), conn_str.end());

	const boost::regex propertyExp("(([ ]*[\t]*)+)=");
	connStr = boost::regex_replace(connStr, propertyExp, std::string("="));

	const boost::regex propertyExp_("=(([ ]*[\t]*)+)");
	connStr = boost::regex_replace(connStr, propertyExp_, std::string("="));

	boost::char_separator<char> sep("\t \n\r");
	boost::tokenizer<boost::char_separator<char> > tok(connStr, sep);

	for (boost::tokenizer< boost::char_separator<char> >::iterator beg = tok.begin(); (beg != tok.end()); ++beg)
	{
		std::wstring prop, value;
		int key_val_pair = 0;

		boost::char_separator<char> sep_("=");
		boost::tokenizer<boost::char_separator<char> > tok_(*beg, sep_);

		for (boost::tokenizer< boost::char_separator<char> >::iterator key_val = tok_.begin(); (key_val != tok_.end()); ++key_val)
		{
			std::string tk = *key_val;
			if (key_val_pair)
				value = std::wstring(tk.begin(), tk.end());
			else
				prop = std::wstring(tk.begin(), tk.end());
			key_val_pair++;
		}

		if (key_val_pair != 2)
			return cnInfo;

		if (boost::iequals(prop, L"user"))
			cnInfo.user = value;
		else if (boost::iequals(prop, L"host") || boost::iequals(prop, L"hostAddr"))
			cnInfo.host = value;
		else if (boost::iequals(prop, L"port"))
			cnInfo.port = boost::lexical_cast<unsigned long>(value);
		else if (boost::iequals(prop, L"password"))
			cnInfo.password = value;
		else if (boost::iequals(prop, L"connection_timeout"))
			cnInfo.connection_timeout = boost::lexical_cast<unsigned long>(value);
		else if (boost::iequals(prop, L"dbname"))
			cnInfo.dbname = value;
		else
		{
			// Not valid property found
			return cnInfo;
		}
	}

	// If user, dbname & host all are blank than we will consider this an invalid connection string
	if (cnInfo.user.empty() && cnInfo.dbname.empty() && cnInfo.host.empty())
		cnInfo.isValid = false;
	else
		cnInfo.isValid = true;

	return cnInfo;
}
