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


/* Height of the top bar (the menu bar, step 11) as measured last frame;
   40 px (the spec's top bar) before the first frame. The name is kept
   from the ribbon it replaced. */
static float voxelpilot_ribbon_height(const StandaloneApp *app)
{
    return (app && app->top_bar_height > 1.0f) ? app->top_bar_height : 40.0f;
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
    ImVec2 dock_size = ImVec2(viewport->Size.x,
                              fmaxf(120.0f, viewport->Size.y - ribbon_height - STATUS_BAR_HEIGHT));

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
        /* Exposed on app so other overlays (step 6's viewport slice strip,
           ui_viewport_overlay.cpp) can find the dockspace's central-node
           rect via ImGui::DockBuilderGetCentralNode without having to
           recompute - or worse, re-derive - this same id themselves. */
        app->main_dockspace_id = dockspace_id;
        ensure_default_dock_layout(app, dockspace_id);
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        {
            /* Record the viewport (central node) rect for the renderer:
               the CUDA image is sized to and drawn into this rect only. */
            ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(dockspace_id);
            app->viewport_rect_valid = 0;
            if (central && central->Size.x > 1.0f && central->Size.y > 1.0f) {
                app->viewport_rect[0] = central->Pos.x - viewport->Pos.x;
                app->viewport_rect[1] = central->Pos.y - viewport->Pos.y;
                app->viewport_rect[2] = central->Size.x;
                app->viewport_rect[3] = central->Size.y;
                app->viewport_rect_valid = 1;
            }
        }
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
    ImGuiID dock_slices_id;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    float ribbon_height = voxelpilot_ribbon_height(app);
    ImVec2 dock_size = ImVec2(viewport->Size.x,
                              fmaxf(120.0f, viewport->Size.y - ribbon_height - STATUS_BAR_HEIGHT));
    float scene_w;
    float inspector_w;
    float bottom_h;

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

    /* Spec "Layout" table, in pixels at 100% scale: Scene 240 px on the
       left and the Inspector on the right, both full height (as in
       Unity); the bottom dock (AI Assist / Histogram / Metadata / Log)
       240 px tall under the viewport only. "Deep review" (variant 1)
       widens the Inspector to 360 px and the dock to 320 px. The status
       bar is no longer docked - draw_status_bar pins it below the
       dockspace at STATUS_BAR_HEIGHT.

       The Inspector (draw_controls_panel, step 10) is 340 px rather than
       the spec's 320 to leave its transfer-function editor a usable
       width. The old Insights panel is retired: its measurement summary
       lives in the Measurement inspector now. */
    /* 280 rather than the spec's 240: ASD region names carry size,
       material, shape and position ("Large dense branching · top,
       front") and were clipped at 240. */
    scene_w = 280.0f;
    inspector_w = (app->dock_layout_variant == 1) ? 360.0f : 340.0f;
    bottom_h = (app->dock_layout_variant == 1) ? 320.0f : 240.0f;

    dock_main_id = dockspace_id;
    dock_scene_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Left,
        fminf(scene_w / dock_size.x, 0.3f), NULL, &dock_main_id);
    dock_right_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Right,
        fminf(inspector_w / fmaxf(dock_size.x - scene_w, 1.0f), 0.45f), NULL, &dock_main_id);
    dock_bottom_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down,
        fminf(bottom_h / dock_size.y, 0.45f), NULL, &dock_main_id);
    /* Slice strip between the viewport and the bottom dock. 180 px, not
       the spec's original 132: at 132 the thumbnails were ~94 px, too
       small to read. Users can still drag it to any height. */
    dock_slices_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down,
        fminf(180.0f / fmaxf(dock_size.y - bottom_h, 1.0f), 0.4f), NULL, &dock_main_id);
    dock_left_id = dock_right_id;

    ImGui::DockBuilderDockWindow("Scene", dock_scene_id);
    ImGui::DockBuilderDockWindow("Inspector", dock_left_id);
    ImGui::DockBuilderDockWindow("Slices", dock_slices_id);
    ImGui::DockBuilderDockWindow("Bottom Panels", dock_bottom_id);

    /* Scene draws its own "SCENE" header, each slice card is labelled and
       Bottom Panels has its own inner tab row, so their dock tab bars
       only duplicated those. */
    {
        ImGuiDockNode *scene_node = ImGui::DockBuilderGetNode(dock_scene_id);
        ImGuiDockNode *slices_node = ImGui::DockBuilderGetNode(dock_slices_id);
        ImGuiDockNode *bottom_node = ImGui::DockBuilderGetNode(dock_bottom_id);
        if (scene_node) scene_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        if (slices_node) slices_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        if (bottom_node) bottom_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
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
static void reset_default_layout(StandaloneApp *app)
{
    if (!app) return;
    app->controls_visible = 1;
    app->insights_visible = 1;
    app->scene_visible = 1;
    app->slices_visible = 1;
    app->dock_visible = 1;
    app->focus_mode = 0;
    app->dock_layout_ready = 0;
    app->layout_refit_next_frame = 1;
    snprintf(app->preset_status, sizeof(app->preset_status), "Layout reset to default.");
    set_ui_toast(app, "Layout reset.");
}

/* View > Focus viewport / F11: hide every panel around the viewport, and
   bring back exactly what was showing on the second press. */
void toggle_focus_viewport(StandaloneApp *app)
{
    if (!app) return;
    if (!app->focus_mode) {
        app->focus_saved[0] = app->scene_visible;
        app->focus_saved[1] = app->slices_visible;
        app->focus_saved[2] = app->dock_visible;
        app->focus_saved[3] = app->controls_visible;
        app->scene_visible = 0;
        app->slices_visible = 0;
        app->dock_visible = 0;
        app->controls_visible = 0;
        app->focus_mode = 1;
        set_ui_toast(app, "Focus viewport - F11 brings the panels back.");
    } else {
        app->scene_visible = app->focus_saved[0];
        app->slices_visible = app->focus_saved[1];
        app->dock_visible = app->focus_saved[2];
        app->controls_visible = app->focus_saved[3];
        app->focus_mode = 0;
    }
}

static void menu_scout_focus_slice(StandaloneApp *app)
{
    float focus_slice = 0.0f;
    float focus_score = 0.0f;

    if (voxelpilot_find_high_variance_axial_slice(
            app->renderer.volume_data, app->renderer.W, app->renderer.H, app->renderer.D,
            &focus_slice, &focus_score)) {
        app->slice_z = focus_slice;
        app->ai_focus_slice = focus_slice;
        app->ai_focus_score = focus_score;
        app->slices_visible = 1;
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "Scout moved Axial Z to %.3f.", app->ai_focus_slice);
        set_ui_toast(app, "Focus slice updated.");
    } else {
        set_ui_toast(app, "Focus scout needs a loaded 3D volume.", VOXELPILOT_LOG_WARNING);
    }
}

static void menu_reset_transfer(StandaloneApp *app)
{
    VoxelPilotDefaultState d;
    voxelpilot_default_state_init(&d);
    apply_transfer_settings_with_undo(app, d.tf_center, d.tf_width, d.tf_opacity_scale,
                                      "Transfer reset to defaults");
    app->tf_palette = d.tf_palette;
    app->tf_invert = d.tf_invert;
}

static void menu_select_volume_section(StandaloneApp *app)
{
    app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_VOLUME;
    app->scene_selection_index = -1;
    app->controls_visible = 1;
    app->inspector_open_file_section = 1;
}

static void draw_shortcuts_popup(StandaloneApp *app)
{
    static const char *rows[][2] = {
        { "Ctrl+O", "Open volume" },          { "Ctrl+S", "Save snapshot" },
        { "Ctrl+Shift+S", "Save workspace" }, { "Ctrl+K", "Ask VoxelPilot (AI prompt)" },
        { "Ctrl+T", "Cinematic look on/off" }, { "Ctrl+1 ... Ctrl+4", "Scene / Slices / Dock / Inspector" },
        { "O  P  M  C  A", "Orbit, Pan, Measure, Clip, Annotate" },
        { "1  2  3  4", "Front, Side, Top, Iso view" },
        { "F", "Frame the volume" },          { "F11", "Focus viewport" },
        { "[  ]", "Previous / next region in review" },
        { "Right-drag / middle-drag", "Pan / orbit, any tool" },
        { "?", "This sheet" }
    };
    int i;

    if (app->show_shortcuts_popup) {
        ImGui::OpenPopup("Keyboard shortcuts");
        app->show_shortcuts_popup = 0;
    }
    if (ImGui::BeginPopupModal("Keyboard shortcuts", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("##shortcuts", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.498f, 0.902f, 0.839f, 1.0f), "%s", rows[i][0]);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(rows[i][1]);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Single-letter keys work when no text field is focused.");
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (app->show_about_popup) {
        ImGui::OpenPopup("About VoxelPilot");
        app->show_about_popup = 0;
    }
    if (ImGui::BeginPopupModal("About VoxelPilot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.78f, 1.0f), "%s", BRAND_TITLE);
        ImGui::TextUnformatted("Laptop-first volume exploration workbench.");
        ImGui::TextUnformatted("For research and teaching. Not a medical device;");
        ImGui::TextUnformatted("not for diagnosis or treatment decisions.");
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

/* ============================================================
   Top bar (step 11; spec "Region by region > Top bar")

   A classic menu bar replaces the Home/View ribbon: File, View, Volume,
   AI, Review, Help, each item with its shortcut (toggles show a check),
   and on the right Open volume, Snapshot and the Safety badge (hover:
   the reason; click: score, reason and recommendation, with a one-click
   switch to the Fast preset when the GPU is under pressure). The bar's
   real height is stored in top_bar_height for the dockspace below it.
   The function keeps its draw_top_ribbon name for its callers.
   ============================================================ */

void draw_top_ribbon(StandaloneApp *app)
{
    bool scene_on, slices_on, dock_on, inspector_on, focus_on, cinematic_on;
    int has_volume;

    if (!app) return;

    /* Keep the always-visible Safety badge accurate every frame,
       regardless of whether AI Assist is open or a gated action ran. */
    refresh_performance_governor(app);
    has_volume = app->renderer.volume_data && app->renderer.W > 0;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 11.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.039f, 0.051f, 0.063f, 1.0f));
    if (!ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }
    app->top_bar_height = ImGui::GetWindowHeight();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.78f, 1.0f), "VoxelPilot");
    ImGui::SameLine(0.0f, 16.0f);

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open volume...", "Ctrl+O")) menu_open_volume(app);
        ImGui::Separator();
        if (ImGui::MenuItem("Open workspace...")) workspace_open_dialog(app);
        if (ImGui::MenuItem("Save workspace", "Ctrl+Shift+S")) workspace_save_dialog(app, 0);
        if (ImGui::MenuItem("Save workspace as...")) workspace_save_dialog(app, 1);
        ImGui::Separator();
        if (ImGui::MenuItem("Save snapshot", "Ctrl+S", false, has_volume)) save_screenshot(app);
        if (ImGui::MenuItem("Export review bundle...", NULL, false, has_volume)) export_ai_review_bundle(app);
        if (ImGui::MenuItem("Export insight report...", NULL, false, has_volume)) export_insight_report(app);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(app->window, 1);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        scene_on = app->scene_visible != 0;
        slices_on = app->slices_visible != 0;
        dock_on = app->dock_visible != 0;
        inspector_on = app->controls_visible != 0;
        focus_on = app->focus_mode != 0;
        if (ImGui::MenuItem("Scene", "Ctrl+1", &scene_on)) app->scene_visible = scene_on;
        if (ImGui::MenuItem("Slices", "Ctrl+2", &slices_on)) app->slices_visible = slices_on;
        if (ImGui::MenuItem("Dock", "Ctrl+3", &dock_on)) app->dock_visible = dock_on;
        if (ImGui::MenuItem("Inspector", "Ctrl+4", &inspector_on)) app->controls_visible = inspector_on;
        if (ImGui::MenuItem("Focus viewport", "F11", &focus_on)) toggle_focus_viewport(app);
        ImGui::Separator();
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Explore", NULL, app->dock_layout_variant == 0)) {
                app->dock_layout_variant = 0;
                app->dock_layout_ready = 0;
            }
            if (ImGui::MenuItem("Deep review", NULL, app->dock_layout_variant == 1)) {
                app->dock_layout_variant = 1;
                app->dock_layout_ready = 0;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) reset_default_layout(app);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::MenuItem("Front", "1")) start_camera_preset_transition(app, VOXELPILOT_CAMERA_PRESET_FRONT);
            if (ImGui::MenuItem("Side", "2")) start_camera_preset_transition(app, VOXELPILOT_CAMERA_PRESET_SIDE);
            if (ImGui::MenuItem("Top", "3")) start_camera_preset_transition(app, VOXELPILOT_CAMERA_PRESET_TOP);
            if (ImGui::MenuItem("Iso", "4")) start_camera_preset_transition(app, VOXELPILOT_CAMERA_PRESET_ISO);
            ImGui::Separator();
            if (ImGui::MenuItem("Frame volume", "F")) frame_volume(app);
            ImGui::EndMenu();
        }
        cinematic_on = app->cinematic_enabled != 0;
        if (ImGui::MenuItem("Cinematic look", "Ctrl+T", &cinematic_on)) {
            app->cinematic_enabled = cinematic_on;
            clamp_cinematic_settings(app);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Volume")) {
        if (ImGui::MenuItem("Import settings...")) menu_select_volume_section(app);
        if (ImGui::MenuItem("Voxel spacing...")) menu_select_volume_section(app);
        ImGui::Separator();
        if (ImGui::MenuItem("Clip tool", "C", app->active_tool == VOXELPILOT_TOOL_CLIP)) {
            set_active_tool(app, VOXELPILOT_TOOL_CLIP);
        }
        if (ImGui::MenuItem("Show full volume", NULL, false, clipping_is_active(app) != 0)) {
            int i;
            for (i = 0; i < 3; ++i) {
                app->clip_min[i] = 0.0f;
                app->clip_max[i] = 1.0f;
            }
        }
        if (ImGui::MenuItem("Reset transfer function", NULL, false, has_volume)) menu_reset_transfer(app);
        ImGui::Separator();
        if (ImGui::MenuItem("Dataset health check", NULL, false, has_volume)) {
            update_dataset_health(app);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "%s", app->dataset_health_status);
            app->dock_visible = 1;
            app->bottom_tab_request = 3;   /* Metadata shows the health score */
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("AI")) {
        if (ImGui::MenuItem("Ask VoxelPilot", "Ctrl+K")) {
            app->dock_visible = 1;
            app->bottom_tab_request = 1;
            app->focus_ai_prompt = 1;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Auto enhance", NULL, false, has_volume)) apply_auto_enhance_transfer(app, "AI menu");
        if (ImGui::MenuItem("Discover regions", NULL, false,
                            has_volume && app->performance_result.safe_to_run_asd)) {
            if (performance_allows_optional_action(app, "Discover regions", 1, 0)) {
                run_adaptive_semantic_discovery(app);
                refresh_performance_governor(app);
                snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "%s", app->semantic_status);
            }
        }
        if (ImGui::MenuItem("Scout focus slice", NULL, false, has_volume)) menu_scout_focus_slice(app);
        if (ImGui::MenuItem("Transfer advisor")) {
            app->dock_visible = 1;
            app->bottom_tab_request = 1;   /* lives under AI Assist > More AI Tools */
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Review")) {
        int i;
        if (ImGui::MenuItem("Start guided review", NULL, false, app->semantic_region_count > 0)) {
            start_guided_review(app);
        }
        if (ImGui::MenuItem("Next region", "]", false, app->semantic_region_count > 0)) {
            select_guided_review_region(app, 1);
        }
        if (ImGui::MenuItem("Previous region", "[", false, app->semantic_region_count > 0)) {
            select_guided_review_region(app, -1);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Regions", app->semantic_region_count > 0)) {
            for (i = 0; i < app->semantic_region_count; ++i) {
                if (app->semantic_regions[i].rejected) continue;
                ImGui::PushID(i);
                if (ImGui::MenuItem(app->semantic_regions[i].name[0] ? app->semantic_regions[i].name : "ASD region")) {
                    app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_REGION;
                    app->scene_selection_index = i;
                    app->semantic_selected_region = i;
                    app->controls_visible = 1;
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Measurement")) {
            app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_MEASUREMENT;
            app->scene_selection_index = -1;
            app->controls_visible = 1;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Guided walkthrough")) {
            app->walkthrough_visible = 1;
            app->walkthrough_step = 0;
        }
        if (ImGui::MenuItem("Keyboard shortcuts", "?")) app->show_shortcuts_popup = 1;
        if (ImGui::MenuItem("About")) app->show_about_popup = 1;
        ImGui::EndMenu();
    }

    /* Right side: primary actions and the Safety badge. */
    {
        char badge[64];
        float badge_w, buttons_w, x;
        ImGuiStyle *style = &ImGui::GetStyle();

        snprintf(badge, sizeof(badge), "Safety: %s %d/100",
                 app->performance_result.status_label, app->performance_result.pressure_score);
        badge_w = ImGui::CalcTextSize(badge).x + style->FramePadding.x * 2.0f;
        buttons_w = ImGui::CalcTextSize("Open volume").x + ImGui::CalcTextSize("Snapshot").x +
                    style->FramePadding.x * 4.0f + style->ItemSpacing.x * 3.0f;
        x = ImGui::GetWindowWidth() - badge_w - buttons_w - 16.0f;
        if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.310f, 0.820f, 0.749f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.498f, 0.902f, 0.839f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.024f, 0.125f, 0.114f, 1.0f));
        if (ImGui::Button("Open volume")) menu_open_volume(app);
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_volume);
        if (ImGui::Button("Snapshot")) save_screenshot(app);
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.067f, 0.149f, 0.141f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, voxelpilot_pressure_color(app->performance_result.status));
        if (ImGui::Button(badge)) ImGui::OpenPopup("##safety_popover");
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered() && app->performance_result.primary_reason[0]) {
            ImGui::SetTooltip("%s", app->performance_result.primary_reason);
        }
        if (ImGui::BeginPopup("##safety_popover")) {
            ImGui::TextColored(voxelpilot_pressure_color(app->performance_result.status),
                               "Safety: %s (%d/100)", app->performance_result.status_label,
                               app->performance_result.pressure_score);
            if (app->performance_result.primary_reason[0]) {
                ImGui::TextWrapped("%s", app->performance_result.primary_reason);
            }
            if (app->performance_result.recommendation[0]) {
                ImGui::TextDisabled("%s", app->performance_result.recommendation);
            }
            if (app->performance_result.status != VOXELPILOT_GOVERNOR_HEALTHY &&
                ImGui::Button("Switch to Fast quality")) {
                apply_render_quality_preset(app, 0);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::PopStyleVar();
    ImGui::EndMainMenuBar();
    ImGui::PopStyleColor();

    draw_shortcuts_popup(app);
}
