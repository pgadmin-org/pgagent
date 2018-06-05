/*
// pgAgent - PostgreSQL Tools
// 
// Copyright (C) 2002 - 2018 The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgagent--3.4--4.0.sql - Upgrade the pgAgent schema to 4.0
//
*/

\echo Use "CREATE EXTENSION pgagent UPDATE" to load this file. \quit

CREATE OR REPLACE FUNCTION pgagent.pgagent_schema_version() RETURNS int2 AS '
BEGIN
    -- RETURNS PGAGENT MAJOR VERSION
    -- WE WILL CHANGE THE MAJOR VERSION, ONLY IF THERE IS A SCHEMA CHANGE
    RETURN 4;
END;
' LANGUAGE 'plpgsql' VOLATILE;