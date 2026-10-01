# ==================================================================================
# Lemon Online Server (embedded HTTP submission service)
#
# Needs Qt HttpServer. Configure with -DENABLE_ONLINE_SERVER=OFF to build
# LemonLime without it (the menu entry is then hidden and the sources, the
# resource bundle and the Qt::HttpServer link dependency are dropped).
# ==================================================================================

set(LEMON_BASEDIR_SERVER ${CMAKE_SOURCE_DIR}/src/server)

set(LEMON_SERVER_SOURCES "")
set(LEMON_SERVER_RESOURCES "")

if(ENABLE_ONLINE_SERVER)
    set(LEMON_SERVER_SOURCES
        ${LEMON_BASEDIR_SERVER}/UserStore.h
        ${LEMON_BASEDIR_SERVER}/UserStore.cpp
        ${LEMON_BASEDIR_SERVER}/SessionManager.h
        ${LEMON_BASEDIR_SERVER}/SessionManager.cpp
        ${LEMON_BASEDIR_SERVER}/SubmissionServer.h
        ${LEMON_BASEDIR_SERVER}/SubmissionServer.cpp
        ${LEMON_BASEDIR_SERVER}/onlineserverdialog.h
        ${LEMON_BASEDIR_SERVER}/onlineserverdialog.cpp
    )
    set(LEMON_SERVER_RESOURCES ${LEMON_BASEDIR_SERVER}/server-assets.qrc)
else()
    message(STATUS "Online submission service disabled (ENABLE_ONLINE_SERVER=OFF)")
endif()
