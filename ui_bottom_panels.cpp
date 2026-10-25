/* Bottom tabbed panel host (AI Assist / Histogram / Metadata / Log).
   Step 2 of the Unity-style redesign (see
   docs/superpowers/plans/2026-10-24-unity-style-redesign.md): only the
   Histogram and Log tabs land here this step, lifted out of
   draw_insights_panel and the step-1 draw_log_panel (both
   standalone_main.cpp). AI Assist and Metadata tabs are deferred to
   step 3, which needs to strip ImGui::Begin/End out of
   draw_ai_assist_panel/draw_metadata_panel first - a separate,
   slightly riskier change, since those functions currently own their
   own top-level dockable window identity.

   No dockspace positioning yet (see ensure_default_dock_layout in
   ui_ribbon_and_layout.cpp, step 10 of the plan) - this opens as an
   undocked floating window for now, same as step 1's Log panel did.
*/

#include <float.h>

#include "imgui.h"

#include "standalone_app.h"

void draw_bottom_panels(StandaloneApp *app)
{
    if (!app) return;

    if (ImGui::Begin("Bottom Panels")) {
        if (ImGui::BeginTabBar("##bottom_panels_tabs")) {

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
