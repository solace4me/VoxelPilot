/* Bottom tabbed panel host (AI Assist / Histogram / Metadata / Log).
   Step 3 of the Unity-style redesign (see
   docs/superpowers/plans/2026-10-24-unity-style-redesign.md): adds the
   AI Assist and Metadata tabs, deferred from step 2. Getting them
   here needed ImGui::Begin/End (and the set_default_window_layout
   call, and the ai_visible/metadata_visible flags that gated it)
   stripped out of draw_ai_assist_panel/draw_metadata_panel
   (ui_ai_assist_panel.cpp) - those functions no longer own their own
   top-level dockable window identity, so they can only be called from
   inside this window's tab items now, never on their own.

   That also orphaned the "AI Assist"/"Metadata" window names that
   ensure_default_dock_layout (ui_ribbon_and_layout.cpp) used to dock
   by name, and the per-panel visibility checkboxes in
   ui_ribbon_and_layout.cpp's ribbon - both updated in this step to
   dock/toggle this window ("Bottom Panels") as a whole instead.
   ensure_default_dock_layout now docks "Bottom Panels" for both
   existing layout variants (0 and 1); a dedicated position for the
   still-unbuilt "Unity" variant (dock_layout_variant == 2) is step 10.

   Step 4 (see docs/superpowers/specs/2026-10-24-ui-ux-interaction-and-
   flow-spec.md's "Bottom dock" section) corrects the Histogram and Log
   tabs to match that spec instead of a quick first pass:
   - Histogram: 140px tall (was 180px), custom-drawn with ImDrawList
     instead of ImGui::PlotHistogram so each bar can be colored by
     whether it falls inside the current transfer-function window
     (accent) or not (#2E3B45) - PlotHistogram only supports one
     uniform bar color. Dragging on it sets Center/Width directly from
     the drag span.
   - Log: entries now carry a timestamp and a severity
     (VOXELPILOT_LOG_INFO/_WARNING/_ERROR, standalone_app.h) set by
     set_ui_toast's new third parameter; rendered oldest-to-newest
     (was newest-first) with warnings in highlight and errors in
     danger, auto-scrolling to the bottom unless the user has scrolled
     up, plus a Copy all button.
*/

#include <float.h>

#include "imgui.h"

#include "standalone_app.h"

static void draw_histogram_tab(StandaloneApp *app)
{
    if (!app->histogram_ready) {
        ImGui::TextWrapped("Histogram becomes available after a volume is loaded.");
        return;
    }

    ImGui::Text("Range: %.4f to %.4f", app->hist_min_value, app->hist_max_value);

    {
        ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, 140.0f);
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        float low = app->tf_center - 0.5f * app->tf_width;
        float high = app->tf_center + 0.5f * app->tf_width;
        float max_count = 0.0f;
        float bin_w;
        int i;

        ImGui::InvisibleButton("##histogram_canvas", canvas_size);
        bool active = ImGui::IsItemActive();

        draw_list->AddRectFilled(
            canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
            IM_COL32(8, 11, 14, 255));

        for (i = 0; i < HISTOGRAM_BINS; ++i) {
            if (app->histogram[i] > max_count) max_count = app->histogram[i];
        }
        if (max_count <= 0.0f) max_count = 1.0f;

        bin_w = canvas_size.x / (float)HISTOGRAM_BINS;
        for (i = 0; i < HISTOGRAM_BINS; ++i) {
            float norm_pos = (float)i / (float)(HISTOGRAM_BINS - 1);
            int inside = (norm_pos >= low) && (norm_pos <= high);
            float bar_h = (app->histogram[i] / max_count) * canvas_size.y;
            ImVec2 bar_min = ImVec2(canvas_pos.x + (float)i * bin_w, canvas_pos.y + canvas_size.y - bar_h);
            ImVec2 bar_max = ImVec2(canvas_pos.x + (float)(i + 1) * bin_w, canvas_pos.y + canvas_size.y);
            ImU32 color = inside ? IM_COL32(79, 209, 191, 255) /* accent #4FD1BF */
                                  : IM_COL32(46, 59, 69, 255);  /* #2E3B45 */
            draw_list->AddRectFilled(bar_min, bar_max, color);
        }

        draw_list->AddRect(
            canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
            IM_COL32(34, 44, 52, 255)); /* border/default #222C34 */

        if (active) {
            /* Drag sets the window directly: the drag's start and current
               x positions become the window's low/high edges. */
            ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            float current_x = ImGui::GetMousePos().x - canvas_pos.x;
            float start_x = current_x - drag_delta.x;
            float frac_a = start_x / canvas_size.x;
            float frac_b = current_x / canvas_size.x;
            float new_low;
            float new_high;
            float new_width;

            if (frac_a < 0.0f) frac_a = 0.0f;
            if (frac_a > 1.0f) frac_a = 1.0f;
            if (frac_b < 0.0f) frac_b = 0.0f;
            if (frac_b > 1.0f) frac_b = 1.0f;

            new_low = (frac_a < frac_b) ? frac_a : frac_b;
            new_high = (frac_a > frac_b) ? frac_a : frac_b;
            new_width = new_high - new_low;
            if (new_width < 0.02f) new_width = 0.02f;

            app->tf_center = (new_low + new_high) * 0.5f;
            app->tf_width = new_width;
        }
    }
}

static void draw_log_tab(StandaloneApp *app)
{
    if (app->log_history_count < 1) {
        ImGui::TextWrapped("No actions logged yet.");
        return;
    }

    if (ImGui::Button("Copy all")) {
        ImGuiTextBuffer buf;
        int i;
        for (i = 0; i < app->log_history_count; ++i) {
            int idx = (app->log_history_next - app->log_history_count + i + LOG_HISTORY_MAX) % LOG_HISTORY_MAX;
            buf.appendf("[%s] %s\n", app->log_history[idx].timestamp, app->log_history[idx].message);
        }
        ImGui::SetClipboardText(buf.c_str());
    }

    ImGui::BeginChild("##log_scroll", ImVec2(0.0f, 0.0f), false);
    {
        int i;
        for (i = 0; i < app->log_history_count; ++i) {
            int idx = (app->log_history_next - app->log_history_count + i + LOG_HISTORY_MAX) % LOG_HISTORY_MAX;
            VoxelPilotLogEntry *entry = &app->log_history[idx];
            ImVec4 color;
            if (entry->severity == VOXELPILOT_LOG_ERROR) {
                color = ImVec4(0.941f, 0.443f, 0.404f, 1.0f);   /* danger #F07167 */
            } else if (entry->severity == VOXELPILOT_LOG_WARNING) {
                color = ImVec4(0.949f, 0.812f, 0.357f, 1.0f);   /* highlight #F2CF5B */
            } else {
                color = ImVec4(0.910f, 0.933f, 0.949f, 1.0f);   /* text/primary #E8EEF2 */
            }
            ImGui::TextColored(color, "[%s] %s", entry->timestamp, entry->message);
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

void draw_bottom_panels(StandaloneApp *app)
{
    if (!app) return;

    if (ImGui::Begin("Bottom Panels")) {
        if (ImGui::BeginTabBar("##bottom_panels_tabs")) {

            if (ImGui::BeginTabItem("AI Assist")) {
                draw_ai_assist_panel(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Histogram")) {
                draw_histogram_tab(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Metadata")) {
                draw_metadata_panel(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Log")) {
                draw_log_tab(app);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
