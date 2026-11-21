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

/// a buffer in host-visible memory, mapped for its whole life
struct Buffer {
    vk::raii::Buffer buffer{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    void* mapped = nullptr;
};

struct Frame {
    vk::raii::CommandBuffer cmd{nullptr};
    vk::raii::Semaphore imageAvailable{nullptr};
    vk::raii::Fence inFlight{nullptr};
    Buffer bones;
    vk::raii::DescriptorSet descriptors{nullptr};
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

    vk::raii::DescriptorSetLayout setLayout{nullptr};
    vk::raii::PipelineLayout pipelineLayout{nullptr};
    vk::raii::Pipeline pipeline{nullptr};
    vk::raii::DescriptorPool descriptorPool{nullptr};
    vk::raii::CommandPool commandPool{nullptr};

    Buffer vertices;
    Buffer indices;
    uint32_t indexCount = 0;
    uint32_t boneCount = 0;

    std::array<Frame, kFramesInFlight> frames;
    uint32_t frameIndex = 0;

    void createInstance();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createPipeline(const std::filesystem::path& shaderDir);
    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, const void* data = nullptr);
    void recordCommands(Frame& frame, uint32_t imageIndex, const glm::mat4& viewProj);
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
            if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
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

void Renderer::Impl::createPipeline(const std::filesystem::path& shaderDir) {
    vk::DescriptorSetLayoutBinding binding(0, vk::DescriptorType::eStorageBuffer, 1,
                                           vk::ShaderStageFlagBits::eVertex);
    setLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, binding));

    vk::PushConstantRange push(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4));
    pipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, *setLayout, push));

    std::vector<uint32_t> vertCode = readSpirv(shaderDir / "skinned.vert.spv");
    std::vector<uint32_t> fragCode = readSpirv(shaderDir / "skinned.frag.spv");
    vk::raii::ShaderModule vert(device, vk::ShaderModuleCreateInfo({}, vertCode));
    vk::raii::ShaderModule frag(device, vk::ShaderModuleCreateInfo({}, fragCode));
    std::array stages{
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vert, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *frag, "main"),
    };

    vk::VertexInputBindingDescription vertexBinding(0, sizeof(SkinnedVertex), vk::VertexInputRate::eVertex);
    std::array attributes{
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(SkinnedVertex, position)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(SkinnedVertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(SkinnedVertex, color)),
        vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Uint, offsetof(SkinnedVertex, joints)),
        vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(SkinnedVertex, weights)),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInput({}, vertexBinding, attributes);
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
    vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    // FBX winding isn't reliable across exporters, so draw both sides
    vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0, 0, 0, 1.0f);
    vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    vk::PipelineDepthStencilStateCreateInfo depth({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess);
    vk::PipelineColorBlendAttachmentState blendAttachment;
    blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, blendAttachment);
    std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain{
        vk::GraphicsPipelineCreateInfo({}, stages, &vertexInput, &inputAssembly, nullptr, &viewport,
                                       &raster, &multisample, &depth, &blend, &dynamic, *pipelineLayout),
        vk::PipelineRenderingCreateInfo(0, surfaceFormat.format, depthFormat)};
    pipeline = vk::raii::Pipeline(device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
}

Buffer Renderer::Impl::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, const void* data) {
    Buffer b;
    b.buffer = vk::raii::Buffer(device, vk::BufferCreateInfo({}, size, usage));
    vk::MemoryRequirements requirements = b.buffer.getMemoryRequirements();
    b.memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(
        requirements.size,
        findMemoryType(physical, requirements.memoryTypeBits,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
    b.buffer.bindMemory(*b.memory, 0);
    b.mapped = b.memory.mapMemory(0, size);
    if (data) std::memcpy(b.mapped, data, size);
    return b;
}

void Renderer::Impl::recordCommands(Frame& frame, uint32_t imageIndex, const glm::mat4& viewProj) {
    vk::raii::CommandBuffer& cmd = frame.cmd;
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

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

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    // negative height flips Vulkan's y-down clip space to GLM's y-up
    cmd.setViewport(0, vk::Viewport(0.0f, float(extent.height), float(extent.width),
                                    -float(extent.height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D({0, 0}, extent));
    cmd.bindVertexBuffers(0, *vertices.buffer, {0});
    cmd.bindIndexBuffer(*indices.buffer, 0, vk::IndexType::eUint32);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *frame.descriptors, {});
    cmd.pushConstants<glm::mat4>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, viewProj);
    cmd.drawIndexed(indexCount, 1, 0, 0, 0);
    cmd.endRendering();

    vk::ImageMemoryBarrier toPresent(
        vk::AccessFlagBits::eColorAttachmentWrite, {}, vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        images[imageIndex], {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, toPresent);
    cmd.end();
}

Renderer::Renderer(GLFWwindow* window, const AnimatedModel& model, const std::filesystem::path& shaderDir)
    : impl_(std::make_unique<Impl>()) {
    Impl& r = *impl_;
    r.window = window;
    r.createInstance();
    r.pickPhysicalDevice();
    r.createDevice();
    r.createSwapchain();
    r.createPipeline(shaderDir);

    const auto& vertices = model.vertices();
    const auto& indices = model.indices();
    r.vertices = r.createBuffer(vertices.size() * sizeof(SkinnedVertex),
                                vk::BufferUsageFlagBits::eVertexBuffer, vertices.data());
    r.indices = r.createBuffer(indices.size() * sizeof(uint32_t),
                               vk::BufferUsageFlagBits::eIndexBuffer, indices.data());
    r.indexCount = uint32_t(indices.size());
    r.boneCount = uint32_t(model.boneCount());

    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, kFramesInFlight);
    r.descriptorPool = vk::raii::DescriptorPool(r.device, vk::DescriptorPoolCreateInfo(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, kFramesInFlight, poolSize));
    r.commandPool = vk::raii::CommandPool(r.device, vk::CommandPoolCreateInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, r.queueFamily));

    vk::raii::CommandBuffers cmds(r.device, vk::CommandBufferAllocateInfo(
        *r.commandPool, vk::CommandBufferLevel::ePrimary, kFramesInFlight));
    std::array<vk::DescriptorSetLayout, kFramesInFlight> layouts;
    layouts.fill(*r.setLayout);
    vk::raii::DescriptorSets sets(r.device, vk::DescriptorSetAllocateInfo(*r.descriptorPool, layouts));

    vk::DeviceSize bonesSize = std::max<vk::DeviceSize>(r.boneCount, 1) * sizeof(glm::mat4);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        Frame& f = r.frames[i];
        f.cmd = std::move(cmds[i]);
        f.imageAvailable = vk::raii::Semaphore(r.device, vk::SemaphoreCreateInfo());
        f.inFlight = vk::raii::Fence(r.device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
        f.bones = r.createBuffer(bonesSize, vk::BufferUsageFlagBits::eStorageBuffer);
        f.descriptors = std::move(sets[i]);

        vk::DescriptorBufferInfo bufferInfo(*f.bones.buffer, 0, VK_WHOLE_SIZE);
        r.device.updateDescriptorSets(vk::WriteDescriptorSet(
            *f.descriptors, 0, 0, vk::DescriptorType::eStorageBuffer, {}, bufferInfo), {});
    }
}

Renderer::~Renderer() {
    if (impl_ && *impl_->device) impl_->device.waitIdle();
}

float Renderer::aspect() const {
    return float(impl_->extent.width) / float(impl_->extent.height);
}

void Renderer::drawFrame(const glm::mat4& viewProj, std::span<const glm::mat4> bones) {
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
    r.recordCommands(frame, imageIndex, viewProj);

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
