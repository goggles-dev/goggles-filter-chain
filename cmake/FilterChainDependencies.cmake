include(CMakeFindDependencyMacro)

function(goggles_filter_chain_find_slang_dependency)
    find_file(GOGGLES_FILTER_CHAIN_SLANG_CONFIG_FILE
        NAMES slangConfig.cmake slang-config.cmake
        PATH_SUFFIXES lib/cmake/slang lib/cmake/Slang share/cmake/slang share/cmake/Slang)
    if(GOGGLES_FILTER_CHAIN_SLANG_CONFIG_FILE)
        get_filename_component(_goggles_filter_chain_slang_config_dir
            "${GOGGLES_FILTER_CHAIN_SLANG_CONFIG_FILE}" DIRECTORY)
        set(slang_DIR "${_goggles_filter_chain_slang_config_dir}")
        find_dependency(slang CONFIG REQUIRED)
        unset(_goggles_filter_chain_slang_config_dir)
        return()
    endif()

    find_library(GOGGLES_FILTER_CHAIN_SLANG_LIBRARY
        NAMES slang libslang
        REQUIRED)
    find_path(GOGGLES_FILTER_CHAIN_SLANG_INCLUDE_DIR
        NAMES slang.h
        REQUIRED)

    if(NOT TARGET slang::slang)
        add_library(slang::slang UNKNOWN IMPORTED)
        set_target_properties(slang::slang PROPERTIES
            IMPORTED_LOCATION "${GOGGLES_FILTER_CHAIN_SLANG_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GOGGLES_FILTER_CHAIN_SLANG_INCLUDE_DIR}")
    endif()
endfunction()

find_dependency(Vulkan REQUIRED)
find_dependency(expected-lite CONFIG REQUIRED)

if(NOT DEFINED GOGGLES_FILTER_CHAIN_FIND_PRIVATE_DEPS)
    set(GOGGLES_FILTER_CHAIN_FIND_PRIVATE_DEPS OFF)
endif()

if(GOGGLES_FILTER_CHAIN_FIND_PRIVATE_DEPS)
    find_dependency(spdlog CONFIG REQUIRED)
    goggles_filter_chain_find_slang_dependency()

    find_path(STB_IMAGE_INCLUDE_DIR
        NAMES stb_image.h
        PATH_SUFFIXES stb
        REQUIRED)
endif()

if(FILTER_CHAIN_BUILD_TESTS AND GOGGLES_FILTER_CHAIN_FIND_PRIVATE_DEPS)
    find_dependency(Catch2 CONFIG REQUIRED)
endif()
