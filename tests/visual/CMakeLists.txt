add_library(fc_test_stb_image_obj OBJECT stb_image_impl.cpp)
target_include_directories(fc_test_stb_image_obj SYSTEM PRIVATE ${STB_IMAGE_INCLUDE_DIR})

add_library(fc_test_stb_image_write_obj OBJECT stb_image_write_impl.cpp)
target_include_directories(fc_test_stb_image_write_obj SYSTEM PRIVATE ${STB_IMAGE_INCLUDE_DIR})

add_library(fc_visual_support STATIC
    image_compare.cpp
    runtime_capture.cpp
)
target_sources(fc_visual_support PRIVATE
    $<TARGET_OBJECTS:fc_test_stb_image_obj>
    $<TARGET_OBJECTS:fc_test_stb_image_write_obj>
)
target_include_directories(fc_visual_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fc_visual_support PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_include_directories(fc_visual_support SYSTEM PRIVATE ${STB_IMAGE_INCLUDE_DIR})
target_link_libraries(fc_visual_support PUBLIC goggles-filter-chain)
target_compile_features(fc_visual_support PRIVATE cxx_std_20)
goggles_enable_sanitizers(fc_visual_support)

function(add_fc_visual_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE
        fc_visual_support
        Catch2::Catch2WithMain
    )
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
    target_compile_definitions(${target_name} PRIVATE
        FILTER_CHAIN_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
        FC_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden"
    )
    goggles_enable_sanitizers(${target_name})
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
        SKIP_RETURN_CODE 4
        TIMEOUT 120
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

add_fc_visual_test(fc_test_temporal_golden test_temporal_golden.cpp)
add_fc_visual_test(fc_test_semantic_probes test_semantic_probes.cpp)

add_executable(fc_visual_capture runtime_capture_main.cpp)
target_link_libraries(fc_visual_capture PRIVATE fc_visual_support)
target_compile_features(fc_visual_capture PRIVATE cxx_std_20)
target_include_directories(fc_visual_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_compile_definitions(fc_visual_capture PRIVATE
    FILTER_CHAIN_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
)
goggles_enable_sanitizers(fc_visual_capture)
