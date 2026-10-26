/* ui_viewport_overlay.cpp

   Step 6 of docs/superpowers/plans/2026-10-24-unity-style-redesign.md:
   the orthogonal slice strip, relocated from the Insights panel (where it
   sat as three static thumbnails plus three plain sliders - see the old
   "Orthogonal Slice Review" block this replaces, formerly in
   draw_insights_panel, standalone_main.cpp) to a small floating overlay
   row anchored to the bottom of the actual 3D viewport - the dockspace's
   passthrough central node, found via ImGui::DockBuilderGetCentralNode -
   and made interactive:

   - Dragging a thumbnail moves its crosshair, which updates app->slice_x/
     y/z. Since all three slice previews are computed from those same
     three shared coordinates (update_slice_previews,
     gl_viewport_and_render.cpp), dragging one plane's crosshair always
     moves the OTHER two previews too - that is the "updates the other
     two slices" behavior the spec asks for, and it falls out of the
     existing data model for free.
   - Scrolling the mouse wheel over a thumbnail steps through that
     plane's own depth axis (Axial -> Z, Coronal -> Y, Sagittal -> X -
     the axis not shown within that plane's own image).
   - Double-clicking a thumbnail enlarges it to fill the viewport; Esc
     (or the on-screen "Back" button) returns to the strip.
   - The old click-to-place-annotation / click-to-place-measurement-point
     behavior (app->annotation_mode / app->measurement_visible) carries
     over unchanged from the function this replaces. A real Measure tool
     mode with its own exclusive input handling is step 7's job, not
     this one's - today's single-orbit-drag-everywhere interaction model
     (process_mouse_camera_controls, gl_viewport_and_render.cpp) is
     untouched by this step.

   Gap flagged, not faked: the spec also wants dragging to move "the 3D
   slice planes". There is no such concept anywhere in this renderer -
   the CUDA raymarch kernel (renderer/raymarch_kernel.cu) has no slice-
   plane overlay of any kind; app->slice_x/y/z only ever drive the 2D
   CPU-side preview thumbnails. Adding actual 3D plane gizmos into the
   raymarch would be a real renderer feature in its own right, well
   beyond "relocate and wire up the existing slice strip" - out of scope
   here. This step makes the 2D previews and their shared coordinates
   fully interactive, which is everything the existing data/render
   pipeline can actually support without new renderer work.

   draw_slice_card, the old static thumbnail-plus-tooltip function this
   takes over from (per the plan's "Proposed module structure" section),
   is removed from standalone_main.cpp; its tooltip/annotation/measurement
   logic is reused here via the same cross-file helpers it already used
   (find_annotation_by_label, find_semantic_region_by_label,
   semantic_relationship_note, voxelpilot_describe_voxel_context,
   point_from_slice_uv, point_to_voxel_index, create_annotation_region_at_point -
   all already declared in standalone_app.h / common/ai_features.h). */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "imgui.h"
#include "imgui_internal.h"   /* ImGuiDockNode / DockBuilderGetCentralNode -
                                  only used in draw_viewport_slice_strip
                                  below, to anchor this overlay to the
                                  dockspace's actual central-node rect
                                  rather than the whole window. */
#include "standalone_app.h"

static const char *k_slice_plane_labels[3] = { "Axial", "Coronal", "Sagittal" };

/* plane 0 = Axial (W x H), 1 = Coronal (W x D), 2 = Sagittal (H x D) -
   the exact pairing the pre-step-6 draw_slice_card call site used. */
static void slice_plane_texture_and_extent(
    const StandaloneApp *app,
    int                   plane,
    GLuint               *out_tex,
    int                  *out_w,
    int                  *out_h)
{
    switch (plane) {
    case 0:
        *out_tex = app->axial_tex;
        *out_w   = app->renderer.W;
        *out_h   = app->renderer.H;
        break;
    case 1:
        *out_tex = app->coronal_tex;
        *out_w   = app->renderer.W;
        *out_h   = app->renderer.D;
        break;
    default:
        *out_tex = app->sagittal_tex;
        *out_w   = app->renderer.H;
        *out_h   = app->renderer.D;
        break;
    }
}

/* A plane's display size in physical units: voxel count times voxel
   spacing on each of its two axes, so a slice keeps the same proportions
   as the 3D render (common/volume_geometry.h). Sizing by voxel count
   alone stretched anisotropic scans - the carp's side views (0.39 vs
   1.0 spacing) were drawn 2.6x too tall for their width. */
static void slice_plane_physical_size(
    const StandaloneApp *app,
    int                  plane,
    float               *out_w,
    float               *out_h)
{
    /* Axes per plane, matching slice_plane_texture_and_extent:
       axial = x by y, coronal = x by z, sagittal = y by z. */
    static const int u_axis[3] = { 0, 0, 1 };
    static const int v_axis[3] = { 1, 2, 2 };
    const int dims[3] = { app->renderer.W, app->renderer.H, app->renderer.D };
    int u = u_axis[plane < 0 || plane > 2 ? 0 : plane];
    int v = v_axis[plane < 0 || plane > 2 ? 0 : plane];
    float su = app->voxel_spacing[u] > 0.0f ? app->voxel_spacing[u] : 1.0f;
    float sv = app->voxel_spacing[v] > 0.0f ? app->voxel_spacing[v] : 1.0f;

    *out_w = (float)(dims[u] > 0 ? dims[u] : 1) * su;
    *out_h = (float)(dims[v] > 0 ? dims[v] : 1) * sv;
}

/* The depth step (in normalized 0..1 units) one "notch" of mouse wheel
   moves along a plane's own depth axis - one voxel per notch. */
static float slice_plane_depth_step(const StandaloneApp *app, int plane)
{
    int dim;

    switch (plane) {
    case 0:  dim = app->renderer.D; break; /* Axial scrolls Z */
    case 1:  dim = app->renderer.H; break; /* Coronal scrolls Y */
    default: dim = app->renderer.W; break; /* Sagittal scrolls X */
    }
    if (dim < 2) {
        return 0.0f;
    }
    return 1.0f / (float)(dim - 1);
}

static void slice_plane_scroll(StandaloneApp *app, int plane, float wheel)
{
    float step = slice_plane_depth_step(app, plane);

    if (step <= 0.0f || wheel == 0.0f) {
        return;
    }
    if (plane == 0) {
        app->slice_z = voxelpilot_ai_clampf(app->slice_z + wheel * step, 0.0f, 1.0f);
    } else if (plane == 1) {
        app->slice_y = voxelpilot_ai_clampf(app->slice_y + wheel * step, 0.0f, 1.0f);
    } else {
        app->slice_x = voxelpilot_ai_clampf(app->slice_x + wheel * step, 0.0f, 1.0f);
    }
}

/* Same voxel-context lookup + tooltip the old draw_slice_card built,
   lifted unchanged so hover behavior doesn't regress. */
static void slice_thumbnail_tooltip(
    StandaloneApp *app,
    int            plane,
    const float    point[3])
{
    int    vx = 0, vy = 0, vz = 0;
    size_t voxel_index = 0;

    (void)plane;
    if (!point_to_voxel_index(app, point, &vx, &vy, &vz, &voxel_index) ||
        !app->renderer.volume_data) {
        return;
    }

    {
        float                      intensity = app->renderer.volume_data[voxel_index];
        unsigned char              label_id = 0;
        AnnotationRegion           *region = NULL;
        VoxelPilotSemanticRegion   *semantic = NULL;

        if (app->label_mask &&
            app->label_mask_W == app->renderer.W &&
            app->label_mask_H == app->renderer.H &&
            app->label_mask_D == app->renderer.D) {
            label_id = app->label_mask[voxel_index];
            region   = find_annotation_by_label(app, label_id);
            semantic = find_semantic_region_by_label(app, label_id);
        }

        if (semantic) {
            const char *note = semantic_relationship_note(app, label_id);
            snprintf(app->hover_description,
                     sizeof(app->hover_description),
                     "%s | %s | match %.0f%% | stability %.0f%% | source ASD-Lite heuristic. %s%s%s",
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
}

/* Draws one interactive slice thumbnail at the given display size.
   Returns 1 if it was just double-clicked (caller enlarges it). */
static int draw_interactive_slice_thumbnail(
    StandaloneApp *app,
    int            plane,
    float          display_w,
    float          display_h)
{
    GLuint tex = 0;
    int    tex_w = 0;
    int    tex_h = 0;
    int    double_clicked = 0;
    ImVec2 image_min, image_max, mouse;
    float  u = 0.5f, v = 0.5f;
    float  point[3];

    slice_plane_texture_and_extent(app, plane, &tex, &tex_w, &tex_h);
    if (tex_w < 1 || tex_h < 1) {
        return 0;
    }

    ImGui::BeginGroup();
    ImGui::TextUnformatted(k_slice_plane_labels[plane]);
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(display_w, display_h));

    image_min = ImGui::GetItemRectMin();
    image_max = ImGui::GetItemRectMax();
    mouse = ImGui::GetIO().MousePos;
    u = (mouse.x - image_min.x) / fmaxf(image_max.x - image_min.x, 1.0f);
    v = (mouse.y - image_min.y) / fmaxf(image_max.y - image_min.y, 1.0f);
    u = voxelpilot_ai_clampf(u, 0.0f, 1.0f);
    v = voxelpilot_ai_clampf(v, 0.0f, 1.0f);
    point_from_slice_uv(app, plane, u, v, point);

    if (ImGui::IsItemHovered()) {
        slice_thumbnail_tooltip(app, plane, point);
        slice_plane_scroll(app, plane, ImGui::GetIO().MouseWheel);

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            double_clicked = 1;
        }

        /* Pre-step-6 click behavior, unchanged: annotate or place the
           next measurement point. (A double-click's second press also
           satisfies IsMouseClicked - so double-clicking to enlarge can
           also drop an annotation/measurement point at that same spot.
           Harmless overlap, not worth guarding against here; step 7's
           real tool-mode state machine resolves this properly.) */
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (app->annotation_mode) {
                create_annotation_region_at_point(app, point);
                app->annotation_mode = 0;
            } else if (app->measurement_visible) {
                float *target = (app->measurement_target == 0) ? app->measure_a : app->measure_b;
                target[0] = point[0];
                target[1] = point[1];
                target[2] = point[2];
                app->measurement_target = 1 - app->measurement_target;
            }
        }
    }

    /* Crosshair drag: while the left button is held on this thumbnail
       (a click or a sustained drag), continuously snap the shared slice
       coordinates to the point under the cursor. This is what makes the
       other two previews follow along, since they read the same three
       coordinates every frame (update_slice_previews,
       gl_viewport_and_render.cpp). */
    if (ImGui::IsItemActive()) {
        app->slice_x = point[0];
        app->slice_y = point[1];
        app->slice_z = point[2];
    }

    ImGui::EndGroup();
    return double_clicked;
}

static ImGuiWindowFlags slice_overlay_base_flags()
{
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;
#ifdef IMGUI_HAS_DOCK
    flags |= ImGuiWindowFlags_NoDocking;
#endif
    return flags;
}

/* The strip is a docked window ("Slices") between the viewport and the
   bottom dock - see ensure_default_dock_layout - rather than an overlay
   floating on top of the render, where it hid the lower part of the
   volume. Each plane gets a third of the strip's width; its thumbnail is
   fitted inside that cell at the plane's true aspect ratio and centred. */
static void draw_slice_strip_row(StandaloneApp *app)
{
    const float kGap = 10.0f;
    const float kLabelRow = 22.0f;
    ImVec2 avail;
    float cell_w;
    float cell_h;
    int plane;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kGap, 4.0f));
    ImGui::Begin("Slices", NULL,
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(2);

    avail = ImGui::GetContentRegionAvail();
    cell_w = fmaxf((avail.x - kGap * 2.0f) / 3.0f, 32.0f);
    cell_h = fmaxf(avail.y - kLabelRow, 24.0f);

    for (plane = 0; plane < 3; ++plane) {
        GLuint tex = 0;
        int    tex_w = 0;
        int    tex_h = 0;
        float  scale, disp_w, disp_h;
        float  cell_x = ImGui::GetCursorPosX();

        if (plane > 0) {
            ImGui::SameLine();
            cell_x = ImGui::GetCursorPosX();
        }
        slice_plane_texture_and_extent(app, plane, &tex, &tex_w, &tex_h);
        if (tex_w < 1 || tex_h < 1) {
            continue;
        }
        {
            float phys_w, phys_h;
            slice_plane_physical_size(app, plane, &phys_w, &phys_h);
            scale = fminf(cell_w / phys_w, cell_h / phys_h);
            disp_w = phys_w * scale;
            disp_h = phys_h * scale;
        }

        /* Centre the thumbnail in its cell, then reserve the full cell
           width so the next plane starts at the same column every time. */
        ImGui::SetCursorPosX(cell_x + (cell_w - disp_w) * 0.5f);
        if (draw_interactive_slice_thumbnail(app, plane, disp_w, disp_h)) {
            app->slice_strip_enlarged_plane = plane;
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(fmaxf(cell_x + cell_w - ImGui::GetCursorPosX(), 0.0f), 1.0f));
    }

    ImGui::End();
}

static void draw_enlarged_slice(
    StandaloneApp *app,
    ImVec2         central_pos,
    ImVec2         central_size,
    int            plane)
{
    const float kMargin = 32.0f;
    const float kHeaderH = 34.0f;
    GLuint tex = 0;
    int    tex_w = 0;
    int    tex_h = 0;
    float  avail_w, avail_h, scale, disp_w, disp_h;

    slice_plane_texture_and_extent(app, plane, &tex, &tex_w, &tex_h);
    if (tex_w < 1 || tex_h < 1) {
        app->slice_strip_enlarged_plane = -1;
        return;
    }

    avail_w = fmaxf(central_size.x - kMargin * 2.0f, 40.0f);
    avail_h = fmaxf(central_size.y - kMargin * 2.0f - kHeaderH, 40.0f);
    {
        float phys_w, phys_h;
        slice_plane_physical_size(app, plane, &phys_w, &phys_h);
        scale = fminf(avail_w / phys_w, avail_h / phys_h);
        disp_w = phys_w * scale;
        disp_h = phys_h * scale;
    }

    ImGui::SetNextWindowPos(central_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(central_size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kMargin, kMargin * 0.5f));
    ImGui::Begin("ViewportSliceEnlarged", NULL,
                  slice_overlay_base_flags() | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    ImGui::TextColored(
        ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
        "%s - enlarged", k_slice_plane_labels[plane]);
    ImGui::SameLine();
    if (ImGui::SmallButton("Back (Esc)")) {
        app->slice_strip_enlarged_plane = -1;
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fmaxf((avail_w - disp_w) * 0.5f, 0.0f));
    draw_interactive_slice_thumbnail(app, plane, disp_w, disp_h);

    ImGui::End();
}

void draw_viewport_slice_strip(StandaloneApp *app)
{
    ImVec2 central_pos;
    ImVec2 central_size;

    if (!app || app->splash_visible || !app->slices_visible) {
        return;
    }
    if (app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        return;
    }

#ifdef IMGUI_HAS_DOCK
    {
        ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(app->main_dockspace_id);
        if (!central) {
            return;
        }
        central_pos = central->Pos;
        central_size = central->Size;
    }
#else
    /* No real dockspace to anchor to on a non-docking ImGui build - see
       commit cee6a89 (docs/superpowers/plans/2026-10-24-unity-style-
       redesign.md) for why this should never actually happen now. */
    return;
#endif

    if (app->slice_strip_enlarged_plane >= 0 &&
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        app->slice_strip_enlarged_plane = -1;
    }

    /* The docked strip is always submitted - if it vanished while a slice
       was enlarged, its dock node would hand its height back to the
       viewport and the layout would jump. */
    draw_slice_strip_row(app);
    if (app->slice_strip_enlarged_plane >= 0) {
        draw_enlarged_slice(app, central_pos, central_size, app->slice_strip_enlarged_plane);
    }
}

/* ============================================================
   Viewport toolbar and tool overlays (step 7)

   The toolbar floats in the viewport's top-left corner: the five tools
   (exactly one active; keys O P M C A), the camera presets with an
   eased 300 ms transition (keys 1-4), Frame (F), and one line of help
   for the active tool. Mouse behaviour lives in
   process_mouse_camera_controls (gl_viewport_and_render.cpp).

   The overlays are drawn on the background draw list, clipped to the
   viewport, so panels always stay on top: the measurement line with its
   distance, and the clip box as a wireframe while the Clip tool is
   active or a clip is set.
   ============================================================ */

static const char *k_tool_labels[VOXELPILOT_TOOL_COUNT] = { "Orbit", "Pan", "Measure", "Clip", "Annotate" };
static const char *k_tool_keys[VOXELPILOT_TOOL_COUNT] = { "O", "P", "M", "C", "A" };
static const char *k_tool_hints[VOXELPILOT_TOOL_COUNT] = {
    "Drag to orbit - right-drag pans - scroll zooms",
    "Drag to pan - middle-drag orbits - scroll zooms",
    "Click the volume to place point A, then B - drag still orbits",
    "Set the bounds in the Inspector - drag orbits",
    "Click the volume to place a note - drag still orbits"
};
static const char *k_view_labels[VOXELPILOT_CAMERA_PRESET_COUNT] = { "Front", "Side", "Top", "Iso" };

static int viewport_origin(const StandaloneApp *app, ImVec2 *pos, ImVec2 *size)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    if (!app->viewport_rect_valid) return 0;
    *pos = ImVec2(vp->Pos.x + app->viewport_rect[0], vp->Pos.y + app->viewport_rect[1]);
    *size = ImVec2(app->viewport_rect[2], app->viewport_rect[3]);
    return size->x > 1.0f && size->y > 1.0f;
}

static int toolbar_button(const char *label, bool active, const char *tooltip)
{
    int clicked;
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.090f, 0.212f, 0.204f, 1.0f));   /* accent/subtle */
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.498f, 0.902f, 0.839f, 1.0f));     /* accent/hover */
    }
    clicked = ImGui::Button(label) ? 1 : 0;
    if (active) {
        ImGui::PopStyleColor(2);
    }
    if (tooltip && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

void draw_viewport_toolbar(StandaloneApp *app)
{
    ImVec2 pos, size;
    int tool, preset;
    char tip[64];

    if (!app || app->splash_visible || !viewport_origin(app, &pos, &size)) return;
    if (app->slice_strip_enlarged_plane >= 0) return;

    ImGui::SetNextWindowPos(ImVec2(pos.x + 12.0f, pos.y + 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.86f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::Begin("##viewport_toolbar", NULL, slice_overlay_base_flags() | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);

    for (tool = 0; tool < VOXELPILOT_TOOL_COUNT; ++tool) {
        if (tool > 0) ImGui::SameLine();
        snprintf(tip, sizeof(tip), "%s (%s)", k_tool_labels[tool], k_tool_keys[tool]);
        if (toolbar_button(k_tool_labels[tool], app->active_tool == tool, tip)) {
            set_active_tool(app, tool);
        }
    }

    ImGui::SameLine(0.0f, 16.0f);
    for (preset = 0; preset < VOXELPILOT_CAMERA_PRESET_COUNT; ++preset) {
        if (preset > 0) ImGui::SameLine();
        snprintf(tip, sizeof(tip), "%s view (%d)", k_view_labels[preset], preset + 1);
        if (toolbar_button(k_view_labels[preset], false, tip)) {
            start_camera_preset_transition(app, preset);
        }
    }
    ImGui::SameLine();
    if (toolbar_button("Frame", false, "Centre the volume at a comfortable distance (F)")) {
        frame_volume(app);
    }

    ImGui::TextDisabled("%s", k_tool_hints[app->active_tool >= 0 && app->active_tool < VOXELPILOT_TOOL_COUNT
                                            ? app->active_tool : 0]);
    ImGui::End();
}

/* Normalized volume point -> screen position in the viewport. */
static int project_volume_point(const StandaloneApp *app, const float extent[3],
                                const float point[3], ImVec2 origin, ImVec2 size, ImVec2 *out)
{
    float world[3], sx = 0.0f, sy = 0.0f, depth = 0.0f;
    voxelpilot_volume_point_to_world(extent, point, world);
    if (!voxelpilot_project_point_to_screen(world, app->cam_pos, app->cam_dir, app->cam_up,
                                            app->fov_y_deg, size.x, size.y, &sx, &sy, &depth)) {
        return 0;
    }
    *out = ImVec2(origin.x + sx, origin.y + sy);
    return 1;
}

void draw_viewport_tool_overlays(StandaloneApp *app)
{
    ImVec2 origin, size;
    ImDrawList *dl;
    float extent[3];
    const ImU32 accent = IM_COL32(127, 230, 214, 255);

    if (!app || app->splash_visible || !viewport_origin(app, &origin, &size)) return;
    if (app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) return;

    voxelpilot_volume_box_extent(app->renderer.W, app->renderer.H, app->renderer.D,
                                 app->voxel_spacing, extent);
    dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

    /* Clip box wireframe. */
    if (app->active_tool == VOXELPILOT_TOOL_CLIP || clipping_is_active(app)) {
        ImVec2 corner[8];
        int ok = 1, i;
        static const int edges[12][2] = {
            { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 },
            { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
        };
        for (i = 0; i < 8; ++i) {
            float p[3];
            p[0] = (i & 1) ? app->clip_max[0] : app->clip_min[0];
            p[1] = (i & 2) ? app->clip_max[1] : app->clip_min[1];
            p[2] = (i & 4) ? app->clip_max[2] : app->clip_min[2];
            ok &= project_volume_point(app, extent, p, origin, size, &corner[i]);
        }
        if (ok) {
            for (i = 0; i < 12; ++i) {
                dl->AddLine(corner[edges[i][0]], corner[edges[i][1]], IM_COL32(127, 230, 214, 150), 1.5f);
            }
        }
    }

    /* Measurement A -> B with its distance. */
    if (app->measurement_visible) {
        ImVec2 a, b;
        int has_a = project_volume_point(app, extent, app->measure_a, origin, size, &a);
        int has_b = project_volume_point(app, extent, app->measure_b, origin, size, &b);
        if (has_a && has_b) {
            char label[64];
            ImVec2 mid = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            ImVec2 text;
            dl->AddLine(a, b, accent, 2.0f);
            snprintf(label, sizeof(label), "%.1f vox  |  %.2f units",
                     measurement_distance(app), measurement_distance_world(app));
            text = ImGui::CalcTextSize(label);
            /* Centred above the midpoint, clear of the A/B markers even
               when the two points project close together. */
            {
                ImVec2 box_min = ImVec2(mid.x - text.x * 0.5f - 6.0f, mid.y - text.y - 22.0f);
                ImVec2 box_max = ImVec2(mid.x + text.x * 0.5f + 6.0f, mid.y - 14.0f);
                dl->AddRectFilled(box_min, box_max, IM_COL32(10, 13, 16, 220), 6.0f);
                dl->AddText(ImVec2(box_min.x + 6.0f, box_min.y + 4.0f), accent, label);
            }
        }
        if (has_a) {
            dl->AddCircleFilled(a, 5.0f, IM_COL32(14, 18, 22, 255));
            dl->AddCircle(a, 5.0f, accent, 0, 2.0f);
            dl->AddText(ImVec2(a.x + 8.0f, a.y - 18.0f), accent, "A");
        }
        if (has_b) {
            dl->AddCircleFilled(b, 5.0f, IM_COL32(14, 18, 22, 255));
            dl->AddCircle(b, 5.0f, accent, 0, 2.0f);
            dl->AddText(ImVec2(b.x + 8.0f, b.y - 18.0f), accent, "B");
        }
    }

    dl->PopClipRect();
}

/* ============================================================
   Axis gizmo (step 8)

   Bottom-left of the viewport: the world X / Y / Z axes as the camera
   currently sees them (X red, Y green, Z blue, each labelled, so colour
   is never the only cue). Axes pointing away from the viewer are drawn
   first and dimmer. Clicking an axis letter looks straight down that
   axis; the small dot at the other end of each axis looks from the
   opposite side. Both use the eased camera transition.
   ============================================================ */

void draw_viewport_axis_gizmo(StandaloneApp *app)
{
    static const char *names[3] = { "X", "Y", "Z" };
    static const ImU32 colors[3] = {
        IM_COL32(240, 113, 103, 255), IM_COL32(123, 216, 143, 255), IM_COL32(110, 168, 254, 255)
    };
    const float size = 96.0f;
    const float arm = 32.0f;
    ImVec2 origin, vsize, centre;
    float3 forward, up, right, view_up;
    float sx[3], sy[3], depth[3];
    int order[3] = { 0, 1, 2 };
    int i, j;
    ImDrawList *dl;

    if (!app || app->splash_visible || !viewport_origin(app, &origin, &vsize)) return;
    if (app->slice_strip_enlarged_plane >= 0) return;

    forward = f3_normalize(f3_make(app->cam_dir[0], app->cam_dir[1], app->cam_dir[2]));
    up = f3_normalize(f3_make(app->cam_up[0], app->cam_up[1], app->cam_up[2]));
    right = f3_normalize(f3_cross(forward, up));
    view_up = f3_cross(right, forward);

    for (i = 0; i < 3; ++i) {
        float3 e = f3_make(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f);
        sx[i] = f3_dot(e, right);
        sy[i] = -f3_dot(e, view_up);
        depth[i] = f3_dot(e, forward);   /* > 0: pointing away from the viewer */
    }
    /* Far axes first so the nearest is drawn on top. */
    for (i = 0; i < 2; ++i)
        for (j = i + 1; j < 3; ++j)
            if (depth[order[j]] > depth[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }

    ImGui::SetNextWindowPos(ImVec2(origin.x + 10.0f, origin.y + vsize.y - size - 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size, size), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##axis_gizmo", NULL, slice_overlay_base_flags() | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    dl = ImGui::GetWindowDrawList();
    centre = ImVec2(ImGui::GetWindowPos().x + size * 0.5f, ImGui::GetWindowPos().y + size * 0.5f);
    dl->AddCircleFilled(centre, size * 0.5f - 2.0f, IM_COL32(10, 13, 16, 150));

    for (j = 0; j < 3; ++j) {
        int a = order[j];
        ImVec2 tip = ImVec2(centre.x + sx[a] * arm, centre.y + sy[a] * arm);
        ImVec2 tail = ImVec2(centre.x - sx[a] * arm, centre.y - sy[a] * arm);
        ImU32 col = colors[a];
        ImU32 dim = (col & 0x00FFFFFFu) | (depth[a] > 0.0f ? 0x80000000u : 0xFF000000u);
        char id[16];
        int hovered;

        /* Opposite end: a small dot that looks from the other side. */
        snprintf(id, sizeof(id), "##neg%d", a);
        ImGui::SetCursorScreenPos(ImVec2(tail.x - 6.0f, tail.y - 6.0f));
        if (ImGui::InvisibleButton(id, ImVec2(12.0f, 12.0f))) start_camera_axis_transition(app, a, -1);
        hovered = ImGui::IsItemHovered();
        dl->AddCircleFilled(tail, hovered ? 6.0f : 4.0f, (col & 0x00FFFFFFu) | 0x70000000u);
        if (hovered) ImGui::SetTooltip("Look from -%s", names[a]);

        dl->AddLine(centre, tip, dim, 2.0f);
        snprintf(id, sizeof(id), "##pos%d", a);
        ImGui::SetCursorScreenPos(ImVec2(tip.x - 9.0f, tip.y - 9.0f));
        if (ImGui::InvisibleButton(id, ImVec2(18.0f, 18.0f))) start_camera_axis_transition(app, a, 1);
        hovered = ImGui::IsItemHovered();
        dl->AddCircleFilled(tip, hovered ? 10.0f : 8.0f, dim);
        dl->AddText(ImVec2(tip.x - ImGui::CalcTextSize(names[a]).x * 0.5f, tip.y - ImGui::GetFontSize() * 0.5f),
                    IM_COL32(10, 13, 16, 255), names[a]);
        if (hovered) ImGui::SetTooltip("Look from +%s", names[a]);
    }
    ImGui::End();
}
