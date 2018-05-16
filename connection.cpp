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
CONNinfo DBconn::ms_basicConnInfo;
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

DBconn::DBconn(const std::wstring &connectString)
: m_inUse(false), m_next(NULL), m_prev(NULL), m_minorVersion(0),
	m_majorVersion(0)
{
	m_connStr = connectString;

	Connect(connectString);
}


bool DBconn::Connect(const std::wstring &connectString)
{
	LogMessage((L"Creating DB connection: " + connectString), LOG_DEBUG);
	char *cstrUTF = WStringToChar(connectString);
	if (cstrUTF != NULL)
	{
		m_conn = PQconnectdb(cstrUTF);
		delete [] cstrUTF;

		if (PQstatus(m_conn) != CONNECTION_OK)
		{
			m_lastError = CharToWString((const char *)PQerrorMessage(m_conn));
			PQfinish(m_conn);
			m_conn = NULL;
			return false;
		}
	}

	return (m_conn != NULL);
}


DBconn::~DBconn()
{
	// clear a single connection
	if (m_conn)
	{
		PQfinish(m_conn);
		m_conn = NULL;
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
	if (!m_majorVersion)
	{
		std::wstring version = ExecuteScalar(L"SELECT version();");
		ver = WStringToChar(version);
		if (ver != NULL)
			sscanf(ver, "%*s %d.%d", &m_majorVersion, &m_minorVersion);
	}

	if (ver != NULL)
		delete []ver;

	return m_majorVersion > major || (m_majorVersion == major && m_minorVersion >= minor);
}

DBconn *DBconn::InitConnection(const std::wstring &connectString)
{
	MutexLocker locker(&s_poolLock);

	if (!ms_basicConnInfo.Set(connectString))
	{
		ms_primaryConn = NULL;
		// Unlock the mutex before logging error.
		locker = (boost::mutex *)NULL;
		LogMessage(
			L"Primary connection string is not valid!\n" + ms_basicConnInfo.GetError(),
			LOG_ERROR
			);
	}

	ms_primaryConn = new DBconn(ms_basicConnInfo.Get());

	if (ms_primaryConn == NULL)
	{
		// Unlock the mutex before logging error.
		locker = (boost::mutex *)NULL;
		LogMessage(
			L"Failed to create primary connection... out of memory?", LOG_ERROR
			);
	}

	if (!ms_primaryConn)
	{
		std::wstring error = ms_primaryConn->GetLastError();
		delete ms_primaryConn;
		ms_primaryConn = NULL;
		LogMessage(
			L"Failed to create primary connection: " + error, LOG_WARNING
			);
		return NULL;
	}
	ms_primaryConn->m_inUse = true;

	return ms_primaryConn;
}


DBconn *DBconn::Get(const std::wstring &connStr, const std::wstring &db)
{
	std::wstring dbConnStr;

	if (!connStr.empty())
	{
		CONNinfo connInfo;
		if (!connInfo.Set(connStr))
		{
			LogMessage((
				L"Failed to parse the connection string \"" + connStr +
				L"\" with error: " + connInfo.GetError()
				), LOG_WARNING);
			return NULL;
		}
		dbConnStr = connInfo.Get();
	}
	else
	{
		dbConnStr = DBconn::ms_basicConnInfo.Get(db);
	}

	MutexLocker locker(&s_poolLock);

	DBconn *thisConn = ms_primaryConn;
	DBconn *lastConn = NULL;

	// find an existing connection
	do
	{
		if (thisConn && !thisConn->m_inUse && thisConn->m_connStr == dbConnStr)
		{
			LogMessage((
				L"Using the existing connection '" +
				CONNinfo::Parse(thisConn->m_connStr, NULL, NULL, true) +
				L"'..."), LOG_DEBUG
				);
			thisConn->m_inUse = true;

			return thisConn;
		}

		lastConn = thisConn;
		if (thisConn != NULL)
			thisConn = thisConn->m_next;

	} while (thisConn != NULL);


	// No suitable connection was found, so create a new one.
	DBconn *newConn = NULL;
	newConn = new DBconn(dbConnStr);

	if (newConn && newConn->m_conn)
	{
		LogMessage((
			L"Allocating new connection for the database with connection string: " +
			CONNinfo::Parse(newConn->m_connStr, NULL, NULL, true) + L"..."
			), LOG_DEBUG);

		newConn->m_inUse = true;
		newConn->m_prev = lastConn;
		lastConn->m_next = newConn;
	}
	else
	{
		std::wstring warnMsg;
		if (connStr.empty())
			warnMsg = (
				L"Failed to create new connection to database '" + db + L"'" +
				(newConn != NULL ? L": " + newConn->GetLastError() : L"")
			);
		else
			warnMsg = (
				L"Failed to create new connection for connection string '" + connStr + L"'" +
				(newConn != NULL ? L": " + newConn->GetLastError() : L"")
			);
		LogMessage(warnMsg, LOG_STARTUP);

		if (newConn != NULL)
		{
			delete newConn;
			newConn = NULL;
		}

		return NULL;
	}

	return newConn;
}


void DBconn::Return()
{
	MutexLocker locker(&s_poolLock);

	// Cleanup
	ExecuteVoid(L"RESET ALL");
	m_lastError.empty();
	m_inUse = false;

	LogMessage((
		L"Returning the connection to the connection pool: '" +
		CONNinfo::Parse(m_connStr, NULL, NULL, true) + L"'..."
		), LOG_DEBUG);
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
		while (thisConn->m_next != NULL)
		{
			total++;

			if (!thisConn->m_inUse)
				free++;

			thisConn = thisConn->m_next;
		}
		if (!thisConn->m_inUse)
			free++;

		// Delete connections as required
		// If a connection is not in use, delete it, and reset the next and previous
		// pointers appropriately. If it is in use, don't touch it.
		while (thisConn->m_prev != NULL)
		{
			if ((!thisConn->m_inUse) || all)
			{
				deleteConn = thisConn;
				thisConn = deleteConn->m_prev;
				thisConn->m_next = deleteConn->m_next;
				if (deleteConn->m_next)
					deleteConn->m_next->m_prev = deleteConn->m_prev;
				delete deleteConn;
				deleted++;
			}
			else
			{
				thisConn = thisConn->m_prev;
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
	boost::algorithm::trim(m_lastError);
	return m_lastError;
}

///////////////////////////////////////////////////////7

DBresult::DBresult(DBconn *conn, const std::wstring &query)
{
	char *cstrUTF = WStringToChar(query);
	m_currentRow = 0;
	m_maxRows = 0;
	if (cstrUTF != NULL)
	{
		m_result = PQexec(conn->m_conn, cstrUTF);

		if (m_result)
		{
			int rc = PQresultStatus(m_result);
			conn->SetLastResult(rc);
			if (rc == PGRES_TUPLES_OK)
				m_maxRows = PQntuples(m_result);
			else if (rc != PGRES_COMMAND_OK)
			{
				const char *last_error = PQerrorMessage(conn->m_conn);
				conn->m_lastError = CharToWString(last_error);
				LogMessage((L"Query error: " + conn->m_lastError), LOG_WARNING);
				PQclear(m_result);
				m_result = NULL;
			}
		}
		else
			conn->m_lastError = CharToWString(PQerrorMessage(conn->m_conn));

		delete [] cstrUTF;
	}
}


DBresult::~DBresult()
{
	if (m_result)
	{
		PQclear(m_result);
		m_result = NULL;
	}
}


std::wstring DBresult::GetString(int col) const
{
	std::wstring str;

	if (m_result && m_currentRow < m_maxRows && col >= 0)
	{
		str = CharToWString((const char *)PQgetvalue(m_result, m_currentRow, col));
	}
	return str;
}


std::wstring DBresult::GetString(const std::wstring &colname) const
{
	char *cstrUTF = WStringToChar(colname);

	// Below function will return -1 if string is NULL or does not match.
	int col = PQfnumber(m_result, cstrUTF);
	if (cstrUTF != NULL)
		delete [] cstrUTF;

	if (col < 0)
	{
		// fatal: not found
		return L"";
	}
	return GetString(col);
}



const std::wstring CONNinfo::Parse(
	const std::wstring& connStr, std::wstring *error,
	std::wstring *dbName, bool forLogging
	)
{
	std::wstring res;
	PQconninfoOption *opts, *opt;
	char *errmsg = NULL;

	if (error)
		*error = L"";

	char *cstrUTF = WStringToChar(connStr);

	if (!cstrUTF)
	{
		if (error)
			*error = CharToWString("Failed to encode the connection string...");
		return res;
	}

	// parse Keyword/Value Connection Strings and Connection URIs
	opts = PQconninfoParse(cstrUTF, &errmsg);
	delete []cstrUTF;
	if (opts == NULL)
	{
		if (errmsg)
		{
			if (error)
				*error = CharToWString((const char *)errmsg);
			PQfreemem(errmsg);
		}
		return res;
	}

	if (connStr.empty())
	{
		if (error)
			*error = CharToWString("Empty connection string");
		return res;
	}

	std::wstring keyword, val;

	LogMessage(L"Parsing connection information...", LOG_DEBUG);

	// Iterate over all options
	for (opt = opts; opt->keyword; opt++)
	{
		if (opt->val == NULL)
			continue;

		if (opt->dispchar[0] == 'D')
			continue;

		keyword = CharToWString(opt->keyword);
		val = (forLogging && opt->dispchar[0] == '*') ? L"*****" :
			CharToWString(opt->val);

		if (!forLogging)
			LogMessage(
				(keyword + L": ") + (
					opt->dispchar[0] == '*' ? L"*****" : val
				), LOG_DEBUG);

		// Create plain keyword=value connection string.  used
		// to find pooled connections in DBconn::Get() and to
		// open the connection in DBconn::Connect. this works
		// because PQconninfoParse() always returns the
		// connection info options in the same order.
		if (!res.empty())
			res += ' ';
		if (dbName != NULL && keyword == L"dbname")
		{
			*dbName = val;
		}
		else
		{
			res += keyword + L"=" + val;
		}
	}

	PQconninfoFree(opts);

	return res;
}


bool CONNinfo::Set(const std::wstring& connStr)
{
	m_connStr = CONNinfo::Parse(connStr, &m_error, &m_dbName);

	return !m_connStr.empty();
}

const std::wstring CONNinfo::Get(const std::wstring &dbName) const
{
	if (m_connStr.empty())
		return m_connStr;

	return (
		m_connStr + L" dbname=" + L"" + (dbName.empty() ? m_dbName : dbName)
		);
}
