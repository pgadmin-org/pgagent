#######################################################################
#
# pgAgent - PostgreSQL tools
# Copyright (C) 2002 - 2008, The pgAdmin Development Team
# This software is released under the Artistic Licence
#
# CMakeLists.txt - CMake build configuration
#
#######################################################################

################################################################################
# Initial setup
################################################################################
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

IF(COMMAND cmake_policy)
    CMAKE_POLICY(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy)

SET(CMAKE_FIND_LIBRARY_PREFIXES "")
SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

################################################################################
# Apple stuff
################################################################################
IF(XXAPPLE)
    # Setup default values
    IF(NOT HAVE_CACHED_VALUES)
        IF(EXISTS /Developer/SDKs/MacOSX10.5.sdk)
            SET(CMAKE_OSX_ARCHITECTURES "ppc;i386" CACHE STRING "Build architectures for OSX" FORCE)
            SET(CMAKE_OSX_SYSROOT "/Developer/SDKs/MacOSX10.5.sdk" CACHE FILEPATH "isysroot used for universal binary support" FORCE)
        ELSE()
            IF(EXISTS /Developer/SDKs/MacOSX10.4u.sdk)
                SET(CMAKE_OSX_ARCHITECTURES "ppc;i386" CACHE STRING "Build architectures for OSX" FORCE)
                SET(CMAKE_OSX_SYSROOT "/Developer/SDKs/MacOSX10.4u.sdk" CACHE FILEPATH "isysroot used for universal binary support" FORCE)
            ELSE()
                MESSAGE(FATAL_ERROR "No supported SDK could be found!")
            ENDIF(EXISTS /Developer/SDKs/MacOSX10.4u.sdk)
        ENDIF(EXISTS /Developer/SDKs/MacOSX10.5.sdk)
    ENDIF(NOT HAVE_CACHED_VALUES)

    # Target Tiger
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.4")
ENDIF(XXAPPLE)

################################################################################
# Project config
################################################################################
PROJECT(pgagent)

SET(CMAKE_MODULE_PATH "${pgagent_SOURCE_DIR}/cmake/")

SET(VERSION 2.0.0)

# CPack stuff
SET(CPACK_PACKAGE_VERSION_MAJOR 2)
SET(CPACK_PACKAGE_VERSION_MINOR 0)
SET(CPACK_PACKAGE_VERSION_PATCH 0)
SET(CPACK_PACKAGE_NAME "pgAgent")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "pgAgent is a job scheduling engine for PostgreSQL")
SET(CPACK_PACKAGE_VENDOR "the pgAdmin Development Team")
SET(CPACK_GENERATOR TGZ)
SET(CPACK_SOURCE_GENERATOR TGZ)
SET(CPACK_SOURCE_IGNORE_FILES "\\\\.DS_Store;/CVS/;/\\\\.svn/;\\\\.swp$;\\\\.#;/#;.*~;cscope.*")

# This must come after we set the CPACK variables!!
INCLUDE(CPACK)

################################################################################
# pgAgent 
################################################################################
INCLUDE_DIRECTORIES(${pgagent_SOURCE_DIR}
                    ${pgagent_SOURCE_DIR}/include)

FILE(GLOB _cpp_files *.cpp)
FILE(GLOB _h_files include/*.h)

SET(_srcs ${_cpp_files} ${_h_files})
 
SET(PROJECT_SOURCE_FILES ${_srcs})
IF( WIN32)
    SET(PROJECT_RC_FILE pgagent.rc)
ELSE( WIN32)
    SET(PROJECT_RC_FILE "")
ENDIF( WIN32)

INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent.sql DESTINATION share)

################################################################################
# Find wxWidgets (this will create the target so must be after we setup sources
# but before we figure out where PostgreSQL is.
################################################################################
SET(WXW_USE_DEBUG OFF)
SET(WXW_USE_UNICODE ON)
SET(WXW_USE_SHARED OFF)
SET(WXW_USE_UNIV OFF)
SET(WXW_USE_MONO OFF)
SET(WXW_FILE_VERSION "28")
SET(WXW_VERSION "2.8")

INCLUDE (FindwxW)
INCLUDE (UsewxW)

################################################################################
# Find PostgreSQL
################################################################################
FIND_PROGRAM(POSTGRESQL_CONFIG_EXECUTABLE pg_config
             PATH $ENV{PGDIR}/bin
                  /usr/local/pgsql/bin
                  /opt/PostgreSQL/*/bin
                  /Library/PostgreSQL/*/bin
                  $ENV{ProgramFiles}/PostgreSQL/*/bin
                  $ENV{SystemDrive}/PostgreSQL/*/bin)

IF(NOT POSTGRESQL_CONFIG_EXECUTABLE)
    MESSAGE(FATAL_ERROR "PostgreSQL could not be found!")
ENDIF(NOT POSTGRESQL_CONFIG_EXECUTABLE)

EXEC_PROGRAM(${POSTGRESQL_CONFIG_EXECUTABLE} ARGS --version OUTPUT_VARIABLE POSTGRESQL_VERSION)

IF(WIN32)
    # Strip the bin and pg_config from the path
    GET_FILENAME_COMPONENT(POSTGRESQL_PATH ${POSTGRESQL_CONFIG_EXECUTABLE} PATH)
    GET_FILENAME_COMPONENT(POSTGRESQL_PATH ${POSTGRESQL_PATH} PATH)

    SET(POSTGRESQL_INCLUDES "${POSTGRESQL_PATH}/include")
    SET(POSTGRESQL_LIBRARIES "${POSTGRESQL_PATH}/lib")
ELSE(WIN32)
    EXEC_PROGRAM(${POSTGRESQL_CONFIG_EXECUTABLE} ARGS --includedir OUTPUT_VARIABLE POSTGRESQL_INCLUDES)
    EXEC_PROGRAM(${POSTGRESQL_CONFIG_EXECUTABLE} ARGS --libdir OUTPUT_VARIABLE POSTGRESQL_LIBRARIES)
ENDIF(WIN32)

INCLUDE_DIRECTORIES(${POSTGRESQL_INCLUDES})

# Setup linking
IF(WIN32)
    # There is no static libpq on PostgreSQL 8.3+ (VC++ built)
    TARGET_LINK_LIBRARIES(pgagent ${POSTGRESQL_LIBRARIES}/libpq.lib)
ELSE(WIN32)
    IF(${WXWIDGETS_USE_SHARED})
        LINK_DIRECTORIES(${POSTGRESQL_LIBRARIES})
        TARGET_LINK_LIBRARIES(pgagent pq)
    ELSE(${WXWIDGETS_USE_SHARED})
        TARGET_LINK_LIBRARIES(pgagent ${POSTGRESQL_LIBRARIES}/libpq.a)
    ENDIF(${WXWIDGETS_USE_SHARED})
ENDIF(WIN32)

################################################################################
# Build summary
################################################################################
MESSAGE(STATUS " ")

MESSAGE(STATUS "================================================================================")
MESSAGE(STATUS "Configuration summary:")
IF(${CONFIG_IS_UPTODATE})
    MESSAGE(STATUS "(Reusing previous identical config to save compilation time)")
ENDIF()
MESSAGE(STATUS " ")
MESSAGE(STATUS "  Project                     : ${PROJECT_NAME}")
MESSAGE(STATUS "  Description                 : ${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
MESSAGE(STATUS "  Version                     : ${VERSION}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  PostgreSQL version          : ${POSTGRESQL_VERSION}")
MESSAGE(STATUS "  PostgreSQL config binary    : ${POSTGRESQL_CONFIG_EXECUTABLE}")
MESSAGE(STATUS "  PostgreSQL include path     : ${POSTGRESQL_INCLUDES}")
MESSAGE(STATUS "  PostgreSQL library path     : ${POSTGRESQL_LIBRARIES}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  wxWidgets version           : ${WXWIDGETS_VERSION}")
MESSAGE(STATUS "  wxWidgets config binary     : ${WXWIDGETS_CONFIG_EXECUTABLE}")
MESSAGE(STATUS "  wxWidgets dynamic linking   : ${WXWIDGETS_USE_SHARED}")
MESSAGE(STATUS "  wxWidgets unicode?          : ${WXWIDGETS_USE_UNICODE}")
MESSAGE(STATUS "  wxWidgets debug?            : ${WXWIDGETS_USE_DEBUG}")
MESSAGE(STATUS "================================================================================")
MESSAGE(STATUS " ")

################################################################################
# Give ourselves a hint that we have cached values - must be last!
################################################################################
IF(NOT HAVE_CACHED_VALUES)
    SET(HAVE_CACHED_VALUES 1 CACHE INTERNAL "Flag to indicate that we have cached values")
ENDIF(NOT HAVE_CACHED_VALUES)


