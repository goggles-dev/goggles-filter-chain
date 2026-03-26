# stb_image_write in an isolated TU, excluded from clang-tidy.
add_library(fc_cli_stb_image_write_obj OBJECT stb_image_write_impl.cpp)
target_include_directories(fc_cli_stb_image_write_obj SYSTEM PRIVATE ${STB_IMAGE_INCLUDE_DIR})

add_executable(goggles-chain-cli
    main.cpp
    vulkan_backend.cpp
    image_io.cpp
    cmd_validate.cpp
    cmd_diagnose.cpp
    cmd_assert.cpp
    cmd_capture.cpp
    $<TARGET_OBJECTS:fc_cli_stb_image_write_obj>
)

target_include_directories(goggles-chain-cli SYSTEM PRIVATE ${STB_IMAGE_INCLUDE_DIR})
target_include_directories(goggles-chain-cli PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(goggles-chain-cli PRIVATE
    VULKAN_HPP_NO_EXCEPTIONS
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

target_link_libraries(goggles-chain-cli PRIVATE
    goggles-filter-chain
    Vulkan::Vulkan
)

goggles_enable_clang_tidy(goggles-chain-cli)
goggles_enable_sanitizers(goggles-chain-cli)
