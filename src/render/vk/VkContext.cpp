#define VOLK_IMPLEMENTATION
#include "render/vk/VkContext.h"
#include "util/File.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <set>

namespace bh {

const char* vkResultString(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "DEVICE_LOST";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "SURFACE_LOST";
        case VK_ERROR_OUT_OF_DATE_KHR: return "OUT_OF_DATE";
        case VK_SUBOPTIMAL_KHR: return "SUBOPTIMAL";
        default: return "VK_ERROR(other)";
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) LOG_ERROR("VK: %s", data->pMessage);
    else if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) LOG_WARN("VK: %s", data->pMessage);
    return VK_FALSE;
}

static bool hasExt(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list) if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

bool VkContext::init(GLFWwindow* window, bool validation, int gpuIndex, bool wantExternal) {
    if (volkInitialize() != VK_SUCCESS) {
        LOG_ERROR("Vulkan loader not found (libvulkan.so.1 / vulkan-1.dll)");
        return false;
    }
    if (window) glfwInitVulkanLoader(vkGetInstanceProcAddr);

    // --- instance --------------------------------------------------------
    std::vector<const char*> exts;
    if (window) {
        uint32_t n = 0;
        const char** g = glfwGetRequiredInstanceExtensions(&n);
        if (!g) { LOG_ERROR("GLFW: Vulkan not supported"); return false; }
        exts.assign(g, g + n);
    }
    std::vector<const char*> layers;
    if (validation) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> props(n);
        vkEnumerateInstanceLayerProperties(&n, props.data());
        for (auto& p : props)
            if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
        if (layers.empty()) LOG_WARN("Validation layer not available");
        else exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "blackhole_render";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = (uint32_t)exts.size();
    ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount = (uint32_t)layers.size();
    ici.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ici, nullptr, &instance);
    if (r != VK_SUCCESS) { LOG_ERROR("vkCreateInstance: %s", vkResultString(r)); return false; }
    volkLoadInstance(instance);

    if (!layers.empty() && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debugCb;
        vkCreateDebugUtilsMessengerEXT(instance, &dci, nullptr, &messenger_);
    }
    if (window) {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            LOG_ERROR("glfwCreateWindowSurface failed");
            return false;
        }
    }

    // --- physical device -------------------------------------------------
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) { LOG_ERROR("No Vulkan devices"); return false; }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());
    // Discrete GPUs first, keeping relative order.
    std::stable_sort(devs.begin(), devs.end(), [](VkPhysicalDevice a, VkPhysicalDevice b) {
        VkPhysicalDeviceProperties pa, pb;
        vkGetPhysicalDeviceProperties(a, &pa);
        vkGetPhysicalDeviceProperties(b, &pb);
        return (pa.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) > (pb.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    });
    physical = VK_NULL_HANDLE;
    for (uint32_t attempt = 0; attempt < count && physical == VK_NULL_HANDLE; ++attempt) {
        VkPhysicalDevice cand = devs[(gpuIndex + attempt) % count];
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(cand, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(cand, &qn, qf.data());
        for (uint32_t i = 0; i < qn; ++i) {
            bool ok = (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT);
            if (ok && surface) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(cand, i, surface, &present);
                ok = present == VK_TRUE;
            }
            if (ok) { physical = cand; queueFamily = i; break; }
        }
    }
    if (physical == VK_NULL_HANDLE) { LOG_ERROR("No suitable Vulkan queue family"); return false; }

    VkPhysicalDeviceIDProperties idp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &idp;
    vkGetPhysicalDeviceProperties2(physical, &p2);
    std::memcpy(deviceUuid, idp.deviceUUID, 16);
    deviceName = std::string(p2.properties.deviceName) + " / Vulkan " +
                 std::to_string(VK_VERSION_MAJOR(p2.properties.apiVersion)) + "." +
                 std::to_string(VK_VERSION_MINOR(p2.properties.apiVersion));
    LOG_INFO("Vulkan: %s", deviceName.c_str());
    vkGetPhysicalDeviceMemoryProperties(physical, &memProps_);

    // --- logical device --------------------------------------------------
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> avail(en);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &en, avail.data());
    std::vector<const char*> dexts;
    if (surface) dexts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef _WIN32
    const char* memExt = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
    const char* semExt = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;
#else
    const char* memExt = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
    const char* semExt = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
#endif
    if (wantExternal && hasExt(avail, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) && hasExt(avail, memExt) &&
        hasExt(avail, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) && hasExt(avail, semExt)) {
        dexts.insert(dexts.end(), {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, memExt,
                                   VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, semExt});
        externalSupported = true;
    } else if (wantExternal) {
        LOG_WARN("Vulkan external memory/semaphore extensions missing - CUDA interop disabled");
    }
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical, &supported);
    VkPhysicalDeviceFeatures features{};
    features.largePoints = supported.largePoints;
    largePoints = supported.largePoints == VK_TRUE;

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)dexts.size();
    dci.ppEnabledExtensionNames = dexts.data();
    dci.pEnabledFeatures = &features;
    r = vkCreateDevice(physical, &dci, nullptr, &device);
    if (r != VK_SUCCESS) { LOG_ERROR("vkCreateDevice: %s", vkResultString(r)); return false; }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily;
    VK_CHECK(vkCreateCommandPool(device, &pci, nullptr, &cmdPool));
    return true;
}

bool VkContext::createSwapchain(uint32_t w, uint32_t h, bool vsync) {
    if (!surface) return false;
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fn, formats.data());
    VkSurfaceFormatKHR fmt = formats[0];
    for (auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { fmt = f; break; }
    uint32_t pn = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &pn, nullptr);
    std::vector<VkPresentModeKHR> modes(pn);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &pn, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
            for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = swapchain;
    VkSwapchainKHR newSwap;
    VkResult r = vkCreateSwapchainKHR(device, &sci, nullptr, &newSwap);
    if (r != VK_SUCCESS) { LOG_ERROR("vkCreateSwapchainKHR: %s", vkResultString(r)); return false; }
    destroySwapchain();
    swapchain = newSwap;
    swapFormat = fmt.format;
    swapExtent = extent;
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    swapImages.resize(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data());
    swapViews.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = swapImages[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &swapViews[i]));
    }
    LOG_INFO("Swapchain %ux%u, %u images, present mode %d", extent.width, extent.height, n, (int)mode);
    return true;
}

void VkContext::destroySwapchain() {
    for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
    swapViews.clear();
    swapImages.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

uint32_t VkContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & props) == props) return i;
    LOG_ERROR("No suitable memory type");
    return 0;
}

BufferRes VkContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool exportable) {
    BufferRes b;
    b.size = size;
#ifdef _WIN32
    const VkExternalMemoryHandleTypeFlags extType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    const VkExternalMemoryHandleTypeFlags extType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    VkExternalMemoryBufferCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    ext.handleTypes = extType;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (exportable) bci.pNext = &ext;
    VK_CHECK(vkCreateBuffer(device, &bci, nullptr, &b.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b.buffer, &req);
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded.buffer = b.buffer;
    VkExportMemoryAllocateInfo exp{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    exp.handleTypes = extType;
    exp.pNext = &ded;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
    if (exportable) mai.pNext = &exp;
    VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &b.memory));
    VK_CHECK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) VK_CHECK(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped));
    return b;
}

ImageRes VkContext::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage) {
    ImageRes im;
    im.format = fmt; im.width = w; im.height = h;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &ici, nullptr, &im.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, im.image, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &im.memory));
    VK_CHECK(vkBindImageMemory(device, im.image, im.memory, 0));
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = im.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &im.view));
    return im;
}

void VkContext::destroy(BufferRes& b) {
    if (b.mapped) vkUnmapMemory(device, b.memory);
    if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device, b.memory, nullptr);
    b = BufferRes{};
}

void VkContext::destroy(ImageRes& i) {
    if (i.view) vkDestroyImageView(device, i.view, nullptr);
    if (i.image) vkDestroyImage(device, i.image, nullptr);
    if (i.memory) vkFreeMemory(device, i.memory, nullptr);
    i = ImageRes{};
}

VkSemaphore VkContext::createSemaphore(bool exportable) {
#ifdef _WIN32
    const VkExternalSemaphoreHandleTypeFlags extType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    const VkExternalSemaphoreHandleTypeFlags extType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    VkExportSemaphoreCreateInfo exp{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    exp.handleTypes = extType;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (exportable) sci.pNext = &exp;
    VkSemaphore s = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &s));
    return s;
}

bool VkContext::exportMemory(VkDeviceMemory mem, OsHandle& out) {
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    gi.memory = mem;
    gi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE h = nullptr;
    if (!vkGetMemoryWin32HandleKHR || vkGetMemoryWin32HandleKHR(device, &gi, &h) != VK_SUCCESS) return false;
    out.win32 = h;
#else
    VkMemoryGetFdInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    gi.memory = mem;
    gi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    if (!vkGetMemoryFdKHR || vkGetMemoryFdKHR(device, &gi, &fd) != VK_SUCCESS) return false;
    out.fd = fd;
#endif
    return true;
}

bool VkContext::exportSemaphore(VkSemaphore sem, OsHandle& out) {
#ifdef _WIN32
    VkSemaphoreGetWin32HandleInfoKHR gi{VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
    gi.semaphore = sem;
    gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE h = nullptr;
    if (!vkGetSemaphoreWin32HandleKHR || vkGetSemaphoreWin32HandleKHR(device, &gi, &h) != VK_SUCCESS) return false;
    out.win32 = h;
#else
    VkSemaphoreGetFdInfoKHR gi{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    gi.semaphore = sem;
    gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    if (!vkGetSemaphoreFdKHR || vkGetSemaphoreFdKHR(device, &gi, &fd) != VK_SUCCESS) return false;
    out.fd = fd;
#endif
    return true;
}

VkCommandBuffer VkContext::beginOneTime() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VkContext::endOneTime(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

VkShaderModule VkContext::loadShader(const std::filesystem::path& spv) {
    std::vector<uint8_t> code;
    if (!file::readBinary(spv, code) || code.size() % 4 != 0) {
        LOG_ERROR("Cannot read SPIR-V %s", spv.string().c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
    return m;
}

void VkContext::transition(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                           VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void VkContext::shutdown() {
    if (device) {
        vkDeviceWaitIdle(device);
        destroySwapchain();
        if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE; instance = VK_NULL_HANDLE; surface = VK_NULL_HANDLE; cmdPool = VK_NULL_HANDLE;
}

}  // namespace bh
