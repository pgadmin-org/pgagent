//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// connection.cpp - database connection
//
//////////////////////////////////////////////////////////////////////////

#include "pgAgent.h"
#include <string>

namespace ip = boost::asio::ip;

DBconn   *DBconn::ms_primaryConn = NULL;
CONNinfo  DBconn::ms_basicConnInfo;

static boost::mutex  s_poolLock;

DBconn::DBconn(const std::string &connectString)
: m_inUse(false), m_next(NULL), m_prev(NULL), m_minorVersion(0),
	m_majorVersion(0)
{
	m_connStr = connectString;

	Connect(connectString);
}


bool DBconn::Connect(const std::string &connStr)
{
	LogMessage(("Creating DB connection: " + connStr), LOG_DEBUG);
	m_conn = PQconnectdb(connStr.c_str());

	if (PQstatus(m_conn) != CONNECTION_OK)
	{
		m_lastError = (const char *)PQerrorMessage(m_conn);
		PQfinish(m_conn);
		m_conn = NULL;

		return false;
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


std::string DBconn::qtDbString(const std::string &value)
{
	std::string result = value;

	boost::replace_all(result, "\\", "\\\\");
	boost::replace_all(result, "'", "''");
	result.append("'");

	if (BackendMinimumVersion(8, 1))
	{
		if (result.find("\\") != std::string::npos)
			result.replace(0, 0, "E'");
		else
			result.replace(0, 0, "'");
	}
	else
		result.replace(0, 0, "'");

	return result;
}

bool DBconn::BackendMinimumVersion(int major, int minor)
{
	if (!m_majorVersion)
	{
		std::string ver = ExecuteScalar("SELECT version();");

		sscanf(ver.c_str(), "%*s %d.%d", &m_majorVersion, &m_minorVersion);
	}

	return m_majorVersion > major || (m_majorVersion == major && m_minorVersion >= minor);
}

DBconn *DBconn::InitConnection(const std::string &connectString)
{
	MutexLocker locker(&s_poolLock);

	if (!ms_basicConnInfo.Set(connectString))
	{
		ms_primaryConn = NULL;
		// Unlock the mutex before logging error.
		locker = (boost::mutex *)NULL;
		LogMessage(
			"Primary connection string is not valid!\n" +
			ms_basicConnInfo.GetError(),
			LOG_ERROR
		);
	}

	ms_primaryConn = new DBconn(ms_basicConnInfo.Get());

	if (ms_primaryConn == NULL)
	{
		// Unlock the mutex before logging error.
		locker = (boost::mutex *)NULL;
		LogMessage(
			"Failed to create primary connection... out of memory?", LOG_ERROR
		);
	}

	if (ms_primaryConn->m_conn == NULL)
	{
		std::string error = ms_primaryConn->GetLastError();
		delete ms_primaryConn;
		ms_primaryConn = NULL;

		LogMessage(
			"Failed to create primary connection: " + error, LOG_WARNING
		);
		return NULL;
	}
	ms_primaryConn->m_inUse = true;

	return ms_primaryConn;
}


DBconn *DBconn::Get(const std::string &_connStr, const std::string &db)
{
	std::string connStr;
	if (!_connStr.empty())
	{
		CONNinfo connInfo;
		if (!connInfo.Set(_connStr))
		{
			LogMessage(
				"Failed to parse the connection string \"" + _connStr +
				"\" with error: " + connInfo.GetError(), LOG_WARNING
			);
			return NULL;
		}
		connStr = connInfo.Get();
	}
	else
	{
		connStr = DBconn::ms_basicConnInfo.Get(db);
	}

	MutexLocker locker(&s_poolLock);

	DBconn *thisConn = ms_primaryConn;
	DBconn *lastConn = NULL;

	// find an existing connection
	do
	{
		if (thisConn && !thisConn->m_inUse && thisConn->m_connStr == connStr)
		{
			LogMessage((
				"Using the existing connection '" +
				CONNinfo::Parse(thisConn->m_connStr, NULL, NULL, true) +
				"'..."), LOG_DEBUG
			);
			thisConn->m_inUse = true;

			return thisConn;
		}

		lastConn = thisConn;
		if (thisConn != NULL)
			thisConn = thisConn->m_next;

	} while (thisConn != NULL);


	// No suitable connection was found, so create a new one.
	DBconn *newConn = new DBconn(connStr);

	if (newConn && newConn->m_conn)
	{
		LogMessage((
			"Allocating new connection for the database with connection string: " +
			CONNinfo::Parse(newConn->m_connStr, NULL, NULL, true) + "..."
			), LOG_DEBUG);

		newConn->m_inUse = true;
		newConn->m_prev = lastConn;
		lastConn->m_next = newConn;
	}
	else
	{
		std::string warnMsg;
		if (connStr.empty())
			warnMsg = (
				"Failed to create new connection to database '" + db + "'" +
				(newConn != NULL ? ": " + newConn->GetLastError() : "")
			);
		else
			warnMsg = (
				"Failed to create new connection for connection string '" + connStr +
				"'" + (newConn != NULL ? ": " + newConn->GetLastError() : "")
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
	ExecuteVoid("RESET ALL");
	m_lastError.clear();
	m_inUse = false;

	LogMessage((
		"Returning the connection to the connection pool: '" +
		CONNinfo::Parse(m_connStr, NULL, NULL, true) + "'..."
		), LOG_DEBUG);
}

void DBconn::ClearConnections(bool all)
{
	MutexLocker locker(&s_poolLock);

	if (all)
		LogMessage("Clearing all connections", LOG_DEBUG);
	else
		LogMessage("Clearing inactive connections", LOG_DEBUG);

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

		LogMessage((boost::format(
			"Connection stats: total - %d, free - %d, deleted - %d"
		) % total % free % deleted).str(), LOG_DEBUG);

	}
	else
		LogMessage("No connections found!", LOG_DEBUG);
}


DBresult *DBconn::Execute(const std::string &query)
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


std::string DBconn::ExecuteScalar(const std::string &query)
{
	int rows = -1;
	DBresultPtr res = Execute(query);
	std::string data;

	if (res)
	{
		data = res->GetString(0);
		rows = res->RowsAffected();

		return data;
	}
	return data;
}


int DBconn::ExecuteVoid(const std::string &query)
{
	int rows = -1;
	DBresultPtr res = Execute(query);

	if (res)
	{
		rows = res->RowsAffected();
	}

	return rows;
}


std::string DBconn::GetLastError()
{
	boost::algorithm::trim(m_lastError);
	return m_lastError;
}

///////////////////////////////////////////////////////7

DBresult::DBresult(DBconn *conn, const std::string &query)
{
	m_currentRow = 0;
	m_maxRows = 0;

	m_result = PQexec(conn->m_conn, query.c_str());

	if (m_result != nullptr)
	{
		int rc = PQresultStatus(m_result);
		conn->SetLastResult(rc);
		if (rc == PGRES_TUPLES_OK)
			m_maxRows = PQntuples(m_result);
		else if (rc != PGRES_COMMAND_OK)
		{
			conn->m_lastError = PQerrorMessage(conn->m_conn);
			LogMessage("Query error: " + conn->m_lastError, LOG_WARNING);
			PQclear(m_result);
			m_result = nullptr;
		}
	}
	else
		conn->m_lastError = PQerrorMessage(conn->m_conn);
}


DBresult::~DBresult()
{
	if (m_result)
	{
		PQclear(m_result);
		m_result = NULL;
	}
}


std::string DBresult::GetString(int col) const
{
	std::string str;

	if (m_result && m_currentRow < m_maxRows && col >= 0)
	{
		str = (const char *)PQgetvalue(m_result, m_currentRow, col);
	}
	return str;
}


std::string DBresult::GetString(const std::string &colname) const
{
	// Below function will return -1 if string is NULL or does not match.
	int col = PQfnumber(m_result, colname.c_str());

	if (col < 0)
	{
		// fatal: not found
		return "";
	}

	return GetString(col);
}



const std::string CONNinfo::Parse(
	const std::string& connStr, std::string *error,
	std::string *dbName, bool forLogging
	)
{
	std::stringstream  res;
	PQconninfoOption  *opts, *opt;
	char              *errmsg = nullptr;

	if (error != nullptr)
		*error = "";

	if (connStr.empty())
	{
		if (error != nullptr)
			*error = "Empty connection string";

		return res.str();
	}

	// Parse Keyword/Value Connection Strings and Connection URIs
	opts = PQconninfoParse(connStr.c_str(), &errmsg);

	if (errmsg != nullptr || opts == NULL)
	{
		if (errmsg != nullptr)
		{
			if (error != nullptr)
				*error = errmsg;
			PQfreemem(errmsg);
		}
		else if (error != nullptr)
		{
				*error = "Failed to parse the connection string";
		}
		return res.str();
	}

	std::string val;
	bool        atleastOneParameter = false;

	LogMessage("Parsing connection information...", LOG_DEBUG);

	// Iterate over all options
	for (opt = opts; opt->keyword; opt++)
	{
		if (opt->val == NULL)
			continue;

		if (opt->dispchar[0] == 'D')
			continue;

		val = opt->val;
		if (forLogging)
		{
			LogMessage((
				boost::format("%s: %s") % opt->keyword %
				(opt->dispchar[0] == '*' ? "*****" : val)).str(), LOG_DEBUG
			);
		}

		// Create plain keyword=value connection string.  used
		// to find pooled connections in DBconn::Get() and to
		// open the connection in DBconn::Connect. this works
		// because PQconninfoParse() always returns the
		// connection info options in the same order.
		if (atleastOneParameter)
			res << ' ';
		atleastOneParameter = true;

		if (
			dbName != NULL &&
			strncmp(opt->keyword, "dbname", strlen(opt->keyword)) == 0
		)
			*dbName = val;
		res << opt->keyword << "=" << val;
	}

	PQconninfoFree(opts);

	return res.str();
}


bool CONNinfo::Set(const std::string& connStr)
{
	m_connStr = CONNinfo::Parse(connStr, &m_error, &m_dbName);

	return !m_connStr.empty();
}

const std::string CONNinfo::Get(const std::string &dbName) const
{
	if (m_connStr.empty())
		return m_connStr;

	return (
		m_connStr + " dbname=" + "" + (dbName.empty() ? m_dbName : dbName)
	);
}
