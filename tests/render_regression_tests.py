import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
VERIFY_SCRIPT = ROOT / "tools" / "verify_render_snapshot.py"
STANDALONE_MAIN = ROOT / "standalone_main.cpp"
WORKSPACE_PRESETS = ROOT / "workspace_presets.cpp"
AI_ASSIST_PANEL = ROOT / "ui_ai_assist_panel.cpp"
CONTROLS_PANEL = ROOT / "ui_controls_panel.cpp"
RIBBON_AND_LAYOUT = ROOT / "ui_ribbon_and_layout.cpp"
DEFAULT_STATE_HEADER = ROOT / "common" / "default_state.h"




def extract_static_function(source, name):
    marker = f"static void {name}("
    start = source.index(marker)
    next_static = source.find("\nstatic ", start + len(marker))
    if next_static == -1:
        return source[start:]
    return source[start:next_static]

def extract_nonstatic_function(source, name):
    marker = f"void {name}("
    start = source.index(marker)
    boundaries = []
    for prefix in ("\nvoid ", "\nstatic ", "\nint "):
        idx = source.find(prefix, start + len(marker))
        if idx != -1:
            boundaries.append(idx)
    if not boundaries:
        return source[start:]
    return source[start:min(boundaries)]

def load_verifier():
    assert VERIFY_SCRIPT.exists(), f"missing verifier script: {VERIFY_SCRIPT}"
    spec = importlib.util.spec_from_file_location("verify_render_snapshot", VERIFY_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RenderRegressionTests(unittest.TestCase):
    def test_png_metrics_detect_visible_bbox(self):
        verifier = load_verifier()
        rgba_pixels = bytes(
            [
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                10, 20, 30, 255,
            ]
        )

        metrics = verifier.compute_pixel_metrics(2, 2, rgba_pixels)

        self.assertEqual(metrics["max_luma"], 20)
        self.assertEqual(metrics["visible_pixels"], 1)
        self.assertEqual(metrics["visible_bbox"], [1, 1, 1, 1])

    def test_foot_baseline_thresholds_require_visible_render(self):
        verifier = load_verifier()

        metrics = {
            "width": 1024,
            "height": 1024,
            "mean_luma": 8.5,
            "max_luma": 120,
            "visible_pixels": 120_000,
            "visible_bbox": [80, 0, 804, 912],
            "render_ms": 120.0,
        }

        failures = verifier.validate_foot_baseline(metrics)

        self.assertEqual(failures, [])



    def test_auto_enhance_view_has_single_home_in_ai_assist(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        ai_assist_source = AI_ASSIST_PANEL.read_text(encoding="utf-8")
        controls_source = CONTROLS_PANEL.read_text(encoding="utf-8")
        controls_panel = extract_nonstatic_function(controls_source, "draw_controls_panel")
        ai_assist_panel = extract_nonstatic_function(ai_assist_source, "draw_ai_assist_panel")

        # Step 4 of the Unity redesign (see docs/superpowers/specs/2026-10-24-
        # ui-ux-interaction-and-flow-spec.md's "Bottom dock > AI Assist")
        # renamed this from a Transfer & Health button labeled "Auto Enhance
        # View" to a primary-surface quick-action chip labeled "Auto
        # enhance", matching the spec's exact wording - same underlying
        # apply_auto_enhance_transfer call, same "single home" invariant.
        total_count = (
            source.count('ImGui::Button("Auto enhance")')
            + ai_assist_source.count('ImGui::Button("Auto enhance")')
        )
        self.assertEqual(total_count, 1)
        self.assertNotIn('ImGui::Button("Auto enhance")', controls_panel)
        self.assertIn('ImGui::Button("Auto enhance")', ai_assist_panel)

    def test_workspace_uses_spec_layout_scene_left_inspector_right_dock_below(self):
        # UI/UX spec "Layout": Scene on the left, the Inspector (Controls,
        # with Insights as a tab) on the right, and the slice strip and
        # bottom dock under the viewport only.
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        ribbon_source = RIBBON_AND_LAYOUT.read_text(encoding="utf-8")
        gui = extract_static_function(source, "draw_gui")
        dock_layout = extract_nonstatic_function(ribbon_source, "ensure_default_dock_layout")

        self.assertIn("void draw_top_ribbon(StandaloneApp *app)", ribbon_source)
        self.assertLess(gui.index("draw_top_ribbon(app);"), gui.index("draw_controls_panel(app);"))
        self.assertIn("ImGuiDir_Left", dock_layout)
        self.assertIn("ImGuiDir_Right", dock_layout)
        self.assertIn("ImGuiDir_Down", dock_layout)
        self.assertIn('DockBuilderDockWindow("Scene", dock_scene_id)', dock_layout)
        self.assertIn('DockBuilderDockWindow("Controls", dock_left_id)', dock_layout)
        self.assertIn('DockBuilderDockWindow("Insights", dock_right_id)', dock_layout)
        self.assertIn('DockBuilderDockWindow("Slices", dock_slices_id)', dock_layout)
        self.assertIn('DockBuilderDockWindow("Bottom Panels", dock_bottom_id)', dock_layout)
        self.assertNotIn('DockBuilderDockWindow("Status"', dock_layout)

    def test_left_controls_panel_retains_import_and_export_actions(self):
        controls_source = CONTROLS_PANEL.read_text(encoding="utf-8")
        controls_panel = extract_nonstatic_function(controls_source, "draw_controls_panel")

        self.assertIn('ImGui::Text("Volume Import")', controls_panel)
        self.assertIn('ImGui::Button("Browse...")', controls_panel)
        self.assertIn('ImGui::Button("Load Volume")', controls_panel)
        self.assertIn('ImGui::Button("Save Snapshot")', controls_panel)

    def test_workspace_defaults_are_shared_between_startup_and_reset(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        header_text = DEFAULT_STATE_HEADER.read_text(encoding="utf-8")
        reset_fn = extract_nonstatic_function(source, "reset_workspace")
        main_fn = source[source.index("int main("):]

        self.assertIn("VoxelPilotDefaultState", header_text)
        self.assertIn("apply_default_workspace_state(app, 0)", reset_fn)
        self.assertIn("apply_default_workspace_state(&app, 1)", main_fn)
        self.assertNotIn("app.render_w = DEFAULT_RENDER_W", main_fn)
        self.assertNotIn("app.render_h = DEFAULT_RENDER_H", main_fn)
        self.assertNotIn("app.tf_center = 0.45f", main_fn)
        self.assertNotIn("app.annotation_tolerance = 0.035f", main_fn)
    def test_sidebar_removes_cinematic_duplication_and_about_panel(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        controls_source = CONTROLS_PANEL.read_text(encoding="utf-8")
        ribbon_source = RIBBON_AND_LAYOUT.read_text(encoding="utf-8")
        controls_panel = extract_nonstatic_function(controls_source, "draw_controls_panel")
        ribbon = extract_nonstatic_function(ribbon_source, "draw_top_ribbon")
        dock_layout = extract_nonstatic_function(ribbon_source, "ensure_default_dock_layout")
        gui = extract_static_function(source, "draw_gui")

        self.assertIn('ImGui::Checkbox("Cinematic"', ribbon)
        self.assertNotIn('ImGui::Checkbox("Cinematic Mode"', controls_panel)
        self.assertNotIn('DockBuilderDockWindow("About"', dock_layout)
        self.assertNotIn("draw_about_panel(app);", gui)
        self.assertNotIn("static void draw_about_panel", source)
        self.assertNotIn("about_visible", source)

    def test_ai_assist_uses_progressive_disclosure_and_compact_copy(self):
        ai_assist_source = AI_ASSIST_PANEL.read_text(encoding="utf-8")
        ai_assist_panel = extract_nonstatic_function(ai_assist_source, "draw_ai_assist_panel")

        self.assertIn('ImGui::CollapsingHeader("More AI Tools"', ai_assist_panel)
        self.assertIn('ImGui::Button("Restore Previous View")', ai_assist_panel)
        self.assertIn("Preview - not applied", ai_assist_panel)
        self.assertIn("draw_help_marker", ai_assist_panel)
        self.assertNotIn("Collapsed review tools", ai_assist_panel)
        self.assertNotIn("AI Assist uses histogram state", ai_assist_panel)
        self.assertNotIn('ImGui::CollapsingHeader("Semantic Review", ImGuiTreeNodeFlags_DefaultOpen)', ai_assist_panel)


    def test_right_panel_visibility_is_configurable_and_saved_with_presets(self):
        ribbon_source = RIBBON_AND_LAYOUT.read_text(encoding="utf-8")
        ribbon = extract_nonstatic_function(ribbon_source, "draw_top_ribbon")

        # save_workspace_preset / load_workspace_preset live in
        # workspace_presets.cpp (Tier 3c module split), not
        # standalone_main.cpp, and are no longer static (they're called
        # cross-file from standalone_main.cpp).
        presets_source = WORKSPACE_PRESETS.read_text(encoding="utf-8")
        save_start = presets_source.index("int save_workspace_preset")
        load_start = presets_source.index("int load_workspace_preset")
        save_fn = presets_source[save_start:load_start]
        load_fn = presets_source[load_start:]

        # AI Assist and Metadata lost their own visibility flags in the
        # Unity redesign (step 3): they are now always-present tabs in
        # ui_bottom_panels.cpp's "Bottom Panels" window, not independently
        # dockable/toggleable windows, so only Insights still has a
        # checkbox and a saved preset flag.
        self.assertIn('ImGui::Checkbox("Insights##panel_insights"', ribbon)
        self.assertNotIn('ImGui::Checkbox("AI Assist##panel_ai"', ribbon)
        self.assertNotIn('ImGui::Checkbox("Metadata##panel_metadata"', ribbon)
        self.assertIn('fprintf(fp, "insights_visible %d\\n"', save_fn)
        self.assertNotIn('fprintf(fp, "ai_visible %d\\n"', save_fn)
        self.assertNotIn('fprintf(fp, "metadata_visible %d\\n"', save_fn)
        self.assertIn('strcmp(key, "insights_visible")', load_fn)
        self.assertNotIn('strcmp(key, "ai_visible")', load_fn)
        self.assertNotIn('strcmp(key, "metadata_visible")', load_fn)
    def test_shortcuts_and_layout_reset_are_discoverable(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        controls_source = CONTROLS_PANEL.read_text(encoding="utf-8")
        ribbon_source = RIBBON_AND_LAYOUT.read_text(encoding="utf-8")
        ribbon = extract_nonstatic_function(ribbon_source, "draw_top_ribbon")

        # handle_keyboard_shortcuts moved to ui_controls_panel.cpp (Tier 3c
        # module split) and is no longer static; standalone_main.cpp still
        # calls it, the shortcut key checks themselves moved with it.
        self.assertIn("void handle_keyboard_shortcuts(StandaloneApp *app)", controls_source)
        self.assertIn("handle_keyboard_shortcuts(&app);", source)
        self.assertIn("ImGuiKey_O", controls_source)
        self.assertIn("ImGuiKey_S", controls_source)
        self.assertIn("ImGuiKey_T", controls_source)
        self.assertIn('ImGui::Button("Reset Layout")', ribbon)
if __name__ == "__main__":
    unittest.main()
