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
#include "common/default_state.h"
#include "common/histogram_profile.h"
#include "common/render_command_builder.h"
#include "common/ai_review_bundle.h"
#include "common/insight_report.h"
#include "common/ui_state_helpers.h"
#include "common/ai_features.h"
#include "common/volume_import.h"
#include "common/cli_options.h"
#include "common/json_utils.h"
#include "common/semantic_discovery.h"
#include "common/performance_governor.h"

#include "standalone_app.h"

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

enum {
    VOXELPILOT_RIBBON_HOME = 0,
    VOXELPILOT_RIBBON_VIEW = 1,
    VOXELPILOT_RIBBON_COUNT = 2
};

static const char *k_ribbon_tab_labels[] = {
    "Home",
    "View"
};

static void apply_render_quality_preset(StandaloneApp *app, int quality_mode);
static void clamp_cinematic_settings(StandaloneApp *app);
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
static void reset_semantic_discovery(StandaloneApp *app);
static VoxelPilotSemanticRegion *find_semantic_region_by_label(
    StandaloneApp *app,
    unsigned char  label_id);
static const char *semantic_relationship_note(
    StandaloneApp *app,
    unsigned char  label_id);
static void run_adaptive_semantic_discovery(StandaloneApp *app);
static int select_guided_review_region(StandaloneApp *app, int direction);
static void start_guided_review(StandaloneApp *app);
static void draw_guided_review_overlay(StandaloneApp *app);
static void update_render_performance_average(StandaloneApp *app);
static void reset_default_layout(StandaloneApp *app);
static void refresh_adaptive_quality_for_frame(StandaloneApp *app);
static void refresh_performance_governor(StandaloneApp *app);
static int performance_allows_optional_action(
    StandaloneApp *app,
    const char    *action_label,
    int            requires_asd,
    int            requires_mask_export);
static void voxelpilot_same_line_if_fits(const char *next_label);
static ImVec4 voxelpilot_pressure_color(int status);
static void refresh_semantic_annotation_overlays(StandaloneApp *app);
static int save_semantic_memory(StandaloneApp *app, const char *path);
static int load_semantic_memory(StandaloneApp *app, const char *path);
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
    app->tex_uniform_loc = glGetUniformLocation(app->prog, "tex");

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
    VoxelPilotHistogramProfile profile;

    if (!app) {
        return;
    }

    if (!voxelpilot_compute_histogram_profile(
            app->renderer.volume_data,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            app->histogram,
            HISTOGRAM_BINS,
            &profile)) {
        app->histogram_ready = 0;
        app->hist_min_value = 0.0f;
        app->hist_max_value = 0.0f;
        app->hist_peak_count = 0.0f;
        return;
    }

    app->hist_min_value = profile.min_value;
    app->hist_max_value = profile.max_value;
    app->hist_peak_count = profile.peak_count;
    app->histogram_ready = profile.ready;
}
static const char *dataset_health_level_label(int level)
{
    if (level == VOXELPILOT_DATASET_HEALTH_ATTENTION) return "Attention";
    if (level == VOXELPILOT_DATASET_HEALTH_WATCH) return "Watch";
    return "Healthy";
}

static void update_dataset_health(StandaloneApp *app)
{
    size_t estimated_bytes;

    if (!app || !app->histogram_ready || app->renderer.W < 1 ||
        app->renderer.H < 1 || app->renderer.D < 1) {
        if (app) {
            app->dataset_health_ready = 0;
            snprintf(app->dataset_health_status,
                     sizeof(app->dataset_health_status),
                     "Dataset Health needs a loaded volume histogram.");
        }
        return;
    }

    estimated_bytes = (size_t)app->renderer.W *
                      (size_t)app->renderer.H *
                      (size_t)app->renderer.D * sizeof(float);
    if (!voxelpilot_compute_dataset_health(
            app->histogram,
            HISTOGRAM_BINS,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            app->hist_min_value,
            app->hist_max_value,
            estimated_bytes,
            &app->dataset_health)) {
        app->dataset_health_ready = 0;
        snprintf(app->dataset_health_status,
                 sizeof(app->dataset_health_status),
                 "Dataset Health could not summarize this volume.");
        return;
    }

    app->dataset_health_ready = 1;
    snprintf(app->dataset_health_status,
             sizeof(app->dataset_health_status),
             "%s (%.0f/100): %s",
             dataset_health_level_label(app->dataset_health.level),
             app->dataset_health.score,
             app->dataset_health.summary);
}

float measurement_distance(const StandaloneApp *app)
{
    float dx = (app->measure_b[0] - app->measure_a[0]) * (float)app->renderer.W;
    float dy = (app->measure_b[1] - app->measure_a[1]) * (float)app->renderer.H;
    float dz = (app->measure_b[2] - app->measure_a[2]) * (float)app->renderer.D;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

float measurement_distance_world(const StandaloneApp *app)
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

static void clamp_cinematic_settings(StandaloneApp *app)
{
    VoxelPilotCinematicParams cinematic;

    if (!app) {
        return;
    }

    cinematic.enabled = app->cinematic_enabled;
    cinematic.rim_strength = app->cinematic_rim_strength;
    cinematic.glow_strength = app->cinematic_glow_strength;
    cinematic.contrast = app->cinematic_contrast;
    voxelpilot_cinematic_clamp(&cinematic);

    app->cinematic_enabled = cinematic.enabled;
    app->cinematic_rim_strength = cinematic.rim_strength;
    app->cinematic_glow_strength = cinematic.glow_strength;
    app->cinematic_contrast = cinematic.contrast;
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

    app->adaptive_quality_multiplier = 1.0f;
    app->adaptive_quality_active = 0;
    app->adaptive_step_size = app->step_size;
    app->adaptive_skip_mult = app->skip_mult;
    snprintf(app->adaptive_quality_status,
             sizeof(app->adaptive_quality_status),
             app->adaptive_quality_enabled
                 ? "Adaptive quality ready: using the selected GPU preset."
                 : "Adaptive quality disabled: using the selected GPU preset.");
}

static void apply_default_workspace_state(StandaloneApp *app, int include_window_defaults)
{
    VoxelPilotDefaultState defaults;

    if (!app) {
        return;
    }

    voxelpilot_default_state_init(&defaults);

    if (include_window_defaults) {
        app->win_w = defaults.window_width;
        app->win_h = defaults.window_height;
        app->layout_refit_next_frame = defaults.layout_refit_next_frame;
        app->window_was_maximized = defaults.window_was_maximized;
    }

    app->render_w = defaults.render_width;
    app->render_h = defaults.render_height;
    app->render_match_window = defaults.render_match_window;
    app->selected_resolution = defaults.selected_resolution;
    app->ribbon_expanded = defaults.ribbon_expanded;
    app->ribbon_active_tab = defaults.ribbon_active_tab;
    app->controls_visible = 1;
    app->insights_visible = 1;
    app->ai_visible = 1;
    app->metadata_visible = 1;
    app->adaptive_quality_enabled = defaults.adaptive_quality_enabled;
    apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_BALANCED);

    app->paused = defaults.paused;
    app->fov_y_deg = defaults.fov_y_degrees;
    memcpy(app->light_pos, defaults.light_pos, sizeof(app->light_pos));
    app->cinematic_enabled = defaults.cinematic_enabled;
    app->cinematic_rim_strength = defaults.cinematic_rim_strength;
    app->cinematic_glow_strength = defaults.cinematic_glow_strength;
    app->cinematic_contrast = defaults.cinematic_contrast;
    app->tf_center = defaults.tf_center;
    app->tf_width = defaults.tf_width;
    app->tf_opacity_scale = defaults.tf_opacity_scale;
    app->tf_palette = defaults.tf_palette;
    app->tf_invert = defaults.tf_invert;
    memcpy(app->clip_min, defaults.clip_min, sizeof(app->clip_min));
    memcpy(app->clip_max, defaults.clip_max, sizeof(app->clip_max));
    app->slice_x = defaults.slice[0];
    app->slice_y = defaults.slice[1];
    app->slice_z = defaults.slice[2];
    memcpy(app->measure_a, defaults.measure_a, sizeof(app->measure_a));
    memcpy(app->measure_b, defaults.measure_b, sizeof(app->measure_b));
    app->measurement_visible = defaults.measurement_visible;
    app->measurement_target = defaults.measurement_target;
    app->annotation_mode = defaults.annotation_mode;
    snprintf(app->annotation_name, sizeof(app->annotation_name), "Region");
    app->annotation_tolerance = defaults.annotation_tolerance;
    memcpy(app->annotation_color, defaults.annotation_color, sizeof(app->annotation_color));
    app->label_overlay_visible = defaults.label_overlay_visible;
    app->label_overlay_alpha = defaults.label_overlay_alpha;
    app->label_selected_emphasis_enabled = defaults.label_selected_emphasis_enabled;
    app->label_selected_emphasis_strength = defaults.label_selected_emphasis_strength;
    app->semantic_max_regions = defaults.semantic_max_regions;
    app->semantic_min_region_voxels = defaults.semantic_min_region_voxels;
    app->semantic_sensitivity = defaults.semantic_sensitivity;
    app->semantic_cinematic_emphasis = defaults.semantic_cinematic_emphasis;
    app->semantic_callouts_enabled = defaults.semantic_callouts_enabled;
    app->semantic_callout_top_regions = defaults.semantic_callout_top_regions;
    app->semantic_callout_selected_only = defaults.semantic_callout_selected_only;
    app->semantic_callout_opacity = defaults.semantic_callout_opacity;

    if (include_window_defaults) {
        reset_semantic_discovery(app);
        app->annotation_count = 0;
        app->selected_annotation = -1;
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "No annotation regions yet.");
    } else {
        reset_annotations(app);
    }

    app->guided_review_visible = 0;
    memset(&app->guided_review_step, 0, sizeof(app->guided_review_step));
    snprintf(app->guided_review_status, sizeof(app->guided_review_status),
             "Run Semantic Demo or Discover Regions, then start Guided Review.");
    app->object_summary_ready = 0;
    snprintf(app->object_summary_status,
             sizeof(app->object_summary_status),
             "Click Analyze Loaded Volume to generate a local object summary.");
    app->quant_metrics_ready = 0;
    snprintf(app->quant_metrics_status,
             sizeof(app->quant_metrics_status),
             "Click Refresh Quant Metrics to summarize the active volume.");
    app->dataset_health_ready = 0;
    memset(&app->dataset_health, 0, sizeof(app->dataset_health));
    snprintf(app->dataset_health_status, sizeof(app->dataset_health_status),
             "Dataset Health needs a loaded volume histogram.");
    snprintf(app->report_status, sizeof(app->report_status),
             "No report exported yet.");
    snprintf(app->report_path, sizeof(app->report_path),
             "voxelpilot_insight_report.html");
    app->render_ms_average = 0.0f;
    app->render_ms_average_ready = 0;
    voxelpilot_performance_result_defaults(&app->performance_result);
    snprintf(app->performance_status, sizeof(app->performance_status),
             "Performance Safety: waiting for render telemetry.");
    app->main_hover_active = 0;
    app->main_hover_label_id = 0;
    memset(&app->main_hover_pick, 0, sizeof(app->main_hover_pick));
    snprintf(app->main_hover_description,
             sizeof(app->main_hover_description),
             "Hover over the 3D render for a direct pick explanation.");
    memcpy(app->voxel_spacing, defaults.voxel_spacing, sizeof(app->voxel_spacing));
    memcpy(app->orbit_target, defaults.orbit_target, sizeof(app->orbit_target));
    sync_resolution_selection(app);
    set_camera_preset(app,
        defaults.camera_pos[0], defaults.camera_pos[1], defaults.camera_pos[2],
        defaults.camera_dir[0], defaults.camera_dir[1], defaults.camera_dir[2],
        defaults.camera_up[0], defaults.camera_up[1], defaults.camera_up[2]);
}

static void reset_workspace(StandaloneApp *app)
{
    apply_default_workspace_state(app, 0);
    strcpy(app->screenshot_status, "Workspace reset");
    strcpy(app->preset_status, "Workspace reset");
}
static void process_mouse_camera_controls(StandaloneApp *app)
{
    ImGuiIO *io;
    double mouse_x, mouse_y;
    int left_down;
    int motion_active = 0;

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
        if (fabsf(dx) > 0.001f || fabsf(dy) > 0.001f) {
            motion_active = 1;
        }
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
        motion_active = 1;
    }

    if (motion_active) {
        app->camera_motion_active = 1;
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

int write_png_file(
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
        /* Force recompute on dimension change */
        app->slice_preview_dirty = 1;
    }

    /* Skip CPU recompute when slice positions and dirty flag are unchanged */
    if (!app->slice_preview_dirty &&
        app->slice_x == app->slice_prev_x &&
        app->slice_y == app->slice_prev_y &&
        app->slice_z == app->slice_prev_z) {
        return;
    }
    app->slice_prev_x        = app->slice_x;
    app->slice_prev_y        = app->slice_y;
    app->slice_prev_z        = app->slice_z;
    app->slice_preview_dirty = 0;

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


static void reset_semantic_discovery(StandaloneApp *app)
{
    if (!app) return;
    memset(app->semantic_regions, 0, sizeof(app->semantic_regions));
    memset(app->semantic_relations, 0, sizeof(app->semantic_relations));
    app->semantic_region_count = 0;
    app->semantic_relation_count = 0;
    app->semantic_selected_region = -1;
    snprintf(app->semantic_status, sizeof(app->semantic_status),
             "Click Discover Regions to run ASD-Lite on the loaded volume.");
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
    reset_semantic_discovery(app);
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

static VoxelPilotSemanticRegion *find_semantic_region_by_label(
    StandaloneApp *app,
    unsigned char  label_id)
{
    int i;

    if (!app || label_id == 0) {
        return NULL;
    }

    for (i = 0; i < app->semantic_region_count; ++i) {
        if (app->semantic_regions[i].label_id == label_id) {
            return &app->semantic_regions[i];
        }
    }

    return NULL;
}

static const char *semantic_relationship_note(
    StandaloneApp *app,
    unsigned char  label_id)
{
    static char empty_note[1] = { '\0' };
    int i;

    if (!app || label_id == 0) {
        return empty_note;
    }

    for (i = 0; i < app->semantic_relation_count; ++i) {
        if (app->semantic_relations[i].region_a == label_id ||
            app->semantic_relations[i].region_b == label_id) {
            return app->semantic_relations[i].description;
        }
    }

    return empty_note;
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

static size_t semantic_total_voxels(StandaloneApp *app)
{
    if (!app || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) return 0;
    return (size_t)app->renderer.W * (size_t)app->renderer.H * (size_t)app->renderer.D;
}

static void refresh_semantic_annotation_overlays(StandaloneApp *app)
{
    int i;
    int overlay_count = 0;

    if (!app) return;
    memset(app->annotations, 0, sizeof(app->annotations));
    app->annotation_count = 0;
    app->selected_annotation = -1;

    for (i = 0; i < app->semantic_region_count; ++i) {
        VoxelPilotSemanticRegion *semantic = &app->semantic_regions[i];
        AnnotationRegion *region;
        if (semantic->rejected) {
            clear_label_from_mask(app, semantic->label_id);
            continue;
        }
        if (overlay_count >= MAX_ANNOTATIONS) continue;
        region = &app->annotations[overlay_count];
        memset(region, 0, sizeof(*region));
        region->active = 1;
        region->label_id = semantic->label_id;
        snprintf(region->name, sizeof(region->name), "%s", semantic->name);
        region->color[0] = semantic->color[0];
        region->color[1] = semantic->color[1];
        region->color[2] = semantic->color[2];
        region->seed[0] = semantic->centroid[0] / fmaxf((float)(app->renderer.W - 1), 1.0f);
        region->seed[1] = semantic->centroid[1] / fmaxf((float)(app->renderer.H - 1), 1.0f);
        region->seed[2] = semantic->centroid[2] / fmaxf((float)(app->renderer.D - 1), 1.0f);
        region->seed_intensity = semantic->mean_intensity;
        region->tolerance = app->semantic_sensitivity;
        region->voxel_count = semantic->voxel_count;
        region->min_intensity = semantic->min_intensity;
        region->max_intensity = semantic->max_intensity;
        if (i == app->semantic_selected_region) app->selected_annotation = overlay_count;
        overlay_count++;
    }

    app->annotation_count = overlay_count;
    if (app->selected_annotation < 0 && overlay_count > 0) app->selected_annotation = 0;
    app->label_mask_revision++;
    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Semantic overlays ready: %d ASD region(s).", overlay_count);
}

static int upsert_semantic_correction(
    StandaloneApp *app,
    const VoxelPilotSemanticRegion *region,
    int confirmed,
    int rejected)
{
    VoxelPilotSemanticCorrection correction;
    size_t total_voxels;
    int i;

    if (!app || !region) return 0;
    total_voxels = semantic_total_voxels(app);
    if (!voxelpilot_asd_capture_correction(region, total_voxels, app->semantic_edit_name, confirmed, rejected, &correction)) return 0;

    for (i = 0; i < app->semantic_correction_count; ++i) {
        if (voxelpilot_asd_region_matches_correction(region, total_voxels, &app->semantic_corrections[i])) {
            app->semantic_corrections[i] = correction;
            return 1;
        }
    }

    if (app->semantic_correction_count >= VOXELPILOT_ASD_MAX_CORRECTIONS) {
        snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status),
                 "ASD memory is full; save or prune before adding more corrections.");
        return 0;
    }
    app->semantic_corrections[app->semantic_correction_count++] = correction;
    return 1;
}

static int save_semantic_memory(StandaloneApp *app, const char *path)
{
    FILE *fp;
    int i;
    if (!app || !path || !path[0]) return 0;
    fp = fopen(path, "w");
    if (!fp) {
        snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status), "Could not save ASD memory: %s", path);
        return 0;
    }
    fprintf(fp, "{\n  \"version\": 1,\n  \"corrections\": [\n");
    for (i = 0; i < app->semantic_correction_count; ++i) {
        VoxelPilotSemanticCorrection *correction = &app->semantic_corrections[i];
        fprintf(fp, "    {\"name\": \"");
        voxelpilot_json_write_escaped(fp, correction->name);
        fprintf(fp, "\", \"archetype\": \"");
        voxelpilot_json_write_escaped(fp, correction->archetype);
        fprintf(fp, "\", \"densityClass\": %d, \"meanIntensity\": %.6f, \"minIntensity\": %.6f, \"maxIntensity\": %.6f, \"compactness\": %.6f, \"elongation\": %.6f, \"sizeRatio\": %.8f, \"confirmed\": %d, \"rejected\": %d}%s\n",
                (int)correction->density_class,
                correction->mean_intensity,
                correction->min_intensity,
                correction->max_intensity,
                correction->compactness,
                correction->elongation,
                correction->size_ratio,
                correction->confirmed,
                correction->rejected,
                (i + 1 < app->semantic_correction_count) ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
    snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status),
             "Saved %d ASD correction(s) to %s.", app->semantic_correction_count, path);
    return 1;
}

static int load_semantic_memory(StandaloneApp *app, const char *path)
{
    FILE *fp;
    char line[1024];
    int loaded = 0;
    if (!app || !path || !path[0]) return 0;
    fp = fopen(path, "r");
    if (!fp) {
        snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status), "Could not load ASD memory: %s", path);
        return 0;
    }
    memset(app->semantic_corrections, 0, sizeof(app->semantic_corrections));
    app->semantic_correction_count = 0;
    while (fgets(line, sizeof(line), fp) && loaded < VOXELPILOT_ASD_MAX_CORRECTIONS) {
        VoxelPilotSemanticCorrection correction;
        int density_class = 0;
        if (!strstr(line, "\"name\"") || !strstr(line, "\"archetype\"")) continue;
        memset(&correction, 0, sizeof(correction));
        if (!voxelpilot_json_extract_string_field(line, "name", correction.name, sizeof(correction.name))) continue;
        voxelpilot_json_extract_string_field(line, "archetype", correction.archetype, sizeof(correction.archetype));
        voxelpilot_json_extract_int_field(line, "densityClass", &density_class);
        correction.density_class = (VoxelPilotDensityClass)density_class;
        voxelpilot_json_extract_float_field(line, "meanIntensity", &correction.mean_intensity);
        voxelpilot_json_extract_float_field(line, "minIntensity", &correction.min_intensity);
        voxelpilot_json_extract_float_field(line, "maxIntensity", &correction.max_intensity);
        voxelpilot_json_extract_float_field(line, "compactness", &correction.compactness);
        voxelpilot_json_extract_float_field(line, "elongation", &correction.elongation);
        voxelpilot_json_extract_float_field(line, "sizeRatio", &correction.size_ratio);
        voxelpilot_json_extract_int_field(line, "confirmed", &correction.confirmed);
        voxelpilot_json_extract_int_field(line, "rejected", &correction.rejected);
        app->semantic_corrections[loaded++] = correction;
    }
    fclose(fp);
    app->semantic_correction_count = loaded;
    snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status),
             "Loaded %d ASD correction(s) from %s.", loaded, path);
    if (app->semantic_region_count > 0) {
        voxelpilot_asd_apply_corrections(app->semantic_regions, app->semantic_region_count, semantic_total_voxels(app), app->semantic_corrections, app->semantic_correction_count);
        refresh_semantic_annotation_overlays(app);
    }
    return 1;
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
    {
        size_t written = fwrite(app->label_mask, 1, voxel_count, fp);
        fclose(fp);
        if (written != voxel_count) {
            snprintf(app->annotation_status, sizeof(app->annotation_status),
                     "Export failed: disk write incomplete (%zu of %zu bytes).",
                     written, voxel_count);
            return;
        }
    }

    snprintf(json_path, sizeof(json_path), "%s.json", selected);
    meta = fopen(json_path, "w");
    if (meta) {
        fprintf(meta, "{\n");
        fprintf(meta, "  \"dimensions\": [%d, %d, %d],\n",
                app->label_mask_W,
                app->label_mask_H,
                app->label_mask_D);
        fprintf(meta, "  \"labels\": [\n");
        {
            int emitted = 0;
            for (i = 0; i < app->annotation_count; ++i) {
                AnnotationRegion *region = &app->annotations[i];
                VoxelPilotSemanticRegion *semantic = NULL;
                if (!region->active) continue;
                semantic = find_semantic_region_by_label(app, region->label_id);
                if (emitted > 0) fprintf(meta, ",\n");
                fprintf(meta,
                        "    {\"id\": %u, \"name\": \"",
                        (unsigned int)region->label_id);
                voxelpilot_json_write_escaped(meta, region->name);
                fprintf(meta,
                        "\", \"voxels\": %zu, \"seed\": [%.6f, %.6f, %.6f], \"tolerance\": %.6f, \"intensityRange\": [%.6f, %.6f]",
                        region->voxel_count,
                        region->seed[0],
                        region->seed[1],
                        region->seed[2],
                        region->tolerance,
                        region->min_intensity,
                        region->max_intensity);
                if (semantic) {
                    fprintf(meta,
                            ", \"semantic\": {\"source\": \"ASD-Lite heuristic\", \"confidence\": %.6f, \"stabilityScore\": %.6f, \"importanceScore\": %.6f, \"importanceRank\": %d, \"archetype\": \"",
                            semantic->confidence,
                            semantic->stability_score,
                            semantic->importance_score,
                            semantic->importance_rank);
                    voxelpilot_json_write_escaped(meta, semantic->archetype);
                    fprintf(meta, "\", \"description\": \"");
                    voxelpilot_json_write_escaped(meta, semantic->description);
                    fprintf(meta, "\"}");
                }
                fprintf(meta, "}");
                emitted++;
            }
            fprintf(meta, "\n  ],\n");
        }
        fprintf(meta, "  \"relationships\": [\n");
        {
            int emitted = 0;
            for (i = 0; i < app->semantic_relation_count; ++i) {
                VoxelPilotRegionRelation *relation = &app->semantic_relations[i];
                if (relation->region_a == 0) continue;
                if (emitted > 0) fprintf(meta, ",\n");
                fprintf(meta,
                        "    {\"regionA\": %u, \"regionB\": %u, \"type\": %d, \"centroidDistance\": %.6f, \"confidence\": %.6f, \"description\": \"",
                        (unsigned int)relation->region_a,
                        (unsigned int)relation->region_b,
                        (int)relation->type,
                        relation->centroid_distance,
                        relation->confidence);
                voxelpilot_json_write_escaped(meta, relation->description);
                fprintf(meta, "\"}");
                emitted++;
            }
            fprintf(meta, "\n  ]\n");
        }
        fprintf(meta, "}\n");
        fclose(meta);
    }

    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Exported annotation mask: %s", selected);
}


static void run_adaptive_semantic_discovery(StandaloneApp *app)
{
    int discovered = 0;
    int applied_corrections = 0;

    if (!app || !app->renderer.volume_data ||
        app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        if (app) {
            snprintf(app->semantic_status, sizeof(app->semantic_status),
                     "Load a volume before running ASD-Lite.");
        }
        return;
    }

    if (app->semantic_max_regions < 1) app->semantic_max_regions = 1;
    if (app->semantic_max_regions > MAX_ANNOTATIONS) app->semantic_max_regions = MAX_ANNOTATIONS;
    if (app->semantic_min_region_voxels < 1) app->semantic_min_region_voxels = 1;
    app->semantic_sensitivity = voxelpilot_asd_clampf(app->semantic_sensitivity, 0.0f, 1.0f);

    if (!ensure_label_mask(app)) {
        snprintf(app->semantic_status, sizeof(app->semantic_status),
                 "Could not allocate semantic label mask for this volume.");
        return;
    }

    discovered = voxelpilot_discover_semantic_regions(
        app->renderer.volume_data,
        app->renderer.W,
        app->renderer.H,
        app->renderer.D,
        app->histogram,
        HISTOGRAM_BINS,
        app->semantic_max_regions,
        app->semantic_sensitivity,
        (size_t)app->semantic_min_region_voxels,
        app->label_mask,
        app->semantic_regions,
        &app->semantic_region_count);

    memset(app->annotations, 0, sizeof(app->annotations));
    app->annotation_count = 0;
    app->selected_annotation = -1;
    app->semantic_selected_region = -1;
    app->semantic_relation_count = 0;

    if (!discovered || app->semantic_region_count < 1) {
        memset(app->semantic_relations, 0, sizeof(app->semantic_relations));
        app->label_mask_revision++;
        snprintf(app->semantic_status, sizeof(app->semantic_status),
                 "ASD-Lite found no stable regions. Try higher sensitivity or lower Min Voxels.");
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "No semantic regions discovered for overlay.");
        return;
    }

    voxelpilot_compute_region_relationships(
        app->semantic_regions,
        app->semantic_region_count,
        app->semantic_relations,
        VOXELPILOT_ASD_MAX_RELATIONS,
        &app->semantic_relation_count);

    if (app->semantic_correction_count > 0) {
        applied_corrections = voxelpilot_asd_apply_corrections(
            app->semantic_regions,
            app->semantic_region_count,
            semantic_total_voxels(app),
            app->semantic_corrections,
            app->semantic_correction_count);
    }

    app->semantic_selected_region = app->semantic_region_count > 0 ? 0 : -1;
    app->label_overlay_visible = 1;
    if (app->semantic_cinematic_emphasis) {
        app->cinematic_enabled = 1;
        if (app->label_overlay_alpha < 0.72f) app->label_overlay_alpha = 0.72f;
    }
    refresh_semantic_annotation_overlays(app);

    if (app->semantic_selected_region >= 0) {
        snprintf(app->semantic_edit_name, sizeof(app->semantic_edit_name),
                 "%s", app->semantic_regions[app->semantic_selected_region].name);
    }

    snprintf(app->semantic_status, sizeof(app->semantic_status),
             "ASD-Lite discovered %d region(s), %d relationship(s), and applied %d memory correction(s).",
             app->semantic_region_count,
             app->semantic_relation_count,
             applied_corrections);
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
            VoxelPilotSemanticRegion *semantic = NULL;

            if (app->label_mask &&
                app->label_mask_W == app->renderer.W &&
                app->label_mask_H == app->renderer.H &&
                app->label_mask_D == app->renderer.D) {
                label_id = app->label_mask[voxel_index];
                region = find_annotation_by_label(app, label_id);
                semantic = find_semantic_region_by_label(app, label_id);
            }

            if (semantic) {
                const char *note = semantic_relationship_note(app, label_id);
                snprintf(app->hover_description,
                         sizeof(app->hover_description),
                         "%s | %s | confidence %.0f%% | stability %.0f%% | source ASD-Lite. %s%s%s",
                         semantic->name,
                         semantic->archetype,
                         semantic->confidence * 100.0f,
                         semantic->stability_score * 100.0f,
                         semantic->description,
                         (note && note[0]) ? " " : "",
                         (note && note[0]) ? note : "");
            } else {
                voxelpilot_describe_voxel_context(
                    intensity,
                    label_id,
                    region ? region->name : "",
                    app->hover_description,
                    sizeof(app->hover_description));
            }

            ImGui::BeginTooltip();
            ImGui::Text("%s", app->hover_description);
            ImGui::Text("Voxel: %d, %d, %d", vx, vy, vz);
            if (semantic) {
                ImGui::Text("ASD rank: %d | importance %.0f%%",
                            semantic->importance_rank,
                            semantic->importance_score * 100.0f);
            }
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
    fprintf(fp, "ribbon_expanded %d\n", app->ribbon_expanded);
    fprintf(fp, "ribbon_active_tab %d\n", app->ribbon_active_tab);
    fprintf(fp, "controls_visible %d\n", app->controls_visible);
    fprintf(fp, "insights_visible %d\n", app->insights_visible);
    fprintf(fp, "ai_visible %d\n", app->ai_visible);
    fprintf(fp, "metadata_visible %d\n", app->metadata_visible);
    fprintf(fp, "step_size %.9f\n", app->step_size);
    fprintf(fp, "threshold %.9f\n", app->threshold);
    fprintf(fp, "skip_mult %.9f\n", app->skip_mult);
    fprintf(fp, "fov_y_deg %.9f\n", app->fov_y_deg);
    fprintf(fp, "tf_center %.9f\n", app->tf_center);
    fprintf(fp, "tf_width %.9f\n", app->tf_width);
    fprintf(fp, "tf_opacity_scale %.9f\n", app->tf_opacity_scale);
    fprintf(fp, "cinematic_enabled %d\n", app->cinematic_enabled);
    fprintf(fp, "cinematic_rim_strength %.9f\n", app->cinematic_rim_strength);
    fprintf(fp, "cinematic_glow_strength %.9f\n", app->cinematic_glow_strength);
    fprintf(fp, "cinematic_contrast %.9f\n", app->cinematic_contrast);
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
        else if (strcmp(key, "ribbon_expanded") == 0) fscanf(fp, "%d", &app->ribbon_expanded);
        else if (strcmp(key, "ribbon_active_tab") == 0) fscanf(fp, "%d", &app->ribbon_active_tab);
        else if (strcmp(key, "controls_visible") == 0) fscanf(fp, "%d", &app->controls_visible);
        else if (strcmp(key, "insights_visible") == 0) fscanf(fp, "%d", &app->insights_visible);
        else if (strcmp(key, "ai_visible") == 0) fscanf(fp, "%d", &app->ai_visible);
        else if (strcmp(key, "metadata_visible") == 0) fscanf(fp, "%d", &app->metadata_visible);
        else if (strcmp(key, "step_size") == 0) fscanf(fp, "%f", &app->step_size);
        else if (strcmp(key, "threshold") == 0) fscanf(fp, "%f", &app->threshold);
        else if (strcmp(key, "skip_mult") == 0) fscanf(fp, "%f", &app->skip_mult);
        else if (strcmp(key, "fov_y_deg") == 0) fscanf(fp, "%f", &app->fov_y_deg);
        else if (strcmp(key, "tf_center") == 0) fscanf(fp, "%f", &app->tf_center);
        else if (strcmp(key, "tf_width") == 0) fscanf(fp, "%f", &app->tf_width);
        else if (strcmp(key, "tf_opacity_scale") == 0) fscanf(fp, "%f", &app->tf_opacity_scale);
        else if (strcmp(key, "cinematic_enabled") == 0) fscanf(fp, "%d", &app->cinematic_enabled);
        else if (strcmp(key, "cinematic_rim_strength") == 0) fscanf(fp, "%f", &app->cinematic_rim_strength);
        else if (strcmp(key, "cinematic_glow_strength") == 0) fscanf(fp, "%f", &app->cinematic_glow_strength);
        else if (strcmp(key, "cinematic_contrast") == 0) fscanf(fp, "%f", &app->cinematic_contrast);
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
    app->ribbon_expanded = app->ribbon_expanded ? 1 : 0;
    if (app->ribbon_active_tab < 0 || app->ribbon_active_tab >= VOXELPILOT_RIBBON_COUNT) {
        app->ribbon_active_tab = VOXELPILOT_RIBBON_HOME;
    }
    app->controls_visible = app->controls_visible ? 1 : 0;
    app->insights_visible = app->insights_visible ? 1 : 0;
    app->ai_visible = app->ai_visible ? 1 : 0;
    app->metadata_visible = app->metadata_visible ? 1 : 0;
    clamp_cinematic_settings(app);
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

static void attach_label_overlay_to_render_command(
    StandaloneApp  *app,
    RendererInput  *cmd)
{
    VoxelPilotRenderLabelOverlayConfig labels;
    int i;
    int color_count;

    if (!app || !cmd) return;

    voxelpilot_render_label_overlay_config_defaults(&labels);
    labels.visible = app->label_overlay_visible;
    labels.label_mask = app->label_mask;
    labels.label_mask_width = app->label_mask_W;
    labels.label_mask_height = app->label_mask_H;
    labels.label_mask_depth = app->label_mask_D;
    labels.renderer_width = app->renderer.W;
    labels.renderer_height = app->renderer.H;
    labels.renderer_depth = app->renderer.D;
    labels.label_count = app->annotation_count;
    labels.alpha = app->label_overlay_alpha;
    labels.revision = app->label_mask_revision;

    if (app->label_selected_emphasis_enabled &&
        app->selected_annotation >= 0 &&
        app->selected_annotation < app->annotation_count) {
        labels.selected_enabled = 1;
        labels.selected_label_id = app->annotations[app->selected_annotation].label_id;
        labels.selected_emphasis = app->label_selected_emphasis_strength;
    }

    color_count = MAX_ANNOTATIONS < RENDERER_MAX_LABELS
                      ? MAX_ANNOTATIONS
                      : RENDERER_MAX_LABELS;
    for (i = 0; i < color_count; ++i) {
        labels.colors[i * 3 + 0] = app->annotations[i].color[0];
        labels.colors[i * 3 + 1] = app->annotations[i].color[1];
        labels.colors[i * 3 + 2] = app->annotations[i].color[2];
    }

    voxelpilot_attach_label_overlay_to_renderer_input(&labels, cmd);
}
static int build_current_renderer_input(
    StandaloneApp *app,
    RendererInput *cmd)
{
    VoxelPilotRenderCommandConfig config;

    if (!app || !cmd) return 0;

    refresh_adaptive_quality_for_frame(app);
    voxelpilot_render_command_config_defaults(&config);

    memcpy(config.cam_pos, app->cam_pos, sizeof(config.cam_pos));
    memcpy(config.cam_dir, app->cam_dir, sizeof(config.cam_dir));
    memcpy(config.cam_up, app->cam_up, sizeof(config.cam_up));
    memcpy(config.light_pos, app->light_pos, sizeof(config.light_pos));
    memcpy(config.clip_min, app->clip_min, sizeof(config.clip_min));
    memcpy(config.clip_max, app->clip_max, sizeof(config.clip_max));

    config.fov_y_degrees = app->fov_y_deg;
    config.step_size = app->adaptive_step_size;
    config.threshold = app->threshold;
    config.empty_space_skip_mult = app->adaptive_skip_mult;
    config.tf_center = app->tf_center;
    config.tf_width = app->tf_width;
    config.tf_opacity_scale = app->tf_opacity_scale;
    config.tf_palette = app->tf_palette;
    config.tf_invert = app->tf_invert;
    config.cinematic_enabled = app->cinematic_enabled;
    config.cinematic_rim_strength = app->cinematic_rim_strength;
    config.cinematic_glow_strength = app->cinematic_glow_strength;
    config.cinematic_contrast = app->cinematic_contrast;
    config.render_width = app->render_w;
    config.render_height = app->render_h;

    if (!voxelpilot_build_renderer_input(&config, cmd)) {
        return 0;
    }

    attach_label_overlay_to_render_command(app, cmd);
    return 1;
}
/* ============================================================
   Render one frame to the CUDA output buffer only (no GL window)
   ============================================================ */
void render_frame_gpu_snapshot(StandaloneApp *app)
{
    RendererInput cmd;

    if (!build_current_renderer_input(app, &cmd)) {
        return;
    }

    apply_render_command(&app->renderer, &cmd);
    app->last_render_ms = render_frame_gpu(&app->renderer);
    update_render_performance_average(app);
    app->frame_id++;
}

/* ============================================================
   Render one frame directly (no networking)
   ============================================================ */
static void render_frame_standalone(StandaloneApp *app)
{
    RendererInput cmd;

    if (!build_current_renderer_input(app, &cmd)) {
        return;
    }

    /* Apply directly to renderer - NO NETWORK */
    apply_render_command(&app->renderer, &cmd);

    /* Render directly on GPU */
    app->last_render_ms = render_frame_gpu(&app->renderer);
    update_render_performance_average(app);
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
    glUniform1i(app->tex_uniform_loc, 0);
    glBindVertexArray(app->vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

static float voxelpilot_ribbon_height(const StandaloneApp *app)
{
    return (app && app->ribbon_expanded) ? 122.0f : 38.0f;
}

static void draw_dockspace(StandaloneApp *app)
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
    float ribbon_height = voxelpilot_ribbon_height(app);
    ImVec2 dock_pos = ImVec2(viewport->Pos.x, viewport->Pos.y + ribbon_height);
    ImVec2 dock_size = ImVec2(viewport->Size.x, fmaxf(120.0f, viewport->Size.y - ribbon_height));

    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) {
        window_flags |= ImGuiWindowFlags_NoBackground;
    }

    ImGui::SetNextWindowPos(dock_pos);
    ImGui::SetNextWindowSize(dock_size);
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
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    float ribbon_height = voxelpilot_ribbon_height(app);
    ImVec2 dock_size = ImVec2(viewport->Size.x, fmaxf(120.0f, viewport->Size.y - ribbon_height));

    dockspace_id = ImGui::GetID("VoxelPilotDockspace");

    if (app->dock_layout_ready &&
        !voxelpilot_should_rebuild_dock_layout(
            app->dock_layout_viewport_w,
            app->dock_layout_viewport_h,
            dock_size.x,
            dock_size.y)) {
        return;
    }

    app->dock_layout_viewport_w = dock_size.x;
    app->dock_layout_viewport_h = dock_size.y;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        ImGuiDockNodeFlags_DockSpace |
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

    dock_main_id = dockspace_id;
    dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.12f, NULL, &dock_main_id);
    dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.26f, NULL, &dock_main_id);
    dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.30f, NULL, &dock_main_id);

    ImGui::DockBuilderDockWindow("Controls", dock_left_id);
    ImGui::DockBuilderDockWindow("Status", dock_bottom_id);

    if (app->dock_layout_variant == 1) {
        ImGuiID dock_right_bottom_id =
            ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.45f, NULL, &dock_right_id);
        ImGui::DockBuilderDockWindow("Insights", dock_right_id);
        ImGui::DockBuilderDockWindow("AI Assist", dock_right_bottom_id);
        ImGui::DockBuilderDockWindow("Metadata", dock_right_bottom_id);
    } else {
        ImGui::DockBuilderDockWindow("AI Assist", dock_right_id);
        ImGui::DockBuilderDockWindow("Insights", dock_right_id);
        ImGui::DockBuilderDockWindow("Metadata", dock_right_id);
    }

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
    float ribbon_height;
    float bottom_height;
    float main_height;
    float right_top_height;
    float right_ai_height;
    float right_mid_height;

    ribbon_height = voxelpilot_ribbon_height(app);
    left_width = fminf(380.0f, fmaxf(280.0f, viewport->Size.x * 0.26f));
    right_width = fminf(440.0f, fmaxf(300.0f, viewport->Size.x * 0.30f));
    bottom_height = viewport->Size.y * 0.12f;
    main_height = viewport->Size.y - ribbon_height - bottom_height;
    right_top_height = main_height * 0.24f;
    right_ai_height = main_height * 0.42f;
    right_mid_height =
        main_height -
        right_top_height -
        right_ai_height;

    if (strcmp(name, "Controls") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x,
                   viewport->Pos.y + ribbon_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(left_width, main_height),
            layout_cond);
    } else if (strcmp(name, "Insights") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + ribbon_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_top_height),
            layout_cond);
    } else if (strcmp(name, "AI Assist") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + ribbon_height + right_top_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_ai_height),
            layout_cond);
    } else if (strcmp(name, "Metadata") == 0) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - right_width,
                   viewport->Pos.y + ribbon_height + right_top_height + right_ai_height),
            layout_cond);
        ImGui::SetNextWindowSize(
            ImVec2(right_width, right_mid_height),
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
static void draw_ribbon_tab(StandaloneApp *app, int tab_index)
{
    int active = app && app->ribbon_active_tab == tab_index;
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.38f, 0.42f, 1.0f));
    }
    if (ImGui::Button(k_ribbon_tab_labels[tab_index], ImVec2(92.0f, 0.0f)) && app) {
        app->ribbon_active_tab = tab_index;
    }
    if (active) {
        ImGui::PopStyleColor(2);
    }
}

static void draw_top_ribbon(StandaloneApp *app)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    float height = voxelpilot_ribbon_height(app);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
#ifdef IMGUI_HAS_DOCK
    flags |= ImGuiWindowFlags_NoDocking;
#endif
    int tab;

    if (app) {
        /* Keep the always-visible Safety badge accurate every frame,
           regardless of whether AI Assist is open or a gated action ran. */
        refresh_performance_governor(app);
    }

    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.070f, 0.085f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.28f, 0.33f, 0.70f));
    ImGui::Begin("VoxelPilot Ribbon", NULL, flags);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    if (ImGui::BeginTable("RibbonLayout", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Brand", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 460.0f);

        ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.78f, 1.0f), "%s", BRAND_TITLE);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.72f, 0.78f, 0.86f, 1.0f), "%s", BRAND_SUBTITLE);
        if (app) {
            ImGui::SameLine();
            ImGui::TextColored(
                voxelpilot_pressure_color(app->performance_result.status),
                "| Safety: %s (%d/100)",
                app->performance_result.status_label,
                app->performance_result.pressure_score);
            if (ImGui::IsItemHovered() && app->performance_result.primary_reason[0]) {
                ImGui::SetTooltip("%s", app->performance_result.primary_reason);
            }
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.20f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.30f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.38f, 0.40f, 1.0f));
        if (ImGui::Button(app && app->ribbon_expanded ? "Collapse" : "Expand", ImVec2(90.0f, 0.0f)) && app) {
            app->ribbon_expanded = app->ribbon_expanded ? 0 : 1;
            app->dock_layout_ready = 0;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::BeginGroup();
        for (tab = 0; tab < VOXELPILOT_RIBBON_COUNT; ++tab) {
            if (tab != 0) {
                ImGui::SameLine();
            }
            draw_ribbon_tab(app, tab);
        }
        ImGui::EndGroup();

        ImGui::EndTable();
    }

    if (app && app->ribbon_expanded) {
        ImGui::Separator();
        if (app->ribbon_active_tab == VOXELPILOT_RIBBON_HOME) {
            /* Reset Workspace and Guided Walkthrough live in Controls -
               kept here once as an exact duplicate; removed. */
            if (ImGui::Button("Reset Layout")) reset_default_layout(app);
            ImGui::SameLine();
            ImGui::Text("GPU %.2f ms | FPS %.1f", app->last_render_ms, app->gui_fps);
            ImGui::SameLine();
            ImGui::Text("Right rail:");
            {
                bool insights_visible = app->insights_visible != 0;
                bool ai_visible = app->ai_visible != 0;
                bool metadata_visible = app->metadata_visible != 0;
                ImGui::SameLine();
                if (ImGui::Checkbox("Insights##panel_insights", &insights_visible)) {
                    app->insights_visible = insights_visible ? 1 : 0;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("AI Assist##panel_ai", &ai_visible)) {
                    app->ai_visible = ai_visible ? 1 : 0;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Metadata##panel_metadata", &metadata_visible)) {
                    app->metadata_visible = metadata_visible ? 1 : 0;
                }
            }
        } else if (app->ribbon_active_tab == VOXELPILOT_RIBBON_VIEW) {
            bool cinematic_enabled = app->cinematic_enabled != 0;
            if (ImGui::Checkbox("Cinematic", &cinematic_enabled)) {
                app->cinematic_enabled = cinematic_enabled ? 1 : 0;
                clamp_cinematic_settings(app);
            }
            ImGui::SameLine();
            ImGui::Text("Quality: %s", k_render_quality_labels[app->render_quality_mode]);
            ImGui::SameLine();
            ImGui::Text("Render: %d x %d", app->render_w, app->render_h);
            if (app->cinematic_enabled) {
                ImGui::SliderFloat("Rim##ribbon_cinematic_rim", &app->cinematic_rim_strength, 0.0f, 1.0f);
                ImGui::SameLine();
                ImGui::SliderFloat("Glow##ribbon_cinematic_glow", &app->cinematic_glow_strength, 0.0f, 1.0f);
                ImGui::SameLine();
                ImGui::SliderFloat("Contrast##ribbon_cinematic_contrast", &app->cinematic_contrast, 0.8f, 1.6f);
                clamp_cinematic_settings(app);
            }
        }
    }

    ImGui::End();
}

static void set_ui_toast(StandaloneApp *app, const char *message)
{
    if (!app) return;
    snprintf(app->ui_toast, sizeof(app->ui_toast), "%s", message ? message : "");
}

static void capture_transfer_undo(StandaloneApp *app)
{
    if (!app) return;
    app->transfer_undo_center = app->tf_center;
    app->transfer_undo_width = app->tf_width;
    app->transfer_undo_opacity_scale = app->tf_opacity_scale;
    app->transfer_undo_available = 1;
}

static void apply_transfer_settings_with_undo(
    StandaloneApp *app,
    float          center,
    float          width,
    float          opacity_scale,
    const char    *label)
{
    if (!app) return;
    capture_transfer_undo(app);
    app->tf_center = center;
    app->tf_width = width;
    app->tf_opacity_scale = opacity_scale;
    snprintf(app->ai_assist_status,
             sizeof(app->ai_assist_status),
             "%s applied: center %.3f, width %.3f, opacity %.2f.",
             label && label[0] ? label : "Transfer",
             app->tf_center,
             app->tf_width,
             app->tf_opacity_scale);
    set_ui_toast(app, "Transfer applied. Undo is available.");
    update_quant_metrics(app);
}

static int undo_transfer_settings(StandaloneApp *app)
{
    if (!app || !app->transfer_undo_available) return 0;
    app->tf_center = app->transfer_undo_center;
    app->tf_width = app->transfer_undo_width;
    app->tf_opacity_scale = app->transfer_undo_opacity_scale;
    app->transfer_undo_available = 0;
    snprintf(app->ai_assist_status,
             sizeof(app->ai_assist_status),
             "Restored previous transfer settings.");
    set_ui_toast(app, "Transfer reverted.");
    update_quant_metrics(app);
    return 1;
}

static void reset_default_layout(StandaloneApp *app)
{
    if (!app) return;
    app->controls_visible = 1;
    app->insights_visible = 1;
    app->ai_visible = 1;
    app->metadata_visible = 1;
    app->dock_layout_ready = 0;
    app->layout_refit_next_frame = 1;
    snprintf(app->preset_status, sizeof(app->preset_status), "Layout reset to default.");
    set_ui_toast(app, "Layout reset.");
}

static void draw_help_marker(const char *description)
{
    ImGui::SameLine();
    ImGui::TextDisabled("?");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", description ? description : "More information");
    }
}

static int browse_volume_file(StandaloneApp *app)
{
    const char *filter_patterns[] = { "*.raw", "*.bin", "*.dat", "*" };
    const char *selected;

    if (!app) return 0;
    selected = tinyfd_openFileDialog(
        "Select Volume File",
        "",
        4,
        filter_patterns,
        "Volume Files (*.raw *.bin *.dat)",
        0);
    if (!selected) return 0;

    strncpy(app->upload_path, selected, sizeof(app->upload_path) - 1);
    app->upload_path[sizeof(app->upload_path) - 1] = '\0';
    refresh_upload_file_details(app);
    strcpy(app->upload_status,
           "File selected. Review import settings, then load volume.");
    set_ui_toast(app, "Volume file selected.");
    return 1;
}

static int load_selected_volume(StandaloneApp *app)
{
    VolumeImportSpec spec;
    float *temp = NULL;
    size_t voxel_count = 0;
    char error[256];
    size_t expected = 0;

    if (!app) return 0;
    spec.width = (uint32_t)clamp_import_dimension(app->upload_width);
    spec.height = (uint32_t)clamp_import_dimension(app->upload_height);
    spec.depth = (uint32_t)clamp_import_dimension(app->upload_depth);
    spec.data_type = (VolumeDataType)app->upload_data_type;

    app->upload_width = (int)spec.width;
    app->upload_height = (int)spec.height;
    app->upload_depth = (int)spec.depth;

    if (!app->upload_path[0]) {
        strcpy(app->upload_status, "Select a raw volume file before loading.");
        set_ui_toast(app, "Select a volume file first.");
        return 0;
    }

    if (volume_import_load(
            app->upload_path,
            &spec,
            &temp,
            &voxel_count,
            error,
            sizeof(error)) != 0) {
        snprintf(app->upload_status, sizeof(app->upload_status), "%s", error);
        set_ui_toast(app, "Volume load failed.");
        return 0;
    }

    volume_import_expected_bytes(&spec, &expected);
    strcpy(app->upload_status, "Loading to GPU...");
    reload_volume(
        &app->renderer,
        temp,
        (int)spec.width,
        (int)spec.height,
        (int)spec.depth);
    update_histogram(app);
    update_dataset_health(app);
    apply_auto_enhance_on_load_if_safe(app, "Loaded volume");
    app->slice_preview_dirty = 1;
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
             spec.width,
             spec.height,
             spec.depth,
             volume_import_data_type_label(spec.data_type),
             voxel_count,
             (double)expected / (1024.0 * 1024.0));
    volume_import_free(temp);
    set_ui_toast(app, "Volume loaded.");
    return 1;
}

static void handle_keyboard_shortcuts(StandaloneApp *app)
{
    ImGuiIO *io;

    if (!app) return;
    io = &ImGui::GetIO();
    if (!io->KeyCtrl || io->WantTextInput) return;

    if (ImGui::IsKeyPressed(ImGuiKey_O)) {
        browse_volume_file(app);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L)) {
        load_selected_volume(app);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_screenshot(app);
        set_ui_toast(app, "Snapshot saved.");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_T)) {
        app->cinematic_enabled = app->cinematic_enabled ? 0 : 1;
        clamp_cinematic_settings(app);
        set_ui_toast(app, app->cinematic_enabled ? "Cinematic on." : "Cinematic off.");
    }
}
static void draw_controls_panel(StandaloneApp *app)
{
    if (!app->controls_visible) {
        return;
    }

    set_default_window_layout(app, "Controls");
    bool controls_open = app->controls_visible != 0;
    if (ImGui::Begin("Controls", &controls_open)) {
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

    {
        const char *layout_label = app->dock_layout_variant == 1
            ? "Switch to Demo Layout"
            : "Switch to Deep Review Layout";
        if (ImGui::Button(layout_label)) {
            app->dock_layout_variant = app->dock_layout_variant == 1 ? 0 : 1;
            app->dock_layout_ready = 0;
        }
        ImGui::TextColored(
            ImVec4(0.62f, 0.68f, 0.74f, 1.0f),
            "%s",
            app->dock_layout_variant == 1
                ? "Deep Review: Insights and Metadata both stay visible for close analysis."
                : "Demo: Insights, AI Assist, and Metadata share one tabbed panel.");
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
    {
        bool adaptive_quality = app->adaptive_quality_enabled != 0;
        if (ImGui::Checkbox("Adaptive GPU Quality", &adaptive_quality)) {
            app->adaptive_quality_enabled = adaptive_quality ? 1 : 0;
            app->adaptive_quality_multiplier = 1.0f;
            refresh_adaptive_quality_for_frame(app);
        }
    }
    ImGui::TextWrapped("%s", app->adaptive_quality_status);
    if (app->adaptive_quality_active) {
        ImGui::Text("Effective step %.4f | skip %.2f",
                    app->adaptive_step_size,
                    app->adaptive_skip_mult);
    }
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
    draw_help_marker("Choose RAW/BIN/DAT dimensions and data type, then load the selected volume to the GPU.");

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

    if (ImGui::Button("Browse...")) {
        browse_volume_file(app);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##filepath",
                     app->upload_path,
                     sizeof(app->upload_path),
                     ImGuiInputTextFlags_ReadOnly);

    if (ImGui::Button("Load Volume")) {
        load_selected_volume(app);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Import status: %s",
                        app->upload_status);

    ImGui::Separator();
    ImGui::Text("Capture & Export");
    if (ImGui::Button("Save Snapshot")) {
        save_screenshot(app);
        set_ui_toast(app, "Snapshot saved.");
    }
    ImGui::TextWrapped("Snapshot status: %s",
                       app->screenshot_status);

    }
    app->controls_visible = controls_open ? 1 : 0;
    ImGui::End();
}

static void draw_insights_panel(StandaloneApp *app)
{
    if (!app->insights_visible) {
        return;
    }

    set_default_window_layout(app, "Insights");
    bool insights_open = app->insights_visible != 0;
    if (ImGui::Begin("Insights", &insights_open)) {

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

    }
    app->insights_visible = insights_open ? 1 : 0;
    ImGui::End();
}

static void apply_auto_enhance_import_view(StandaloneApp *app)
{
    if (!app) {
        return;
    }

    app->tf_palette = 1;
    app->cinematic_enabled = 1;
    app->cinematic_rim_strength = fmaxf(app->cinematic_rim_strength, 0.45f);
    app->cinematic_glow_strength = fmaxf(app->cinematic_glow_strength, 0.35f);
    app->cinematic_contrast = fmaxf(app->cinematic_contrast, 1.20f);
    clamp_cinematic_settings(app);

    app->light_pos[0] = -0.80f;
    app->light_pos[1] =  2.05f;
    app->light_pos[2] = -3.00f;
    app->fov_y_deg = 82.0f;
    set_camera_preset(app,
        1.15f, 0.16f, 1.45f,
       -0.54f, 0.29f,-0.79f,
        0.0f, 1.0f, 0.0f);
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

    apply_transfer_settings_with_undo(
        app,
        center,
        width,
        opacity_scale,
        reason && reason[0] ? reason : "Auto enhance");
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
        app->tf_opacity_scale = 2.0f;
        snprintf(app->ai_assist_status,
                 sizeof(app->ai_assist_status),
                 "%s boosted opacity for CT-style volume; use Auto Enhance to also adjust the TF window.",
                 reason && reason[0] ? reason : "Volume load");
        return 1;
    }

    if (!apply_auto_enhance_transfer(app, reason)) {
        return 0;
    }

    apply_auto_enhance_import_view(app);
    snprintf(app->ai_assist_status,
             sizeof(app->ai_assist_status),
             "%s auto-enhanced transfer, palette, lighting, and camera framing for import visibility.",
             reason && reason[0] ? reason : "Volume load");
    return 1;
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
        apply_transfer_settings_with_undo(
            app,
            result.tf_center,
            result.tf_width,
            result.tf_opacity_scale,
            "Prompt transfer");
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

void update_quant_metrics(StandaloneApp *app)
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

void update_object_summary(StandaloneApp *app)
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

void run_semantic_demo_workflow(StandaloneApp *app, const char *reason)
{
    VoxelPilotSemanticDemoSettings settings;
    const char *label = reason && reason[0] ? reason : "Semantic demo";

    if (!app || !app->renderer.volume_data ||
        app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        if (app) {
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "%s needs a loaded volume.", label);
            snprintf(app->semantic_status, sizeof(app->semantic_status),
                     "Load a volume before running the semantic demo.");
        }
        return;
    }

    voxelpilot_semantic_demo_defaults(&settings);

    app->cinematic_enabled = settings.cinematic_enabled;
    app->cinematic_rim_strength = fmaxf(app->cinematic_rim_strength, settings.cinematic_rim_strength);
    app->cinematic_glow_strength = fmaxf(app->cinematic_glow_strength, settings.cinematic_glow_strength);
    app->cinematic_contrast = fmaxf(app->cinematic_contrast, settings.cinematic_contrast);
    clamp_cinematic_settings(app);

    app->semantic_callouts_enabled = settings.callouts_enabled;
    app->semantic_callout_top_regions = settings.callout_top_regions;
    app->semantic_callout_selected_only = settings.selected_only;
    app->semantic_callout_opacity = settings.callout_opacity;
    app->label_overlay_visible = settings.label_overlay_visible;
    app->label_overlay_alpha = fmaxf(app->label_overlay_alpha, settings.label_overlay_alpha);
    app->label_selected_emphasis_enabled = 1;
    app->label_selected_emphasis_strength = fmaxf(app->label_selected_emphasis_strength, 0.72f);
    app->semantic_max_regions = settings.semantic_max_regions;
    app->semantic_min_region_voxels = settings.semantic_min_region_voxels;
    app->semantic_sensitivity = settings.semantic_sensitivity;
    app->semantic_cinematic_emphasis = 1;

    apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_BALANCED);
    apply_auto_enhance_transfer(app, label);
    apply_auto_enhance_import_view(app);
    update_object_summary(app);
    update_quant_metrics(app);
    run_adaptive_semantic_discovery(app);

    if (app->semantic_region_count > 0) {
        app->semantic_selected_region = 0;
        app->selected_annotation = 0;
        snprintf(app->semantic_edit_name, sizeof(app->semantic_edit_name),
                 "%s", app->semantic_regions[0].name);
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s ready: cinematic mode, auto window, ASD discovery, region overlay, and callouts are active.",
                 label);
    } else {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s ran, but ASD-Lite found no stable regions. Try lowering Min Voxels or increasing sensitivity.",
                 label);
    }
}

static int select_guided_review_region(StandaloneApp *app, int direction)
{
    VoxelPilotGuidedReviewStep step;
    VoxelPilotSemanticRegion *region;
    AnnotationRegion *overlay_region;

    if (!app || app->semantic_region_count < 1) {
        if (app) {
            snprintf(app->guided_review_status, sizeof(app->guided_review_status),
                     "Guided Review needs ASD regions. Run Semantic Demo or Discover Regions first.");
        }
        return 0;
    }

    if (!voxelpilot_guided_review_select_region(
            app->semantic_regions,
            app->semantic_region_count,
            app->semantic_selected_region,
            direction,
            &step)) {
        snprintf(app->guided_review_status, sizeof(app->guided_review_status),
                 "Guided Review found no reviewable ASD regions. Check rejected labels or rerun discovery.");
        return 0;
    }

    app->guided_review_step = step;
    app->semantic_selected_region = step.region_index;
    region = &app->semantic_regions[step.region_index];
    overlay_region = find_annotation_by_label(app, region->label_id);
    app->selected_annotation = overlay_region ? (int)(overlay_region - app->annotations) : -1;

    if (app->renderer.W > 1) {
        app->slice_x = voxelpilot_ai_clampf(region->centroid[0] / (float)(app->renderer.W - 1), 0.0f, 1.0f);
    }
    if (app->renderer.H > 1) {
        app->slice_y = voxelpilot_ai_clampf(region->centroid[1] / (float)(app->renderer.H - 1), 0.0f, 1.0f);
    }
    if (app->renderer.D > 1) {
        app->slice_z = voxelpilot_ai_clampf(region->centroid[2] / (float)(app->renderer.D - 1), 0.0f, 1.0f);
    }
    app->slice_preview_dirty = 1;

    app->semantic_callouts_enabled = 1;
    app->semantic_callout_selected_only = 1;
    app->semantic_callout_top_regions = 1;
    app->label_overlay_visible = 1;
    app->label_selected_emphasis_enabled = 1;
    app->label_selected_emphasis_strength = fmaxf(app->label_selected_emphasis_strength, 0.78f);
    app->semantic_cinematic_emphasis = 1;
    if (app->semantic_callout_opacity < 0.90f) app->semantic_callout_opacity = 0.90f;

    snprintf(app->semantic_edit_name, sizeof(app->semantic_edit_name), "%s", region->name);
    snprintf(app->guided_review_status, sizeof(app->guided_review_status),
             "Guided Review focused %s at slices X %.3f, Y %.3f, Z %.3f.",
             region->name,
             app->slice_x,
             app->slice_y,
             app->slice_z);
    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "Guided Review focused region %d of %d: %s.",
             step.step_number,
             step.step_count,
             region->name);
    return 1;
}

static void start_guided_review(StandaloneApp *app)
{
    if (!app) return;
    if (select_guided_review_region(app, 1)) {
        app->guided_review_visible = 1;
    }
}

static void update_render_performance_average(StandaloneApp *app)
{
    if (!app) return;
    app->render_ms_average = voxelpilot_update_render_ms_average(
        app->render_ms_average,
        app->last_render_ms,
        app->render_ms_average_ready);
    app->render_ms_average_ready = 1;
}

static void refresh_adaptive_quality_for_frame(StandaloneApp *app)
{
    VoxelPilotAdaptiveQualityInputs inputs;
    VoxelPilotAdaptiveQualityResult result;

    if (!app) return;

    voxelpilot_adaptive_quality_inputs_defaults(&inputs);
    inputs.enabled = app->adaptive_quality_enabled;
    inputs.interaction_active = app->camera_motion_active;
    inputs.cinematic_enabled = app->cinematic_enabled;
    inputs.base_step_size = app->step_size;
    inputs.base_skip_mult = app->skip_mult;
    inputs.current_multiplier = app->adaptive_quality_multiplier > 0.0f
                                    ? app->adaptive_quality_multiplier
                                    : 1.0f;
    inputs.render_ms_last = app->last_render_ms;
    inputs.render_ms_average = app->render_ms_average_ready
                                    ? app->render_ms_average
                                    : app->last_render_ms;

    if (!voxelpilot_update_adaptive_quality(&inputs, &result)) {
        app->adaptive_quality_multiplier = 1.0f;
        app->adaptive_quality_active = 0;
        app->adaptive_step_size = app->step_size;
        app->adaptive_skip_mult = app->skip_mult;
        snprintf(app->adaptive_quality_status,
                 sizeof(app->adaptive_quality_status),
                 "Adaptive quality unavailable: using the selected GPU preset.");
        return;
    }

    app->adaptive_quality_multiplier = result.multiplier;
    app->adaptive_quality_active = result.active;
    app->adaptive_step_size = result.step_size;
    app->adaptive_skip_mult = result.skip_mult;
    snprintf(app->adaptive_quality_status,
             sizeof(app->adaptive_quality_status),
             "%s",
             result.status);
}
static size_t estimate_current_volume_bytes(const StandaloneApp *app)
{
    if (!app || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        return 0;
    }
    return (size_t)app->renderer.W *
           (size_t)app->renderer.H *
           (size_t)app->renderer.D *
           sizeof(float);
}

static size_t estimate_current_label_mask_bytes(const StandaloneApp *app)
{
    if (!app || !app->label_mask ||
        app->label_mask_W < 1 || app->label_mask_H < 1 || app->label_mask_D < 1) {
        return 0;
    }
    return (size_t)app->label_mask_W *
           (size_t)app->label_mask_H *
           (size_t)app->label_mask_D;
}

static void refresh_performance_governor(StandaloneApp *app)
{
    VoxelPilotPerformanceInputs inputs;
    size_t free_bytes = 0;
    size_t total_bytes = 0;
    cudaError_t telemetry_status;

    if (!app) return;

    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = app->renderer.W;
    inputs.volume_height = app->renderer.H;
    inputs.volume_depth = app->renderer.D;
    inputs.render_width = app->render_w;
    inputs.render_height = app->render_h;
    inputs.estimated_volume_bytes = estimate_current_volume_bytes(app);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3u;
    inputs.estimated_label_mask_bytes = estimate_current_label_mask_bytes(app);
    inputs.estimated_render_target_bytes = voxelpilot_estimate_render_target_bytes(app->render_w, app->render_h);
    inputs.render_ms_last = app->last_render_ms;
    inputs.render_ms_average = app->render_ms_average_ready ? app->render_ms_average : app->last_render_ms;
    inputs.render_quality_mode = app->render_quality_mode;
    inputs.cinematic_enabled = app->cinematic_enabled;
    inputs.label_overlay_enabled = app->label_overlay_visible;
    inputs.label_mask_present = app->label_mask != NULL;
    inputs.semantic_callouts_enabled = app->semantic_callouts_enabled;
    inputs.semantic_callout_top_regions = app->semantic_callout_top_regions;
    inputs.semantic_region_count = app->semantic_region_count;

    telemetry_status = cudaMemGetInfo(&free_bytes, &total_bytes);
    if (telemetry_status == cudaSuccess && free_bytes > 0 && total_bytes > 0) {
        inputs.has_gpu_memory_telemetry = 1;
        inputs.gpu_memory_available = free_bytes;
        inputs.gpu_memory_total = total_bytes;
    }

    if (!voxelpilot_evaluate_performance(&inputs, &app->performance_result)) {
        voxelpilot_performance_result_defaults(&app->performance_result);
        snprintf(app->performance_status, sizeof(app->performance_status),
                 "Performance Safety: telemetry unavailable.");
        return;
    }

    snprintf(app->performance_status, sizeof(app->performance_status),
             "Performance Safety: %s (%d/100). %s",
             app->performance_result.status_label,
             app->performance_result.pressure_score,
             app->performance_result.recommendation);
}

void update_streaming_estimate(StandaloneApp *app)
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

static int performance_allows_optional_action(
    StandaloneApp *app,
    const char    *action_label,
    int            requires_asd,
    int            requires_mask_export)
{
    if (!app) return 0;

    refresh_performance_governor(app);

    if (requires_asd && !app->performance_result.safe_to_run_asd) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s paused by Performance Safety: %s",
                 action_label,
                 app->performance_result.recommendation);
        return 0;
    }

    if (requires_mask_export && !app->performance_result.safe_to_export_mask) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s paused by Performance Safety: full label-mask export is not recommended for this memory pressure. Use metadata/export snapshot first.",
                 action_label);
        return 0;
    }

    return 1;
}

static ImVec4 voxelpilot_score_color(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.5f) {
        float k = t / 0.5f;
        return ImVec4(0.85f, 0.35f + 0.45f * k, 0.25f, 1.0f);
    }
    {
        float k = (t - 0.5f) / 0.5f;
        return ImVec4(0.85f - 0.65f * k, 0.80f, 0.25f + 0.10f * k, 1.0f);
    }
}

static ImVec4 voxelpilot_pressure_color(int status)
{
    if (status == VOXELPILOT_GOVERNOR_CRITICAL) return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    if (status == VOXELPILOT_GOVERNOR_PRESSURE) return ImVec4(0.90f, 0.60f, 0.20f, 1.0f);
    if (status == VOXELPILOT_GOVERNOR_WATCH)    return ImVec4(0.85f, 0.80f, 0.25f, 1.0f);
    return ImVec4(0.35f, 0.85f, 0.55f, 1.0f);
}

static void voxelpilot_same_line_if_fits(const char *next_label)
{
    ImGuiStyle *style = &ImGui::GetStyle();
    ImVec2 label_size = ImGui::CalcTextSize(next_label);
    float next_width = label_size.x + style->FramePadding.x * 2.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > next_width + style->ItemSpacing.x) {
        ImGui::SameLine();
    }
}

static void draw_performance_safety_block(StandaloneApp *app)
{
    if (!app) return;

    refresh_performance_governor(app);

    ImGui::Separator();
    ImGui::Text("Performance Safety");
    ImGui::TextColored(voxelpilot_pressure_color(app->performance_result.status),
                "Status: %s", app->performance_result.status_label);
    {
        char pressure_overlay[16];
        snprintf(pressure_overlay, sizeof(pressure_overlay), "%d/100", app->performance_result.pressure_score);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, voxelpilot_pressure_color(app->performance_result.status));
        ImGui::ProgressBar(app->performance_result.pressure_score / 100.0f, ImVec2(120.0f, 0.0f), pressure_overlay);
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("%s", app->performance_result.primary_reason);
    ImGui::TextWrapped("Recommendation: %s", app->performance_result.recommendation);

    if (app->performance_result.suggest_interactive_gpu &&
        app->render_quality_mode != VOXELPILOT_RENDER_QUALITY_INTERACTIVE) {
        if (ImGui::Button("Use Interactive GPU")) {
            apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_INTERACTIVE);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety applied Interactive GPU. Return to Balanced GPU for screenshots when stable.");
            refresh_performance_governor(app);
        }
        ImGui::SameLine();
    }

    if (app->render_quality_mode == VOXELPILOT_RENDER_QUALITY_INTERACTIVE) {
        if (ImGui::Button("Return to Balanced GPU")) {
            apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_BALANCED);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety returned rendering to Balanced GPU.");
            refresh_performance_governor(app);
        }
    }

    if (app->performance_result.suggest_reduce_callouts &&
        app->semantic_callouts_enabled &&
        app->semantic_callout_top_regions > 1) {
        if (ImGui::Button("Limit Callouts to 1")) {
            app->semantic_callout_top_regions = 1;
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety limited cinematic callouts to one highlighted region.");
            refresh_performance_governor(app);
        }
    }

    if (ImGui::Button("Refresh Safety Check")) {
        refresh_performance_governor(app);
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s", app->performance_status);
    }
}

static void draw_ai_assist_panel(StandaloneApp *app)
{
    int has_volume;
    float suggested_center = 0.0f;
    float suggested_width = 0.0f;
    float suggested_opacity = 0.0f;
    int has_transfer_suggestion = 0;
    VoxelPilotTransferAdvisorResult transfer_advisor;
    int has_transfer_advisor = 0;

    if (!app || !app->ai_visible) {
        return;
    }

    has_volume =
        app->renderer.volume_data &&
        app->renderer.W > 0 &&
        app->renderer.H > 0 &&
        app->renderer.D > 0;

    memset(&transfer_advisor, 0, sizeof(transfer_advisor));
    if (has_volume && app->histogram_ready) {
        has_transfer_suggestion = voxelpilot_compute_auto_enhance_transfer(
            app->histogram,
            HISTOGRAM_BINS,
            &suggested_center,
            &suggested_width,
            &suggested_opacity);
        has_transfer_advisor = voxelpilot_compute_transfer_advisor(
            app->histogram,
            HISTOGRAM_BINS,
            app->tf_center,
            app->tf_width,
            app->tf_opacity_scale,
            app->semantic_region_count,
            &transfer_advisor);
    }

    set_default_window_layout(app, "AI Assist");
    bool ai_open = app->ai_visible != 0;
    if (ImGui::Begin("AI Assist", &ai_open)) {
        ImGui::Text("AI Assist");
        draw_help_marker("Local tools only: transfer suggestions, health checks, semantic discovery, and export helpers.");
        ImGui::Separator();

        if (!has_volume) {
            ImGui::TextWrapped(
                "AI Assist needs a loaded volume first: use Browse... in Controls to set "
                "Width, Height, Depth, and Data Type, then click Load Volume.");
            if (ImGui::Button("Show Me: Volume Import Step")) {
                app->walkthrough_visible = 1;
                app->walkthrough_step = 1;
            }
        } else {
            ImGui::Text("Volume: %d x %d x %d", app->renderer.W, app->renderer.H, app->renderer.D);
            ImGui::Text("Range: %.4f to %.4f", app->hist_min_value, app->hist_max_value);
            ImGui::Text("ASD: %d region(s), %d relation(s)", app->semantic_region_count, app->semantic_relation_count);
            ImGui::Text("ASD memory: %d correction(s)", app->semantic_correction_count);
            ImGui::Text("ASD safety: %s", app->performance_result.safe_to_run_asd ? "Ready" : "Blocked");
            if (app->performance_result.status != VOXELPILOT_GOVERNOR_HEALTHY) {
                draw_performance_safety_block(app);
            }
        }

        if (ImGui::CollapsingHeader("Discovery", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_help_marker("Adaptive Semantic Discovery finds candidate regions and labels them cautiously.");
        if (ImGui::Button("Discover Regions")) {
            if (performance_allows_optional_action(app, "ASD discovery", 1, 0)) {
                run_adaptive_semantic_discovery(app);
                refresh_performance_governor(app);
            }
        }
        voxelpilot_same_line_if_fits("Start Guided Review");
        if (ImGui::Button("Start Guided Review")) {
            start_guided_review(app);
        }
        voxelpilot_same_line_if_fits("Run Semantic Demo");
        if (ImGui::Button("Run Semantic Demo")) {
            if (performance_allows_optional_action(app, "Semantic demo", 1, 0)) {
                run_semantic_demo_workflow(app, "Semantic demo");
                refresh_performance_governor(app);
            }
        }

        ImGui::SliderInt("Max Regions", &app->semantic_max_regions, 1, MAX_ANNOTATIONS);
        ImGui::SliderFloat("Sensitivity", &app->semantic_sensitivity, 0.0f, 1.0f);
        ImGui::InputInt("Min Voxels", &app->semantic_min_region_voxels);
        if (app->semantic_min_region_voxels < 1) app->semantic_min_region_voxels = 1;

        ImGui::Separator();
        ImGui::Text("ASD Summary");
        ImGui::SameLine();
        draw_help_marker("Review the top discovered semantic regions and choose one to highlight.");
        ImGui::TextColored(
            ImVec4(0.55f, 0.62f, 0.68f, 1.0f),
            "Heuristic scores from shape/intensity geometry - not a trained model.");
        if (app->semantic_region_count > 0) {
            if (app->semantic_status[0]) {
                ImGui::TextWrapped("Status: %s", app->semantic_status);
            }
            ImGui::BeginGroup();
            int visible_regions = app->semantic_region_count < 3 ? app->semantic_region_count : 3;
            for (int i = 0; i < visible_regions; ++i) {
                VoxelPilotSemanticRegion *region = &app->semantic_regions[i];
                char label[128];
                snprintf(label, sizeof(label), "#%d %s", i + 1, region->name[0] ? region->name : "ASD candidate");
                bool selected = (app->semantic_selected_region == i);
                ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemClicked()) {
                    app->semantic_selected_region = i;
                }
                ImGui::SameLine();
                ImGui::Text("%s", region->archetype);
                ImGui::SameLine();
                {
                    char conf_overlay[16];
                    snprintf(conf_overlay, sizeof(conf_overlay), "%.0f%%", region->confidence * 100.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, voxelpilot_score_color(region->confidence));
                    ImGui::ProgressBar(region->confidence, ImVec2(52.0f, 0.0f), conf_overlay);
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine();
                {
                    char stab_overlay[16];
                    snprintf(stab_overlay, sizeof(stab_overlay), "%.0f%%", region->stability_score * 100.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, voxelpilot_score_color(region->stability_score));
                    ImGui::ProgressBar(region->stability_score, ImVec2(52.0f, 0.0f), stab_overlay);
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Archetype, confidence, and stability come from local shape/intensity heuristics\n"
                        "(connected components + geometry), not a trained model.");
                }
            }
            if (app->semantic_selected_region >= 0 && app->semantic_selected_region < app->semantic_region_count) {
                VoxelPilotSemanticRegion *selected = &app->semantic_regions[app->semantic_selected_region];
                ImGui::TextWrapped("Selected: %s | %s | %zu voxels", selected->name, selected->category, selected->voxel_count);
                if (selected->label_id != 0) {
                    const char *relation_note = semantic_relationship_note(app, selected->label_id);
                    if (relation_note[0]) {
                        ImGui::TextWrapped("Relation note: %s", relation_note);
                    }
                }
            }
            ImGui::EndGroup();
        } else {
            ImGui::TextWrapped("No ASD regions discovered yet. Click Discover Regions to run ASD-Lite.");
        }
        }

        if (ImGui::CollapsingHeader("Transfer & Health", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!has_volume || !app->histogram_ready) {
                ImGui::TextWrapped("Load a volume and build its histogram to enable transfer advice.");
            } else {
                if (ImGui::Button("Auto Enhance View")) {
                    apply_auto_enhance_transfer(app, "AI Assist");
                }
                voxelpilot_same_line_if_fits("Preview Auto Window");
                if (ImGui::Button("Preview Auto Window")) {
                    if (has_transfer_suggestion) {
                        snprintf(app->ai_assist_status,
                                 sizeof(app->ai_assist_status),
                                 "Preview - not applied: center %.3f, width %.3f, opacity %.2f.",
                                 suggested_center,
                                 suggested_width,
                                 suggested_opacity);
                        set_ui_toast(app, "Preview ready. No settings changed.");
                    } else {
                        snprintf(app->ai_assist_status,
                                 sizeof(app->ai_assist_status),
                                 "Auto window needs a populated histogram.");
                    }
                }
                if (app->transfer_undo_available) {
                    voxelpilot_same_line_if_fits("Restore Previous View");
                    if (ImGui::Button("Restore Previous View")) {
                        undo_transfer_settings(app);
                    }
                }
                ImGui::InputText("Prompt", app->ai_prompt, sizeof(app->ai_prompt));
                if (ImGui::Button("Apply Prompt")) {
                    apply_ai_prompt_to_app(app);
                }
                voxelpilot_same_line_if_fits("Health Check");
                if (ImGui::Button("Health Check")) {
                    update_dataset_health(app);
                }
                voxelpilot_same_line_if_fits("Export Bundle");
                if (ImGui::Button("Export Bundle")) {
                    if (performance_allows_optional_action(app, "AI review bundle", 1, 1)) {
                        export_ai_review_bundle(app);
                        refresh_performance_governor(app);
                    }
                }
            }
        }

        ImGui::TextWrapped("%s", app->ai_assist_status);

        if (ImGui::CollapsingHeader("More AI Tools")) {
            ImGui::Text("Advisor");
            ImGui::TextColored(
                ImVec4(0.55f, 0.62f, 0.68f, 1.0f),
                "Heuristic confidence from histogram shape - not a trained model.");
            if (!has_volume || !app->histogram_ready) {
                ImGui::TextWrapped("Load a volume to see AI Transfer Advisor recommendations here.");
            } else if (has_transfer_advisor) {
                ImGui::Text("%s | %.0f%%", transfer_advisor.action, transfer_advisor.confidence * 100.0f);
                if (transfer_advisor.has_recommendation) {
                    ImGui::Text("%.3f / %.3f / %.2f",
                                transfer_advisor.tf_center,
                                transfer_advisor.tf_width,
                                transfer_advisor.tf_opacity_scale);
                    if (ImGui::Button("Apply Advisor Settings")) {
                        apply_transfer_settings_with_undo(
                            app,
                            transfer_advisor.tf_center,
                            transfer_advisor.tf_width,
                            transfer_advisor.tf_opacity_scale,
                            "Advisor settings");
                    }
                }
            } else {
                ImGui::TextWrapped("Advisor could not summarize this histogram.");
            }

            ImGui::Separator();
            if (ImGui::Button("Refresh Quant Metrics")) {
                update_quant_metrics(app);
            }
            voxelpilot_same_line_if_fits("Export Insight Report");
            if (ImGui::Button("Export Insight Report")) {
                export_insight_report(app);
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
                             "Scout moved Axial Z to %.3f.",
                             app->ai_focus_slice);
                    set_ui_toast(app, "Focus slice updated.");
                } else {
                    snprintf(app->ai_assist_status,
                             sizeof(app->ai_assist_status),
                             "Focus scout needs a loaded 3D volume.");
                }
            }
        }
    }
    app->ai_visible = ai_open ? 1 : 0;
    ImGui::End();
}
static void draw_metadata_panel(StandaloneApp *app)
{
    if (!app->metadata_visible) {
        return;
    }

    size_t voxel_count =
        (size_t)app->renderer.W *
        (size_t)app->renderer.H *
        (size_t)app->renderer.D;

    set_default_window_layout(app, "Metadata");
    bool metadata_open = app->metadata_visible != 0;
    if (ImGui::Begin("Metadata", &metadata_open)) {
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
    if (app->dataset_health_ready) {
        ImGui::Text("Health: %s %.0f/100",
                    dataset_health_level_label(app->dataset_health.level),
                    app->dataset_health.score);
        ImGui::TextWrapped("Health summary: %s", app->dataset_health.summary);
    } else {
        ImGui::TextWrapped("Health summary: %s", app->dataset_health_status);
    }
    ImGui::TextWrapped("Dataset status: %s", app->upload_status);
    }
    app->metadata_visible = metadata_open ? 1 : 0;
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
    VoxelPilotSemanticRegion *semantic = NULL;

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
        semantic = find_semantic_region_by_label(app, label_id);
    }

    app->main_hover_active = 1;
    app->main_hover_label_id = label_id;
    if (semantic) {
        const char *note = semantic_relationship_note(app, label_id);
        snprintf(app->main_hover_description,
                 sizeof(app->main_hover_description),
                 "%s | %s | confidence %.0f%% | stability %.0f%% | source ASD-Lite. %s%s%s",
                 semantic->name,
                 semantic->archetype,
                 semantic->confidence * 100.0f,
                 semantic->stability_score * 100.0f,
                 semantic->description,
                 (note && note[0]) ? " " : "",
                 (note && note[0]) ? note : "");
    } else {
        voxelpilot_describe_voxel_context(
            app->main_hover_pick.intensity,
            label_id,
            region ? region->name : "",
            app->main_hover_description,
            sizeof(app->main_hover_description));
    }
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
    VoxelPilotSemanticRegion *semantic = NULL;

    if (!app->main_hover_active) {
        return;
    }

    region = find_annotation_by_label(app, app->main_hover_label_id);
    semantic = find_semantic_region_by_label(app, app->main_hover_label_id);
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
    if (semantic) {
        ImGui::Text("ASD rank: %d | importance %.0f%%",
                    semantic->importance_rank,
                    semantic->importance_score * 100.0f);
    }
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
    if (app->demo_auto_enabled) {
        ImGui::TextColored(
            ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
            "Auto Demo");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", app->demo_auto_status);
    }
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


static void update_auto_demo_sequence(StandaloneApp *app, double now)
{
    double t;
    double active_t;
    double denom;
    float phase;

    if (!app || !app->demo_auto_enabled) {
        return;
    }

    if (app->demo_auto_start_time <= 0.0) {
        app->demo_auto_start_time = now;
    }

    t = now - app->demo_auto_start_time;

    if (app->splash_visible) {
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Launching VoxelPilot and preparing the foot volume.");
        return;
    }

    active_t = t - 3.8;
    if (active_t < 0.0) active_t = 0.0;
    if (active_t > app->demo_auto_duration_sec) {
        active_t = app->demo_auto_duration_sec;
    }

    app->walkthrough_visible = 0;
    app->paused = 0;
    app->orbit_radius = 1.62f;
    app->orbit_target[0] = 0.5f;
    app->orbit_target[1] = 0.5f;
    app->orbit_target[2] = 0.5f;

    if (active_t < 5.0) {
        phase = (float)(active_t / 5.0);
        app->cinematic_enabled = 0;
        app->tf_palette = 0;
        app->orbit_yaw = -0.55f + phase * 0.85f;
        app->orbit_pitch = 0.18f + 0.16f * sinf(phase * 3.14159265f);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Loaded foot_256x256x256_uint8.raw | rotating across X/Y.");
    } else if (active_t < 12.0) {
        phase = (float)((active_t - 5.0) / 7.0);
        app->cinematic_enabled = 0;
        app->orbit_yaw = 0.30f + phase * 1.45f;
        app->orbit_pitch = 0.24f + 0.28f * sinf(phase * 3.14159265f);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Interactive CUDA orbit: X/Y rotation stays real-time.");
    } else if (active_t < 17.0) {
        phase = (float)((active_t - 12.0) / 5.0);
        app->cinematic_enabled = 1;
        app->cinematic_rim_strength = 0.45f + 0.10f * phase;
        app->cinematic_glow_strength = 0.32f + 0.08f * phase;
        app->cinematic_contrast = 1.16f + 0.08f * phase;
        app->orbit_yaw = 1.75f + phase * 0.65f;
        app->orbit_pitch = 0.32f + 0.16f * sinf(phase * 3.14159265f);
        clamp_cinematic_settings(app);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Cinematic Mode enabled: rim, glow, and contrast improve depth.");
    } else {
        denom = app->demo_auto_duration_sec - 17.0;
        if (denom < 1.0) denom = 1.0;
        phase = (float)((active_t - 17.0) / denom);
        if (phase < 0.0f) phase = 0.0f;
        if (phase > 1.0f) phase = 1.0f;
        if (!app->demo_auto_enhance_applied) {
            apply_auto_enhance_transfer(app, "Auto demo AI Assist");
            app->tf_palette = 1;
            app->demo_auto_enhance_applied = 1;
        }
        app->cinematic_enabled = 1;
        app->cinematic_rim_strength = fmaxf(app->cinematic_rim_strength, 0.55f);
        app->cinematic_glow_strength = fmaxf(app->cinematic_glow_strength, 0.40f);
        app->cinematic_contrast = fmaxf(app->cinematic_contrast, 1.24f);
        app->orbit_yaw = 2.40f + phase * 0.45f;
        app->orbit_pitch = 0.34f;
        clamp_cinematic_settings(app);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "AI Assist Auto Enhance applied: transfer and palette tuned for clarity.");
    }

    apply_orbit_camera(app);
    app->camera_motion_active = 1;

    if (t >= app->demo_auto_duration_sec + 5.0) {
        glfwSetWindowShouldClose(app->window, GLFW_TRUE);
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
    ImGui::Text("| Cinematic: %s", app->cinematic_enabled ? "On" : "Off");
    ImGui::SameLine();
    ImGui::Text("| Safety: %s", app->performance_result.status_label);
    ImGui::SameLine();
    ImGui::Text("| Measure: %.3f vox / %.3f u",
                measurement_distance(app),
                measurement_distance_world(app));
    ImGui::SameLine();
    ImGui::Text("| Preset: %s", app->preset_status);
    if (app->ui_toast[0]) {
        ImGui::SameLine();
        ImGui::Text("| %s", app->ui_toast);
    }
    ImGui::End();
}


static void draw_guided_review_overlay(StandaloneApp *app)
{
    const ImGuiViewport *viewport;
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;
    VoxelPilotSemanticRegion *region;
    const char *note = NULL;

    if (!app || !app->guided_review_visible) {
        return;
    }

    if (app->guided_review_step.region_index < 0 ||
        app->guided_review_step.region_index >= app->semantic_region_count) {
        if (!select_guided_review_region(app, 1)) {
            app->guided_review_visible = 0;
            return;
        }
    }

    region = &app->semantic_regions[app->guided_review_step.region_index];
    note = semantic_relationship_note(app, region->label_id);
    viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x - 456.0f,
               viewport->Pos.y + 92.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Always);

    {
        bool review_visible = app->guided_review_visible != 0;
        ImGui::Begin("Guided Review", &review_visible, flags);
        app->guided_review_visible = review_visible ? 1 : 0;
    }
    ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
                       "%s", app->guided_review_step.title);
    ImGui::Separator();
    ImGui::TextWrapped("%s", app->guided_review_step.cue);
    ImGui::Spacing();
    ImGui::Text("Archetype: %s", region->archetype[0] ? region->archetype : "unclassified");
    ImGui::Text("Confidence: %.0f%% | Stability: %.0f%% | Importance: %.0f%%",
                region->confidence * 100.0f,
                region->stability_score * 100.0f,
                region->importance_score * 100.0f);
    ImGui::Text("Centroid slices: X %.3f | Y %.3f | Z %.3f",
                app->slice_x,
                app->slice_y,
                app->slice_z);
    if (note && note[0]) {
        ImGui::TextWrapped("Relationship: %s", note);
    }
    ImGui::TextWrapped("Description: %s", region->description);
    ImGui::Separator();
    ImGui::TextWrapped("Review mode keeps the CUDA renderer intact: it only changes the selected region, slice focus, callout focus, and label glow.");

    if (ImGui::Button("Previous Region")) {
        select_guided_review_region(app, -1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Region")) {
        select_guided_review_region(app, 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Finish Review")) {
        app->guided_review_visible = 0;
        app->semantic_callout_selected_only = 0;
        if (app->semantic_callout_top_regions < 3) app->semantic_callout_top_regions = 3;
        snprintf(app->guided_review_status, sizeof(app->guided_review_status),
                 "Guided Review finished. Region overlays remain available for manual review.");
    }
    ImGui::End();
}

static void draw_cinematic_semantic_callouts(StandaloneApp *app)
{
    ImGuiViewport *viewport;
    ImDrawList *draw_list;
    float viewport_w;
    float viewport_h;
    int drawn = 0;
    int i;

    if (!app || !app->semantic_callouts_enabled || app->semantic_region_count < 1 ||
        app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        return;
    }

    viewport = ImGui::GetMainViewport();
    if (!viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f) {
        return;
    }

    viewport_w = viewport->Size.x;
    viewport_h = viewport->Size.y;
    draw_list = ImGui::GetForegroundDrawList(viewport);

    if (app->semantic_callout_top_regions < 1) app->semantic_callout_top_regions = 1;
    if (app->semantic_callout_top_regions > MAX_ANNOTATIONS) app->semantic_callout_top_regions = MAX_ANNOTATIONS;
    app->semantic_callout_opacity = voxelpilot_clampf(app->semantic_callout_opacity, 0.35f, 1.0f);

    for (i = 0; i < app->semantic_region_count && drawn < MAX_ANNOTATIONS; ++i) {
        VoxelPilotSemanticRegion *semantic = &app->semantic_regions[i];
        int selected = (i == app->semantic_selected_region);
        float point[3];
        float local_x = 0.0f;
        float local_y = 0.0f;
        float depth = 0.0f;
        float anchor_x;
        float anchor_y;
        float card_w = selected ? 260.0f : 230.0f;
        float card_h = selected ? 86.0f : 74.0f;
        float card_x;
        float card_y;
        float alpha = app->semantic_callout_opacity;
        ImU32 region_color;
        ImU32 line_color;
        ImU32 fill_color;
        ImU32 border_color;
        char title[128];
        char detail[192];
        char source[96];

        if (semantic->rejected || semantic->label_id == 0) {
            continue;
        }
        if (!voxelpilot_callout_should_show_rank(
                semantic->importance_rank,
                app->semantic_callout_top_regions,
                app->semantic_callout_selected_only,
                selected)) {
            continue;
        }

        point[0] = semantic->centroid[0] / fmaxf((float)(app->renderer.W - 1), 1.0f);
        point[1] = semantic->centroid[1] / fmaxf((float)(app->renderer.H - 1), 1.0f);
        point[2] = semantic->centroid[2] / fmaxf((float)(app->renderer.D - 1), 1.0f);
        if (!voxelpilot_project_point_to_screen(
                point,
                app->cam_pos,
                app->cam_dir,
                app->cam_up,
                app->fov_y_deg,
                viewport_w,
                viewport_h,
                &local_x,
                &local_y,
                &depth)) {
            continue;
        }

        anchor_x = viewport->Pos.x + local_x;
        anchor_y = viewport->Pos.y + local_y;

        region_color = ImGui::ColorConvertFloat4ToU32(ImVec4(
            semantic->color[0], semantic->color[1], semantic->color[2], alpha));
        line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(
            semantic->color[0], semantic->color[1], semantic->color[2], 0.88f * alpha));
        fill_color = ImGui::ColorConvertFloat4ToU32(ImVec4(0.035f, 0.055f, 0.070f, 0.84f * alpha));
        border_color = ImGui::ColorConvertFloat4ToU32(ImVec4(
            semantic->color[0], semantic->color[1], semantic->color[2], selected ? 1.0f : 0.70f));

        snprintf(title, sizeof(title), "%s", semantic->name[0] ? semantic->name : "ASD candidate");
        snprintf(detail, sizeof(detail), "%s | confidence %.0f%% | stability %.0f%%",
                 semantic->archetype,
                 semantic->confidence * 100.0f,
                 semantic->stability_score * 100.0f);
        snprintf(source, sizeof(source), "%s | rank %d",
                 semantic->source == VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED
                    ? "User-confirmed"
                    : "ASD-Lite",
                 semantic->importance_rank);

        const float card_padding = 12.0f;
        const float max_card_w = selected ? 320.0f : 280.0f;
        ImFont *font = (ImFont *)ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        const float available_text_width = max_card_w - card_padding * 2.0f;
        const char *title_text = title;
        const char *detail_text = detail;
        const char *source_text = source;
        ImVec2 title_size = ImGui::CalcTextSize(title_text, NULL, false, available_text_width);
        ImVec2 detail_size = ImGui::CalcTextSize(detail_text, NULL, false, available_text_width);
        ImVec2 source_size = ImGui::CalcTextSize(source_text, NULL, false, available_text_width);

        card_w = fmaxf(card_w, fminf(max_card_w, fmaxf(title_size.x, fmaxf(detail_size.x, source_size.x)) + card_padding * 2.0f));
        card_h = card_padding + title_size.y + 4.0f + detail_size.y + 4.0f + source_size.y + card_padding;
        card_h = fmaxf(card_h, selected ? 86.0f : 74.0f);

        card_x = anchor_x + ((local_x > viewport_w * 0.62f) ? -(card_w + 34.0f) : 34.0f);
        card_y = anchor_y - card_h * 0.5f + (float)(drawn % 3) * 12.0f;
        card_x = fmaxf(viewport->Pos.x + 12.0f, fminf(card_x, viewport->Pos.x + viewport_w - card_w - 12.0f));
        card_y = fmaxf(viewport->Pos.y + 36.0f, fminf(card_y, viewport->Pos.y + viewport_h - card_h - 44.0f));

        draw_list->AddCircleFilled(ImVec2(anchor_x, anchor_y), selected ? 8.0f : 6.0f, region_color, 24);
        draw_list->AddCircle(ImVec2(anchor_x, anchor_y), selected ? 15.0f : 12.0f, line_color, 32, selected ? 2.4f : 1.8f);
        draw_list->AddLine(
            ImVec2(anchor_x, anchor_y),
            ImVec2(card_x + (card_x > anchor_x ? 0.0f : card_w), card_y + card_h * 0.50f),
            line_color,
            selected ? 2.4f : 1.6f);
        draw_list->AddRectFilled(
            ImVec2(card_x, card_y),
            ImVec2(card_x + card_w, card_y + card_h),
            fill_color,
            12.0f);
        draw_list->AddRect(
            ImVec2(card_x, card_y),
            ImVec2(card_x + card_w, card_y + card_h),
            border_color,
            12.0f,
            0,
            selected ? 2.2f : 1.4f);

        draw_list->AddText(font, font_size,
                           ImVec2(card_x + card_padding, card_y + card_padding),
                           region_color,
                           title_text,
                           nullptr,
                           available_text_width);
        draw_list->AddText(font, font_size,
                           ImVec2(card_x + card_padding, card_y + card_padding + title_size.y + 4.0f),
                           IM_COL32(228, 236, 240, (int)(235.0f * alpha)),
                           detail_text,
                           nullptr,
                           available_text_width);
        draw_list->AddText(font, font_size,
                           ImVec2(card_x + card_padding, card_y + card_padding + title_size.y + detail_size.y + 8.0f),
                           IM_COL32(168, 218, 218, (int)(225.0f * alpha)),
                           source_text,
                           nullptr,
                           available_text_width);
        drawn++;
    }
}

static void draw_gui(StandaloneApp *app)
{
    if (app->splash_visible) {
        draw_startup_splash(app);
        return;
    }

    draw_top_ribbon(app);
    draw_dockspace(app);
    ensure_default_dock_layout(app);
    draw_demo_banner(app);
    draw_controls_panel(app);
    draw_insights_panel(app);
    draw_ai_assist_panel(app);
    draw_metadata_panel(app);
    draw_walkthrough_overlay(app);
    draw_guided_review_overlay(app);
    draw_status_bar(app);
    update_main_render_hover_pick(app);
    draw_main_render_hover_tooltip(app);
    draw_cinematic_semantic_callouts(app);
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
    const char   *review_bundle_folder = NULL;
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
    int           cli_cinematic_enabled = 0;
    int           cli_cinematic_rim_set = 0;
    int           cli_cinematic_glow_set = 0;
    int           cli_cinematic_contrast_set = 0;
    int           cli_semantic_demo = 0;
    int           cli_demo_auto = 0;
    double        cli_demo_duration_sec = 24.0;
    VoxelPilotCliExportPlan export_plan;
    float         cli_cinematic_rim = 0.35f;
    float         cli_cinematic_glow = 0.20f;
    float         cli_cinematic_contrast = 1.10f;
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
        } else if (strcmp(argv[i], "--review-bundle") == 0 &&
                   i+1 < argc) {
            review_bundle_folder = argv[++i];
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
        } else if (strcmp(argv[i], "--cinematic-lite") == 0) {
            cli_cinematic_enabled = 1;
        } else if (strcmp(argv[i], "--semantic-demo") == 0) {
            cli_semantic_demo = 1;
        } else if (strcmp(argv[i], "--demo-auto") == 0) {
            cli_demo_auto = 1;
        } else if (strcmp(argv[i], "--demo-duration") == 0 &&
                   i+1 < argc) {
            cli_demo_duration_sec = atof(argv[++i]);
            if (cli_demo_duration_sec < 8.0) cli_demo_duration_sec = 8.0;
            if (cli_demo_duration_sec > 60.0) cli_demo_duration_sec = 60.0;
        } else if (strcmp(argv[i], "--cinematic-rim") == 0 &&
                   i+1 < argc) {
            if (parse_cli_float(argv[++i], 0.0f, 1.0f, &cli_cinematic_rim) != 0) {
                fprintf(stderr, "Invalid value for --cinematic-rim: %s\n", argv[i]);
                return -1;
            }
            cli_cinematic_rim_set = 1;
        } else if (strcmp(argv[i], "--cinematic-glow") == 0 &&
                   i+1 < argc) {
            if (parse_cli_float(argv[++i], 0.0f, 1.0f, &cli_cinematic_glow) != 0) {
                fprintf(stderr, "Invalid value for --cinematic-glow: %s\n", argv[i]);
                return -1;
            }
            cli_cinematic_glow_set = 1;
        } else if (strcmp(argv[i], "--cinematic-contrast") == 0 &&
                   i+1 < argc) {
            if (parse_cli_float(argv[++i], 0.8f, 1.6f, &cli_cinematic_contrast) != 0) {
                fprintf(stderr, "Invalid value for --cinematic-contrast: %s\n", argv[i]);
                return -1;
            }
            cli_cinematic_contrast_set = 1;
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
                    strcmp(argv[i], "--review-bundle") == 0 ||
                    strcmp(argv[i], "--tf-center") == 0 ||
                    strcmp(argv[i], "--tf-width") == 0 ||
                    strcmp(argv[i], "--tf-opacity") == 0 ||
                    strcmp(argv[i], "--demo-duration") == 0 ||
                    strcmp(argv[i], "--cinematic-rim") == 0 ||
                    strcmp(argv[i], "--cinematic-glow") == 0 ||
                    strcmp(argv[i], "--cinematic-contrast") == 0) &&
                   i+1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return -1;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr,
                    "Usage: %s [--volume file.raw] [--dim N | --width W --height H --depth D] [--type uint8|uint16|float32] [--snapshot path.png] [--review-bundle folder] [--tf-center C --tf-width W --tf-opacity O] [--cinematic-lite --cinematic-rim R --cinematic-glow G --cinematic-contrast C] [--semantic-demo] [--demo-auto --demo-duration seconds]\n",
                    argv[0]);
            return -1;
        }
    }

    memset(&startup_spec, 0, sizeof(startup_spec));
    memset(&inferred_spec, 0, sizeof(inferred_spec));
    memset(startup_error, 0, sizeof(startup_error));
    memset(&export_plan, 0, sizeof(export_plan));
    voxelpilot_cli_make_export_plan(snapshot_path, review_bundle_folder, &export_plan);

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
    apply_default_workspace_state(&app, 1);

    if (cli_cinematic_enabled) app.cinematic_enabled = 1;
    if (cli_cinematic_rim_set) app.cinematic_rim_strength = cli_cinematic_rim;
    if (cli_cinematic_glow_set) app.cinematic_glow_strength = cli_cinematic_glow;
    if (cli_cinematic_contrast_set) app.cinematic_contrast = cli_cinematic_contrast;
    clamp_cinematic_settings(&app);
    if (cli_tf_center_set) app.tf_center = cli_tf_center;
    if (cli_tf_width_set) app.tf_width = cli_tf_width;
    if (cli_tf_opacity_set) app.tf_opacity_scale = cli_tf_opacity;

    app.demo_mode_enabled = 1;
    app.demo_auto_enabled = cli_demo_auto;
    app.demo_auto_start_time = 0.0;
    app.demo_auto_duration_sec = cli_demo_duration_sec;
    app.demo_auto_enhance_applied = 0;
    strcpy(app.demo_auto_status,
           cli_demo_auto
               ? "Auto demo armed: launching VoxelPilot."
               : "Manual demo mode ready.");
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
    app.transfer_undo_available = 0;
    strcpy(app.ui_toast, "");
    app.ai_focus_slice = 0.5f;
    app.ai_focus_score = 0.0f;
    strcpy(app.ai_prompt, "show high-density shell");
    strcpy(app.ai_assist_status,
           "AI Assist ready. Load a volume to run local review suggestions.");
    memset(app.semantic_corrections, 0, sizeof(app.semantic_corrections));
    app.semantic_correction_count = 0;
    strcpy(app.semantic_edit_name, "");
    strcpy(app.semantic_memory_path, "voxelpilot_asd_memory.json");
    strcpy(app.semantic_memory_status, "No ASD correction memory loaded yet.");
    app.slice_preview_dirty = 1;
    app.slice_prev_x = -1.0f;
    app.slice_prev_y = -1.0f;
    app.slice_prev_z = -1.0f;
    strcpy(app.hover_description, "Hover over a slice preview for context.");
    app.streaming_budget_bricks = 0;
    app.streaming_stream_now = 0;
    app.streaming_queue = 0;
    app.streaming_evictable = 0;
    strcpy(app.streaming_status, "Brick streaming telemetry pending.");
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
    if (!renderer_state_init(
            &app.renderer,
            NULL,
            app.upload_width,
            app.upload_height,
            app.upload_depth,
            brick_dim)) {
        snprintf(app.upload_status, sizeof(app.upload_status),
                 "Renderer init failed: %s",
                 app.renderer.last_error_message[0]
                     ? app.renderer.last_error_message
                     : "unknown CUDA error");
        fprintf(stderr, "renderer_state_init failed: %s\n",
                app.renderer.last_error_message);
    }

    if (startup_volume_data) {
        strcpy(app.upload_status, "Loading startup volume to GPU...");
        reload_volume(
            &app.renderer,
            startup_volume_data,
            (int)startup_spec.width,
            (int)startup_spec.height,
            (int)startup_spec.depth);
        update_histogram(&app);
        update_dataset_health(&app);
        apply_auto_enhance_on_load_if_safe(&app, "Startup volume");
        app.slice_preview_dirty = 1;
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
        update_dataset_health(&app);
        if (volume_path && startup_error[0]) {
            snprintf(app.upload_status,
                     sizeof(app.upload_status),
                     "Startup import failed: %s. Synthetic volume loaded instead.",
                     startup_error);
        }
    }

    if (cli_semantic_demo) {
        run_semantic_demo_workflow(&app, "CLI semantic demo");
    }

    if (export_plan.run_review_bundle) {
        if (app.render_w < 1024) app.render_w = 1024;
        if (app.render_h < 1024) app.render_h = 1024;
        set_camera_preset(&app,
            -0.35f, 0.95f, -0.35f,
            0.7f, -0.35f, 0.7f,
            0.0f, 1.0f, 0.0f);
        apply_auto_enhance_on_load_if_safe(&app, "AI review bundle");
        if (!export_ai_review_bundle_to_folder(&app, review_bundle_folder)) {
            fprintf(stderr, "AI review bundle export failed: %s\n", app.report_status);
            renderer_state_cleanup(&app.renderer);
            cudaDeviceReset();
            return -1;
        }
        printf("AI review bundle saved: %s\n", app.report_path);
        if (!export_plan.run_snapshot) {
            renderer_state_cleanup(&app.renderer);
            cudaDeviceReset();
            return 0;
        }
    }

    if (export_plan.run_snapshot) {
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
        io->FontGlobalScale = 1.06f;
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
        app.camera_motion_active = 0;
        update_auto_demo_sequence(&app, now);
        if (!app.splash_visible) {
            handle_keyboard_shortcuts(&app);
        }
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
