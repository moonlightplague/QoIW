import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "bench" / "regret"))
try:
    from performance import budgets, measure_pair, timing_ratios
except ModuleNotFoundError as error:
    if error.name != "numpy":
        raise
    print("NumPy is required for the performance benchmark")
    sys.exit(77)

from run import Case


class PerformanceTests(unittest.TestCase):
    def test_paired_ratios_use_the_matching_plain_call(self):
        wrapped = {"encode_seconds": 10, "planning_seconds": 3, "decode_seconds": 4}
        plain = {"encode_seconds": 2, "decode_seconds": 5}
        self.assertEqual(timing_ratios(wrapped, plain), {
            "compression_ratio": 5, "frontend_share": .3, "decompression_ratio": .8,
        })

    def test_budgets_weight_cases_equally_and_keep_thread_settings_separate(self):
        def group(compression, frontend, decompression):
            return {"medians": {"compression_ratio": compression, "frontend_share": frontend,
                                "decompression_ratio": decompression}}

        results = budgets([
            {"case": "a", "threads": {1: group(7, .1, 1.1), 16: group(3, .36, 1.2)}},
            {"case": "b", "threads": {1: group(3.5, .35, .9), 16: group(1, .35, 1.1)}},
            {"case": "c", "threads": {1: group(2, .9, 1.1), 16: group(1, .4, .8)}},
        ])
        self.assertEqual(results[1]["compression_ratio"]["corpus_median"], 3.5)
        self.assertEqual(results[1]["compression_ratio"]["cases_over_limit"], ["a"])
        self.assertTrue(results[1]["compression_ratio"]["pass"])
        self.assertTrue(results[1]["frontend_share"]["pass"])
        self.assertTrue(results[1]["decompression_ratio"]["pass"])
        self.assertTrue(results[16]["compression_ratio"]["pass"])
        self.assertFalse(results[16]["frontend_share"]["pass"])
        self.assertEqual(results[16]["decompression_ratio"]["corpus_median"], 1.1)
        self.assertEqual(results[16]["decompression_ratio"]["worst_case"], "a")
        self.assertFalse(results[16]["decompression_ratio"]["pass"])

    def test_baseline_can_run_first_at_the_saved_bound(self):
        source = ROOT / "bench" / "cases" / "regret.csv"
        case = Case("sample", "sample.f32", (4, 4), "x^2", .001, .01)
        bounds = {"qoi_abs": .001, "data_abs": .01}
        plain = {"encode_seconds": 2, "decode_seconds": 1, "bound": .004,
                 "core_bytes": 80, "compress_peak_rss_kib": 1024}
        wrapped = {**plain, "encode_seconds": 5, "planning_seconds": 1, "stream_bytes": 140}
        previous = {"qoiw": wrapped}

        with patch("performance.invoke", side_effect=[(json.dumps(plain), {}), (json.dumps(wrapped), {})]) as invoke:
            row = measure_pair("probe", source, case, bounds, 16, previous)
        self.assertEqual(invoke.call_args_list[0].args[0], ["probe", "plain", source, 16, "0.004", 4, 4])
        self.assertEqual(row["order"], ["plain", "qoiw"])

        plain["core_bytes"] += 1
        with patch("performance.invoke", side_effect=[(json.dumps(wrapped), {}), (json.dumps(plain), {})]):
            with self.assertRaisesRegex(ValueError, "different core payload lengths"):
                measure_pair("probe", source, case, bounds, 16, None)


if __name__ == "__main__":
    unittest.main()
