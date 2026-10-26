/* Controls panel: volume browse/load, the resolution/data-type/
   rendering-quality/transfer-function/clipping/camera UI, workspace
   save/load buttons, and the app's keyboard shortcuts.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tinyfiledialogs.h"

#include "standalone_app.h"

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
    strcpy(app->upload_status, "Loading to GPU...");
    reload_volume(
        &app->renderer,
        temp,
        (int)spec.width,
        (int)spec.height,
        (int)spec.depth);
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
    volume_import_free(temp);
    set_ui_toast(app, "Volume loaded.");
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
void draw_controls_panel(StandaloneApp *app)
{
    if (!app->controls_visible) {
        return;
    }

    set_default_window_layout(app, "Controls");
    bool controls_open = app->controls_visible != 0;
    if (ImGui::Begin("Controls", &controls_open)) {
        float measurement_voxels = measurement_distance(app);

    /* ImGui puts each label to the right of its widget, and the default
       item width (65% of the panel) left too little room in the 340 px
       right-hand panel - "GPU Quality Preset", "Opacity Threshold" and
       "Render Resolution" were clipped. Size widgets to the panel minus a
       fixed label column instead. */
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 8.5f);

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
                ? "Deep Review: Insights stays visible above the AI Assist/Histogram/Metadata/Log tabs."
                : "Demo: Insights and the AI Assist/Histogram/Metadata/Log tabs share one panel.");
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

    ImGui::PopItemWidth();
    }
    app->controls_visible = controls_open ? 1 : 0;
    ImGui::End();
}
