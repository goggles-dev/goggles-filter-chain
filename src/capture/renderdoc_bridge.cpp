#include "capture/renderdoc_bridge.hpp"

#ifdef GOGGLES_FILTER_CHAIN_ENABLE_RENDERDOC_CAPTURE

#include <cstdio>
#include <dlfcn.h>
#include <renderdoc_app.h>

namespace goggles::fc {

auto RenderDocBridge::create() -> RenderDocBridge {
    RenderDocBridge bridge;

    // Try to load the RenderDoc shared library at runtime.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    void* module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (module == nullptr) {
        // RenderDoc not loaded into this process; try loading it explicitly.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        module = dlopen("librenderdoc.so", RTLD_NOW);
    }
    if (module == nullptr) {
        return bridge;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module, "RENDERDOC_GetAPI"));
    if (get_api == nullptr) {
        return bridge;
    }

    RENDERDOC_API_1_6_0* api = nullptr;
    int ret = get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api));
    if (ret != 1 || api == nullptr) {
        return bridge;
    }

    bridge.m_api = api;
    return bridge;
}

auto RenderDocBridge::available() const -> bool {
    return m_api != nullptr;
}

void RenderDocBridge::start_capture(VkDevice device) {
    if (m_api == nullptr) {
        return;
    }
    auto* api = static_cast<RENDERDOC_API_1_6_0*>(m_api);
    api->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(device), nullptr);
}

void RenderDocBridge::end_capture(VkDevice device) {
    if (m_api == nullptr) {
        return;
    }
    auto* api = static_cast<RENDERDOC_API_1_6_0*>(m_api);
    api->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(device), nullptr);
}

void RenderDocBridge::set_capture_path(const char* path_template) {
    if (m_api == nullptr) {
        return;
    }
    auto* api = static_cast<RENDERDOC_API_1_6_0*>(m_api);
    api->SetCaptureFilePathTemplate(path_template);
}

} // namespace goggles::fc

#else // !GOGGLES_FILTER_CHAIN_ENABLE_RENDERDOC_CAPTURE

namespace goggles::fc {

auto RenderDocBridge::create() -> RenderDocBridge {
    return {};
}

auto RenderDocBridge::available() const -> bool {
    return m_api != nullptr;
}

void RenderDocBridge::start_capture(VkDevice /*device*/) {}

void RenderDocBridge::end_capture(VkDevice /*device*/) {}

void RenderDocBridge::set_capture_path(const char* /*path_template*/) {}

} // namespace goggles::fc

#endif // GOGGLES_FILTER_CHAIN_ENABLE_RENDERDOC_CAPTURE
