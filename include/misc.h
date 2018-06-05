//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2018, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// misc.h - misc functions
//
//////////////////////////////////////////////////////////////////////////


#ifndef MISC_H
#define MISC_H


void WaitAWhile(const bool waitLong = false);
void setOptions(int argc, char **argv, const std::wstring &executable);
std::wstring getArg(int &argc, char **&argv);
std::wstring NumToStr(const long l);
void printVersion();
std::wstring CharToWString(const char* cstr);
char* WStringToChar(const std::wstring &wstr);
std::string generateRandomString(size_t length);
std::wstring getTemporaryDirectoryPath();

#endif // MISC_H

