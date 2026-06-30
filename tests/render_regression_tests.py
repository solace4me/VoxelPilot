import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
VERIFY_SCRIPT = ROOT / "tools" / "verify_render_snapshot.py"


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


if __name__ == "__main__":
    unittest.main()
