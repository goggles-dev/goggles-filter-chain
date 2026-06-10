# Self-contained SHARED packaging: $ORIGIN RUNPATH + opt-in runtime-dep bundling.
#
# The gamescope consumer builds the SHARED library inside its own build tree and
# runs straight from there, so private deps (slang family, spdlog, fmt) must be
# resolvable relative to the .so without LD_LIBRARY_PATH. We give the artifact a
# `$ORIGIN` RUNPATH and, when bundling is requested, copy those deps next to it.
#
# We never bundle the C/C++ runtime or libvulkan: putting the toolchain's
# libstdc++ ahead of the system one is a known breakage.

# Stamp `$ORIGIN` RUNPATH onto a SHARED filter-chain target so colocated deps
# resolve at runtime. No-op for STATIC.
function(goggles_fc_apply_origin_runpath target)
    get_target_property(_fc_type ${target} TYPE)
    if(NOT _fc_type STREQUAL "SHARED_LIBRARY")
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
endfunction()

# Directory holding an imported target's resolved library file.
function(_goggles_fc_imported_dir target out_var)
    set(_loc "")
    get_target_property(_imported_configs ${target} IMPORTED_CONFIGURATIONS)
    if(_imported_configs)
        foreach(_cfg IN LISTS _imported_configs)
            get_target_property(_loc ${target} IMPORTED_LOCATION_${_cfg})
            if(_loc)
                break()
            endif()
        endforeach()
    endif()
    if(NOT _loc)
        get_target_property(_loc ${target} IMPORTED_LOCATION)
    endif()
    if(NOT _loc)
        message(FATAL_ERROR
            "GOGGLES_FC_BUNDLE_RUNTIME_DEPS: cannot resolve location of imported target '${target}'.")
    endif()
    get_filename_component(_dir "${_loc}" DIRECTORY)
    set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

# Copy the private runtime deps of a SHARED filter-chain target next to the
# built .so. Errors politely for STATIC. Resolves source dirs from the imported
# targets, never from hardcoded prefixes.
function(goggles_fc_bundle_runtime_deps target)
    get_target_property(_fc_type ${target} TYPE)
    if(NOT _fc_type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR
            "GOGGLES_FC_BUNDLE_RUNTIME_DEPS=ON requires FILTER_CHAIN_LIBRARY_TYPE=SHARED.")
    endif()

    # slang dlopens its siblings (glslang, glsl-module, llvm, rt) at
    # shader-compile time, so bundle the whole libslang* family, not just the
    # DT_NEEDED entry.
    _goggles_fc_imported_dir(slang::slang _slang_dir)
    _goggles_fc_imported_dir(spdlog::spdlog _spdlog_dir)

    # Constraint: globs are resolved at configure time and stale copies are
    # never removed — rebuild from a clean dir after upgrading slang/spdlog/fmt.
    file(GLOB _slang_libs "${_slang_dir}/libslang*.so*")
    file(GLOB _spdlog_libs "${_spdlog_dir}/libspdlog.so*")

    # fmt::fmt only exists when spdlog was built against external fmt
    # (SPDLOG_FMT_EXTERNAL); bundled fmt needs no runtime copy.
    set(_fmt_libs "")
    if(TARGET fmt::fmt)
        _goggles_fc_imported_dir(fmt::fmt _fmt_dir)
        file(GLOB _fmt_libs "${_fmt_dir}/libfmt.so*")
    endif()

    # Unquoted list expansion drops empty family globs (e.g. header-only fmt).
    set(_deps ${_slang_libs} ${_spdlog_libs} ${_fmt_libs})
    if(NOT _deps)
        message(FATAL_ERROR
            "GOGGLES_FC_BUNDLE_RUNTIME_DEPS: no runtime deps resolved next to slang/spdlog/fmt.")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_deps} "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Bundling filter-chain runtime deps (slang family, spdlog, fmt) next to $<TARGET_FILE_NAME:${target}>"
        VERBATIM)
endfunction()
