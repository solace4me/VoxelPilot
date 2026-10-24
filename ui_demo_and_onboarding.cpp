/* Demo banner, guided walkthrough, startup splash, status bar, guided
   review overlay, and cinematic semantic callouts: the onboarding and
   presentation-mode overlays drawn on top of the 3D viewport, plus
   the bottom status bar.
   Extracted from standalone_main.cu as part of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   All eight functions here are called from standalone_main.cu
   (draw_gui or the main loop) and are now non-static for that reason.

   The five walkthrough/splash tables (k_walkthrough_titles,
   k_walkthrough_bodies, k_walkthrough_step_count, k_splash_statuses,
   k_splash_status_count) moved here entirely rather than becoming
   extern, since nothing outside this cluster reads them - the same
   situation as step 7's k_ribbon_tab_labels.

   select_guided_review_region stayed in standalone_main.cu (it's also
   called from start_guided_review, which lives there) but lost its
   `static` and gained a standalone_app.h declaration, since
   draw_guided_review_overlay here calls it too.

   Everything else this cluster calls was already cross-file before
   this step: apply_auto_enhance_transfer and clipping_is_active
   (step 5), apply_orbit_camera (step 6), clamp_cinematic_settings
   (step 3), measurement_distance/measurement_distance_world (step 1),
   semantic_relationship_note (step 2), set_default_window_layout
   (step 7) - or are `static inline` helpers in common/ui_state_helpers.h
   (voxelpilot_callout_should_show_rank, voxelpilot_clampf,
   voxelpilot_project_point_to_screen) / an enum constant in
   common/semantic_discovery.h (VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED),
   both safe to use directly. */

#include <stdio.h>

#include "standalone_app.h"

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

void draw_demo_banner(StandaloneApp *app)
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

void draw_walkthrough_overlay(StandaloneApp *app)
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

void update_startup_splash(StandaloneApp *app)
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


void update_auto_demo_sequence(StandaloneApp *app, double now)
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
void draw_startup_splash(StandaloneApp *app)
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

void draw_status_bar(StandaloneApp *app)
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


void draw_guided_review_overlay(StandaloneApp *app)
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

void draw_cinematic_semantic_callouts(StandaloneApp *app)
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
