#include "chain/chain_runtime.hpp"
#include "chain/debug_label_scope.hpp"
#include "diagnostics/test_harness_sink.hpp"
#include "runtime_diagnostics_test_support.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace runtime_diagnostics_test_support;

namespace {

struct DebugLabelCapture {
    uint32_t begin_calls = 0;
    uint32_t end_calls = 0;
    std::vector<std::string> labels;
};

struct DebugLabelDispatcherOverride {
    explicit DebugLabelDispatcherOverride(DebugLabelCapture* capture)
        : previous_begin(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT),
          previous_end(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT) {
        active_capture = capture;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT = &begin_stub;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT = &end_stub;
    }

    ~DebugLabelDispatcherOverride() {
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT = previous_begin;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT = previous_end;
        active_capture = nullptr;
    }

    DebugLabelDispatcherOverride(const DebugLabelDispatcherOverride&) = delete;
    auto operator=(const DebugLabelDispatcherOverride&) -> DebugLabelDispatcherOverride& = delete;

    static VKAPI_ATTR void VKAPI_CALL begin_stub(VkCommandBuffer command_buffer,
                                                 const VkDebugUtilsLabelEXT* label) {
        (void)command_buffer;
        if (active_capture == nullptr) {
            return;
        }

        active_capture->begin_calls++;
        if (label != nullptr && label->pLabelName != nullptr) {
            active_capture->labels.emplace_back(label->pLabelName);
        }
    }

    static VKAPI_ATTR void VKAPI_CALL end_stub(VkCommandBuffer command_buffer) {
        (void)command_buffer;
        if (active_capture != nullptr) {
            active_capture->end_calls++;
        }
    }

    static void begin_label(vk::CommandBuffer command_buffer, const vk::DebugUtilsLabelEXT& label) {
        (void)command_buffer;
        if (active_capture == nullptr) {
            return;
        }

        active_capture->begin_calls++;
        if (label.pLabelName != nullptr) {
            active_capture->labels.emplace_back(label.pLabelName);
        }
    }

    static void end_label(vk::CommandBuffer command_buffer) {
        (void)command_buffer;
        if (active_capture != nullptr) {
            active_capture->end_calls++;
        }
    }

    inline static DebugLabelCapture* active_capture = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT previous_begin = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT previous_end = nullptr;
};

struct DebugLabelDispatcherDisable {
    DebugLabelDispatcherDisable()
        : previous_begin(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT),
          previous_end(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT) {
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT = nullptr;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT = nullptr;
    }

    ~DebugLabelDispatcherDisable() {
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT = previous_begin;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT = previous_end;
    }

    DebugLabelDispatcherDisable(const DebugLabelDispatcherDisable&) = delete;
    auto operator=(const DebugLabelDispatcherDisable&) -> DebugLabelDispatcherDisable& = delete;

    PFN_vkCmdBeginDebugUtilsLabelEXT previous_begin = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT previous_end = nullptr;
};

struct DebugLabelCaptureScope {
    explicit DebugLabelCaptureScope(DebugLabelCapture* capture) {
        DebugLabelDispatcherOverride::active_capture = capture;
    }

    ~DebugLabelCaptureScope() { DebugLabelDispatcherOverride::active_capture = nullptr; }

    DebugLabelCaptureScope(const DebugLabelCaptureScope&) = delete;
    auto operator=(const DebugLabelCaptureScope&) -> DebugLabelCaptureScope& = delete;
};

void enable_runtime_diagnostics(goggles::fc::ChainRuntime& runtime,
                                const goggles::diagnostics::DiagnosticPolicy& policy) {
    runtime.create_diagnostic_session(policy);
}

} // namespace

TEST_CASE("ScopedDebugLabel skips incomplete debug-utils dispatch",
          "[render][diagnostics][runtime][profiling]") {
    DebugLabelCapture capture{};
    const DebugLabelCaptureScope capture_scope(&capture);

    {
        const goggles::fc::ScopedDebugLabel label(
            vk::CommandBuffer{}, "Pass 0 Null Begin", {0.18F, 0.46F, 0.92F, 1.0F},
            goggles::fc::DebugLabelDispatch{.begin = nullptr,
                                            .end = &DebugLabelDispatcherOverride::end_label,
                                            .enabled = true});
        CHECK_FALSE(label.active());
    }
    CHECK(capture.begin_calls == 0u);
    CHECK(capture.end_calls == 0u);

    {
        const goggles::fc::ScopedDebugLabel label(
            vk::CommandBuffer{}, "Pass 0 Null End", {0.18F, 0.46F, 0.92F, 1.0F},
            goggles::fc::DebugLabelDispatch{.begin = &DebugLabelDispatcherOverride::begin_label,
                                            .end = nullptr,
                                            .enabled = true});
        CHECK_FALSE(label.active());
    }
    CHECK(capture.begin_calls == 0u);
    CHECK(capture.end_calls == 0u);

    {
        const goggles::fc::ScopedDebugLabel label(
            vk::CommandBuffer{}, "Pass 0 Active", {0.18F, 0.46F, 0.92F, 1.0F},
            goggles::fc::DebugLabelDispatch{.begin = &DebugLabelDispatcherOverride::begin_label,
                                            .end = &DebugLabelDispatcherOverride::end_label,
                                            .enabled = true});
        CHECK(label.active());
    }
    CHECK(capture.begin_calls == 1u);
    CHECK(capture.end_calls == 1u);
    REQUIRE(capture.labels.size() == 1u);
    CHECK(capture.labels.front() == "Pass 0 Active");
}

TEST_CASE("ChainRuntime emits runtime diagnostics ledgers", "[render][diagnostics][runtime]") {
    auto& fixture = shared_vulkan_runtime_fixture();
    if (!fixture.available()) {
        SKIP("Skipping runtime diagnostics test because no Vulkan graphics device is available");
    }

    const auto shader_root = std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders";
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/format.slangp";
    REQUIRE(std::filesystem::exists(shader_root));
    REQUIRE(std::filesystem::exists(preset_path));

    goggles::fc::VulkanContext vk_ctx{
        .device = vk::Device{fixture.device},
        .physical_device = vk::PhysicalDevice{fixture.physical_device},
        .graphics_queue = vk::Queue{fixture.queue},
        .graphics_queue_family_index = fixture.queue_family_index,
    };

    const auto cache_dir = std::filesystem::temp_directory_path() / "goggles_runtime_diag_cache";
    std::filesystem::create_directories(cache_dir);
    auto runtime_result = goggles::fc::ChainRuntime::create(
        vk_ctx, vk::Format::eB8G8R8A8Unorm, 2u, {.shader_dir = shader_root, .cache_dir = cache_dir},
        {1u, 1u});
    REQUIRE(runtime_result);
    auto runtime = std::move(*runtime_result);

    enable_runtime_diagnostics(*runtime, {});
    auto* session = runtime->diagnostic_session();
    REQUIRE(session != nullptr);
    session->register_sink(std::make_unique<goggles::diagnostics::TestHarnessSink>());

    REQUIRE(runtime->load_preset(preset_path));

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = fixture.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(fixture.device, &alloc_info, &command_buffer) == VK_SUCCESS);

    const VkExtent2D extent{.width = 1u, .height = 1u};
    auto source_image = create_image(fixture.device, fixture.physical_device, extent);
    auto target_image = create_image(fixture.device, fixture.physical_device, extent);
    REQUIRE(source_image.has_value());
    REQUIRE(target_image.has_value());

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    REQUIRE(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
    transition_image(command_buffer, source_image->image,
                     {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    transition_image(command_buffer, target_image->image,
                     {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                      .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    runtime->record(vk::CommandBuffer{command_buffer}, vk::Image{source_image->image},
                    vk::ImageView{source_image->view}, vk::Extent2D{1u, 1u},
                    vk::ImageView{target_image->view}, vk::Extent2D{1u, 1u}, 0u);
    REQUIRE(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);

    REQUIRE_FALSE(session->binding_ledger().all_entries().empty());
    REQUIRE_FALSE(session->semantic_ledger().all_entries().empty());
    REQUIRE_FALSE(session->execution_timeline().events().empty());

    bool saw_prechain_start = false;
    bool saw_prechain_end = false;
    bool saw_final_composition_start = false;
    bool saw_final_composition_end = false;
    for (const auto& event : session->execution_timeline().events()) {
        saw_prechain_start = saw_prechain_start ||
                             event.type == goggles::diagnostics::TimelineEventType::prechain_start;
        saw_prechain_end =
            saw_prechain_end || event.type == goggles::diagnostics::TimelineEventType::prechain_end;
        saw_final_composition_start =
            saw_final_composition_start ||
            event.type == goggles::diagnostics::TimelineEventType::final_composition_start;
        saw_final_composition_end =
            saw_final_composition_end ||
            event.type == goggles::diagnostics::TimelineEventType::final_composition_end;
    }

    CHECK(saw_prechain_start);
    CHECK(saw_prechain_end);
    CHECK(saw_final_composition_start);
    CHECK(saw_final_composition_end);

    vkFreeCommandBuffers(fixture.device, fixture.command_pool, 1u, &command_buffer);
    runtime->shutdown();
    std::filesystem::remove_all(cache_dir);
}

TEST_CASE("Strict diagnostics forbid fallback pass execution", "[render][diagnostics][runtime]") {
    auto& fixture = shared_vulkan_runtime_fixture();
    if (!fixture.available()) {
        SKIP("Skipping runtime diagnostics test because no Vulkan graphics device is available");
    }

    const auto shader_root = std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders";
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/history.slangp";
    REQUIRE(std::filesystem::exists(shader_root));
    REQUIRE(std::filesystem::exists(preset_path));

    goggles::fc::VulkanContext vk_ctx{
        .device = vk::Device{fixture.device},
        .physical_device = vk::PhysicalDevice{fixture.physical_device},
        .graphics_queue = vk::Queue{fixture.queue},
        .graphics_queue_family_index = fixture.queue_family_index,
    };

    const auto cache_dir =
        std::filesystem::temp_directory_path() / "goggles_runtime_diag_strict_cache";
    std::filesystem::create_directories(cache_dir);
    auto runtime_result = goggles::fc::ChainRuntime::create(
        vk_ctx, vk::Format::eB8G8R8A8Unorm, 2u, {.shader_dir = shader_root, .cache_dir = cache_dir},
        {1u, 1u});
    REQUIRE(runtime_result);
    auto runtime = std::move(*runtime_result);

    goggles::diagnostics::DiagnosticPolicy policy{};
    policy.mode = goggles::diagnostics::PolicyMode::strict;
    policy.promote_fallback_to_error = true;
    enable_runtime_diagnostics(*runtime, policy);

    auto* session = runtime->diagnostic_session();
    REQUIRE(session != nullptr);
    auto harness = std::make_unique<goggles::diagnostics::TestHarnessSink>();
    auto* harness_ptr = harness.get();
    session->register_sink(std::move(harness));

    REQUIRE(runtime->load_preset(preset_path));

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = fixture.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(fixture.device, &alloc_info, &command_buffer) == VK_SUCCESS);

    const VkExtent2D extent{.width = 1u, .height = 1u};
    auto source_image = create_image(fixture.device, fixture.physical_device, extent);
    auto target_image = create_image(fixture.device, fixture.physical_device, extent);
    REQUIRE(source_image.has_value());
    REQUIRE(target_image.has_value());

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    REQUIRE(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
    transition_image(command_buffer, source_image->image,
                     {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    transition_image(command_buffer, target_image->image,
                     {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                      .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    runtime->record(vk::CommandBuffer{command_buffer}, vk::Image{source_image->image},
                    vk::ImageView{source_image->view}, vk::Extent2D{1u, 1u},
                    vk::ImageView{target_image->view}, vk::Extent2D{1u, 1u}, 0u);
    REQUIRE(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);

    REQUIRE(harness_ptr != nullptr);
    CHECK(session->event_count(goggles::diagnostics::Severity::error) > 0u);
    CHECK_FALSE(session->degradation_ledger().all_entries().empty());

    bool saw_pass_start = false;
    for (const auto& event : session->execution_timeline().events()) {
        if (event.type == goggles::diagnostics::TimelineEventType::pass_start) {
            saw_pass_start = true;
            break;
        }
    }
    CHECK_FALSE(saw_pass_start);

    const auto runtime_events =
        harness_ptr->events_by_category(goggles::diagnostics::Category::runtime);
    CHECK(std::any_of(runtime_events.begin(), runtime_events.end(), [](const auto& event) {
        return event.severity == goggles::diagnostics::Severity::error &&
               event.localization.stage == "record" &&
               event.message.find("skipping remaining effect passes") != std::string::npos;
    }));

    vkFreeCommandBuffers(fixture.device, fixture.command_pool, 1u, &command_buffer);
    runtime->shutdown();
    std::filesystem::remove_all(cache_dir);
}

TEST_CASE("ChainRuntime Tier 1 diagnostics expose GPU timing evidence",
          "[render][diagnostics][runtime][profiling]") {
    auto& fixture = shared_vulkan_runtime_fixture();
    if (!fixture.available()) {
        SKIP("Skipping Tier 1 runtime diagnostics test because no Vulkan graphics device is "
             "available");
    }

    const auto shader_root = std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders";
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/format.slangp";
    REQUIRE(std::filesystem::exists(shader_root));
    REQUIRE(std::filesystem::exists(preset_path));

    goggles::fc::VulkanContext vk_ctx{
        .device = vk::Device{fixture.device},
        .physical_device = vk::PhysicalDevice{fixture.physical_device},
        .graphics_queue = vk::Queue{fixture.queue},
        .graphics_queue_family_index = fixture.queue_family_index,
    };

    const auto cache_dir =
        std::filesystem::temp_directory_path() / "goggles_runtime_diag_tier1_cache";
    std::filesystem::create_directories(cache_dir);
    auto runtime_result = goggles::fc::ChainRuntime::create(
        vk_ctx, vk::Format::eB8G8R8A8Unorm, 2u, {.shader_dir = shader_root, .cache_dir = cache_dir},
        {1u, 1u});
    REQUIRE(runtime_result);
    auto runtime = std::move(*runtime_result);

    if (!goggles::diagnostics::GpuTimestampPool::supports_timestamps(
            vk::PhysicalDevice{fixture.physical_device}, fixture.queue_family_index)) {
        runtime->shutdown();
        std::filesystem::remove_all(cache_dir);
        SKIP("Skipping Tier 1 GPU timing runtime test because timestamps are unavailable");
    }

    goggles::diagnostics::DiagnosticPolicy policy{};
    policy.tier = goggles::diagnostics::ActivationTier::tier1;
    enable_runtime_diagnostics(*runtime, policy);

    auto* session = runtime->diagnostic_session();
    REQUIRE(session != nullptr);

    REQUIRE(runtime->load_preset(preset_path));

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = fixture.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;

    const auto record_frame = [&](VkCommandBuffer command_buffer) {
        const VkExtent2D extent{.width = 1u, .height = 1u};
        auto source_image = create_image(fixture.device, fixture.physical_device, extent);
        auto target_image = create_image(fixture.device, fixture.physical_device, extent);
        REQUIRE(source_image.has_value());
        REQUIRE(target_image.has_value());

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        REQUIRE(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
        transition_image(command_buffer, source_image->image,
                         {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        transition_image(command_buffer, target_image->image,
                         {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

        runtime->record(vk::CommandBuffer{command_buffer}, vk::Image{source_image->image},
                        vk::ImageView{source_image->view}, vk::Extent2D{1u, 1u},
                        vk::ImageView{target_image->view}, vk::Extent2D{1u, 1u}, 0u);
        REQUIRE(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
        REQUIRE(submit_and_wait(fixture.device, fixture.queue, command_buffer));
        REQUIRE(vkResetCommandBuffer(command_buffer, 0u) == VK_SUCCESS);
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(fixture.device, &alloc_info, &command_buffer) == VK_SUCCESS);
    record_frame(command_buffer);
    record_frame(command_buffer);

    const bool saw_unavailable_event =
        std::any_of(session->events().begin(), session->events().end(), [](const auto& event) {
            return event.category == goggles::diagnostics::Category::runtime &&
                   event.severity == goggles::diagnostics::Severity::info &&
                   event.message == "GPU timestamps are unavailable on this device";
        });
    CHECK_FALSE(saw_unavailable_event);

    std::vector<goggles::diagnostics::TimelineEvent> annotated_pass_events;
    bool saw_prechain_gpu_duration = false;
    bool saw_final_gpu_duration = false;
    for (const auto& event : session->execution_timeline().events()) {
        if (!event.gpu_duration_us.has_value()) {
            continue;
        }

        if (event.type == goggles::diagnostics::TimelineEventType::pass_end) {
            annotated_pass_events.push_back(event);
        } else if (event.type == goggles::diagnostics::TimelineEventType::prechain_end) {
            saw_prechain_gpu_duration = true;
        } else if (event.type == goggles::diagnostics::TimelineEventType::final_composition_end) {
            saw_final_gpu_duration = true;
        }
    }

    CHECK(saw_prechain_gpu_duration);
    CHECK(saw_final_gpu_duration);
    REQUIRE(annotated_pass_events.size() >= 2u);

    std::sort(annotated_pass_events.begin(), annotated_pass_events.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.gpu_duration_us.value() > rhs.gpu_duration_us.value();
              });
    CHECK(annotated_pass_events.front().gpu_duration_us.value() >=
          annotated_pass_events.back().gpu_duration_us.value());
    CHECK(std::any_of(annotated_pass_events.begin(), annotated_pass_events.end(),
                      [](const auto& event) { return event.pass_ordinal == 0u; }));
    CHECK(std::any_of(annotated_pass_events.begin(), annotated_pass_events.end(),
                      [](const auto& event) { return event.pass_ordinal == 1u; }));

    vkFreeCommandBuffers(fixture.device, fixture.command_pool, 1u, &command_buffer);
    runtime->shutdown();
    std::filesystem::remove_all(cache_dir);
}

TEST_CASE("ChainRuntime reports unavailable GPU timestamps deterministically",
          "[render][diagnostics][runtime][profiling]") {
    auto& fixture = shared_vulkan_runtime_fixture();
    if (!fixture.available()) {
        SKIP("Skipping Tier 1 runtime diagnostics test because no Vulkan graphics device is "
             "available");
    }

    const auto shader_root = std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders";
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/format.slangp";
    REQUIRE(std::filesystem::exists(shader_root));
    REQUIRE(std::filesystem::exists(preset_path));

    goggles::fc::VulkanContext vk_ctx{
        .device = vk::Device{fixture.device},
        .physical_device = vk::PhysicalDevice{fixture.physical_device},
        .graphics_queue = vk::Queue{fixture.queue},
        .graphics_queue_family_index = fixture.queue_family_index,
    };

    const auto cache_dir =
        std::filesystem::temp_directory_path() / "goggles_runtime_diag_tier1_unavailable_cache";
    std::filesystem::create_directories(cache_dir);
    auto runtime_result = goggles::fc::ChainRuntime::create(
        vk_ctx, vk::Format::eB8G8R8A8Unorm, 2u, {.shader_dir = shader_root, .cache_dir = cache_dir},
        {1u, 1u});
    REQUIRE(runtime_result);
    auto runtime = std::move(*runtime_result);

    goggles::diagnostics::DiagnosticPolicy policy{};
    policy.tier = goggles::diagnostics::ActivationTier::tier1;
    policy.gpu_timestamp_availability =
        goggles::diagnostics::GpuTimestampAvailabilityMode::force_unavailable;
    enable_runtime_diagnostics(*runtime, policy);

    auto* session = runtime->diagnostic_session();
    REQUIRE(session != nullptr);
    REQUIRE(runtime->load_preset(preset_path));

    const bool saw_unavailable_event =
        std::any_of(session->events().begin(), session->events().end(), [](const auto& event) {
            return event.category == goggles::diagnostics::Category::runtime &&
                   event.severity == goggles::diagnostics::Severity::info &&
                   event.message == "GPU timestamps are unavailable on this device";
        });
    CHECK(saw_unavailable_event);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = fixture.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(fixture.device, &alloc_info, &command_buffer) == VK_SUCCESS);

    const VkExtent2D extent{.width = 1u, .height = 1u};
    auto source_image = create_image(fixture.device, fixture.physical_device, extent);
    auto target_image = create_image(fixture.device, fixture.physical_device, extent);
    REQUIRE(source_image.has_value());
    REQUIRE(target_image.has_value());

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    REQUIRE(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
    transition_image(command_buffer, source_image->image,
                     {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    transition_image(command_buffer, target_image->image,
                     {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                      .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    runtime->record(vk::CommandBuffer{command_buffer}, vk::Image{source_image->image},
                    vk::ImageView{source_image->view}, vk::Extent2D{1u, 1u},
                    vk::ImageView{target_image->view}, vk::Extent2D{1u, 1u}, 0u);
    REQUIRE(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
    REQUIRE(submit_and_wait(fixture.device, fixture.queue, command_buffer));

    const bool saw_gpu_duration =
        std::any_of(session->execution_timeline().events().begin(),
                    session->execution_timeline().events().end(),
                    [](const auto& event) { return event.gpu_duration_us.has_value(); });
    CHECK_FALSE(saw_gpu_duration);

    vkFreeCommandBuffers(fixture.device, fixture.command_pool, 1u, &command_buffer);
    runtime->shutdown();
    std::filesystem::remove_all(cache_dir);
}

TEST_CASE("ChainRuntime emits profiling debug labels when dispatch is available",
          "[render][diagnostics][runtime][profiling]") {
    auto& fixture = shared_vulkan_runtime_fixture();
    if (!fixture.available()) {
        SKIP("Skipping debug label runtime test because no Vulkan graphics device is available");
    }

    const auto shader_root = std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders";
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/history.slangp";
    REQUIRE(std::filesystem::exists(shader_root));
    REQUIRE(std::filesystem::exists(preset_path));

    goggles::fc::VulkanContext vk_ctx{
        .device = vk::Device{fixture.device},
        .physical_device = vk::PhysicalDevice{fixture.physical_device},
        .graphics_queue = vk::Queue{fixture.queue},
        .graphics_queue_family_index = fixture.queue_family_index,
    };

    const auto cache_dir =
        std::filesystem::temp_directory_path() / "goggles_runtime_diag_debug_label_cache";
    std::filesystem::create_directories(cache_dir);
    auto runtime_result = goggles::fc::ChainRuntime::create(
        vk_ctx, vk::Format::eB8G8R8A8Unorm, 2u, {.shader_dir = shader_root, .cache_dir = cache_dir},
        {1u, 1u});
    REQUIRE(runtime_result);
    auto runtime = std::move(*runtime_result);

    goggles::diagnostics::DiagnosticPolicy policy{};
    policy.tier = goggles::diagnostics::ActivationTier::tier1;
    enable_runtime_diagnostics(*runtime, policy);
    REQUIRE(runtime->load_preset(preset_path));

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = fixture.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;

    const auto record_frame = [&](VkCommandBuffer command_buffer) {
        const VkExtent2D extent{.width = 1u, .height = 1u};
        auto source_image = create_image(fixture.device, fixture.physical_device, extent);
        auto target_image = create_image(fixture.device, fixture.physical_device, extent);
        REQUIRE(source_image.has_value());
        REQUIRE(target_image.has_value());

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        REQUIRE(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
        transition_image(command_buffer, source_image->image,
                         {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        transition_image(command_buffer, target_image->image,
                         {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

        runtime->record(vk::CommandBuffer{command_buffer}, vk::Image{source_image->image},
                        vk::ImageView{source_image->view}, vk::Extent2D{1u, 1u},
                        vk::ImageView{target_image->view}, vk::Extent2D{1u, 1u}, 0u);
        REQUIRE(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
        REQUIRE(submit_and_wait(fixture.device, fixture.queue, command_buffer));
        REQUIRE(vkResetCommandBuffer(command_buffer, 0u) == VK_SUCCESS);
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(fixture.device, &alloc_info, &command_buffer) == VK_SUCCESS);

    DebugLabelCapture capture{};
    {
        const DebugLabelDispatcherOverride debug_labels(&capture);
        record_frame(command_buffer);
    }

    CHECK(capture.begin_calls > 0u);
    CHECK(capture.begin_calls == capture.end_calls);
    CHECK(std::any_of(capture.labels.begin(), capture.labels.end(),
                      [](const auto& label) { return label.starts_with("Pass "); }));
    CHECK(std::find(capture.labels.begin(), capture.labels.end(), "History Push") !=
          capture.labels.end());
    CHECK(std::find(capture.labels.begin(), capture.labels.end(), "Feedback Copy") !=
          capture.labels.end());

    {
        const DebugLabelDispatcherDisable debug_labels_disabled;
        record_frame(command_buffer);
    }

    vkFreeCommandBuffers(fixture.device, fixture.command_pool, 1u, &command_buffer);
    runtime->shutdown();
    std::filesystem::remove_all(cache_dir);
}
