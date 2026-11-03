/* SPDX-License-Identifier: GPL-3.0-or-later */
/* VK_LAYER_DESKTOP_MELEE_overlay_alpha: a Vulkan layer that only Desktop
 * Melee loads, on Linux, for its overlay window (platform_linux.c sets it up).
 *
 * Dawn, in the version aurora pins, creates every Vulkan swap chain with
 * VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR whatever alpha mode is asked for (Google
 * fixed that on 2026-09-15, "Honor the surface's alphaMode on the Vulkan
 * swapchain"). An opaque swap chain gets buffers without alpha, so the
 * compositor shows the overlay's transparent pixels as black. This layer
 * turns the swap chain's opaque into premultiplied (or inherit) when the
 * surface supports it, and changes nothing else. Remove it once aurora's
 * Dawn has the fix. */
#include <stdbool.h>
#include <string.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#if defined(__GNUC__)
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

#define MAX_HANDLES 8

/* What the next layer (or the driver) answers for each instance and device:
 * Dawn makes very few of either. */
static struct {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr next_gipa;
} s_instances[MAX_HANDLES];

static struct {
    VkDevice device;
    VkPhysicalDevice physical;
    PFN_vkGetDeviceProcAddr next_gdpa;
    PFN_vkCreateSwapchainKHR next_create_swapchain;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR surface_capabilities;
} s_devices[MAX_HANDLES];

/* The newest instance: a device's physical device belongs to it (Dawn makes
 * one Vulkan instance). */
static VkInstance newest_instance(void) {
    VkInstance newest = VK_NULL_HANDLE;
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (s_instances[i].instance != VK_NULL_HANDLE) {
            newest = s_instances[i].instance;
        }
    }
    return newest;
}

static PFN_vkGetInstanceProcAddr instance_gipa(VkInstance instance) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (s_instances[i].instance == instance) {
            return s_instances[i].next_gipa;
        }
    }
    return s_instances[0].next_gipa;
}

static int device_index(VkDevice device) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (s_devices[i].device == device) {
            return i;
        }
    }
    return -1;
}

/* The loader's link for this layer, in a create info's pNext chain. */
static void* find_link(const void* chain, VkStructureType type) {
    for (VkBaseInStructure* s = (VkBaseInStructure*)chain; s != NULL;
         s = (VkBaseInStructure*)s->pNext)
    {
        if (s->sType == type && ((VkLayerInstanceCreateInfo*)s)->function == VK_LAYER_LINK_INFO) {
            return s;
        }
    }
    return NULL;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkInstance* instance) {
    VkLayerInstanceCreateInfo* link =
        find_link(info->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO);
    if (link == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)next(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult result = create(info, allocator, instance);
    if (result == VK_SUCCESS) {
        for (int i = 0; i < MAX_HANDLES; i++) {
            if (s_instances[i].instance == VK_NULL_HANDLE) {
                s_instances[i].instance = *instance;
                s_instances[i].next_gipa = next;
                break;
            }
        }
    }
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice physical,
    const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device) {
    VkLayerDeviceCreateInfo* link =
        find_link(info->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO);
    if (link == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const VkInstance instance = newest_instance();
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)next_gipa(instance, "vkCreateDevice");
    if (create == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = create(physical, info, allocator, device);
    if (result == VK_SUCCESS) {
        for (int i = 0; i < MAX_HANDLES; i++) {
            if (s_devices[i].device == VK_NULL_HANDLE) {
                s_devices[i].device = *device;
                s_devices[i].physical = physical;
                s_devices[i].next_gdpa = next_gdpa;
                s_devices[i].next_create_swapchain =
                    (PFN_vkCreateSwapchainKHR)next_gdpa(*device, "vkCreateSwapchainKHR");
                s_devices[i].surface_capabilities =
                    (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)next_gipa(
                        instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
                break;
            }
        }
    }
    return result;
}

/* The same Dawn also refuses a surface that can't be opaque, which an X11
 * window with an ARGB visual can't. So the surface's capabilities say opaque
 * too, when it has premultiplied or inherit to stand in for it. */
static VkCompositeAlphaFlagsKHR with_opaque(VkCompositeAlphaFlagsKHR supported) {
    const VkCompositeAlphaFlagsKHR stand_ins =
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    return (supported & stand_ins) != 0 ? supported | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : supported;
}

static VKAPI_ATTR VkResult VKAPI_CALL surface_capabilities(
    VkPhysicalDevice physical, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* caps) {
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR next =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)instance_gipa(newest_instance())(
            newest_instance(), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    const VkResult result = next(physical, surface, caps);
    if (result == VK_SUCCESS) {
        caps->supportedCompositeAlpha = with_opaque(caps->supportedCompositeAlpha);
    }
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL surface_capabilities2(VkPhysicalDevice physical,
    const VkPhysicalDeviceSurfaceInfo2KHR* info, VkSurfaceCapabilities2KHR* caps) {
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR next =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)instance_gipa(newest_instance())(
            newest_instance(), "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    const VkResult result = next(physical, info, caps);
    if (result == VK_SUCCESS) {
        caps->surfaceCapabilities.supportedCompositeAlpha =
            with_opaque(caps->surfaceCapabilities.supportedCompositeAlpha);
    }
    return result;
}

/* Opaque becomes premultiplied (Wayland), else inherit (X11, with the
 * window's ARGB visual), when the surface really supports it; the real
 * capabilities come from the next layer, not from surface_capabilities. */
static VKAPI_ATTR VkResult VKAPI_CALL create_swapchain(VkDevice device,
    const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator,
    VkSwapchainKHR* swapchain) {
    const int d = device_index(device);
    if (d < 0 || s_devices[d].next_create_swapchain == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSwapchainCreateInfoKHR changed = *info;
    VkSurfaceCapabilitiesKHR caps;
    if (info->compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR &&
        s_devices[d].surface_capabilities != NULL &&
        s_devices[d].surface_capabilities(s_devices[d].physical, info->surface, &caps) ==
            VK_SUCCESS)
    {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
            changed.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            changed.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
    }
    return s_devices[d].next_create_swapchain(device, &changed, allocator, swapchain);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_device_proc_addr(
    VkDevice device, const char* name) {
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)get_device_proc_addr;
    }
    if (strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return (PFN_vkVoidFunction)create_swapchain;
    }
    const int d = device_index(device);
    return d >= 0 ? s_devices[d].next_gdpa(device, name) : NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_instance_proc_addr(
    VkInstance instance, const char* name) {
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return (PFN_vkVoidFunction)get_instance_proc_addr;
    }
    if (strcmp(name, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)create_instance;
    }
    if (strcmp(name, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)create_device;
    }
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)get_device_proc_addr;
    }
    if (strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return (PFN_vkVoidFunction)create_swapchain;
    }
    if (strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0) {
        return (PFN_vkVoidFunction)surface_capabilities;
    }
    if (strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilities2KHR") == 0) {
        return (PFN_vkVoidFunction)surface_capabilities2;
    }
    PFN_vkGetInstanceProcAddr next = instance_gipa(instance);
    return next != NULL ? next(instance, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* interface) {
    if (interface->loaderLayerInterfaceVersion > 2) {
        interface->loaderLayerInterfaceVersion = 2;
    }
    interface->pfnGetInstanceProcAddr = get_instance_proc_addr;
    interface->pfnGetDeviceProcAddr = get_device_proc_addr;
    interface->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
