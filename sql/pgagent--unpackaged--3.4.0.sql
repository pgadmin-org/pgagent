/*
// pgAgent - PostgreSQL Tools
// 
// Copyright (C) 2002 - 2014 The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
//
// pgagent--unpackaged--3.4.sql - Convert pgAgent existing tables and functions to an extension
//
*/

\echo Use "CREATE EXTENSION pgagent FROM unpackaged" to load this file. \quit

ALTER EXTENSION pgagent ADD TABLE pgagent.pga_jobagent;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_jobclass;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_job;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_jobstep;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_schedule;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_exception;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_joblog;
ALTER EXTENSION pgagent ADD TABLE pgagent.pga_jobsteplog;

ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_exception_jexid_seq;
ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_job_jobid_seq;
ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_jobclass_jclid_seq;
ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_joblog_jlgid_seq;
ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_jobstep_jstid_seq;
ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_jobsteplog_jslid_seq;
ALTER EXTENSION pgagent ADD SEQUENCE pgagent.pga_schedule_jscid_seq;

ALTER EXTENSION pgagent ADD FUNCTION pgagent.pgagent_schema_version();
ALTER EXTENSION pgagent ADD FUNCTION pgagent.pga_next_schedule(int4, timestamptz, timestamptz, _bool, _bool, _bool, _bool, _bool);
ALTER EXTENSION pgagent ADD FUNCTION pgagent.pga_is_leap_year(int2);
ALTER EXTENSION pgagent ADD FUNCTION pgagent.pga_job_trigger();
ALTER EXTENSION pgagent ADD FUNCTION pgagent.pga_schedule_trigger();
ALTER EXTENSION pgagent ADD FUNCTION pgagent.pga_exception_trigger();

SELECT pg_catalog.pg_extension_config_dump('pga_jobagent', '');
SELECT pg_catalog.pg_extension_config_dump('pga_jobclass', $$WHERE jclname NOT IN ('Routine Maintenance', 'Data Import', 'Data Export', 'Data Summarisation', 'Miscellaneous')$$);
SELECT pg_catalog.pg_extension_config_dump('pga_job', '');
SELECT pg_catalog.pg_extension_config_dump('pga_jobstep', '');
SELECT pg_catalog.pg_extension_config_dump('pga_schedule', '');
SELECT pg_catalog.pg_extension_config_dump('pga_exception', '');
SELECT pg_catalog.pg_extension_config_dump('pga_joblog', '');
SELECT pg_catalog.pg_extension_config_dump('pga_jobsteplog', '');
