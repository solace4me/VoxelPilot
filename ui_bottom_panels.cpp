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
*/

#include <float.h>

#include "imgui.h"

#include "standalone_app.h"

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
                if (app->histogram_ready) {
                    ImGui::Text("Range: %.4f to %.4f", app->hist_min_value, app->hist_max_value);
                    ImGui::PlotHistogram(
                        "##volume_histogram",
                        app->histogram,
                        HISTOGRAM_BINS,
                        0,
                        "Volume intensity distribution",
                        0.0f,
                        FLT_MAX,
                        ImVec2(0.0f, 180.0f));
                } else {
                    ImGui::TextWrapped("Histogram becomes available after a volume is loaded.");
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Metadata")) {
                draw_metadata_panel(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Log")) {
                if (app->log_history_count < 1) {
                    ImGui::TextWrapped("No actions logged yet.");
                } else {
                    ImGui::BeginChild("##log_scroll", ImVec2(0.0f, 0.0f), false);
                    for (int i = 0; i < app->log_history_count; ++i) {
                        int idx = (app->log_history_next - 1 - i + LOG_HISTORY_MAX) % LOG_HISTORY_MAX;
                        ImGui::TextWrapped("%s", app->log_history[idx]);
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
