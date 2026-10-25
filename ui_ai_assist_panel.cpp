/* AI Assist panel and Metadata panel: ASD discovery, guided review,
   transfer advisor, dataset health, AI review bundle export, and the
   dataset metadata readout.
   Extracted from standalone_main.cu as part of the Tier 3c module split
   (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   voxelpilot_score_color and voxelpilot_same_line_if_fits stay static
   here - private layout/color helpers with no caller outside this
   file (confirmed by the same dependency trace used for every step).

   Everything else draw_ai_assist_panel and draw_metadata_panel call
   that isn't a common/*.h library function was already cross-file
   before this step (apply_render_quality_preset,
   refresh_performance_governor, performance_allows_optional_action,
   draw_performance_safety_block from step 4; run_semantic_demo_workflow,
   update_quant_metrics, export_ai_review_bundle*, export_insight_report
   from step 1; semantic_relationship_note from step 2) or is newly
   made cross-file by this step: apply_ai_prompt_to_app,
   apply_auto_enhance_transfer, apply_transfer_settings_with_undo,
   clipping_is_active, dataset_health_level_label, draw_help_marker,
   run_adaptive_semantic_discovery, set_ui_toast, start_guided_review, undo_transfer_settings, and
   update_dataset_health all stay defined in standalone_main.cu
   (most have callers elsewhere too - the ribbon, controls panel,
   keyboard shortcuts - that aren't moving), dropped `static`, and
   gained declarations in standalone_app.h.

   apply_ai_prompt_to_app, start_guided_review, and undo_transfer_settings
   currently have no caller outside this panel, but were left in
   standalone_main.cu alongside their closely related siblings
   (apply_auto_enhance_transfer, apply_transfer_settings_with_undo,
   set_ui_toast) rather than special-cased into this file: extracting
   three isolated functions out of a block of a dozen very similar
   ones would have meant three more separate, scattered edits to
   standalone_main.cu for no real benefit - "defined there, declared
   here" is a one-line entry per function here regardless of which
   file ends up defining it.
   Step 3 of docs/superpowers/plans/2026-10-24-unity-style-redesign.md
   (Unity-style redesign): draw_ai_assist_panel and draw_metadata_panel
   no longer call ImGui::Begin/End or set_default_window_layout - they
   lost their own top-level window identity and the ai_visible/
   metadata_visible flags that gated it, and are now called only from
   ui_bottom_panels.cpp's tab bar (alongside Histogram and Log) as
   plain content inside that shared window/tab context. */

#include <stdio.h>

#include "standalone_app.h"

static ImVec4 voxelpilot_score_color(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.5f) {
        float k = t / 0.5f;
        return ImVec4(0.85f, 0.35f + 0.45f * k, 0.25f, 1.0f);
    }
    {
        float k = (t - 0.5f) / 0.5f;
        return ImVec4(0.85f - 0.65f * k, 0.80f, 0.25f + 0.10f * k, 1.0f);
    }
}

static void voxelpilot_same_line_if_fits(const char *next_label)
{
    ImGuiStyle *style = &ImGui::GetStyle();
    ImVec2 label_size = ImGui::CalcTextSize(next_label);
    float next_width = label_size.x + style->FramePadding.x * 2.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > next_width + style->ItemSpacing.x) {
        ImGui::SameLine();
    }
}

void draw_ai_assist_panel(StandaloneApp *app)
{
    int has_volume;
    float suggested_center = 0.0f;
    float suggested_width = 0.0f;
    float suggested_opacity = 0.0f;
    int has_transfer_suggestion = 0;
    VoxelPilotTransferAdvisorResult transfer_advisor;
    int has_transfer_advisor = 0;

    if (!app) {
        return;
    }

    has_volume =
        app->renderer.volume_data &&
        app->renderer.W > 0 &&
        app->renderer.H > 0 &&
        app->renderer.D > 0;

    memset(&transfer_advisor, 0, sizeof(transfer_advisor));
    if (has_volume && app->histogram_ready) {
        has_transfer_suggestion = voxelpilot_compute_auto_enhance_transfer(
            app->histogram,
            HISTOGRAM_BINS,
            &suggested_center,
            &suggested_width,
            &suggested_opacity);
        has_transfer_advisor = voxelpilot_compute_transfer_advisor(
            app->histogram,
            HISTOGRAM_BINS,
            app->tf_center,
            app->tf_width,
            app->tf_opacity_scale,
            app->semantic_region_count,
            &transfer_advisor);
    }

    ImGui::Text("AI Assist");
        draw_help_marker("Local tools only: transfer suggestions, health checks, semantic discovery, and export helpers.");
        ImGui::Separator();

        if (!has_volume) {
            ImGui::TextWrapped(
                "AI Assist needs a loaded volume first: use Browse... in Controls to set "
                "Width, Height, Depth, and Data Type, then click Load Volume.");
            if (ImGui::Button("Show Me: Volume Import Step")) {
                app->walkthrough_visible = 1;
                app->walkthrough_step = 1;
            }
        } else {
            ImGui::Text("Volume: %d x %d x %d", app->renderer.W, app->renderer.H, app->renderer.D);
            ImGui::Text("Range: %.4f to %.4f", app->hist_min_value, app->hist_max_value);
            ImGui::Text("ASD: %d region(s), %d relation(s)", app->semantic_region_count, app->semantic_relation_count);
            ImGui::Text("ASD memory: %d correction(s)", app->semantic_correction_count);
            ImGui::Text("ASD safety: %s", app->performance_result.safe_to_run_asd ? "Ready" : "Blocked");
            if (app->performance_result.status != VOXELPILOT_GOVERNOR_HEALTHY) {
                draw_performance_safety_block(app);
            }
        }

        if (ImGui::CollapsingHeader("Discovery", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_help_marker("Adaptive Semantic Discovery finds candidate regions and labels them cautiously.");
        if (ImGui::Button("Discover Regions")) {
            if (performance_allows_optional_action(app, "ASD discovery", 1, 0)) {
                run_adaptive_semantic_discovery(app);
                refresh_performance_governor(app);
            }
        }
        voxelpilot_same_line_if_fits("Start Guided Review");
        if (ImGui::Button("Start Guided Review")) {
            start_guided_review(app);
        }
        voxelpilot_same_line_if_fits("Run Semantic Demo");
        if (ImGui::Button("Run Semantic Demo")) {
            if (performance_allows_optional_action(app, "Semantic demo", 1, 0)) {
                run_semantic_demo_workflow(app, "Semantic demo");
                refresh_performance_governor(app);
            }
        }

        ImGui::SliderInt("Max Regions", &app->semantic_max_regions, 1, MAX_ANNOTATIONS);
        ImGui::SliderFloat("Sensitivity", &app->semantic_sensitivity, 0.0f, 1.0f);
        ImGui::InputInt("Min Voxels", &app->semantic_min_region_voxels);
        if (app->semantic_min_region_voxels < 1) app->semantic_min_region_voxels = 1;

        ImGui::Separator();
        ImGui::Text("ASD Summary");
        ImGui::SameLine();
        draw_help_marker("Review the top discovered semantic regions and choose one to highlight.");
        ImGui::TextColored(
            ImVec4(0.55f, 0.62f, 0.68f, 1.0f),
            "Heuristic scores from shape/intensity geometry - not a trained model.");
        if (app->semantic_region_count > 0) {
            if (app->semantic_status[0]) {
                ImGui::TextWrapped("Status: %s", app->semantic_status);
            }
            ImGui::BeginGroup();
            int visible_regions = app->semantic_region_count < 3 ? app->semantic_region_count : 3;
            for (int i = 0; i < visible_regions; ++i) {
                VoxelPilotSemanticRegion *region = &app->semantic_regions[i];
                char label[128];
                snprintf(label, sizeof(label), "#%d %s", i + 1, region->name[0] ? region->name : "ASD candidate");
                bool selected = (app->semantic_selected_region == i);
                ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemClicked()) {
                    app->semantic_selected_region = i;
                }
                ImGui::SameLine();
                ImGui::Text("%s", region->archetype);
                ImGui::SameLine();
                {
                    char conf_overlay[16];
                    snprintf(conf_overlay, sizeof(conf_overlay), "%.0f%%", region->confidence * 100.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, voxelpilot_score_color(region->confidence));
                    ImGui::ProgressBar(region->confidence, ImVec2(52.0f, 0.0f), conf_overlay);
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine();
                {
                    char stab_overlay[16];
                    snprintf(stab_overlay, sizeof(stab_overlay), "%.0f%%", region->stability_score * 100.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, voxelpilot_score_color(region->stability_score));
                    ImGui::ProgressBar(region->stability_score, ImVec2(52.0f, 0.0f), stab_overlay);
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Archetype, confidence, and stability come from local shape/intensity heuristics\n"
                        "(connected components + geometry), not a trained model.");
                }
            }
            if (app->semantic_selected_region >= 0 && app->semantic_selected_region < app->semantic_region_count) {
                VoxelPilotSemanticRegion *selected = &app->semantic_regions[app->semantic_selected_region];
                ImGui::TextWrapped("Selected: %s | %s | %zu voxels", selected->name, selected->category, selected->voxel_count);
                if (selected->label_id != 0) {
                    const char *relation_note = semantic_relationship_note(app, selected->label_id);
                    if (relation_note[0]) {
                        ImGui::TextWrapped("Relation note: %s", relation_note);
                    }
                }
            }
            ImGui::EndGroup();
        } else {
            ImGui::TextWrapped("No ASD regions discovered yet. Click Discover Regions to run ASD-Lite.");
        }
        }

        if (ImGui::CollapsingHeader("Transfer & Health", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!has_volume || !app->histogram_ready) {
                ImGui::TextWrapped("Load a volume and build its histogram to enable transfer advice.");
            } else {
                if (ImGui::Button("Auto Enhance View")) {
                    apply_auto_enhance_transfer(app, "AI Assist");
                }
                voxelpilot_same_line_if_fits("Preview Auto Window");
                if (ImGui::Button("Preview Auto Window")) {
                    if (has_transfer_suggestion) {
                        snprintf(app->ai_assist_status,
                                 sizeof(app->ai_assist_status),
                                 "Preview - not applied: center %.3f, width %.3f, opacity %.2f.",
                                 suggested_center,
                                 suggested_width,
                                 suggested_opacity);
                        set_ui_toast(app, "Preview ready. No settings changed.");
                    } else {
                        snprintf(app->ai_assist_status,
                                 sizeof(app->ai_assist_status),
                                 "Auto window needs a populated histogram.");
                    }
                }
                if (app->transfer_undo_available) {
                    voxelpilot_same_line_if_fits("Restore Previous View");
                    if (ImGui::Button("Restore Previous View")) {
                        undo_transfer_settings(app);
                    }
                }
                ImGui::InputText("Prompt", app->ai_prompt, sizeof(app->ai_prompt));
                if (ImGui::Button("Apply Prompt")) {
                    apply_ai_prompt_to_app(app);
                }
                voxelpilot_same_line_if_fits("Health Check");
                if (ImGui::Button("Health Check")) {
                    update_dataset_health(app);
                }
                voxelpilot_same_line_if_fits("Export Bundle");
                if (ImGui::Button("Export Bundle")) {
                    if (performance_allows_optional_action(app, "AI review bundle", 1, 1)) {
                        export_ai_review_bundle(app);
                        refresh_performance_governor(app);
                    }
                }
            }
        }

        ImGui::TextWrapped("%s", app->ai_assist_status);

        if (ImGui::CollapsingHeader("More AI Tools")) {
            ImGui::Text("Advisor");
            ImGui::TextColored(
                ImVec4(0.55f, 0.62f, 0.68f, 1.0f),
                "Heuristic confidence from histogram shape - not a trained model.");
            if (!has_volume || !app->histogram_ready) {
                ImGui::TextWrapped("Load a volume to see AI Transfer Advisor recommendations here.");
            } else if (has_transfer_advisor) {
                ImGui::Text("%s | %.0f%%", transfer_advisor.action, transfer_advisor.confidence * 100.0f);
                if (transfer_advisor.has_recommendation) {
                    ImGui::Text("%.3f / %.3f / %.2f",
                                transfer_advisor.tf_center,
                                transfer_advisor.tf_width,
                                transfer_advisor.tf_opacity_scale);
                    if (ImGui::Button("Apply Advisor Settings")) {
                        apply_transfer_settings_with_undo(
                            app,
                            transfer_advisor.tf_center,
                            transfer_advisor.tf_width,
                            transfer_advisor.tf_opacity_scale,
                            "Advisor settings");
                    }
                }
            } else {
                ImGui::TextWrapped("Advisor could not summarize this histogram.");
            }

            ImGui::Separator();
            if (ImGui::Button("Refresh Quant Metrics")) {
                update_quant_metrics(app);
            }
            voxelpilot_same_line_if_fits("Export Insight Report");
            if (ImGui::Button("Export Insight Report")) {
                export_insight_report(app);
            }
            if (ImGui::Button("Scout Focus Slice")) {
                float focus_slice = 0.0f;
                float focus_score = 0.0f;
                if (voxelpilot_find_high_variance_axial_slice(
                        app->renderer.volume_data,
                        app->renderer.W,
                        app->renderer.H,
                        app->renderer.D,
                        &focus_slice,
                        &focus_score)) {
                    app->slice_z = focus_slice;
                    app->ai_focus_slice = focus_slice;
                    app->ai_focus_score = focus_score;
                    snprintf(app->ai_assist_status,
                             sizeof(app->ai_assist_status),
                             "Scout moved Axial Z to %.3f.",
                             app->ai_focus_slice);
                    set_ui_toast(app, "Focus slice updated.");
                } else {
                    snprintf(app->ai_assist_status,
                             sizeof(app->ai_assist_status),
                             "Focus scout needs a loaded 3D volume.");
                }
            }
        }
}
void draw_metadata_panel(StandaloneApp *app)
{
    if (!app) {
        return;
    }

    size_t voxel_count =
        (size_t)app->renderer.W *
        (size_t)app->renderer.H *
        (size_t)app->renderer.D;

    ImGui::Text("Dataset Summary");
    ImGui::Separator();
    ImGui::Text("Dimensions: %d x %d x %d",
                app->renderer.W, app->renderer.H, app->renderer.D);
    ImGui::Text("Voxel Count: %zu", voxel_count);
    ImGui::Text("Value Range: %.4f to %.4f",
                app->hist_min_value, app->hist_max_value);
    ImGui::Text("Histogram Peak: %.0f samples", app->hist_peak_count);
    ImGui::Text("Render Target: %d x %d", app->render_w, app->render_h);
    ImGui::Text("Voxel Spacing: %.3f / %.3f / %.3f",
                app->voxel_spacing[0],
                app->voxel_spacing[1],
                app->voxel_spacing[2]);
    ImGui::Text("Transfer Palette: %s", k_tf_palette_labels[app->tf_palette]);
    ImGui::Text("Clipping State: %s", clipping_is_active(app) ? "Active" : "Full Volume");
    if (app->dataset_health_ready) {
        ImGui::Text("Health: %s %.0f/100",
                    dataset_health_level_label(app->dataset_health.level),
                    app->dataset_health.score);
        ImGui::TextWrapped("Health summary: %s", app->dataset_health.summary);
    } else {
        ImGui::TextWrapped("Health summary: %s", app->dataset_health_status);
    }
    ImGui::TextWrapped("Dataset status: %s", app->upload_status);
}
