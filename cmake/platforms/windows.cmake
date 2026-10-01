# ==================================================================================
# Windows: self-contained portable ("green") package
# ==================================================================================
#
#   cmake --build <build-dir> --target windows-portable
#
# assembles <build-dir>/portable/lemon-<version>-Windows-<arch>-portable/ and a
# .zip of it. The folder holds lemon.exe, the Qt runtime (windeployqt), the MSVC
# runtime DLLs and a `portable` marker file, so settings/logs stay inside the
# folder and nothing needs to be installed on the target machine.

install(TARGETS lemon RUNTIME DESTINATION .)

option(ENABLE_WINDOWS_PORTABLE_PACKAGE "Create the windows-portable packaging target" ON)
option(LEMON_WINDOWS_PORTABLE_ZIP "Compress the portable folder into a .zip" ON)
option(LEMON_WINDOWS_PORTABLE_OPENGL_SW "Ship the 20 MB software OpenGL fallback (remote desktop, VMs, old GPUs)" ON)
set(LEMON_WINDOWS_PORTABLE_BUNDLE_COMPILER "" CACHE PATH
    "Optional MinGW-w64 root directory (the one containing bin/g++.exe) to bundle for judging")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(LEMON_WINDOWS_ARCH "x64")
else()
    set(LEMON_WINDOWS_ARCH "x86")
endif()

if(TARGET ${LEMON_QT_LIBNAME}::qmake)
    get_target_property(_lemon_qmake ${LEMON_QT_LIBNAME}::qmake IMPORTED_LOCATION)
    get_filename_component(_lemon_qt_bin "${_lemon_qmake}" DIRECTORY)
endif()

find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS "${_lemon_qt_bin}")

set(LEMON_PORTABLE_PACKAGE_NAME "lemon-${VERSION_STRING}-Windows-${LEMON_WINDOWS_ARCH}-portable")

if(NOT ENABLE_WINDOWS_PORTABLE_PACKAGE)
    # nothing to do
elseif(NOT WINDEPLOYQT_EXECUTABLE)
    message(WARNING "windeployqt was not found - the windows-portable target is disabled")
else()
    add_custom_target(windows-portable
        COMMAND ${CMAKE_COMMAND}
            "-DLEMON_PACKAGE_NAME=${LEMON_PORTABLE_PACKAGE_NAME}"
            "-DLEMON_PORTABLE_ROOT=${CMAKE_BINARY_DIR}/portable"
            "-DLEMON_OUTPUT_DIR=${CMAKE_BINARY_DIR}/portable/${LEMON_PORTABLE_PACKAGE_NAME}"
            "-DLEMON_EXECUTABLE=$<TARGET_FILE:lemon>"
            "-DLEMON_WINDEPLOYQT=${WINDEPLOYQT_EXECUTABLE}"
            "-DLEMON_VCTOOLS_REDIST_DIR=$ENV{VCToolsRedistDir}"
            "-DLEMON_BUNDLE_COMPILER=${LEMON_WINDOWS_PORTABLE_BUNDLE_COMPILER}"
            "-DLEMON_OPENGL_SW=${LEMON_WINDOWS_PORTABLE_OPENGL_SW}"
            "-DLEMON_MAKE_ZIP=${LEMON_WINDOWS_PORTABLE_ZIP}"
            "-DLEMON_README=${CMAKE_SOURCE_DIR}/assets/windows-portable-readme.txt"
            "-DLEMON_ONLINE_SERVER=${ENABLE_ONLINE_SERVER}"
            -P "${CMAKE_SOURCE_DIR}/cmake/deploy-windows.cmake"
        DEPENDS lemon
        COMMENT "Assembling portable Windows package ${LEMON_PORTABLE_PACKAGE_NAME}"
        VERBATIM
        USES_TERMINAL)
endif()
