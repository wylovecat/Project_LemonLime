# ==================================================================================
# Build-time helper for the `windows-portable` target (cmake -P script)
# ==================================================================================
#
# Assembles a self-contained LemonLime folder:
#   lemon.exe + Qt runtime/plugins/translations (windeployqt)
#   + MSVC runtime DLLs (so no vc_redist install is needed)
#   + a `portable` marker (settings/logs stay inside the folder)
#   + optional bundled MinGW-w64 toolchain
# and, unless disabled, a .zip of the result.

cmake_minimum_required(VERSION 3.16)

function(lemon_step text)
    message(STATUS "[lemon-portable] ${text}")
endfunction()

if(NOT LEMON_EXECUTABLE OR NOT EXISTS "${LEMON_EXECUTABLE}")
    message(FATAL_ERROR "[lemon-portable] lemon.exe not found at '${LEMON_EXECUTABLE}'")
endif()

if(NOT LEMON_WINDEPLOYQT)
    message(FATAL_ERROR "[lemon-portable] windeployqt is not available")
endif()

# ----------------------------------------------------------------------------------
# 0b. Header dependency gate
# ----------------------------------------------------------------------------------
# Refuse to package a build tree that recorded no header dependencies: such a tree
# links object files compiled against different header versions, which is exactly how
# a portable package shipped with mismatched struct layouts and crashed (0xC0000374).
if(NOT LEMON_BUILD_DIR OR NOT LEMON_MAKE_PROGRAM)
    lemon_step("dependency check: skipped (no Ninja build tree given)")
else()
    execute_process(
        COMMAND "${LEMON_MAKE_PROGRAM}" -C "${LEMON_BUILD_DIR}" -t deps
        RESULT_VARIABLE _lemon_deps_status
        OUTPUT_VARIABLE _lemon_deps_output
        ERROR_QUIET)
    if(NOT _lemon_deps_status EQUAL 0)
        lemon_step("dependency check: skipped (ninja -t deps failed)")
    else()
        foreach(_lemon_deps_obj "lemon.cpp.obj" "onlineserverdialog.cpp.obj" "main.cpp.obj")
            string(REGEX MATCH "${_lemon_deps_obj}: #deps ([0-9]+)," _lemon_deps_match "${_lemon_deps_output}")
            if(NOT _lemon_deps_match)
                lemon_step("dependency check: no database entry for ${_lemon_deps_obj}")
            elseif(CMAKE_MATCH_1 EQUAL 0)
                message(FATAL_ERROR
                    "[lemon-portable] ${_lemon_deps_obj} recorded no header dependencies (#deps 0). "
                    "This build tree mixes object files compiled against different header versions; "
                    "delete it and configure/build again in a console where `chcp 65001` has been run "
                    "(configure and build must share that console).")
            else()
                lemon_step("dependency check: ${_lemon_deps_obj} -> ${CMAKE_MATCH_1} headers")
            endif()
        endforeach()
        string(REGEX MATCHALL ": #deps [0-9]+," _lemon_deps_all "${_lemon_deps_output}")
        string(REGEX MATCHALL ": #deps 0," _lemon_deps_zero "${_lemon_deps_output}")
        list(LENGTH _lemon_deps_all _lemon_deps_all_count)
        list(LENGTH _lemon_deps_zero _lemon_deps_zero_count)
        if(_lemon_deps_all_count GREATER 20 AND _lemon_deps_zero_count GREATER 0)
            math(EXPR _lemon_deps_zero_percent "${_lemon_deps_zero_count} * 100 / ${_lemon_deps_all_count}")
            lemon_step("dependency check: ${_lemon_deps_zero_count} of ${_lemon_deps_all_count} objects without header dependencies (${_lemon_deps_zero_percent}%)")
            if(_lemon_deps_zero_percent GREATER 50)
                message(FATAL_ERROR
                    "[lemon-portable] ${_lemon_deps_zero_count} of ${_lemon_deps_all_count} object files "
                    "recorded no header dependencies (${_lemon_deps_zero_percent}%): this build tree is "
                    "not safe to package. Delete it and rebuild after `chcp 65001`.")
            endif()
        endif()
    endif()
endif()

lemon_step("assembling ${LEMON_PACKAGE_NAME}")

file(REMOVE_RECURSE "${LEMON_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${LEMON_OUTPUT_DIR}")
file(COPY "${LEMON_EXECUTABLE}" DESTINATION "${LEMON_OUTPUT_DIR}")

# ----------------------------------------------------------------------------------
# 1. Qt runtime, plugins and Qt's own translations
# ----------------------------------------------------------------------------------
set(_windeploy_args --release --no-compiler-runtime --verbose 1)

if(NOT LEMON_OPENGL_SW)
    list(APPEND _windeploy_args --no-opengl-sw)
endif()

execute_process(
    COMMAND "${LEMON_WINDEPLOYQT}" ${_windeploy_args} "${LEMON_OUTPUT_DIR}/lemon.exe"
    WORKING_DIRECTORY "${LEMON_OUTPUT_DIR}"
    RESULT_VARIABLE _windeploy_result
    OUTPUT_VARIABLE _windeploy_output
    ERROR_VARIABLE _windeploy_error)

if(NOT _windeploy_result EQUAL 0)
    message(FATAL_ERROR "[lemon-portable] windeployqt failed (${_windeploy_result}):\n${_windeploy_output}\n${_windeploy_error}")
endif()

# ----------------------------------------------------------------------------------
# 2. MSVC runtime
# ----------------------------------------------------------------------------------
# windeployqt only drops vc_redist.x64.exe next to the binary; copying the CRT DLLs
# themselves is what makes the folder runnable on a machine that never had a Visual
# Studio redistributable installed.
set(_crt_dlls
    msvcp140.dll
    msvcp140_1.dll
    msvcp140_2.dll
    msvcp140_atomic_wait.dll
    msvcp140_codecvt_ids.dll
    vcruntime140.dll
    vcruntime140_1.dll
    concrt140.dll)

set(_crt_candidates "")

foreach(_env_dir "${LEMON_VCTOOLS_REDIST_DIR}" "$ENV{VCToolsRedistDir}" "$ENV{VCINSTALLDIR}/Redist/MSVC")
    if(_env_dir AND IS_DIRECTORY "${_env_dir}")
        file(GLOB _found_msvc "${_env_dir}/x64/Microsoft.VC*.CRT" "${_env_dir}/*/x64/Microsoft.VC*.CRT")
        list(APPEND _crt_candidates ${_found_msvc})
    endif()
endforeach()

file(GLOB _found_vs
    "C:/Program Files/Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
    "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT")

list(APPEND _crt_candidates ${_found_vs})
list(REMOVE_DUPLICATES _crt_candidates)
list(SORT _crt_candidates)

set(_crt_source "")
set(_crt_missing "")

foreach(_dir ${_crt_candidates})
    # candidates are sorted, so the last usable one is the newest toolset
    if(EXISTS "${_dir}/msvcp140.dll" AND EXISTS "${_dir}/vcruntime140.dll")
        set(_crt_source "${_dir}")
    endif()
endforeach()

if(_crt_source)
    lemon_step("MSVC runtime from ${_crt_source}")

    foreach(_dll ${_crt_dlls})
        if(EXISTS "${_crt_source}/${_dll}")
            file(COPY "${_crt_source}/${_dll}" DESTINATION "${LEMON_OUTPUT_DIR}")
        else()
            list(APPEND _crt_missing ${_dll})
        endif()
    endforeach()
else()
    message(WARNING "[lemon-portable] no MSVC redistributable directory was found; the package will "
                    "require the Visual C++ 2015-2022 x64 runtime on the target machine")
endif()

if(_crt_missing)
    lemon_step("not shipped (not part of this toolset): ${_crt_missing}")
endif()

# windeployqt's own copy of the installer is pointless once the DLLs are in place
file(GLOB _vc_redist "${LEMON_OUTPUT_DIR}/vc_redist*.exe")

if(_vc_redist)
    file(REMOVE ${_vc_redist})
endif()

# ----------------------------------------------------------------------------------
# 3. Optional bundled MinGW-w64 (needed to compile the contestants' code)
# ----------------------------------------------------------------------------------
if(LEMON_BUNDLE_COMPILER)
    if(EXISTS "${LEMON_BUNDLE_COMPILER}/bin/g++.exe")
        lemon_step("bundling toolchain from ${LEMON_BUNDLE_COMPILER}")
        file(COPY "${LEMON_BUNDLE_COMPILER}/" DESTINATION "${LEMON_OUTPUT_DIR}/compiler/mingw64")
    else()
        message(WARNING "[lemon-portable] '${LEMON_BUNDLE_COMPILER}/bin/g++.exe' does not exist, "
                        "no compiler is bundled")
    endif()
endif()

# ----------------------------------------------------------------------------------
# 4. Portable marker + user documentation
# ----------------------------------------------------------------------------------
file(WRITE "${LEMON_OUTPUT_DIR}/portable"
    "LemonLime portable mode\nSettings: LemonLime/lemon.ini\nLogs: logs/\n")

if(EXISTS "${LEMON_README}")
    file(COPY "${LEMON_README}" DESTINATION "${LEMON_OUTPUT_DIR}")
    file(RENAME "${LEMON_OUTPUT_DIR}/windows-portable-readme.txt" "${LEMON_OUTPUT_DIR}/README.txt")
endif()

# ----------------------------------------------------------------------------------
# 5. Sanity check
# ----------------------------------------------------------------------------------
set(_required lemon.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll)

if(LEMON_ONLINE_SERVER)
    list(APPEND _required Qt6HttpServer.dll)
endif()

foreach(_required_dll ${_required})
    if(NOT EXISTS "${LEMON_OUTPUT_DIR}/${_required_dll}")
        message(WARNING "[lemon-portable] ${_required_dll} is missing from the package")
    endif()
endforeach()

file(GLOB_RECURSE _packaged_files RELATIVE "${LEMON_OUTPUT_DIR}" "${LEMON_OUTPUT_DIR}/*")
list(LENGTH _packaged_files _packaged_count)

set(_packaged_bytes 0)

foreach(_file ${_packaged_files})
    file(SIZE "${LEMON_OUTPUT_DIR}/${_file}" _file_bytes)
    math(EXPR _packaged_bytes "${_packaged_bytes} + ${_file_bytes}")
endforeach()

math(EXPR _packaged_mib "${_packaged_bytes} / 1048576")
lemon_step("package folder: ${LEMON_OUTPUT_DIR}")
lemon_step("contents: ${_packaged_count} files, about ${_packaged_mib} MiB")

# ----------------------------------------------------------------------------------
# 6. Zip archive
# ----------------------------------------------------------------------------------
if(LEMON_MAKE_ZIP)
    file(REMOVE "${LEMON_PORTABLE_ROOT}/${LEMON_PACKAGE_NAME}.zip")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${LEMON_PORTABLE_ROOT}/${LEMON_PACKAGE_NAME}.zip"
                --format=zip "${LEMON_PACKAGE_NAME}"
        WORKING_DIRECTORY "${LEMON_PORTABLE_ROOT}"
        RESULT_VARIABLE _zip_result
        OUTPUT_VARIABLE _zip_output
        ERROR_VARIABLE _zip_error)

    if(NOT _zip_result EQUAL 0)
        message(WARNING "[lemon-portable] zipping failed (${_zip_result}): ${_zip_output} ${_zip_error}")
    else()
        file(SIZE "${LEMON_PORTABLE_ROOT}/${LEMON_PACKAGE_NAME}.zip" _zip_bytes)
        math(EXPR _zip_mib "${_zip_bytes} / 1048576")
        lemon_step("archive: ${LEMON_PORTABLE_ROOT}/${LEMON_PACKAGE_NAME}.zip (${_zip_mib} MiB)")
    endif()
endif()
