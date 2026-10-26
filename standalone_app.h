#ifndef VOXELPILOT_STANDALONE_APP_H
#define VOXELPILOT_STANDALONE_APP_H

/* Shared state and cross-file declarations for the standalone app.
   Extracted from standalone_main.cpp as part of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).
   Every file that is split out of standalone_main.cpp includes this. */

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
/* Spec "Layout" table: a fixed 28 px status bar, outside the dockspace. */
#define STATUS_BAR_HEIGHT 28.0f
#define MAX_ANNOTATIONS  VOXELPILOT_DEFAULT_MAX_ANNOTATIONS
#define ANNOTATION_NAME_MAX 64
#define LOG_HISTORY_MAX  64
#define VOXELPILOT_LOG_INFO     0
#define VOXELPILOT_LOG_WARNING  1
#define VOXELPILOT_LOG_ERROR    2

enum {
    VOXELPILOT_RIBBON_HOME = 0,
    VOXELPILOT_RIBBON_VIEW = 1,
    VOXELPILOT_RIBBON_COUNT = 2
};

/* What the Scene panel's tree currently has selected (step 5 of
   docs/superpowers/plans/2026-10-24-unity-style-redesign.md). REGION and
   ANNOTATION use scene_selection_index as an index into
   semantic_regions[]/annotations[]; the singleton kinds (VOLUME,
   MEASUREMENT, CLIP_BOX, CAMERA, KEY_LIGHT) leave it at -1. This is the
   selection model a future Inspector (step 10) will read to decide which
   section to show - today only the Scene panel itself reads it. */
enum {
    VOXELPILOT_SCENE_SELECTION_NONE = 0,
    VOXELPILOT_SCENE_SELECTION_VOLUME,
    VOXELPILOT_SCENE_SELECTION_REGION,
    VOXELPILOT_SCENE_SELECTION_ANNOTATION,
    VOXELPILOT_SCENE_SELECTION_MEASUREMENT,
    VOXELPILOT_SCENE_SELECTION_CLIP_BOX,
    VOXELPILOT_SCENE_SELECTION_CAMERA,
    VOXELPILOT_SCENE_SELECTION_KEY_LIGHT
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

typedef struct {
    char          message[256];
    char          timestamp[12];
    int           severity;
} VoxelPilotLogEntry;

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
    /* Scene-tree eye icon for Clip box (step 5): hiding caches the real
       bounds here and forces clip_min/clip_max to the full 0..1 range
       (a real, immediate effect on the render, unlike the annotation/
       region eye icons below); showing restores them. */
    float         clip_cached_min[3];
    float         clip_cached_max[3];
    int           clip_hidden;
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
    VoxelPilotLogEntry log_history[LOG_HISTORY_MAX];
    int           log_history_count;
    int           log_history_next;
    char          ai_prompt[256];
    int           annotation_mode;
    char          annotation_name[ANNOTATION_NAME_MAX];
    float         annotation_tolerance;
    float         annotation_color[3];
    AnnotationRegion annotations[MAX_ANNOTATIONS];
    int           annotation_count;
    int           selected_annotation;
    /* Per-annotation Scene-tree eye icon (step 5). Toggling this hides
       the row in the tree (dimmed label, eye-off icon) and excludes the
       annotation from UI lists, but does NOT yet hide its voxels in the
       3D label overlay: that overlay blends by label id for every
       active label every frame (see attach_label_overlay_to_render_command
       in gl_viewport_and_render.cpp), and truly suppressing one label
       needs either a per-frame masked-copy upload (real GPU-upload cost)
       or a shader change - both out of scope for this step. Flagged
       rather than faked; see the plan doc's step 5 notes. */
    int           annotation_hidden[MAX_ANNOTATIONS];
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
    /* Per-region Scene-tree eye icon (step 5); same renderer limitation
       as annotation_hidden above - see its comment. */
    int           semantic_region_hidden[VOXELPILOT_ASD_MAX_REGIONS];
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
    /* The dockspace's own ImGuiID, set by draw_dockspace (ui_ribbon_and_
       layout.cpp) each frame before anything else needs it. Lets other
       overlays find the dockspace's central-node rect via
       ImGui::DockBuilderGetCentralNode(app->main_dockspace_id) instead of
       recomputing/re-deriving the id themselves - see the dockspace-id-
       mismatch bug this avoided a repeat of, docs/superpowers/plans/
       2026-10-24-unity-style-redesign.md. */
    ImGuiID       main_dockspace_id;
    /* The dockspace central node's rect (x, y, w, h) in ImGui/window
       coordinates, captured by draw_dockspace each frame. The CUDA render
       is sized to and drawn into this rect only - before this it filled
       the whole window behind every panel, so most of it was hidden and
       the volume sat off-centre in the visible gap. 0 until the first
       docked frame; callers fall back to the full framebuffer then. */
    float         viewport_rect[4];
    int           viewport_rect_valid;
    /* The last render command actually sent to the GPU. render_frame_
       standalone skips the ~30 ms raymarch when the new command is
       byte-identical (camera, transfer, clip, labels, size all unchanged),
       so an idle UI is not throttled to the render's frame rate. Anything
       that changes the volume itself must clear last_render_valid. */
    RendererInput last_render_cmd;
    int           last_render_valid;
    int           controls_visible;
    int           insights_visible;
    int           scene_visible;
    int           ribbon_expanded;
    int           ribbon_active_tab;
    /* Scene panel interaction state (step 5). */
    int           scene_selection_kind;   /* VOXELPILOT_SCENE_SELECTION_* */
    int           scene_selection_index;  /* region/annotation index, else -1 */
    char          scene_search_query[64];
    int           scene_rename_kind;      /* 0 = not renaming, else REGION/ANNOTATION */
    int           scene_rename_index;
    char          scene_rename_buffer[ANNOTATION_NAME_MAX];
    int           scene_undo_kind;        /* 0 = none, else REGION/ANNOTATION */
    int           scene_undo_index;
    char          scene_undo_label[ANNOTATION_NAME_MAX];
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

    /* Viewport slice-strip overlay (step 6, ui_viewport_overlay.cpp):
       -1 = showing the normal 3-up strip; 0/1/2 = that plane (Axial/
       Coronal/Sagittal) is enlarged to fill the viewport. */
    int            slice_strip_enlarged_plane;

} StandaloneApp;

/* ============================================================
   Cross-file declarations

   These functions are defined in one file and called from another
   (standalone_main.cpp <-> export_bundles.cpp today; more pairs will
   be added here as later split steps extract more files). Each was
   `static` when it lived alone in standalone_main.cpp; moving a caller
   or a callee across a file boundary requires dropping `static` at
   the definition site too, since `static` gives internal (this-file-
   only) linkage in C/C++.
   ============================================================ */

/* Defined in standalone_main.cpp, called from export_bundles.cpp */
void run_semantic_demo_workflow(StandaloneApp *app, const char *reason);
void update_quant_metrics(StandaloneApp *app);
void update_object_summary(StandaloneApp *app);
void update_streaming_estimate(StandaloneApp *app);
int write_png_file(
    const unsigned char *pixels,
    int                  width,
    int                  height,
    const char          *path);
float measurement_distance(const StandaloneApp *app);
float measurement_distance_world(const StandaloneApp *app);

/* Defined in annotation_and_semantic_memory.cpp, called from
   standalone_main.cpp (specifically from run_adaptive_semantic_discovery
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
/* confirmed = 1 keeps and remembers the region, 0 hides and remembers
   the rejection; both are saved to semantic_memory_path. */
int teach_semantic_region(StandaloneApp *app, int index, int confirmed);
void load_semantic_memory_if_present(StandaloneApp *app);
void create_annotation_region_at_point(
    StandaloneApp *app,
    const float    point[3]);

/* Defined in export_bundles.cpp, called from standalone_main.cpp */
int export_ai_review_bundle_to_folder(StandaloneApp *app, const char *folder);
void export_ai_review_bundle(StandaloneApp *app);
void export_insight_report(StandaloneApp *app);

/* Defined in standalone_main.cpp, called from workspace_presets.cpp */
void clamp_cinematic_settings(StandaloneApp *app);
void sync_resolution_selection(StandaloneApp *app);

/* Defined in workspace_presets.cpp, called from standalone_main.cpp */
int parse_cli_int(const char *text, int *out_value);
int parse_cli_float(
    const char *text,
    float       min_value,
    float       max_value,
    float      *out_value);
int parse_cli_data_type(const char *text, int *out_data_type);
int save_workspace_preset(StandaloneApp *app, const char *path);
int load_workspace_preset(StandaloneApp *app, const char *path);

/* Defined in standalone_main.cpp, called from performance_safety_glue.cpp */
void apply_render_quality_preset(StandaloneApp *app, int quality_mode);

/* Defined in performance_safety_glue.cpp, called from standalone_main.cpp.
   ImVec4 is declared in imgui.h, included transitively wherever this
   header is included from a file that already includes imgui.h
   (standalone_main.cpp); performance_safety_glue.cpp includes it
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

/* Defined in standalone_main.cpp, called from ui_ai_assist_panel.cpp.
   apply_ai_prompt_to_app, start_guided_review and
   undo_transfer_settings have no other caller today, but stayed
   here with their closely related siblings rather than being
   special-cased into ui_ai_assist_panel.cpp - see that file's
   header comment. */
const char *dataset_health_level_label(int level);
void update_dataset_health(StandaloneApp *app);
int clipping_is_active(const StandaloneApp *app);
void set_ui_toast(StandaloneApp *app, const char *message, int severity = VOXELPILOT_LOG_INFO);
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

/* Defined in ui_ai_assist_panel.cpp, called from standalone_main.cpp */
void draw_ai_assist_panel(StandaloneApp *app);
void draw_metadata_panel(StandaloneApp *app);

/* Defined in standalone_main.cpp, called from ui_controls_panel.cpp.
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

/* Defined in ui_controls_panel.cpp, called from standalone_main.cpp
   (and, for browse_volume_file, from ui_scene_panel.cpp's empty-state
   "Open volume" button - step 5). */
void draw_controls_panel(StandaloneApp *app);
void handle_keyboard_shortcuts(StandaloneApp *app);
int browse_volume_file(StandaloneApp *app);

/* Defined in ui_ribbon_and_layout.cpp, called from standalone_main.cpp
   and (for set_default_window_layout) from ui_ai_assist_panel.cpp and
   ui_controls_panel.cpp. */
void draw_dockspace(StandaloneApp *app);
/* Defined in ui_viewport_overlay.cpp, called from standalone_main.cpp
   right after draw_dockspace - step 6 of docs/superpowers/plans/
   2026-10-24-unity-style-redesign.md, the interactive orthogonal
   slice strip anchored to the viewport's own dock node. */
void draw_viewport_slice_strip(StandaloneApp *app);
/* Takes the dockspace's ImGuiID (computed inside DockSpaceRoot's
   Begin/End by draw_dockspace, which also calls this) rather than
   recomputing it internally via ImGui::GetID() with no window active -
   see the comment at the call site in ui_ribbon_and_layout.cpp. */
void ensure_default_dock_layout(StandaloneApp *app, ImGuiID dockspace_id);
void set_default_window_layout(
    const StandaloneApp *app,
    const char          *name);
void draw_top_ribbon(StandaloneApp *app);

/* Defined in standalone_main.cpp, called from ui_demo_and_onboarding.cpp */
int select_guided_review_region(StandaloneApp *app, int direction);

/* Defined in standalone_main.cpp, called from gl_viewport_and_render.cpp */
void sync_orbit_from_camera(StandaloneApp *app);

/* Defined in ui_demo_and_onboarding.cpp, called from standalone_main.cpp */
void draw_demo_banner(StandaloneApp *app);
void draw_walkthrough_overlay(StandaloneApp *app);
void update_startup_splash(StandaloneApp *app);
void update_auto_demo_sequence(StandaloneApp *app, double now);
void draw_startup_splash(StandaloneApp *app);
void draw_status_bar(StandaloneApp *app);
void draw_guided_review_overlay(StandaloneApp *app);
void draw_cinematic_semantic_callouts(StandaloneApp *app);

/* Defined in gl_viewport_and_render.cpp, called from standalone_main.cpp
   (and, for render_frame_gpu_snapshot, from export_bundles.cpp). */
int setup_gl_resources(StandaloneApp *app);
void process_mouse_camera_controls(StandaloneApp *app);
void sync_render_target_to_framebuffer(
    StandaloneApp *app,
    int            framebuffer_width,
    int            framebuffer_height);
/* The 3D viewport (dockspace central node) in framebuffer pixels, GL
   convention: out[0..3] = x, y from the bottom, width, height. Falls back
   to the whole framebuffer before the first docked frame. */
void viewport_framebuffer_rect(
    const StandaloneApp *app,
    int                  framebuffer_width,
    int                  framebuffer_height,
    int                  out_rect[4]);
void update_slice_previews(StandaloneApp *app);
void render_frame_gpu_snapshot(StandaloneApp *app);
void render_frame_standalone(StandaloneApp *app);
void draw_fullscreen_quad(StandaloneApp *app);
void update_main_render_hover_pick(StandaloneApp *app);
void draw_main_render_hover_tooltip(StandaloneApp *app);

/* Defined in ui_bottom_panels.cpp, called from standalone_main.cpp.
   Step 2 of docs/superpowers/plans/2026-10-24-unity-style-redesign.md -
   only Histogram and Log land here this step; AI Assist and Metadata
   join as tabs in step 3. */
void draw_bottom_panels(StandaloneApp *app);

/* Defined in ui_scene_panel.cpp, called from standalone_main.cpp.
   Step 5 of docs/superpowers/plans/2026-10-24-unity-style-redesign.md -
   the Scene tree over existing annotation/region/measurement/clip-box
   data, per the spec's "Scene panel" region. */
void draw_scene_panel(StandaloneApp *app);

#endif /* VOXELPILOT_STANDALONE_APP_H */
