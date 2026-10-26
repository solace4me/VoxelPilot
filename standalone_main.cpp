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
#include <time.h>

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
    /* ImGuiCol_TabActive was renamed to ImGuiCol_TabSelected in 1.90.9;
       the old name's compat alias is commented out upstream (see
       imgui.h's ImGuiCol_ obsolete-enum block), so it has to be the new
       name here now that imgui/ is the real docking branch (commit
       cee6a89, docs/superpowers/plans/2026-10-24-unity-style-redesign.md). */
    colors[ImGuiCol_TabSelected] = ImVec4(0.130f, 0.250f, 0.270f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.95f, 0.97f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.66f, 0.72f, 1.0f);
}


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

void sync_orbit_from_camera(StandaloneApp *app)
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
    app->scene_visible = 1;
    /* -1 = showing the normal 3-up strip, not a single enlarged plane
       (step 6, ui_viewport_overlay.cpp). 0 is a valid plane index
       (Axial), so this can't just rely on zero-init. */
    app->slice_strip_enlarged_plane = -1;
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

/* draw_slice_card used to live here (static, called only from
   draw_insights_panel below). Step 6 of docs/superpowers/plans/
   2026-10-24-unity-style-redesign.md relocates the orthogonal slice
   strip to an interactive overlay anchored to the viewport itself -
   see draw_viewport_slice_strip in the new ui_viewport_overlay.cpp,
   called from draw_gui below. */


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

/* Scene panel empty-state drop zone (step 5 of
   docs/superpowers/plans/2026-10-24-unity-style-redesign.md): OS-level
   file drop onto the VoxelPilot window. A .vpilot path loads it as a
   workspace, same as File > Open Workspace; anything else is treated as
   a volume file path, same as Controls' Browse... button - the user
   still reviews Width/Height/Depth/Data Type and clicks Load Volume
   afterward, this just fills in the path. Only paths[0] is used; a
   multi-file drop ignores the rest. */
static void drop_callback(
    GLFWwindow  *window,
    int          count,
    const char **paths)
{
    StandaloneApp *app;
    const char    *path;
    const char    *ext;

    app = (StandaloneApp *)glfwGetWindowUserPointer(window);
    if (!app || count < 1 || !paths || !paths[0]) {
        return;
    }

    path = paths[0];
    ext = strrchr(path, '.');
    if (ext && (strcmp(ext, ".vpilot") == 0 || strcmp(ext, ".VPILOT") == 0)) {
        if (load_workspace_preset(app, path) == 0) {
            app->dock_layout_ready = 0;
            set_ui_toast(app, "Workspace loaded.");
        } else {
            set_ui_toast(app, "Could not load workspace file.", VOXELPILOT_LOG_ERROR);
        }
        return;
    }

    strncpy(app->upload_path, path, sizeof(app->upload_path) - 1);
    app->upload_path[sizeof(app->upload_path) - 1] = '\0';
    refresh_upload_file_details(app);
    snprintf(app->upload_status, sizeof(app->upload_status),
             "File selected. Review import settings, then load volume.");
    set_ui_toast(app, "Volume file selected.");
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


void set_ui_toast(StandaloneApp *app, const char *message, int severity)
{
    if (!app) return;
    snprintf(app->ui_toast, sizeof(app->ui_toast), "%s", message ? message : "");

    if (message && message[0] != '\0') {
        VoxelPilotLogEntry *entry = &app->log_history[app->log_history_next];
        time_t now = time(NULL);
        struct tm local_tm;
        snprintf(entry->message, sizeof(entry->message), "%s", message);
        entry->severity = severity;
        localtime_s(&local_tm, &now);
        strftime(entry->timestamp, sizeof(entry->timestamp), "%H:%M:%S", &local_tm);
        app->log_history_next = (app->log_history_next + 1) % LOG_HISTORY_MAX;
        if (app->log_history_count < LOG_HISTORY_MAX) {
            app->log_history_count++;
        }
    }
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

/* draw_insights_panel is retired (step 10): its slice row moved to the
   docked Slices strip (ui_viewport_overlay.cpp) and its measurement
   summary to the Measurement inspector (ui_controls_panel.cpp).
   app->insights_visible stays only so older .vpilot files still load. */

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


static void draw_gui(StandaloneApp *app)
{
    if (app->splash_visible) {
        draw_startup_splash(app);
        return;
    }

    draw_top_ribbon(app);
    /* draw_dockspace now also calls ensure_default_dock_layout itself,
       against the dockspace id it computes inside DockSpaceRoot's own
       Begin/End - see both functions in ui_ribbon_and_layout.cpp. It used
       to be called separately, from out here with no ImGui window active,
       which is what let the dockspace id mismatch slip in. */
    draw_dockspace(app);
    /* Reads app->main_dockspace_id (set by draw_dockspace just above) to
       anchor itself to the dockspace's actual central-node rect - must
       run after draw_dockspace has built the layout this frame. Step 6,
       ui_viewport_overlay.cpp. */
    draw_viewport_slice_strip(app);
    draw_demo_banner(app);
    draw_scene_panel(app);
    draw_controls_panel(app);
    draw_bottom_panels(app);
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
    int           cli_demo_mode = 0;
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
        } else if (strcmp(argv[i], "--demo") == 0) {
            cli_demo_mode = 1;
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
                    "Usage: %s [--volume file.raw] [--dim N | --width W --height H --depth D] [--type uint8|uint16|float32] [--snapshot path.png] [--review-bundle folder] [--tf-center C --tf-width W --tf-opacity O] [--cinematic-lite --cinematic-rim R --cinematic-glow G --cinematic-contrast C] [--demo] [--semantic-demo] [--demo-auto --demo-duration seconds]\n",
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

    /* The presenter banner is for demos only; an everyday launch opens
       straight into the workstation. release/.../Run_VoxelPilot.bat
       passes --demo so the packaged presentation build is unchanged. */
    app.demo_mode_enabled = cli_demo_mode || cli_semantic_demo || cli_demo_auto;
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
    /* Before any discovery runs, so what the user taught in earlier
       sessions (Inspector "Is this right?") is applied from the start. */
    load_semantic_memory_if_present(&app);
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
        /* Auto-enhance only when the user did not choose a transfer on the
           command line - it used to run regardless and silently replace
           --tf-center/--tf-width/--tf-opacity. */
        if (!cli_tf_center_set && !cli_tf_width_set && !cli_tf_opacity_set) {
            apply_auto_enhance_on_load_if_safe(&app, "Startup volume");
        }
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
        /* Same rule as at startup: an explicit --tf-* choice wins. */
        if (!cli_tf_center_set && !cli_tf_width_set && !cli_tf_opacity_set) {
            apply_auto_enhance_on_load_if_safe(&app, "Snapshot");
        }
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
    glfwSetDropCallback(app.window, drop_callback);

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
        /* The spec's UI typeface is a proportional sans; ImGui's built-in
           ProggyClean is a bitmap monospace font, which is most of why the
           app looked like a terminal. Use Segoe UI (on every Windows
           install) when present, otherwise keep the built-in font. */
        {
            const char *ui_font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
            FILE *font_probe = fopen(ui_font_path, "rb");
            if (font_probe) {
                fclose(font_probe);
                if (io->Fonts->AddFontFromFileTTF(ui_font_path, 16.0f)) {
                    io->FontGlobalScale = 1.0f;
                }
            }
        }
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
        {
            /* Draw the render into the 3D viewport only, not the whole
               window behind the panels. */
            int vp[4];
            viewport_framebuffer_rect(&app, fb_w, fb_h, vp);
            glViewport(vp[0], vp[1], vp[2], vp[3]);
            draw_fullscreen_quad(&app);
            glViewport(0, 0, fb_w, fb_h);
        }

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
