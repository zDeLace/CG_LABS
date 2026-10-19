#include "application.hpp"

#include <imgui.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <GLFW/glfw3.h>

namespace application {
namespace {
constexpr uint32_t SEGMENTS = 50;
constexpr float PI = 3.14159265358979323846f;

struct Vertex {
    float position[3];
    float color[3];
};
struct Mat4 { float v[16]{}; };
struct UniformData { Mat4 mvp; float tint[4]; };

VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexAllocation = VK_NULL_HANDLE;
VkBuffer indexBuffer = VK_NULL_HANDLE;
VmaAllocation indexAllocation = VK_NULL_HANDLE;
VkBuffer uniformBuffer = VK_NULL_HANDLE;
VmaAllocation uniformAllocation = VK_NULL_HANDLE;
void* uniformMapped = nullptr;
VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;
uint32_t indexCount = 0;
float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
float cameraRotationX = -0.35f;
float cameraRotationY = 0.65f;

bool isDragging = false;
double lastMouseX = 0.0;
double lastMouseY = 0.0;

float mouseSensitivity = 0.01f;


int projectionMode = 0; // 0 — перспектива, 1 — ортографическая

float objectPosition[3] = {0.0f, 0.0f, 0.0f};
float objectRotation[3] = {0.0f, 0.0f, 0.0f};
float objectScale[3] = {1.0f, 1.0f, 1.0f};

bool animationPlaying = true;
float animationSpeed = 1.0f;
float animationRadius = 1.0f;
double animationTime = 0.0;
double previousFrameTime = -1.0;


Mat4 identity() {
    Mat4 m{};
    m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1.0f;
    return m;
}
Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r.v[col * 4 + row] += a.v[k * 4 + row] * b.v[col * 4 + k];
    return r;
}
Mat4 rotationX(float a) {
    Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a);
    m.v[5] = c; m.v[9] = -s; m.v[6] = s; m.v[10] = c; return m;
}
Mat4 rotationY(float a) {
    Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a);
    m.v[0] = c; m.v[8] = s; m.v[2] = -s; m.v[10] = c; return m;
}

Mat4 rotationZ(float a) {
    Mat4 m = identity();
    const float c = std::cos(a), s = std::sin(a);
    m.v[0] = c;
    m.v[4] = -s;
    m.v[1] = s;
    m.v[5] = c;
    return m;
}

Mat4 translation(float x, float y, float z) {
    Mat4 m = identity();
    m.v[12] = x;
    m.v[13] = y;
    m.v[14] = z;
    return m;
}

Mat4 scaling(float x, float y, float z) {
    Mat4 m = identity();
    m.v[0] = x;
    m.v[5] = y;
    m.v[10] = z;
    return m;
}

Mat4 orthographic(float width, float height,
                  float nearZ, float farZ) {
    Mat4 m{};
    m.v[0] = 2.0f / width;
    m.v[5] = -2.0f / height;
    m.v[10] = 1.0f / (nearZ - farZ);
    m.v[14] = nearZ / (nearZ - farZ);
    m.v[15] = 1.0f;
    return m;
}

Mat4 perspective(float fovy, float aspect, float nearZ, float farZ) {
    Mat4 m{}; const float f = 1.0f / std::tan(fovy * 0.5f);
    m.v[0] = f / aspect; m.v[5] = -f;
    m.v[10] = farZ / (nearZ - farZ); m.v[11] = -1.0f;
    m.v[14] = (farZ * nearZ) / (nearZ - farZ);
    return m;
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkBuffer& buffer, VmaAllocation& allocation, void** mapped = nullptr) {
    auto& c = graphics::internal::context;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    if (mapped) ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(c.allocator, &bi, &ai, &buffer, &allocation, &info) != VK_SUCCESS) return false;
    if (mapped) *mapped = info.pMappedData;
    return true;
}

bool createCylinderGeometry() {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(SEGMENTS * 2 + 2);
    indices.reserve(SEGMENTS * 12);

    // По 50 вершин в верхнем и нижнем кольцах.
    for (uint32_t ring = 0; ring < 2; ++ring) {
        const float y = ring == 0 ? -1.0f : 1.0f;
        for (uint32_t i = 0; i < SEGMENTS; ++i) {
            const float angle = 2.0f * PI * static_cast<float>(i) / SEGMENTS;
            const float x = std::cos(angle), z = std::sin(angle);
            vertices.push_back({{x, y, z},
                {0.35f + 0.45f * (x + 1.0f) * 0.5f,
                 0.25f + 0.55f * (y + 1.0f) * 0.5f,
                 0.35f + 0.55f * (z + 1.0f) * 0.5f}});
        }
    }
    const uint32_t bottomCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, -1.0f, 0.0f}, {0.35f, 0.45f, 1.0f}});
    const uint32_t topCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, 1.0f, 0.0f}, {1.0f, 0.45f, 0.25f}});

    for (uint32_t i = 0; i < SEGMENTS; ++i) {
        const uint32_t next = (i + 1) % SEGMENTS;
        const uint32_t top = SEGMENTS + i, topNext = SEGMENTS + next;
        // Два треугольника боковой поверхности и по одному треугольнику в каждом основании.
        indices.insert(indices.end(), {i, next, topNext, i, topNext, top});
        indices.insert(indices.end(), {bottomCenter, next, i});
        indices.insert(indices.end(), {topCenter, top, topNext});
    }
    indexCount = static_cast<uint32_t>(indices.size());

    if (!createBuffer(vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      vertexBuffer, vertexAllocation)) return false;
    if (!createBuffer(indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      indexBuffer, indexAllocation)) return false;
    void* data = nullptr;
    if (vmaMapMemory(graphics::internal::context.allocator, vertexAllocation, &data) != VK_SUCCESS) return false;
    std::memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
    vmaUnmapMemory(graphics::internal::context.allocator, vertexAllocation);
    if (vmaMapMemory(graphics::internal::context.allocator, indexAllocation, &data) != VK_SUCCESS) return false;
    std::memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
    vmaUnmapMemory(graphics::internal::context.allocator, indexAllocation);
    return true;
}

VkShaderModule loadShader(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { std::cerr << "Cannot open shader: " << path << '\n'; return VK_NULL_HANDLE; }
    const auto end = file.tellg();
    if (end <= 0) return VK_NULL_HANDLE;
    std::vector<uint32_t> code(static_cast<size_t>(end) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(code.size() * sizeof(uint32_t)));
    if (!file) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size() * sizeof(uint32_t); ci.pCode = code.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    if (vkCreateShaderModule(graphics::internal::context.device, &ci, nullptr, &shader) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return shader;
}

bool createPipeline() {
    auto& c = graphics::internal::context;
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0; binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1; binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl.bindingCount = 1; dsl.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(c.device, &dsl, nullptr, &descriptorSetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(c.device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS) return false;

    VkShaderModule vert = loadShader("shaders/cylinder.vert.spv");
    VkShaderModule frag = loadShader("shaders/cylinder.frag.spv");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        if (vert == VK_NULL_HANDLE) {
            std::cerr << "[ERROR] Vertex shader loading failed" << std::endl;
        }
        if (frag == VK_NULL_HANDLE) {
            std::cerr << "[ERROR] Fragment shader loading failed" << std::endl;
        }
        if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(c.device, vert, nullptr);
        if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(c.device, frag, nullptr);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    VkVertexInputBindingDescription vertexBinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vertexBinding;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dynamicStates;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &blendAttachment;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2; gp.pStages = stages; gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds; gp.pColorBlendState = &blend; gp.pDynamicState = &dy;
    gp.layout = pipelineLayout; gp.renderPass = c.render_pass; gp.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline);
    vkDestroyShaderModule(c.device, vert, nullptr); vkDestroyShaderModule(c.device, frag, nullptr);
    if (result != VK_SUCCESS) return false;

    if (!createBuffer(sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      uniformBuffer, uniformAllocation, &uniformMapped)) return false;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1; dpi.poolSizeCount = 1; dpi.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(c.device, &dpi, nullptr, &descriptorPool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptorPool; dai.descriptorSetCount = 1; dai.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(c.device, &dai, &descriptorSet) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo dbi{uniformBuffer, 0, sizeof(UniformData)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(c.device, 1, &write, 0, nullptr);
    return true;
}

void destroyResources() {
    auto& c = graphics::internal::context;
    if (c.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(c.device);
    if (descriptorPool) vkDestroyDescriptorPool(c.device, descriptorPool, nullptr);
    if (uniformAllocation) vmaDestroyBuffer(c.allocator, uniformBuffer, uniformAllocation);
    if (pipeline) vkDestroyPipeline(c.device, pipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(c.device, pipelineLayout, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(c.device, descriptorSetLayout, nullptr);
    if (vertexAllocation) vmaDestroyBuffer(c.allocator, vertexBuffer, vertexAllocation);
    if (indexAllocation) vmaDestroyBuffer(c.allocator, indexBuffer, indexAllocation);
    vertexBuffer = indexBuffer = uniformBuffer = VK_NULL_HANDLE;
    vertexAllocation = indexAllocation = uniformAllocation = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    pipeline = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE; uniformMapped = nullptr;
}
} // namespace

bool initialize() {
    std::ofstream log("application.log", std::ios::app);

    log << "[APP 1] Starting cylinder geometry creation" << std::endl;

    if (!createCylinderGeometry()) {
        log << "[ERROR] createCylinderGeometry() failed" << std::endl;
        destroyResources();
        return false;
    }

    log << "[APP 2] Cylinder geometry created" << std::endl;
    log << "[APP 3] Starting graphics pipeline creation" << std::endl;

    if (!createPipeline()) {
        log << "[ERROR] createPipeline() failed" << std::endl;
        destroyResources();
        return false;
    }

    log << "[APP 4] Graphics pipeline created" << std::endl;
    return true;
}

void shutdown() {
    destroyResources();
}


void update(double time) {
    if (previousFrameTime < 0.0) {
        previousFrameTime = time;
    }

    double deltaTime = time - previousFrameTime;
    previousFrameTime = time;

    if (deltaTime > 0.1) {
        deltaTime = 0.1;
    }

    if (animationPlaying) {
        animationTime += deltaTime * animationSpeed;

        objectPosition[0] =
            animationRadius * std::cos(static_cast<float>(animationTime));

        objectPosition[1] =
            animationRadius * 0.5f *
            std::sin(2.0f * static_cast<float>(animationTime));

        objectPosition[2] =
            animationRadius * 0.5f *
            std::sin(static_cast<float>(animationTime));
    }

    ImGui::Begin("Laboratory 1 - Cylinder");

    ImGui::Text("Variant 8: cylinder, 50 segments per base");

    const char* projectionNames[] = {
        "Perspective",
        "Orthographic"
    };

    ImGui::Combo(
        "Projection",
        &projectionMode,
        projectionNames,
        IM_ARRAYSIZE(projectionNames)
    );

    ImGui::Separator();
    ImGui::Text("Animation");

    if (ImGui::Button(animationPlaying ? "Pause" : "Play")) {
        animationPlaying = !animationPlaying;
    }

    ImGui::SliderFloat(
        "Animation speed",
        &animationSpeed,
        0.1f,
        3.0f
    );

    ImGui::SliderFloat(
        "Trajectory radius",
        &animationRadius,
        0.0f,
        2.0f
    );

    ImGui::Separator();
    ImGui::Text("Position");
    ImGui::DragFloat3("Position XYZ", objectPosition, 0.05f);

    ImGui::Separator();
    ImGui::Text("Rotation (degrees)");
    ImGui::DragFloat3("Rotation XYZ", objectRotation, 1.0f);

    ImGui::Separator();
    ImGui::Text("Scale");
    ImGui::DragFloat3(
        "Scale XYZ",
        objectScale,
        0.02f,
        0.05f,
        5.0f
    );

    ImGui::Separator();
    ImGui::ColorEdit3("Cylinder tint", tint);

    ImGui::Text("Hold left mouse button to rotate");

    ImGui::End();
}


void render(const graphics::internal::FrameData& fd) {
    if (fd.command_buffer == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE) return;
    auto& c = graphics::internal::context;
    const VkClearValue clearValues[2] = {
        {.color = {{0.035f, 0.045f, 0.075f, 1.0f}}},
        {.depthStencil = {1.0f, 0}}
    };
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = c.render_pass; rp.framebuffer = fd.framebuffer;
    rp.renderArea = {{0, 0}, c.swapchain_extent}; rp.clearValueCount = 2; rp.pClearValues = clearValues;
    vkCmdBeginRenderPass(fd.command_buffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(c.swapchain_extent.width),
                        static_cast<float>(c.swapchain_extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, c.swapchain_extent};
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    
const float width = static_cast<float>(c.swapchain_extent.width);
const float height = static_cast<float>(
    c.swapchain_extent.height ? c.swapchain_extent.height : 1
);
const float aspect = width / height;

Mat4 projection{};

if (projectionMode == 0) {
    projection = perspective(PI / 3.0f, aspect, 0.1f, 20.0f);
} else {
    const float orthoHeight = 4.0f;
    const float orthoWidth = orthoHeight * aspect;
    projection = orthographic(
        orthoWidth, orthoHeight, 0.1f, 20.0f
    );
}

const Mat4 move = translation(
    objectPosition[0],
    objectPosition[1],
    objectPosition[2]
);

const Mat4 scale = scaling(
    objectScale[0],
    objectScale[1],
    objectScale[2]
);

const Mat4 rotateX = rotationX(
    cameraRotationX + objectRotation[0] * PI / 180.0f
);
const Mat4 rotateY = rotationY(
    cameraRotationY + objectRotation[1] * PI / 180.0f
);
const Mat4 rotateZ = rotationZ(
    objectRotation[2] * PI / 180.0f
);

const Mat4 rotation = multiply(
    rotateZ,
    multiply(rotateY, rotateX)
);

const Mat4 model = multiply(
    move,
    multiply(rotation, scale)
);

Mat4 view = identity();
view.v[14] = -4.0f;

    UniformData uniform{};
    uniform.mvp = multiply(projection, multiply(view, model));
    std::memcpy(uniform.tint, tint, sizeof(tint));
    std::memcpy(uniformMapped, &uniform, sizeof(uniform));

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, indexCount, 1, 0, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
}
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        isDragging = (action == GLFW_PRESS);

        if (isDragging)
        {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
    }
}

void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos)
{
    if (!isDragging)
        return;

    double deltaX = xpos - lastMouseX;
    double deltaY = ypos - lastMouseY;

    lastMouseX = xpos;
    lastMouseY = ypos;

    cameraRotationY += static_cast<float>(deltaX) * mouseSensitivity;
    cameraRotationX += static_cast<float>(deltaY) * mouseSensitivity;

    const float limit = 1.5f;
    if (cameraRotationX > limit) cameraRotationX = limit;
    if (cameraRotationX < -limit) cameraRotationX = -limit;
}
} // namespace application
