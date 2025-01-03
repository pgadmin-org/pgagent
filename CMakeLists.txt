#######################################################################
#
# pgAgent - PostgreSQL tools
# Copyright (C) 2002 - 2021, The pgAdmin Development Team
# This software is released under the PostgreSQL Licence
#
# CMakeLists.txt - CMake build configuration
#
#######################################################################

################################################################################
# Initial setup
################################################################################
CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

IF(COMMAND cmake_policy)
    CMAKE_POLICY(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy)

SET(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

SET(CMAKE_FIND_LIBRARY_PREFIXES "")
SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

SET(STATIC_BUILD NO CACHE BOOL "Statically link the executable?")
SET(BOOST_MULTITHREADED_BUILD YES CACHE BOOL "Build multithreaded executable?")
SET(BOOST_STATIC_BUILD NO CACHE BOOL "Statically link the executable?")
################################################################################
# Apple stuff
################################################################################
IF(APPLE)
    # Setup default values
    IF(NOT HAVE_CACHED_VALUES)

        IF(EXISTS ${CMAKE_OSX_SYSROOT})
            SET(CMAKE_OSX_SYSROOT ${CMAKE_OSX_SYSROOT} CACHE FILEPATH "isysroot used for universal binary support" FORCE)
        ENDIF(EXISTS ${CMAKE_OSX_SYSROOT})

        IF(NOT "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
            SET(CMAKE_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES} CACHE STRING "Build architectures for OSX" FORCE)
        ELSE()
            SET(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for OSX" FORCE)
        ENDIF(NOT ${CMAKE_OSX_ARCHITECTURES} STREQUAL "")

    ENDIF(NOT HAVE_CACHED_VALUES)

   # Target Tiger
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.9")
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
SET(VERSION "4.2.3")

# CPack stuff
SET(CPACK_PACKAGE_VERSION_MAJOR 4)
SET(CPACK_PACKAGE_VERSION_MINOR 2)
SET(CPACK_PACKAGE_VERSION_PATCH 2)
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
ADD_DEFINITIONS(-DPGAGENT_VERSION="${VERSION}")

IF(WIN32)
    SET(BOOST_WIN_VERSION 0x0501)
    ADD_DEFINITIONS(-D_WIN32_WINNT=${BOOST_WIN_VERSION} -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS)
ENDIF(WIN32)

# This must come after we set the CPACK variables!!
INCLUDE(CPack)

################################################################################
# Find PostgreSQL
################################################################################
SET(PG_STATIC ${STATIC_BUILD})

FIND_PACKAGE(PG REQUIRED)

INCLUDE_DIRECTORIES(${PG_INCLUDE_DIRS})
LINK_DIRECTORIES(${PG_LIBRARY_DIRS})

################################################################################
# Find Boost Libraries
################################################################################
SET (Boost_FIND_REQUIRED TRUE)
SET (Boost_FIND_QUIETLY FALSE)
SET (Boost_DEBUG FALSE)
SET (Boost_USE_MULTITHREADED ${BOOST_MULTITHREADED_BUILD})
SET (Boost_USE_STATIC_LIBS ${BOOST_STATIC_BUILD})

FIND_PACKAGE(Boost COMPONENTS filesystem regex date_time thread system)

if(Boost_FOUND)
  INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
  LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ELSE()
  MESSAGE(FATAL_ERROR "Boost library not found.")
endif()

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
IF(UNIX AND NOT APPLE)
TARGET_LINK_LIBRARIES(
        pgagent ${PG_LIBRARIES} ${Boost_LIBRARIES} -pthread
)
ELSE()
TARGET_LINK_LIBRARIES(
        pgagent ${PG_LIBRARIES} ${Boost_LIBRARIES}
)
ENDIF()

# Installation
IF (WIN32)
    INSTALL(TARGETS pgagent DESTINATION .)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/sql/pgagent.sql DESTINATION .)
ELSE(WIN32)
    INSTALL(TARGETS pgagent DESTINATION bin)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/sql/pgagent.sql DESTINATION share)
ENDIF(WIN32)

INSTALL(FILES ${pgagent_SOURCE_DIR}/README DESTINATION .)
INSTALL(FILES ${pgagent_SOURCE_DIR}/LICENSE DESTINATION .)

# Extension
IF(PG_EXTENSION)
    ADD_CUSTOM_COMMAND(
        OUTPUT ${CMAKE_BINARY_DIR}/pgagent--${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.sql pgagent.control
        COMMAND ${CMAKE_COMMAND} -D MAJOR_VERSION=${CPACK_PACKAGE_VERSION_MAJOR} -D MINOR_VERSION=${CPACK_PACKAGE_VERSION_MINOR} -DPGAGENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeExt.cmake
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/sql/pgagent.sql
    )
    ADD_CUSTOM_TARGET(run ALL DEPENDS ${CMAKE_BINARY_DIR}/pgagent--${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.sql ${CMAKE_BINARY_DIR}/pgagent.control)
    INSTALL(FILES ${CMAKE_BINARY_DIR}/pgagent--${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.sql DESTINATION ${PG_SHARE_DIR}/extension)
    FILE(GLOB SQL "${CMAKE_CURRENT_SOURCE_DIR}/sql/*--*.sql")
    FILE(GLOB CONTROL "${CMAKE_CURRENT_SOURCE_DIR}/*.control")
    INSTALL(FILES ${CMAKE_BINARY_DIR}/pgagent.control ${CONTROL} ${SQL} DESTINATION ${PG_SHARE_DIR}/extension)
ENDIF(PG_EXTENSION)

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
MESSAGE(STATUS "  PostgreSQL version string   : ${PG_VERSION_STRING}")
IF(${PG_MAJOR_VERSION} GREATER 9)
	MESSAGE(STATUS "  PostgreSQL version parts    : ${PG_MAJOR_VERSION}")
ELSE()
	MESSAGE(STATUS "  PostgreSQL version parts    : ${PG_MAJOR_VERSION}.${PG_MINOR_VERSION}")
ENDIF(${PG_MAJOR_VERSION} GREATER 9)
MESSAGE(STATUS "  PostgreSQL path             : ${PG_ROOT_DIR}")
MESSAGE(STATUS "  PostgreSQL config binary    : ${PG_CONFIG_PATH}")
MESSAGE(STATUS "  PostgreSQL include path     : ${PG_INCLUDE_DIRS}")
MESSAGE(STATUS "  PostgreSQL library path     : ${PG_LIBRARY_DIRS}")
MESSAGE(STATUS "  PostgreSQL share path       : ${PG_SHARE_DIR}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  Boost version               : ${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}")
MESSAGE(STATUS "  Boost path                  : ${Boost_INCLUDE_DIRS}")
MESSAGE(STATUS "  Boost include directory     : ${Boost_INCLUDE_DIRS}")
MESSAGE(STATUS "  Boost library directory     : ${Boost_LIBRARY_DIRS}")
MESSAGE(STATUS "  Boost Static linking        : ${Boost_USE_STATIC_LIBS}")
MESSAGE(STATUS "================================================================================")
MESSAGE(STATUS " ")

################################################################################
# Give ourselves a hint that we have cached values - must be last!
################################################################################
IF(NOT HAVE_CACHED_VALUES)
    SET(HAVE_CACHED_VALUES 1 CACHE INTERNAL "Flag to indicate that we have cached values")
ENDIF(NOT HAVE_CACHED_VALUES)
