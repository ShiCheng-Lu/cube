#include "render/vulkan.hpp"
#include "render/fbx-loader.hpp"

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFramesInFlight = 2;

/// a buffer, mapped for its whole life if it's in host-visible memory
struct Buffer {
    vk::raii::Buffer buffer{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    void* mapped = nullptr;
};

// GPU-side vertex layouts (std430-friendly, matching skin.comp)
struct SourceVertex {
    glm::vec4 position;
    glm::vec4 normal;
    glm::uvec4 joints;
    glm::vec4 weights;
};
struct GpuSkinnedVertex {   // skin.comp output, read back as vertex attributes
    glm::vec4 position;
    glm::vec4 normal;
};

struct Frame {
    vk::raii::CommandBuffer cmd{nullptr};
    vk::raii::Semaphore imageAvailable{nullptr};
    vk::raii::Fence inFlight{nullptr};
    Buffer bones;       // host-visible, written by the CPU each frame
    Buffer skinned;     // device-local, written by skin.comp, read by mesh.vert
    vk::raii::DescriptorSet skinDescriptors{nullptr};
};

uint32_t findMemoryType(const vk::raii::PhysicalDevice& physical, uint32_t typeBits,
                        vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memory = physical.getMemoryProperties();
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("no suitable Vulkan memory type");
}

std::vector<uint32_t> readSpirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("can't open shader " + path.string());
    size_t size = size_t(file.tellg());
    std::vector<uint32_t> code(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), std::streamsize(code.size() * sizeof(uint32_t)));
    return code;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    std::fprintf(stderr, "vulkan: %s\n", data->pMessage);
    return VK_FALSE;
}

} // namespace

struct Renderer::Impl {
    GLFWwindow* window;

    vk::raii::Context context;
    vk::raii::Instance instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT messenger{nullptr};
    vk::raii::SurfaceKHR surface{nullptr};
    vk::raii::PhysicalDevice physical{nullptr};
    uint32_t queueFamily = 0;
    vk::raii::Device device{nullptr};
    vk::raii::Queue queue{nullptr};

    vk::SurfaceFormatKHR surfaceFormat;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::Extent2D extent;
    vk::raii::SwapchainKHR swapchain{nullptr};
    std::vector<vk::Image> images;
    std::vector<vk::raii::ImageView> imageViews;
    std::vector<vk::raii::Semaphore> renderFinished;   // one per swapchain image
    vk::raii::Image depthImage{nullptr};
    vk::raii::DeviceMemory depthMemory{nullptr};
    vk::raii::ImageView depthView{nullptr};

    vk::raii::DescriptorSetLayout skinSetLayout{nullptr};
    vk::raii::PipelineLayout skinPipelineLayout{nullptr};
    vk::raii::Pipeline skinPipeline{nullptr};
    vk::raii::PipelineLayout meshPipelineLayout{nullptr};   // shared by the line pipeline
    vk::raii::Pipeline meshPipeline{nullptr};
    vk::raii::Pipeline linePipeline{nullptr};
    vk::raii::DescriptorPool descriptorPool{nullptr};
    vk::raii::CommandPool commandPool{nullptr};

    Buffer sources;     // static skinning input, read by skin.comp
    Buffer colors;      // static per-vertex color, read by mesh.vert
    Buffer indices;
    Buffer lines;
    uint32_t lineVertexCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t boneCount = 0;

    std::array<Frame, kFramesInFlight> frames;
    uint32_t frameIndex = 0;

    void createInstance();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createPipelines(const std::filesystem::path& shaderDir);
    vk::raii::Pipeline createGraphicsPipeline(const std::filesystem::path& shaderDir, const char* vert,
                                              const char* frag,
                                              const vk::PipelineVertexInputStateCreateInfo& vertexInput,
                                              vk::PrimitiveTopology topology, bool depthTest);
    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, const void* data = nullptr,
                        bool hostVisible = true);
    void recordCommands(Frame& frame, uint32_t imageIndex, std::span<const View> views);

    using View = Renderer::View;
    using LineVertex = Renderer::LineVertex;
};

void Renderer::Impl::createInstance() {
    vk::ApplicationInfo app("cube", 1, "cube", 1, VK_API_VERSION_1_3);

    uint32_t glfwCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
    if (!glfwExtensions) throw std::runtime_error("GLFW found no Vulkan support");
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
    std::vector<const char*> layers;
    vk::InstanceCreateFlags flags;

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

    bool validation = false;
#ifdef DEBUG
    for (const vk::LayerProperties& layer : context.enumerateInstanceLayerProperties()) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) validation = true;
    }
    if (validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
        std::fprintf(stderr, "validation layer not installed, running without it\n");
    }
#endif

    instance = vk::raii::Instance(context, vk::InstanceCreateInfo(flags, &app, layers, extensions));

    if (validation) {
        using Severity = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using Type = vk::DebugUtilsMessageTypeFlagBitsEXT;
        messenger = vk::raii::DebugUtilsMessengerEXT(instance, vk::DebugUtilsMessengerCreateInfoEXT(
            {}, Severity::eWarning | Severity::eError,
            Type::eGeneral | Type::eValidation | Type::ePerformance, debugCallback));
    }

    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
    surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

void Renderer::Impl::pickPhysicalDevice() {
    vk::raii::PhysicalDevices devices(instance);
    int bestScore = -1;
    for (vk::raii::PhysicalDevice& candidate : devices) {
        vk::PhysicalDeviceProperties properties = candidate.getProperties();
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;

        bool hasSwapchain = false;
        for (const vk::ExtensionProperties& ext : candidate.enumerateDeviceExtensionProperties()) {
            if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) hasSwapchain = true;
        }
        if (!hasSwapchain) continue;

        auto families = candidate.getQueueFamilyProperties();
        for (uint32_t i = 0; i < families.size(); ++i) {
            vk::QueueFlags needed = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
            if ((families[i].queueFlags & needed) != needed) continue;
            if (!candidate.getSurfaceSupportKHR(i, *surface)) continue;

            // prefer real GPUs over software rasterizers
            int score = properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu   ? 3
                      : properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ? 2
                      : properties.deviceType == vk::PhysicalDeviceType::eCpu          ? 0
                                                                                       : 1;
            if (score > bestScore) {
                bestScore = score;
                physical = std::move(candidate);
                queueFamily = i;
            }
            break;
        }
    }
    if (bestScore < 0) throw std::runtime_error("no Vulkan 1.3 GPU that can present to the window");
    std::printf("GPU: %s\n", physical.getProperties().deviceName.data());
}

void Renderer::Impl::createDevice() {
    float priority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, queueFamily, 1, &priority);
    std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceVulkan13Features> chain{
        vk::DeviceCreateInfo({}, queueInfo, {}, extensions),
        vk::PhysicalDeviceVulkan13Features()};
    chain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;

    device = vk::raii::Device(physical, chain.get<vk::DeviceCreateInfo>());
    queue = vk::raii::Queue(device, queueFamily, 0);

    std::vector<vk::SurfaceFormatKHR> formats = physical.getSurfaceFormatsKHR(*surface);
    surfaceFormat = formats.front();
    for (const vk::SurfaceFormatKHR& format : formats) {
        if ((format.format == vk::Format::eB8G8R8A8Srgb || format.format == vk::Format::eR8G8B8A8Srgb) &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            surfaceFormat = format;
            break;
        }
    }

    for (vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm}) {
        if (physical.getFormatProperties(format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            depthFormat = format;
            break;
        }
    }
    if (depthFormat == vk::Format::eUndefined) throw std::runtime_error("no depth format");
}

void Renderer::Impl::createSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {   // minimized: wait until there's something to draw
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }
    device.waitIdle();

    vk::SurfaceCapabilitiesKHR caps = physical.getSurfaceCapabilitiesKHR(*surface);
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = caps.currentExtent;
    } else {
        extent = vk::Extent2D(
            std::clamp(uint32_t(width), caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(uint32_t(height), caps.minImageExtent.height, caps.maxImageExtent.height));
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    vk::CompositeAlphaFlagBitsKHR alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    if (!(caps.supportedCompositeAlpha & alpha)) alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;

    vk::SwapchainCreateInfoKHR info(
        {}, *surface, imageCount, surfaceFormat.format, surfaceFormat.colorSpace, extent, 1,
        vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, {},
        caps.currentTransform, alpha, vk::PresentModeKHR::eFifo, VK_TRUE, *swapchain);
    swapchain = vk::raii::SwapchainKHR(device, info);

    images.clear();
    for (VkImage image : swapchain.getImages()) images.emplace_back(image);

    imageViews.clear();
    renderFinished.clear();
    for (vk::Image image : images) {
        imageViews.emplace_back(device, vk::ImageViewCreateInfo(
            {}, image, vk::ImageViewType::e2D, surfaceFormat.format, {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
        renderFinished.emplace_back(device, vk::SemaphoreCreateInfo());
    }

    depthView = nullptr;
    depthImage = vk::raii::Image(device, vk::ImageCreateInfo(
        {}, vk::ImageType::e2D, depthFormat, vk::Extent3D(extent, 1), 1, 1,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment));
    vk::MemoryRequirements requirements = depthImage.getMemoryRequirements();
    depthMemory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(
        requirements.size,
        findMemoryType(physical, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
    depthImage.bindMemory(*depthMemory, 0);
    depthView = vk::raii::ImageView(device, vk::ImageViewCreateInfo(
        {}, *depthImage, vk::ImageViewType::e2D, depthFormat, {},
        {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}));
}

void Renderer::Impl::createPipelines(const std::filesystem::path& shaderDir) {
    auto loadShader = [&](const char* name) {
        std::vector<uint32_t> code = readSpirv(shaderDir / name);
        return vk::raii::ShaderModule(device, vk::ShaderModuleCreateInfo({}, code));
    };

    // ---- skinning (compute): bones + source vertices -> skinned vertices
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings;
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i] = vk::DescriptorSetLayoutBinding(i, vk::DescriptorType::eStorageBuffer, 1,
                                                     vk::ShaderStageFlagBits::eCompute);
    }
    skinSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));
    vk::PushConstantRange skinPush(vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t));
    skinPipelineLayout = vk::raii::PipelineLayout(
        device, vk::PipelineLayoutCreateInfo({}, *skinSetLayout, skinPush));

    vk::raii::ShaderModule comp = loadShader("skin.comp.spv");
    skinPipeline = vk::raii::Pipeline(device, nullptr, vk::ComputePipelineCreateInfo(
        {}, vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *comp, "main"),
        *skinPipelineLayout));

    // ---- mesh (graphics): skinned vertices -> one view
    vk::PushConstantRange meshPush(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4));
    meshPipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, {}, meshPush));

    std::array meshBindings{
        vk::VertexInputBindingDescription(0, sizeof(GpuSkinnedVertex), vk::VertexInputRate::eVertex),
        vk::VertexInputBindingDescription(1, sizeof(glm::vec3), vk::VertexInputRate::eVertex),
    };
    std::array meshAttributes{
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuSkinnedVertex, position)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuSkinnedVertex, normal)),
        vk::VertexInputAttributeDescription(2, 1, vk::Format::eR32G32B32Sfloat, 0),
    };
    meshPipeline = createGraphicsPipeline(shaderDir, "mesh.vert.spv", "mesh.frag.spv",
                                          vk::PipelineVertexInputStateCreateInfo({}, meshBindings, meshAttributes),
                                          vk::PrimitiveTopology::eTriangleList, true);

    // ---- lines (graphics): overlay drawn on top of everything
    vk::VertexInputBindingDescription lineBinding(0, sizeof(LineVertex), vk::VertexInputRate::eVertex);
    std::array lineAttributes{
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(LineVertex, position)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(LineVertex, color)),
    };
    linePipeline = createGraphicsPipeline(shaderDir, "line.vert.spv", "line.frag.spv",
                                          vk::PipelineVertexInputStateCreateInfo({}, lineBinding, lineAttributes),
                                          vk::PrimitiveTopology::eLineList, false);
}

vk::raii::Pipeline Renderer::Impl::createGraphicsPipeline(
    const std::filesystem::path& shaderDir, const char* vertName, const char* fragName,
    const vk::PipelineVertexInputStateCreateInfo& vertexInput, vk::PrimitiveTopology topology, bool depthTest) {
    std::vector<uint32_t> vertCode = readSpirv(shaderDir / vertName);
    std::vector<uint32_t> fragCode = readSpirv(shaderDir / fragName);
    vk::raii::ShaderModule vert(device, vk::ShaderModuleCreateInfo({}, vertCode));
    vk::raii::ShaderModule frag(device, vk::ShaderModuleCreateInfo({}, fragCode));
    std::array stages{
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vert, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *frag, "main"),
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, topology);
    vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    // Back faces are culled: models need counter-clockwise front faces
    vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0, 0, 0, 1.0f);
    vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    vk::PipelineDepthStencilStateCreateInfo depth({}, depthTest, depthTest, vk::CompareOp::eLess);
    vk::PipelineColorBlendAttachmentState blendAttachment;
    blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, blendAttachment);
    std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain{
        vk::GraphicsPipelineCreateInfo({}, stages, &vertexInput, &inputAssembly, nullptr, &viewport,
                                       &raster, &multisample, &depth, &blend, &dynamic, *meshPipelineLayout),
        vk::PipelineRenderingCreateInfo(0, surfaceFormat.format, depthFormat)};
    return vk::raii::Pipeline(device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
}

Buffer Renderer::Impl::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, const void* data,
                                    bool hostVisible) {
    Buffer b;
    b.buffer = vk::raii::Buffer(device, vk::BufferCreateInfo({}, size, usage));
    vk::MemoryRequirements requirements = b.buffer.getMemoryRequirements();
    vk::MemoryPropertyFlags properties = hostVisible
        ? vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        : vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal);
    b.memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(
        requirements.size, findMemoryType(physical, requirements.memoryTypeBits, properties)));
    b.buffer.bindMemory(*b.memory, 0);
    if (hostVisible) {
        b.mapped = b.memory.mapMemory(0, size);
        if (data) std::memcpy(b.mapped, data, size);
    }
    return b;
}

void Renderer::Impl::recordCommands(Frame& frame, uint32_t imageIndex, std::span<const View> views) {
    vk::raii::CommandBuffer& cmd = frame.cmd;
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // Skin every vertex once; all views below share the result
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *skinPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *skinPipelineLayout, 0, *frame.skinDescriptors, {});
    cmd.pushConstants<uint32_t>(*skinPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, vertexCount);
    cmd.dispatch((vertexCount + 63) / 64, 1, 1);
    vk::MemoryBarrier skinned(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eVertexAttributeRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eVertexInput,
                        {}, skinned, {}, {});

    vk::ImageMemoryBarrier toAttachment(
        {}, vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        images[imageIndex], {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vk::ImageMemoryBarrier depthBarrier(
        vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        *depthImage, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, toAttachment);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, {}, {}, depthBarrier);

    vk::RenderingAttachmentInfo color(
        *imageViews[imageIndex], vk::ImageLayout::eColorAttachmentOptimal, {}, {}, {},
        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
        vk::ClearColorValue(std::array<float, 4>{0.02f, 0.02f, 0.03f, 1.0f}));
    vk::RenderingAttachmentInfo depth(
        *depthView, vk::ImageLayout::eDepthAttachmentOptimal, {}, {}, {},
        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
        vk::ClearDepthStencilValue(1.0f, 0));
    cmd.beginRendering(vk::RenderingInfo({}, vk::Rect2D({0, 0}, extent), 1, 0, color, &depth));

    // Views don't overlap, so one depth clear serves them all
    auto setView = [&](const View& view) {
        int x0 = std::clamp(view.rect.x, 0, int(extent.width));
        int y0 = std::clamp(view.rect.y, 0, int(extent.height));
        int x1 = std::clamp(view.rect.x + view.rect.z, 0, int(extent.width));
        int y1 = std::clamp(view.rect.y + view.rect.w, 0, int(extent.height));
        if (x1 <= x0 || y1 <= y0) return false;

        // negative height flips Vulkan's y-down clip space to GLM's y-up
        cmd.setViewport(0, vk::Viewport(float(view.rect.x), float(view.rect.y + view.rect.w),
                                        float(view.rect.z), -float(view.rect.w), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D({x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}));
        return true;
    };

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *meshPipeline);
    cmd.bindVertexBuffers(0, {*frame.skinned.buffer, *colors.buffer}, {0, 0});
    cmd.bindIndexBuffer(*indices.buffer, 0, vk::IndexType::eUint32);
    for (const View& view : views) {
        if (!setView(view)) continue;
        cmd.pushConstants<glm::mat4>(*meshPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                                     view.viewProj * view.model);
        cmd.drawIndexed(indexCount, 1, 0, 0, 0);
    }

    if (lineVertexCount > 0) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *linePipeline);
        cmd.bindVertexBuffers(0, *lines.buffer, {0});
        for (const View& view : views) {
            if (!view.lines || !setView(view)) continue;
            cmd.pushConstants<glm::mat4>(*meshPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, view.viewProj);
            cmd.draw(lineVertexCount, 1, 0, 0);
        }
    }
    cmd.endRendering();

    vk::ImageMemoryBarrier toPresent(
        vk::AccessFlagBits::eColorAttachmentWrite, {}, vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        images[imageIndex], {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, toPresent);
    cmd.end();
}

Renderer::Renderer(GLFWwindow* window, const AnimatedModel& model, std::span<const LineVertex> lines,
                   const std::filesystem::path& shaderDir)
    : impl_(std::make_unique<Impl>()) {
    Impl& r = *impl_;
    r.window = window;
    r.createInstance();
    r.pickPhysicalDevice();
    r.createDevice();
    r.createSwapchain();
    r.createPipelines(shaderDir);

    // Split the model's vertices into what skinning needs and what drawing needs
    const auto& vertices = model.vertices();
    const auto& indices = model.indices();
    std::vector<SourceVertex> sources;
    std::vector<glm::vec3> colors;
    sources.reserve(vertices.size());
    colors.reserve(vertices.size());
    for (const SkinnedVertex& v : vertices) {
        sources.push_back({glm::vec4(v.position, 1.0f), glm::vec4(v.normal, 0.0f), v.joints, v.weights});
        colors.push_back(v.color);
    }
    r.sources = r.createBuffer(sources.size() * sizeof(SourceVertex),
                               vk::BufferUsageFlagBits::eStorageBuffer, sources.data());
    r.colors = r.createBuffer(colors.size() * sizeof(glm::vec3),
                              vk::BufferUsageFlagBits::eVertexBuffer, colors.data());
    r.indices = r.createBuffer(indices.size() * sizeof(uint32_t),
                               vk::BufferUsageFlagBits::eIndexBuffer, indices.data());
    if (!lines.empty()) {
        r.lines = r.createBuffer(lines.size_bytes(), vk::BufferUsageFlagBits::eVertexBuffer, lines.data());
        r.lineVertexCount = uint32_t(lines.size());
    }
    r.vertexCount = uint32_t(vertices.size());
    r.indexCount = uint32_t(indices.size());
    r.boneCount = uint32_t(model.boneCount());

    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 3 * kFramesInFlight);
    r.descriptorPool = vk::raii::DescriptorPool(r.device, vk::DescriptorPoolCreateInfo(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, kFramesInFlight, poolSize));
    r.commandPool = vk::raii::CommandPool(r.device, vk::CommandPoolCreateInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, r.queueFamily));

    vk::raii::CommandBuffers cmds(r.device, vk::CommandBufferAllocateInfo(
        *r.commandPool, vk::CommandBufferLevel::ePrimary, kFramesInFlight));
    std::array<vk::DescriptorSetLayout, kFramesInFlight> layouts;
    layouts.fill(*r.skinSetLayout);
    vk::raii::DescriptorSets sets(r.device, vk::DescriptorSetAllocateInfo(*r.descriptorPool, layouts));

    vk::DeviceSize bonesSize = std::max<vk::DeviceSize>(r.boneCount, 1) * sizeof(glm::mat4);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        Frame& f = r.frames[i];
        f.cmd = std::move(cmds[i]);
        f.imageAvailable = vk::raii::Semaphore(r.device, vk::SemaphoreCreateInfo());
        f.inFlight = vk::raii::Fence(r.device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
        f.bones = r.createBuffer(bonesSize, vk::BufferUsageFlagBits::eStorageBuffer);
        f.skinned = r.createBuffer(
            vertices.size() * sizeof(GpuSkinnedVertex),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer,
            nullptr, /*hostVisible=*/false);
        f.skinDescriptors = std::move(sets[i]);

        std::array<vk::DescriptorBufferInfo, 3> buffers{
            vk::DescriptorBufferInfo(*f.bones.buffer, 0, VK_WHOLE_SIZE),
            vk::DescriptorBufferInfo(*r.sources.buffer, 0, VK_WHOLE_SIZE),
            vk::DescriptorBufferInfo(*f.skinned.buffer, 0, VK_WHOLE_SIZE),
        };
        r.device.updateDescriptorSets(vk::WriteDescriptorSet(
            *f.skinDescriptors, 0, 0, vk::DescriptorType::eStorageBuffer, {}, buffers), {});
    }
}

Renderer::~Renderer() {
    if (impl_ && *impl_->device) impl_->device.waitIdle();
}

glm::uvec2 Renderer::size() const {
    return {impl_->extent.width, impl_->extent.height};
}

void Renderer::drawFrame(std::span<const View> views, std::span<const glm::mat4> bones) {
    Impl& r = *impl_;
    Frame& frame = r.frames[r.frameIndex];
    (void)r.device.waitForFences(*frame.inFlight, VK_TRUE, std::numeric_limits<uint64_t>::max());

    // Some platforms (Wayland) never report the swapchain out of date on resize
    int width = 0, height = 0;
    glfwGetFramebufferSize(r.window, &width, &height);
    if (uint32_t(width) != r.extent.width || uint32_t(height) != r.extent.height) {
        r.createSwapchain();
        return;
    }

    uint32_t imageIndex;
    try {
        auto [result, index] = r.swapchain.acquireNextImage(
            std::numeric_limits<uint64_t>::max(), *frame.imageAvailable);
        imageIndex = index;
    } catch (const vk::OutOfDateKHRError&) {
        r.createSwapchain();
        return;
    }
    r.device.resetFences(*frame.inFlight);

    std::memcpy(frame.bones.mapped, bones.data(),
                std::min<size_t>(bones.size(), r.boneCount) * sizeof(glm::mat4));
    r.recordCommands(frame, imageIndex, views);

    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::Semaphore signal = *r.renderFinished[imageIndex];
    vk::CommandBuffer cmd = *frame.cmd;
    vk::Semaphore wait = *frame.imageAvailable;
    r.queue.submit(vk::SubmitInfo(wait, waitStage, cmd, signal), *frame.inFlight);

    vk::SwapchainKHR swapchain = *r.swapchain;
    try {
        vk::Result result = r.queue.presentKHR(vk::PresentInfoKHR(signal, swapchain, imageIndex));
        if (result == vk::Result::eSuboptimalKHR) r.createSwapchain();
    } catch (const vk::OutOfDateKHRError&) {
        r.createSwapchain();
    }

    r.frameIndex = (r.frameIndex + 1) % kFramesInFlight;
}
