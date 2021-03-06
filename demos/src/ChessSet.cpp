#include "GLFW/glfw3.h"
#if defined(__linux__)
  #define GLFW_EXPOSE_NATIVE_X11
#elif defined(_WIN32)
  #define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "GLFW/glfw3native.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#define TINY_RENDERER_IMPLEMENTATION
#if defined(TINY_RENDERER_DX)
    #include "tinydx.h"
#elif defined(TINY_RENDERER_VK)
    #include "tinyvk.h"
#endif
#include "camera.h"
#include "cbuffer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "mesh.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using float2   = glm::vec2;
using float3   = glm::vec3;
using float4   = glm::vec4;
using float3x3 = glm::mat3;
using float4x4 = glm::mat4;
using float3x4 = glm::mat3x4;
using float4x3 = glm::mat4x3;


const char*         k_app_name = "ChessSet";
const uint32_t      k_image_count = 1;
#if defined(__linux__)
const std::string   k_asset_dir = "../demos/assets/";
#elif defined(_WIN32)
const std::string   k_asset_dir = "../../demos/assets/";
#endif

tr_renderer*        m_renderer = nullptr;
tr_cmd_pool*        m_cmd_pool = nullptr;
tr_cmd**            m_cmds = nullptr;

tr_pipeline*        m_chess_board_1_pipeline = nullptr;
tr_pipeline*        m_chess_board_2_pipeline = nullptr;
tr_pipeline*        m_chess_pieces_1_pipeline = nullptr;
tr_pipeline*        m_chess_pieces_2_pipeline = nullptr;
tr_pipeline*        m_normal_wireframe_pipeline = nullptr;

tr_shader_program*  m_phong_shader = nullptr;
tr_shader_program*  m_normal_wireframe_shader = nullptr;

tr_buffer*          m_chess_board_1_uniform_buffer = nullptr;
tr_buffer*          m_chess_board_2_uniform_buffer = nullptr;
tr_buffer*          m_chess_pieces_1_uniform_buffer = nullptr;
tr_buffer*          m_chess_pieces_2_uniform_buffer = nullptr;
tr_buffer*          m_normal_wireframe_uniform_buffer = nullptr;

tr_descriptor_set*  m_chess_board_1_desc_set = nullptr;
tr_descriptor_set*  m_chess_board_2_desc_set = nullptr;
tr_descriptor_set*  m_chess_pieces_1_desc_set = nullptr;
tr_descriptor_set*  m_chess_pieces_2_desc_set = nullptr;
tr_descriptor_set*  m_normal_wireframe_desc_set = nullptr;

uint32_t            m_chess_board_1_vertex_count = 0;
tr_buffer*          m_chess_board_1_vertex_buffer = nullptr;
uint32_t            m_chess_board_2_vertex_count = 0;
tr_buffer*          m_chess_board_2_vertex_buffer = nullptr;
uint32_t            m_chess_pieces_1_vertex_count = 0;
tr_buffer*          m_chess_pieces_1_vertex_buffer = nullptr;
uint32_t            m_chess_pieces_2_vertex_count = 0;
tr_buffer*          m_chess_pieces_2_vertex_buffer = nullptr;

uint32_t            s_window_width;
uint32_t            s_window_height;
uint64_t            s_frame_count = 0;

tr::Camera              m_camera;
tr::Transform           m_transform;
tr::ViewTransformBuffer m_cbuffer;

#define LOG(STR)  { std::stringstream ss; ss << STR << std::endl; \
                    platform_log(ss.str().c_str()); }

static void platform_log(const char* s)
{
#if defined(_WIN32)
  OutputDebugStringA(s);
#else
  printf("%s", s);
#endif
}

static void app_glfw_error(int error, const char* description)
{
  LOG("Error " << error << ":" << description);
}

void renderer_log(tr_log_type type, const char* msg, const char* component)
{
  switch(type) {
    case tr_log_type_info  : {LOG("[INFO]" << "[" << component << "] : " << msg);} break;
    case tr_log_type_warn  : {LOG("[WARN]"  << "[" << component << "] : " << msg);} break;
    case tr_log_type_debug : {LOG("[DEBUG]" << "[" << component << "] : " << msg);} break;
    case tr_log_type_error : {LOG("[ERORR]" << "[" << component << "] : " << msg);} break;
    default: break;
  }
}

#if defined(TINY_RENDERER_VK)
VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT objectType,
    uint64_t                   object,
    size_t                     location,
    int32_t                    messageCode,
    const char*                pLayerPrefix,
    const char*                pMessage,
    void*                      pUserData
)
{
    if( flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ) {
        //LOG("[INFO]" << "[" << pLayerPrefix << "] : " << pMessage << " (" << messageCode << ")");
    }
    else if( flags & VK_DEBUG_REPORT_WARNING_BIT_EXT ) {
        LOG("[WARN]" << "[" << pLayerPrefix << "] : " << pMessage << " (" << messageCode << ")");
    }
    else if( flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT ) {
        //LOG("[PERF]" << "[" << pLayerPrefix << "] : " << pMessage << " (" << messageCode << ")");
    }
    else if( flags & VK_DEBUG_REPORT_ERROR_BIT_EXT ) {
        LOG("[ERROR]" << "[" << pLayerPrefix << "] : " << pMessage << " (" << messageCode << ")"); 
    }
    else if( flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT ) {
        LOG("[DEBUG]" << "[" << pLayerPrefix << "] : " << pMessage << " (" << messageCode << ")");
    }
    return VK_FALSE;
}
#endif

std::vector<uint8_t> load_file(const std::string& path)
{
    std::ifstream is;
    is.open(path.c_str(), std::ios::in | std::ios::binary);
    assert(is.is_open());

    is.seekg(0, std::ios::end);
    std::vector<uint8_t> buffer(is.tellg());
    assert(0 != buffer.size());

    is.seekg(0, std::ios::beg);
    is.read((char*)buffer.data(), buffer.size());

    return buffer;
}

void init_tiny_renderer(GLFWwindow* window)
{
    std::vector<const char*> instance_layers = {
#if defined(_DEBUG)
      // "VK_LAYER_LUNARG_standard_validation",
#endif
    };

    std::vector<const char*> device_layers;

    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    s_window_width = (uint32_t)width;
    s_window_height = (uint32_t)height;

    tr_renderer_settings settings = {0};
#if defined(__linux__)
    settings.handle.connection              = XGetXCBConnection(glfwGetX11Display());
    settings.handle.window                  = glfwGetX11Window(window);
#elif defined(_WIN32)
    settings.handle.hinstance               = ::GetModuleHandle(NULL);
    settings.handle.hwnd                    = glfwGetWin32Window(window);
#endif
    settings.width                          = s_window_width;
    settings.height                         = s_window_height;
    settings.swapchain.image_count          = k_image_count;
    settings.swapchain.sample_count         = tr_sample_count_8;
    settings.swapchain.color_format         = tr_format_b8g8r8a8_unorm;
    settings.swapchain.depth_stencil_format = tr_format_d32_float;
    settings.swapchain.depth_stencil_clear_value.depth    = 1.0f;
    settings.swapchain.depth_stencil_clear_value.stencil  = 255;
    settings.log_fn                         = renderer_log;
#if defined(TINY_RENDERER_VK)
    settings.vk_debug_fn                    = vulkan_debug;
    settings.instance_layers.count          = (uint32_t)instance_layers.size();
    settings.instance_layers.names          = instance_layers.empty() ? nullptr : instance_layers.data();
#endif
    tr_create_renderer(k_app_name, &settings, &m_renderer);

    tr_create_cmd_pool(m_renderer, m_renderer->graphics_queue, false, &m_cmd_pool);
    tr_create_cmd_n(m_cmd_pool, false, k_image_count, &m_cmds);
    
#if defined(TINY_RENDERER_VK)
    auto vert = load_file(k_asset_dir + "ChessSet/shaders/phong.vs.spv");
    auto frag = load_file(k_asset_dir + "ChessSet/shaders/phong.ps.spv");
    tr_create_shader_program(m_renderer, 
                             (uint32_t)vert.size(), (uint32_t*)(vert.data()), "VSMain",
                             (uint32_t)frag.size(), (uint32_t*)(frag.data()), "PSMain",
                             &m_phong_shader);

         vert = load_file(k_asset_dir + "ChessSet/shaders/normal_wireframe.vs.spv");
    auto geom = load_file(k_asset_dir + "ChessSet/shaders/normal_wireframe.gs.spv");
         frag = load_file(k_asset_dir + "ChessSet/shaders/normal_wireframe.ps.spv");
    tr_create_shader_program_n(m_renderer, 
                               (uint32_t)vert.size(), (uint32_t*)(vert.data()), "VSMain",
                               0, nullptr, nullptr,
                               0, nullptr, nullptr,
                               (uint32_t)geom.size(), (uint32_t*)(geom.data()), "GSMain",
                               (uint32_t)frag.size(), (uint32_t*)(frag.data()), "PSMain",
                               0, nullptr, nullptr,
                               &m_normal_wireframe_shader);
#elif defined(TINY_RENDERER_DX)
    auto hlsl = load_file(k_asset_dir + "ChessSet/shaders/phong.hlsl");
    tr_create_shader_program(m_renderer,
                             (uint32_t)hlsl.size(), hlsl.data(), "VSMain", 
                             (uint32_t)hlsl.size(), hlsl.data(), "PSMain", 
                             &m_phong_shader);    
    hlsl = load_file(k_asset_dir + "ChessSet/shaders/normal_wireframe.hlsl");
    tr_create_shader_program_n(m_renderer,
                               (uint32_t)hlsl.size(), hlsl.data(), "VSMain", 
                               0, nullptr, nullptr,
                               0, nullptr, nullptr,
                               (uint32_t)hlsl.size(), hlsl.data(), "GSMain",
                               (uint32_t)hlsl.size(), hlsl.data(), "PSMain",
                               0, nullptr, nullptr, 
                               &m_normal_wireframe_shader);    
#endif

    // Descriptors
    {
      std::vector<tr_descriptor> descriptors(1);
      descriptors[0].type          = tr_descriptor_type_uniform_buffer_cbv;
      descriptors[0].count         = 1;
      descriptors[0].binding       = 0;
      descriptors[0].shader_stages = (tr_shader_stage)(tr_shader_stage_vert | tr_shader_stage_geom | tr_shader_stage_frag);

      tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_chess_board_1_desc_set);    
      tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_chess_board_2_desc_set);
      tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_chess_pieces_1_desc_set);
      tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_chess_pieces_2_desc_set);

      tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_normal_wireframe_desc_set);
    }

    // Vertex layout
    tr_vertex_layout vertex_layout = {};
    vertex_layout.attrib_count = 3;
    // Position
    vertex_layout.attribs[0].semantic = tr_semantic_position;
    vertex_layout.attribs[0].format   = tr_format_r32g32b32_float;
    vertex_layout.attribs[0].binding  = 0;
    vertex_layout.attribs[0].location = 0;
    vertex_layout.attribs[0].offset   = 0;
    // Normal
    vertex_layout.attribs[1].semantic = tr_semantic_normal;
    vertex_layout.attribs[1].format   = tr_format_r32g32b32_float;
    vertex_layout.attribs[1].binding  = 0;
    vertex_layout.attribs[1].location = 1;   
    vertex_layout.attribs[1].offset   = tr_util_format_stride(tr_format_r32g32b32_float);
    // Tex Coord
    vertex_layout.attribs[2].semantic = tr_semantic_texcoord0;
    vertex_layout.attribs[2].format   = tr_format_r32g32_float;
    vertex_layout.attribs[2].binding  = 0;
    vertex_layout.attribs[2].location = 2;
    vertex_layout.attribs[2].offset   = tr_util_format_stride(tr_format_r32g32b32_float) + tr_util_format_stride(tr_format_r32g32b32_float);
    // Phong pipeline
    {
      tr_pipeline_settings pipeline_settings = {tr_primitive_topo_tri_list};
      pipeline_settings.depth = true;
      pipeline_settings.cull_mode = tr_cull_mode_back;   
      tr_create_pipeline(m_renderer, m_phong_shader, &vertex_layout, m_chess_board_1_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_chess_board_1_pipeline);
      tr_create_pipeline(m_renderer, m_phong_shader, &vertex_layout, m_chess_board_2_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_chess_board_2_pipeline);
      tr_create_pipeline(m_renderer, m_phong_shader, &vertex_layout, m_chess_pieces_1_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_chess_pieces_1_pipeline);
      tr_create_pipeline(m_renderer, m_phong_shader, &vertex_layout, m_chess_pieces_2_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_chess_pieces_2_pipeline);
    }
    // Normal wireframe pipeline
    {
      tr_pipeline_settings pipeline_settings = {tr_primitive_topo_tri_list};
      pipeline_settings.depth = true;
      tr_create_pipeline(m_renderer, m_normal_wireframe_shader, &vertex_layout, m_normal_wireframe_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_normal_wireframe_pipeline);
    }

    // Vertex data
    {
      // Chess board 1
      tr::Mesh mesh;
      bool mesh_load_res = tr::Mesh::Load(k_asset_dir + "ChessSet/models/board1.obj", &mesh);
      assert(mesh_load_res);
      tr_create_vertex_buffer(m_renderer, mesh.GetVertexDataSize(), true, mesh.GetVertexStride(), &m_chess_board_1_vertex_buffer);
      memcpy(m_chess_board_1_vertex_buffer->cpu_mapped_address, mesh.GetVertexData(), mesh.GetVertexDataSize());
      m_chess_board_1_vertex_count = mesh.GetVertexCount();
      
      // Chess board 2
      mesh_load_res = tr::Mesh::Load(k_asset_dir + "ChessSet/models/board2.obj", &mesh);
      assert(mesh_load_res);
      tr_create_vertex_buffer(m_renderer, mesh.GetVertexDataSize(), true, mesh.GetVertexStride(), &m_chess_board_2_vertex_buffer);
      memcpy(m_chess_board_2_vertex_buffer->cpu_mapped_address, mesh.GetVertexData(), mesh.GetVertexDataSize());
      m_chess_board_2_vertex_count = mesh.GetVertexCount();
      
      // Chest pieces 1
      mesh_load_res = tr::Mesh::Load(k_asset_dir + "ChessSet/models/pieces1.obj", &mesh);
      assert(mesh_load_res);
      tr_create_vertex_buffer(m_renderer, mesh.GetVertexDataSize(), true, mesh.GetVertexStride(), &m_chess_pieces_1_vertex_buffer);
      memcpy(m_chess_pieces_1_vertex_buffer->cpu_mapped_address, mesh.GetVertexData(), mesh.GetVertexDataSize());
      m_chess_pieces_1_vertex_count = mesh.GetVertexCount();
     
      // Chest pieces 2
      mesh_load_res = tr::Mesh::Load(k_asset_dir + "ChessSet/models/pieces2.obj", &mesh);
      assert(mesh_load_res);
      tr_create_vertex_buffer(m_renderer, mesh.GetVertexDataSize(), true, mesh.GetVertexStride(), &m_chess_pieces_2_vertex_buffer);
      memcpy(m_chess_pieces_2_vertex_buffer->cpu_mapped_address, mesh.GetVertexData(), mesh.GetVertexDataSize());
      m_chess_pieces_2_vertex_count = mesh.GetVertexCount();
    }

    // Uniform buffers
    {
      uint32_t uniform_buffer_size = m_cbuffer.GetDataSize();
     
      // Chess board 1
      tr_create_uniform_buffer(m_renderer, uniform_buffer_size, true, &m_chess_board_1_uniform_buffer);
      m_chess_board_1_desc_set->descriptors[0].uniform_buffers[0] = m_chess_board_1_uniform_buffer;
      tr_update_descriptor_set(m_renderer, m_chess_board_1_desc_set);

      // Chess board 2
      tr_create_uniform_buffer(m_renderer, uniform_buffer_size, true, &m_chess_board_2_uniform_buffer);
      m_chess_board_2_desc_set->descriptors[0].uniform_buffers[0] = m_chess_board_2_uniform_buffer;
      tr_update_descriptor_set(m_renderer, m_chess_board_2_desc_set);

      // Chess pieces 1
      tr_create_uniform_buffer(m_renderer, uniform_buffer_size, true, &m_chess_pieces_1_uniform_buffer);
      m_chess_pieces_1_desc_set->descriptors[0].uniform_buffers[0] = m_chess_pieces_1_uniform_buffer;
      tr_update_descriptor_set(m_renderer, m_chess_pieces_1_desc_set);

      // Chess board 2
      tr_create_uniform_buffer(m_renderer, uniform_buffer_size, true, &m_chess_pieces_2_uniform_buffer);
      m_chess_pieces_2_desc_set->descriptors[0].uniform_buffers[0] = m_chess_pieces_2_uniform_buffer;
      tr_update_descriptor_set(m_renderer, m_chess_pieces_2_desc_set);

      // Normal wireframe
      tr_create_uniform_buffer(m_renderer, uniform_buffer_size, true, &m_normal_wireframe_uniform_buffer);
      m_normal_wireframe_desc_set->descriptors[0].uniform_buffers[0] = m_normal_wireframe_uniform_buffer;
      tr_update_descriptor_set(m_renderer, m_normal_wireframe_desc_set);
    }
}

void destroy_tiny_renderer()
{
    tr_destroy_renderer(m_renderer);
}

void draw_frame()
{
    uint32_t frameIdx = s_frame_count % m_renderer->settings.swapchain.image_count;

    tr_fence* image_acquired_fence = m_renderer->image_acquired_fences[frameIdx];
    tr_semaphore* image_acquired_semaphore = m_renderer->image_acquired_semaphores[frameIdx];
    tr_semaphore* render_complete_semaphores = m_renderer->render_complete_semaphores[frameIdx];

    tr_acquire_next_image(m_renderer, image_acquired_semaphore, image_acquired_fence);

    uint32_t swapchain_image_index = m_renderer->swapchain_image_index;
    tr_render_target* render_target = m_renderer->swapchain_render_targets[swapchain_image_index];

    float3 eye = float3(0, 7, 12);
    float3 look_at = float3(0, 0, 0);
    m_camera.LookAt(eye, look_at);
    m_camera.Perspective(65.0f, (float)s_window_width / (float)s_window_height);

    // Model
    float t = (float)glfwGetTime();
    m_transform.Rotate(0, t / 2.0f, 0);
    
    // Constant buffer for phong shader
    {
      m_cbuffer.SetTransform(m_transform);
      m_cbuffer.SetCamera(m_camera);
      // Color
      {
        // Board 1
        m_cbuffer.SetColor(float3(0.23f));
        m_cbuffer.Write(m_chess_board_1_uniform_buffer->cpu_mapped_address);
     
        // Board 2
        m_cbuffer.SetColor(float3(0.88f));
        m_cbuffer.Write(m_chess_board_2_uniform_buffer->cpu_mapped_address);

        // Pieces 1
        m_cbuffer.SetColor(0.85f, 0.3f, 0.3f);
        m_cbuffer.Write(m_chess_pieces_1_uniform_buffer->cpu_mapped_address);
     
        // Pieces 2
        m_cbuffer.SetColor(0.4f, 0.4f, 0.8f);
        m_cbuffer.Write(m_chess_pieces_2_uniform_buffer->cpu_mapped_address);
      }
    }

    // Constant buffer for normal wireframe shader
    {
      m_cbuffer.SetTransform(m_transform);
      m_cbuffer.SetCamera(m_camera);      
      m_cbuffer.Write(m_normal_wireframe_uniform_buffer->cpu_mapped_address);
    }

    tr_cmd* cmd = m_cmds[frameIdx];
    tr_begin_cmd(cmd);
    tr_cmd_render_target_transition(cmd, render_target, tr_texture_usage_present, tr_texture_usage_color_attachment); 
    tr_cmd_depth_stencil_transition(cmd, render_target, tr_texture_usage_sampled_image, tr_texture_usage_depth_stencil_attachment);
    tr_cmd_set_viewport(cmd, 0, 0, (float)s_window_width, (float)s_window_height, 0.0f, 1.0f);
    tr_cmd_set_scissor(cmd, 0, 0, s_window_width, s_window_height);
    tr_cmd_begin_render(cmd, render_target);
    tr_clear_value color_clear_value = {0.1f, 0.1f, 0.1f, 0.1f};
    tr_cmd_clear_color_attachment(cmd, 0, &color_clear_value);
    tr_clear_value depth_stencil_clear_value = { 0 };
    depth_stencil_clear_value.depth = 1.0f;
    depth_stencil_clear_value.stencil = 255;
    tr_cmd_clear_depth_stencil_attachment(cmd, &depth_stencil_clear_value);
    // Draw phong
    {
      // Draw board 1
      tr_cmd_bind_pipeline(cmd, m_chess_board_1_pipeline);
      tr_cmd_bind_descriptor_sets(cmd, m_chess_board_1_pipeline, m_chess_board_1_desc_set);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_chess_board_1_vertex_buffer);
      tr_cmd_draw(cmd, m_chess_board_1_vertex_count, 0);
      // Draw board 2
      tr_cmd_bind_pipeline(cmd, m_chess_board_2_pipeline);
      tr_cmd_bind_descriptor_sets(cmd, m_chess_board_2_pipeline, m_chess_board_2_desc_set);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_chess_board_2_vertex_buffer);
      tr_cmd_draw(cmd, m_chess_board_2_vertex_count, 0);
      // Draw pieces 1
      tr_cmd_bind_pipeline(cmd, m_chess_pieces_1_pipeline);
      tr_cmd_bind_descriptor_sets(cmd, m_chess_pieces_1_pipeline, m_chess_pieces_1_desc_set);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_chess_pieces_1_vertex_buffer);
      tr_cmd_draw(cmd, m_chess_pieces_1_vertex_count, 0);
      // Draw pieces 2
      tr_cmd_bind_pipeline(cmd, m_chess_pieces_2_pipeline);
      tr_cmd_bind_descriptor_sets(cmd, m_chess_pieces_2_pipeline, m_chess_pieces_2_desc_set);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_chess_pieces_2_vertex_buffer);
      tr_cmd_draw(cmd, m_chess_pieces_2_vertex_count, 0);
    }
    // Draw normal wireframe 
    {
      // Draw pieces 2
      tr_cmd_bind_pipeline(cmd, m_normal_wireframe_pipeline);
      tr_cmd_bind_descriptor_sets(cmd, m_normal_wireframe_pipeline, m_normal_wireframe_desc_set);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_chess_pieces_1_vertex_buffer);
      tr_cmd_draw(cmd, m_chess_pieces_1_vertex_count, 0);

      // Draw pieces 2
      tr_cmd_bind_pipeline(cmd, m_normal_wireframe_pipeline);
      tr_cmd_bind_descriptor_sets(cmd, m_normal_wireframe_pipeline, m_normal_wireframe_desc_set);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_chess_pieces_2_vertex_buffer);
      tr_cmd_draw(cmd, m_chess_pieces_2_vertex_count, 0);
    }
    tr_cmd_end_render(cmd);
    tr_cmd_render_target_transition(cmd, render_target, tr_texture_usage_color_attachment, tr_texture_usage_present); 
    tr_cmd_depth_stencil_transition(cmd, render_target, tr_texture_usage_depth_stencil_attachment, tr_texture_usage_sampled_image);
    tr_end_cmd(cmd);

    tr_queue_submit(m_renderer->graphics_queue, 1, &cmd, 1, &image_acquired_semaphore, 1, &render_complete_semaphores);
    tr_queue_present(m_renderer->present_queue, 1, &render_complete_semaphores);

    tr_queue_wait_idle(m_renderer->graphics_queue);
}

int main(int argc, char **argv)
{
    glfwSetErrorCallback(app_glfw_error);
    if (! glfwInit()) {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1920, 1080, k_app_name, NULL, NULL);
    init_tiny_renderer(window);

    while (! glfwWindowShouldClose(window)) {
        draw_frame();
        glfwPollEvents();
    }
    
    destroy_tiny_renderer();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
