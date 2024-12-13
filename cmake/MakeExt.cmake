#######################################################################
#
# pgAgent - PostgreSQL tools
# Copyright (C) 2002 - 2024, The pgAdmin Development Team
# This software is released under the PostgreSQL Licence
#
# MakeExt,cmake - Create the PG Extension
#
#######################################################################

FILE(READ ${PGAGENT_SOURCE_DIR}/sql/pgagent.sql PGAGENT_SQL)
STRING(REPLACE "BEGIN TRANSACTION;" "" PGAGENT_SQL "${PGAGENT_SQL}")
STRING(REPLACE "COMMIT TRANSACTION;" "" PGAGENT_SQL "${PGAGENT_SQL}")
STRING(REPLACE "CREATE SCHEMA pgagent;" "" PGAGENT_SQL "${PGAGENT_SQL}")
STRING(REPLACE "-- EXT SELECT" "SELECT" PGAGENT_SQL "${PGAGENT_SQL}")
FILE(WRITE "${CMAKE_BINARY_DIR}/pgagent--${MAJOR_VERSION}.${MINOR_VERSION}.sql" "${PGAGENT_SQL}")

CONFIGURE_FILE(${PGAGENT_SOURCE_DIR}/pgagent.control.in ${CMAKE_BINARY_DIR}/pgagent.control)
