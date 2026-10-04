install(TARGETS lemon RUNTIME DESTINATION bin)

include(GNUInstallDirs)

set(LEMON_LINUX_ICON_DIMENSIONS 16 22 24 32 36 44 48 64 72 96 128 150 192 256 310 512 1024)
install(FILES assets/lemon-lime.metainfo.xml.in DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo" RENAME lemon-lime.metainfo.xml)
install(FILES assets/x-lemon-contest.xml.in DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/application" RENAME x-lemon-contest.xml)
install(FILES assets/lemon-lime.desktop.in DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" RENAME lemon-lime.desktop)
#install(FILES assets/icons/lemon-lime.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
foreach(LEMON_LINUX_ICON_DIMENSION ${LEMON_LINUX_ICON_DIMENSIONS})
	install(FILES assets/icons/lemon-lime.${LEMON_LINUX_ICON_DIMENSION}.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${LEMON_LINUX_ICON_DIMENSION}x${LEMON_LINUX_ICON_DIMENSION}/apps" RENAME lemon-lime.png)
endforeach(LEMON_LINUX_ICON_DIMENSION)
if(NOT EMBED_TRANSLATIONS)
    install(FILES ${QM_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/lemon-lime/lang")
endif()
if(NOT EMBED_DOCS)
    install(FILES manual/llmanual.pdf DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/lemon-lime")
endif()
install(FILES assets/Testlib-for-Lemons/testlib.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} RENAME testlib_for_lemons.h)

# ==================================================================================
# Linux: self-contained portable package
# ==================================================================================
#
#   cmake --build <build-dir> --target linux-portable
#
# assembles <build-dir>/portable/LemonLime-<version>-linux-qt6-<arch>/ together with
# a .tar.gz of it and the archive's .sha256. The folder holds the binary, the Qt
# runtime libraries and plugins it needs, a qt.conf and the install tree, so it runs
# on a machine without Qt installed.
#
# The tree must be configured with -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib'; the
# packaging script (cmake/deploy-linux.cmake) refuses to run without it.

option(ENABLE_LINUX_PORTABLE_PACKAGE "Create the linux-portable packaging target" ON)
option(LEMON_LINUX_PORTABLE_TARGZ "Compress the portable folder into a .tar.gz" ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(LEMON_LINUX_ARCH "x86_64")
else()
    set(LEMON_LINUX_ARCH "i686")
endif()

# Qt's plugin directory is a sibling of the Qt installation's lib directory, which
# is where Qt6::qmake points.
set(LEMON_LINUX_QT_PLUGIN_DIR "")

if(TARGET ${LEMON_QT_LIBNAME}::qmake)
    get_target_property(_lemon_qmake ${LEMON_QT_LIBNAME}::qmake IMPORTED_LOCATION)

    if(_lemon_qmake)
        get_filename_component(_lemon_qt_bin "${_lemon_qmake}" DIRECTORY)
        get_filename_component(_lemon_qt_root "${_lemon_qt_bin}" DIRECTORY)

        if(IS_DIRECTORY "${_lemon_qt_root}/plugins")
            set(LEMON_LINUX_QT_PLUGIN_DIR "${_lemon_qt_root}/plugins")
        endif()

        set(LEMON_LINUX_QT_VERSION "${${LEMON_QT_LIBNAME}_VERSION}")
    endif()
endif()

set(LEMON_PORTABLE_PACKAGE_NAME "LemonLime-${VERSION_STRING}-linux-qt6-${LEMON_LINUX_ARCH}")

if(NOT ENABLE_LINUX_PORTABLE_PACKAGE)
    # nothing to do
else()
    add_custom_target(linux-portable
        COMMAND ${CMAKE_COMMAND}
            "-DLEMON_PACKAGE_NAME=${LEMON_PORTABLE_PACKAGE_NAME}"
            "-DLEMON_PORTABLE_ROOT=${CMAKE_BINARY_DIR}/portable"
            "-DLEMON_OUTPUT_DIR=${CMAKE_BINARY_DIR}/portable/${LEMON_PORTABLE_PACKAGE_NAME}"
            "-DLEMON_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DLEMON_EXECUTABLE=$<TARGET_FILE:lemon>"
            "-DLEMON_QT_PLUGIN_DIR=${LEMON_LINUX_QT_PLUGIN_DIR}"
            "-DLEMON_QT_VERSION=${LEMON_LINUX_QT_VERSION}"
            "-DLEMON_MAKE_TARGZ=${LEMON_LINUX_PORTABLE_TARGZ}"
            "-DLEMON_ONLINE_SERVER=${ENABLE_ONLINE_SERVER}"
            "-DLEMON_ARCH=${LEMON_LINUX_ARCH}"
            "-DLEMON_VERSION=${VERSION_STRING}"
            "-DLEMON_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DLEMON_BUILD_VERSION=${BUILD_VERSION}"
            -P "${CMAKE_SOURCE_DIR}/cmake/deploy-linux.cmake"
        DEPENDS lemon
        COMMENT "Assembling portable Linux package ${LEMON_PORTABLE_PACKAGE_NAME}"
        VERBATIM
        USES_TERMINAL)
endif()
