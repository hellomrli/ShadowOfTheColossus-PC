// SPDX-FileCopyrightText: Copyright 2026 IFreemz (shadps4_dlss bridge), bbport contributors,
// SotC-PC contributors
// SPDX-License-Identifier: MIT
//
// C interface between the port and sotc_dlss.dll. The port contains no NVIDIA code: it loads
// the bridge at run time when it is present (next to bb-probe.exe, with nvngx_dlss.dll) and
// keeps its own upscalers when it is absent.
//
// Derived from bloodborne_pc's bbport_dlss bridge (itself adapted from
// IFreemz/shadPS4-Bloodborne-DLSS-FSR). This variant is Super Resolution only (SotC-PC uses
// Streamline for DLSS Frame Generation) and adds the motion-vector scale, because the game's
// vectors are stored in UV units rather than render pixels.

#pragma once

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOTC_DLSS_BRIDGE_ABI 1

typedef void (*SotcDlssLogFn)(int warning, const char* message);

typedef struct SotcDlssImage {
    VkImage image;
    VkImageView view;
    VkImageSubresourceRange range;
    VkFormat format;
    uint32_t width;
    uint32_t height;
} SotcDlssImage;

typedef struct SotcDlssFeature {
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
    int32_t quality; // 0 DLAA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance
    int32_t depth_inverted;
    uint32_t preset; // NVSDK_NGX_DLSS_Hint_Render_Preset value, 0 = driver default
    int32_t hdr;     // 1: linear HDR colour with automatic exposure, 0: tonemapped colour
} SotcDlssFeature;

typedef struct SotcDlssEvaluate {
    SotcDlssImage color;  // sampled
    SotcDlssImage depth;  // sampled, hardware depth or R32F
    SotcDlssImage motion; // sampled, current to previous
    SotcDlssImage output; // storage, general layout
    float jitter_x;
    float jitter_y;
    int32_t reset;
    float frame_ms;
    float sharpness; // 0..1; 0 leaves the output unsharpened
    // Multiplies the motion vectors into render pixels. The game stores them as UV deltas
    // (curUV - prevUV), so the port passes the render width and height here; a game that
    // already stores pixels passes 1 and 1.
    float mv_scale_x;
    float mv_scale_y;
} SotcDlssEvaluate;

typedef struct SotcDlssApi {
    uint32_t abi;
    // UTF-16 paths: the directory with nvngx_dlss.dll, and a writable data directory.
    int32_t (*Configure)(const wchar_t* dll_directory, const wchar_t* data_directory,
                         SotcDlssLogFn log);
    // Vulkan extensions NGX needs. The arrays stay valid until Shutdown.
    int32_t (*InstanceExtensions)(uint32_t* count, const VkExtensionProperties** extensions);
    int32_t (*DeviceExtensions)(VkInstance instance, VkPhysicalDevice physical, uint32_t* count,
                                const VkExtensionProperties** extensions);
    int32_t (*Initialize)(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                          PFN_vkGetInstanceProcAddr get_instance_proc,
                          PFN_vkGetDeviceProcAddr get_device_proc);
    int32_t (*CreateFeature)(VkCommandBuffer command, const SotcDlssFeature* feature);
    int32_t (*Evaluate)(VkCommandBuffer command, const SotcDlssEvaluate* evaluate);
    // The caller must have waited for the GPU work that used the feature.
    void (*ReleaseFeature)(void);
    void (*Shutdown)(void);
} SotcDlssApi;

typedef const SotcDlssApi* (*SotcDlssGetApiFn)(void);

#ifdef __cplusplus
}
#endif
