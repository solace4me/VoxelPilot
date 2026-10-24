#ifndef VOXELPILOT_STANDALONE_APP_H
#define VOXELPILOT_STANDALONE_APP_H

/* Shared state and cross-file declarations for the standalone app.
   Extracted from standalone_main.cu as part of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).
   Every file that is split out of standalone_main.cu includes this. */

#include <stdint.h>
#include <stddef.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "imgui.h"

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

/* ============================================================
   Constants
   ============================================================ */
#define DEFAULT_WIN_W    VOXELPILOT_DEFAULT_WIN_W
#define DEFAULT_WIN_H    VOXELPILOT_DEFAULT_WIN_H
#define DEFAULT_RENDER_W VOXELPILOT_DEFAULT_RENDER_W
#define DEFAULT_RENDER_H VOXELPILOT_DEFAULT_RENDER_H
#define MAX_PATH_BUF     1024
#define BRAND_TITLE      "VoxelPilot v2"
#define BRAND_SUBTITLE   "Cinematic AI Preview | NVIDIA Volume Explorer"
#define HISTOGRAM_BINS   128
#define MAX_ANNOTATIONS  VOXELPILOT_DEFAULT_MAX_ANNOTATIONS
#define ANNOTATION_NAME_MAX 64

enum {
    VOXELPILOT_RIBBON_HOME = 0,
    VOXELPILOT_RIBBON_VIEW = 1,
    VOXELPILOT_RIBBON_COUNT = 2
};

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
    GLint         tex_uniform_loc;
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

    /* UI: Cinematic render style */
    int           cinematic_enabled;
    float         cinematic_rim_strength;
    float         cinematic_glow_strength;
    float         cinematic_contrast;

    /* UI: Render params */
    float         step_size;
    float         threshold;
    float         skip_mult;
    int           render_w;
    int           render_h;
    int           render_match_window;
    int           render_quality_mode;
    int           adaptive_quality_enabled;
    int           adaptive_quality_active;
    float         adaptive_quality_multiplier;
    float         adaptive_step_size;
    float         adaptive_skip_mult;
    char          adaptive_quality_status[256];

    /* UI: Control */
    int           paused;

    /* Stats */
    float         last_render_ms;
    float         gui_fps;
    uint32_t      frame_id;
    float         render_ms_average;
    int           render_ms_average_ready;
    VoxelPilotPerformanceResult performance_result;
    char          performance_status[512];

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
    VoxelPilotDatasetHealthResult dataset_health;
    int           dataset_health_ready;
    char          dataset_health_status[512];
    float         ai_focus_slice;
    float         ai_focus_score;
    char          ai_assist_status[256];
    float         transfer_undo_center;
    float         transfer_undo_width;
    float         transfer_undo_opacity_scale;
    int           transfer_undo_available;
    char          ui_toast[256];
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
    int           label_selected_emphasis_enabled;
    float         label_selected_emphasis_strength;
    VoxelPilotSemanticRegion semantic_regions[VOXELPILOT_ASD_MAX_REGIONS];
    int           semantic_region_count;
    VoxelPilotRegionRelation semantic_relations[VOXELPILOT_ASD_MAX_RELATIONS];
    int           semantic_relation_count;
    int           semantic_selected_region;
    int           semantic_max_regions;
    int           semantic_min_region_voxels;
    float         semantic_sensitivity;
    int           semantic_cinematic_emphasis;
    int           semantic_callouts_enabled;
    int           semantic_callout_top_regions;
    int           semantic_callout_selected_only;
    float         semantic_callout_opacity;
    char          semantic_status[512];
    VoxelPilotSemanticCorrection semantic_corrections[VOXELPILOT_ASD_MAX_CORRECTIONS];
    int           semantic_correction_count;
    char          semantic_edit_name[VOXELPILOT_ASD_NAME_MAX];
    char          semantic_memory_status[512];
    char          semantic_memory_path[MAX_PATH_BUF];
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
    int           camera_motion_active;
    int           dock_layout_ready;
    float         dock_layout_viewport_w;
    float         dock_layout_viewport_h;
    int           dock_layout_variant;
    int           controls_visible;
    int           insights_visible;
    int           ai_visible;
    int           metadata_visible;
    int           ribbon_expanded;
    int           ribbon_active_tab;
    int           demo_mode_enabled;
    int           demo_auto_enabled;
    double        demo_auto_start_time;
    double        demo_auto_duration_sec;
    int           demo_auto_enhance_applied;
    char          demo_auto_status[192];
    int           walkthrough_visible;
    int           walkthrough_step;
    int           guided_review_visible;
    VoxelPilotGuidedReviewStep guided_review_step;
    char          guided_review_status[256];
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

    /* Dirty tracking for slice preview — skip CPU recompute when positions unchanged */
    float          slice_prev_x;
    float          slice_prev_y;
    float          slice_prev_z;
    int            slice_preview_dirty;

} StandaloneApp;

/* ============================================================
   Cross-file declarations

   These functions are defined in one file and called from another
   (standalone_main.cu <-> export_bundles.cpp today; more pairs will
   be added here as later split steps extract more files). Each was
   `static` when it lived alone in standalone_main.cu; moving a caller
   or a callee across a file boundary requires dropping `static` at
   the definition site too, since `static` gives internal (this-file-
   only) linkage in C/C++.
   ============================================================ */

/* Defined in standalone_main.cu, called from export_bundles.cpp */
void run_semantic_demo_workflow(StandaloneApp *app, const char *reason);
void update_quant_metrics(StandaloneApp *app);
void update_object_summary(StandaloneApp *app);
void update_streaming_estimate(StandaloneApp *app);
void render_frame_gpu_snapshot(StandaloneApp *app);
int write_png_file(
    const unsigned char *pixels,
    int                  width,
    int                  height,
    const char          *path);
float measurement_distance(const StandaloneApp *app);
float measurement_distance_world(const StandaloneApp *app);

/* Defined in annotation_and_semantic_memory.cpp, called from
   standalone_main.cu (specifically from run_adaptive_semantic_discovery
   and a handful of UI/reset call sites) */
void reset_semantic_discovery(StandaloneApp *app);
AnnotationRegion *find_annotation_by_label(
    StandaloneApp *app,
    unsigned char label_id);
VoxelPilotSemanticRegion *find_semantic_region_by_label(
    StandaloneApp *app,
    unsigned char label_id);
void reset_annotations(StandaloneApp *app);
int ensure_label_mask(StandaloneApp *app);
int point_to_voxel_index(
    const StandaloneApp *app,
    const float          point[3],
    int                 *out_x,
    int                 *out_y,
    int                 *out_z,
    size_t              *out_index);
void point_from_slice_uv(
    const StandaloneApp *app,
    int                  plane,
    float                u,
    float                v,
    float                point[3]);
const char *semantic_relationship_note(
    StandaloneApp *app,
    unsigned char  label_id);
size_t semantic_total_voxels(StandaloneApp *app);
void refresh_semantic_annotation_overlays(StandaloneApp *app);
void create_annotation_region_at_point(
    StandaloneApp *app,
    const float    point[3]);

/* Defined in export_bundles.cpp, called from standalone_main.cu */
int export_ai_review_bundle_to_folder(StandaloneApp *app, const char *folder);
void export_ai_review_bundle(StandaloneApp *app);
void export_insight_report(StandaloneApp *app);

/* Defined in standalone_main.cu, called from workspace_presets.cpp */
void clamp_cinematic_settings(StandaloneApp *app);
void sync_resolution_selection(StandaloneApp *app);

/* Defined in workspace_presets.cpp, called from standalone_main.cu */
int parse_cli_int(const char *text, int *out_value);
int parse_cli_float(
    const char *text,
    float       min_value,
    float       max_value,
    float      *out_value);
int parse_cli_data_type(const char *text, int *out_data_type);
int save_workspace_preset(StandaloneApp *app, const char *path);
int load_workspace_preset(StandaloneApp *app, const char *path);

/* Defined in standalone_main.cu, called from performance_safety_glue.cpp */
void apply_render_quality_preset(StandaloneApp *app, int quality_mode);

/* Defined in performance_safety_glue.cpp, called from standalone_main.cu.
   ImVec4 is declared in imgui.h, included transitively wherever this
   header is included from a file that already includes imgui.h
   (standalone_main.cu); performance_safety_glue.cpp includes it
   directly. */
void refresh_adaptive_quality_for_frame(StandaloneApp *app);
void refresh_performance_governor(StandaloneApp *app);
int performance_allows_optional_action(
    StandaloneApp *app,
    const char    *action_label,
    int            requires_asd,
    int            requires_mask_export);
ImVec4 voxelpilot_pressure_color(int status);
void draw_performance_safety_block(StandaloneApp *app);

/* Defined in standalone_main.cu, called from ui_ai_assist_panel.cpp.
   apply_ai_prompt_to_app, start_guided_review and
   undo_transfer_settings have no other caller today, but stayed
   here with their closely related siblings rather than being
   special-cased into ui_ai_assist_panel.cpp - see that file's
   header comment. */
const char *dataset_health_level_label(int level);
void update_dataset_health(StandaloneApp *app);
int clipping_is_active(const StandaloneApp *app);
void set_ui_toast(StandaloneApp *app, const char *message);
void apply_transfer_settings_with_undo(
    StandaloneApp *app,
    float          center,
    float          width,
    float          opacity_scale,
    const char    *label);
int undo_transfer_settings(StandaloneApp *app);
void draw_help_marker(const char *description);
void run_adaptive_semantic_discovery(StandaloneApp *app);
int apply_auto_enhance_transfer(StandaloneApp *app, const char *reason);
void apply_ai_prompt_to_app(StandaloneApp *app);
void start_guided_review(StandaloneApp *app);
extern const char *k_tf_palette_labels[];

/* Defined in ui_ai_assist_panel.cpp, called from standalone_main.cu */
void draw_ai_assist_panel(StandaloneApp *app);
void draw_metadata_panel(StandaloneApp *app);

/* Defined in standalone_main.cu, called from ui_controls_panel.cpp.
   refresh_upload_file_details, reset_workspace, and save_screenshot
   have no other caller today, but stayed here with their five
   closely related siblings rather than being split out separately -
   see ui_controls_panel.cpp's header comment. */
void update_histogram(StandaloneApp *app);
void set_camera_preset(
    StandaloneApp *app,
    float pos_x, float pos_y, float pos_z,
    float dir_x, float dir_y, float dir_z,
    float up_x,  float up_y,  float up_z);
void apply_orbit_camera(StandaloneApp *app);
void reset_workspace(StandaloneApp *app);
int clamp_import_dimension(int value);
void refresh_upload_file_details(StandaloneApp *app);
void save_screenshot(StandaloneApp *app);
int apply_auto_enhance_on_load_if_safe(StandaloneApp *app, const char *reason);
extern const ResolutionPreset k_resolution_presets[];
extern const int k_resolution_preset_count;
extern const char *k_render_quality_labels[];
extern const char *k_volume_data_type_labels[];

/* Defined in ui_controls_panel.cpp, called from standalone_main.cu */
void draw_controls_panel(StandaloneApp *app);
void handle_keyboard_shortcuts(StandaloneApp *app);

/* Defined in ui_ribbon_and_layout.cpp, called from standalone_main.cu
   and (for set_default_window_layout) from ui_ai_assist_panel.cpp and
   ui_controls_panel.cpp. */
void draw_dockspace(StandaloneApp *app);
void ensure_default_dock_layout(StandaloneApp *app);
void set_default_window_layout(
    const StandaloneApp *app,
    const char          *name);
void draw_top_ribbon(StandaloneApp *app);

#endif /* VOXELPILOT_STANDALONE_APP_H */
