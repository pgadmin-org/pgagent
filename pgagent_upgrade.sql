/*
// pgAgent - PostgreSQL Tools
// $Id$
// Copyright (C) 2002 - 2009 The pgAdmin Development Team
// This software is released under the BSD Licence
//
// pgagent_upgrade.sql - Upgrade pgAgent tables and functions
//
*/

CREATE OR REPLACE FUNCTION pgagent.pgagent_schema_version() RETURNS int2 AS '
BEGIN
    -- RETURNS PGAGENT MAJOR VERSION
    -- WE WILL CHANGE THE MAJOR VERSION, ONLY IF THERE IS A SCHEMA CHANGE
    RETURN 3;
END;
' LANGUAGE 'plpgsql' VOLATILE;

ALTER TABLE pgagent.pga_jobstep ADD COLUMN jstconnstr text NOT NULL DEFAULT '' CONSTRAINT pga_jobstep_connstr_check CHECK ((jstconnstr != '' AND jstkind = 's' ) OR (jstconnstr = '' AND (jstkind = 'b' OR jstdbname != '')));
ALTER TABLE pgagent.pga_jobstep DROP CONSTRAINT pga_jobstep_check;

ALTER TABLE pgagent.pga_jobstep ADD CONSTRAINT pga_jobstep_dbname_check CHECK ((jstdbname != '' AND jstkind = 's' ) OR (jstdbname = '' AND (jstkind = 'b' OR jstconnstr != '')));
