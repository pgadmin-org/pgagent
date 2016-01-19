/*
// pgAgent - PostgreSQL Tools
// 
// Copyright (C) 2002 - 2016 The pgAdmin Development Team
// This software is released under the PostgreSQL Licence
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

CREATE OR REPLACE FUNCTION pgagent.pga_exception_trigger() RETURNS "trigger" AS '
DECLARE

    v_jobid int4 := 0;

BEGIN

     IF TG_OP = ''DELETE'' THEN

        SELECT INTO v_jobid jscjobid FROM pgagent.pga_schedule WHERE jscid = OLD.jexscid;

        -- update pga_job from remaining schedules
        -- the actual calculation of jobnextrun will be performed in the trigger
        UPDATE pgagent.pga_job
           SET jobnextrun = NULL
         WHERE jobenabled AND jobid = v_jobid;
        RETURN OLD;
    ELSE

        SELECT INTO v_jobid jscjobid FROM pgagent.pga_schedule WHERE jscid = NEW.jexscid;

        UPDATE pgagent.pga_job
           SET jobnextrun = NULL
         WHERE jobenabled AND jobid = v_jobid;
        RETURN NEW;
    END IF;
END;
' LANGUAGE 'plpgsql' VOLATILE;
COMMENT ON FUNCTION pgagent.pga_exception_trigger() IS 'Update the job''s next run time whenever an exception changes';

ALTER TABLE pgagent.pga_jobstep ADD COLUMN jstconnstr text NOT NULL DEFAULT '' CONSTRAINT pga_jobstep_connstr_check CHECK ((jstconnstr != '' AND jstkind = 's' ) OR (jstconnstr = '' AND (jstkind = 'b' OR jstdbname != '')));
ALTER TABLE pgagent.pga_jobstep DROP CONSTRAINT pga_jobstep_check;

ALTER TABLE pgagent.pga_jobstep ADD CONSTRAINT pga_jobstep_dbname_check CHECK ((jstdbname != '' AND jstkind = 's' ) OR (jstdbname = '' AND (jstkind = 'b' OR jstconnstr != '')));
