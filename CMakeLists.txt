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
IF(APPLE)
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
ENDIF(APPLE)

################################################################################
# Project config
################################################################################
PROJECT(pgagent)

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
INCLUDE(CPack)

################################################################################
# Find PostgreSQL
################################################################################
FIND_PROGRAM(PostgreSQL_CONFIG_EXECUTABLE pg_config
             PATH $ENV{PGDIR}/bin
                  /usr/local/pgsql/bin
                  /opt/PostgreSQL/*/bin
                  /Library/PostgreSQL/*/bin
                  $ENV{ProgramFiles}/PostgreSQL/*/bin
                  $ENV{SystemDrive}/PostgreSQL/*/bin)

IF(NOT PostgreSQL_CONFIG_EXECUTABLE)
    MESSAGE(FATAL_ERROR "PostgreSQL could not be found!")
ENDIF(NOT PostgreSQL_CONFIG_EXECUTABLE)

EXEC_PROGRAM(${PostgreSQL_CONFIG_EXECUTABLE} ARGS --version OUTPUT_VARIABLE PostgreSQL_VERSION)

IF(WIN32)
    # Strip the bin and pg_config from the path
    GET_FILENAME_COMPONENT(PostgreSQL_PATH ${PostgreSQL_CONFIG_EXECUTABLE} PATH)
    GET_FILENAME_COMPONENT(PostgreSQL_PATH ${PostgreSQL_PATH} PATH)

    SET(PostgreSQL_INCLUDES "${PostgreSQL_PATH}/include")
    SET(PostgreSQL_LIBRARIES "${PostgreSQL_PATH}/lib")
ELSE(WIN32)
    EXEC_PROGRAM(${PostgreSQL_CONFIG_EXECUTABLE} ARGS --includedir OUTPUT_VARIABLE PostgreSQL_INCLUDES)
    EXEC_PROGRAM(${PostgreSQL_CONFIG_EXECUTABLE} ARGS --libdir OUTPUT_VARIABLE PostgreSQL_LIBRARIES)
ENDIF(WIN32)

INCLUDE_DIRECTORIES(. ${PostgreSQL_INCLUDES})
LINK_DIRECTORIES(. ${PostgreSQL_LIBRARIES})

################################################################################
# Find wxWidgets
################################################################################
IF(NOT HAVE_CACHED_VALUES)
    SET(wxWidgets_USE_UNICODE ON CACHE BOOL "Use unicode build?" FORCE)
    SET(wxWidgets_USE_DEBUG OFF CACHE BOOL "Use debug build?" FORCE)
    SET(wxWidgets_USE_STATIC ON CACHE BOOL "Link libraries statically?" FORCE)
ENDIF(NOT HAVE_CACHED_VALUES)

SET(wxWidgets_USE_LIBS base core)
FIND_PACKAGE(wxWidgets)

IF(NOT wxWidgets_FOUND)
    MESSAGE(FATAL_ERROR "wxWidgets could not be found!")
ENDIF(NOT wxWidgets_FOUND)

INCLUDE(${wxWidgets_USE_FILE})

IF(NOT WIN32)
    # Get the wxWidgets version
    EXECUTE_PROCESS(
        COMMAND sh "${wxWidgets_CONFIG_EXECUTABLE}" --version
        OUTPUT_VARIABLE wxWidgets_VERSION
        RESULT_VARIABLE RET
        ERROR_QUIET
    )
    STRING(STRIP ${wxWidgets_VERSION} wxWidgets_VERSION)

    IF(NOT ${wxWidgets_VERSION} MATCHES "^2.8.")
        MESSAGE(FATAL_ERROR "wxWidgets must be version 2.8.x (${wxWidgets_VERSION} found)")
    ENDIF(NOT ${wxWidgets_VERSION} MATCHES "^2.8.")
ELSE(NOT WIN32)
    IF(NOT wxWidgets_LIBRARIES MATCHES "wxmsw28")
        MESSAGE(FATAL_ERROR "wxWidgets must be version 2.8.x")
    ELSE(NOT wxWidgets_LIBRARIES MATCHES "wxmsw28")
        SET(wxWidgets_VERSION "2.8.x")
    ENDIF(NOT wxWidgets_LIBRARIES MATCHES "wxmsw28")
ENDIF(NOT WIN32)

################################################################################
# Let's rock!
################################################################################
INCLUDE_DIRECTORIES(${pgagent_SOURCE_DIR}
                    ${pgagent_SOURCE_DIR}/include
                    ${wxWidgets_INCLUDE_DIRS} )

FILE(GLOB _cpp_files *.cpp)
FILE(GLOB _h_files include/*.h)

SET(_srcs ${_cpp_files} ${_h_files})
 
IF(WIN32)
    SET(_srcs ${_srcs} pgagent.rc)
ENDIF(WIN32)
 
ADD_EXECUTABLE(pgagent ${_srcs})

# Setup linking
IF(WIN32)
    # There is no static libpq on PostgreSQL 8.3+ (VC++ built)
    TARGET_LINK_LIBRARIES(pgagent ${wxWidgets_LIBRARIES} ${PostgreSQL_LIBRARIES}/libpq.lib)
ELSE(WIN32)
    IF(${wxWidgets_USE_STATIC})
        TARGET_LINK_LIBRARIES(pgagent ${wxWidgets_LIBRARIES} ${PostgreSQL_LIBRARIES}/libpq.a)
    ELSE(${wxWidgets_USE_STATIC})
        TARGET_LINK_LIBRARIES(pgagent ${wxWidgets_LIBRARIES} pq)
    ENDIF(${wxWidgets_USE_STATIC})
ENDIF(WIN32)

# Installation
IF (WIN32)
    INSTALL(TARGETS pgagent DESTINATION .)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent.sql DESTINATION .)
ELSE(WIN32)
    INSTALL(TARGETS pgagent DESTINATION bin)
    INSTALL(FILES ${pgagent_SOURCE_DIR}/pgagent.sql DESTINATION share)
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
IF(${CONFIG_IS_UPTODATE})
    MESSAGE(STATUS "(Reusing previous identical config to save compilation time)")
ENDIF()
MESSAGE(STATUS " ")
MESSAGE(STATUS "  Project                     : ${PROJECT_NAME}")
MESSAGE(STATUS "  Description                 : ${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
MESSAGE(STATUS "  Version                     : ${VERSION}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  PostgreSQL version          : ${PostgreSQL_VERSION}")
MESSAGE(STATUS "  PostgreSQL config binary    : ${PostgreSQL_CONFIG_EXECUTABLE}")
MESSAGE(STATUS "  PostgreSQL include path     : ${PostgreSQL_INCLUDES}")
MESSAGE(STATUS "  PostgreSQL library path     : ${PostgreSQL_LIBRARIES}")
MESSAGE(STATUS " ")
MESSAGE(STATUS "  wxWidgets version           : ${wxWidgets_VERSION}")
MESSAGE(STATUS "  wxWidgets config binary     : ${wxWidgets_CONFIG_EXECUTABLE}")
MESSAGE(STATUS "  wxWidgets Static linking    : ${wxWidgets_USE_STATIC}")
MESSAGE(STATUS "  wxWidgets Unicode?          : ${wxWidgets_USE_UNICODE}")
MESSAGE(STATUS "  wxWidgets Debug?            : ${wxWidgets_USE_DEBUG}")
MESSAGE(STATUS "================================================================================")
MESSAGE(STATUS " ")

################################################################################
# Give ourselves a hint that we have cached values - must be last!
################################################################################
IF(NOT HAVE_CACHED_VALUES)
    SET(HAVE_CACHED_VALUES 1 CACHE INTERNAL "Flag to indicate that we have cached values")
ENDIF(NOT HAVE_CACHED_VALUES)


