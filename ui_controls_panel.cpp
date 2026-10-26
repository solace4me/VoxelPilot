/* Inspector (formerly the Controls panel): volume browse/load, the
   selection-driven Inspector window (see the comment above
   draw_controls_panel's helpers below), and the app's keyboard
   shortcuts. The function keeps its draw_controls_panel name and the
   controls_visible flag; its window is now titled "Inspector".
   Extracted from standalone_main.cu as part of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   load_selected_volume stays static here - a private helper with no
   caller outside this file. browse_volume_file was also private until
   step 5 (docs/superpowers/plans/2026-10-24-unity-style-redesign.md),
   which dropped its `static` so the Scene panel's empty-state "Open
   volume" button (ui_scene_panel.cpp) could call it too - see its
   cross-file declaration in standalone_app.h.

   Of what handle_keyboard_shortcuts and draw_controls_panel call that
   isn't a common/*.h library function: apply_render_quality_preset,
   clamp_cinematic_settings, draw_help_marker, load_workspace_preset,
   measurement_distance, measurement_distance_world, reset_annotations,
   save_workspace_preset, set_default_window_layout, set_ui_toast,
   sync_resolution_selection, and update_dataset_health were already
   cross-file before this step (steps 1-5). apply_auto_enhance_on_load_if_safe,
   apply_orbit_camera, clamp_import_dimension, refresh_upload_file_details,
   reset_workspace, save_screenshot, set_camera_preset, and
   update_histogram are newly made cross-file by this step - each still
   has callers elsewhere in standalone_main.cu's remaining code except
   refresh_upload_file_details, reset_workspace, and save_screenshot,
   which have none today; those three stayed with their five siblings
   for consistency rather than being split out separately, the same
   judgment call made for step 5's leftover trio. The k_resolution_presets,
   k_resolution_preset_count, k_render_quality_labels, and
   k_volume_data_type_labels tables (plus k_tf_palette_labels, already
   made extern during step 5's build fix) are read directly, not
   through a function call, and are now extern for the same reason. */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tinyfiledialogs.h"

#include "standalone_app.h"
#include "imgui_internal.h"   /* ImGui::MarkItemEdited, for the transfer
                                  editor's undo-on-release */

int browse_volume_file(StandaloneApp *app)
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
    char fit_message[320];
    int fit_factor = 1;
    int loaded_w = 0, loaded_h = 0, loaded_d = 0;

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
        set_ui_toast(app, "Select a volume file first.", VOXELPILOT_LOG_WARNING);
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
        set_ui_toast(app, "Volume load failed.", VOXELPILOT_LOG_ERROR);
        return 0;
    }

    volume_import_expected_bytes(&spec, &expected);

    /* Fit the GPU first: downsample if full resolution will not fit,
       refuse with the numbers if nothing will. */
    {
        int w = (int)spec.width;
        int h = (int)spec.height;
        int d = (int)spec.depth;

        fit_factor = fit_volume_to_gpu_budget(app, &temp, &w, &h, &d, fit_message, sizeof(fit_message));
        if (fit_factor == 0) {
            snprintf(app->upload_status, sizeof(app->upload_status), "%s", fit_message);
            set_ui_toast(app, "Volume too large for this GPU.", VOXELPILOT_LOG_ERROR);
            volume_import_free(temp);
            return 0;
        }
        loaded_w = w;
        loaded_h = h;
        loaded_d = d;
    }

    strcpy(app->upload_status, "Loading to GPU...");
    reload_volume(
        &app->renderer,
        temp,
        loaded_w,
        loaded_h,
        loaded_d);
    /* New voxels, possibly with an identical render command: force the
       next frame to raymarch (see render_frame_standalone). */
    app->last_render_valid = 0;
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
    if (fit_factor > 1) {
        snprintf(app->upload_status, sizeof(app->upload_status), "%s", fit_message);
    }
    volume_import_free(temp);
    set_ui_toast(app, fit_factor > 1 ? "Volume loaded at reduced resolution to fit this GPU."
                                     : "Volume loaded.");
    return 1;
}

void handle_keyboard_shortcuts(StandaloneApp *app)
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

/* ============================================================
   Inspector (step 10 of docs/superpowers/plans/2026-10-24-unity-style-
   redesign.md; spec "Region by region > Inspector").

   draw_controls_panel now draws the selection-driven Inspector: the
   sections shown depend on what is selected in the Scene panel
   (app->scene_selection_kind/index). Nothing selected shows the Volume,
   so the panel is never empty.

   Every section header carries a one-line summary of its current values
   (so a collapsed section still says what it is set to); when a section
   differs from the app defaults the summary turns accent-coloured and a
   Reset button appears on the header. The transfer function is edited
   directly on the histogram (drag the window, its edges, or up/down for
   opacity), and every transfer edit - drag, slider, palette, reset -
   feeds the same transfer_undo_* state the AI Assist "Last action" card
   undoes. Discovered regions get an "Is this right?" answer that is
   written to ASD memory (teach_semantic_region), so the next discovery
   labels similar regions the way the user said.
   ============================================================ */

static const ImVec4 kInspectorSecondary = ImVec4(0.624f, 0.690f, 0.737f, 1.0f); /* text/secondary #9FB0BC */
static const ImVec4 kInspectorAccent    = ImVec4(0.310f, 0.820f, 0.749f, 1.0f); /* accent #4FD1BF */
static const ImVec4 kInspectorHighlight = ImVec4(0.949f, 0.812f, 0.357f, 1.0f); /* highlight #F2CF5B */

static float s_tf_before[3];

static float inspector_label_width(void)
{
    return ImGui::GetFontSize() * 6.5f;
}

/* Spec slider row: label on the left in text/secondary, widget filling
   the rest of the row. Callers pass "##id" labels to their widgets. */
static void inspector_label(const char *label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kInspectorSecondary, "%s", label);
    ImGui::SameLine(inspector_label_width());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/* Collapsible section header with a right-aligned summary and, when the
   section is modified, a Reset button. Returns whether it is open. */
static bool inspector_section(
    const char *id,
    const char *title,
    const char *summary,
    bool        default_open,
    bool        modified,
    bool       *reset_clicked)
{
    char         label[96];
    bool         open;
    ImVec2       header_min, header_max;
    ImGuiStyle  *style = &ImGui::GetStyle();
    float        reset_w = 0.0f;

    snprintf(label, sizeof(label), "%s###insp_%s", title, id);
    open = ImGui::CollapsingHeader(
        label,
        ImGuiTreeNodeFlags_AllowOverlap |
        (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0));
    header_min = ImGui::GetItemRectMin();
    header_max = ImGui::GetItemRectMax();

    if (modified && reset_clicked) {
        ImVec2 saved = ImGui::GetCursorScreenPos();
        /* SmallButton has no vertical frame padding: it is one text line
           tall, so centre that height on the header. */
        float  button_h = ImGui::GetFontSize();

        reset_w = ImGui::CalcTextSize("Reset").x + style->FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(
            header_max.x - reset_w - style->FramePadding.x,
            header_min.y + (header_max.y - header_min.y - button_h) * 0.5f));
        ImGui::PushID(id);
        if (ImGui::SmallButton("Reset")) {
            *reset_clicked = true;
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(saved);
        reset_w += style->ItemSpacing.x;
    }

    if (summary && summary[0]) {
        ImVec2 text_size = ImGui::CalcTextSize(summary);
        float  x = header_max.x - style->FramePadding.x - reset_w - text_size.x;
        /* Header text starts after the frame padding and the arrow; leave
           a clear gap after the title, and drop the summary entirely
           rather than let it run into the title on a narrow panel. */
        float  title_end = header_min.x + style->FramePadding.x * 2.0f + ImGui::GetFontSize() * 2.0f +
                           ImGui::CalcTextSize(title).x + 16.0f;

        if (x > title_end) {
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(x, header_min.y + (header_max.y - header_min.y - text_size.y) * 0.5f),
                ImGui::GetColorU32(modified ? kInspectorAccent : kInspectorSecondary),
                summary);
        }
    }
    return open;
}

/* Equal-width segmented control; returns the clicked index or -1. */
static int inspector_segmented(const char *id, const char *const *labels, int count, int current)
{
    float spacing = 4.0f;
    float width = (ImGui::GetContentRegionAvail().x - spacing * (float)(count - 1)) / (float)count;
    int   clicked = -1;
    int   i;

    ImGui::PushID(id);
    for (i = 0; i < count; ++i) {
        bool selected = (i == current);
        if (i > 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.122f, 0.227f, 0.239f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        if (ImGui::Button(labels[i], ImVec2(width, 0.0f))) {
            clicked = i;
        }
        if (selected) {
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::PopID();
    return clicked;
}

static int inspector_has_volume(const StandaloneApp *app)
{
    return app->renderer.volume_data &&
           app->renderer.W > 0 && app->renderer.H > 0 && app->renderer.D > 0;
}

static const char *inspector_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    return slash ? slash + 1 : path;
}

static void inspector_focus_camera_on(StandaloneApp *app, const float point_norm[3])
{
    /* orbit_target is world space; point_norm is normalized volume
       coordinates (see common/volume_geometry.h). */
    float extent[3];
    voxelpilot_volume_box_extent(app->renderer.W, app->renderer.H, app->renderer.D,
                                 app->voxel_spacing, extent);
    voxelpilot_volume_point_to_world(extent, point_norm, app->orbit_target);
    apply_orbit_camera(app);
}

/* ---------- transfer function: undo + direct-manipulation editor ---------- */

static void inspector_transfer_begin(const StandaloneApp *app)
{
    s_tf_before[0] = app->tf_center;
    s_tf_before[1] = app->tf_width;
    s_tf_before[2] = app->tf_opacity_scale;
}

static void inspector_transfer_commit(StandaloneApp *app, const char *what)
{
    if (s_tf_before[0] == app->tf_center &&
        s_tf_before[1] == app->tf_width &&
        s_tf_before[2] == app->tf_opacity_scale) {
        return;
    }
    app->transfer_undo_center = s_tf_before[0];
    app->transfer_undo_width = s_tf_before[1];
    app->transfer_undo_opacity_scale = s_tf_before[2];
    app->transfer_undo_available = 1;
    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "%s: center %.3f, width %.3f, opacity %.2f.",
             what, app->tf_center, app->tf_width, app->tf_opacity_scale);
}

/* Call right after a transfer widget: snapshots on activation, records
   one undo step when the edit is released. */
static void inspector_transfer_undo_hook(StandaloneApp *app, const char *what)
{
    if (ImGui::IsItemActivated()) {
        inspector_transfer_begin(app);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        inspector_transfer_commit(app, what);
    }
}

enum {
    INSPECTOR_TF_DRAG_NONE = 0,
    INSPECTOR_TF_DRAG_MOVE,
    INSPECTOR_TF_DRAG_LOW_EDGE,
    INSPECTOR_TF_DRAG_HIGH_EDGE
};

static void inspector_transfer_editor(StandaloneApp *app)
{
    const float  height = 96.0f;
    const float  edge_grab = 7.0f;
    ImVec2       p0 = ImGui::GetCursorScreenPos();
    float        width = fmaxf(ImGui::GetContentRegionAvail().x, 60.0f);
    ImVec2       p1 = ImVec2(p0.x + width, p0.y + height);
    ImDrawList  *dl = ImGui::GetWindowDrawList();
    ImGuiStorage *storage = ImGui::GetStateStorage();
    ImGuiID      mode_id;
    int          mode;
    float        lo, hi, x_lo, x_hi, x_c, y_peak;
    float        inner_h = height - 8.0f;
    ImVec2       mouse = ImGui::GetIO().MousePos;
    bool         hovered, active;

    ImGui::InvisibleButton("##tf_editor", ImVec2(width, height));
    hovered = ImGui::IsItemHovered();
    active = ImGui::IsItemActive();
    mode_id = ImGui::GetItemID();

    lo = app->tf_center - app->tf_width * 0.5f;
    hi = app->tf_center + app->tf_width * 0.5f;
    x_lo = p0.x + voxelpilot_ai_clampf(lo, 0.0f, 1.0f) * width;
    x_hi = p0.x + voxelpilot_ai_clampf(hi, 0.0f, 1.0f) * width;
    x_c = p0.x + voxelpilot_ai_clampf(app->tf_center, 0.0f, 1.0f) * width;
    y_peak = p1.y - 4.0f - voxelpilot_ai_clampf(app->tf_opacity_scale / 3.0f, 0.0f, 1.0f) * inner_h;

    /* Pick the drag mode on press: an edge if the press lands on one,
       otherwise move the whole window (x = centre, y = opacity). */
    if (ImGui::IsItemActivated()) {
        inspector_transfer_begin(app);
        if (fabsf(mouse.x - x_lo) <= edge_grab) {
            mode = INSPECTOR_TF_DRAG_LOW_EDGE;
        } else if (fabsf(mouse.x - x_hi) <= edge_grab) {
            mode = INSPECTOR_TF_DRAG_HIGH_EDGE;
        } else {
            mode = INSPECTOR_TF_DRAG_MOVE;
        }
        storage->SetInt(mode_id, mode);
    }
    mode = storage->GetInt(mode_id, INSPECTOR_TF_DRAG_NONE);

    if (active) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        float  mouse_norm = voxelpilot_ai_clampf((mouse.x - p0.x) / width, 0.0f, 1.0f);
        bool   changed = false;

        if (mode == INSPECTOR_TF_DRAG_LOW_EDGE || mode == INSPECTOR_TF_DRAG_HIGH_EDGE) {
            float w = voxelpilot_ai_clampf(fabsf(mouse_norm - app->tf_center) * 2.0f, 0.02f, 1.0f);
            changed = (w != app->tf_width);
            app->tf_width = w;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else if (mode == INSPECTOR_TF_DRAG_MOVE && (delta.x != 0.0f || delta.y != 0.0f)) {
            app->tf_center = voxelpilot_ai_clampf(app->tf_center + delta.x / width, 0.0f, 1.0f);
            app->tf_opacity_scale = voxelpilot_ai_clampf(
                app->tf_opacity_scale - delta.y / inner_h * 3.0f, 0.1f, 3.0f);
            changed = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (changed) {
            ImGui::MarkItemEdited(mode_id);
        }
    } else if (hovered) {
        if (fabsf(mouse.x - x_lo) <= edge_grab || fabsf(mouse.x - x_hi) <= edge_grab) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::SetTooltip("Drag to move the window, drag its edges to widen,\n"
                          "drag up or down to change opacity.");
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        inspector_transfer_commit(app, "Transfer edited");
    }

    /* Recompute after edits so the drawing matches this frame's values. */
    lo = app->tf_center - app->tf_width * 0.5f;
    hi = app->tf_center + app->tf_width * 0.5f;
    x_lo = p0.x + voxelpilot_ai_clampf(lo, 0.0f, 1.0f) * width;
    x_hi = p0.x + voxelpilot_ai_clampf(hi, 0.0f, 1.0f) * width;
    x_c = p0.x + voxelpilot_ai_clampf(app->tf_center, 0.0f, 1.0f) * width;
    y_peak = p1.y - 4.0f - voxelpilot_ai_clampf(app->tf_opacity_scale / 3.0f, 0.0f, 1.0f) * inner_h;

    dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 22, 255), 6.0f);
    dl->AddRectFilled(ImVec2(x_lo, p0.y), ImVec2(x_hi, p1.y), IM_COL32(79, 209, 191, 22));

    /* Histogram, square-root scaled (one dominant background bin would
       otherwise flatten everything else); bin 0 is left out of the
       scale for the same reason. Bars inside the window use the accent. */
    if (app->histogram_ready) {
        float peak = 0.0f;
        float bar_w = width / (float)HISTOGRAM_BINS;
        int   b;

        for (b = 1; b < HISTOGRAM_BINS; ++b) {
            peak = fmaxf(peak, app->histogram[b]);
        }
        if (peak > 0.0f) {
            for (b = 0; b < HISTOGRAM_BINS; ++b) {
                float v = sqrtf(fminf(app->histogram[b] / peak, 1.0f));
                float bx = p0.x + (float)b * bar_w;
                float centre = ((float)b + 0.5f) / (float)HISTOGRAM_BINS;
                bool  inside = centre >= lo && centre <= hi;
                if (v <= 0.0f) {
                    continue;
                }
                dl->AddRectFilled(
                    ImVec2(bx, p1.y - 4.0f - v * inner_h),
                    ImVec2(bx + fmaxf(bar_w - 1.0f, 1.0f), p1.y - 4.0f),
                    inside ? IM_COL32(79, 209, 191, 150) : IM_COL32(46, 59, 69, 255));
            }
        }
    }

    /* Opacity tent and its handles. */
    dl->AddLine(ImVec2(x_lo, p1.y - 4.0f), ImVec2(x_c, y_peak), ImGui::GetColorU32(kInspectorHighlight), 2.0f);
    dl->AddLine(ImVec2(x_c, y_peak), ImVec2(x_hi, p1.y - 4.0f), ImGui::GetColorU32(kInspectorHighlight), 2.0f);
    dl->AddLine(ImVec2(x_lo, p0.y + 4.0f), ImVec2(x_lo, p1.y - 4.0f), IM_COL32(79, 209, 191, 160), 1.0f);
    dl->AddLine(ImVec2(x_hi, p0.y + 4.0f), ImVec2(x_hi, p1.y - 4.0f), IM_COL32(79, 209, 191, 160), 1.0f);
    dl->AddCircleFilled(ImVec2(x_c, y_peak), 5.0f, ImGui::GetColorU32(kInspectorHighlight));
    dl->AddRect(p0, p1, IM_COL32(34, 44, 52, 255), 6.0f);
}

/* ---------- Volume ---------- */

static void inspector_volume_rendering_body(StandaloneApp *app, const char *const *quality_labels);

static void inspector_volume_rendering(StandaloneApp *app)
{
    static const char *quality_labels[3] = { "Fast", "Balanced", "Quality" };
    VoxelPilotDefaultState d;
    char   summary[64];
    bool   reset = false;
    bool   modified;

    voxelpilot_default_state_init(&d);
    modified = app->cinematic_enabled != d.cinematic_enabled ||
               app->cinematic_rim_strength != d.cinematic_rim_strength ||
               app->cinematic_glow_strength != d.cinematic_glow_strength ||
               app->cinematic_contrast != d.cinematic_contrast;
    snprintf(summary, sizeof(summary), "%s%s",
             quality_labels[app->render_quality_mode >= 0 && app->render_quality_mode < 3 ? app->render_quality_mode : 1],
             app->cinematic_enabled ? " · Cinematic" : "");

    if (inspector_section("render", "Rendering", summary, true, modified, &reset)) {
        inspector_volume_rendering_body(app, quality_labels);
    }
    if (reset) {
        app->cinematic_enabled = d.cinematic_enabled;
        app->cinematic_rim_strength = d.cinematic_rim_strength;
        app->cinematic_glow_strength = d.cinematic_glow_strength;
        app->cinematic_contrast = d.cinematic_contrast;
        clamp_cinematic_settings(app);
        set_ui_toast(app, "Rendering look reset.");
    }
}

static void inspector_volume_rendering_body(StandaloneApp *app, const char *const *quality_labels)
{
    int clicked;

    clicked = inspector_segmented("quality", quality_labels, 3, app->render_quality_mode);
    if (clicked >= 0) {
        apply_render_quality_preset(app, clicked);
    }

    {
        bool cinematic = app->cinematic_enabled != 0;
        if (ImGui::Checkbox("Cinematic look", &cinematic)) {
            app->cinematic_enabled = cinematic ? 1 : 0;
            clamp_cinematic_settings(app);
        }
    }
    if (app->cinematic_enabled) {
        inspector_label("Rim");
        ImGui::SliderFloat("##rim", &app->cinematic_rim_strength, 0.0f, 1.0f, "%.2f");
        inspector_label("Glow");
        ImGui::SliderFloat("##glow", &app->cinematic_glow_strength, 0.0f, 1.0f, "%.2f");
        inspector_label("Contrast");
        ImGui::SliderFloat("##contrast", &app->cinematic_contrast, 0.8f, 1.6f, "%.2f");
        clamp_cinematic_settings(app);
    }

    {
        bool adaptive = app->adaptive_quality_enabled != 0;
        if (ImGui::Checkbox("Adaptive quality", &adaptive)) {
            app->adaptive_quality_enabled = adaptive ? 1 : 0;
            app->adaptive_quality_multiplier = 1.0f;
            refresh_adaptive_quality_for_frame(app);
        }
        ImGui::SameLine();
        draw_help_marker("Lowers ray quality while the camera moves, then restores it. Keeps orbiting smooth on laptops.");
    }

    if (ImGui::TreeNodeEx("Advanced##render_adv", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        inspector_label("Step size");
        ImGui::SliderFloat("##step", &app->step_size, 0.0005f, 0.01f, "%.4f");
        inspector_label("Opacity stop");
        ImGui::SliderFloat("##threshold", &app->threshold, 0.1f, 1.0f, "%.2f");
        inspector_label("Empty skip");
        ImGui::SliderFloat("##skip", &app->skip_mult, 1.0f, 8.0f, "%.1f");

        {
            bool match_window = app->render_match_window != 0;
            if (ImGui::Checkbox("Render at viewport size", &match_window)) {
                app->render_match_window = match_window ? 1 : 0;
                sync_resolution_selection(app);
            }
        }
        if (!app->render_match_window) {
            const char *current_label = "Custom";
            if (app->selected_resolution >= 0 && app->selected_resolution < k_resolution_preset_count) {
                current_label = k_resolution_presets[app->selected_resolution].label;
            }
            inspector_label("Resolution");
            if (ImGui::BeginCombo("##resolution", current_label)) {
                int i;
                for (i = 0; i < k_resolution_preset_count; ++i) {
                    bool selected = (i == app->selected_resolution);
                    if (ImGui::Selectable(k_resolution_presets[i].label, selected)) {
                        app->render_w = k_resolution_presets[i].width;
                        app->render_h = k_resolution_presets[i].height;
                        app->selected_resolution = i;
                    }
                }
                ImGui::EndCombo();
            }
            inspector_label("Width");
            if (ImGui::InputInt("##render_w", &app->render_w)) {
                if (app->render_w < 64) app->render_w = 64;
                if (app->render_w > 4096) app->render_w = 4096;
                sync_resolution_selection(app);
            }
            inspector_label("Height");
            if (ImGui::InputInt("##render_h", &app->render_h)) {
                if (app->render_h < 64) app->render_h = 64;
                if (app->render_h > 4096) app->render_h = 4096;
                sync_resolution_selection(app);
            }
        } else {
            ImGui::TextColored(kInspectorSecondary, "Rendering %d x %d", app->render_w, app->render_h);
        }
        {
            bool paused = app->paused != 0;
            if (ImGui::Checkbox("Pause rendering", &paused)) {
                app->paused = paused ? 1 : 0;
            }
        }
        ImGui::TreePop();
    }
    ImGui::Spacing();
}

static void inspector_volume_transfer(StandaloneApp *app)
{
    VoxelPilotDefaultState d;
    char   summary[64];
    bool   reset = false;
    bool   modified;

    voxelpilot_default_state_init(&d);
    modified = fabsf(app->tf_center - d.tf_center) > 0.0005f ||
               fabsf(app->tf_width - d.tf_width) > 0.0005f ||
               fabsf(app->tf_opacity_scale - d.tf_opacity_scale) > 0.005f ||
               app->tf_palette != d.tf_palette ||
               app->tf_invert != d.tf_invert;
    snprintf(summary, sizeof(summary), "C %.2f · W %.2f · O %.2f",
             app->tf_center, app->tf_width, app->tf_opacity_scale);

    if (inspector_section("transfer", "Transfer function", summary, true, modified, &reset)) {
        inspector_transfer_editor(app);
        ImGui::Spacing();

        inspector_label("Center");
        ImGui::SliderFloat("##tf_center", &app->tf_center, 0.0f, 1.0f, "%.3f");
        inspector_transfer_undo_hook(app, "Transfer center");
        inspector_label("Width");
        ImGui::SliderFloat("##tf_width", &app->tf_width, 0.02f, 1.0f, "%.3f");
        inspector_transfer_undo_hook(app, "Transfer width");
        inspector_label("Opacity");
        ImGui::SliderFloat("##tf_opacity", &app->tf_opacity_scale, 0.1f, 3.0f, "%.2f");
        inspector_transfer_undo_hook(app, "Transfer opacity");

        inspector_label("Palette");
        ImGui::Combo("##tf_palette", &app->tf_palette, k_tf_palette_labels, 4);
        {
            bool invert = app->tf_invert != 0;
            if (ImGui::Checkbox("Invert", &invert)) {
                app->tf_invert = invert ? 1 : 0;
            }
        }
        ImGui::Spacing();
    }

    if (reset) {
        inspector_transfer_begin(app);
        app->tf_center = d.tf_center;
        app->tf_width = d.tf_width;
        app->tf_opacity_scale = d.tf_opacity_scale;
        app->tf_palette = d.tf_palette;
        app->tf_invert = d.tf_invert;
        inspector_transfer_commit(app, "Transfer reset to defaults");
        set_ui_toast(app, "Transfer reset. Undo is in AI Assist.");
    }
}

static int inspector_clip_active(const StandaloneApp *app)
{
    int i;
    for (i = 0; i < 3; ++i) {
        if (app->clip_min[i] > 0.0005f || app->clip_max[i] < 0.9995f) {
            return 1;
        }
    }
    return 0;
}

static void inspector_clip_controls(StandaloneApp *app)
{
    static const char *axis_names[3] = { "X", "Y", "Z" };
    static const ImVec4 axis_colors[3] = {
        ImVec4(0.941f, 0.443f, 0.404f, 1.0f),  /* #F07167 */
        ImVec4(0.482f, 0.847f, 0.561f, 1.0f),  /* #7BD88F */
        ImVec4(0.431f, 0.659f, 0.996f, 1.0f)   /* #6EA8FE */
    };
    int i;

    for (i = 0; i < 3; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "##clip_%d", i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(axis_colors[i], "%s", axis_names[i]);
        ImGui::SameLine(inspector_label_width());
        ImGui::SetNextItemWidth(-FLT_MIN);
        /* A range drag cannot cross over: min stops at max and back, so
           the old swap-when-crossed jump is gone. */
        ImGui::DragFloatRange2(id, &app->clip_min[i], &app->clip_max[i],
                               0.005f, 0.0f, 1.0f, "%.2f", "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
    }
    if (inspector_clip_active(app) && ImGui::Button("Show full volume")) {
        for (i = 0; i < 3; ++i) {
            app->clip_min[i] = 0.0f;
            app->clip_max[i] = 1.0f;
        }
    }
}

static void inspector_volume_clipping(StandaloneApp *app)
{
    bool reset = false;
    int  active = inspector_clip_active(app);
    int  i;

    if (inspector_section("clip", "Clipping", active ? "Active" : "Full volume", false, active != 0, &reset)) {
        inspector_clip_controls(app);
        ImGui::Spacing();
    }
    if (reset) {
        for (i = 0; i < 3; ++i) {
            app->clip_min[i] = 0.0f;
            app->clip_max[i] = 1.0f;
        }
    }
}

static void inspector_volume_file(StandaloneApp *app)
{
    char summary[96];
    int  has_volume = inspector_has_volume(app);

    if (has_volume) {
        snprintf(summary, sizeof(summary), "%d x %d x %d", app->renderer.W, app->renderer.H, app->renderer.D);
    } else {
        snprintf(summary, sizeof(summary), "No volume");
    }
    /* Open by default only while there is nothing loaded - then it is
       the one thing the user needs. */
    if (!inspector_section("file", "Volume file", summary, !has_volume, false, NULL)) {
        return;
    }

    ImGui::Text("Volume Import");
    draw_help_marker("RAW files have no header: set the dimensions and data type, then load. "
                     "Dimensions are read from file names like name_256x256x256_uint8.raw.");

    if (ImGui::Button("Browse...")) {
        browse_volume_file(app);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##filepath", app->upload_path, sizeof(app->upload_path), ImGuiInputTextFlags_ReadOnly);

    inspector_label("Width");
    if (ImGui::InputInt("##up_w", &app->upload_width)) app->upload_width = clamp_import_dimension(app->upload_width);
    inspector_label("Height");
    if (ImGui::InputInt("##up_h", &app->upload_height)) app->upload_height = clamp_import_dimension(app->upload_height);
    inspector_label("Depth");
    if (ImGui::InputInt("##up_d", &app->upload_depth)) app->upload_depth = clamp_import_dimension(app->upload_depth);
    inspector_label("Data type");
    ImGui::Combo("##up_type", &app->upload_data_type, k_volume_data_type_labels, 3);

    {
        VolumeImportSpec spec = {
            (uint32_t)app->upload_width,
            (uint32_t)app->upload_height,
            (uint32_t)app->upload_depth,
            (VolumeDataType)app->upload_data_type
        };
        size_t expected = 0;

        if (volume_import_expected_bytes(&spec, &expected) == 0 && app->upload_file_size_known) {
            if (app->upload_file_size_bytes == expected) {
                ImGui::TextColored(kInspectorAccent, "Matches file size (%.1f MB)",
                                   (double)expected / (1024.0 * 1024.0));
            } else {
                ImGui::TextColored(kInspectorHighlight,
                                   "Expected %.1f MB, file is %.1f MB - check type or dimensions",
                                   (double)expected / (1024.0 * 1024.0),
                                   (double)app->upload_file_size_bytes / (1024.0 * 1024.0));
            }
        } else if (app->upload_hint_status[0]) {
            ImGui::TextWrapped("%s", app->upload_hint_status);
        }
    }

    if (ImGui::Button("Load Volume", ImVec2(-FLT_MIN, 0.0f))) {
        load_selected_volume(app);
    }
    if (app->upload_status[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, kInspectorSecondary);
        ImGui::TextWrapped("%s", app->upload_status);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    inspector_label("Voxel spacing");
    ImGui::InputFloat3("##spacing", app->voxel_spacing, "%.3f");
    {
        int i;
        for (i = 0; i < 3; ++i) {
            if (app->voxel_spacing[i] <= 0.0f) app->voxel_spacing[i] = 1.0f;
        }
    }
    ImGui::Spacing();
}

static void inspector_workspace(StandaloneApp *app)
{
    if (!inspector_section("workspace", "Workspace", "", false, false, NULL)) {
        return;
    }

    if (ImGui::Button("Save Preset")) {
        const char *patterns[] = { "*.vpilot" };
        const char *selected = tinyfd_saveFileDialog(
            "Save VoxelPilot Workspace",
            app->preset_path[0] ? app->preset_path : "workspace.vpilot",
            1, patterns, "VoxelPilot Workspace (*.vpilot)");
        if (selected) {
            strncpy(app->preset_path, selected, sizeof(app->preset_path) - 1);
            app->preset_path[sizeof(app->preset_path) - 1] = '\0';
            if (save_workspace_preset(app, app->preset_path) == 0) {
                snprintf(app->preset_status, sizeof(app->preset_status), "Saved · %s", inspector_basename(app->preset_path));
                set_ui_toast(app, "Workspace saved.");
            } else {
                snprintf(app->preset_status, sizeof(app->preset_status), "Failed to save preset: %s", app->preset_path);
                set_ui_toast(app, "Workspace save failed.", VOXELPILOT_LOG_ERROR);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Preset")) {
        const char *patterns[] = { "*.vpilot", "*" };
        const char *selected = tinyfd_openFileDialog(
            "Load VoxelPilot Workspace", "", 2, patterns, "VoxelPilot Workspace (*.vpilot)", 0);
        if (selected) {
            strncpy(app->preset_path, selected, sizeof(app->preset_path) - 1);
            app->preset_path[sizeof(app->preset_path) - 1] = '\0';
            if (load_workspace_preset(app, app->preset_path) == 0) {
                apply_orbit_camera(app);
                snprintf(app->preset_status, sizeof(app->preset_status), "Loaded · %s", inspector_basename(app->preset_path));
                set_ui_toast(app, "Workspace loaded.");
            } else {
                snprintf(app->preset_status, sizeof(app->preset_status), "Failed to load preset: %s", app->preset_path);
                set_ui_toast(app, "Workspace load failed.", VOXELPILOT_LOG_ERROR);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Snapshot")) {
        save_screenshot(app);
        set_ui_toast(app, "Snapshot saved.");
    }

    if (ImGui::Button("Reset Workspace")) {
        reset_workspace(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Guided Walkthrough")) {
        app->walkthrough_visible = 1;
        app->walkthrough_step = 0;
    }
    {
        const char *layout_label = app->dock_layout_variant == 1 ? "Explore layout" : "Deep review layout";
        if (ImGui::Button(layout_label)) {
            app->dock_layout_variant = app->dock_layout_variant == 1 ? 0 : 1;
            app->dock_layout_ready = 0;
        }
        ImGui::SameLine();
        draw_help_marker("Deep review gives the Inspector and the bottom dock more room for close analysis.");
    }
    ImGui::Spacing();
}

static void inspector_draw_volume(StandaloneApp *app)
{
    inspector_volume_file(app);
    if (inspector_has_volume(app)) {
        inspector_volume_rendering(app);
        inspector_volume_transfer(app);
        inspector_volume_clipping(app);
    }
    inspector_workspace(app);
}

/* ---------- Region / Annotation ---------- */

static void inspector_sync_region_overlay(StandaloneApp *app, VoxelPilotSemanticRegion *region)
{
    AnnotationRegion *mirror = find_annotation_by_label(app, region->label_id);
    if (mirror) {
        snprintf(mirror->name, sizeof(mirror->name), "%s", region->name);
        mirror->color[0] = region->color[0];
        mirror->color[1] = region->color[1];
        mirror->color[2] = region->color[2];
    }
    app->label_mask_revision++;
}

static void inspector_draw_region(StandaloneApp *app, int index)
{
    VoxelPilotSemanticRegion *region = &app->semantic_regions[index];
    size_t total = semantic_total_voxels(app);
    bool   visible = app->semantic_region_hidden[index] == 0;

    if (inspector_section("region", "Region", region->archetype, true, false, NULL)) {
        inspector_label("Name");
        if (ImGui::InputText("##region_name", region->name, sizeof(region->name))) {
            inspector_sync_region_overlay(app, region);
        }
        inspector_label("Colour");
        if (ImGui::ColorEdit3("##region_color", region->color, ImGuiColorEditFlags_NoInputs)) {
            inspector_sync_region_overlay(app, region);
        }
        if (ImGui::Checkbox("Visible", &visible)) {
            app->semantic_region_hidden[index] = visible ? 0 : 1;
        }

        ImGui::Spacing();
        inspector_label("Voxels");
        ImGui::Text("%zu (%.1f%% of volume)", region->voxel_count,
                    total ? 100.0 * (double)region->voxel_count / (double)total : 0.0);
        inspector_label("Match score");
        ImGui::ProgressBar(region->confidence, ImVec2(-FLT_MIN, 0.0f));
        inspector_label("Source");
        ImGui::Text("%s", region->user_confirmed ? "Confirmed by you" : "ASD-Lite suggestion");
        if (region->description[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, kInspectorSecondary);
            ImGui::TextWrapped("%s", region->description);
            ImGui::PopStyleColor();
        }
        if (region->label_id != 0) {
            const char *note = semantic_relationship_note(app, region->label_id);
            if (note && note[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, kInspectorSecondary);
                ImGui::TextWrapped("%s", note);
                ImGui::PopStyleColor();
            }
        }

        {
            float focus[3];
            focus[0] = region->centroid[0] / fmaxf((float)(app->renderer.W - 1), 1.0f);
            focus[1] = region->centroid[1] / fmaxf((float)(app->renderer.H - 1), 1.0f);
            focus[2] = region->centroid[2] / fmaxf((float)(app->renderer.D - 1), 1.0f);
            if (ImGui::Button("Focus camera", ImVec2(-FLT_MIN, 0.0f))) {
                inspector_focus_camera_on(app, focus);
                app->semantic_selected_region = index;
            }
        }
        ImGui::Spacing();
    }

    /* Human-in-the-loop: one question, two answers, remembered. */
    if (inspector_section("teach", "Is this right?", region->user_confirmed ? "Confirmed" : "", true, false, NULL)) {
        if (region->user_confirmed) {
            ImGui::TextColored(kInspectorAccent, "You confirmed this region.");
            ImGui::PushStyleColor(ImGuiCol_Text, kInspectorSecondary);
            ImGui::TextWrapped("VoxelPilot will label similar regions \"%s\" in future discoveries.", region->name);
            ImGui::PopStyleColor();
        } else {
            float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Text, kInspectorSecondary);
            ImGui::TextWrapped("Your answer is saved on this computer and shapes future discoveries.");
            ImGui::PopStyleColor();
            if (ImGui::Button("Yes, keep it", ImVec2(half, 0.0f))) {
                if (teach_semantic_region(app, index, 1)) {
                    set_ui_toast(app, "Confirmed. VoxelPilot will remember this.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("No, hide it", ImVec2(half, 0.0f))) {
                if (teach_semantic_region(app, index, 0)) {
                    app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_NONE;
                    app->scene_selection_index = -1;
                    if (app->semantic_selected_region == index) {
                        app->semantic_selected_region = -1;
                    }
                    set_ui_toast(app, "Hidden. Similar regions will be hidden next time.");
                }
            }
        }
        if (app->semantic_memory_status[0]) {
            ImGui::TextDisabled("%s", app->semantic_memory_status);
        }
        ImGui::Spacing();
    }
}

static void inspector_draw_annotation(StandaloneApp *app, int index)
{
    AnnotationRegion *ann = &app->annotations[index];
    bool visible = app->annotation_hidden[index] == 0;

    if (!inspector_section("annotation", "Annotation", "", true, false, NULL)) {
        return;
    }
    inspector_label("Name");
    ImGui::InputText("##ann_name", ann->name, sizeof(ann->name));
    inspector_label("Colour");
    if (ImGui::ColorEdit3("##ann_color", ann->color, ImGuiColorEditFlags_NoInputs)) {
        app->label_mask_revision++;
    }
    if (ImGui::Checkbox("Visible", &visible)) {
        app->annotation_hidden[index] = visible ? 0 : 1;
    }
    inspector_label("Voxels");
    ImGui::Text("%zu", ann->voxel_count);
    inspector_label("Seed");
    ImGui::Text("%.3f, %.3f, %.3f", ann->seed[0], ann->seed[1], ann->seed[2]);
    if (ImGui::Button("Focus camera", ImVec2(-FLT_MIN, 0.0f))) {
        inspector_focus_camera_on(app, ann->seed);
        app->selected_annotation = index;
    }
}

/* ---------- Measurement / Clip box / Camera / Light ---------- */

static void inspector_draw_measurement(StandaloneApp *app)
{
    if (!inspector_section("measure", "Distance", "", true, false, NULL)) {
        return;
    }
    ImGui::PushFont(NULL, 26.0f);
    ImGui::Text("%.2f vox", measurement_distance(app));
    ImGui::PopFont();
    ImGui::TextColored(kInspectorSecondary, "%.3f world units  ·  delta %.1f, %.1f, %.1f vox",
                       measurement_distance_world(app),
                       (app->measure_b[0] - app->measure_a[0]) * (float)app->renderer.W,
                       (app->measure_b[1] - app->measure_a[1]) * (float)app->renderer.H,
                       (app->measure_b[2] - app->measure_a[2]) * (float)app->renderer.D);
    {
        bool show = app->measurement_visible != 0;
        if (ImGui::Checkbox("Show in viewport and slices", &show)) {
            app->measurement_visible = show ? 1 : 0;
        }
    }

    ImGui::Spacing();
    inspector_label("Point A");
    ImGui::DragFloat3("##meas_a", app->measure_a, 0.002f, 0.0f, 1.0f, "%.3f");
    inspector_label("Point B");
    ImGui::DragFloat3("##meas_b", app->measure_b, 0.002f, 0.0f, 1.0f, "%.3f");
    if (ImGui::Button("A from slices")) {
        app->measure_a[0] = app->slice_x;
        app->measure_a[1] = app->slice_y;
        app->measure_a[2] = app->slice_z;
    }
    ImGui::SameLine();
    if (ImGui::Button("B from slices")) {
        app->measure_b[0] = app->slice_x;
        app->measure_b[1] = app->slice_y;
        app->measure_b[2] = app->slice_z;
    }
    ImGui::TextDisabled("With Show on, clicks in a slice place A, then B.");
}

static void inspector_draw_clip_box(StandaloneApp *app)
{
    if (!inspector_section("clipbox", "Bounds", inspector_clip_active(app) ? "Active" : "Full volume", true, false, NULL)) {
        return;
    }
    inspector_clip_controls(app);
}

static void inspector_draw_camera(StandaloneApp *app)
{
    static const char *view_labels[4] = { "Front", "Side", "Top", "Iso" };
    int clicked;

    if (inspector_section("view", "View", "", true, false, NULL)) {
        clicked = inspector_segmented("views", view_labels, 4, -1);
        if (clicked == 0) set_camera_preset(app, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
        if (clicked == 1) set_camera_preset(app, -1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        if (clicked == 2) set_camera_preset(app, 0.5f, 1.7f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        if (clicked == 3) set_camera_preset(app, -0.9f, 1.2f, -0.9f, 0.7f, -0.35f, 0.7f, 0.0f, 1.0f, 0.0f);
        inspector_label("Field of view");
        ImGui::SliderFloat("##fov", &app->fov_y_deg, 10.0f, 120.0f, "%.0f°");
        ImGui::TextDisabled("Drag in the viewport to orbit, scroll to zoom.");
        ImGui::Spacing();
    }
    if (inspector_section("camera_adv", "Advanced", "", false, false, NULL)) {
        inspector_label("Position");
        ImGui::DragFloat3("##cam_pos", app->cam_pos, 0.01f, -2.0f, 2.0f, "%.2f");
        inspector_label("Direction");
        ImGui::DragFloat3("##cam_dir", app->cam_dir, 0.01f, -1.0f, 1.0f, "%.2f");
        inspector_label("Up");
        ImGui::DragFloat3("##cam_up", app->cam_up, 0.01f, -1.0f, 1.0f, "%.2f");
    }
}

static void inspector_draw_light(StandaloneApp *app)
{
    VoxelPilotDefaultState d;
    bool reset = false;
    bool modified;

    voxelpilot_default_state_init(&d);
    modified = memcmp(app->light_pos, d.light_pos, sizeof(d.light_pos)) != 0;
    if (inspector_section("light", "Light", "", true, modified, &reset)) {
        inspector_label("Position");
        ImGui::DragFloat3("##light_pos", app->light_pos, 0.02f, -3.0f, 3.0f, "%.2f");
    }
    if (reset) {
        memcpy(app->light_pos, d.light_pos, sizeof(app->light_pos));
    }
}

/* ---------- Window ---------- */

void draw_controls_panel(StandaloneApp *app)
{
    int         kind;
    int         index;
    const char *title = "Volume";
    const char *kind_label = "Volume";
    char        title_buf[160];

    if (!app->controls_visible) {
        return;
    }

    /* Resolve the selection, falling back to the Volume whenever it
       points at something that no longer exists (deleted, rejected). */
    kind = app->scene_selection_kind;
    index = app->scene_selection_index;
    if (kind == VOXELPILOT_SCENE_SELECTION_REGION &&
        (index < 0 || index >= app->semantic_region_count || app->semantic_regions[index].rejected)) {
        kind = VOXELPILOT_SCENE_SELECTION_VOLUME;
    }
    if (kind == VOXELPILOT_SCENE_SELECTION_ANNOTATION &&
        (index < 0 || index >= app->annotation_count || !app->annotations[index].active)) {
        kind = VOXELPILOT_SCENE_SELECTION_VOLUME;
    }
    if (kind == VOXELPILOT_SCENE_SELECTION_NONE) {
        kind = VOXELPILOT_SCENE_SELECTION_VOLUME;
    }

    switch (kind) {
    case VOXELPILOT_SCENE_SELECTION_REGION:
        title = app->semantic_regions[index].name[0] ? app->semantic_regions[index].name : "ASD region";
        kind_label = "Region";
        break;
    case VOXELPILOT_SCENE_SELECTION_ANNOTATION:
        title = app->annotations[index].name[0] ? app->annotations[index].name : "Annotation";
        kind_label = "Annotation";
        break;
    case VOXELPILOT_SCENE_SELECTION_MEASUREMENT: title = "Measurement A to B"; kind_label = "Measurement"; break;
    case VOXELPILOT_SCENE_SELECTION_CLIP_BOX:    title = "Clip box";          kind_label = "Clip box";    break;
    case VOXELPILOT_SCENE_SELECTION_CAMERA:      title = "Camera";            kind_label = "Camera";      break;
    case VOXELPILOT_SCENE_SELECTION_KEY_LIGHT:   title = "Key light";         kind_label = "Light";       break;
    default:
        if (inspector_has_volume(app) && app->upload_path[0]) {
            snprintf(title_buf, sizeof(title_buf), "%s", inspector_basename(app->upload_path));
            title = title_buf;
        } else {
            title = inspector_has_volume(app) ? "Volume" : "No volume loaded";
        }
        break;
    }

    set_default_window_layout(app, "Inspector");
    bool inspector_open = app->controls_visible != 0;
    if (ImGui::Begin("Inspector", &inspector_open)) {
        /* Header: overline, then the selected item's name and type pill. */
        ImGui::TextColored(kInspectorSecondary, "INSPECTOR");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
        {
            ImVec2 pill = ImGui::CalcTextSize(kind_label);
            float  pill_w = pill.x + 16.0f;
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x > pill_w) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - pill_w);
            }
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  h = ImGui::GetFrameHeight();
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + pill_w, p.y + h - 2.0f),
                IM_COL32(42, 54, 64, 255), 999.0f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, p.y));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kInspectorSecondary, "%s", kind_label);
        }
        ImGui::Separator();

        switch (kind) {
        case VOXELPILOT_SCENE_SELECTION_REGION:      inspector_draw_region(app, index);     break;
        case VOXELPILOT_SCENE_SELECTION_ANNOTATION:  inspector_draw_annotation(app, index); break;
        case VOXELPILOT_SCENE_SELECTION_MEASUREMENT: inspector_draw_measurement(app);       break;
        case VOXELPILOT_SCENE_SELECTION_CLIP_BOX:    inspector_draw_clip_box(app);          break;
        case VOXELPILOT_SCENE_SELECTION_CAMERA:      inspector_draw_camera(app);            break;
        case VOXELPILOT_SCENE_SELECTION_KEY_LIGHT:   inspector_draw_light(app);             break;
        default:                                     inspector_draw_volume(app);            break;
        }
    }
    app->controls_visible = inspector_open ? 1 : 0;
    ImGui::End();
}
