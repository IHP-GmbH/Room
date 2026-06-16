# Build third_party/capnp-install at configure time when missing.

function(core_bootstrap_capnp core_root capnp_root capnp_include)
    if(NOT CORE_BOOTSTRAP_CAPNP)
        return()
    endif()

    if(EXISTS "${capnp_include}/capnp/message.h")
        return()
    endif()

    message(STATUS "Cap'n Proto not found in ${capnp_root}")
    message(STATUS "Bootstrapping Cap'n Proto (first configure may take several minutes)...")

    set(_env_args "")
    set(_path_prefix "")

    if(CMAKE_MAKE_PROGRAM)
        get_filename_component(_tool_bin_dir "${CMAKE_MAKE_PROGRAM}" DIRECTORY)
        file(TO_NATIVE_PATH "${_tool_bin_dir}" _tool_bin_dir)
        set(_path_prefix "${_tool_bin_dir}")
    endif()

    if(CMAKE_C_COMPILER)
        get_filename_component(_cc_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        file(TO_NATIVE_PATH "${_cc_bin_dir}" _cc_bin_dir)
        if(_path_prefix)
            if(WIN32)
                set(_path_prefix "${_path_prefix};${_cc_bin_dir}")
            else()
                set(_path_prefix "${_path_prefix}:${_cc_bin_dir}")
            endif()
        else()
            set(_path_prefix "${_cc_bin_dir}")
        endif()
    endif()

    if(_path_prefix)
        if(WIN32)
            set(_env_args "PATH=${_path_prefix}\;$ENV{PATH}")
        else()
            set(_env_args "PATH=${_path_prefix}:$ENV{PATH}")
        endif()
    endif()

    if(WIN32)
        set(_bootstrap_cmd cmd /c "${core_root}/scripts/mkcapnp.cmd")
    else()
        set(_bootstrap_cmd bash "${core_root}/scripts/mkcapnp.sh")
    endif()

    if(_env_args)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env "${_env_args}" ${_bootstrap_cmd}
            WORKING_DIRECTORY "${core_root}"
            RESULT_VARIABLE _capnp_bootstrap_result
            OUTPUT_VARIABLE _capnp_bootstrap_out
            ERROR_VARIABLE _capnp_bootstrap_err
        )
    else()
        execute_process(
            COMMAND ${_bootstrap_cmd}
            WORKING_DIRECTORY "${core_root}"
            RESULT_VARIABLE _capnp_bootstrap_result
            OUTPUT_VARIABLE _capnp_bootstrap_out
            ERROR_VARIABLE _capnp_bootstrap_err
        )
    endif()

    if(_capnp_bootstrap_result)
        message("${_capnp_bootstrap_out}")
        message("${_capnp_bootstrap_err}")
        message(FATAL_ERROR
            "Failed to bootstrap Cap'n Proto (exit ${_capnp_bootstrap_result}).\n"
            "Ensure Git and your C++ toolchain (MinGW make/gcc) are on PATH, or run manually:\n"
            "  Windows: scripts/mkcapnp.cmd\n"
            "  Linux:   ./scripts/mkcapnp.sh\n"
            "See third_party/README.md.")
    endif()

    if(NOT EXISTS "${capnp_include}/capnp/message.h")
        message(FATAL_ERROR
            "Cap'n Proto bootstrap finished but ${capnp_include}/capnp/message.h is missing.\n"
            "Check the output above or build manually (scripts/mkcapnp.cmd).")
    endif()

    message(STATUS "Cap'n Proto ready at ${capnp_root}")
endfunction()
