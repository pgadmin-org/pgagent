#######################################################################
#
# pgAgent - PostgreSQL tools
# Copyright (C) 2002 - 2012, The pgAdmin Development Team
# This software is released under the PostgreSQL Licence
#
# CMakeLists.txt - CMake build configuration
#
#######################################################################

################################################################################
# Initial setup
################################################################################
IF(APPLE)
    CMAKE_MINIMUM_REQUIRED(VERSION 2.8.8)
ELSE()
    CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
ENDIF(APPLE)

IF(COMMAND cmake_policy)
    CMAKE_POLICY(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy)

SET(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

SET(CMAKE_FIND_LIBRARY_PREFIXES "")
SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

SET(STATIC_BUILD YES CACHE BOOL "Statically link the executable?")

################################################################################
# Apple stuff
################################################################################
IF(APPLE)
    # Setup default values
    IF(NOT HAVE_CACHED_VALUES)

        IF(EXISTS ${CMAKE_OSX_SYSROOT})
            SET(CMAKE_OSX_SYSROOT ${CMAKE_OSX_SYSROOT} CACHE FILEPATH "isysroot used for universal binary support" FORCE)
        ENDIF(EXISTS ${CMAKE_OSX_SYSROOT})
    
        IF(NOT ${CMAKE_OSX_ARCHITECTURES} STREQUAL "")
            SET(CMAKE_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES} CACHE STRING "Build architectures for OSX" FORCE)
        ELSE()
            SET(CMAKE_OSX_ARCHITECTURES "ppc;i386" CACHE STRING "Build architectures for OSX" FORCE)
        ENDIF(NOT ${CMAKE_OSX_ARCHITECTURES} STREQUAL "")
    
    ENDIF(NOT HAVE_CACHED_VALUES)
 
   # Target Tiger
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.4")
ENDIF(APPLE)

################################################################################
# Project config
################################################################################
PROJECT(pgagent)

# If changing the version number, remember to change here and under the CPack 
# settings in this file, as well as the definition for pgagent_schema_version()
# in pgagent.sql and upgrade_pgagent.sql if the major version number is
# changed. The full version number also needs to be included in pgAgent.rc and
# pgaevent/pgamsgevent.rc at present.
SET(VERSION 3.3.0)

# CPack stuff
SET(CPACK_PACKAGE_VERSION_MAJOR 3)
SET(CPACK_PACKAGE_VERSION_MINOR 3)
SET(CPACK_PACKAGE_VERSION_PATCH 0)
SET(CPACK_PACKAGE_NAME "pgAgent")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "pgAgent is a job scheduling engine for PostgreSQL")
SET(CPACK_PACKAGE_VENDOR "the pgAdmin Development Team")
IF(WIN32)
    SET(CPACK_GENERATOR ZIP)
ELSE(WIN32)
    SET(CPACK_GENERATOR TGZ)
ENDIF(WIN32)
SET(CPACK_SOURCE_GENERATOR TGZ)
SET(CPACK_SOURCE_IGNORE_FILES "\\\\.DS_Store;/CVS/;/\\\\.svn/;\\\\.swp$;\\\\.#;/#;.*~;cscope.*")

ADD_DEFINITIONS(-DPGAGENT_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR})

# This must come after we set the CPACK variables!!
INCLUDE(CPack)

################################################################################
# Find wxWidgets
################################################################################
SET(WX_DEBUG NO CACHE BOOL "Use the debug build of wxWidgets?")
SET(WX_STATIC ${STATIC_BUILD})
SET(WX_VERSION "2.8")
SET(WX_UNICODE YES)
SET(WX_MODULES "base")

FIND_PACKAGE(WX REQUIRED)

INCLUDE_DIRECTORIES(${WX_INCLUDE_DIRS})
ADD_DEFINITIONS(${WX_DEFINITIONS})
LINK_DIRECTORIES(${WX_LIBRARY_DIRS})
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WX_CXX_FLAGS}")

################################################################################
# Find PostgreSQL
################################################################################
SET(PG_STATIC ${WX_STATIC})

FIND_PACKAGE(PG REQUIRED)

INCLUDE_DIRECTORIES(${PG_INCLUDE_DIRS})
LINK_DIRECTORIES(${PG_LIBRARY_DIRS})

################################################################################
# Let's rock!
################################################################################
INCLUDE_DIRECTORIES(${pgagent_SOURCE_DIR}
                    ${pgagent_SOURCE_DIR}/include)

FILE(GLOB _cpp_files *.cpp)
FILE(GLOB _h_files include/*.h)

SET(_srcs ${_cpp_files} ${_h_files})
 
IF(WIN32)
    SET(_srcs ${_srcs} pgagent.rc)
ENDIF(WIN32)
 
ADD_EXECUTABLE(pgagent ${_srcs})
TARGET_LINK_LIBRARIES(pgagent ${PG_LIBRARIES} ${WX_LIBRARIES})

# Installation
IF (WIN32)
    INSTALL(TARGETS pgagent DESTINATION .)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent.sql DESTINATION .)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent_upgrade.sql DESTINATION .)
ELSE(WIN32)
    INSTALL(TARGETS pgagent DESTINATION bin)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent.sql DESTINATION share)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent_upgrade.sql DESTINATION share)
ENDIF(WIN32)

INSTALL(FILES ${pgagent_SOURCE_DIR}/README DESTINATION .)
INSTALL(FILES ${pgagent_SOURCE_DIR}/LICENSE DESTINATION .)

################################################################################
# pgaevent
################################################################################
IF(WIN32)
    ADD_SUBDIRECTORY(pgaevent)
ENDIF(WIN32)

################################################################################
# Build summary
################################################################################
MESSAGE(STATUS " ")

MESSAGE(STATUS "================================================================================")
MESSAGE(STATUS "Configuration summary:")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  Project                     : ${PROJECT_NAME}")
MESSAGE(STATUS "  Description                 : ${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
MESSAGE(STATUS "  Version                     : ${VERSION}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  PostgreSQL version          : ${PG_VERSION_STRING}")
MESSAGE(STATUS "  PostgreSQL path             : ${PG_ROOT_DIR}")
MESSAGE(STATUS "  PostgreSQL config binary    : ${PG_CONFIG_PATH}")
MESSAGE(STATUS "  PostgreSQL include path     : ${PG_INCLUDE_DIRS}")
MESSAGE(STATUS "  PostgreSQL library path     : ${PG_LIBRARY_DIRS}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  wxWidgets version           : ${WX_VERSION_STRING}")
MESSAGE(STATUS "  wxWidgets path              : ${WX_ROOT_DIR}")
MESSAGE(STATUS "  wxWidgets config binary     : ${WX_CONFIG_PATH}")
MESSAGE(STATUS "  wxWidgets Static linking    : ${WX_STATIC}")
MESSAGE(STATUS "  wxWidgets Debug?            : ${WX_DEBUG}")
MESSAGE(STATUS "================================================================================")
MESSAGE(STATUS " ")

################################################################################
# Give ourselves a hint that we have cached values - must be last!
################################################################################
IF(NOT HAVE_CACHED_VALUES)
    SET(HAVE_CACHED_VALUES 1 CACHE INTERNAL "Flag to indicate that we have cached values")
ENDIF(NOT HAVE_CACHED_VALUES)


