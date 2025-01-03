//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2024, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// misc.h - misc functions
//
//////////////////////////////////////////////////////////////////////////


#ifndef MISC_H
#define MISC_H

void          WaitAWhile(const bool waitLong = false);
void          setOptions(int argc, char **argv, const std::string &executable);
std::string   getArg(int &argc, char **&argv);
std::string   NumToStr(const long l);
void          printVersion();
#if BOOST_OS_WINDOWS
std::wstring  s2ws(const std::string &str);
std::string   ws2s(const std::wstring &wstr);
#endif
std::string   generateRandomString(size_t length);
bool          createUniqueTemporaryDirectory(const std::string &prefix, boost::filesystem::path &tempDir);

class MutexLocker
{
public:
	MutexLocker(boost::mutex *lock): m_lock(lock)
	{
		if (m_lock != NULL)
			m_lock->lock();
	}

	~MutexLocker()
	{
		if (m_lock != NULL)
			m_lock->unlock();
		m_lock = NULL;
	}

	// When the exit(code) is being called, static object is being released
	// earlier. Hence - it is a good idea to set the current mutex object to
	// NULL to avoid ASSERTION in debug mode (specifically on OSX).
	MutexLocker& operator =(boost::mutex *lock)
	{
		if (m_lock != NULL)
			m_lock->unlock();
		m_lock = lock;

		if (m_lock != NULL)
			m_lock->lock();

		return *this;
	}

private:
	boost::mutex *m_lock;
};

#endif // MISC_H

