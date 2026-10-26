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
        scale = fminf(cell_w / (float)tex_w, cell_h / (float)tex_h);
        disp_w = (float)tex_w * scale;
        disp_h = (float)tex_h * scale;

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
    scale = fminf(avail_w / (float)tex_w, avail_h / (float)tex_h);
    disp_w = (float)tex_w * scale;
    disp_h = (float)tex_h * scale;

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

    if (!app || app->splash_visible) {
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
