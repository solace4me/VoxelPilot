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

const ResolutionPreset k_resolution_presets[] = {
    { "256 x 256", 256, 256 },
    { "512 x 512", 512, 512 },
    { "768 x 768", 768, 768 },
    { "1024 x 1024", 1024, 1024 },
    { "1280 x 720", 1280, 720 },
    { "1920 x 1080", 1920, 1080 }
};

const int k_resolution_preset_count =
    (int)(sizeof(k_resolution_presets) / sizeof(k_resolution_presets[0]));

const char *k_tf_palette_labels[] = {
    "Grayscale",
    "Bone",
    "Heat",
    "Cool"
};

const char *k_render_quality_labels[] = {
    "Interactive GPU",
    "Balanced GPU",
    "Quality GPU"
};



const char *k_volume_data_type_labels[] = {
    "uint8",
    "uint16",
    "float32"
};






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
static void update_render_performance_average(StandaloneApp *app);

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

void update_histogram(StandaloneApp *app)
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
const char *dataset_health_level_label(int level)
{
    if (level == VOXELPILOT_DATASET_HEALTH_ATTENTION) return "Attention";
    if (level == VOXELPILOT_DATASET_HEALTH_WATCH) return "Watch";
    return "Healthy";
}

void update_dataset_health(StandaloneApp *app)
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

int clipping_is_active(const StandaloneApp *app)
{
    return voxelpilot_clip_is_active(app->clip_min, app->clip_max);
}

void set_camera_preset(
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

void apply_orbit_camera(StandaloneApp *app)
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

void clamp_cinematic_settings(StandaloneApp *app)
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

void apply_render_quality_preset(StandaloneApp *app, int quality_mode)
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

void reset_workspace(StandaloneApp *app)
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

void sync_resolution_selection(StandaloneApp *app)
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

int clamp_import_dimension(int value)
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

void refresh_upload_file_details(StandaloneApp *app)
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


void run_adaptive_semantic_discovery(StandaloneApp *app)
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

void save_screenshot(StandaloneApp *app)
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

void set_ui_toast(StandaloneApp *app, const char *message)
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

void apply_transfer_settings_with_undo(
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

int undo_transfer_settings(StandaloneApp *app)
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

void draw_help_marker(const char *description)
{
    ImGui::SameLine();
    ImGui::TextDisabled("?");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", description ? description : "More information");
    }
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

int apply_auto_enhance_transfer(StandaloneApp *app, const char *reason)
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

int apply_auto_enhance_on_load_if_safe(StandaloneApp *app, const char *reason)
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

void apply_ai_prompt_to_app(StandaloneApp *app)
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

int select_guided_review_region(StandaloneApp *app, int direction)
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

void start_guided_review(StandaloneApp *app)
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
