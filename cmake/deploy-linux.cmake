# ==================================================================================
# Build-time helper for the `linux-portable` target (cmake -P script)
# ==================================================================================
#
# Assembles a self-contained LemonLime folder:
#   bin/lemon + the Qt runtime libraries it links against
#   + Qt's plugin directories (platform, image format, icon, TLS, ...)
#   + bin/qt.conf pointing Qt at the bundled plugins
#   + the regular install tree (icons, desktop file, metainfo, testlib.h)
# and, unless disabled, a .tar.gz of the result plus its .sha256.
#
# The installed binary carries RUNPATH $ORIGIN/../lib, so it resolves Qt from the
# bundle's own lib/ directory instead of the Qt installation it was built against.
# That RUNPATH comes from CMAKE_INSTALL_RPATH at configure time; this script
# refuses to package a tree configured without it, because the result would look
# fine on the build machine and fail on every other one.

cmake_minimum_required(VERSION 3.16)

function(lemon_step text)
    message(STATUS "[lemon-portable] ${text}")
endfunction()

set(_expected_rpath [=[$ORIGIN/../lib]=])

# The plugin subdirectories worth shipping: svg icons, xcb/wayland platform
# plugins, image formats, TLS, network status, themes.
set(_plugin_dirs
    platforms imageformats iconengines tls networkinformation platformthemes
    xcbglintegrations generic platforminputcontexts
    wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration
    egldeviceintegrations)

if(NOT LEMON_EXECUTABLE OR NOT EXISTS "${LEMON_EXECUTABLE}")
    message(FATAL_ERROR "[lemon-portable] lemon not found at '${LEMON_EXECUTABLE}'")
endif()

lemon_step("assembling ${LEMON_PACKAGE_NAME}")

file(REMOVE_RECURSE "${LEMON_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${LEMON_OUTPUT_DIR}")

# ----------------------------------------------------------------------------------
# 1. Install tree
# ----------------------------------------------------------------------------------
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${LEMON_BUILD_DIR}" --prefix "${LEMON_OUTPUT_DIR}"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error)

if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "[lemon-portable] cmake --install failed (${_install_result}):\n${_install_output}\n${_install_error}")
endif()

set(_installed_binary "${LEMON_OUTPUT_DIR}/bin/lemon")

if(NOT EXISTS "${_installed_binary}")
    message(FATAL_ERROR "[lemon-portable] ${_installed_binary} is missing after install")
endif()

# ----------------------------------------------------------------------------------
# 2. RUNPATH gate
# ----------------------------------------------------------------------------------
execute_process(
    COMMAND readelf -d "${_installed_binary}"
    RESULT_VARIABLE _readelf_status
    OUTPUT_VARIABLE _readelf_out
    ERROR_QUIET)

if(_readelf_status EQUAL 0)
    string(REGEX MATCH "(RUNPATH|RPATH)[^[]*\\[([^]]*)\\]" _rpath_match "${_readelf_out}")
    set(_installed_rpath "${CMAKE_MATCH_2}")

    if(NOT _installed_rpath STREQUAL _expected_rpath)
        message(FATAL_ERROR
            "[lemon-portable] the installed binary has RUNPATH '${_installed_rpath}', "
            "expected '${_expected_rpath}'.\n"
            "Configure the build tree with -DCMAKE_INSTALL_RPATH='\$ORIGIN/../lib' "
            "(quoted, so the shell does not swallow the dollar sign); without it the "
            "package resolves Qt from the Qt installation and is not portable.")
    endif()

    lemon_step("RUNPATH: ${_installed_rpath}")
else()
    message(WARNING "[lemon-portable] readelf is unavailable; the RUNPATH could not be verified")
endif()

# ----------------------------------------------------------------------------------
# 3. Qt runtime libraries
# ----------------------------------------------------------------------------------
# Enumerate the dependencies of the *build tree* binary: its RUNPATH still points at
# the Qt installation, so ldd resolves every Qt library to an absolute path. The
# installed binary cannot be used for this — its RUNPATH points at lib/, which is
# still empty at this point.
file(MAKE_DIRECTORY "${LEMON_OUTPUT_DIR}/lib")

execute_process(
    COMMAND ldd "${LEMON_EXECUTABLE}"
    RESULT_VARIABLE _ldd_status
    OUTPUT_VARIABLE _ldd_out
    ERROR_QUIET)

if(NOT _ldd_status EQUAL 0)
    message(FATAL_ERROR "[lemon-portable] ldd failed on '${LEMON_EXECUTABLE}'")
endif()

string(REPLACE "\n" ";" _ldd_lines "${_ldd_out}")
set(_bundled_libs 0)

foreach(_line ${_ldd_lines})
    # "libQt6Core.so.6 => /path/to/libQt6Core.so.6 (0x...)"
    if(NOT _line MATCHES "=> (/[^ ]+)")
        continue()
    endif()

    set(_lib "${CMAKE_MATCH_1}")

    # System libraries stay where they are: shipping libc/libstdc++/libGL inside the
    # bundle couples it to the build distribution and breaks on other machines.
    if(_lib MATCHES "linux-vdso|ld-linux|^/lib/|^/lib64/|^/usr/lib/")
        continue()
    endif()

    get_filename_component(_lib_name "${_lib}" NAME)

    if(EXISTS "${LEMON_OUTPUT_DIR}/lib/${_lib_name}")
        continue()
    endif()

    # -L: Qt ships libQt6X.so.6 as a symlink to the real .so.6.x.y. Copying the link
    # itself would leave it dangling and send the runtime back to the system Qt.
    execute_process(
        COMMAND cp -aL "${_lib}" "${LEMON_OUTPUT_DIR}/lib/${_lib_name}"
        RESULT_VARIABLE _cp_result
        ERROR_VARIABLE _cp_error)

    if(NOT _cp_result EQUAL 0)
        message(FATAL_ERROR "[lemon-portable] copying ${_lib} failed: ${_cp_error}")
    endif()

    math(EXPR _bundled_libs "${_bundled_libs} + 1")
    lemon_step("bundled ${_lib_name}")
endforeach()

if(_bundled_libs LESS 5)
    message(FATAL_ERROR
        "[lemon-portable] only ${_bundled_libs} libraries were bundled. The build tree "
        "binary's RUNPATH probably does not point at a Qt installation, so ldd resolved "
        "Qt from the system instead.")
endif()

# ----------------------------------------------------------------------------------
# 4. Qt plugins
# ----------------------------------------------------------------------------------
if(LEMON_QT_PLUGIN_DIR AND IS_DIRECTORY "${LEMON_QT_PLUGIN_DIR}")
    file(MAKE_DIRECTORY "${LEMON_OUTPUT_DIR}/plugins")
    set(_bundled_plugins 0)

    foreach(_plugin_dir ${_plugin_dirs})
        if(IS_DIRECTORY "${LEMON_QT_PLUGIN_DIR}/${_plugin_dir}")
            file(COPY "${LEMON_QT_PLUGIN_DIR}/${_plugin_dir}" DESTINATION "${LEMON_OUTPUT_DIR}/plugins")
            math(EXPR _bundled_plugins "${_bundled_plugins} + 1")
        endif()
    endforeach()

    lemon_step("plugins: ${_bundled_plugins} directories from ${LEMON_QT_PLUGIN_DIR}")
else()
    message(WARNING "[lemon-portable] Qt plugin directory '${LEMON_QT_PLUGIN_DIR}' does not exist; "
                    "the package will have no platform, image format or TLS plugins")
endif()

# ----------------------------------------------------------------------------------
# 5. qt.conf and user documentation
# ----------------------------------------------------------------------------------
file(WRITE "${LEMON_OUTPUT_DIR}/bin/qt.conf"
"[Paths]
Prefix = ..
Plugins = plugins
")

file(WRITE "${LEMON_OUTPUT_DIR}/README.txt"
"LemonLime ${LEMON_VERSION} (Linux ${LEMON_ARCH}, Qt ${LEMON_QT_VERSION})

运行：
    ./bin/lemon

本包自带 Qt 运行库与插件，解压即可运行，无需安装 Qt 或设环境变量。

运行依赖（系统提供）：
    bubblewrap (bwrap)  —— 评测沙箱，缺少它无法评测；Debian/Ubuntu: apt install bubblewrap
    编译器（如 g++）    —— 在“选项 → 编译器”中配置

构建版本：${LEMON_BUILD_VERSION}
构建类型：${LEMON_BUILD_TYPE}
")

# ----------------------------------------------------------------------------------
# 6. Sanity check
# ----------------------------------------------------------------------------------
execute_process(
    COMMAND ldd "${_installed_binary}"
    RESULT_VARIABLE _check_status
    OUTPUT_VARIABLE _check_out
    ERROR_QUIET)

if(_check_status EQUAL 0)
    if(_check_out MATCHES "not found")
        message(FATAL_ERROR "[lemon-portable] the packaged binary has unresolved dependencies:\n${_check_out}")
    endif()

    string(REPLACE "\n" ";" _check_lines "${_check_out}")
    foreach(_line ${_check_lines})
        if(_line MATCHES "libQt6" AND NOT _line MATCHES "=> ${LEMON_OUTPUT_DIR}")
            message(FATAL_ERROR
                "[lemon-portable] a Qt library resolves outside the package:\n${_line}\n"
                "The bundle would need the Qt installation it was built against.")
        endif()
    endforeach()

    lemon_step("every library resolves inside the package")
endif()

set(_required_libs libQt6Core.so.6 libQt6Gui.so.6 libQt6Widgets.so.6 libQt6Network.so.6)

if(LEMON_ONLINE_SERVER)
    list(APPEND _required_libs libQt6HttpServer.so.6)
endif()

foreach(_required_lib ${_required_libs})
    if(NOT EXISTS "${LEMON_OUTPUT_DIR}/lib/${_required_lib}")
        message(WARNING "[lemon-portable] ${_required_lib} is missing from the package")
    endif()
endforeach()

file(GLOB_RECURSE _packaged_files RELATIVE "${LEMON_OUTPUT_DIR}" "${LEMON_OUTPUT_DIR}/*")
list(LENGTH _packaged_files _packaged_count)
lemon_step("package folder: ${LEMON_OUTPUT_DIR}")
lemon_step("contents: ${_packaged_count} files")

# ----------------------------------------------------------------------------------
# 7. Archive and checksum
# ----------------------------------------------------------------------------------
if(LEMON_MAKE_TARGZ)
    set(_archive "${LEMON_PACKAGE_NAME}.tar.gz")
    file(REMOVE "${LEMON_PORTABLE_ROOT}/${_archive}")

    execute_process(
        COMMAND tar -czf "${_archive}" "${LEMON_PACKAGE_NAME}"
        WORKING_DIRECTORY "${LEMON_PORTABLE_ROOT}"
        RESULT_VARIABLE _tar_result
        ERROR_VARIABLE _tar_error)

    if(NOT _tar_result EQUAL 0)
        message(FATAL_ERROR "[lemon-portable] tar failed: ${_tar_error}")
    endif()

    execute_process(
        COMMAND sha256sum "${_archive}"
        WORKING_DIRECTORY "${LEMON_PORTABLE_ROOT}"
        RESULT_VARIABLE _sha_result
        OUTPUT_VARIABLE _sha_out
        ERROR_QUIET)

    if(NOT _sha_result EQUAL 0)
        message(FATAL_ERROR "[lemon-portable] sha256sum failed on ${_archive}")
    endif()

    file(WRITE "${LEMON_PORTABLE_ROOT}/${_archive}.sha256" "${_sha_out}")

    file(SIZE "${LEMON_PORTABLE_ROOT}/${_archive}" _archive_bytes)
    math(EXPR _archive_mib "${_archive_bytes} / 1048576")
    lemon_step("archive: ${LEMON_PORTABLE_ROOT}/${_archive} (${_archive_mib} MiB)")
    lemon_step("checksum: ${_sha_out}")
endif()
