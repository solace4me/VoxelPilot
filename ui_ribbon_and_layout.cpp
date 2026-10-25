/* Dockspace, top ribbon, and window-layout management: the ribbon's
   Home/View tabs, the ImGui docking layout (built once, rebuilt on
   request), per-panel default window positions/sizes, and the
   "Reset Layout" action.
   Extracted from standalone_main.cu as part of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   voxelpilot_ribbon_height, draw_ribbon_tab, reset_default_layout, and
   k_ribbon_tab_labels stay static/private here - all four have zero
   callers outside this cluster (voxelpilot_ribbon_height and
   draw_ribbon_tab are used only by the functions in this file;
   reset_default_layout is only reached via draw_top_ribbon's "Reset
   Layout" button; k_ribbon_tab_labels is only read by draw_ribbon_tab).

   set_default_window_layout was already non-static before this step
   (made cross-file during step 5, since ui_ai_assist_panel.cpp and
   ui_controls_panel.cpp also call it) and moves here as its natural
   home - it depends on voxelpilot_ribbon_height, which lives in this
   file. clamp_cinematic_settings (step 3), refresh_performance_governor
   and voxelpilot_pressure_color (step 4), and set_ui_toast (step 5)
   were already cross-file before this step.
   voxelpilot_should_rebuild_dock_layout is a `static inline` helper
   in common/ui_state_helpers.h, safe to use directly - no declaration
   needed. */

#include <stdio.h>
#include <string.h>

#include "standalone_app.h"
/* imgui_internal.h: needed for the DockBuilder* functions and the
   ImGuiDockNodeFlags_DockSpace private flag used below. The vendored
   ImGui was swapped from the plain/master branch to the real 'docking'
   branch (see the fix for the step-5 broken-layout screenshot,
   docs/superpowers/plans/2026-10-24-unity-style-redesign.md) - before
   that swap, IMGUI_HAS_DOCK was never defined, so every block in this
   file guarded by "#ifdef IMGUI_HAS_DOCK" silently compiled to nothing
   and no window was ever actually docked. standalone_main.cpp already
   includes this header for the same reason. */
#include "imgui_internal.h"


static const char *k_ribbon_tab_labels[] = {
    "Home",
    "View"
};

static float voxelpilot_ribbon_height(const StandaloneApp *app)
{
    return (app && app->ribbon_expanded) ? 122.0f : 38.0f;
}

void draw_dockspace(StandaloneApp *app)
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
    {
        /* ImGui::GetID() hashes relative to the CURRENT window's ID stack,
           so this must be computed right here, inside DockSpaceRoot's
           Begin/End, and that exact ImGuiID is what DockBuilder has to
           build against. ensure_default_dock_layout used to call
           ImGui::GetID("VoxelPilotDockspace") again on its own from
           standalone_main.cpp's draw_gui, with no ImGui window active at
           that call site - very likely hashing to a different id than
           this one. That meant DockBuilderDockWindow() was pre-docking
           Scene/Controls/Status/Insights/Bottom Panels into a dock-node
           tree that this DockSpace() call never actually submitted, so
           every one of those windows silently fell back to floating -
           exactly the "windows sitting on each other" symptom reported
           after the docking-branch ImGui swap (commit cee6a89) made this
           code run for the first time. Fixed by building the layout
           against this one shared, correctly-scoped id instead. */
        ImGuiID dockspace_id = ImGui::GetID("VoxelPilotDockspace");
        ensure_default_dock_layout(app, dockspace_id);
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }
    ImGui::End();
#endif
}

void ensure_default_dock_layout(StandaloneApp *app, ImGuiID dockspace_id)
{
#ifdef IMGUI_HAS_DOCK
    ImGuiID dock_main_id;
    ImGuiID dock_scene_id;
    ImGuiID dock_left_id;
    ImGuiID dock_right_id;
    ImGuiID dock_bottom_id;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    float ribbon_height = voxelpilot_ribbon_height(app);
    ImVec2 dock_size = ImVec2(viewport->Size.x, fmaxf(120.0f, viewport->Size.y - ribbon_height));

    /* dockspace_id is passed in by draw_dockspace, computed inside
       DockSpaceRoot's Begin/End (see the comment there) - it must NOT be
       recomputed here via ImGui::GetID(), which would run with no ImGui
       window active and could hash to a different id than the one
       DockSpace() actually submits. */

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
    /* Scene panel (step 5 of the spec) - a narrow strip carved off the far
       left, ahead of Controls. 0.167 of dock_main_id approximates the
       spec default_widths of the spec's Scene panel (240px of 1440px)
       since this dock split works in fractions, not pixels; Controls
       keeps its existing position/width untouched. Controls itself
       moves to the right and becomes the spec Inspector in step 10/11 -
       not attempted here, per the plan doc's smallest-blast-radius
       ordering. */
    dock_scene_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.167f, NULL, &dock_main_id);
    /* Controls' split fraction is taken against what's left AFTER Scene's
       cut, not against the full width, so leaving this at the pre-Scene
       0.26f silently shrank Controls from 26% of the window to ~0.26 *
       (1 - 0.167) = ~21.7% - narrow enough that several of its labels
       ("GPU Quality Preset", "Render Resolution", "Opacity Threshold")
       started getting clipped (reported after the step-5 docking fixes,
       commit 5e47cd4). Bumped to 0.26 / (1 - 0.167) = 0.312 so Controls'
       actual on-screen width is restored to the same ~26% of the window
       it had before step 5 added Scene; Scene's own 0.167f is unchanged.
       This is a stopgap - step 10 replaces Controls with the spec's
       selection-driven Inspector, which gets its own sizing then. */
    dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.312f, NULL, &dock_main_id);
    dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.30f, NULL, &dock_main_id);

    ImGui::DockBuilderDockWindow("Scene", dock_scene_id);
    ImGui::DockBuilderDockWindow("Controls", dock_left_id);
    ImGui::DockBuilderDockWindow("Status", dock_bottom_id);

    if (app->dock_layout_variant == 1) {
        ImGuiID dock_right_bottom_id =
            ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.45f, NULL, &dock_right_id);
        ImGui::DockBuilderDockWindow("Insights", dock_right_id);
        ImGui::DockBuilderDockWindow("Bottom Panels", dock_right_bottom_id);
    } else {
        ImGui::DockBuilderDockWindow("Insights", dock_right_id);
        ImGui::DockBuilderDockWindow("Bottom Panels", dock_right_id);
    }

    ImGui::DockBuilderFinish(dockspace_id);

    app->dock_layout_ready = 1;
#else
    (void)app;
    (void)dockspace_id;
#endif
}
void set_default_window_layout(
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

static void reset_default_layout(StandaloneApp *app)
{
    if (!app) return;
    app->controls_visible = 1;
    app->insights_visible = 1;
    app->scene_visible = 1;
    app->dock_layout_ready = 0;
    app->layout_refit_next_frame = 1;
    snprintf(app->preset_status, sizeof(app->preset_status), "Layout reset to default.");
    set_ui_toast(app, "Layout reset.");
}

void draw_top_ribbon(StandaloneApp *app)
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
                ImGui::SameLine();
                if (ImGui::Checkbox("Insights##panel_insights", &insights_visible)) {
                    app->insights_visible = insights_visible ? 1 : 0;
                }
            }
            {
                /* Scene panel visibility (step 5). The spec eventually
                   wants this on Ctrl+1 in a real View menu (step 11); this
                   checkbox is the only way to bring Scene back once hidden
                   until that exists. */
                bool scene_visible = app->scene_visible != 0;
                ImGui::SameLine();
                if (ImGui::Checkbox("Scene##panel_scene", &scene_visible)) {
                    app->scene_visible = scene_visible ? 1 : 0;
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
