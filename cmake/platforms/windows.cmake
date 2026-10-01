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

# Ninja only: hand the dependency database to the packaging script, which refuses
# to package a tree that recorded no header dependencies (see the probe below).
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(LEMON_PORTABLE_DEP_CHECK_ARGS
        "-DLEMON_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DLEMON_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
else()
    set(LEMON_PORTABLE_DEP_CHECK_ARGS "")
endif()

if(NOT ENABLE_WINDOWS_PORTABLE_PACKAGE)
    # nothing to do
elseif(NOT WINDEPLOYQT_EXECUTABLE)
    message(WARNING "windeployqt was not found - the windows-portable target is disabled")
else()
    # --------------------------------------------------------------------------
    # Ninja + MSVC: the /showIncludes prefix must round-trip byte for byte
    # --------------------------------------------------------------------------
    # Ninja records a header dependency only when the bytes CMake stored in
    # msvc_deps_prefix (CMakeFiles/rules.ninja) equal the bytes cl.exe prints.
    # cl.exe encodes that note in the console code page (GBK on a zh-CN system,
    # UTF-8 after `chcp 65001`), so a tree configured under the wrong code page
    # silently records *no* header dependencies: `ninja -t deps` reports
    # `#deps 0` for every object, stale objects survive header edits, and object
    # files built against different header versions are linked together. That is
    # how a shipped portable package ended up with mismatched struct layouts and
    # crashed with intermittent heap corruption (0xC0000374) far from the cause.
    # This probe warns early; cmake/deploy-windows.cmake holds the hard gate.
    if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CXX_CL_SHOWINCLUDES_PREFIX)
        set(_lemon_showincludes_dir "${CMAKE_BINARY_DIR}/CMakeFiles/lemon-showincludes-probe")
        file(MAKE_DIRECTORY "${_lemon_showincludes_dir}")
        file(WRITE "${_lemon_showincludes_dir}/probe.h" "#define LEMON_SHOWINCLUDES_PROBE 1\n")
        file(WRITE "${_lemon_showincludes_dir}/probe.cpp" "#include \"probe.h\"\nint main() { return LEMON_SHOWINCLUDES_PROBE; }\n")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c "probe.cpp"
                    "/Fo${_lemon_showincludes_dir}/probe.obj"
            WORKING_DIRECTORY "${_lemon_showincludes_dir}"
            OUTPUT_FILE "${_lemon_showincludes_dir}/probe.out"
            ERROR_QUIET
            RESULT_VARIABLE _lemon_showincludes_status)
        if(_lemon_showincludes_status EQUAL 0)
            file(READ "${_lemon_showincludes_dir}/probe.out" _lemon_showincludes_out HEX)
            string(HEX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}" _lemon_showincludes_prefix)
            string(TOLOWER "${_lemon_showincludes_out}" _lemon_showincludes_out)
            string(TOLOWER "${_lemon_showincludes_prefix}" _lemon_showincludes_prefix)
            string(FIND "${_lemon_showincludes_out}" "${_lemon_showincludes_prefix}" _lemon_showincludes_pos)
            if(_lemon_showincludes_pos EQUAL -1)
                message(WARNING
                    "header dependency tracking is broken in this build tree: the recorded "
                    "/showIncludes prefix does not match what cl.exe prints, so Ninja will record "
                    "no header dependencies at all. Editing a header can then leave stale object "
                    "files in the build and link a mismatched binary. Fix: delete this build tree, "
                    "then configure and build in one console where `chcp 65001` has been run.")
            endif()
        endif()
    endif()
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
            ${LEMON_PORTABLE_DEP_CHECK_ARGS}
            -P "${CMAKE_SOURCE_DIR}/cmake/deploy-windows.cmake"
        DEPENDS lemon
        COMMENT "Assembling portable Windows package ${LEMON_PORTABLE_PACKAGE_NAME}"
        VERBATIM
        USES_TERMINAL)
endif()
