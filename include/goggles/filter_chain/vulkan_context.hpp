#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace goggles::fc {

struct VulkanContext {
    vk::Device device;
    vk::PhysicalDevice physical_device;
    vk::Queue graphics_queue;
    uint32_t graphics_queue_family_index = UINT32_MAX;
};

} // namespace goggles::fc
