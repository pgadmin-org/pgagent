#######################################################################
#
# pgAgent - PostgreSQL tools
# Copyright (C) 2002 - 2009, The pgAdmin Development Team
# This software is released under the BSD Licence
#
# pgaevent/CMakeLists.txt - CMake build configuration
#
#######################################################################

################################################################################
# Let's rock!
################################################################################
IF(WIN32)
    SET(_srcs pgaevent.c pgaevent.def pgamsgevent.rc)

    ADD_LIBRARY(pgaevent MODULE ${_srcs})

    INSTALL(TARGETS pgaevent DESTINATION .)
ENDIF(WIN32)


