import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
VERIFY_SCRIPT = ROOT / "tools" / "verify_render_snapshot.py"
STANDALONE_MAIN = ROOT / "standalone_main.cu"
DEFAULT_STATE_HEADER = ROOT / "common" / "default_state.h"




def extract_static_function(source, name):
    marker = f"static void {name}("
    start = source.index(marker)
    next_static = source.find("\nstatic ", start + len(marker))
    if next_static == -1:
        return source[start:]
    return source[start:next_static]

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
        controls_panel = extract_static_function(source, "draw_controls_panel")
        ai_assist_panel = extract_static_function(source, "draw_ai_assist_panel")

        self.assertEqual(source.count('ImGui::Button("Auto Enhance View")'), 1)
        self.assertNotIn('ImGui::Button("Auto Enhance View")', controls_panel)
        self.assertIn('ImGui::Button("Auto Enhance View")', ai_assist_panel)

    def test_workspace_uses_collapsible_top_ribbon_left_controls_and_right_panels(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        gui = extract_static_function(source, "draw_gui")
        dock_layout = extract_static_function(source, "ensure_default_dock_layout")

        self.assertIn("static void draw_top_ribbon", source)
        self.assertLess(gui.index("draw_top_ribbon(app);"), gui.index("draw_controls_panel(app);"))
        self.assertIn("ImGuiDir_Left", dock_layout)
        self.assertIn("ImGuiDir_Right", dock_layout)
        self.assertIn('DockBuilderDockWindow("Controls", dock_left_id)', dock_layout)
        self.assertIn('DockBuilderDockWindow("AI Assist", dock_right_id)', dock_layout)
        self.assertIn('DockBuilderDockWindow("Insights", dock_right_id)', dock_layout)

    def test_left_controls_panel_retains_import_and_export_actions(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        controls_panel = extract_static_function(source, "draw_controls_panel")

        self.assertIn('ImGui::Text("Volume Import")', controls_panel)
        self.assertIn('ImGui::Button("Browse...")', controls_panel)
        self.assertIn('ImGui::Button("Load Volume")', controls_panel)
        self.assertIn('ImGui::Button("Save Snapshot")', controls_panel)

    def test_workspace_defaults_are_shared_between_startup_and_reset(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        header_text = DEFAULT_STATE_HEADER.read_text(encoding="utf-8")
        reset_fn = extract_static_function(source, "reset_workspace")
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
        controls_panel = extract_static_function(source, "draw_controls_panel")
        ribbon = extract_static_function(source, "draw_top_ribbon")
        dock_layout = extract_static_function(source, "ensure_default_dock_layout")
        gui = extract_static_function(source, "draw_gui")

        self.assertIn('ImGui::Checkbox("Cinematic"', ribbon)
        self.assertNotIn('ImGui::Checkbox("Cinematic Mode"', controls_panel)
        self.assertNotIn('DockBuilderDockWindow("About"', dock_layout)
        self.assertNotIn("draw_about_panel(app);", gui)
        self.assertNotIn("static void draw_about_panel", source)
        self.assertNotIn("about_visible", source)

    def test_ai_assist_uses_progressive_disclosure_and_compact_copy(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        ai_assist_panel = extract_static_function(source, "draw_ai_assist_panel")

        self.assertIn('ImGui::CollapsingHeader("Advanced AI"', ai_assist_panel)
        self.assertIn('ImGui::Button("Undo Transfer")', ai_assist_panel)
        self.assertIn("Preview - not applied", ai_assist_panel)
        self.assertIn("draw_help_marker", source)
        self.assertNotIn("Collapsed review tools", ai_assist_panel)
        self.assertNotIn("AI Assist uses histogram state", ai_assist_panel)
        self.assertNotIn('ImGui::CollapsingHeader("Semantic Review", ImGuiTreeNodeFlags_DefaultOpen)', ai_assist_panel)


    def test_right_panel_visibility_is_configurable_and_saved_with_presets(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        ribbon = extract_static_function(source, "draw_top_ribbon")
        save_start = source.index("static int save_workspace_preset")
        load_start = source.index("static int load_workspace_preset")
        next_fn = source.index("static void scroll_callback", load_start)
        save_fn = source[save_start:load_start]
        load_fn = source[load_start:next_fn]

        self.assertIn('ImGui::Checkbox("Insights##panel_insights"', ribbon)
        self.assertIn('ImGui::Checkbox("AI Assist##panel_ai"', ribbon)
        self.assertIn('ImGui::Checkbox("Metadata##panel_metadata"', ribbon)
        self.assertIn('fprintf(fp, "insights_visible %d\\n"', save_fn)
        self.assertIn('fprintf(fp, "ai_visible %d\\n"', save_fn)
        self.assertIn('fprintf(fp, "metadata_visible %d\\n"', save_fn)
        self.assertIn('strcmp(key, "insights_visible")', load_fn)
        self.assertIn('strcmp(key, "ai_visible")', load_fn)
        self.assertIn('strcmp(key, "metadata_visible")', load_fn)
    def test_shortcuts_and_layout_reset_are_discoverable(self):
        source = STANDALONE_MAIN.read_text(encoding="utf-8")
        ribbon = extract_static_function(source, "draw_top_ribbon")

        self.assertIn("static void handle_keyboard_shortcuts", source)
        self.assertIn("handle_keyboard_shortcuts(&app);", source)
        self.assertIn("ImGuiKey_O", source)
        self.assertIn("ImGuiKey_S", source)
        self.assertIn("ImGuiKey_T", source)
        self.assertIn('ImGui::Button("Reset Layout")', ribbon)
if __name__ == "__main__":
    unittest.main()
