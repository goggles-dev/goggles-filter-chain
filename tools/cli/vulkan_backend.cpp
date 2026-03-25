#include "vulkan_backend.hpp"

#include <cstring>
#include <optional>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace goggles::cli {

namespace {

auto find_memory_type(VkPhysicalDevice physical_device, uint32_t type_bits,
                      VkMemoryPropertyFlags properties) -> std::optional<uint32_t> {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) != 0u &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

// ── ImageResource RAII ──────────────────────────────────────────────────────

void ImageResource::cleanup() {
    if (device != VK_NULL_HANDLE && view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (device != VK_NULL_HANDLE && image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (device != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

ImageResource::~ImageResource() { cleanup(); }

ImageResource::ImageResource(ImageResource&& other) noexcept { *this = std::move(other); }

auto ImageResource::operator=(ImageResource&& other) noexcept -> ImageResource& {
    if (this != &other) {
        cleanup();
        device = other.device;
        image = other.image;
        memory = other.memory;
        view = other.view;
        other.device = VK_NULL_HANDLE;
        other.image = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.view = VK_NULL_HANDLE;
    }
    return *this;
}

// ── BufferResource RAII ─────────────────────────────────────────────────────

void BufferResource::cleanup() {
    if (device != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (device != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

BufferResource::~BufferResource() { cleanup(); }

BufferResource::BufferResource(BufferResource&& other) noexcept { *this = std::move(other); }

auto BufferResource::operator=(BufferResource&& other) noexcept -> BufferResource& {
    if (this != &other) {
        cleanup();
        device = other.device;
        buffer = other.buffer;
        memory = other.memory;
        other.device = VK_NULL_HANDLE;
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
    }
    return *this;
}

// ── VulkanBackend lifecycle ─────────────────────────────────────────────────

VulkanBackend::VulkanBackend(VulkanBackend&& other) noexcept
    : instance(other.instance),
      physical_device(other.physical_device),
      device(other.device),
      queue(other.queue),
      queue_family_index(other.queue_family_index),
      command_pool(other.command_pool),
      fence(other.fence) {
    other.instance = VK_NULL_HANDLE;
    other.physical_device = VK_NULL_HANDLE;
    other.device = VK_NULL_HANDLE;
    other.queue = VK_NULL_HANDLE;
    other.queue_family_index = UINT32_MAX;
    other.command_pool = VK_NULL_HANDLE;
    other.fence = VK_NULL_HANDLE;
}

VulkanBackend::~VulkanBackend() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
        }
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

auto VulkanBackend::create(bool enable_validation) -> goggles::Result<VulkanBackend> {
    VulkanBackend backend;

    // ── Instance ────────────────────────────────────────────────────────
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "goggles-chain-cli";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    const char* validation_layer = "VK_LAYER_KHRONOS_validation";

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    if (enable_validation) {
        instance_info.enabledLayerCount = 1u;
        instance_info.ppEnabledLayerNames = &validation_layer;
    }

    if (vkCreateInstance(&instance_info, nullptr, &backend.instance) != VK_SUCCESS) {
        return goggles::make_error<VulkanBackend>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to create Vulkan instance");
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance{backend.instance});

    // ── Physical device (prefer discrete GPU) ───────────────────────────
    uint32_t device_count = 0;
    if (vkEnumeratePhysicalDevices(backend.instance, &device_count, nullptr) != VK_SUCCESS ||
        device_count == 0) {
        return goggles::make_error<VulkanBackend>(goggles::ErrorCode::vulkan_init_failed,
                                                  "No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(backend.instance, &device_count, devices.data());

    // First pass: look for a discrete GPU with a graphics queue
    for (const auto candidate : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            continue;
        }

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (uint32_t i = 0; i < family_count; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                backend.physical_device = candidate;
                backend.queue_family_index = i;
                break;
            }
        }
        if (backend.physical_device != VK_NULL_HANDLE) {
            break;
        }
    }

    // Fallback: any device with a graphics queue
    if (backend.physical_device == VK_NULL_HANDLE) {
        for (const auto candidate : devices) {
            uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
            for (uint32_t i = 0; i < family_count; ++i) {
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                    backend.physical_device = candidate;
                    backend.queue_family_index = i;
                    break;
                }
            }
            if (backend.physical_device != VK_NULL_HANDLE) {
                break;
            }
        }
    }

    if (backend.physical_device == VK_NULL_HANDLE) {
        return goggles::make_error<VulkanBackend>(goggles::ErrorCode::vulkan_init_failed,
                                                  "No Vulkan device with graphics queue found");
    }

    // ── Logical device + queue ──────────────────────────────────────────
    float queue_priority = 1.0F;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;

    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = backend.queue_family_index;
    queue_info.queueCount = 1U;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1U;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pNext = &features13;

    if (vkCreateDevice(backend.physical_device, &device_info, nullptr, &backend.device) !=
        VK_SUCCESS) {
        return goggles::make_error<VulkanBackend>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to create Vulkan logical device");
    }

    vkGetDeviceQueue(backend.device, backend.queue_family_index, 0U, &backend.queue);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device{backend.device});

    // ── Command pool + fence ────────────────────────────────────────────
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = backend.queue_family_index;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(backend.device, &pool_info, nullptr, &backend.command_pool) !=
        VK_SUCCESS) {
        return goggles::make_error<VulkanBackend>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to create command pool");
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(backend.device, &fence_info, nullptr, &backend.fence) != VK_SUCCESS) {
        return goggles::make_error<VulkanBackend>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to create fence");
    }

    return std::move(backend);
}

// ── Resource creation ───────────────────────────────────────────────────────

auto VulkanBackend::create_image(VkExtent2D extent, VkImageUsageFlags usage)
    -> goggles::Result<ImageResource> {
    ImageResource res;
    res.device = device;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {.width = extent.width, .height = extent.height, .depth = 1u};
    image_info.mipLevels = 1u;
    image_info.arrayLayers = 1u;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &image_info, nullptr, &res.image) != VK_SUCCESS) {
        return goggles::make_error<ImageResource>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to create image");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, res.image, &requirements);
    const auto mem_type =
        find_memory_type(physical_device, requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!mem_type.has_value()) {
        return goggles::make_error<ImageResource>(goggles::ErrorCode::vulkan_init_failed,
                                                  "No suitable memory type for image");
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = *mem_type;

    if (vkAllocateMemory(device, &alloc_info, nullptr, &res.memory) != VK_SUCCESS) {
        return goggles::make_error<ImageResource>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to allocate image memory");
    }
    if (vkBindImageMemory(device, res.image, res.memory, 0u) != VK_SUCCESS) {
        return goggles::make_error<ImageResource>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to bind image memory");
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = res.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1u;
    view_info.subresourceRange.layerCount = 1u;

    if (vkCreateImageView(device, &view_info, nullptr, &res.view) != VK_SUCCESS) {
        return goggles::make_error<ImageResource>(goggles::ErrorCode::vulkan_init_failed,
                                                  "Failed to create image view");
    }

    return res;
}

auto VulkanBackend::create_staging_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
    -> goggles::Result<BufferResource> {
    BufferResource res;
    res.device = device;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, nullptr, &res.buffer) != VK_SUCCESS) {
        return goggles::make_error<BufferResource>(goggles::ErrorCode::vulkan_init_failed,
                                                   "Failed to create staging buffer");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, res.buffer, &requirements);
    const auto mem_type = find_memory_type(
        physical_device, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!mem_type.has_value()) {
        return goggles::make_error<BufferResource>(goggles::ErrorCode::vulkan_init_failed,
                                                   "No suitable memory type for staging buffer");
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = *mem_type;

    if (vkAllocateMemory(device, &alloc_info, nullptr, &res.memory) != VK_SUCCESS) {
        return goggles::make_error<BufferResource>(goggles::ErrorCode::vulkan_init_failed,
                                                   "Failed to allocate staging buffer memory");
    }
    if (vkBindBufferMemory(device, res.buffer, res.memory, 0u) != VK_SUCCESS) {
        return goggles::make_error<BufferResource>(goggles::ErrorCode::vulkan_init_failed,
                                                   "Failed to bind staging buffer memory");
    }

    return res;
}

// ── Command helpers ─────────────────────────────────────────────────────────

void VulkanBackend::transition_image(
    VkCommandBuffer cmd, VkImage image,
    VkImageLayout old_layout, // NOLINT(bugprone-easily-swappable-parameters)
    VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1u;
    barrier.subresourceRange.layerCount = 1u;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0u, 0u, nullptr, 0u, nullptr, 1u, &barrier);
}

auto VulkanBackend::begin_commands() -> goggles::Result<VkCommandBuffer> {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &alloc_info, &cmd) != VK_SUCCESS) {
        return goggles::make_error<VkCommandBuffer>(goggles::ErrorCode::vulkan_init_failed,
                                                    "Failed to allocate command buffer");
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, command_pool, 1u, &cmd);
        return goggles::make_error<VkCommandBuffer>(goggles::ErrorCode::vulkan_device_lost,
                                                    "Failed to begin command buffer");
    }

    return cmd;
}

auto VulkanBackend::submit_and_wait(VkCommandBuffer cmd) -> goggles::Result<void> {
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, command_pool, 1u, &cmd);
        return goggles::make_error<void>(goggles::ErrorCode::vulkan_device_lost,
                                         "Failed to end command buffer");
    }

    vkResetFences(device, 1u, &fence);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1u;
    submit_info.pCommandBuffers = &cmd;

    if (vkQueueSubmit(queue, 1u, &submit_info, fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, command_pool, 1u, &cmd);
        return goggles::make_error<void>(goggles::ErrorCode::vulkan_device_lost,
                                         "Failed to submit command buffer");
    }

    if (vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, command_pool, 1u, &cmd);
        return goggles::make_error<void>(goggles::ErrorCode::vulkan_device_lost,
                                         "Fence wait failed");
    }

    vkFreeCommandBuffers(device, command_pool, 1u, &cmd);
    return {};
}

// ── Upload / download ───────────────────────────────────────────────────────

auto VulkanBackend::upload_image(const uint8_t* pixels, uint32_t width, uint32_t height,
                                 const ImageResource& dst) -> goggles::Result<void> {
    const auto buffer_size = static_cast<VkDeviceSize>(width) * height * 4u;
    auto staging_result = create_staging_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging_result) {
        return nonstd::make_unexpected(staging_result.error());
    }
    auto staging = std::move(*staging_result);

    void* mapped = nullptr;
    if (vkMapMemory(device, staging.memory, 0u, buffer_size, 0u, &mapped) != VK_SUCCESS) {
        return goggles::make_error<void>(goggles::ErrorCode::vulkan_device_lost,
                                         "Failed to map staging buffer");
    }
    std::memcpy(mapped, pixels, static_cast<size_t>(buffer_size));
    vkUnmapMemory(device, staging.memory);

    auto cmd_result = begin_commands();
    if (!cmd_result) {
        return nonstd::make_unexpected(cmd_result.error());
    }
    auto cmd = *cmd_result;

    transition_image(cmd, dst.image, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1u;
    region.imageExtent = {.width = width, .height = height, .depth = 1u};
    vkCmdCopyBufferToImage(cmd, staging.buffer, dst.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);

    transition_image(cmd, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return submit_and_wait(cmd);
}

auto VulkanBackend::download_image(const ImageResource& src, uint32_t width, uint32_t height,
                                   VkImageLayout current_layout, std::vector<uint8_t>& out_pixels)
    -> goggles::Result<void> {
    const auto buffer_size = static_cast<VkDeviceSize>(width) * height * 4u;
    auto staging_result = create_staging_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (!staging_result) {
        return nonstd::make_unexpected(staging_result.error());
    }
    auto staging = std::move(*staging_result);

    auto cmd_result = begin_commands();
    if (!cmd_result) {
        return nonstd::make_unexpected(cmd_result.error());
    }
    auto cmd = *cmd_result;

    transition_image(cmd, src.image, current_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1u;
    region.imageExtent = {.width = width, .height = height, .depth = 1u};
    vkCmdCopyImageToBuffer(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer, 1u, &region);

    transition_image(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current_layout);

    auto submit_result = submit_and_wait(cmd);
    if (!submit_result) {
        return nonstd::make_unexpected(submit_result.error());
    }

    out_pixels.resize(static_cast<size_t>(buffer_size));
    void* mapped = nullptr;
    if (vkMapMemory(device, staging.memory, 0u, buffer_size, 0u, &mapped) != VK_SUCCESS) {
        return goggles::make_error<void>(goggles::ErrorCode::vulkan_device_lost,
                                         "Failed to map readback buffer");
    }
    std::memcpy(out_pixels.data(), mapped, out_pixels.size());
    vkUnmapMemory(device, staging.memory);

    return {};
}

} // namespace goggles::cli
