//////////////////////////////////////////////////////////////////////////
//
// pgAgent - PostgreSQL Tools
//
// Copyright (C) 2002 - 2015, The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// misc.h - misc functions
//
//////////////////////////////////////////////////////////////////////////


#ifndef MISC_H
#define MISC_H


void WaitAWhile(const bool waitLong = false);
void setOptions(int argc, char **argv, const wxString &executable);
wxString getArg(int &argc, char **&argv);
wxString NumToStr(const long l);
void printVersion();

#endif // MISC_H

