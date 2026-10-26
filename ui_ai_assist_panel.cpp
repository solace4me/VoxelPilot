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
   plain content inside that shared window/tab context.

   Step 4 (docs/superpowers/specs/2026-10-24-ui-ux-interaction-and-
   flow-spec.md's "Metadata" section) rebuilds draw_metadata_panel's
   body as a 4-column label/value grid (ImGui table) instead of a
   plain vertical Text list, adds Data type and Size (from
   upload_data_type/upload_file_size_bytes, already tracked but not
   shown here before), and keeps the full health-status sentence
   (dataset_health_status) as a wrapped line below the grid rather
   than only inside the grid cell.

   Step 4 (part 2) rebuilds draw_ai_assist_panel's body against the
   same spec's "AI Assist" section: a prompt input + five
   quick-action chips (Auto enhance, Discover regions, Health check,
   Guided review, Export bundle) + a "Last action" card on the
   right (an ImGui table, two columns) replace the old "Auto Enhance
   View"/"Health Check"/"Export Bundle"/standalone "Prompt" controls
   that lived inside the always-open "Transfer & Health" header.
   The old Discovery/Transfer & Health/More AI Tools collapsing
   headers stay - renamed to default-closed, per the spec's own
   progressive-disclosure principle now that the primary surface
   covers the common case - and keep everything that has no
   primary-chip equivalent: the ASD sliders and region-browsing list,
   Preview Auto Window (preview without applying - distinct from
   Auto enhance), Restore Previous View (same undo_transfer_settings
   call the new "Undo" button in the Last Action card makes - two
   entry points to the same action), the Transfer Advisor, Quant
   Metrics, Export Insight Report, and Scout Focus Slice. The
   duplicate "Apply Prompt"/"Health Check"/"Export Bundle" controls
   that had no distinct behavior from their new primary-chip
   equivalents were removed rather than kept as redundant copies.

   The "Last action" card leans on whatever each action already
   wrote - apply_auto_enhance_transfer already sets ai_assist_status
   with a full description and the transfer_undo_* fields with real
   before/after values; Discover regions/Health check/Export bundle
   copy their own richer status fields (semantic_status/
   dataset_health_status/report_status) into ai_assist_status right
   after the call, since those functions do not set it themselves.
   Guided review has no status field of its own, so it gets a fixed
   one-line description. Before/after values only show for the
   transfer-related actions, since transfer_undo_* is the only
   before/after state this app tracks - the card does not fabricate
   one for the others.

   Discover regions/Export bundle read
   performance_result.safe_to_run_asd/safe_to_export_mask directly
   to decide whether to show the chip disabled, rather than calling
   performance_allows_optional_action every frame for that check -
   that function also writes ai_assist_status when it blocks
   something, which would stomp the Last Action text every frame
   while blocked, not just on a real click. It is still called at
   click time, same as before, as the authoritative gate. */

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

    /* The prompt row comes first. The volume/range/ASD readout that used
       to sit above it duplicated the Metadata tab and pushed the prompt
       and Last action card to the bottom of the 240 px dock; the ASD
       counts now live in the Discovery section below. */
    if (!has_volume) {
        ImGui::TextWrapped(
            "AI Assist needs a loaded volume first: use Browse... in Controls to set "
            "Width, Height, Depth, and Data Type, then click Load Volume.");
        if (ImGui::Button("Show Me: Volume Import Step")) {
            app->walkthrough_visible = 1;
            app->walkthrough_step = 1;
        }
        ImGui::Separator();
    }

    /* Primary surface, per docs/superpowers/specs/2026-10-24-ui-ux-
       interaction-and-flow-spec.md's "Bottom dock > AI Assist": a
       prompt input, a row of quick-action chips, and a "Last action"
       card. Two columns via a table so the card sits on the right.
       Discover regions/Export bundle stay gated by the same Safety
       checks the old Discovery/Transfer & Health buttons used
       (safe_to_run_asd/safe_to_export_mask, already refreshed every
       frame by draw_top_ribbon) - read directly here instead of
       through performance_allows_optional_action, since that function
       also writes ai_assist_status as a side effect and calling it
       every frame just to decide whether to disable a chip would
       stomp the Last Action text even when nothing was clicked. */
    if (ImGui::BeginTable("##ai_assist_layout", 2, ImGuiTableFlags_None)) {
        int blocked_asd = !app->performance_result.safe_to_run_asd;
        int blocked_export = !app->performance_result.safe_to_export_mask;
        bool prompt_submitted;

        ImGui::TableSetupColumn("Main", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("LastAction", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        /* Prompt fills the column, leaving room for the Apply button. */
        ImGui::SetNextItemWidth(
            -(ImGui::CalcTextSize("Apply").x +
              ImGui::GetStyle().FramePadding.x * 2.0f +
              ImGui::GetStyle().ItemSpacing.x));
        if (app->focus_ai_prompt) {   /* AI > Ask VoxelPilot, Ctrl+K */
            ImGui::SetKeyboardFocusHere();
            app->focus_ai_prompt = 0;
        }
        prompt_submitted = ImGui::InputTextWithHint(
            "##ai_prompt",
            "Ask VoxelPilot",
            app->ai_prompt,
            sizeof(app->ai_prompt),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Apply") || prompt_submitted) {
            apply_ai_prompt_to_app(app);
        }

        if (ImGui::Button("Auto enhance")) {
            apply_auto_enhance_transfer(app, "AI Assist");
        }

        voxelpilot_same_line_if_fits("Discover regions");
        ImGui::BeginDisabled(blocked_asd);
        if (ImGui::Button("Discover regions")) {
            if (performance_allows_optional_action(app, "Discover regions", 1, 0)) {
                run_adaptive_semantic_discovery(app);
                refresh_performance_governor(app);
                snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "%s", app->semantic_status);
            }
        }
        ImGui::EndDisabled();
        if (blocked_asd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", app->performance_result.recommendation);
        }

        voxelpilot_same_line_if_fits("Health check");
        if (ImGui::Button("Health check")) {
            update_dataset_health(app);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "%s", app->dataset_health_status);
        }

        voxelpilot_same_line_if_fits("Guided review");
        if (ImGui::Button("Guided review")) {
            start_guided_review(app);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "Guided review started.");
        }

        voxelpilot_same_line_if_fits("Export bundle");
        ImGui::BeginDisabled(blocked_export);
        if (ImGui::Button("Export bundle")) {
            if (performance_allows_optional_action(app, "Export bundle", 0, 1)) {
                export_ai_review_bundle(app);
                refresh_performance_governor(app);
                snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "%s", app->report_status);
            }
        }
        ImGui::EndDisabled();
        if (blocked_export && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", app->performance_result.recommendation);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.624f, 0.690f, 0.737f, 1.0f), "LAST ACTION");
        ImGui::Separator();
        if (app->ai_assist_status[0]) {
            ImGui::TextWrapped("%s", app->ai_assist_status);
        } else {
            ImGui::TextWrapped("No AI action yet.");
        }
        if (app->transfer_undo_available) {
            ImGui::Spacing();
            ImGui::Text("Before: %.3f / %.3f / %.2f",
                        app->transfer_undo_center, app->transfer_undo_width, app->transfer_undo_opacity_scale);
            ImGui::Text("After:  %.3f / %.3f / %.2f",
                        app->tf_center, app->tf_width, app->tf_opacity_scale);
            if (ImGui::Button("Undo")) {
                undo_transfer_settings(app);
            }
        }
        ImGui::Separator();
        ImGui::TextColored(
            voxelpilot_pressure_color(app->performance_result.status),
            "Safety: %s", app->performance_result.status_label);
        if (app->performance_result.primary_reason[0]) {
            ImGui::TextWrapped("%s", app->performance_result.primary_reason);
        }

        ImGui::EndTable();
    }

    if (has_volume && app->performance_result.status != VOXELPILOT_GOVERNOR_HEALTHY) {
        draw_performance_safety_block(app);
    }

        if (ImGui::CollapsingHeader("Discovery")) {
        draw_help_marker("Adaptive Semantic Discovery finds candidate regions and labels them cautiously.");
        if (has_volume) {
            ImGui::Text("ASD: %d region(s), %d relation(s), %d correction(s) in memory",
                        app->semantic_region_count,
                        app->semantic_relation_count,
                        app->semantic_correction_count);
        }
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
            /* Iterate all regions (not just the first 3) and skip any that
               are rejected or hidden via the Scene panel eye icon (step 5,
               ui_scene_panel.cpp) - Delete there sets rejected=1, so a
               region the user just removed should not keep showing up
               here. Still caps the list at 3 shown, same as before. */
            for (int i = 0, shown = 0; i < app->semantic_region_count && shown < 3; ++i) {
                VoxelPilotSemanticRegion *region = &app->semantic_regions[i];
                char label[128];
                if (region->rejected || app->semantic_region_hidden[i]) {
                    continue;
                }
                shown++;
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
                        "Archetype, match score and stability come from local shape/intensity heuristics\n"
                        "(connected components + geometry), not a trained model. They are not probabilities.");
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

        if (ImGui::CollapsingHeader("Transfer & Health")) {
            if (!has_volume || !app->histogram_ready) {
                ImGui::TextWrapped("Load a volume and build its histogram to enable transfer advice.");
            } else {
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
            }
        }

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
    char dims_text[64];
    char voxels_text[32];
    char size_text[32];
    char spacing_text[64];
    char range_text[64];
    ImVec4 label_color = ImVec4(0.624f, 0.690f, 0.737f, 1.0f); /* text/secondary #9FB0BC */
    size_t voxel_count;

    if (!app) {
        return;
    }

    voxel_count =
        (size_t)app->renderer.W *
        (size_t)app->renderer.H *
        (size_t)app->renderer.D;

    snprintf(dims_text, sizeof(dims_text), "%d x %d x %d", app->renderer.W, app->renderer.H, app->renderer.D);
    snprintf(voxels_text, sizeof(voxels_text), "%zu", voxel_count);
    if (app->upload_file_size_known) {
        snprintf(size_text, sizeof(size_text), "%.1f MB", (double)app->upload_file_size_bytes / (1024.0 * 1024.0));
    } else {
        snprintf(size_text, sizeof(size_text), "Unknown");
    }
    snprintf(spacing_text, sizeof(spacing_text), "%.3f / %.3f / %.3f",
             app->voxel_spacing[0], app->voxel_spacing[1], app->voxel_spacing[2]);
    snprintf(range_text, sizeof(range_text), "%.4f to %.4f", app->hist_min_value, app->hist_max_value);

    ImGui::Text("Dataset Summary");
    ImGui::Separator();

    /* 4-column label/value grid, per docs/superpowers/specs/2026-10-24-
       ui-ux-interaction-and-flow-spec.md's Metadata tab: Dimensions,
       Data type, Voxels, Size, Spacing, Value range, Palette, Clipping,
       Health score - two label/value pairs per row. */
    if (ImGui::BeginTable("##metadata_grid", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(label_color, "Dimensions");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", dims_text);
        ImGui::TableSetColumnIndex(2); ImGui::TextColored(label_color, "Data type");
        ImGui::TableSetColumnIndex(3); ImGui::Text("%s", k_volume_data_type_labels[app->upload_data_type]);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(label_color, "Voxels");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", voxels_text);
        ImGui::TableSetColumnIndex(2); ImGui::TextColored(label_color, "Size");
        ImGui::TableSetColumnIndex(3); ImGui::Text("%s", size_text);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(label_color, "Spacing");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", spacing_text);
        ImGui::TableSetColumnIndex(2); ImGui::TextColored(label_color, "Value range");
        ImGui::TableSetColumnIndex(3); ImGui::Text("%s", range_text);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(label_color, "Palette");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", k_tf_palette_labels[app->tf_palette]);
        ImGui::TableSetColumnIndex(2); ImGui::TextColored(label_color, "Clipping");
        ImGui::TableSetColumnIndex(3); ImGui::Text("%s", clipping_is_active(app) ? "Active" : "Full Volume");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(label_color, "Health score");
        ImGui::TableSetColumnIndex(1);
        if (app->dataset_health_ready) {
            ImGui::Text("%.0f/100", app->dataset_health.score);
        } else {
            ImGui::TextWrapped("Not yet checked");
        }

        ImGui::EndTable();
    }

    /* dataset_health_status already folds in level/score/summary when
       ready (see update_dataset_health, standalone_main.cpp), or a
       fallback explanation otherwise - the grid cell above is just the
       quick-glance number. */
    ImGui::TextWrapped("Health: %s", app->dataset_health_status);
    ImGui::TextWrapped("Dataset status: %s", app->upload_status);
}
