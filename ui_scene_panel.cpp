/* Scene panel (left dock) - Unity-style redesign step 5 (see
   docs/superpowers/plans/2026-10-24-unity-style-redesign.md and the
   spec's "Scene panel" region in
   docs/superpowers/specs/2026-10-24-ui-ux-interaction-and-flow-spec.md).

   A tree over data that already existed before this step - discovered
   (ASD) regions, annotations, the single measurement, the single clip
   box, camera, and key light - with search, an add(+) menu, row
   hover affordances (eye icon, "..." menu), click-to-select,
   double-click-to-focus-camera, F2 rename, and Delete-with-Undo.

   What's genuinely new rather than just relocated, and what's
   deliberately scoped down or deferred (flagged, not silently
   dropped - same discipline as step 4):

   - Single-measurement / single-clip-box data model: the spec's "+"
     menu implies more could be added later, but VoxelPilot's data
     model only ever holds one of each (see standalone_app.h's
     measure_a/measure_b and clip_min/clip_max - no arrays, no counts).
     "+Measurement"/"+Clip box" reveal or (re)size the one slot that
     exists; a toast says so when one is already active instead of
     silently doing nothing. Open question, same as noted in the plan
     doc's Risks section.
   - Per-row eye icon "show/hide in the viewport": real and immediate
     for Measurement (reuses the existing measurement_visible flag,
     which already gated the point-placement click handler too) and
     Clip box (new clip_hidden/clip_cached_min/max fields - hiding
     caches the real bounds and forces the full 0..1 range, a genuine
     render-affecting toggle). For Annotations and Discovered regions
     there is no per-item visibility lever in the renderer today - the
     3D label overlay (attach_label_overlay_to_render_command,
     gl_viewport_and_render.cpp) blends every active label's color by
     id, every frame, with no per-label on/off; truly suppressing one
     label needs either a per-frame masked-copy upload (real GPU cost)
     or a shader change, both out of scope here. So their eye icons
     toggle a real, new per-item flag (annotation_hidden/
     semantic_region_hidden) that dims the Scene-tree row and excludes
     the item from UI lists (e.g. AI Assist's ASD Summary), but does
     NOT yet hide anything in the 3D view. Flagged here and in the plan
     doc rather than faked.
   - Camera and Key light rows get no eye icon at all: hiding the
     camera makes no sense (it is the viewpoint itself) and there is no
     "disable this light" flag to wire honestly, so no icon is drawn
     rather than drawing one that would do nothing.
   - Delete: for an annotation this sets its existing `active = 0`
     (the same flag create_annotation_region_at_point already uses to
     free/reuse a slot) - Undo sets it back to 1. For a discovered
     region this sets the existing `rejected = 1` (already respected
     by Guided Review and the semantic callouts) - Undo clears it.
     Both are soft, reversible, pre-existing flags; nothing is zeroed
     or reallocated, so Undo is exact as long as nothing reuses that
     slot first (a new annotation placed in the exact same slot before
     Undo is clicked would overwrite it - an accepted, short-lived edge
     case, consistent with this app having no real toast-expiry timer
     yet; see ui_toast's lack of one).
   - Empty-state drop zone: the dashed box and "Open volume..." button
     are real; OS-level drag-and-drop onto the window is also real now
     (drop_callback in standalone_main.cpp, registered via
     glfwSetDropCallback) - a .vpilot path loads it as a workspace,
     anything else is treated as a volume file path exactly like
     Controls' Browse... button (the user still reviews import
     settings and clicks Load Volume).
   - Dock position: a narrow strip carved off the far left, ahead of
     Controls (ui_ribbon_and_layout.cpp's ensure_default_dock_layout).
     This is NOT yet the spec's full layout (Scene left / Inspector
     right at 320px, replacing today's Controls-on-the-left) - that
     repositioning, plus the real View menu and Ctrl+1..4 shortcuts,
     is step 10/11's job per the plan doc's smallest-blast-radius
     ordering. A "Scene" visibility checkbox was added next to the
     existing "Insights" one in the ribbon so the panel can be brought
     back once hidden, since no View menu exists yet to do that.
   - Selection model: scene_selection_kind/scene_selection_index
     (standalone_app.h) is new - nothing consumed a unified "what is
     selected in the Scene tree" concept before this step. Today only
     this file reads it; it exists now because the spec's whole
     interaction model is "select, then inspect", and a future
     Inspector (step 10, retiring draw_insights_panel and reworking
     draw_controls_panel) will read it to decide which section to
     show.
   - F2/Delete: wired here, scoped to while this panel is open (checked
     inside draw_scene_panel, gated on !WantTextInput so typing "delete"
     in the search box does not delete the selection). The spec lists
     these as global shortcuts; full app-wide shortcut wiring
     (independent of which panel is open) is step 11's job.
   - Search match highlighting: the spec says "bold"; this app loads a
     single default ImGui font with no bold/weight variants (confirmed
     - no AddFontFromFileTTF/PushFont anywhere in this codebase), so a
     matched substring is shown in accent/hover colour instead, the
     same kind of textual approximation already used for Overline/
     Label tokens in steps 2-4.
*/

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"

#include "standalone_app.h"

/* ============================================================
   Small local helpers
   ============================================================ */

static const char *scene_basename(const char *path)
{
    const char *slash;
    const char *backslash;

    if (!path) {
        return "";
    }
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    return slash ? slash + 1 : path;
}

/* Whether an annotation gets its own row under "Annotations".
   refresh_semantic_annotation_overlays rebuilds annotations[] as a
   one-for-one copy of the discovered regions (that copy is what feeds
   the render's label overlay), so listing them all showed every region
   twice. The tree keeps the two sources apart instead: AI suggestions
   under "Discovered regions", things the user placed under
   "Annotations". Mirrored entries stay in annotations[] untouched. */
static bool scene_annotation_listed(StandaloneApp *app, int index)
{
    const AnnotationRegion *ann = &app->annotations[index];

    if (!ann->active) {
        return false;
    }
    return ann->label_id == 0 || find_semantic_region_by_label(app, ann->label_id) == NULL;
}

/* Case-insensitive substring search. On a match, *match_offset (if not
   NULL) is set to the byte offset of the first match. */
static bool scene_text_matches_query(
    const char *text,
    const char *query,
    size_t     *match_offset)
{
    size_t text_len, query_len, i;

    if (!query || !query[0] || !text) {
        return false;
    }
    text_len = strlen(text);
    query_len = strlen(query);
    if (query_len > text_len) {
        return false;
    }
    for (i = 0; i + query_len <= text_len; ++i) {
        size_t j;
        bool ok = true;
        for (j = 0; j < query_len; ++j) {
            if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)query[j])) {
                ok = false;
                break;
            }
        }
        if (ok) {
            if (match_offset) {
                *match_offset = i;
            }
            return true;
        }
    }
    return false;
}

/* Draws `text`, highlighting the first match of `query` (if any) in
   accent/hover colour in place of real bold - see this file's header
   comment. No copies are made; TextUnformatted's text_end parameter
   lets each segment be drawn directly out of `text`. */
static void scene_draw_searchable_label(const char *text, const char *query)
{
    size_t offset = 0;
    size_t text_len;
    size_t query_len;

    if (!text) {
        text = "";
    }
    text_len = strlen(text);

    if (!scene_text_matches_query(text, query, &offset)) {
        ImGui::TextUnformatted(text);
        return;
    }

    query_len = strlen(query);
    if (offset > 0) {
        ImGui::TextUnformatted(text, text + offset);
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.498f, 0.902f, 0.839f, 1.0f)); /* accent/hover #7FE6D6 */
    ImGui::TextUnformatted(text + offset, text + offset + query_len);
    ImGui::PopStyleColor();
    if (offset + query_len < text_len) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(text + offset + query_len);
    }
}

/* Small custom-drawn eye icon (open almond + pupil, or a single closed
   line), following the same InvisibleButton + ImDrawList technique
   ui_bottom_panels.cpp's histogram tab already uses for custom controls
   - this codebase has no icon font loaded (see header comment). Returns
   true on click; the caller decides what toggling means. */
static bool scene_draw_eye_icon(const char *str_id, bool visible)
{
    ImVec2 size = ImVec2(18.0f, 18.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = visible
        ? IM_COL32(159, 176, 188, 255)  /* text/secondary #9FB0BC */
        : IM_COL32(159, 176, 188, 110); /* dimmed - hidden */
    bool clicked;

    ImGui::InvisibleButton(str_id, size);
    clicked = ImGui::IsItemClicked();

    if (visible) {
        draw_list->PathArcTo(ImVec2(center.x, center.y + 2.0f), 6.5f, 3.14159265f, 0.0f, 10);
        draw_list->PathArcTo(ImVec2(center.x, center.y - 2.0f), 6.5f, 0.0f, 3.14159265f, 10);
        draw_list->PathStroke(color, ImDrawFlags_Closed, 1.4f);
        draw_list->AddCircleFilled(center, 2.0f, color);
    } else {
        draw_list->AddLine(
            ImVec2(center.x - 7.0f, center.y),
            ImVec2(center.x + 7.0f, center.y),
            color, 1.4f);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(visible ? "Hide in viewport" : "Show in viewport");
    }
    return clicked;
}

static void scene_draw_dashed_rect(ImDrawList *draw_list, ImVec2 mn, ImVec2 mx, ImU32 color)
{
    const float dash = 6.0f;
    const float gap = 4.0f;
    float x, y;

    for (x = mn.x; x < mx.x; x += dash + gap) {
        float x2 = (x + dash < mx.x) ? x + dash : mx.x;
        draw_list->AddLine(ImVec2(x, mn.y), ImVec2(x2, mn.y), color, 1.5f);
        draw_list->AddLine(ImVec2(x, mx.y), ImVec2(x2, mx.y), color, 1.5f);
    }
    for (y = mn.y; y < mx.y; y += dash + gap) {
        float y2 = (y + dash < mx.y) ? y + dash : mx.y;
        draw_list->AddLine(ImVec2(mn.x, y), ImVec2(mn.x, y2), color, 1.5f);
        draw_list->AddLine(ImVec2(mx.x, y), ImVec2(mx.x, y2), color, 1.5f);
    }
}

static void scene_focus_camera_on_point(StandaloneApp *app, const float point_norm[3])
{
    /* orbit_target is a world-space point; the region/annotation seed is
       in normalized volume coordinates. */
    float extent[3];
    voxelpilot_volume_box_extent(app->renderer.W, app->renderer.H, app->renderer.D,
                                 app->voxel_spacing, extent);
    voxelpilot_volume_point_to_world(extent, point_norm, app->orbit_target);
    apply_orbit_camera(app);
}

static void scene_begin_rename(
    StandaloneApp *app,
    int            kind,
    int            index,
    const char    *current_name)
{
    app->scene_rename_kind = kind;
    app->scene_rename_index = index;
    snprintf(app->scene_rename_buffer, sizeof(app->scene_rename_buffer), "%s", current_name ? current_name : "");
    app->scene_selection_kind = kind;
    app->scene_selection_index = index;
}

static void scene_commit_rename(StandaloneApp *app)
{
    const char *fallback;

    if (app->scene_rename_kind == VOXELPILOT_SCENE_SELECTION_ANNOTATION &&
        app->scene_rename_index >= 0 &&
        app->scene_rename_index < app->annotation_count) {
        fallback = "Region";
        snprintf(app->annotations[app->scene_rename_index].name,
                 sizeof(app->annotations[app->scene_rename_index].name),
                 "%s", app->scene_rename_buffer[0] ? app->scene_rename_buffer : fallback);
    } else if (app->scene_rename_kind == VOXELPILOT_SCENE_SELECTION_REGION &&
               app->scene_rename_index >= 0 &&
               app->scene_rename_index < app->semantic_region_count) {
        fallback = "ASD region";
        snprintf(app->semantic_regions[app->scene_rename_index].name,
                 sizeof(app->semantic_regions[app->scene_rename_index].name),
                 "%s", app->scene_rename_buffer[0] ? app->scene_rename_buffer : fallback);
    }
    app->scene_rename_kind = VOXELPILOT_SCENE_SELECTION_NONE;
    app->scene_rename_index = -1;
}

static void scene_delete_annotation(StandaloneApp *app, int index)
{
    if (index < 0 || index >= app->annotation_count || !app->annotations[index].active) {
        return;
    }
    app->annotations[index].active = 0;
    snprintf(app->scene_undo_label, sizeof(app->scene_undo_label), "%s",
             app->annotations[index].name[0] ? app->annotations[index].name : "Region");
    app->scene_undo_kind = VOXELPILOT_SCENE_SELECTION_ANNOTATION;
    app->scene_undo_index = index;
    if (app->selected_annotation == index) {
        app->selected_annotation = -1;
    }
    if (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_ANNOTATION &&
        app->scene_selection_index == index) {
        app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_NONE;
        app->scene_selection_index = -1;
    }
    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Deleted \"%s\".", app->scene_undo_label);
    set_ui_toast(app, app->annotation_status);
}

static void scene_delete_region(StandaloneApp *app, int index)
{
    if (index < 0 || index >= app->semantic_region_count || app->semantic_regions[index].rejected) {
        return;
    }
    app->semantic_regions[index].rejected = 1;
    snprintf(app->scene_undo_label, sizeof(app->scene_undo_label), "%s",
             app->semantic_regions[index].name[0] ? app->semantic_regions[index].name : "ASD region");
    app->scene_undo_kind = VOXELPILOT_SCENE_SELECTION_REGION;
    app->scene_undo_index = index;
    if (app->semantic_selected_region == index) {
        app->semantic_selected_region = -1;
    }
    if (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_REGION &&
        app->scene_selection_index == index) {
        app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_NONE;
        app->scene_selection_index = -1;
    }
    snprintf(app->semantic_status, sizeof(app->semantic_status),
             "Deleted \"%s\".", app->scene_undo_label);
    set_ui_toast(app, app->semantic_status);
}

static void scene_undo_last_delete(StandaloneApp *app)
{
    if (app->scene_undo_kind == VOXELPILOT_SCENE_SELECTION_ANNOTATION &&
        app->scene_undo_index >= 0 &&
        app->scene_undo_index < app->annotation_count) {
        app->annotations[app->scene_undo_index].active = 1;
        set_ui_toast(app, "Annotation restored.");
    } else if (app->scene_undo_kind == VOXELPILOT_SCENE_SELECTION_REGION &&
               app->scene_undo_index >= 0 &&
               app->scene_undo_index < app->semantic_region_count) {
        app->semantic_regions[app->scene_undo_index].rejected = 0;
        set_ui_toast(app, "Region restored.");
    }
    app->scene_undo_kind = VOXELPILOT_SCENE_SELECTION_NONE;
    app->scene_undo_index = -1;
}

static void scene_toggle_clip_box_visibility(StandaloneApp *app)
{
    if (app->clip_hidden) {
        memcpy(app->clip_min, app->clip_cached_min, sizeof(app->clip_min));
        memcpy(app->clip_max, app->clip_cached_max, sizeof(app->clip_max));
        app->clip_hidden = 0;
    } else {
        memcpy(app->clip_cached_min, app->clip_min, sizeof(app->clip_min));
        memcpy(app->clip_cached_max, app->clip_max, sizeof(app->clip_max));
        app->clip_min[0] = app->clip_min[1] = app->clip_min[2] = 0.0f;
        app->clip_max[0] = app->clip_max[1] = app->clip_max[2] = 1.0f;
        app->clip_hidden = 1;
    }
}

static void scene_add_clip_box(StandaloneApp *app)
{
    if (app->clip_hidden) {
        scene_toggle_clip_box_visibility(app);
        set_ui_toast(app, "Clip box shown.");
        return;
    }
    if (clipping_is_active(app)) {
        set_ui_toast(app, "Clip box already active - VoxelPilot supports one at a time.");
        return;
    }
    app->clip_min[0] = app->clip_min[1] = app->clip_min[2] = 0.25f;
    app->clip_max[0] = app->clip_max[1] = app->clip_max[2] = 0.75f;
    set_ui_toast(app, "Clip box added.");
}

static void scene_add_measurement(StandaloneApp *app)
{
    if (app->measurement_visible) {
        set_ui_toast(app, "Measurement already active - VoxelPilot supports one at a time.");
        return;
    }
    app->measurement_visible = 1;
    app->measurement_target = 0;
    set_ui_toast(app, "Measurement tool on - click two points in the viewport.");
}

static void scene_add_annotation(StandaloneApp *app)
{
    app->annotation_mode = 1;
    set_ui_toast(app, "Click in the viewport to place an annotation.");
}

/* ============================================================
   Row drawing
   ============================================================ */

/* One Discovered-region or Annotation row: colour swatch, searchable
   (or renaming) label, and - on hover or while selected - an eye icon
   plus a "..." menu (Rename / Focus camera / Change colour / Delete).
   Click selects; double-click focuses the camera (see this file's
   header comment on what that does and does not affect). */
static void scene_draw_region_or_annotation_row(StandaloneApp *app, int kind, int index)
{
    bool    is_region = (kind == VOXELPILOT_SCENE_SELECTION_REGION);
    char   *name;
    float  *color;
    int    *hidden_flag;
    float   focus_point[3];
    bool    selected;
    bool    renaming;
    bool    hovered;
    bool    show_affordances;
    char    push_id[48];
    float   row_left_x;
    float   avail_w;
    float   right_edge_x;

    if (is_region) {
        VoxelPilotSemanticRegion *region = &app->semantic_regions[index];
        name = region->name;
        color = region->color;
        hidden_flag = &app->semantic_region_hidden[index];
        focus_point[0] = region->centroid[0] / fmaxf((float)(app->renderer.W - 1), 1.0f);
        focus_point[1] = region->centroid[1] / fmaxf((float)(app->renderer.H - 1), 1.0f);
        focus_point[2] = region->centroid[2] / fmaxf((float)(app->renderer.D - 1), 1.0f);
    } else {
        AnnotationRegion *ann = &app->annotations[index];
        name = ann->name;
        color = ann->color;
        hidden_flag = &app->annotation_hidden[index];
        focus_point[0] = ann->seed[0];
        focus_point[1] = ann->seed[1];
        focus_point[2] = ann->seed[2];
    }

    selected = (app->scene_selection_kind == kind) && (app->scene_selection_index == index);
    renaming = (app->scene_rename_kind == kind) && (app->scene_rename_index == index);

    snprintf(push_id, sizeof(push_id), "scene_row_%d_%d", kind, index);
    ImGui::PushID(push_id);

    row_left_x = ImGui::GetCursorPosX();
    avail_w = ImGui::GetContentRegionAvail().x;
    right_edge_x = row_left_x + avail_w;

    if (*hidden_flag) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.906f, 0.933f, 0.949f, 0.5f));
    }
    ImGui::Selectable("##hit", selected, ImGuiSelectableFlags_AllowOverlap);
    if (*hidden_flag) {
        ImGui::PopStyleColor();
    }
    hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked() && !renaming) {
        app->scene_selection_kind = kind;
        app->scene_selection_index = index;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        scene_focus_camera_on_point(app, focus_point);
        if (is_region) {
            app->semantic_selected_region = index;
        } else {
            app->selected_annotation = index;
        }
    }

    show_affordances = hovered || selected;

    ImGui::SameLine(row_left_x);
    ImGui::ColorButton(
        "##swatch",
        ImVec4(color[0], color[1], color[2], 1.0f),
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoBorder,
        ImVec2(8.0f, 8.0f));
    ImGui::SameLine();

    if (renaming) {
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText(
                "##rename", app->scene_rename_buffer, sizeof(app->scene_rename_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            scene_commit_rename(app);
        } else if (ImGui::IsItemDeactivated()) {
            scene_commit_rename(app);
        }
    } else {
        scene_draw_searchable_label(name, app->scene_search_query);
    }

    /* Spec tree row: a short meta tag on the right. ASD often gives
       several regions the same archetype name ("dense shell-like
       structure"), so R1..Rn / A1..An is what tells the rows apart. The
       eye and "..." controls take this spot while hovered or selected. */
    if (!show_affordances && !renaming) {
        char meta[16];
        snprintf(meta, sizeof(meta), "%s%d", is_region ? "R" : "A", index + 1);
        ImGui::SameLine(right_edge_x - ImGui::CalcTextSize(meta).x);
        ImGui::TextDisabled("%s", meta);
    }

    if (show_affordances) {
        ImGui::SameLine(right_edge_x - 40.0f);
        if (scene_draw_eye_icon("##eye", *hidden_flag == 0)) {
            *hidden_flag = !*hidden_flag;
        }

        ImGui::SameLine(right_edge_x - 20.0f);
        if (ImGui::SmallButton("...")) {
            ImGui::OpenPopup("##row_menu");
        }
    }

    if (ImGui::BeginPopup("##row_menu")) {
        if (ImGui::MenuItem("Rename")) {
            scene_begin_rename(app, kind, index, name);
        }
        if (ImGui::MenuItem("Focus camera")) {
            scene_focus_camera_on_point(app, focus_point);
            if (is_region) {
                app->semantic_selected_region = index;
            } else {
                app->selected_annotation = index;
            }
        }
        if (ImGui::MenuItem("Change colour")) {
            ImGui::OpenPopup("##row_color");
        }
        if (ImGui::MenuItem("Delete")) {
            if (is_region) {
                scene_delete_region(app, index);
            } else {
                scene_delete_annotation(app, index);
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##row_color")) {
        ImGui::ColorEdit3("##c", color);
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

/* A non-indexed (singleton) row: Measurement, Clip box, Camera, or Key
   light. `eye_visible` is NULL for rows with no eye icon (Camera/Key
   light - see this file's header comment on why); otherwise it is
   toggled in place via *eye_visible, and the row dims while hidden. */
static void scene_draw_singleton_row(
    StandaloneApp *app,
    int            kind,
    const char    *label,
    const char    *meta,
    int           *eye_visible)
{
    bool  selected = (app->scene_selection_kind == kind);
    bool  hovered;
    float row_left_x;
    float avail_w;
    float right_edge_x;

    ImGui::PushID(label);

    row_left_x = ImGui::GetCursorPosX();
    avail_w = ImGui::GetContentRegionAvail().x;
    right_edge_x = row_left_x + avail_w;

    if (eye_visible && !*eye_visible) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.906f, 0.933f, 0.949f, 0.5f));
    }
    ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowOverlap);
    if (eye_visible && !*eye_visible) {
        ImGui::PopStyleColor();
    }
    hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
        app->scene_selection_kind = kind;
        app->scene_selection_index = -1;
    }

    if (meta && meta[0]) {
        float meta_w = ImGui::CalcTextSize(meta).x;
        float reserve = eye_visible ? 24.0f : 0.0f;
        ImGui::SameLine(right_edge_x - meta_w - reserve);
        ImGui::TextColored(ImVec4(0.624f, 0.690f, 0.737f, 1.0f), "%s", meta);
    }

    if (eye_visible && (hovered || selected)) {
        ImGui::SameLine(right_edge_x - 18.0f);
        if (scene_draw_eye_icon("##eye", *eye_visible != 0)) {
            if (kind == VOXELPILOT_SCENE_SELECTION_CLIP_BOX) {
                scene_toggle_clip_box_visibility(app);
            } else {
                *eye_visible = !*eye_visible;
            }
        }
    }

    ImGui::PopID();
}

/* ============================================================
   Main entry point
   ============================================================ */

void draw_scene_panel(StandaloneApp *app)
{
    bool has_volume;
    bool scene_open;

    if (!app || !app->scene_visible) {
        return;
    }

    set_default_window_layout(app, "Scene");
    scene_open = app->scene_visible != 0;
    if (!ImGui::Begin("Scene", &scene_open)) {
        app->scene_visible = scene_open ? 1 : 0;
        ImGui::End();
        return;
    }

    ImGui::Text("SCENE");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
    if (ImGui::SmallButton("+")) {
        ImGui::OpenPopup("##scene_add_menu");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add to scene");
    }
    if (ImGui::BeginPopup("##scene_add_menu")) {
        if (ImGui::MenuItem("Annotation")) {
            scene_add_annotation(app);
        }
        if (ImGui::MenuItem("Measurement")) {
            scene_add_measurement(app);
        }
        if (ImGui::MenuItem("Clip box")) {
            scene_add_clip_box(app);
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    has_volume =
        app->renderer.volume_data &&
        app->renderer.W > 0 &&
        app->renderer.H > 0 &&
        app->renderer.D > 0;

    if (!has_volume) {
        /* Empty state: tree hidden, dashed drop zone fills the panel
           (per the spec). Dropping a file here is real - see
           drop_callback in standalone_main.cpp. */
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 box_pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const float inset = 10.0f;
        ImVec2 box_min = ImVec2(box_pos.x + inset, box_pos.y + inset);
        ImVec2 box_max = ImVec2(box_pos.x + avail.x - inset, box_pos.y + avail.y - inset);

        if (box_max.x > box_min.x && box_max.y > box_min.y) {
            scene_draw_dashed_rect(draw_list, box_min, box_max, IM_COL32(42, 54, 64, 255));

            ImGui::SetCursorScreenPos(ImVec2(box_min.x + 12.0f, box_min.y + avail.y * 0.32f));
            /* PushTextWrapPos takes a window-LOCAL x, unlike the
               screen-space coordinates used above for positioning -
               convert by subtracting the window's screen origin. */
            ImGui::PushTextWrapPos(box_max.x - 12.0f - ImGui::GetWindowPos().x);
            ImGui::TextColored(
                ImVec4(0.624f, 0.690f, 0.737f, 1.0f),
                "Drop a .raw or .vpilot file here, or Open volume.");
            ImGui::PopTextWrapPos();

            ImGui::SetCursorScreenPos(ImVec2(box_min.x + 12.0f, box_min.y + avail.y * 0.32f + 44.0f));
            if (ImGui::Button("Open volume...")) {
                browse_volume_file(app);
            }
        }
    } else {
        const char *query = app->scene_search_query;
        bool searching;

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##scene_search", "Search", app->scene_search_query, sizeof(app->scene_search_query));
        searching = query[0] != '\0';
        ImGui::Separator();

        /* Volume root */
        {
            char vol_label[96];
            const char *fname = scene_basename(app->upload_path);
            snprintf(vol_label, sizeof(vol_label), "Volume: %s", fname[0] ? fname : "(unsaved)");
            if (!searching || scene_text_matches_query(vol_label, query, NULL)) {
                bool vol_selected = (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_VOLUME);
                if (ImGui::Selectable(vol_label, vol_selected)) {
                    app->scene_selection_kind = VOXELPILOT_SCENE_SELECTION_VOLUME;
                    app->scene_selection_index = -1;
                }
            }
        }

        /* Discovered regions group */
        {
            int  i;
            int  count = 0;
            bool header_matches;
            bool any_child_matches = false;
            char group_label[64];

            for (i = 0; i < app->semantic_region_count; ++i) {
                if (!app->semantic_regions[i].rejected) {
                    count++;
                }
            }
            snprintf(group_label, sizeof(group_label), "Discovered regions (%d)", count);
            header_matches = !searching || scene_text_matches_query(group_label, query, NULL);
            if (searching && !header_matches) {
                for (i = 0; i < app->semantic_region_count; ++i) {
                    if (app->semantic_regions[i].rejected) {
                        continue;
                    }
                    if (scene_text_matches_query(app->semantic_regions[i].name, query, NULL)) {
                        any_child_matches = true;
                        break;
                    }
                }
            }
            if (count > 0 && (header_matches || any_child_matches)) {
                if (ImGui::TreeNodeEx("##regions_group", ImGuiTreeNodeFlags_DefaultOpen, "%s", group_label)) {
                    for (i = 0; i < app->semantic_region_count; ++i) {
                        if (app->semantic_regions[i].rejected) {
                            continue;
                        }
                        if (searching && !header_matches &&
                            !scene_text_matches_query(app->semantic_regions[i].name, query, NULL)) {
                            continue;
                        }
                        scene_draw_region_or_annotation_row(app, VOXELPILOT_SCENE_SELECTION_REGION, i);
                    }
                    ImGui::TreePop();
                }
            }
        }

        /* Annotations group */
        {
            int  i;
            int  count = 0;
            bool header_matches;
            bool any_child_matches = false;
            char group_label[48];

            for (i = 0; i < app->annotation_count; ++i) {
                if (scene_annotation_listed(app, i)) {
                    count++;
                }
            }
            snprintf(group_label, sizeof(group_label), "Annotations (%d)", count);
            header_matches = !searching || scene_text_matches_query(group_label, query, NULL);
            if (searching && !header_matches) {
                for (i = 0; i < app->annotation_count; ++i) {
                    if (!scene_annotation_listed(app, i)) {
                        continue;
                    }
                    if (scene_text_matches_query(app->annotations[i].name, query, NULL)) {
                        any_child_matches = true;
                        break;
                    }
                }
            }
            if (count > 0 && (header_matches || any_child_matches)) {
                if (ImGui::TreeNodeEx("##annotations_group", ImGuiTreeNodeFlags_DefaultOpen, "%s", group_label)) {
                    for (i = 0; i < app->annotation_count; ++i) {
                        if (!scene_annotation_listed(app, i)) {
                            continue;
                        }
                        if (searching && !header_matches &&
                            !scene_text_matches_query(app->annotations[i].name, query, NULL)) {
                            continue;
                        }
                        scene_draw_region_or_annotation_row(app, VOXELPILOT_SCENE_SELECTION_ANNOTATION, i);
                    }
                    ImGui::TreePop();
                }
            }
        }

        /* Measurement (singleton - see header comment on the data
           model limit) */
        {
            const char *label = "Measurement";
            if (!searching || scene_text_matches_query(label, query, NULL)) {
                char meta[48];
                snprintf(meta, sizeof(meta), "%.2f u", measurement_distance_world(app));
                scene_draw_singleton_row(
                    app, VOXELPILOT_SCENE_SELECTION_MEASUREMENT, label, meta,
                    &app->measurement_visible);
            }
        }

        /* Clip box (singleton; shown only while active or hidden-but-
           cached - "+Clip box" is what creates it) */
        if (clipping_is_active(app) || app->clip_hidden) {
            const char *label = "Clip box";
            if (!searching || scene_text_matches_query(label, query, NULL)) {
                int visible_flag = app->clip_hidden ? 0 : 1;
                scene_draw_singleton_row(
                    app, VOXELPILOT_SCENE_SELECTION_CLIP_BOX, label, NULL, &visible_flag);
            }
        }

        ImGui::Separator();

        /* Camera and Key light - root siblings of Volume, no eye icon
           (see header comment on why). */
        {
            const char *label = "Camera";
            if (!searching || scene_text_matches_query(label, query, NULL)) {
                scene_draw_singleton_row(app, VOXELPILOT_SCENE_SELECTION_CAMERA, label, NULL, NULL);
            }
        }
        {
            const char *label = "Key light";
            if (!searching || scene_text_matches_query(label, query, NULL)) {
                scene_draw_singleton_row(app, VOXELPILOT_SCENE_SELECTION_KEY_LIGHT, label, NULL, NULL);
            }
        }

        /* F2/Delete, scoped to while this panel is open and no text
           field is capturing input (see header comment - full app-wide
           shortcut wiring independent of panel focus is step 11). */
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                if (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_REGION &&
                    app->scene_selection_index >= 0 &&
                    app->scene_selection_index < app->semantic_region_count) {
                    scene_begin_rename(
                        app, VOXELPILOT_SCENE_SELECTION_REGION, app->scene_selection_index,
                        app->semantic_regions[app->scene_selection_index].name);
                } else if (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_ANNOTATION &&
                           app->scene_selection_index >= 0 &&
                           app->scene_selection_index < app->annotation_count) {
                    scene_begin_rename(
                        app, VOXELPILOT_SCENE_SELECTION_ANNOTATION, app->scene_selection_index,
                        app->annotations[app->scene_selection_index].name);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                if (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_REGION &&
                    app->scene_selection_index >= 0 &&
                    app->scene_selection_index < app->semantic_region_count) {
                    scene_delete_region(app, app->scene_selection_index);
                } else if (app->scene_selection_kind == VOXELPILOT_SCENE_SELECTION_ANNOTATION &&
                           app->scene_selection_index >= 0 &&
                           app->scene_selection_index < app->annotation_count) {
                    scene_delete_annotation(app, app->scene_selection_index);
                }
            }
        }

        if (app->scene_undo_kind != VOXELPILOT_SCENE_SELECTION_NONE) {
            ImGui::Separator();
            ImGui::TextWrapped("Deleted \"%s\".", app->scene_undo_label);
            ImGui::SameLine();
            if (ImGui::SmallButton("Undo")) {
                scene_undo_last_delete(app);
            }
        }
    }

    app->scene_visible = scene_open ? 1 : 0;
    ImGui::End();
}
