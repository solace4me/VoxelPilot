/* ============================================================
   standalone_main.cu

   Standalone mode: GUI + CUDA renderer on one machine.
   No networking required. No TCP. No SSH tunnel.

   Usage:
     ./volume_renderer_standalone
     ./volume_renderer_standalone --volume brain.raw --dim 256
     ./volume_renderer_standalone --volume foot_256x256x256_uint8.raw
     ./volume_renderer_standalone --volume head.raw --width 512 --height 512 --depth 256 --type uint16
   ============================================================ */

#include <cuda_runtime.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glfw/deps/stb_image_write.h"

#include "tinyfiledialogs.h"

#include "common/volume_structs.h"
#include "common/math_utils.h"
#include "common/renderer_api.h"
#include "common/ui_state_helpers.h"
#include "common/volume_import.h"

/* ============================================================
   Constants
   ============================================================ */
#define DEFAULT_WIN_W    1600
#define DEFAULT_WIN_H    900
#define DEFAULT_RENDER_W 512
#define DEFAULT_RENDER_H 512
#define MAX_PATH_BUF     1024
#define BRAND_TITLE      "VoxelPilot"
#define BRAND_SUBTITLE   "NVIDIA Volume Explorer"
#define HISTOGRAM_BINS   128

typedef struct {
    const char *label;
    int         width;
    int         height;
} ResolutionPreset;

/* ============================================================
   Standalone App State
   ============================================================ */
typedef struct {
    /* Window */
    GLFWwindow   *window;
    int           win_w, win_h;

    /* GL */
    GLuint        prog;
    GLuint        vao, vbo, ebo;
    GLuint        gl_tex;
    int           tex_w, tex_h;

    /* Renderer */
    RendererState renderer;

    /* UI: Camera */
    float         cam_pos[3];
    float         cam_dir[3];
    float         cam_up[3];
    float         fov_y_deg;

    /* UI: Light */
    float         light_pos[3];

    /* UI: Render params */
    float         step_size;
    float         threshold;
    float         skip_mult;
    int           render_w;
    int           render_h;

    /* UI: Control */
    int           paused;

    /* Stats */
    float         last_render_ms;
    float         gui_fps;
    uint32_t      frame_id;

    /* Volume upload */
    char          upload_path[1024];
    int           upload_width;
    int           upload_height;
    int           upload_depth;
    int           upload_data_type;
    size_t        upload_file_size_bytes;
    int           upload_file_size_known;
    char          upload_hint_status[256];
    char          upload_status[256];
    char          screenshot_status[256];
    char          screenshot_path[MAX_PATH_BUF];
    int           selected_resolution;
    float         tf_center;
    float         tf_width;
    float         tf_opacity_scale;
    int           tf_palette;
    int           tf_invert;
    float         slice_x;
    float         slice_y;
    float         slice_z;
    GLuint        axial_tex;
    GLuint        coronal_tex;
    GLuint        sagittal_tex;
    float         clip_min[3];
    float         clip_max[3];
    float         histogram[HISTOGRAM_BINS];
    float         hist_min_value;
    float         hist_max_value;
    float         hist_peak_count;
    int           histogram_ready;
    float         measure_a[3];
    float         measure_b[3];
    int           measurement_visible;
    int           measurement_target;
    float         voxel_spacing[3];
    char          preset_status[256];
    char          preset_path[MAX_PATH_BUF];
    float         orbit_yaw;
    float         orbit_pitch;
    float         orbit_radius;
    float         orbit_target[3];
    double        last_mouse_x;
    double        last_mouse_y;
    float         pending_scroll;
    int           orbit_drag_active;
    int           dock_layout_ready;
    float         dock_layout_viewport_w;
    float         dock_layout_viewport_h;
    int           demo_mode_enabled;
    int           walkthrough_visible;
    int           walkthrough_step;
    int           splash_visible;
    double        splash_start_time;
    int           splash_status_index;

} StandaloneApp;

static const ResolutionPreset k_resolution_presets[] = {
    { "256 x 256", 256, 256 },
    { "512 x 512", 512, 512 },
    { "768 x 768", 768, 768 },
    { "1024 x 1024", 1024, 1024 },
    { "1280 x 720", 1280, 720 },
    { "1920 x 1080", 1920, 1080 }
};

static const int k_resolution_preset_count =
    (int)(sizeof(k_resolution_presets) / sizeof(k_resolution_presets[0]));

static const char *k_tf_palette_labels[] = {
    "Grayscale",
    "Bone",
    "Heat",
    "Cool"
};

static const char *k_volume_data_type_labels[] = {
    "uint8",
    "uint16",
    "float32"
};

static const char *k_walkthrough_titles[] = {
    "1. Launch And Readiness",
    "2. Volume Import",
    "3. Camera Framing",
    "4. Slice Review",
    "5. Measurement Pass",
    "6. Snapshot And Save"
};

static const char *k_walkthrough_bodies[] = {
    "Confirm the demo is running on the NVIDIA laptop, the main panels are visible, and the renderer is updating smoothly.",
    "Use Browse, confirm width, height, depth, and data type, then click Load Volume. Watch the dataset status text and metadata update.",
    "Use Front or Isometric, then orbit in the viewport and zoom with the mouse wheel to frame the anatomy clearly.",
    "Move Sagittal X, Coronal Y, and Axial Z in Insights to explain how the orthogonal slices complement the 3D view.",
    "Set Point A and Point B from the current slice position to show a quick distance readout in voxels and world units.",
    "Export a PNG with Save Snapshot and optionally save the full workspace session for a repeatable presentation setup."
};

static const int k_walkthrough_step_count =
    (int)(sizeof(k_walkthrough_titles) / sizeof(k_walkthrough_titles[0]));

static const char *k_splash_statuses[] = {
    "Preparing VoxelPilot",
    "Loading the exploration workspace",
    "Activating presentation mode",
    "Ready for interactive review"
};

static const int k_splash_status_count =
    (int)(sizeof(k_splash_statuses) / sizeof(k_splash_statuses[0]));

static void apply_demo_theme(void)
{
    ImGuiStyle *style = &ImGui::GetStyle();
    ImVec4 *colors = style->Colors;

    ImGui::StyleColorsDark();

    style->WindowRounding = 10.0f;
    style->ChildRounding = 10.0f;
    style->FrameRounding = 7.0f;
    style->PopupRounding = 8.0f;
    style->GrabRounding = 7.0f;
    style->ScrollbarRounding = 8.0f;
    style->TabRounding = 8.0f;
    style->WindowBorderSize = 1.0f;
    style->FrameBorderSize = 0.0f;
    style->WindowPadding = ImVec2(12.0f, 10.0f);
    style->FramePadding = ImVec2(10.0f, 6.0f);
    style->ItemSpacing = ImVec2(9.0f, 8.0f);
    style->ItemInnerSpacing = ImVec2(8.0f, 6.0f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.090f, 0.110f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.090f, 0.105f, 0.130f, 0.94f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.085f, 0.100f, 0.120f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.180f, 0.250f, 0.290f, 0.85f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.110f, 0.140f, 0.170f, 0.95f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.150f, 0.220f, 0.250f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.180f, 0.300f, 0.320f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.060f, 0.080f, 0.100f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.080f, 0.120f, 0.145f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.080f, 0.100f, 0.120f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.050f, 0.060f, 0.080f, 0.55f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.200f, 0.280f, 0.320f, 0.80f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.83f, 0.35f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.33f, 0.78f, 0.72f, 0.95f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.96f, 0.84f, 0.38f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.130f, 0.230f, 0.250f, 0.95f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.180f, 0.320f, 0.350f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.240f, 0.420f, 0.420f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.140f, 0.240f, 0.260f, 0.90f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.200f, 0.340f, 0.360f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.240f, 0.420f, 0.430f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.210f, 0.290f, 0.330f, 0.90f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.330f, 0.780f, 0.720f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.330f, 0.780f, 0.720f, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.100f, 0.155f, 0.185f, 0.92f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.180f, 0.320f, 0.350f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.130f, 0.250f, 0.270f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.95f, 0.97f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.66f, 0.72f, 1.0f);
}

static void sync_orbit_from_camera(StandaloneApp *app);
static void sync_resolution_selection(StandaloneApp *app);
static void refresh_upload_file_details(StandaloneApp *app);

static int detect_cuda_runtime(char *message, size_t message_size)
{
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    const char *err_name;
    const char *err_text;

    if (err != cudaSuccess) {
        err_name = cudaGetErrorName(err);
        err_text = cudaGetErrorString(err);
        if (!err_name) err_name = "unknown";
        if (!err_text) err_text = "no error string available";
        snprintf(message, message_size,
                 "CUDA runtime unavailable: [%d] %s - %s",
                 (int)err, err_name, err_text);
        cudaGetLastError();
        return 0;
    }

    if (device_count < 1) {
        snprintf(message, message_size,
                 "No CUDA-capable NVIDIA device was detected.");
        return 0;
    }

    snprintf(message, message_size,
             "CUDA ready: %d device(s) detected.",
             device_count);
    return 1;
}

/* ============================================================
   Fullscreen blit shaders for the standalone viewport
   ============================================================ */
static const char *vs_source =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *fs_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    float v = texture(tex, uv).r;\n"
    "    outColor = vec4(v, v, v, 1.0);\n"
    "}\n";

/* ============================================================
   Setup GL resources for the standalone viewport
   ============================================================ */
static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        fprintf(stderr, "Shader error: %s\n", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static int setup_gl_resources(StandaloneApp *app)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    if (!vs || !fs) return -1;

    app->prog = glCreateProgram();
    glAttachShader(app->prog, vs);
    glAttachShader(app->prog, fs);
    glLinkProgram(app->prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    float quadVerts[] = {
        -1,-1, 0,0,
         1,-1, 1,0,
         1, 1, 1,1,
        -1, 1, 0,1
    };
    unsigned int quadIdx[] = {0,1,2, 2,3,0};

    glGenVertexArrays(1, &app->vao);
    glGenBuffers(1, &app->vbo);
    glGenBuffers(1, &app->ebo);
    glBindVertexArray(app->vao);
    glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4*sizeof(float),
                          (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glGenTextures(1, &app->gl_tex);
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 app->render_w, app->render_h,
                 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &app->axial_tex);
    glGenTextures(1, &app->coronal_tex);
    glGenTextures(1, &app->sagittal_tex);

    app->tex_w = app->render_w;
    app->tex_h = app->render_h;
    return 0;
}

static void upload_gray_texture(
    GLuint               tex,
    const unsigned char *pixels,
    int                  width,
    int                  height)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_R8,
        width, height,
        0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void update_histogram(StandaloneApp *app)
{
    size_t voxel_count;
    size_t i;
    float min_v;
    float max_v;
    float range;
    float peak_count;

    memset(app->histogram, 0, sizeof(app->histogram));
    app->histogram_ready = 0;
    app->hist_peak_count = 0.0f;

    if (!app->renderer.volume_data ||
        app->renderer.W < 1 ||
        app->renderer.H < 1 ||
        app->renderer.D < 1) {
        return;
    }

    voxel_count = (size_t)app->renderer.W *
                  (size_t)app->renderer.H *
                  (size_t)app->renderer.D;
    if (voxel_count == 0) {
        return;
    }

    min_v = app->renderer.volume_data[0];
    max_v = app->renderer.volume_data[0];

    for (i = 1; i < voxel_count; ++i) {
        float v = app->renderer.volume_data[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }

    range = max_v - min_v;
    if (range < 1e-6f) {
        app->histogram[0] = (float)voxel_count;
    } else {
        for (i = 0; i < voxel_count; ++i) {
            float normalized = (app->renderer.volume_data[i] - min_v) / range;
            int bin = (int)(normalized * (float)(HISTOGRAM_BINS - 1));
            if (bin < 0) bin = 0;
            if (bin >= HISTOGRAM_BINS) bin = HISTOGRAM_BINS - 1;
            app->histogram[bin] += 1.0f;
        }
    }

    peak_count = 0.0f;
    for (i = 0; i < HISTOGRAM_BINS; ++i) {
        if (app->histogram[i] > peak_count) {
            peak_count = app->histogram[i];
        }
    }

    app->hist_min_value = min_v;
    app->hist_max_value = max_v;
    app->hist_peak_count = peak_count;
    app->histogram_ready = 1;
}

static float measurement_distance(const StandaloneApp *app)
{
    float dx = (app->measure_b[0] - app->measure_a[0]) * (float)app->renderer.W;
    float dy = (app->measure_b[1] - app->measure_a[1]) * (float)app->renderer.H;
    float dz = (app->measure_b[2] - app->measure_a[2]) * (float)app->renderer.D;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float measurement_distance_world(const StandaloneApp *app)
{
    float dx = (app->measure_b[0] - app->measure_a[0]) * (float)app->renderer.W * app->voxel_spacing[0];
    float dy = (app->measure_b[1] - app->measure_a[1]) * (float)app->renderer.H * app->voxel_spacing[1];
    float dz = (app->measure_b[2] - app->measure_a[2]) * (float)app->renderer.D * app->voxel_spacing[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static int clipping_is_active(const StandaloneApp *app)
{
    return voxelpilot_clip_is_active(app->clip_min, app->clip_max);
}

static void set_camera_preset(
    StandaloneApp *app,
    float pos_x, float pos_y, float pos_z,
    float dir_x, float dir_y, float dir_z,
    float up_x,  float up_y,  float up_z)
{
    app->cam_pos[0] = pos_x;
    app->cam_pos[1] = pos_y;
    app->cam_pos[2] = pos_z;

    app->cam_dir[0] = dir_x;
    app->cam_dir[1] = dir_y;
    app->cam_dir[2] = dir_z;

    app->cam_up[0] = up_x;
    app->cam_up[1] = up_y;
    app->cam_up[2] = up_z;
    sync_orbit_from_camera(app);
}

static void apply_orbit_camera(StandaloneApp *app)
{
    float cos_pitch = cosf(app->orbit_pitch);
    float sin_pitch = sinf(app->orbit_pitch);
    float cos_yaw = cosf(app->orbit_yaw);
    float sin_yaw = sinf(app->orbit_yaw);
    float3 eye;
    float3 target;
    float3 up;
    float3 dir;
    float3 right;

    target = f3_make(
        app->orbit_target[0],
        app->orbit_target[1],
        app->orbit_target[2]);

    eye.x = target.x + app->orbit_radius * cos_pitch * sin_yaw;
    eye.y = target.y + app->orbit_radius * sin_pitch;
    eye.z = target.z - app->orbit_radius * cos_pitch * cos_yaw;

    dir = f3_normalize(f3_sub(target, eye));
    up = f3_make(0.0f, 1.0f, 0.0f);
    right = f3_cross(dir, up);
    if (f3_dot(right, right) < 1e-5f) {
        up = f3_make(0.0f, 0.0f, 1.0f);
    }

    app->cam_pos[0] = eye.x;
    app->cam_pos[1] = eye.y;
    app->cam_pos[2] = eye.z;
    app->cam_dir[0] = dir.x;
    app->cam_dir[1] = dir.y;
    app->cam_dir[2] = dir.z;
    app->cam_up[0] = up.x;
    app->cam_up[1] = up.y;
    app->cam_up[2] = up.z;
}

static void sync_orbit_from_camera(StandaloneApp *app)
{
    float dx = app->cam_pos[0] - app->orbit_target[0];
    float dy = app->cam_pos[1] - app->orbit_target[1];
    float dz = app->cam_pos[2] - app->orbit_target[2];
    float planar;

    app->orbit_radius = sqrtf(dx * dx + dy * dy + dz * dz);
    if (app->orbit_radius < 0.1f) {
        app->orbit_radius = 0.1f;
    }

    planar = sqrtf(dx * dx + dz * dz);
    app->orbit_pitch = atan2f(dy, fmaxf(planar, 1e-5f));
    app->orbit_yaw = atan2f(dx, -dz);
}

static void reset_workspace(StandaloneApp *app)
{
    app->render_w = DEFAULT_RENDER_W;
    app->render_h = DEFAULT_RENDER_H;
    app->step_size = 0.0025f;
    app->threshold = 0.95f;
    app->skip_mult = 2.0f;
    app->paused = 0;
    app->fov_y_deg = 45.0f;
    app->light_pos[0] = 1.5f;
    app->light_pos[1] = 1.5f;
    app->light_pos[2] = -1.0f;
    app->tf_center = 0.45f;
    app->tf_width = 0.35f;
    app->tf_opacity_scale = 1.0f;
    app->tf_palette = 0;
    app->tf_invert = 0;
    voxelpilot_set_default_clip_bounds(app->clip_min, app->clip_max);
    app->slice_x = 0.5f;
    app->slice_y = 0.5f;
    app->slice_z = 0.5f;
    app->measure_a[0] = 0.25f;
    app->measure_a[1] = 0.50f;
    app->measure_a[2] = 0.50f;
    app->measure_b[0] = 0.75f;
    app->measure_b[1] = 0.50f;
    app->measure_b[2] = 0.50f;
    app->measurement_visible = 1;
    app->measurement_target = 0;
    app->voxel_spacing[0] = 1.0f;
    app->voxel_spacing[1] = 1.0f;
    app->voxel_spacing[2] = 1.0f;
    app->orbit_target[0] = 0.5f;
    app->orbit_target[1] = 0.5f;
    app->orbit_target[2] = 0.5f;
    app->orbit_yaw = 0.0f;
    app->orbit_pitch = 0.0f;
    app->orbit_radius = 1.5f;
    sync_resolution_selection(app);
    apply_orbit_camera(app);
    strcpy(app->screenshot_status, "Workspace reset");
    strcpy(app->preset_status, "Workspace reset");
}

static void process_mouse_camera_controls(StandaloneApp *app)
{
    ImGuiIO *io;
    double mouse_x, mouse_y;
    int left_down;

    io = &ImGui::GetIO();
    glfwGetCursorPos(app->window, &mouse_x, &mouse_y);
    left_down = glfwGetMouseButton(app->window, GLFW_MOUSE_BUTTON_LEFT);

    if (!io->WantCaptureMouse && left_down == GLFW_PRESS) {
        if (!app->orbit_drag_active) {
            sync_orbit_from_camera(app);
            app->orbit_drag_active = 1;
        }
        float dx = (float)(mouse_x - app->last_mouse_x);
        float dy = (float)(mouse_y - app->last_mouse_y);

        app->orbit_yaw += dx * 0.01f;
        app->orbit_pitch += dy * 0.01f;
        if (app->orbit_pitch > 1.45f) app->orbit_pitch = 1.45f;
        if (app->orbit_pitch < -1.45f) app->orbit_pitch = -1.45f;
        apply_orbit_camera(app);
    } else {
        app->orbit_drag_active = 0;
    }

    if (!io->WantCaptureMouse && app->pending_scroll != 0.0f) {
        app->orbit_radius *= (1.0f - app->pending_scroll * 0.10f);
        if (app->orbit_radius < 0.25f) app->orbit_radius = 0.25f;
        if (app->orbit_radius > 6.0f) app->orbit_radius = 6.0f;
        apply_orbit_camera(app);
    }

    app->pending_scroll = 0.0f;
    app->last_mouse_x = mouse_x;
    app->last_mouse_y = mouse_y;
}

static void sync_resolution_selection(StandaloneApp *app)
{
    int i;

    for (i = 0; i < k_resolution_preset_count; ++i) {
        if (k_resolution_presets[i].width == app->render_w &&
            k_resolution_presets[i].height == app->render_h) {
            app->selected_resolution = i;
            return;
        }
    }

    app->selected_resolution = -1;
}

static int clamp_import_dimension(int value)
{
    if (value < 1) return 1;
    if (value > 4096) return 4096;
    return value;
}

static int query_file_size_bytes(const char *path, size_t *out_size)
{
    FILE *fp;
    __int64 size_64;

    if (!path || !path[0] || !out_size) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
#else
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
#endif

#if defined(_WIN32)
    size_64 = _ftelli64(fp);
#else
    size_64 = ftell(fp);
#endif
    fclose(fp);
    if (size_64 < 0) {
        return -1;
    }

    *out_size = (size_t)size_64;
    return 0;
}

static int parse_cli_int(const char *text, int *out_value)
{
    char *end = NULL;
    long value;

    if (!text || !out_value) {
        return -1;
    }

    value = strtol(text, &end, 10);
    if (end == text || (end && *end != '\0')) {
        return -1;
    }

    if (value < 1 || value > 4096) {
        return -1;
    }

    *out_value = (int)value;
    return 0;
}

static int parse_cli_data_type(const char *text, int *out_data_type)
{
    VolumeDataType data_type;

    if (!text || !out_data_type) {
        return -1;
    }

    if (volume_import_parse_data_type(text, &data_type) != 0) {
        return -1;
    }

    *out_data_type = (int)data_type;
    return 0;
}

static void refresh_upload_file_details(StandaloneApp *app)
{
    VolumeImportSpec inferred_spec;

    app->upload_file_size_known = 0;
    app->upload_file_size_bytes = 0;

    if (!app->upload_path[0]) {
        strcpy(app->upload_hint_status, "No file selected.");
        return;
    }

    if (query_file_size_bytes(app->upload_path, &app->upload_file_size_bytes) == 0) {
        app->upload_file_size_known = 1;
    }

    if (volume_import_infer_from_filename(app->upload_path, &inferred_spec) == 0) {
        app->upload_width = (int)inferred_spec.width;
        app->upload_height = (int)inferred_spec.height;
        app->upload_depth = (int)inferred_spec.depth;
        app->upload_data_type = (int)inferred_spec.data_type;
        snprintf(
            app->upload_hint_status,
            sizeof(app->upload_hint_status),
            "Detected %ux%ux%u %s from filename.",
            inferred_spec.width,
            inferred_spec.height,
            inferred_spec.depth,
            volume_import_data_type_label(inferred_spec.data_type));
    } else {
        strcpy(
            app->upload_hint_status,
            "No size/data-type pattern detected in filename. Enter the settings manually.");
    }
}

static int write_png_file(
    const char          *path,
    const unsigned char *pixels,
    int                  width,
    int                  height)
{
    return stbi_write_png(path, width, height, 1, pixels, width) ? 0 : -1;
}

static unsigned char tf_preview_value(
    const StandaloneApp *app,
    float                value)
{
    VolumeParams params;
    float r, g, b, a;
    float lum;

    memset(&params, 0, sizeof(params));
    params.tf_center = app->tf_center;
    params.tf_width = app->tf_width;
    params.tf_opacity_scale = app->tf_opacity_scale;
    params.tf_palette = app->tf_palette;
    params.tf_invert = app->tf_invert;

    tf_lookup(params, value, &r, &g, &b, &a);
    lum = ((r + g + b) / 3.0f) * fmaxf(a, 0.25f);
    return (unsigned char)(fminf(fmaxf(lum, 0.0f), 1.0f) * 255.0f);
}

static void update_slice_previews(StandaloneApp *app)
{
    int x, y, z;
    int W = app->renderer.W;
    int H = app->renderer.H;
    int D = app->renderer.D;
    int axial_z;
    int coronal_y;
    int sagittal_x;
    unsigned char *axial_pixels;
    unsigned char *coronal_pixels;
    unsigned char *sagittal_pixels;

    if (!app->renderer.volume_data || W < 1 || H < 1 || D < 1) {
        return;
    }

    axial_z = (int)(app->slice_z * (float)(D - 1));
    coronal_y = (int)(app->slice_y * (float)(H - 1));
    sagittal_x = (int)(app->slice_x * (float)(W - 1));

    axial_pixels = (unsigned char *)malloc((size_t)W * (size_t)H);
    coronal_pixels = (unsigned char *)malloc((size_t)W * (size_t)D);
    sagittal_pixels = (unsigned char *)malloc((size_t)H * (size_t)D);

    if (!axial_pixels || !coronal_pixels || !sagittal_pixels) {
        free(axial_pixels);
        free(coronal_pixels);
        free(sagittal_pixels);
        return;
    }

    for (y = 0; y < H; ++y) {
        for (x = 0; x < W; ++x) {
            size_t idx = (size_t)axial_z * (size_t)W * (size_t)H
                       + (size_t)y * (size_t)W
                       + (size_t)x;
            axial_pixels[y * W + x] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    for (z = 0; z < D; ++z) {
        for (x = 0; x < W; ++x) {
            size_t idx = (size_t)z * (size_t)W * (size_t)H
                       + (size_t)coronal_y * (size_t)W
                       + (size_t)x;
            coronal_pixels[z * W + x] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    for (z = 0; z < D; ++z) {
        for (y = 0; y < H; ++y) {
            size_t idx = (size_t)z * (size_t)W * (size_t)H
                       + (size_t)y * (size_t)W
                       + (size_t)sagittal_x;
            sagittal_pixels[z * H + y] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    upload_gray_texture(app->axial_tex, axial_pixels, W, H);
    upload_gray_texture(app->coronal_tex, coronal_pixels, W, D);
    upload_gray_texture(app->sagittal_tex, sagittal_pixels, H, D);

    free(axial_pixels);
    free(coronal_pixels);
    free(sagittal_pixels);
}

static void draw_slice_card(
    StandaloneApp *app,
    const char    *label,
    GLuint         texture,
    int            width,
    int            height,
    int            plane)
{
    float max_width = 120.0f;
    float scale = 1.0f;

    if (width > 0 && (float)width > max_width) {
        scale = max_width / (float)width;
    }

    ImGui::Text("%s", label);
    ImGui::Image(
        (ImTextureID)(intptr_t)texture,
        ImVec2((float)width * scale, (float)height * scale));

    if (app->measurement_visible &&
        ImGui::IsItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float u = (mouse.x - min.x) / fmaxf(max.x - min.x, 1.0f);
        float v = (mouse.y - min.y) / fmaxf(max.y - min.y, 1.0f);
        float point[3];
        float *target;

        u = fminf(fmaxf(u, 0.0f), 1.0f);
        v = fminf(fmaxf(v, 0.0f), 1.0f);

        point[0] = app->slice_x;
        point[1] = app->slice_y;
        point[2] = app->slice_z;

        if (plane == 0) {
            point[0] = u;
            point[1] = v;
        } else if (plane == 1) {
            point[0] = u;
            point[2] = v;
        } else {
            point[1] = u;
            point[2] = v;
        }

        target = (app->measurement_target == 0) ? app->measure_a : app->measure_b;
        target[0] = point[0];
        target[1] = point[1];
        target[2] = point[2];
        app->measurement_target = 1 - app->measurement_target;
    }
}

static int save_workspace_preset(StandaloneApp *app, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "render_w %d\n", app->render_w);
    fprintf(fp, "render_h %d\n", app->render_h);
    fprintf(fp, "step_size %.9f\n", app->step_size);
    fprintf(fp, "threshold %.9f\n", app->threshold);
    fprintf(fp, "skip_mult %.9f\n", app->skip_mult);
    fprintf(fp, "fov_y_deg %.9f\n", app->fov_y_deg);
    fprintf(fp, "tf_center %.9f\n", app->tf_center);
    fprintf(fp, "tf_width %.9f\n", app->tf_width);
    fprintf(fp, "tf_opacity_scale %.9f\n", app->tf_opacity_scale);
    fprintf(fp, "tf_palette %d\n", app->tf_palette);
    fprintf(fp, "tf_invert %d\n", app->tf_invert);
    fprintf(fp, "measurement_visible %d\n", app->measurement_visible);
    fprintf(fp, "measurement_target %d\n", app->measurement_target);
    fprintf(fp, "cam_pos %.9f %.9f %.9f\n", app->cam_pos[0], app->cam_pos[1], app->cam_pos[2]);
    fprintf(fp, "cam_dir %.9f %.9f %.9f\n", app->cam_dir[0], app->cam_dir[1], app->cam_dir[2]);
    fprintf(fp, "cam_up %.9f %.9f %.9f\n", app->cam_up[0], app->cam_up[1], app->cam_up[2]);
    fprintf(fp, "light_pos %.9f %.9f %.9f\n", app->light_pos[0], app->light_pos[1], app->light_pos[2]);
    fprintf(fp, "clip_min %.9f %.9f %.9f\n", app->clip_min[0], app->clip_min[1], app->clip_min[2]);
    fprintf(fp, "clip_max %.9f %.9f %.9f\n", app->clip_max[0], app->clip_max[1], app->clip_max[2]);
    fprintf(fp, "slice %.9f %.9f %.9f\n", app->slice_x, app->slice_y, app->slice_z);
    fprintf(fp, "measure_a %.9f %.9f %.9f\n", app->measure_a[0], app->measure_a[1], app->measure_a[2]);
    fprintf(fp, "measure_b %.9f %.9f %.9f\n", app->measure_b[0], app->measure_b[1], app->measure_b[2]);
    fprintf(fp, "voxel_spacing %.9f %.9f %.9f\n", app->voxel_spacing[0], app->voxel_spacing[1], app->voxel_spacing[2]);
    fprintf(fp, "orbit_target %.9f %.9f %.9f\n", app->orbit_target[0], app->orbit_target[1], app->orbit_target[2]);
    fprintf(fp, "orbit %.9f %.9f %.9f\n", app->orbit_yaw, app->orbit_pitch, app->orbit_radius);
    fclose(fp);
    return 0;
}

static int load_workspace_preset(StandaloneApp *app, const char *path)
{
    FILE *fp = fopen(path, "r");
    char key[64];

    if (!fp) {
        return -1;
    }

    while (fscanf(fp, "%63s", key) == 1) {
        if (strcmp(key, "render_w") == 0) fscanf(fp, "%d", &app->render_w);
        else if (strcmp(key, "render_h") == 0) fscanf(fp, "%d", &app->render_h);
        else if (strcmp(key, "step_size") == 0) fscanf(fp, "%f", &app->step_size);
        else if (strcmp(key, "threshold") == 0) fscanf(fp, "%f", &app->threshold);
        else if (strcmp(key, "skip_mult") == 0) fscanf(fp, "%f", &app->skip_mult);
        else if (strcmp(key, "fov_y_deg") == 0) fscanf(fp, "%f", &app->fov_y_deg);
        else if (strcmp(key, "tf_center") == 0) fscanf(fp, "%f", &app->tf_center);
        else if (strcmp(key, "tf_width") == 0) fscanf(fp, "%f", &app->tf_width);
        else if (strcmp(key, "tf_opacity_scale") == 0) fscanf(fp, "%f", &app->tf_opacity_scale);
        else if (strcmp(key, "tf_palette") == 0) fscanf(fp, "%d", &app->tf_palette);
        else if (strcmp(key, "tf_invert") == 0) fscanf(fp, "%d", &app->tf_invert);
        else if (strcmp(key, "measurement_visible") == 0) fscanf(fp, "%d", &app->measurement_visible);
        else if (strcmp(key, "measurement_target") == 0) fscanf(fp, "%d", &app->measurement_target);
        else if (strcmp(key, "cam_pos") == 0) fscanf(fp, "%f %f %f", &app->cam_pos[0], &app->cam_pos[1], &app->cam_pos[2]);
        else if (strcmp(key, "cam_dir") == 0) fscanf(fp, "%f %f %f", &app->cam_dir[0], &app->cam_dir[1], &app->cam_dir[2]);
        else if (strcmp(key, "cam_up") == 0) fscanf(fp, "%f %f %f", &app->cam_up[0], &app->cam_up[1], &app->cam_up[2]);
        else if (strcmp(key, "light_pos") == 0) fscanf(fp, "%f %f %f", &app->light_pos[0], &app->light_pos[1], &app->light_pos[2]);
        else if (strcmp(key, "clip_min") == 0) fscanf(fp, "%f %f %f", &app->clip_min[0], &app->clip_min[1], &app->clip_min[2]);
        else if (strcmp(key, "clip_max") == 0) fscanf(fp, "%f %f %f", &app->clip_max[0], &app->clip_max[1], &app->clip_max[2]);
        else if (strcmp(key, "slice") == 0) fscanf(fp, "%f %f %f", &app->slice_x, &app->slice_y, &app->slice_z);
        else if (strcmp(key, "measure_a") == 0) fscanf(fp, "%f %f %f", &app->measure_a[0], &app->measure_a[1], &app->measure_a[2]);
        else if (strcmp(key, "measure_b") == 0) fscanf(fp, "%f %f %f", &app->measure_b[0], &app->measure_b[1], &app->measure_b[2]);
        else if (strcmp(key, "voxel_spacing") == 0) fscanf(fp, "%f %f %f", &app->voxel_spacing[0], &app->voxel_spacing[1], &app->voxel_spacing[2]);
        else if (strcmp(key, "orbit_target") == 0) fscanf(fp, "%f %f %f", &app->orbit_target[0], &app->orbit_target[1], &app->orbit_target[2]);
        else if (strcmp(key, "orbit") == 0) fscanf(fp, "%f %f %f", &app->orbit_yaw, &app->orbit_pitch, &app->orbit_radius);
    }

    fclose(fp);
    sync_resolution_selection(app);
    return 0;
}

static void scroll_callback(
    GLFWwindow *window,
    double      xoffset,
    double      yoffset)
{
    StandaloneApp *app;
    (void)xoffset;

    app = (StandaloneApp *)glfwGetWindowUserPointer(window);
    if (!app) {
        return;
    }

    app->pending_scroll += (float)yoffset;
}

static void save_screenshot(StandaloneApp *app)
{
    const char *selected;
    const char *filter_patterns[] = { "*.png" };

    selected = tinyfd_saveFileDialog(
        "Save Render Snapshot",
        app->screenshot_path[0] ? app->screenshot_path : "volume_snapshot.png",
        1,
        filter_patterns,
        "PNG Image (*.png)"
    );

    if (!selected) {
        strcpy(app->screenshot_status, "Screenshot canceled");
        return;
    }

    strncpy(app->screenshot_path, selected, sizeof(app->screenshot_path) - 1);
    app->screenshot_path[sizeof(app->screenshot_path) - 1] = '\0';

    if (!app->renderer.h_out) {
        strcpy(app->screenshot_status, "No rendered frame available yet");
        return;
    }

    if (write_png_file(
            app->screenshot_path,
            app->renderer.h_out,
            app->render_w,
            app->render_h) != 0) {
        snprintf(
            app->screenshot_status,
            sizeof(app->screenshot_status),
            "Failed to save: %s",
            app->screenshot_path);
        return;
    }

    snprintf(
        app->screenshot_status,
        sizeof(app->screenshot_status),
        "Saved snapshot: %s",
        app->screenshot_path);
}

/* ============================================================
   Render one frame directly (no networking)
   ============================================================ */
static void render_frame_standalone(StandaloneApp *app)
{
    RendererInput cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.cam_pos[0] = app->cam_pos[0];
    cmd.cam_pos[1] = app->cam_pos[1];
    cmd.cam_pos[2] = app->cam_pos[2];
    cmd.cam_dir[0] = app->cam_dir[0];
    cmd.cam_dir[1] = app->cam_dir[1];
    cmd.cam_dir[2] = app->cam_dir[2];
    cmd.cam_up[0]  = app->cam_up[0];
    cmd.cam_up[1]  = app->cam_up[1];
    cmd.cam_up[2]  = app->cam_up[2];
    cmd.fov_y      = app->fov_y_deg * 3.14159265f / 180.0f;
    cmd.light_pos[0] = app->light_pos[0];
    cmd.light_pos[1] = app->light_pos[1];
    cmd.light_pos[2] = app->light_pos[2];
    cmd.step_size             = app->step_size;
    cmd.threshold             = app->threshold;
    cmd.empty_space_skip_mult = app->skip_mult;
    cmd.tf_center             = app->tf_center;
    cmd.tf_width              = app->tf_width;
    cmd.tf_opacity_scale      = app->tf_opacity_scale;
    cmd.tf_palette            = app->tf_palette;
    cmd.tf_invert             = app->tf_invert;
    memcpy(cmd.clip_min, app->clip_min, sizeof(cmd.clip_min));
    memcpy(cmd.clip_max, app->clip_max, sizeof(cmd.clip_max));
    cmd.img_width  = (uint32_t)app->render_w;
    cmd.img_height = (uint32_t)app->render_h;

    /* Apply directly to renderer - NO NETWORK */
    apply_render_command(&app->renderer, &cmd);

    /* Render directly on GPU */
    app->last_render_ms = render_frame_gpu(&app->renderer);
    app->frame_id++;

    /* Upload pixels directly to OpenGL texture */
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    if (app->render_w != app->tex_w ||
        app->render_h != app->tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                     app->render_w, app->render_h,
                     0, GL_RED, GL_UNSIGNED_BYTE,
                     app->renderer.h_out);
        app->tex_w = app->render_w;
        app->tex_h = app->render_h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        app->render_w, app->render_h,
                        GL_RED, GL_UNSIGNED_BYTE,
                        app->renderer.h_out);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ============================================================
   Draw fullscreen quad
   ============================================================ */
static void draw_fullscreen_quad(StandaloneApp *app)
{
    glUseProgram(app->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    glUniform1i(
        glGetUniformLocation(app->prog, "tex"), 0);
    glBindVertexArray(app->vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

static void draw_dockspace(void)
{
#ifdef IMGUI_HAS_DOCK
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();

    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) {
        window_flags |= ImGuiWindowFlags_NoBackground;
    }

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpaceRoot", NULL, window_flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("VoxelPilotDockspace"), ImVec2(0.0f, 0.0f), dockspace_flags);
    ImGui::End();
#endif
}

static void ensure_default_dock_layout(StandaloneApp *app)
{
#ifdef IMGUI_HAS_DOCK
    ImGuiID dockspace_id;
    ImGuiID dock_main_id;
    ImGuiID dock_left_id;
    ImGuiID dock_right_id;
    ImGuiID dock_bottom_id;
    ImGuiID dock_right_mid_id;
    ImGuiID dock_right_bottom_id;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();

    dockspace_id = ImGui::GetID("VoxelPilotDockspace");

    if (app->dock_layout_ready &&
        !voxelpilot_should_rebuild_dock_layout(
            app->dock_layout_viewport_w,
            app->dock_layout_viewport_h,
            viewport->Size.x,
            viewport->Size.y)) {
        return;
    }

    app->dock_layout_viewport_w = viewport->Size.x;
    app->dock_layout_viewport_h = viewport->Size.y;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        ImGuiDockNodeFlags_DockSpace |
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    dock_main_id = dockspace_id;
    dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.28f, NULL, &dock_main_id);
    dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.30f, NULL, &dock_main_id);
    dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.12f, NULL, &dock_main_id);
    dock_right_bottom_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.52f, NULL, &dock_right_id);
    dock_right_mid_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.34f, NULL, &dock_right_id);

    ImGui::DockBuilderDockWindow("Controls", dock_left_id);
    ImGui::DockBuilderDockWindow("Insights", dock_right_id);
    ImGui::DockBuilderDockWindow("Metadata", dock_right_mid_id);
    ImGui::DockBuilderDockWindow("Help", dock_right_bottom_id);
    ImGui::DockBuilderDockWindow("About", dock_right_bottom_id);
    ImGui::DockBuilderDockWindow("Status", dock_bottom_id);
    ImGui::DockBuilderFinish(dockspace_id);

    app->dock_layout_ready = 1;
#else
    (void)app;
#endif
}

static void set_default_window_layout(
    const StandaloneApp *app,
    const char          *name)
{
#ifndef IMGUI_HAS_DOCK
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiCond layout_cond = ImGuiCond_Always;
    float left_width;
    float right_width;
    float bottom_height;
    float main_height;
    float right_top_height;
    float right_mid_height;
    float right_low_height;
    float right_bottom_height;

    (void)app;

    left_width = viewport->Size.x * 0.28f;
    right_width = viewport->Size.x * 0.30f;
    bottom_height = viewport->Size.y * 0.12f;
    main_height = viewport->Size.y - bottom_height;
    right_top_height = main_height * 0.42f;
    right_mid_height = main_height * 0.18f;
    right_low_height = main_height * 0.18f;
    right_bottom_height = main_height - right_top_height - right_mid_height - right_low_height;

    if (strcmp(name, "Controls") == 0) {
        ImGui::SetNextWindowCollapsed(false, layout_cond);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(left_width, main_height),
            layout_cond);
    } else if (strcmp(name, "Insights") == 0) {
        ImGui::SetNextWindowCollapsed(false, layout_cond);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_top_height),
            layout_cond);
    } else if (strcmp(name, "Metadata") == 0) {
        ImGui::SetNextWindowCollapsed(false, layout_cond);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + right_top_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_mid_height),
            layout_cond);
    } else if (strcmp(name, "Help") == 0) {
        ImGui::SetNextWindowCollapsed(false, layout_cond);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + right_top_height + right_mid_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_low_height),
            layout_cond);
    } else if (strcmp(name, "About") == 0) {
        ImGui::SetNextWindowCollapsed(false, layout_cond);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + right_top_height + right_mid_height + right_low_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_bottom_height),
            layout_cond);
    } else if (strcmp(name, "Status") == 0) {
        ImGui::SetNextWindowCollapsed(false, layout_cond);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x,
                   viewport->Pos.y + viewport->Size.y - bottom_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(viewport->Size.x, bottom_height),
            layout_cond);
    }
#else
    (void)app;
    (void)name;
#endif
}

static void draw_controls_panel(StandaloneApp *app)
{
    set_default_window_layout(app, "Controls");
    ImGui::Begin("Controls");
    {
        float measurement_voxels = measurement_distance(app);

    /* Status */
    ImGui::TextColored(
        ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
        "%s", BRAND_SUBTITLE);
    ImGui::TextColored(
        ImVec4(0.72f, 0.78f, 0.86f, 1.0f),
        "Interactive NVIDIA workstation for volume rendering, slice review, and session capture");

    ImGui::Separator();

    /* Stats */
    ImGui::Text("Frame ID: %u",   (unsigned)app->frame_id);
    ImGui::Text("GPU Render: %.2f ms", app->last_render_ms);
    ImGui::Text("GUI FPS: %.1f",  app->gui_fps);
    ImGui::TextWrapped("VoxelPilot viewport: drag to orbit, use the mouse wheel to zoom");

    if (ImGui::Button("Reset Workspace")) {
        reset_workspace(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Guided Walkthrough")) {
        app->walkthrough_visible = 1;
        app->walkthrough_step = 0;
    }

    ImGui::Separator();

    /* Render Settings */
    ImGui::Text("Rendering");
    ImGui::SliderFloat("Step Size",
                       &app->step_size,    0.0005f, 0.01f);
    ImGui::SliderFloat("Opacity Threshold",
                       &app->threshold,    0.1f,    1.0f);
    ImGui::SliderFloat("Skip Multiplier",
                       &app->skip_mult,    1.0f,    8.0f);

    {
        const char *current_label = "Custom";
        if (app->selected_resolution >= 0 &&
            app->selected_resolution < k_resolution_preset_count) {
            current_label =
                k_resolution_presets[app->selected_resolution].label;
        }

        if (ImGui::BeginCombo("Render Resolution", current_label)) {
            int i;
            for (i = 0; i < k_resolution_preset_count; ++i) {
                int selected = (i == app->selected_resolution);
                if (ImGui::Selectable(
                        k_resolution_presets[i].label,
                        selected)) {
                    app->render_w = k_resolution_presets[i].width;
                    app->render_h = k_resolution_presets[i].height;
                    app->selected_resolution = i;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::InputInt("Width", &app->render_w)) {
        if (app->render_w < 64) app->render_w = 64;
        if (app->render_w > 4096) app->render_w = 4096;
        sync_resolution_selection(app);
    }

    if (ImGui::InputInt("Height", &app->render_h)) {
        if (app->render_h < 64) app->render_h = 64;
        if (app->render_h > 4096) app->render_h = 4096;
        sync_resolution_selection(app);
    }

    ImGui::Separator();
    ImGui::Text("Transfer Mapping");
    ImGui::SliderFloat("Center", &app->tf_center, 0.0f, 1.0f);
    ImGui::SliderFloat("Width", &app->tf_width, 0.02f, 1.0f);
    ImGui::SliderFloat("Opacity Scale", &app->tf_opacity_scale, 0.1f, 3.0f);
    ImGui::Combo("Palette", &app->tf_palette, k_tf_palette_labels, 4);
    {
        bool invert_transfer = (app->tf_invert != 0);
        if (ImGui::Checkbox("Invert Transfer", &invert_transfer)) {
            app->tf_invert = invert_transfer ? 1 : 0;
        }
    }

    ImGui::Separator();
    ImGui::Text("Volume Clipping");
    ImGui::SliderFloat3("Clip Min", app->clip_min, 0.0f, 1.0f);
    ImGui::SliderFloat3("Clip Max", app->clip_max, 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) {
        if (app->clip_min[i] > app->clip_max[i]) {
            float tmp = app->clip_min[i];
            app->clip_min[i] = app->clip_max[i];
            app->clip_max[i] = tmp;
        }
    }

    ImGui::Separator();
    ImGui::Text("Measurement");
    {
        bool show_measurement = (app->measurement_visible != 0);
        if (ImGui::Checkbox("Show Measurement", &show_measurement)) {
            app->measurement_visible = show_measurement ? 1 : 0;
        }
    }
    ImGui::SliderFloat3("Point A", app->measure_a, 0.0f, 1.0f);
    ImGui::SliderFloat3("Point B", app->measure_b, 0.0f, 1.0f);
    ImGui::Text("Active placement target: Point %s", app->measurement_target == 0 ? "A" : "B");
    if (ImGui::Button("Set A From Slices")) {
        app->measure_a[0] = app->slice_x;
        app->measure_a[1] = app->slice_y;
        app->measure_a[2] = app->slice_z;
    }
    ImGui::SameLine();
    if (ImGui::Button("Set B From Slices")) {
        app->measure_b[0] = app->slice_x;
        app->measure_b[1] = app->slice_y;
        app->measure_b[2] = app->slice_z;
    }
    ImGui::Text("Distance: %.3f voxels | %.3f world units",
                measurement_voxels,
                measurement_distance_world(app));
    ImGui::Text("Delta XYZ: (%.3f, %.3f, %.3f)",
                (app->measure_b[0] - app->measure_a[0]) * (float)app->renderer.W,
                (app->measure_b[1] - app->measure_a[1]) * (float)app->renderer.H,
                (app->measure_b[2] - app->measure_a[2]) * (float)app->renderer.D);

    ImGui::Separator();
    ImGui::Text("Voxel Spacing");
    ImGui::InputFloat3("Spacing XYZ", app->voxel_spacing);
    for (int i = 0; i < 3; ++i) {
        if (app->voxel_spacing[i] <= 0.0f) app->voxel_spacing[i] = 1.0f;
    }

    ImGui::Separator();
    ImGui::Text("Workspace Sessions");
    if (ImGui::Button("Save Preset")) {
        const char *patterns[] = { "*.vpilot" };
        const char *selected = tinyfd_saveFileDialog(
            "Save VoxelPilot Workspace",
            app->preset_path[0] ? app->preset_path : "workspace.vpilot",
            1,
            patterns,
            "VoxelPilot Workspace (*.vpilot)");
        if (selected) {
            strncpy(app->preset_path, selected, sizeof(app->preset_path) - 1);
            app->preset_path[sizeof(app->preset_path) - 1] = '\0';
            if (save_workspace_preset(app, app->preset_path) == 0) {
                snprintf(app->preset_status, sizeof(app->preset_status),
                         "Saved preset: %s", app->preset_path);
            } else {
                snprintf(app->preset_status, sizeof(app->preset_status),
                         "Failed to save preset: %s", app->preset_path);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Preset")) {
        const char *patterns[] = { "*.vpilot", "*" };
        const char *selected = tinyfd_openFileDialog(
            "Load VoxelPilot Workspace",
            "",
            2,
            patterns,
            "VoxelPilot Workspace (*.vpilot)",
            0);
        if (selected) {
            strncpy(app->preset_path, selected, sizeof(app->preset_path) - 1);
            app->preset_path[sizeof(app->preset_path) - 1] = '\0';
            if (load_workspace_preset(app, app->preset_path) == 0) {
                apply_orbit_camera(app);
                snprintf(app->preset_status, sizeof(app->preset_status),
                         "Loaded preset: %s", app->preset_path);
            } else {
                snprintf(app->preset_status, sizeof(app->preset_status),
                         "Failed to load preset: %s", app->preset_path);
            }
        }
    }
    ImGui::TextWrapped("Session status: %s", app->preset_status);

    ImGui::Separator();

    /* Camera */
    ImGui::Text("Camera Framing");
    ImGui::SliderFloat3("Position",  app->cam_pos,
                        -2.0f, 2.0f);
    ImGui::SliderFloat3("Direction", app->cam_dir,
                        -1.0f, 1.0f);
    ImGui::SliderFloat3("Up",        app->cam_up,
                        -1.0f, 1.0f);
    ImGui::SliderFloat("FOV (deg)",
                       &app->fov_y_deg, 10.0f, 120.0f);

    if (ImGui::Button("Front")) {
        set_camera_preset(app,
            0.5f, 0.5f, -1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Side")) {
        set_camera_preset(app,
            -1.0f, 0.5f, 0.5f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Top")) {
        set_camera_preset(app,
            0.5f, 1.7f, 0.5f,
            0.0f, -1.0f, 0.0f,
            0.0f, 0.0f, 1.0f);
    }
    if (ImGui::Button("Isometric")) {
        set_camera_preset(app,
            -0.9f, 1.2f, -0.9f,
            0.7f, -0.35f, 0.7f,
            0.0f, 1.0f, 0.0f);
    }

    ImGui::Separator();

    /* Light */
    ImGui::Text("Lighting");
    ImGui::SliderFloat3("Light Pos", app->light_pos,
                        -3.0f, 3.0f);

    ImGui::Separator();

    /* Pause */
    {
        bool paused = (app->paused != 0);
        if (ImGui::Checkbox("Pause Rendering", &paused)) {
            app->paused = paused ? 1 : 0;
        }
    }

    ImGui::Separator();

    /* ---- Volume Upload (direct, no network) ---- */
    ImGui::Text("Volume Import");

    if (ImGui::InputInt("Width", &app->upload_width)) {
        app->upload_width = clamp_import_dimension(app->upload_width);
    }
    if (ImGui::InputInt("Height", &app->upload_height)) {
        app->upload_height = clamp_import_dimension(app->upload_height);
    }
    if (ImGui::InputInt("Depth", &app->upload_depth)) {
        app->upload_depth = clamp_import_dimension(app->upload_depth);
    }
    ImGui::Combo("Data Type",
                 &app->upload_data_type,
                 k_volume_data_type_labels, 3);

    {
        VolumeImportSpec spec = {
            (uint32_t)app->upload_width,
            (uint32_t)app->upload_height,
            (uint32_t)app->upload_depth,
            (VolumeDataType)app->upload_data_type
        };
        size_t expected = 0;

        if (volume_import_expected_bytes(&spec, &expected) == 0) {
            ImGui::Text("Expected dataset size: %.1f MB",
                        (double)expected / (1024.0 * 1024.0));
            if (app->upload_file_size_known) {
                ImGui::Text("Selected file size: %.1f MB",
                            (double)app->upload_file_size_bytes / (1024.0 * 1024.0));
                if (app->upload_file_size_bytes == expected) {
                    ImGui::TextColored(
                        ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
                        "Current settings match the selected file.");
                } else {
                    ImGui::TextColored(
                        ImVec4(0.94f, 0.66f, 0.36f, 1.0f),
                        "Current settings do not match the selected file size.");
                }
            }
        }
    }

    ImGui::TextWrapped("Import hint: %s", app->upload_hint_status);

    /* Browse button - opens native file dialog */
    if (ImGui::Button("Browse..."))
    {
        const char *filter_patterns[] = {
            "*.raw", "*.bin", "*.dat", "*"
        };
        const char *selected = tinyfd_openFileDialog(
            "Select Volume File",
            "",
            4,
            filter_patterns,
            "Volume Files (*.raw *.bin *.dat)",
            0
        );
        if (selected != NULL) {
            strncpy(app->upload_path, selected,
                    sizeof(app->upload_path) - 1);
            app->upload_path[
                sizeof(app->upload_path)-1] = '\0';
            refresh_upload_file_details(app);
            strcpy(app->upload_status,
                   "File selected. Review the import settings, then click Load Volume.");
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##filepath",
                     app->upload_path,
                     sizeof(app->upload_path),
                     ImGuiInputTextFlags_ReadOnly);

    /* Load button - loads DIRECTLY to GPU, no network */
    if (ImGui::Button("Load Volume"))
    {
        VolumeImportSpec spec = {
            (uint32_t)clamp_import_dimension(app->upload_width),
            (uint32_t)clamp_import_dimension(app->upload_height),
            (uint32_t)clamp_import_dimension(app->upload_depth),
            (VolumeDataType)app->upload_data_type
        };
        float *temp = NULL;
        size_t voxel_count = 0;
        char error[256];
        size_t expected = 0;

        app->upload_width = (int)spec.width;
        app->upload_height = (int)spec.height;
        app->upload_depth = (int)spec.depth;

        if (!app->upload_path[0]) {
            strcpy(app->upload_status,
                   "Select a raw volume file before loading.");
        } else if (volume_import_load(
                       app->upload_path,
                       &spec,
                       &temp,
                       &voxel_count,
                       error,
                       sizeof(error)) != 0) {
            snprintf(app->upload_status,
                     sizeof(app->upload_status),
                     "%s",
                     error);
        } else {
            volume_import_expected_bytes(&spec, &expected);
            strcpy(app->upload_status,
                   "Loading to GPU...");
            reload_volume(
                &app->renderer,
                temp,
                (int)spec.width,
                (int)spec.height,
                (int)spec.depth);
            update_histogram(app);
            snprintf(app->upload_status,
                     sizeof(app->upload_status),
                     "Loaded: %ux%ux%u %s (%zu voxels, %.1f MB)",
                     spec.width, spec.height, spec.depth,
                     volume_import_data_type_label(spec.data_type),
                     voxel_count,
                     (double)expected / (1024.0 * 1024.0));
            volume_import_free(temp);
            temp = NULL;
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("Import status: %s",
                        app->upload_status);

    ImGui::Separator();
    ImGui::Text("Capture & Export");
    if (ImGui::Button("Save Snapshot")) {
        save_screenshot(app);
    }
    ImGui::TextWrapped("Snapshot status: %s",
                       app->screenshot_status);

    }
    ImGui::End();
}

static void draw_insights_panel(StandaloneApp *app)
{
    set_default_window_layout(app, "Insights");
    ImGui::Begin("Insights");

    ImGui::Text("Orthogonal Slice Review");
    ImGui::SliderFloat("Sagittal X", &app->slice_x, 0.0f, 1.0f);
    ImGui::SliderFloat("Coronal Y", &app->slice_y, 0.0f, 1.0f);
    ImGui::SliderFloat("Axial Z", &app->slice_z, 0.0f, 1.0f);

    if (app->renderer.W > 0 && app->renderer.H > 0 && app->renderer.D > 0) {
        draw_slice_card(app, "Axial", app->axial_tex, app->renderer.W, app->renderer.H, 0);
        ImGui::SameLine();
        draw_slice_card(app, "Coronal", app->coronal_tex, app->renderer.W, app->renderer.D, 1);
        draw_slice_card(app, "Sagittal", app->sagittal_tex, app->renderer.H, app->renderer.D, 2);
    }

    if (app->measurement_visible) {
        ImGui::Separator();
        ImGui::Text("Measurement Summary");
        ImGui::Text("A: (%.3f, %.3f, %.3f)",
                    app->measure_a[0], app->measure_a[1], app->measure_a[2]);
        ImGui::Text("B: (%.3f, %.3f, %.3f)",
                    app->measure_b[0], app->measure_b[1], app->measure_b[2]);
        ImGui::Text("Distance: %.3f voxels", measurement_distance(app));
        ImGui::Text("World distance: %.3f units", measurement_distance_world(app));
    }

    ImGui::Separator();
    ImGui::Text("Intensity Histogram");
    if (app->histogram_ready) {
        ImGui::Text("Range: %.4f to %.4f", app->hist_min_value, app->hist_max_value);
        ImGui::PlotHistogram(
            "##volume_histogram",
            app->histogram,
            HISTOGRAM_BINS,
            0,
            "Volume intensity distribution",
            0.0f,
            FLT_MAX,
            ImVec2(0.0f, 180.0f));
    } else {
        ImGui::TextWrapped("Histogram becomes available after a volume is loaded.");
    }

    ImGui::End();
}

static void draw_metadata_panel(StandaloneApp *app)
{
    size_t voxel_count =
        (size_t)app->renderer.W *
        (size_t)app->renderer.H *
        (size_t)app->renderer.D;

    set_default_window_layout(app, "Metadata");
    ImGui::Begin("Metadata");
    ImGui::Text("Dataset Summary");
    ImGui::Separator();
    ImGui::Text("Dimensions: %d x %d x %d",
                app->renderer.W, app->renderer.H, app->renderer.D);
    ImGui::Text("Voxel Count: %zu", voxel_count);
    ImGui::Text("Value Range: %.4f to %.4f",
                app->hist_min_value, app->hist_max_value);
    ImGui::Text("Histogram Peak: %.0f samples", app->hist_peak_count);
    ImGui::Text("Render Target: %d x %d", app->render_w, app->render_h);
    ImGui::Text("Voxel Spacing: %.3f / %.3f / %.3f",
                app->voxel_spacing[0],
                app->voxel_spacing[1],
                app->voxel_spacing[2]);
    ImGui::Text("Transfer Palette: %s", k_tf_palette_labels[app->tf_palette]);
    ImGui::Text("Clipping State: %s", clipping_is_active(app) ? "Active" : "Full Volume");
    ImGui::TextWrapped("Dataset status: %s", app->upload_status);
    ImGui::End();
}

static void draw_help_panel(StandaloneApp *app)
{
    (void)app;

    set_default_window_layout(app, "Help");
    ImGui::Begin("Help");

    ImGui::Text("Demo Launch Guide");
    ImGui::Separator();
    ImGui::BulletText("Use a Windows laptop with an NVIDIA GPU and working display driver.");
    ImGui::BulletText("Confirm `nvidia-smi` responds before launch.");
    ImGui::BulletText("Start the demo with `Run_VoxelPilot.bat`.");
    ImGui::BulletText("Confirm Controls, Insights, Metadata, About, and Status are visible.");
    ImGui::BulletText("If no dataset is loaded yet, the app should still open on the synthetic fallback volume.");

    ImGui::Spacing();
    ImGui::Text("Quick Demo Flow");
    ImGui::Separator();
    ImGui::BulletText("Use `Browse...`, confirm `Width`, `Height`, `Depth`, and `Data Type`, then click `Load Volume`.");
    ImGui::BulletText("Try `Front` and `Isometric`, then orbit and zoom in the main viewport.");
    ImGui::BulletText("Adjust `Sagittal X`, `Coronal Y`, and `Axial Z` in Insights for slice review.");
    ImGui::BulletText("Use `Set A From Slices` and `Set B From Slices` for a quick measurement pass.");
    ImGui::BulletText("Click `Save Snapshot` to export a PNG still.");
    ImGui::BulletText("Save a workspace session if you want to revisit the same setup later.");

    ImGui::Spacing();
    ImGui::TextWrapped(
        "If VoxelPilot fails to start on the NVIDIA laptop, check the NVIDIA driver, confirm the app is using the discrete GPU, and verify OpenGL is available on the active display path.");

    ImGui::End();
}

static void draw_about_panel(StandaloneApp *app)
{
    (void)app;

    set_default_window_layout(app, "About");
    ImGui::Begin("About");

    ImGui::TextColored(
        ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
        "%s", BRAND_TITLE);
    ImGui::Text("%s", BRAND_SUBTITLE);
    ImGui::Separator();
    ImGui::TextWrapped(
        "A single-machine CUDA volume exploration workstation built for fast interactive rendering, slice inspection, and demo-ready review on NVIDIA hardware.");

    ImGui::Spacing();
    ImGui::Text("Current Highlights");
    ImGui::Separator();
    ImGui::BulletText("CUDA volume rendering with live camera framing and lighting control.");
    ImGui::BulletText("Transfer mapping, clipping, histogram analysis, and orthogonal slice review.");
    ImGui::BulletText("Mouse orbit and zoom, preset viewpoints, measurement tools, and PNG snapshots.");
    ImGui::BulletText("Workspace session save/load plus a packaged Windows launcher flow.");

    ImGui::Spacing();
    ImGui::Text("Next Version");
    ImGui::Separator();
    ImGui::BulletText("Direct viewport-based measurement, annotation, and richer review tools.");
    ImGui::BulletText("Better metadata import and calibrated dataset spacing workflows.");
    ImGui::BulletText("AI-assisted exploration, summaries, and guided inspection layers.");

    ImGui::End();
}

static void draw_demo_banner(StandaloneApp *app)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (!app->demo_mode_enabled) {
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + 16.0f),
        ImGuiCond_Always);
    ImGui::Begin("DemoBanner", NULL, flags);
    ImGui::TextColored(
        ImVec4(0.96f, 0.85f, 0.36f, 1.0f),
        "Demo Mode Active");
    ImGui::SameLine();
    ImGui::Text("| %s", BRAND_TITLE);
    ImGui::TextWrapped(
        "Presentation build for the NVIDIA laptop: use the guided walkthrough to move through the demo in order.");
    if (ImGui::Button("Start Guided Walkthrough")) {
        app->walkthrough_visible = 1;
        app->walkthrough_step = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide Demo Banner")) {
        app->demo_mode_enabled = 0;
    }
    ImGui::End();
}

static void draw_walkthrough_overlay(StandaloneApp *app)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;
    const char *title;
    const char *body;

    if (!app->walkthrough_visible) {
        return;
    }

    if (app->walkthrough_step < 0) {
        app->walkthrough_step = 0;
    }
    if (app->walkthrough_step >= k_walkthrough_step_count) {
        app->walkthrough_step = k_walkthrough_step_count - 1;
    }

    title = k_walkthrough_titles[app->walkthrough_step];
    body = k_walkthrough_bodies[app->walkthrough_step];

    ImGui::SetNextWindowSize(ImVec2(480.0f, 260.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
               viewport->Pos.y + viewport->Size.y * 0.5f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::OpenPopup("Guided Walkthrough");

    if (ImGui::BeginPopupModal("Guided Walkthrough", NULL, flags)) {
        ImGui::TextColored(
            ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
            "%s", title);
        ImGui::Separator();
        ImGui::TextWrapped("%s", body);
        ImGui::Spacing();
        ImGui::Text("Presenter cue");
        ImGui::BulletText("Keep the main viewport visible while describing this step.");
        ImGui::BulletText("Use the Controls and Insights panels to demonstrate the action live.");

        ImGui::Separator();
        ImGui::Text("Step %d of %d",
                    app->walkthrough_step + 1,
                    k_walkthrough_step_count);

        if (app->walkthrough_step > 0) {
            if (ImGui::Button("Previous")) {
                app->walkthrough_step--;
            }
            ImGui::SameLine();
        }

        if (app->walkthrough_step + 1 < k_walkthrough_step_count) {
            if (ImGui::Button("Next")) {
                app->walkthrough_step++;
            }
        } else {
            if (ImGui::Button("Finish")) {
                app->walkthrough_visible = 0;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            app->walkthrough_visible = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void update_startup_splash(StandaloneApp *app)
{
    double elapsed;
    int status_index;

    if (!app->splash_visible) {
        return;
    }

    elapsed = glfwGetTime() - app->splash_start_time;
    status_index = (int)(elapsed / 0.95);
    if (status_index < 0) status_index = 0;
    if (status_index >= k_splash_status_count) {
        status_index = k_splash_status_count - 1;
    }
    app->splash_status_index = status_index;

    if (elapsed >= 3.8) {
        app->splash_visible = 0;
    }
}

static void draw_startup_splash(StandaloneApp *app)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    double elapsed;
    float progress;
    float pulse;
    int active_dot_count;
    int i;

    if (!app->splash_visible) {
        return;
    }

    elapsed = glfwGetTime() - app->splash_start_time;
    progress = (float)(elapsed / 3.6);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    pulse = 0.65f + 0.35f * sinf((float)elapsed * 3.0f);
    active_dot_count = ((int)(elapsed * 3.0) % 3) + 1;

    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::Begin("StartupSplash", NULL, flags);

    {
        float card_w = 560.0f;
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 p0 = viewport->Pos;
        ImVec2 p1 = ImVec2(viewport->Pos.x + viewport->Size.x,
                           viewport->Pos.y + viewport->Size.y);

        draw_list->AddRectFilledMultiColor(
            p0, p1,
            IM_COL32(10, 16, 22, 245),
            IM_COL32(14, 28, 34, 245),
            IM_COL32(8, 16, 20, 245),
            IM_COL32(12, 22, 26, 245));

        ImGui::SetCursorPos(
            ImVec2((viewport->Size.x - card_w) * 0.5f,
                   viewport->Size.y * 0.22f));
        ImGui::BeginChild(
            "SplashCard",
            ImVec2(card_w, 320.0f),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextColored(
            ImVec4(0.96f, 0.85f, 0.36f, 1.0f),
            "VoxelPilot NVIDIA Laptop Demo");
        ImGui::TextColored(
            ImVec4(0.40f, 0.88f, 0.78f, 0.88f + 0.12f * pulse),
            "%s", BRAND_TITLE);
        ImGui::Text("%s", BRAND_SUBTITLE);
        ImGui::Separator();
        ImGui::TextWrapped(
            "A unified CUDA volume exploration workstation for interactive rendering, slice inspection, and presentation-ready review on local NVIDIA hardware.");
        ImGui::Spacing();
        ImGui::Text("Startup sequence");
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        ImGui::TextWrapped("%s", k_splash_statuses[app->splash_status_index]);
        ImGui::Text("Loading");
        ImGui::SameLine();
        for (i = 0; i < 3; ++i) {
            ImVec4 dot_color =
                (i < active_dot_count)
                ? ImVec4(0.95f, 0.83f, 0.35f, 0.85f + 0.15f * pulse)
                : ImVec4(0.34f, 0.42f, 0.48f, 0.45f);
            ImGui::TextColored(dot_color, "•");
            if (i < 2) {
                ImGui::SameLine();
            }
        }
        ImGui::Spacing();
        for (i = 0; i < k_splash_status_count; ++i) {
            if (i < app->splash_status_index) {
                ImGui::BulletText("[Ready] %s", k_splash_statuses[i]);
            } else if (i == app->splash_status_index) {
                ImGui::BulletText("[Active] %s", k_splash_statuses[i]);
            } else {
                ImGui::BulletText("[Queued] %s", k_splash_statuses[i]);
            }
        }

        if (progress >= 0.92f) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.72f, 0.86f, 0.78f, 1.0f),
                "Handing off to the VoxelPilot workspace...");
        }

        ImGui::EndChild();
    }

    ImGui::End();
}

static void draw_status_bar(StandaloneApp *app)
{
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize;

    set_default_window_layout(app, "Status");
    ImGui::Begin("Status", NULL, flags);
    ImGui::Text("Volume: %dx%dx%d", app->renderer.W, app->renderer.H, app->renderer.D);
    ImGui::SameLine();
    ImGui::Text("| Render: %.2f ms", app->last_render_ms);
    ImGui::SameLine();
    ImGui::Text("| FPS: %.1f", app->gui_fps);
    ImGui::SameLine();
    ImGui::Text("| Clip: %s", clipping_is_active(app) ? "On" : "Off");
    ImGui::SameLine();
    ImGui::Text("| Measure: %.3f vox / %.3f u",
                measurement_distance(app),
                measurement_distance_world(app));
    ImGui::SameLine();
    ImGui::Text("| Preset: %s", app->preset_status);
    ImGui::End();
}

static void draw_gui(StandaloneApp *app)
{
    if (app->splash_visible) {
        draw_startup_splash(app);
        return;
    }

    draw_dockspace();
    ensure_default_dock_layout(app);
    draw_demo_banner(app);
    draw_controls_panel(app);
    draw_insights_panel(app);
    draw_metadata_panel(app);
    draw_help_panel(app);
    draw_about_panel(app);
    draw_walkthrough_overlay(app);
    draw_status_bar(app);
}

/* ============================================================
   Main Entry Point
   ============================================================ */
int main(int argc, char **argv)
{
    StandaloneApp app;
    const char   *volume_path = NULL;
    int           startup_width = 256;
    int           startup_height = 256;
    int           startup_depth = 256;
    int           startup_data_type = VOLUME_DATA_FLOAT32;
    int           startup_width_set = 0;
    int           startup_height_set = 0;
    int           startup_depth_set = 0;
    int           startup_type_set = 0;
    int           brick_dim   = 64;
    char          cuda_status[256];
    char          startup_error[256];
    size_t        startup_voxel_count = 0;
    float        *startup_volume_data = NULL;
    VolumeImportSpec startup_spec;
    VolumeImportSpec inferred_spec;
    int           inferred_spec_valid = 0;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--volume") == 0 &&
            i+1 < argc) {
            volume_path = argv[++i];
        } else if (strcmp(argv[i], "--dim") == 0 &&
                   i+1 < argc) {
            if (parse_cli_int(argv[++i], &startup_width) != 0) {
                fprintf(stderr, "Invalid value for --dim: %s\n", argv[i]);
                return -1;
            }
            startup_height = startup_width;
            startup_depth = startup_width;
            startup_width_set = 1;
            startup_height_set = 1;
            startup_depth_set = 1;
        } else if (strcmp(argv[i], "--width") == 0 &&
                   i+1 < argc) {
            if (parse_cli_int(argv[++i], &startup_width) != 0) {
                fprintf(stderr, "Invalid value for --width: %s\n", argv[i]);
                return -1;
            }
            startup_width_set = 1;
        } else if (strcmp(argv[i], "--height") == 0 &&
                   i+1 < argc) {
            if (parse_cli_int(argv[++i], &startup_height) != 0) {
                fprintf(stderr, "Invalid value for --height: %s\n", argv[i]);
                return -1;
            }
            startup_height_set = 1;
        } else if (strcmp(argv[i], "--depth") == 0 &&
                   i+1 < argc) {
            if (parse_cli_int(argv[++i], &startup_depth) != 0) {
                fprintf(stderr, "Invalid value for --depth: %s\n", argv[i]);
                return -1;
            }
            startup_depth_set = 1;
        } else if (strcmp(argv[i], "--type") == 0 &&
                   i+1 < argc) {
            if (parse_cli_data_type(argv[++i], &startup_data_type) != 0) {
                fprintf(stderr,
                        "Invalid value for --type: %s (use uint8, uint16, or float32)\n",
                        argv[i]);
                return -1;
            }
            startup_type_set = 1;
        } else if (strcmp(argv[i], "--dim") == 0 &&
                   i+1 >= argc) {
            fprintf(stderr, "Missing value for --dim\n");
            return -1;
        } else if ((strcmp(argv[i], "--width") == 0 ||
                    strcmp(argv[i], "--height") == 0 ||
                    strcmp(argv[i], "--depth") == 0 ||
                    strcmp(argv[i], "--type") == 0 ||
                    strcmp(argv[i], "--volume") == 0) &&
                   i+1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return -1;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr,
                    "Usage: %s [--volume file.raw] [--dim N | --width W --height H --depth D] [--type uint8|uint16|float32]\n",
                    argv[0]);
            return -1;
        }
    }

    memset(&startup_spec, 0, sizeof(startup_spec));
    memset(&inferred_spec, 0, sizeof(inferred_spec));
    memset(startup_error, 0, sizeof(startup_error));

    if (volume_path) {
        if (volume_import_infer_from_filename(volume_path, &inferred_spec) == 0) {
            inferred_spec_valid = 1;
            if (!startup_width_set) {
                startup_width = (int)inferred_spec.width;
            }
            if (!startup_height_set) {
                startup_height = (int)inferred_spec.height;
            }
            if (!startup_depth_set) {
                startup_depth = (int)inferred_spec.depth;
            }
            if (!startup_type_set) {
                startup_data_type = (int)inferred_spec.data_type;
            }
        }

        startup_spec.width = (uint32_t)clamp_import_dimension(startup_width);
        startup_spec.height = (uint32_t)clamp_import_dimension(startup_height);
        startup_spec.depth = (uint32_t)clamp_import_dimension(startup_depth);
        startup_spec.data_type = (VolumeDataType)startup_data_type;

        if (volume_import_load(
                volume_path,
                &startup_spec,
                &startup_volume_data,
                &startup_voxel_count,
                startup_error,
                sizeof(startup_error)) != 0) {
            fprintf(stderr, "Startup import failed: %s\n", startup_error);
        } else {
            printf("Startup import prepared: %ux%ux%u %s (%zu voxels)\n",
                   startup_spec.width,
                   startup_spec.height,
                   startup_spec.depth,
                   volume_import_data_type_label(startup_spec.data_type),
                   startup_voxel_count);
        }
    }

    memset(&app, 0, sizeof(app));
    app.win_w    = DEFAULT_WIN_W;
    app.win_h    = DEFAULT_WIN_H;
    app.render_w = DEFAULT_RENDER_W;
    app.render_h = DEFAULT_RENDER_H;
    app.selected_resolution = 1;

    /* Default camera */
    app.cam_pos[0] =  0.5f;
    app.cam_pos[1] =  0.5f;
    app.cam_pos[2] = -1.0f;
    app.cam_dir[0] =  0.0f;
    app.cam_dir[1] =  0.0f;
    app.cam_dir[2] =  1.0f;
    app.cam_up[0]  =  0.0f;
    app.cam_up[1]  =  1.0f;
    app.cam_up[2]  =  0.0f;
    app.fov_y_deg  = 45.0f;

    /* Default light */
    app.light_pos[0] =  1.5f;
    app.light_pos[1] =  1.5f;
    app.light_pos[2] = -1.0f;

    /* Default render params */
    app.step_size  = 0.0025f;
    app.threshold  = 0.95f;
    app.skip_mult  = 2.0f;
    app.paused     = 0;
    app.tf_center = 0.45f;
    app.tf_width = 0.35f;
    app.tf_opacity_scale = 1.0f;
    app.tf_palette = 0;
    app.tf_invert = 0;
    voxelpilot_set_default_clip_bounds(app.clip_min, app.clip_max);
    app.slice_x = 0.5f;
    app.slice_y = 0.5f;
    app.slice_z = 0.5f;
    app.orbit_target[0] = 0.5f;
    app.orbit_target[1] = 0.5f;
    app.orbit_target[2] = 0.5f;
    app.orbit_yaw = 0.0f;
    app.orbit_pitch = 0.0f;
    app.orbit_radius = 1.5f;
    app.pending_scroll = 0.0f;
    app.demo_mode_enabled = 1;
    app.walkthrough_visible = 0;
    app.walkthrough_step = 0;
    app.splash_visible = 1;
    app.splash_start_time = 0.0;
    app.splash_status_index = 0;

    /* Upload defaults */
    app.upload_width = clamp_import_dimension(startup_width);
    app.upload_height = clamp_import_dimension(startup_height);
    app.upload_depth = clamp_import_dimension(startup_depth);
    app.upload_data_type = startup_data_type;
    app.upload_file_size_bytes = 0;
    app.upload_file_size_known = 0;
    if (volume_path) {
        strncpy(app.upload_path, volume_path, sizeof(app.upload_path) - 1);
        app.upload_path[sizeof(app.upload_path) - 1] = '\0';
        if (query_file_size_bytes(app.upload_path, &app.upload_file_size_bytes) == 0) {
            app.upload_file_size_known = 1;
        }
        if (inferred_spec_valid) {
            snprintf(app.upload_hint_status,
                     sizeof(app.upload_hint_status),
                     "Detected %ux%ux%u %s from filename.",
                     inferred_spec.width,
                     inferred_spec.height,
                     inferred_spec.depth,
                     volume_import_data_type_label(inferred_spec.data_type));
        } else {
            strcpy(app.upload_hint_status,
                   "No size/data-type pattern detected in filename. Using the current import settings.");
        }
    } else {
        strcpy(app.upload_hint_status, "No file selected.");
    }
    strcpy(app.upload_status, "No volume loaded");
    strcpy(app.screenshot_status, "No snapshot saved");
    strcpy(app.screenshot_path, "volume_snapshot.png");
    strcpy(app.preset_status, "No preset loaded");
    strcpy(app.preset_path, "workspace.vpilot");
    sync_resolution_selection(&app);
    apply_orbit_camera(&app);

    if (!detect_cuda_runtime(cuda_status, sizeof(cuda_status))) {
        fprintf(stderr, "%s\n", cuda_status);
        strncpy(app.upload_status, cuda_status, sizeof(app.upload_status) - 1);
        app.upload_status[sizeof(app.upload_status) - 1] = '\0';
        if (startup_volume_data) {
            volume_import_free(startup_volume_data);
            startup_volume_data = NULL;
        }
        return -1;
    }

    /* Init CUDA renderer directly */
    renderer_state_init(
        &app.renderer,
        NULL,
        app.upload_width,
        app.upload_height,
        app.upload_depth,
        brick_dim);
    if (startup_volume_data) {
        strcpy(app.upload_status, "Loading startup volume to GPU...");
        reload_volume(
            &app.renderer,
            startup_volume_data,
            (int)startup_spec.width,
            (int)startup_spec.height,
            (int)startup_spec.depth);
        update_histogram(&app);
        snprintf(app.upload_status,
                 sizeof(app.upload_status),
                 "Loaded on startup: %ux%ux%u %s (%zu voxels)",
                 startup_spec.width,
                 startup_spec.height,
                 startup_spec.depth,
                 volume_import_data_type_label(startup_spec.data_type),
                 startup_voxel_count);
        volume_import_free(startup_volume_data);
        startup_volume_data = NULL;
    } else {
        update_histogram(&app);
        if (volume_path && startup_error[0]) {
            snprintf(app.upload_status,
                     sizeof(app.upload_status),
                     "Startup import failed: %s. Synthetic volume loaded instead.",
                     startup_error);
        }
    }

    /* GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit() failed\n");
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,
                   GLFW_OPENGL_CORE_PROFILE);

    app.window = glfwCreateWindow(
        app.win_w, app.win_h,
        BRAND_TITLE " | " BRAND_SUBTITLE,
        NULL, NULL);
    if (!app.window) {
        fprintf(stderr, "glfwCreateWindow() failed\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(app.window, &app);
    glfwSetScrollCallback(app.window, scroll_callback);

    /* GLEW */
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "glewInit() failed\n");
        return -1;
    }

    printf("OpenGL %s\n", glGetString(GL_VERSION));

    /* ImGui */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    {
        ImGuiIO *io = &ImGui::GetIO();
        io->IniFilename = NULL;
#ifdef IMGUI_HAS_DOCK
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    }
    apply_demo_theme();
    ImGui_ImplGlfw_InitForOpenGL(app.window, 1);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* GL resources */
    if (setup_gl_resources(&app) != 0) {
        fprintf(stderr, "GL setup failed\n");
        return -1;
    }

    app.splash_start_time = glfwGetTime();

    /* FPS tracking */
    double last_time   = glfwGetTime();
    int    frame_count = 0;

    /* ====================================================
       Main Loop
       ==================================================== */
    while (!glfwWindowShouldClose(app.window))
    {
        glfwPollEvents();

        /* FPS */
        double now = glfwGetTime();
        frame_count++;
        if (now - last_time >= 1.0) {
            app.gui_fps = (float)frame_count
                        / (float)(now - last_time);
            frame_count = 0;
            last_time   = now;
        }

        /* ImGui */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        update_startup_splash(&app);
        if (!app.splash_visible) {
            process_mouse_camera_controls(&app);
        }
        draw_gui(&app);

        /* Render directly on GPU */
        if (!app.paused) {
            render_frame_standalone(&app);
        }
        update_slice_previews(&app);

        /* Draw */
        int fb_w, fb_h;
        glfwGetFramebufferSize(
            app.window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.055f, 0.075f, 0.095f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        draw_fullscreen_quad(&app);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData());

        glfwSwapBuffers(app.window);
    }

    /* ====================================================
       Cleanup
       ==================================================== */
    renderer_state_cleanup(&app.renderer);
    cudaDeviceReset();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (app.gl_tex) glDeleteTextures(1, &app.gl_tex);
    if (app.axial_tex) glDeleteTextures(1, &app.axial_tex);
    if (app.coronal_tex) glDeleteTextures(1, &app.coronal_tex);
    if (app.sagittal_tex) glDeleteTextures(1, &app.sagittal_tex);
    if (app.prog)   glDeleteProgram(app.prog);
    if (app.vbo)    glDeleteBuffers(1, &app.vbo);
    if (app.ebo)    glDeleteBuffers(1, &app.ebo);
    if (app.vao)    glDeleteVertexArrays(1, &app.vao);

    glfwDestroyWindow(app.window);
    glfwTerminate();

    printf("Standalone renderer exited cleanly.\n");
    return 0;
}
