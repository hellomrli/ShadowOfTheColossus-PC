// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: the host-side half of DLSS Super Resolution: converts the game's presented image and
// its depth to what NGX accepts, records the DLSS evaluation and returns the upscaled image.

#include "video_core/renderer_vulkan/host_passes/dlss_upscaler.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <vector>

#include "common/assert.h"
#include "common/scope_exit.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/dlss_color_comp.h"
#include "video_core/host_shaders/dlss_color_encode_comp.h"
#include "video_core/host_shaders/fg_depth_comp.h"
#include "video_core/renderer_vulkan/vk_dlss.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan::HostPasses {

namespace {

struct ColorParams {
    u32 width;
    u32 height;
};

struct DepthParams {
    u32 width;
    u32 height;
    u32 invert;
};

} // namespace

void DlssUpscaler::Create(vk::Device device, VmaAllocator allocator, u32 num_images) {
    this->device = device;
    this->allocator = allocator;
    this->num_images = num_images;

    sampler = Check<"create dlss sampler">(device.createSamplerUnique(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxAnisotropy = 1.0f,
        .minLod = -1000.0f,
        .maxLod = 1000.0f,
    }));

    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 2,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .pImmutableSamplers = &sampler.get(),
        },
    }};

    descriptor_set_layout =
        Check<"create dlss descriptor set layout">(device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
            .bindingCount = bindings.size(),
            .pBindings = bindings.data(),
        }));

    const std::array push_constants{
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .offset = 0,
            .size = sizeof(DepthParams),
        },
    };
    pipeline_layout = Check<"create dlss pipeline layout">(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout.get(),
        .pushConstantRangeCount = push_constants.size(),
        .pPushConstantRanges = push_constants.data(),
    }));
    SetObjectName(device, pipeline_layout.get(), "dlss pipeline layout");

    const auto make_pipeline = [&](const char* name, const auto& source) {
        const auto& module = CompileSPV(source, device);
        ASSERT(module);
        SetObjectName(device, module, name);
        const vk::ComputePipelineCreateInfo info{
            .stage{
                .stage = vk::ShaderStageFlagBits::eCompute,
                .module = module,
                .pName = "main",
            },
            .layout = pipeline_layout.get(),
        };
        auto pipeline = Check<"create dlss compute pipeline">(
            device.createComputePipelineUnique({}, info));
        device.destroyShaderModule(module);
        return pipeline;
    };
    color_copy_pipeline = make_pipeline("dlss color copy", DLSS_COLOR_COMP);
    color_encode_pipeline = make_pipeline("dlss color encode", DLSS_COLOR_ENCODE_COMP);
    depth_pipeline = make_pipeline("dlss depth", FG_DEPTH_COMP);

    images.resize(num_images);
    for (u32 i = 0; i < num_images; ++i) {
        auto& img = images[i];
        img.id = static_cast<u8>(i);
        img.color = VideoCore::UniqueImage(device, allocator);
        img.depth = VideoCore::UniqueImage(device, allocator);
        img.output = VideoCore::UniqueImage(device, allocator);
    }
}

void DlssUpscaler::Destroy() {
    // The ring images and views are owned by their wrappers; nothing else to release here.
    images.clear();
}

vk::ImageView DlssUpscaler::Render(Scheduler& scheduler, vk::ImageView color,
                                   vk::Format color_format, vk::Extent2D input_size,
                                   vk::Extent2D output_size, u32 frame_id, const Inputs& inputs,
                                   float frame_ms) {
    Dlss* dlss = Dlss::Get();
    if (!dlss || !dlss->Available() || feature_failed || frame_id >= images.size()) {
        return {};
    }
    if (color_format == vk::Format::eUndefined || input_size.width == 0 || input_size.height == 0 ||
        output_size.width == 0 || output_size.height == 0) {
        return {};
    }

    if (input_size != cur_input || output_size != cur_output) {
        if (feature) {
            // Rebuilding the NGX feature while the GPU may still be reading the previous one
            // is unsafe: keep DLSS off for the rest of the session and say why once.
            std::fprintf(stderr,
                         "SOTC DLSS: render/output size changed (%ux%u -> %ux%u); DLSS stays "
                         "off (restart the game after changing it)\n",
                         cur_input.width, cur_input.height, input_size.width, input_size.height);
            feature_failed = true;
            return {};
        }
        ResizeAndInvalidate(input_size, output_size);
        reset = true;
    }

    const Dlss::FeatureDesc desc{
        input_size.width, input_size.height, output_size.width, output_size.height,
        Dlss::QualityForScale(static_cast<float>(output_size.width) /
                              static_cast<float>(input_size.width)),
        inputs.hdr,
        false,
    };
    if (feature && !(*feature == desc)) {
        std::fprintf(stderr, "SOTC DLSS: feature parameters changed; keeping FSR 1\n");
        feature_failed = true;
        return {};
    }
    const bool create_feature = !feature;
    const auto cmdbuf = scheduler.CommandBuffer();

    Img& img = images[frame_id % num_images];
    if (img.dirty) {
        CreateImages(img);
    }

    constexpr vk::ImageSubresourceRange color_range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const auto barrier = [&](vk::Image image, vk::ImageLayout old_layout,
                             vk::ImageLayout new_layout, vk::PipelineStageFlags2 src_stage,
                             vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage,
                             vk::AccessFlags2 dst_access, vk::ImageAspectFlags aspect) {
        const vk::ImageMemoryBarrier2 b{
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .image = image,
            .subresourceRange{
                .aspectMask = aspect,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &b,
        });
    };

    const bool markers = EmulatorSettings.IsVkHostMarkersEnabled();
    if (markers) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/DLSS",
        });
    }
    SCOPE_EXIT {
        if (markers) {
            cmdbuf.endDebugUtilsLabelEXT();
        }
    };

    const u32 groups_x = (input_size.width + 7) / 8;
    const u32 groups_y = (input_size.height + 7) / 8;

    // Colour -> RGBA16F.
    barrier(img.color, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::ImageAspectFlagBits::eColor);

    const vk::DescriptorImageInfo color_src{
        .imageView = color,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo color_dst{
        .imageView = img.color_view.get(),
        .imageLayout = vk::ImageLayout::eGeneral,
    };
    const vk::DescriptorImageInfo sampler_info{
        .sampler = sampler.get(),
    };
    std::array color_writes{
        vk::WriteDescriptorSet{
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &color_src,
        },
        vk::WriteDescriptorSet{
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &color_dst,
        },
        vk::WriteDescriptorSet{
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &sampler_info,
        },
    };
    // sRGB views are decoded by the sampler, so the shader encodes the result back; a UNORM
    // A2R10G10B10 view already holds encoded values and is copied as is.
    const bool encode = color_format == vk::Format::eR8G8B8A8Srgb ||
                        color_format == vk::Format::eB8G8R8A8Srgb ||
                        color_format == vk::Format::eR8G8B8Srgb ||
                        color_format == vk::Format::eB8G8R8Srgb;
    const ColorParams color_params{input_size.width, input_size.height};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute,
                        encode ? color_encode_pipeline.get() : color_copy_pipeline.get());
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                color_writes);
    cmdbuf.pushConstants(pipeline_layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(color_params), &color_params);
    cmdbuf.dispatch(groups_x, groups_y, 1);

    barrier(img.color, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead,
            vk::ImageAspectFlagBits::eColor);

    // Depth -> R32F with 0 at the near plane (the game's buffer is reversed-Z: 1 - d).
    barrier(img.depth, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::ImageAspectFlagBits::eColor);

    const vk::DescriptorImageInfo depth_src{
        .imageView = inputs.depth_view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo depth_dst{
        .imageView = img.depth_view.get(),
        .imageLayout = vk::ImageLayout::eGeneral,
    };
    std::array depth_writes{
        vk::WriteDescriptorSet{
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &depth_src,
        },
        vk::WriteDescriptorSet{
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &depth_dst,
        },
    };
    const DepthParams depth_params{input_size.width, input_size.height, 1};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, depth_pipeline.get());
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                depth_writes);
    cmdbuf.pushConstants(pipeline_layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(depth_params), &depth_params);
    cmdbuf.dispatch(groups_x, groups_y, 1);

    barrier(img.depth, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead,
            vk::ImageAspectFlagBits::eColor);

    // DLSS writes the output in General layout.
    barrier(img.output, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::ImageAspectFlagBits::eColor);

    const Dlss::Frame frame{
        .color{img.color, img.color_view.get(), vk::Format::eR16G16B16A16Sfloat,
               vk::ImageAspectFlagBits::eColor, input_size.width, input_size.height},
        .depth{img.depth, img.depth_view.get(), vk::Format::eR32Sfloat,
               vk::ImageAspectFlagBits::eColor, input_size.width, input_size.height},
        .motion{inputs.motion, inputs.motion_view, vk::Format::eR16G16Sfloat,
                vk::ImageAspectFlagBits::eColor, input_size.width, input_size.height},
        .output{img.output, img.output_view.get(), vk::Format::eR16G16B16A16Sfloat,
                vk::ImageAspectFlagBits::eColor, output_size.width, output_size.height},
        .jitter_x = inputs.jitter_x,
        .jitter_y = inputs.jitter_y,
        .reset = inputs.reset || reset || create_feature,
        .frame_ms = frame_ms,
        .sharpness = inputs.sharpness,
        .mv_scale_x = inputs.mv_scale_x,
        .mv_scale_y = inputs.mv_scale_y,
    };
    // sotc: NGX loads raw Vulkan entrypoints, bypassing our fake-handle dispatcher.
    // Both calls must run on the command buffer's recording thread after the conversions.
    const bool evaluated = scheduler.RecordSynchronous([&](vk::CommandBuffer real) {
        if (create_feature && !dlss->CreateFeature(real, desc)) {
            std::fprintf(stderr, "SOTC DLSS: feature creation failed; keeping FSR 1\n");
            return false;
        }
        if (!dlss->Evaluate(real, frame)) {
            std::fprintf(stderr, "SOTC DLSS: evaluation failed; keeping FSR 1\n");
            return false;
        }
        return true;
    });
    if (!evaluated) {
        feature_failed = true;
        return {};
    }
    feature = desc;
    reset = false;

    barrier(img.output, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
            vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead,
            vk::ImageAspectFlagBits::eColor);

    return img.output_view.get();
}

void DlssUpscaler::ResizeAndInvalidate(vk::Extent2D input, vk::Extent2D output) {
    cur_input = input;
    cur_output = output;
    for (auto& img : images) {
        img.dirty = true;
    }
}

void DlssUpscaler::CreateImages(Img& img) const {
    img.dirty = false;
    img.color_view.reset();
    img.depth_view.reset();
    img.output_view.reset();
    img.color.Destroy();
    img.depth.Destroy();
    img.output.Destroy();

    const auto make_image = [&](VideoCore::UniqueImage& texture, vk::Format format,
                                vk::Extent2D size, const char* name) {
        texture.Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent{size.width, size.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        SetObjectName(device, static_cast<vk::Image>(texture), "{} #{}", name, img.id);
    };
    const auto make_view = [&](vk::Image image, vk::Format format, const char* name) {
        return Check<"create dlss image view">(device.createImageViewUnique(vk::ImageViewCreateInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
        }));
    };

    make_image(img.color, vk::Format::eR16G16B16A16Sfloat, cur_input, "DLSS Color");
    img.color_view =
        make_view(img.color, vk::Format::eR16G16B16A16Sfloat, "DLSS Color View");
    make_image(img.depth, vk::Format::eR32Sfloat, cur_input, "DLSS Depth");
    img.depth_view = make_view(img.depth, vk::Format::eR32Sfloat, "DLSS Depth View");
    make_image(img.output, vk::Format::eR16G16B16A16Sfloat, cur_output, "DLSS Output");
    img.output_view =
        make_view(img.output, vk::Format::eR16G16B16A16Sfloat, "DLSS Output View");
}

} // namespace Vulkan::HostPasses
