add_library(goggles_core INTERFACE)
target_include_directories(goggles_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(goggles_core INTERFACE nonstd::expected-lite)
