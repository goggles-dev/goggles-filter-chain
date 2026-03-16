#include "runtime_diagnostics_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace runtime_diagnostics_test_support;

TEST_CASE("Public diagnostic summary is passive chain metadata",
          "[render][diagnostics][goggles_fc_api]") {
    auto& fixture = shared_vulkan_runtime_fixture();
    if (!fixture.available()) {
        SKIP("Skipping public diagnostics summary test because no Vulkan graphics device is "
             "available");
    }

    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/format.slangp";
    REQUIRE(std::filesystem::exists(preset_path));

    const auto cache_dir =
        std::filesystem::temp_directory_path() / "goggles_runtime_diag_public_capture_cache";
    std::filesystem::create_directories(cache_dir);

    FcInstanceGuard instance_guard;
    FcDeviceGuard device_guard;
    FcProgramGuard program_guard;
    FcChainGuard chain_guard;

    auto instance_info = goggles_fc_instance_create_info_init();
    REQUIRE(goggles_fc_instance_create(&instance_info, &instance_guard.instance) ==
            GOGGLES_FC_STATUS_OK);

    auto device_info = goggles_fc_vk_device_create_info_init();
    const auto cache_dir_string = cache_dir.string();
    device_info.physical_device = fixture.physical_device;
    device_info.device = fixture.device;
    device_info.graphics_queue = fixture.queue;
    device_info.graphics_queue_family_index = fixture.queue_family_index;
    device_info.cache_dir = make_utf8_view(cache_dir_string);
    REQUIRE(goggles_fc_device_create_vk(instance_guard.instance, &device_info,
                                        &device_guard.device) == GOGGLES_FC_STATUS_OK);

    auto source = goggles_fc_preset_source_init();
    const auto preset_path_string = preset_path.string();
    source.kind = GOGGLES_FC_PRESET_SOURCE_FILE;
    source.source_name = make_utf8_view("format.slangp");
    source.path = make_utf8_view(preset_path_string);
    REQUIRE(goggles_fc_program_create(device_guard.device, &source, &program_guard.program) ==
            GOGGLES_FC_STATUS_OK);

    auto chain_info = goggles_fc_chain_create_info_init();
    chain_info.target_format = VK_FORMAT_B8G8R8A8_UNORM;
    chain_info.frames_in_flight = 2u;
    chain_info.initial_prechain_resolution = {.width = 1u, .height = 1u};
    REQUIRE(goggles_fc_chain_create(device_guard.device, program_guard.program, &chain_info,
                                    &chain_guard.chain) == GOGGLES_FC_STATUS_OK);

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

    const auto record_frame = [&](uint32_t frame_index) {
        auto record_info = goggles_fc_record_info_vk_init();
        record_info.command_buffer = command_buffer;
        record_info.source_image = source_image->image;
        record_info.source_view = source_image->view;
        record_info.source_extent = {.width = extent.width, .height = extent.height};
        record_info.target_view = target_image->view;
        record_info.target_extent = {.width = extent.width, .height = extent.height};
        record_info.frame_index = frame_index;
        record_info.scale_mode = GOGGLES_FC_SCALE_MODE_STRETCH;
        record_info.integer_scale = 1u;

        VkCommandBufferBeginInfo frame_begin_info{};
        frame_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        REQUIRE(vkBeginCommandBuffer(command_buffer, &frame_begin_info) == VK_SUCCESS);
        transition_image(command_buffer, source_image->image,
                         {.old_layout = frame_index == 0u
                                            ? VK_IMAGE_LAYOUT_UNDEFINED
                                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        transition_image(command_buffer, target_image->image,
                         {.old_layout = frame_index == 0u
                                            ? VK_IMAGE_LAYOUT_UNDEFINED
                                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        REQUIRE(goggles_fc_chain_record_vk(chain_guard.chain, &record_info) ==
                GOGGLES_FC_STATUS_OK);
        REQUIRE(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
        REQUIRE(submit_and_wait(fixture.device, fixture.queue, command_buffer));
        REQUIRE(vkResetCommandBuffer(command_buffer, 0u) == VK_SUCCESS);
    };

    auto summary = goggles_fc_diagnostic_summary_init();
    REQUIRE(goggles_fc_chain_get_diagnostic_summary(chain_guard.chain, &summary) ==
            GOGGLES_FC_STATUS_OK);
    CHECK(summary.current_frame == 0u);
    CHECK(summary.total_events == 0u);
    CHECK(summary.debug_count == 0u);
    CHECK(summary.info_count == 0u);
    CHECK(summary.warning_count == 0u);
    CHECK(summary.error_count == 0u);

    record_frame(0u);

    auto passive_summary = goggles_fc_diagnostic_summary_init();
    REQUIRE(goggles_fc_chain_get_diagnostic_summary(chain_guard.chain, &passive_summary) ==
            GOGGLES_FC_STATUS_OK);
    CHECK(passive_summary.current_frame == 1u);
    CHECK(passive_summary.total_events == 0u);
    CHECK(passive_summary.debug_count == 0u);
    CHECK(passive_summary.info_count == 0u);
    CHECK(passive_summary.warning_count == 0u);
    CHECK(passive_summary.error_count == 0u);

    vkFreeCommandBuffers(fixture.device, fixture.command_pool, 1u, &command_buffer);
    std::filesystem::remove_all(cache_dir);
}
