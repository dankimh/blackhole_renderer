#pragma once
// Vulkan instance/device/swapchain helpers (volk-loaded, no link-time libvulkan).
#include <volk.h>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace bh {

struct OsHandle { int fd = -1; void* win32 = nullptr; };

struct BufferRes {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};
struct ImageRes {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
};

class VkContext {
public:
    bool init(GLFWwindow* window, bool validation, int gpuIndex, bool wantExternal);
    void shutdown();

    bool createSwapchain(uint32_t w, uint32_t h, bool vsync);
    void destroySwapchain();

    BufferRes createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool exportable = false);
    ImageRes createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage);
    void destroy(BufferRes& b);
    void destroy(ImageRes& i);
    VkSemaphore createSemaphore(bool exportable);
    bool exportMemory(VkDeviceMemory mem, OsHandle& out);
    bool exportSemaphore(VkSemaphore sem, OsHandle& out);

    VkCommandBuffer beginOneTime();
    void endOneTime(VkCommandBuffer cmd);
    VkShaderModule loadShader(const std::filesystem::path& spv);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);
    void transition(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                    VkAccessFlags srcAccess, VkAccessFlags dstAccess);

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapExtent{};
    bool externalSupported = false;
    bool largePoints = false;
    uint8_t deviceUuid[16]{};
    std::string deviceName;

private:
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps_{};
};

const char* vkResultString(VkResult r);
#define VK_CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { LOG_ERROR("Vulkan error %s at %s:%d", ::bh::vkResultString(_r), __FILE__, __LINE__); } } while (0)

}  // namespace bh
