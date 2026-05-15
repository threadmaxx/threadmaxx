#include "VulkanContext.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace threadmaxx_vk {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool layerAvailable(const char* name) {
    std::uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (const auto& l : layers) {
        if (std::strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[vulkan_validation] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

void populateDebugCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& info) {
    info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
}

bool deviceSupportsRequired(VkPhysicalDevice dev) {
    VkPhysicalDeviceVulkan13Features v13 = {};
    v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features v12 = {};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12.pNext = &v13;
    VkPhysicalDeviceFeatures2 feat = {};
    feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat.pNext = &v12;
    vkGetPhysicalDeviceFeatures2(dev, &feat);
    return v13.dynamicRendering && v13.synchronization2 &&
           v12.timelineSemaphore;
}

} // namespace

VulkanContext::~VulkanContext() { shutdown(); }

bool VulkanContext::init(GLFWwindow* window, bool enableValidation) {
    // ---- Instance ----------------------------------------------------------
    std::uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);

    const bool wantValidation = enableValidation && layerAvailable(kValidationLayer);
    if (wantValidation) {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "threadmaxx_vulkan_renderer";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "threadmaxx";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ic = {};
    ic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ic.ppEnabledExtensionNames = exts.data();

    const char* layers[] = { kValidationLayer };
    VkDebugUtilsMessengerCreateInfoEXT dbgCI = {};
    if (wantValidation) {
        ic.enabledLayerCount = 1;
        ic.ppEnabledLayerNames = layers;
        populateDebugCreateInfo(dbgCI);
        ic.pNext = &dbgCI;
    }

    if (vkCreateInstance(&ic, nullptr, &instance_) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan_renderer] vkCreateInstance failed\n");
        return false;
    }

    if (wantValidation) {
        auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (createFn) {
            VkDebugUtilsMessengerCreateInfoEXT info;
            populateDebugCreateInfo(info);
            createFn(instance_, &info, nullptr, &debugMessenger_);
        }
    }

    // ---- Surface -----------------------------------------------------------
    if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan_renderer] glfwCreateWindowSurface failed\n");
        return false;
    }

    // ---- Physical device ---------------------------------------------------
    std::uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance_, &devCount, nullptr);
    if (devCount == 0) {
        std::fprintf(stderr, "[vulkan_renderer] no Vulkan physical devices found\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(instance_, &devCount, devices.data());

    auto scoreDevice = [&](VkPhysicalDevice d) -> int {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) return -1;
        if (!deviceSupportsRequired(d)) return -1;
        int s = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   s += 1000;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) s += 100;
        return s;
    };
    int bestScore = -1;
    for (auto d : devices) {
        const int s = scoreDevice(d);
        if (s > bestScore) {
            bestScore = s;
            physicalDevice_ = d;
        }
    }
    if (bestScore < 0) {
        std::fprintf(stderr, "[vulkan_renderer] no Vulkan 1.3-capable device with "
                             "dynamic_rendering / sync2 / timelineSemaphore\n");
        return false;
    }

    // ---- Queue families ----------------------------------------------------
    std::uint32_t famCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &famCount, nullptr);
    std::vector<VkQueueFamilyProperties> fams(famCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &famCount, fams.data());

    bool foundGfx = false;
    bool foundPresent = false;
    for (std::uint32_t i = 0; i < famCount; ++i) {
        if (!foundGfx && (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            graphicsQueueIndex_ = i;
            foundGfx = true;
        }
        VkBool32 supportsPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_,
                                             &supportsPresent);
        if (!foundPresent && supportsPresent) {
            presentQueueIndex_ = i;
            foundPresent = true;
        }
    }
    if (!foundGfx || !foundPresent) {
        std::fprintf(stderr, "[vulkan_renderer] required queue families missing\n");
        return false;
    }

    // ---- Logical device ----------------------------------------------------
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCIs;
    {
        VkDeviceQueueCreateInfo q = {};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = graphicsQueueIndex_;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueCIs.push_back(q);
    }
    if (presentQueueIndex_ != graphicsQueueIndex_) {
        VkDeviceQueueCreateInfo q = {};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = presentQueueIndex_;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueCIs.push_back(q);
    }

    VkPhysicalDeviceVulkan13Features v13 = {};
    v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    v13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceVulkan12Features v12 = {};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12.timelineSemaphore = VK_TRUE;
    v12.pNext = &v13;

    VkPhysicalDeviceFeatures2 feat = {};
    feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat.features.fillModeNonSolid = VK_TRUE;
    feat.features.wideLines        = VK_TRUE;
    feat.features.largePoints      = VK_TRUE;
    feat.pNext = &v12;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dc = {};
    dc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dc.queueCreateInfoCount = static_cast<std::uint32_t>(queueCIs.size());
    dc.pQueueCreateInfos = queueCIs.data();
    dc.enabledExtensionCount = 1;
    dc.ppEnabledExtensionNames = deviceExtensions;
    dc.pNext = &feat;

    if (vkCreateDevice(physicalDevice_, &dc, nullptr, &device_) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan_renderer] vkCreateDevice failed\n");
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueIndex_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueIndex_,  0, &presentQueue_);
    return true;
}

void VulkanContext::shutdown() noexcept {
    if (device_) { vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; }
    if (surface_ && instance_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (debugMessenger_ && instance_) {
        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn) destroyFn(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (instance_) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_  = VK_NULL_HANDLE;
    presentQueue_   = VK_NULL_HANDLE;
}

std::uint32_t VulkanContext::findMemoryType(std::uint32_t typeBits,
                                            VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
    for (std::uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    std::fprintf(stderr, "[vulkan_renderer] no matching memory type\n");
    std::abort();
}

} // namespace threadmaxx_vk
