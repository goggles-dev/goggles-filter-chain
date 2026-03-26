#pragma once

#include <vulkan/vulkan.h>

namespace goggles::fc {

/// Runtime bridge to the RenderDoc in-application API.
///
/// Uses dlopen at runtime so the binary works without RenderDoc installed.
/// When the CMake option GOGGLES_FILTER_CHAIN_ENABLE_RENDERDOC_CAPTURE is OFF,
/// all methods compile to no-ops.
class RenderDocBridge {
public:
    static auto create() -> RenderDocBridge;

    [[nodiscard]] auto available() const -> bool;
    void start_capture(VkDevice device);
    void end_capture(VkDevice device);
    void set_capture_path(const char* path_template);

private:
    void* m_api = nullptr; // Non-owning pointer to RENDERDOC_API_1_6_0, owned by RenderDoc
};

} // namespace goggles::fc
