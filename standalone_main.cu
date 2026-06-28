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
#include "common/ai_features.h"
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
#define MAX_ANNOTATIONS  8
#define ANNOTATION_NAME_MAX 64

typedef struct {
    const char *label;
    int         width;
    int         height;
} ResolutionPreset;

typedef struct {
    int           active;
    unsigned char label_id;
    char          name[ANNOTATION_NAME_MAX];
    float         color[3];
    float         seed[3];
    float         seed_intensity;
    float         tolerance;
    size_t        voxel_count;
    float         min_intensity;
    float         max_intensity;
} AnnotationRegion;

/* ============================================================
   Standalone App State
   ============================================================ */
typedef struct {
    /* Window */
    GLFWwindow   *window;
    int           win_w, win_h;
    int           layout_refit_next_frame;
    int           window_was_maximized;

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
    int           render_match_window;
    int           render_quality_mode;

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
    float         ai_focus_slice;
    float         ai_focus_score;
    char          ai_assist_status[256];
    char          ai_prompt[256];
    int           annotation_mode;
    char          annotation_name[ANNOTATION_NAME_MAX];
    float         annotation_tolerance;
    float         annotation_color[3];
    AnnotationRegion annotations[MAX_ANNOTATIONS];
    int           annotation_count;
    int           selected_annotation;
    unsigned char *label_mask;
    int           label_mask_W;
    int           label_mask_H;
    int           label_mask_D;
    uint32_t      label_mask_revision;
    int           label_overlay_visible;
    float         label_overlay_alpha;
    char          annotation_status[256];
    char          hover_description[256];
    int           main_hover_active;
    VoxelPilotRayPickResult main_hover_pick;
    char          main_hover_description[256];
    unsigned char main_hover_label_id;
    VoxelPilotObjectSummary object_summary;
    int           object_summary_ready;
    char          object_summary_status[512];
    VoxelPilotQuantMetrics quant_metrics;
    int           quant_metrics_ready;
    char          quant_metrics_status[256];
    char          report_status[256];
    char          report_path[MAX_PATH_BUF];
    int           streaming_visible_bricks;
    int           streaming_resident_candidates;
    int           streaming_budget_bricks;
    int           streaming_stream_now;
    int           streaming_queue;
    int           streaming_evictable;
    char          streaming_status[256];
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

    /* Persistent slice preview buffers (reused each frame, realloced on dim change) */
    unsigned char *slice_axial_buf;
    unsigned char *slice_coronal_buf;
    unsigned char *slice_sagittal_buf;
    int            slice_buf_W;
    int            slice_buf_H;
    int            slice_buf_D;

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

static const char *k_render_quality_labels[] = {
    "Interactive GPU",
    "Balanced GPU",
    "Quality GPU"
};

static void apply_render_quality_preset(StandaloneApp *app, int quality_mode);
static int apply_auto_enhance_transfer(StandaloneApp *app, const char *reason);
static int apply_auto_enhance_on_load_if_safe(StandaloneApp *app, const char *reason);

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
static void reset_annotations(StandaloneApp *app);
static void create_annotation_region_at_point(
    StandaloneApp *app,
    const float    point[3]);
static void point_from_slice_uv(
    const StandaloneApp *app,
    int                  plane,
    float                u,
    float                v,
    float                point[3]);

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
    "    outColor = vec4(texture(tex, uv).rgb, 1.0);\n"
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 app->render_w, app->render_h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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

static void apply_render_quality_preset(StandaloneApp *app, int quality_mode)
{
    if (!app) {
        return;
    }

    if (quality_mode < VOXELPILOT_RENDER_QUALITY_INTERACTIVE ||
        quality_mode > VOXELPILOT_RENDER_QUALITY_QUALITY) {
        quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    }

    app->render_quality_mode = quality_mode;

    if (quality_mode == VOXELPILOT_RENDER_QUALITY_INTERACTIVE) {
        app->step_size = 0.0035f;
        app->threshold = 0.94f;
        app->skip_mult = 2.50f;
    } else if (quality_mode == VOXELPILOT_RENDER_QUALITY_QUALITY) {
        app->step_size = 0.00125f;
        app->threshold = 0.98f;
        app->skip_mult = 1.00f;
    } else {
        app->step_size = 0.0020f;
        app->threshold = 0.96f;
        app->skip_mult = 1.50f;
    }
}

static void reset_workspace(StandaloneApp *app)
{
    app->render_w = DEFAULT_RENDER_W;
    app->render_h = DEFAULT_RENDER_H;
    app->render_match_window = 1;
    apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_BALANCED);
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
    app->annotation_mode = 0;
    app->annotation_tolerance = 0.035f;
    app->annotation_color[0] = 0.95f;
    app->annotation_color[1] = 0.28f;
    app->annotation_color[2] = 0.16f;
    app->label_overlay_visible = 1;
    app->label_overlay_alpha = 0.55f;
    app->object_summary_ready = 0;
    snprintf(app->object_summary_status,
             sizeof(app->object_summary_status),
             "Click Analyze Loaded Volume to generate a local object summary.");
    app->quant_metrics_ready = 0;
    snprintf(app->quant_metrics_status,
             sizeof(app->quant_metrics_status),
             "Click Refresh Quant Metrics to summarize the active volume.");
    snprintf(app->report_status, sizeof(app->report_status),
             "No report exported yet.");
    snprintf(app->report_path, sizeof(app->report_path),
             "voxelpilot_insight_report.html");
    app->main_hover_active = 0;
    app->main_hover_label_id = 0;
    memset(&app->main_hover_pick, 0, sizeof(app->main_hover_pick));
    snprintf(app->main_hover_description,
             sizeof(app->main_hover_description),
             "Hover over the 3D render for a direct pick explanation.");
    snprintf(app->annotation_name, sizeof(app->annotation_name), "Region");
    reset_annotations(app);
    app->voxel_spacing[0] = 1.0f;
    app->voxel_spacing[1] = 1.0f;
    app->voxel_spacing[2] = 1.0f;
    app->orbit_target[0] = 0.5f;
    app->orbit_target[1] = 0.5f;
    app->orbit_target[2] = 0.5f;
    app->orbit_yaw = 0.0f;
    app->orbit_pitch = 0.0f;
    app->orbit_radius = 1.2f;
    sync_resolution_selection(app);
    set_camera_preset(app,
        -0.35f, 0.95f, -0.35f,
        0.7f, -0.35f, 0.7f,
        0.0f, 1.0f, 0.0f);
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

static void sync_render_target_to_framebuffer(
    StandaloneApp *app,
    int            framebuffer_width,
    int            framebuffer_height)
{
    if (!voxelpilot_should_sync_render_target(
            app->render_match_window,
            app->render_w,
            app->render_h,
            framebuffer_width,
            framebuffer_height)) {
        return;
    }

    app->render_w = voxelpilot_clamp_render_extent(framebuffer_width);
    app->render_h = voxelpilot_clamp_render_extent(framebuffer_height);
    sync_resolution_selection(app);
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

static int parse_cli_float(const char *text, float min_value, float max_value, float *out_value)
{
    char *end = NULL;
    double value;

    if (!text || !out_value) {
        return -1;
    }

    value = strtod(text, &end);
    if (end == text || (end && *end != '\0')) {
        return -1;
    }

    if (value < (double)min_value || value > (double)max_value) {
        return -1;
    }

    *out_value = (float)value;
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
    const unsigned char *pixels,
    int                  width,
    int                  height,
    const char          *path)
{
    return stbi_write_png(path, width, height, 4, pixels, width * 4) ? 0 : -1;
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

    if (!app->renderer.volume_data || W < 1 || H < 1 || D < 1) {
        return;
    }

    /* Reallocate persistent buffers only when volume dimensions change */
    if (W != app->slice_buf_W ||
        H != app->slice_buf_H ||
        D != app->slice_buf_D) {

        free(app->slice_axial_buf);
        free(app->slice_coronal_buf);
        free(app->slice_sagittal_buf);

        app->slice_axial_buf    = (unsigned char *)malloc((size_t)W * (size_t)H);
        app->slice_coronal_buf  = (unsigned char *)malloc((size_t)W * (size_t)D);
        app->slice_sagittal_buf = (unsigned char *)malloc((size_t)H * (size_t)D);

        if (!app->slice_axial_buf ||
            !app->slice_coronal_buf ||
            !app->slice_sagittal_buf) {
            free(app->slice_axial_buf);
            free(app->slice_coronal_buf);
            free(app->slice_sagittal_buf);
            app->slice_axial_buf    = NULL;
            app->slice_coronal_buf  = NULL;
            app->slice_sagittal_buf = NULL;
            app->slice_buf_W = 0;
            app->slice_buf_H = 0;
            app->slice_buf_D = 0;
            return;
        }

        app->slice_buf_W = W;
        app->slice_buf_H = H;
        app->slice_buf_D = D;
    }

    axial_z    = (int)(app->slice_z * (float)(D - 1));
    coronal_y  = (int)(app->slice_y * (float)(H - 1));
    sagittal_x = (int)(app->slice_x * (float)(W - 1));

    for (y = 0; y < H; ++y) {
        for (x = 0; x < W; ++x) {
            size_t idx = (size_t)axial_z * (size_t)W * (size_t)H
                       + (size_t)y * (size_t)W
                       + (size_t)x;
            app->slice_axial_buf[y * W + x] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    for (z = 0; z < D; ++z) {
        for (x = 0; x < W; ++x) {
            size_t idx = (size_t)z * (size_t)W * (size_t)H
                       + (size_t)coronal_y * (size_t)W
                       + (size_t)x;
            app->slice_coronal_buf[z * W + x] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    for (z = 0; z < D; ++z) {
        for (y = 0; y < H; ++y) {
            size_t idx = (size_t)z * (size_t)W * (size_t)H
                       + (size_t)y * (size_t)W
                       + (size_t)sagittal_x;
            app->slice_sagittal_buf[z * H + y] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    upload_gray_texture(app->axial_tex,    app->slice_axial_buf,    W, H);
    upload_gray_texture(app->coronal_tex,  app->slice_coronal_buf,  W, D);
    upload_gray_texture(app->sagittal_tex, app->slice_sagittal_buf, H, D);
}

static void reset_annotations(StandaloneApp *app)
{
    if (app->label_mask) {
        memset(
            app->label_mask,
            0,
            (size_t)app->label_mask_W *
            (size_t)app->label_mask_H *
            (size_t)app->label_mask_D);
    }

    memset(app->annotations, 0, sizeof(app->annotations));
    app->annotation_count = 0;
    app->selected_annotation = -1;
    app->label_mask_revision++;
    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "No annotation regions yet.");
}

static int ensure_label_mask(StandaloneApp *app)
{
    size_t voxel_count;
    int W = app->renderer.W;
    int H = app->renderer.H;
    int D = app->renderer.D;

    if (!app->renderer.volume_data || W < 1 || H < 1 || D < 1) {
        return 0;
    }

    if (app->label_mask &&
        app->label_mask_W == W &&
        app->label_mask_H == H &&
        app->label_mask_D == D) {
        return 1;
    }

    free(app->label_mask);
    app->label_mask = NULL;
    app->label_mask_W = W;
    app->label_mask_H = H;
    app->label_mask_D = D;

    voxel_count = (size_t)W * (size_t)H * (size_t)D;
    app->label_mask = (unsigned char *)calloc(voxel_count, 1);
    if (!app->label_mask) {
        app->label_mask_W = 0;
        app->label_mask_H = 0;
        app->label_mask_D = 0;
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Could not allocate label mask for this volume.");
        return 0;
    }

    reset_annotations(app);
    return 1;
}

static int point_to_voxel_index(
    const StandaloneApp *app,
    const float          point[3],
    int                 *out_x,
    int                 *out_y,
    int                 *out_z,
    size_t              *out_index)
{
    int W = app->renderer.W;
    int H = app->renderer.H;
    int D = app->renderer.D;
    int x;
    int y;
    int z;

    if (!point || W < 1 || H < 1 || D < 1) {
        return 0;
    }

    x = (int)(voxelpilot_ai_clampf(point[0], 0.0f, 1.0f) * (float)(W - 1) + 0.5f);
    y = (int)(voxelpilot_ai_clampf(point[1], 0.0f, 1.0f) * (float)(H - 1) + 0.5f);
    z = (int)(voxelpilot_ai_clampf(point[2], 0.0f, 1.0f) * (float)(D - 1) + 0.5f);

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_z) *out_z = z;
    if (out_index) {
        *out_index =
            (size_t)z * (size_t)W * (size_t)H +
            (size_t)y * (size_t)W +
            (size_t)x;
    }

    return 1;
}

static void point_from_slice_uv(
    const StandaloneApp *app,
    int                  plane,
    float                u,
    float                v,
    float                point[3])
{
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
}

static AnnotationRegion *find_annotation_by_label(
    StandaloneApp *app,
    unsigned char  label_id)
{
    int i;

    for (i = 0; i < MAX_ANNOTATIONS; ++i) {
        if (app->annotations[i].active &&
            app->annotations[i].label_id == label_id) {
            return &app->annotations[i];
        }
    }

    return NULL;
}

static void clear_label_from_mask(
    StandaloneApp *app,
    unsigned char  label_id)
{
    size_t voxel_count;
    size_t i;

    if (!app->label_mask || label_id == 0) {
        return;
    }

    voxel_count =
        (size_t)app->label_mask_W *
        (size_t)app->label_mask_H *
        (size_t)app->label_mask_D;

    for (i = 0; i < voxel_count; ++i) {
        if (app->label_mask[i] == label_id) {
            app->label_mask[i] = 0;
        }
    }
}

static void create_annotation_region_at_point(
    StandaloneApp *app,
    const float    point[3])
{
    AnnotationRegion *region;
    size_t seed_index = 0;
    size_t count = 0;
    float min_value = 0.0f;
    float max_value = 0.0f;
    int seed_x = 0;
    int seed_y = 0;
    int seed_z = 0;
    int slot;
    unsigned char label_id;

    if (!ensure_label_mask(app)) {
        return;
    }

    if (!point_to_voxel_index(app, point, &seed_x, &seed_y, &seed_z, &seed_index)) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Could not place annotation on this volume.");
        return;
    }

    slot = app->selected_annotation;
    if (slot < 0 || slot >= MAX_ANNOTATIONS || !app->annotations[slot].active) {
        if (app->annotation_count >= MAX_ANNOTATIONS) {
            snprintf(app->annotation_status, sizeof(app->annotation_status),
                     "Maximum annotation count reached.");
            return;
        }
        slot = app->annotation_count;
        app->annotation_count++;
    }

    region = &app->annotations[slot];
    label_id = (unsigned char)(slot + 1);
    clear_label_from_mask(app, label_id);

    if (!voxelpilot_flood_fill_label_region(
            app->renderer.volume_data,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            seed_x,
            seed_y,
            seed_z,
            app->annotation_tolerance,
            label_id,
            app->label_mask,
            &count,
            &min_value,
            &max_value)) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Flood fill failed for annotation seed.");
        return;
    }

    memset(region, 0, sizeof(*region));
    region->active = 1;
    region->label_id = label_id;
    snprintf(region->name, sizeof(region->name),
             "%s",
             app->annotation_name[0] ? app->annotation_name : "Region");
    region->color[0] = app->annotation_color[0];
    region->color[1] = app->annotation_color[1];
    region->color[2] = app->annotation_color[2];
    region->seed[0] = point[0];
    region->seed[1] = point[1];
    region->seed[2] = point[2];
    region->seed_intensity = app->renderer.volume_data[seed_index];
    region->tolerance = app->annotation_tolerance;
    region->voxel_count = count;
    region->min_intensity = min_value;
    region->max_intensity = max_value;
    app->selected_annotation = slot;
    app->label_mask_revision++;

    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Annotated %s: %zu voxels around intensity %.3f.",
             region->name,
             region->voxel_count,
             region->seed_intensity);
}

static void export_annotation_mask(StandaloneApp *app)
{
    const char *patterns[] = { "*.raw" };
    const char *selected;
    char json_path[MAX_PATH_BUF + 16];
    FILE *fp;
    FILE *meta;
    size_t voxel_count;
    int i;

    if (!app->label_mask || app->annotation_count == 0) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Create at least one annotation before exporting.");
        return;
    }

    selected = tinyfd_saveFileDialog(
        "Export VoxelPilot Annotation Mask",
        "voxelpilot_annotation_mask.raw",
        1,
        patterns,
        "Raw Label Mask (*.raw)");

    if (!selected) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Annotation export canceled.");
        return;
    }

    voxel_count =
        (size_t)app->label_mask_W *
        (size_t)app->label_mask_H *
        (size_t)app->label_mask_D;

    fp = fopen(selected, "wb");
    if (!fp) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Could not write annotation mask.");
        return;
    }
    fwrite(app->label_mask, 1, voxel_count, fp);
    fclose(fp);

    snprintf(json_path, sizeof(json_path), "%s.json", selected);
    meta = fopen(json_path, "w");
    if (meta) {
        fprintf(meta, "{\n");
        fprintf(meta, "  \"dimensions\": [%d, %d, %d],\n",
                app->label_mask_W,
                app->label_mask_H,
                app->label_mask_D);
        fprintf(meta, "  \"labels\": [\n");
        for (i = 0; i < app->annotation_count; ++i) {
            AnnotationRegion *region = &app->annotations[i];
            if (!region->active) continue;
            fprintf(meta,
                    "    {\"id\": %u, \"name\": \"%s\", \"voxels\": %zu, \"seed\": [%.6f, %.6f, %.6f], \"tolerance\": %.6f, \"intensityRange\": [%.6f, %.6f]}%s\n",
                    (unsigned int)region->label_id,
                    region->name,
                    region->voxel_count,
                    region->seed[0],
                    region->seed[1],
                    region->seed[2],
                    region->tolerance,
                    region->min_intensity,
                    region->max_intensity,
                    (i + 1 < app->annotation_count) ? "," : "");
        }
        fprintf(meta, "  ]\n");
        fprintf(meta, "}\n");
        fclose(meta);
    }

    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Exported annotation mask: %s", selected);
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

    if (ImGui::IsItemHovered()) {
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float u = (mouse.x - min.x) / fmaxf(max.x - min.x, 1.0f);
        float v = (mouse.y - min.y) / fmaxf(max.y - min.y, 1.0f);
        float point[3];
        int vx = 0;
        int vy = 0;
        int vz = 0;
        size_t voxel_index = 0;

        u = fminf(fmaxf(u, 0.0f), 1.0f);
        v = fminf(fmaxf(v, 0.0f), 1.0f);
        point_from_slice_uv(app, plane, u, v, point);

        if (point_to_voxel_index(app, point, &vx, &vy, &vz, &voxel_index) &&
            app->renderer.volume_data) {
            float intensity = app->renderer.volume_data[voxel_index];
            unsigned char label_id = 0;
            AnnotationRegion *region = NULL;

            if (app->label_mask &&
                app->label_mask_W == app->renderer.W &&
                app->label_mask_H == app->renderer.H &&
                app->label_mask_D == app->renderer.D) {
                label_id = app->label_mask[voxel_index];
                region = find_annotation_by_label(app, label_id);
            }

            voxelpilot_describe_voxel_context(
                intensity,
                label_id,
                region ? region->name : "",
                app->hover_description,
                sizeof(app->hover_description));

            ImGui::BeginTooltip();
            ImGui::Text("%s", app->hover_description);
            ImGui::Text("Voxel: %d, %d, %d", vx, vy, vz);
            if (region) {
                ImGui::Text("Region voxels: %zu", region->voxel_count);
                ImGui::ColorButton("##label_color",
                                   ImVec4(region->color[0],
                                          region->color[1],
                                          region->color[2],
                                          1.0f),
                                   ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(18.0f, 18.0f));
            }
            ImGui::EndTooltip();
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (app->annotation_mode) {
                create_annotation_region_at_point(app, point);
            } else if (app->measurement_visible) {
                float *target = (app->measurement_target == 0) ? app->measure_a : app->measure_b;
                target[0] = point[0];
                target[1] = point[1];
                target[2] = point[2];
                app->measurement_target = 1 - app->measurement_target;
            }
        }
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
    fprintf(fp, "render_match_window %d\n", app->render_match_window);
    fprintf(fp, "render_quality_mode %d\n", app->render_quality_mode);
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
        else if (strcmp(key, "render_match_window") == 0) fscanf(fp, "%d", &app->render_match_window);
        else if (strcmp(key, "render_quality_mode") == 0) fscanf(fp, "%d", &app->render_quality_mode);
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
            (const unsigned char *)app->renderer.h_out,
            app->render_w,
            app->render_h,
            app->screenshot_path) != 0) {
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
   Render one frame to the CUDA output buffer only (no GL window)
   ============================================================ */
static void render_frame_gpu_snapshot(StandaloneApp *app)
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

    apply_render_command(&app->renderer, &cmd);
    app->last_render_ms = render_frame_gpu(&app->renderer);
    app->frame_id++;
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
    if (app->label_overlay_visible &&
        app->label_mask &&
        app->annotation_count > 0 &&
        app->label_mask_W == app->renderer.W &&
        app->label_mask_H == app->renderer.H &&
        app->label_mask_D == app->renderer.D) {
        int i;

        cmd.label_mask = app->label_mask;
        cmd.labels.enabled = 1;
        cmd.labels.width = app->label_mask_W;
        cmd.labels.height = app->label_mask_H;
        cmd.labels.depth = app->label_mask_D;
        cmd.labels.label_count = MAX_ANNOTATIONS;
        cmd.labels.alpha = app->label_overlay_alpha;
        cmd.labels.revision = app->label_mask_revision;

        for (i = 0; i < MAX_ANNOTATIONS; ++i) {
            cmd.labels.colors[i * 3 + 0] = app->annotations[i].color[0];
            cmd.labels.colors[i * 3 + 1] = app->annotations[i].color[1];
            cmd.labels.colors[i * 3 + 2] = app->annotations[i].color[2];
        }
    }

    /* Apply directly to renderer - NO NETWORK */
    apply_render_command(&app->renderer, &cmd);

    /* Render directly on GPU */
    app->last_render_ms = render_frame_gpu(&app->renderer);
    app->frame_id++;

    /* Upload pixels directly to OpenGL texture (RGBA) */
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (app->render_w != app->tex_w ||
        app->render_h != app->tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     app->render_w, app->render_h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE,
                     app->renderer.h_out);
        app->tex_w = app->render_w;
        app->tex_h = app->render_h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        app->render_w, app->render_h,
                        GL_RGBA, GL_UNSIGNED_BYTE,
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
    ImGuiID dock_right_ai_id;
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
    dock_right_bottom_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.24f, NULL, &dock_right_id);
    dock_right_mid_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.18f, NULL, &dock_right_id);
    dock_right_ai_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.50f, NULL, &dock_right_id);

    ImGui::DockBuilderDockWindow("Controls", dock_left_id);
    ImGui::DockBuilderDockWindow("Insights", dock_right_id);
    ImGui::DockBuilderDockWindow("AI Assist", dock_right_ai_id);
    ImGui::DockBuilderDockWindow("Metadata", dock_right_mid_id);
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
    ImGuiCond layout_cond =
        (app && app->layout_refit_next_frame)
            ? ImGuiCond_Always
            : ImGuiCond_FirstUseEver;
    float left_width;
    float right_width;
    float bottom_height;
    float main_height;
    float right_top_height;
    float right_ai_height;
    float right_mid_height;
    float right_bottom_height;

    (void)app;

    left_width = viewport->Size.x * 0.28f;
    right_width = viewport->Size.x * 0.30f;
    bottom_height = viewport->Size.y * 0.12f;
    main_height = viewport->Size.y - bottom_height;
    right_top_height = main_height * 0.22f;
    right_ai_height = main_height * 0.43f;
    right_mid_height = main_height * 0.16f;
    right_bottom_height =
        main_height -
        right_top_height -
        right_ai_height -
        right_mid_height;

    if (strcmp(name, "Controls") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(left_width, main_height),
            layout_cond);
    } else if (strcmp(name, "Insights") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_top_height),
            layout_cond);
    } else if (strcmp(name, "AI Assist") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + right_top_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_ai_height),
            layout_cond);
    } else if (strcmp(name, "Metadata") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + right_top_height + right_ai_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_mid_height),
            layout_cond);
    } else if (strcmp(name, "About") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + right_top_height + right_ai_height + right_mid_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_bottom_height),
            layout_cond);
    } else if (strcmp(name, "Status") == 0) {
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
    {
        int quality_mode = app->render_quality_mode;
        if (ImGui::Combo("GPU Quality Preset",
                         &quality_mode,
                         k_render_quality_labels,
                         3)) {
            apply_render_quality_preset(app, quality_mode);
        }
    }
    ImGui::TextWrapped("Balanced keeps the NVIDIA laptop responsive; Quality keeps the full viewport and uses finer CUDA ray steps.");
    ImGui::SliderFloat("Step Size",
                       &app->step_size,    0.0005f, 0.01f);
    ImGui::SliderFloat("Opacity Threshold",
                       &app->threshold,    0.1f,    1.0f);
    ImGui::SliderFloat("Skip Multiplier",
                       &app->skip_mult,    1.0f,    8.0f);

    {
        bool match_window = (app->render_match_window != 0);
        if (ImGui::Checkbox("Match Window Size", &match_window)) {
            app->render_match_window = match_window ? 1 : 0;
            sync_resolution_selection(app);
        }
    }

    {
        const char *current_label = app->render_match_window ? "Window" : "Custom";
        if (app->selected_resolution >= 0 &&
            app->selected_resolution < k_resolution_preset_count &&
            !app->render_match_window) {
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
                    app->render_match_window = 0;
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
        app->render_match_window = 0;
        sync_resolution_selection(app);
    }

    if (ImGui::InputInt("Height", &app->render_h)) {
        if (app->render_h < 64) app->render_h = 64;
        if (app->render_h > 4096) app->render_h = 4096;
        app->render_match_window = 0;
        sync_resolution_selection(app);
    }

    ImGui::Separator();
    ImGui::Text("Transfer Mapping");
    ImGui::SliderFloat("Center", &app->tf_center, 0.0f, 1.0f);
    ImGui::SliderFloat("Width", &app->tf_width, 0.02f, 1.0f);
    ImGui::SliderFloat("Opacity Scale", &app->tf_opacity_scale, 0.1f, 3.0f);
    if (ImGui::Button("Auto Enhance View")) {
        apply_auto_enhance_transfer(app, "Controls panel");
    }
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
            apply_auto_enhance_on_load_if_safe(app, "Loaded volume");
            free(app->label_mask);
            app->label_mask = NULL;
            app->label_mask_W = 0;
            app->label_mask_H = 0;
            app->label_mask_D = 0;
            reset_annotations(app);
            app->object_summary_ready = 0;
            snprintf(app->object_summary_status,
                     sizeof(app->object_summary_status),
                     "Click Analyze Loaded Volume to generate a local object summary.");
            app->quant_metrics_ready = 0;
            snprintf(app->quant_metrics_status,
                     sizeof(app->quant_metrics_status),
                     "Click Refresh Quant Metrics to summarize the active volume.");
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

static int apply_auto_enhance_transfer(StandaloneApp *app, const char *reason)
{
    float center = 0.0f;
    float width = 0.0f;
    float opacity_scale = 0.0f;

    if (!app || !app->histogram_ready) {
        if (app) {
            snprintf(app->ai_assist_status,
                     sizeof(app->ai_assist_status),
                     "Auto enhance needs a loaded volume histogram.");
        }
        return 0;
    }

    if (!voxelpilot_compute_auto_enhance_transfer(
            app->histogram,
            HISTOGRAM_BINS,
            &center,
            &width,
            &opacity_scale)) {
        snprintf(app->ai_assist_status,
                 sizeof(app->ai_assist_status),
                 "Auto enhance could not find a useful intensity window.");
        return 0;
    }

    app->tf_center = center;
    app->tf_width = width;
    app->tf_opacity_scale = opacity_scale;

    snprintf(app->ai_assist_status,
             sizeof(app->ai_assist_status),
             "%s auto-enhanced the GPU transfer: center %.3f, width %.3f, opacity %.2f.",
             reason && reason[0] ? reason : "Histogram",
             app->tf_center,
             app->tf_width,
             app->tf_opacity_scale);
    return 1;
}

static int apply_auto_enhance_on_load_if_safe(StandaloneApp *app, const char *reason)
{
    if (!app || !app->histogram_ready) {
        return 0;
    }

    if (!voxelpilot_should_auto_enhance_on_load(app->histogram, HISTOGRAM_BINS)) {
        snprintf(app->ai_assist_status,
                 sizeof(app->ai_assist_status),
                 "%s preserved the default transfer for a high-background CT-style volume.",
                 reason && reason[0] ? reason : "Volume load");
        return 0;
    }

    return apply_auto_enhance_transfer(app, reason);
}

static void apply_ai_prompt_to_app(StandaloneApp *app)
{
    VoxelPilotPromptResult result;

    if (!voxelpilot_parse_prompt_action(app->ai_prompt, &result)) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "Prompt not recognized. Try flexible intents like: make it brighter, show bone, hide below 0.3, quality mode, or reset view.");
        return;
    }

    if (result.set_quality) {
        apply_render_quality_preset(app, result.quality_mode);
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s", result.status);
        return;
    }

    if (result.reset_view) {
        voxelpilot_set_default_clip_bounds(app->clip_min, app->clip_max);
    }

    if (result.set_transfer) {
        app->tf_center = result.tf_center;
        app->tf_width = result.tf_width;
        app->tf_opacity_scale = result.tf_opacity_scale;
        if (result.min_intensity >= 0.60f) {
            app->tf_palette = 1;
        }
    }

    if (result.auto_enhance) {
        apply_auto_enhance_transfer(app, result.status);
        return;
    }

    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "%s", result.status);
}

static void update_quant_metrics(StandaloneApp *app)
{
    float visible_threshold;

    if (!app->histogram_ready) {
        app->quant_metrics_ready = 0;
        snprintf(app->quant_metrics_status,
                 sizeof(app->quant_metrics_status),
                 "Quantitative metrics need a loaded volume histogram.");
        return;
    }

    visible_threshold = fmaxf(0.0f, app->tf_center - app->tf_width * 0.5f);
    if (!voxelpilot_compute_quant_metrics_from_histogram(
            app->histogram,
            HISTOGRAM_BINS,
            visible_threshold,
            &app->quant_metrics)) {
        app->quant_metrics_ready = 0;
        snprintf(app->quant_metrics_status,
                 sizeof(app->quant_metrics_status),
                 "Could not compute quantitative metrics from the active histogram.");
        return;
    }

    app->quant_metrics_ready = 1;
    snprintf(app->quant_metrics_status,
             sizeof(app->quant_metrics_status),
             "%s", app->quant_metrics.summary);
}

static void update_object_summary(StandaloneApp *app)
{
    if (!app->histogram_ready ||
        !voxelpilot_summarize_object_context(
            app->histogram,
            HISTOGRAM_BINS,
            app->annotation_count,
            &app->object_summary)) {
        app->object_summary_ready = 0;
        snprintf(app->object_summary_status,
                 sizeof(app->object_summary_status),
                 "Object summary needs a loaded volume with a populated histogram.");
        return;
    }

    app->object_summary_ready = 1;
    snprintf(app->object_summary_status,
             sizeof(app->object_summary_status),
             "%s", app->object_summary.description);
}

static void update_streaming_estimate(StandaloneApp *app)
{
    VoxelPilotBrickCachePlan plan;

    if (!app->renderer.h_bricks || app->renderer.h_grid.numBricks < 1) {
        app->streaming_visible_bricks = 0;
        app->streaming_resident_candidates = 0;
        app->streaming_stream_now = 0;
        app->streaming_queue = 0;
        app->streaming_evictable = 0;
        snprintf(app->streaming_status, sizeof(app->streaming_status),
                 "Brick metadata is not available yet.");
        return;
    }

    if (!voxelpilot_estimate_visible_bricks(
            app->renderer.h_bricks,
            app->renderer.h_grid.numBricks,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            app->clip_min,
            app->clip_max,
            &app->streaming_visible_bricks,
            &app->streaming_resident_candidates)) {
        snprintf(app->streaming_status, sizeof(app->streaming_status),
                 "Could not estimate visible bricks.");
        return;
    }

    if (app->streaming_budget_bricks < 1) {
        app->streaming_budget_bricks = app->renderer.h_grid.numBricks;
    }

    if (!voxelpilot_plan_brick_cache(
            app->streaming_visible_bricks,
            app->streaming_resident_candidates,
            app->streaming_budget_bricks,
            app->renderer.h_grid.numBricks,
            &plan)) {
        snprintf(app->streaming_status, sizeof(app->streaming_status),
                 "Could not plan brick cache residency.");
        return;
    }

    app->streaming_stream_now = plan.stream_now;
    app->streaming_queue = plan.queued;
    app->streaming_evictable = plan.evictable;

    snprintf(app->streaming_status, sizeof(app->streaming_status),
             "%d visible, %d resident candidates, stream %d now, queue %d, evictable %d.",
             app->streaming_visible_bricks,
             app->streaming_resident_candidates,
             app->streaming_stream_now,
             app->streaming_queue,
             app->streaming_evictable);
}

static void write_html_escaped(FILE *fp, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (!fp || !text) {
        return;
    }

    while (*cursor) {
        if (*cursor == '&') {
            fputs("&amp;", fp);
        } else if (*cursor == '<') {
            fputs("&lt;", fp);
        } else if (*cursor == '>') {
            fputs("&gt;", fp);
        } else if (*cursor == '"') {
            fputs("&quot;", fp);
        } else {
            fputc(*cursor, fp);
        }
        ++cursor;
    }
}

static void export_insight_report(StandaloneApp *app)
{
    const char *patterns[] = { "*.html" };
    const char *selected;
    FILE *fp;
    size_t voxel_count;
    int i;

    if (!app->renderer.volume_data || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "Load a volume before exporting a report.");
        return;
    }

    selected = tinyfd_saveFileDialog(
        "Export VoxelPilot Insight Report",
        app->report_path[0] ? app->report_path : "voxelpilot_insight_report.html",
        1,
        patterns,
        "HTML Report (*.html)");

    if (!selected) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "Report export canceled.");
        return;
    }

    update_quant_metrics(app);
    if (!app->object_summary_ready) {
        update_object_summary(app);
    }
    update_streaming_estimate(app);

    fp = fopen(selected, "w");
    if (!fp) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "Could not write report file.");
        return;
    }

    voxel_count =
        (size_t)app->renderer.W *
        (size_t)app->renderer.H *
        (size_t)app->renderer.D;

    fputs("<!doctype html><html><head><meta charset=\"utf-8\">", fp);
    fputs("<title>VoxelPilot Insight Report</title>", fp);
    fputs("<style>body{font-family:Segoe UI,Arial,sans-serif;background:#101820;color:#eef;line-height:1.45;margin:32px;}section{border:1px solid #345;padding:16px;margin:16px 0;border-radius:10px;background:#16212b;}table{border-collapse:collapse;width:100%;}td,th{border-bottom:1px solid #345;padding:6px;text-align:left;}code{color:#8fe;}</style>", fp);
    fputs("</head><body><h1>VoxelPilot Insight Report</h1>", fp);

    fputs("<section><h2>Dataset</h2><table>", fp);
    fprintf(fp, "<tr><th>Dimensions</th><td>%d x %d x %d</td></tr>", app->renderer.W, app->renderer.H, app->renderer.D);
    fprintf(fp, "<tr><th>Voxel count</th><td>%zu</td></tr>", voxel_count);
    fprintf(fp, "<tr><th>Value range</th><td>%.4f to %.4f</td></tr>", app->hist_min_value, app->hist_max_value);
    fprintf(fp, "<tr><th>Voxel spacing</th><td>%.3f / %.3f / %.3f</td></tr>", app->voxel_spacing[0], app->voxel_spacing[1], app->voxel_spacing[2]);
    fputs("<tr><th>Source path</th><td><code>", fp);
    write_html_escaped(fp, app->upload_path[0] ? app->upload_path : "startup/synthetic volume");
    fputs("</code></td></tr></table></section>", fp);

    fputs("<section><h2>Quantitative Metrics</h2><table>", fp);
    fprintf(fp, "<tr><th>Visible ratio</th><td>%.1f%%</td></tr>", app->quant_metrics.visible_ratio * 100.0f);
    fprintf(fp, "<tr><th>Median / P95 / Mean</th><td>%.3f / %.3f / %.3f</td></tr>", app->quant_metrics.percentile_50, app->quant_metrics.percentile_95, app->quant_metrics.mean_intensity);
    fprintf(fp, "<tr><th>Low / Mid / High density</th><td>%.1f%% / %.1f%% / %.1f%%</td></tr>", app->quant_metrics.low_density_ratio * 100.0f, app->quant_metrics.mid_density_ratio * 100.0f, app->quant_metrics.high_density_ratio * 100.0f);
    fputs("<tr><th>Summary</th><td>", fp);
    write_html_escaped(fp, app->quant_metrics_status);
    fputs("</td></tr></table></section>", fp);

    fputs("<section><h2>AI/Object Review</h2><table>", fp);
    fputs("<tr><th>AI type</th><td>Local explainable heuristic AI: rule prompts, histogram/material heuristics, ray picking, flood-fill segmentation, and human-confirmed annotations. No cloud model or trained neural network is used in this demo slice.</td></tr>", fp);
    fputs("<tr><th>Primary material</th><td>", fp);
    write_html_escaped(fp, app->object_summary.primary_material);
    fputs("</td></tr><tr><th>Object summary</th><td>", fp);
    write_html_escaped(fp, app->object_summary_status);
    fputs("</td></tr></table></section>", fp);

    fputs("<section><h2>Measurements</h2><table>", fp);
    fprintf(fp, "<tr><th>Point A</th><td>%.3f, %.3f, %.3f</td></tr>", app->measure_a[0], app->measure_a[1], app->measure_a[2]);
    fprintf(fp, "<tr><th>Point B</th><td>%.3f, %.3f, %.3f</td></tr>", app->measure_b[0], app->measure_b[1], app->measure_b[2]);
    fprintf(fp, "<tr><th>Distance</th><td>%.3f voxels / %.3f world units</td></tr>", measurement_distance(app), measurement_distance_world(app));
    fputs("</table></section>", fp);

    fputs("<section><h2>Annotations</h2><table><tr><th>ID</th><th>Name</th><th>Voxels</th><th>Intensity Range</th></tr>", fp);
    for (i = 0; i < app->annotation_count; ++i) {
        AnnotationRegion *region = &app->annotations[i];
        if (!region->active) continue;
        fprintf(fp, "<tr><td>%u</td><td>", (unsigned int)region->label_id);
        write_html_escaped(fp, region->name);
        fprintf(fp, "</td><td>%zu</td><td>%.3f - %.3f</td></tr>", region->voxel_count, region->min_intensity, region->max_intensity);
    }
    if (app->annotation_count == 0) {
        fputs("<tr><td colspan=\"4\">No annotations yet.</td></tr>", fp);
    }
    fputs("</table></section>", fp);

    fputs("<section><h2>Performance / Streaming</h2><table>", fp);
    fprintf(fp, "<tr><th>Render time</th><td>%.2f ms</td></tr>", app->last_render_ms);
    fprintf(fp, "<tr><th>FPS</th><td>%.1f</td></tr>", app->gui_fps);
    fprintf(fp, "<tr><th>Brick cache plan</th><td>%d visible, %d candidates, stream %d now, queue %d, evictable %d</td></tr>", app->streaming_visible_bricks, app->streaming_resident_candidates, app->streaming_stream_now, app->streaming_queue, app->streaming_evictable);
    fputs("</table></section>", fp);

    fputs("</body></html>\n", fp);
    fclose(fp);

    strncpy(app->report_path, selected, sizeof(app->report_path) - 1);
    app->report_path[sizeof(app->report_path) - 1] = '\0';
    snprintf(app->report_status, sizeof(app->report_status),
             "Exported insight report: %s", selected);
}

static void draw_ai_assist_panel(StandaloneApp *app)
{
    int has_volume =
        app->renderer.volume_data &&
        app->renderer.W > 0 &&
        app->renderer.H > 0 &&
        app->renderer.D > 0;
    float suggested_center = 0.0f;
    float suggested_width = 0.0f;
    float suggested_opacity = 0.0f;
    int has_transfer_suggestion = 0;

    set_default_window_layout(app, "AI Assist");
    ImGui::Begin("AI Assist");

    ImGui::Text("AI Review Workbench");
    ImGui::Separator();
    ImGui::TextWrapped("Mode: flexible local intent parser, label masks, hover explanations, GPU transfer tuning, and brick-streaming telemetry");
    ImGui::TextWrapped(
        "AI concept: local explainable heuristic AI. It parses open-ended visibility/material/quality intents, derives transfer windows from the histogram, and lets the CUDA renderer apply the result without cloud inference or a heavy model pass.");

    if (has_volume && app->histogram_ready) {
        has_transfer_suggestion =
            voxelpilot_compute_auto_enhance_transfer(
                app->histogram,
                HISTOGRAM_BINS,
                &suggested_center,
                &suggested_width,
                &suggested_opacity);
    }

    if (!has_volume) {
        ImGui::TextWrapped("Load a volume to enable AI-assisted review suggestions.");
    } else {
        ImGui::Text("Dataset: %d x %d x %d",
                    app->renderer.W,
                    app->renderer.H,
                    app->renderer.D);
        ImGui::Text("Intensity range: %.4f to %.4f",
                    app->hist_min_value,
                    app->hist_max_value);
    }

    ImGui::Separator();
    ImGui::Text("Auto Object Identification");
    ImGui::TextWrapped("Local AI heuristic: estimates dominant material classes from the active volume histogram.");
    if (ImGui::Button("Analyze Loaded Volume")) {
        update_object_summary(app);
    }
    if (app->object_summary_ready) {
        ImGui::Text("Primary: %s", app->object_summary.primary_material);
        ImGui::Text("Confidence: %.1f%%", app->object_summary.confidence * 100.0f);
        ImGui::Text("Low/Mid/High: %.1f%% / %.1f%% / %.1f%%",
                    app->object_summary.low_density_ratio * 100.0f,
                    app->object_summary.mid_density_ratio * 100.0f,
                    app->object_summary.high_density_ratio * 100.0f);
    }
    ImGui::TextWrapped("%s", app->object_summary_status);

    ImGui::Separator();
    ImGui::Text("Quantitative Insight");
    ImGui::TextWrapped("Lightweight metrics are derived from the existing histogram, so this does not add a heavy render or model pass.");
    if (ImGui::Button("Refresh Quant Metrics")) {
        update_quant_metrics(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Insight Report")) {
        export_insight_report(app);
    }
    if (app->quant_metrics_ready) {
        ImGui::Text("Visible: %.1f%% | Median: %.3f | P95: %.3f",
                    app->quant_metrics.visible_ratio * 100.0f,
                    app->quant_metrics.percentile_50,
                    app->quant_metrics.percentile_95);
        ImGui::Text("Low/Mid/High: %.1f%% / %.1f%% / %.1f%%",
                    app->quant_metrics.low_density_ratio * 100.0f,
                    app->quant_metrics.mid_density_ratio * 100.0f,
                    app->quant_metrics.high_density_ratio * 100.0f);
    }
    ImGui::TextWrapped("Metrics: %s", app->quant_metrics_status);
    ImGui::TextWrapped("Report: %s", app->report_status);

    ImGui::Separator();
    ImGui::Text("Prompt-Based Segmentation");
    ImGui::InputText("Prompt", app->ai_prompt, sizeof(app->ai_prompt));
    if (ImGui::Button("Run Prompt")) {
        apply_ai_prompt_to_app(app);
    }
    ImGui::TextWrapped("Examples are guidance, not a fixed list: make it brighter | show bone | hide below 0.3 | switch to quality GPU rendering | reset view");

    if (ImGui::Button("Auto Enhance View")) {
        apply_auto_enhance_transfer(app, "AI Assist");
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview Auto Window")) {
        if (has_transfer_suggestion) {
            app->tf_center = suggested_center;
            app->tf_width = suggested_width;
            app->tf_opacity_scale = suggested_opacity;
            snprintf(app->ai_assist_status,
                     sizeof(app->ai_assist_status),
                     "Previewed auto window: center %.3f, width %.3f, opacity %.2f.",
                     app->tf_center,
                     app->tf_width,
                     app->tf_opacity_scale);
        } else {
            snprintf(app->ai_assist_status,
                     sizeof(app->ai_assist_status),
                     "Auto window needs a populated histogram.");
        }
    }

    if (ImGui::Button("Scout Focus Slice")) {
        float focus_slice = 0.0f;
        float focus_score = 0.0f;
        if (voxelpilot_find_high_variance_axial_slice(
                app->renderer.volume_data,
                app->renderer.W,
                app->renderer.H,
                app->renderer.D,
                &focus_slice,
                &focus_score)) {
            app->slice_z = focus_slice;
            app->ai_focus_slice = focus_slice;
            app->ai_focus_score = focus_score;
            snprintf(app->ai_assist_status,
                     sizeof(app->ai_assist_status),
                     "Scout moved Axial Z to %.3f with variance score %.4f.",
                     app->ai_focus_slice,
                     app->ai_focus_score);
        } else {
            snprintf(app->ai_assist_status,
                     sizeof(app->ai_assist_status),
                     "Focus scout needs a loaded 3D volume.");
        }
    }

    if (has_transfer_suggestion) {
        ImGui::Text("Suggested center: %.3f", suggested_center);
        ImGui::Text("Suggested width: %.3f", suggested_width);
    }
    if (app->ai_focus_score > 0.0f) {
        ImGui::Text("Focus Axial Z: %.3f", app->ai_focus_slice);
        ImGui::Text("Focus score: %.4f", app->ai_focus_score);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", app->ai_assist_status);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Multi-Material Labels");
    {
        bool annotation_mode = (app->annotation_mode != 0);
        if (ImGui::Checkbox("Slice Click Labels", &annotation_mode)) {
            app->annotation_mode = annotation_mode ? 1 : 0;
        }
    }
    {
        bool overlay_visible = (app->label_overlay_visible != 0);
        if (ImGui::Checkbox("Show Labels in 3D Render", &overlay_visible)) {
            app->label_overlay_visible = overlay_visible ? 1 : 0;
        }
    }
    ImGui::SliderFloat("3D Label Opacity", &app->label_overlay_alpha, 0.10f, 0.95f);
    ImGui::InputText("Label Name", app->annotation_name, sizeof(app->annotation_name));
    ImGui::SliderFloat("Label Tolerance", &app->annotation_tolerance, 0.005f, 0.250f);
    ImGui::ColorEdit3("Label Color", app->annotation_color);
    if (ImGui::Button("Label Current Slice Point")) {
        float point[3] = { app->slice_x, app->slice_y, app->slice_z };
        create_annotation_region_at_point(app, point);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Mask")) {
        export_annotation_mask(app);
    }
    ImGui::TextWrapped("Annotation status: %s", app->annotation_status);
    if (app->annotation_count > 0) {
        int i;
        for (i = 0; i < app->annotation_count; ++i) {
            AnnotationRegion *region = &app->annotations[i];
            char row_label[128];
            if (!region->active) continue;
            snprintf(row_label, sizeof(row_label),
                     "%u: %s (%zu voxels)",
                     (unsigned int)region->label_id,
                     region->name,
                     region->voxel_count);
            if (ImGui::Selectable(row_label, app->selected_annotation == i)) {
                app->selected_annotation = i;
                snprintf(app->annotation_name, sizeof(app->annotation_name),
                         "%s", region->name);
                app->annotation_tolerance = region->tolerance;
                app->annotation_color[0] = region->color[0];
                app->annotation_color[1] = region->color[1];
                app->annotation_color[2] = region->color[2];
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Object Hover + Streaming");
    ImGui::TextWrapped("Hover over slice previews or the main 3D render for density or label explanations.");
    ImGui::TextWrapped("3D hover: %s", app->main_hover_description);
    update_streaming_estimate(app);
    if (app->renderer.h_grid.numBricks > 0) {
        ImGui::SliderInt("Brick Budget",
                         &app->streaming_budget_bricks,
                         1,
                         app->renderer.h_grid.numBricks);
    }
    ImGui::TextWrapped("Streaming: %s", app->streaming_status);
    ImGui::Text("Cache Plan: stream %d | queue %d | evictable %d",
                app->streaming_stream_now,
                app->streaming_queue,
                app->streaming_evictable);

    ImGui::Spacing();
    ImGui::Text("Review Summary");
    ImGui::Separator();
    if (has_volume) {
        ImGui::TextWrapped(
            "Current review combines 3D rendering, prompt-guided transfer mapping, labels, hover explanations, and measurement context.");
        ImGui::TextWrapped(
            "Research track: true model-backed 3D segmentation and disk-backed brick paging can build on this scaffold.");
    } else {
        ImGui::TextWrapped("No active dataset summary yet.");
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
    ImGui::BulletText("AI Assist panel with prompt rules, label masks, hover explanations, and brick telemetry.");
    ImGui::BulletText("Mouse orbit and zoom, preset viewpoints, measurement tools, and PNG snapshots.");
    ImGui::BulletText("Workspace session save/load plus a packaged Windows launcher flow.");

    ImGui::Spacing();
    ImGui::Text("Next Version");
    ImGui::Separator();
    ImGui::BulletText("Model-backed anatomical identification and uncertainty overlays.");
    ImGui::BulletText("Disk-backed brick paging for true out-of-core volume streaming.");
    ImGui::BulletText("3D label-mask overlays and richer correction-log export.");

    ImGui::End();
}

static void update_main_render_hover_pick(StandaloneApp *app)
{
    ImGuiIO *io = &ImGui::GetIO();
    int fb_w = 0;
    int fb_h = 0;
    double mouse_x = 0.0;
    double mouse_y = 0.0;
    float ndc_x;
    float ndc_y;
    float aspect;
    float tan_fov;
    float pick_threshold;
    float ray_origin[3];
    float ray_direction[3];
    float3 view_dir;
    float3 up;
    float3 right;
    float3 view_up;
    float3 ray;
    unsigned char label_id = 0;
    AnnotationRegion *region = NULL;

    app->main_hover_active = 0;
    app->main_hover_label_id = 0;

    if (!app->renderer.volume_data ||
        app->renderer.W < 1 ||
        app->renderer.H < 1 ||
        app->renderer.D < 1 ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
        ImGui::IsAnyItemActive() ||
        glfwGetMouseButton(app->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        return;
    }

    glfwGetFramebufferSize(app->window, &fb_w, &fb_h);
    glfwGetCursorPos(app->window, &mouse_x, &mouse_y);
    if (fb_w <= 0 || fb_h <= 0 ||
        mouse_x < 0.0 || mouse_y < 0.0 ||
        mouse_x >= (double)fb_w || mouse_y >= (double)fb_h) {
        return;
    }

    ndc_x = ((float)mouse_x / (float)fb_w) * 2.0f - 1.0f;
    ndc_y = 1.0f - ((float)mouse_y / (float)fb_h) * 2.0f;
    aspect = (float)fb_w / (float)fb_h;
    tan_fov = tanf(0.5f * app->fov_y_deg * 3.14159265f / 180.0f);

    view_dir = f3_normalize(f3_make(
        app->cam_dir[0],
        app->cam_dir[1],
        app->cam_dir[2]));
    up = f3_normalize(f3_make(
        app->cam_up[0],
        app->cam_up[1],
        app->cam_up[2]));
    right = f3_normalize(f3_cross(view_dir, up));
    view_up = f3_cross(right, view_dir);
    ray = f3_normalize(
        f3_add(
            view_dir,
            f3_add(
                f3_scale(right, ndc_x * aspect * tan_fov),
                f3_scale(view_up, ndc_y * tan_fov))));

    ray_origin[0] = app->cam_pos[0];
    ray_origin[1] = app->cam_pos[1];
    ray_origin[2] = app->cam_pos[2];
    ray_direction[0] = ray.x;
    ray_direction[1] = ray.y;
    ray_direction[2] = ray.z;
    pick_threshold = fmaxf(0.03f, app->tf_center - app->tf_width * 0.5f);

    if (!voxelpilot_pick_volume_along_ray(
            app->renderer.volume_data,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            ray_origin,
            ray_direction,
            fmaxf(app->step_size * 2.0f, 0.003f),
            pick_threshold,
            app->clip_min,
            app->clip_max,
            &app->main_hover_pick)) {
        snprintf(app->main_hover_description,
                 sizeof(app->main_hover_description),
                 "3D hover: no visible material under cursor.");
        return;
    }

    if (app->label_mask &&
        app->label_mask_W == app->renderer.W &&
        app->label_mask_H == app->renderer.H &&
        app->label_mask_D == app->renderer.D) {
        label_id = app->label_mask[app->main_hover_pick.index];
        region = find_annotation_by_label(app, label_id);
    }

    app->main_hover_active = 1;
    app->main_hover_label_id = label_id;
    voxelpilot_describe_voxel_context(
        app->main_hover_pick.intensity,
        label_id,
        region ? region->name : "",
        app->main_hover_description,
        sizeof(app->main_hover_description));
}

static void draw_main_render_hover_tooltip(StandaloneApp *app)
{
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    AnnotationRegion *region = NULL;

    if (!app->main_hover_active) {
        return;
    }

    region = find_annotation_by_label(app, app->main_hover_label_id);
    ImGui::SetNextWindowPos(ImVec2(mouse.x + 18.0f, mouse.y + 18.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("3D Hover Pick", NULL, flags);
    ImGui::Text("3D Hover Pick");
    ImGui::Separator();
    ImGui::TextWrapped("%s", app->main_hover_description);
    ImGui::Text("Voxel: %d, %d, %d",
                app->main_hover_pick.x,
                app->main_hover_pick.y,
                app->main_hover_pick.z);
    ImGui::Text("Point: %.3f, %.3f, %.3f",
                app->main_hover_pick.point[0],
                app->main_hover_pick.point[1],
                app->main_hover_pick.point[2]);
    if (region) {
        ImGui::Text("Region voxels: %zu", region->voxel_count);
        ImGui::ColorButton("##main_hover_label_color",
                           ImVec4(region->color[0],
                                  region->color[1],
                                  region->color[2],
                                  1.0f),
                           ImGuiColorEditFlags_NoTooltip,
                           ImVec2(18.0f, 18.0f));
    }
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
    draw_ai_assist_panel(app);
    draw_metadata_panel(app);
    draw_about_panel(app);
    draw_walkthrough_overlay(app);
    draw_status_bar(app);
    update_main_render_hover_pick(app);
    draw_main_render_hover_tooltip(app);
    app->layout_refit_next_frame = 0;
}

/* ============================================================
   Main Entry Point
   ============================================================ */
int main(int argc, char **argv)
{
    StandaloneApp app;
    const char   *volume_path = NULL;
    const char   *snapshot_path = NULL;
    int           startup_width = 256;
    int           startup_height = 256;
    int           startup_depth = 256;
    int           startup_data_type = VOLUME_DATA_FLOAT32;
    int           startup_width_set = 0;
    int           startup_height_set = 0;
    int           startup_depth_set = 0;
    int           startup_type_set = 0;
    int           cli_tf_center_set = 0;
    int           cli_tf_width_set = 0;
    int           cli_tf_opacity_set = 0;
    float         cli_tf_center = 0.45f;
    float         cli_tf_width = 0.35f;
    float         cli_tf_opacity = 1.0f;
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
        } else if (strcmp(argv[i], "--snapshot") == 0 &&
                   i+1 < argc) {
            snapshot_path = argv[++i];
        } else if (strcmp(argv[i], "--tf-center") == 0 &&
                   i+1 < argc) {
            if (parse_cli_float(argv[++i], 0.0f, 1.0f, &cli_tf_center) != 0) {
                fprintf(stderr, "Invalid value for --tf-center: %s\n", argv[i]);
                return -1;
            }
            cli_tf_center_set = 1;
        } else if (strcmp(argv[i], "--tf-width") == 0 &&
                   i+1 < argc) {
            if (parse_cli_float(argv[++i], 0.02f, 1.0f, &cli_tf_width) != 0) {
                fprintf(stderr, "Invalid value for --tf-width: %s\n", argv[i]);
                return -1;
            }
            cli_tf_width_set = 1;
        } else if (strcmp(argv[i], "--tf-opacity") == 0 &&
                   i+1 < argc) {
            if (parse_cli_float(argv[++i], 0.1f, 3.0f, &cli_tf_opacity) != 0) {
                fprintf(stderr, "Invalid value for --tf-opacity: %s\n", argv[i]);
                return -1;
            }
            cli_tf_opacity_set = 1;
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
                    strcmp(argv[i], "--volume") == 0 ||
                    strcmp(argv[i], "--snapshot") == 0 ||
                    strcmp(argv[i], "--tf-center") == 0 ||
                    strcmp(argv[i], "--tf-width") == 0 ||
                    strcmp(argv[i], "--tf-opacity") == 0) &&
                   i+1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return -1;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr,
                    "Usage: %s [--volume file.raw] [--dim N | --width W --height H --depth D] [--type uint8|uint16|float32] [--snapshot path.png] [--tf-center C --tf-width W --tf-opacity O]\n",
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
    app.render_match_window = 1;
    app.layout_refit_next_frame = 1;
    app.window_was_maximized = 0;
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
    apply_render_quality_preset(&app, VOXELPILOT_RENDER_QUALITY_BALANCED);
    app.paused     = 0;
    app.tf_center = 0.45f;
    app.tf_width = 0.35f;
    app.tf_opacity_scale = 1.0f;
    if (cli_tf_center_set) app.tf_center = cli_tf_center;
    if (cli_tf_width_set) app.tf_width = cli_tf_width;
    if (cli_tf_opacity_set) app.tf_opacity_scale = cli_tf_opacity;
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
    app.ai_focus_slice = 0.5f;
    app.ai_focus_score = 0.0f;
    strcpy(app.ai_prompt, "show high-density shell");
    strcpy(app.ai_assist_status,
           "AI Assist ready. Load a volume to run local review suggestions.");
    app.annotation_mode = 0;
    strcpy(app.annotation_name, "Region");
    app.annotation_tolerance = 0.035f;
    app.annotation_color[0] = 0.95f;
    app.annotation_color[1] = 0.28f;
    app.annotation_color[2] = 0.16f;
    app.label_overlay_visible = 1;
    app.label_overlay_alpha = 0.55f;
    app.selected_annotation = -1;
    strcpy(app.annotation_status, "No annotation regions yet.");
    strcpy(app.hover_description, "Hover over a slice preview for context.");
    app.main_hover_active = 0;
    app.main_hover_label_id = 0;
    memset(&app.main_hover_pick, 0, sizeof(app.main_hover_pick));
    strcpy(app.main_hover_description,
           "Hover over the 3D render for a direct pick explanation.");
    app.object_summary_ready = 0;
    strcpy(app.object_summary_status,
           "Click Analyze Loaded Volume to generate a local object summary.");
    app.quant_metrics_ready = 0;
    strcpy(app.quant_metrics_status,
           "Click Refresh Quant Metrics to summarize the active volume.");
    strcpy(app.report_status, "No report exported yet.");
    strcpy(app.report_path, "voxelpilot_insight_report.html");
    app.streaming_budget_bricks = 0;
    app.streaming_stream_now = 0;
    app.streaming_queue = 0;
    app.streaming_evictable = 0;
    strcpy(app.streaming_status, "Brick streaming telemetry pending.");
    sync_resolution_selection(&app);
    set_camera_preset(&app,
        -0.35f, 0.95f, -0.35f,
        0.7f, -0.35f, 0.7f,
        0.0f, 1.0f, 0.0f);

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
        apply_auto_enhance_on_load_if_safe(&app, "Startup volume");
        free(app.label_mask);
        app.label_mask = NULL;
        app.label_mask_W = 0;
        app.label_mask_H = 0;
        app.label_mask_D = 0;
        reset_annotations(&app);
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

    if (snapshot_path) {
        if (app.render_w < 1024) app.render_w = 1024;
        if (app.render_h < 1024) app.render_h = 1024;
        set_camera_preset(&app,
            -0.35f, 0.95f, -0.35f,
            0.7f, -0.35f, 0.7f,
            0.0f, 1.0f, 0.0f);
        apply_auto_enhance_on_load_if_safe(&app, "Snapshot");
        render_frame_gpu_snapshot(&app);

        if (!app.renderer.h_out) {
            fprintf(stderr, "Snapshot failed: no rendered frame available.\n");
            renderer_state_cleanup(&app.renderer);
            cudaDeviceReset();
            return -1;
        }

        if (write_png_file(
                (const unsigned char *)app.renderer.h_out,
                app.render_w,
                app.render_h,
                snapshot_path) != 0) {
            fprintf(stderr, "Snapshot failed: could not write %s\n", snapshot_path);
            renderer_state_cleanup(&app.renderer);
            cudaDeviceReset();
            return -1;
        }

        printf("Snapshot saved: %s (%dx%d, %.2f ms)\n",
               snapshot_path,
               app.render_w,
               app.render_h,
               app.last_render_ms);
        renderer_state_cleanup(&app.renderer);
        cudaDeviceReset();
        return 0;
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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    app.window = glfwCreateWindow(
        app.win_w, app.win_h,
        BRAND_TITLE " | " BRAND_SUBTITLE,
        NULL, NULL);
    if (!app.window) {
        fprintf(stderr, "glfwCreateWindow() failed\n");
        glfwTerminate();
        return -1;
    }
    glfwMaximizeWindow(app.window);
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
        int fb_w;
        int fb_h;
        int window_is_maximized;

        glfwPollEvents();
        window_is_maximized = glfwGetWindowAttrib(app.window, GLFW_MAXIMIZED);
        if (voxelpilot_should_refit_layout_after_maximize(
                app.window_was_maximized,
                window_is_maximized)) {
            app.layout_refit_next_frame = 1;
            app.dock_layout_ready = 0;
        }
        app.window_was_maximized = window_is_maximized;

        glfwGetFramebufferSize(app.window, &fb_w, &fb_h);
        sync_render_target_to_framebuffer(&app, fb_w, fb_h);

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

    free(app.slice_axial_buf);
    free(app.slice_coronal_buf);
    free(app.slice_sagittal_buf);
    free(app.label_mask);

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
