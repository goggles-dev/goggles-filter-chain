#pragma once

#include <cstdint>
#include <goggles/error.hpp>
#include <vulkan/vulkan.h>

namespace goggles::cli {

struct ImageResource {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    ImageResource() = default;
    ~ImageResource();
    ImageResource(ImageResource&& other) noexcept;
    auto operator=(ImageResource&& other) noexcept -> ImageResource&;
    ImageResource(const ImageResource&) = delete;
    auto operator=(const ImageResource&) -> ImageResource& = delete;

    void cleanup();
};

struct BufferResource {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    BufferResource() = default;
    ~BufferResource();
    BufferResource(BufferResource&& other) noexcept;
    auto operator=(BufferResource&& other) noexcept -> BufferResource&;
    BufferResource(const BufferResource&) = delete;
    auto operator=(const BufferResource&) -> BufferResource& = delete;

    void cleanup();
};

struct VulkanBackend {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family_index = UINT32_MAX;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VulkanBackend() = default;
    ~VulkanBackend();
    VulkanBackend(const VulkanBackend&) = delete;
    auto operator=(const VulkanBackend&) -> VulkanBackend& = delete;
    VulkanBackend(VulkanBackend&& other) noexcept;
    auto operator=(VulkanBackend&&) -> VulkanBackend& = delete;

    [[nodiscard]] static auto create(bool enable_validation) -> goggles::Result<VulkanBackend>;

    [[nodiscard]] auto create_image(VkExtent2D extent, VkImageUsageFlags usage)
        -> goggles::Result<ImageResource>;

    [[nodiscard]] auto create_staging_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
        -> goggles::Result<BufferResource>;

    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                          VkImageLayout new_layout);

    [[nodiscard]] auto begin_commands() -> goggles::Result<VkCommandBuffer>;
    [[nodiscard]] auto submit_and_wait(VkCommandBuffer cmd) -> goggles::Result<void>;

    [[nodiscard]] auto upload_image(const uint8_t* pixels, uint32_t width, uint32_t height,
                                    const ImageResource& dst) -> goggles::Result<void>;

    [[nodiscard]] auto download_image(const ImageResource& src, uint32_t width, uint32_t height,
                                      VkImageLayout current_layout,
                                      std::vector<uint8_t>& out_pixels) -> goggles::Result<void>;
};

} // namespace goggles::cli
