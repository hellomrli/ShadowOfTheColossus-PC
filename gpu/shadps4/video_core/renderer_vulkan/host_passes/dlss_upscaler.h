// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: the host-side half of DLSS Super Resolution: converts the game's presented image and
// its depth to what NGX accepts, records the DLSS evaluation and returns the upscaled image.
//
// Inputs from the game (presentation stage, after tonemapping, HUD included):
//   colour: the presented image (R8G8B8A8Srgb, B8G8R8A8Srgb or A2R10G10B10), converted to
//           RGBA16F in display-referred sRGB values (DLSS's SDR mode).
//   depth:  the game's reversed-Z depth, converted to R32F with 0 at the near plane.
//   motion: the G-buffer's R16G16 float (curUV - prevUV), already at render resolution.
// The output is an RGBA16F image at the presentation size; the existing post-process pass
// draws it to the swapchain exactly as it draws the FSR 1 result.

#pragma once

#include <optional>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_dlss.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

class DlssUpscaler {
public:
    struct Inputs {
        vk::Image depth;
        vk::ImageView depth_view;
        vk::Format depth_format;
        vk::ImageAspectFlags depth_aspect;
        vk::Image motion;
        vk::ImageView motion_view;
        float jitter_x, jitter_y;
        bool reset;
        bool hdr;
        float mv_scale_x, mv_scale_y;
        float sharpness;
    };

    void Create(vk::Device device, VmaAllocator allocator, u32 num_images);
    void Destroy();
    /// A skipped frame or a different upscaler breaks the temporal history chain.
    void ResetHistory() { reset = true; }

    /// Returns the upscaled view (RGBA16F, output size), or null when DLSS did not run this
    /// frame (the caller then keeps its FSR 1 path).
    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView color, vk::Format color_format,
                         vk::Extent2D input_size, vk::Extent2D output_size, u32 frame_id,
                         const Inputs& inputs, float frame_ms);

private:
    struct Img {
        u8 id{};
        bool dirty{true};

        VideoCore::UniqueImage color;
        vk::UniqueImageView color_view;
        VideoCore::UniqueImage depth;
        vk::UniqueImageView depth_view;
        VideoCore::UniqueImage output;
        vk::UniqueImageView output_view;
    };

    void ResizeAndInvalidate(vk::Extent2D input, vk::Extent2D output);
    void CreateImages(Img& img) const;

    vk::Device device{};
    VmaAllocator allocator{};
    u32 num_images{};
    vk::Extent2D cur_input{}, cur_output{};
    u32 cur_image{};

    vk::UniqueSampler sampler{};
    vk::UniqueDescriptorSetLayout descriptor_set_layout{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniquePipeline color_copy_pipeline{};
    vk::UniquePipeline color_encode_pipeline{};
    vk::UniquePipeline depth_pipeline{};

    /// The feature the current run created; a size or mode change cannot rebuild it safely
    /// while the GPU may still use it, so DLSS then stays off for the session.
    std::optional<Dlss::FeatureDesc> feature;
    bool feature_failed{};
    bool reset{true};

    std::vector<Img> images;
};

} // namespace Vulkan::HostPasses
