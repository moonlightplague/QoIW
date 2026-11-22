import sys
from pathlib import Path
import tempfile
import unittest

try:
    import numpy as np
except ModuleNotFoundError as error:
    if error.name != "numpy":
        raise
    print("NumPy is required for the independent checker tests")
    sys.exit(77)

from qoi_numpy import Target, Tolerance, c_round, check_arrays, check_files


class CheckTests(unittest.TestCase):
    def test_overlapping_violations_and_relative_resolution(self):
        source = np.array([1, 2, 4, 8, 8], dtype="<f4")
        result = np.array([1.25, 2.5, 2, 8.5, 8], dtype="<f4")
        targets = [
            Target("x^2", np.square, Tolerance("REL", 0.125)),
            Target("x < 4", lambda x: x < 4, Tolerance("ABS", 0)),
        ]
        report = check_arrays(source, result, targets, Tolerance("REL", 1 / 14), chunk_values=2)
        self.assertEqual(report["data_bound_abs"], 0.5)
        self.assertEqual(report["violating_values"], 2)
        self.assertEqual(report["data"], dict(max_abs_error=2, violations=1, nonfinite_violations=0))
        self.assertEqual(report["targets"][0]["tolerance_abs"], 7.875)
        self.assertEqual(report["targets"][0]["errors"]["max_abs_error"], 12)
        self.assertEqual([t["errors"]["violations"] for t in report["targets"]], [2, 1])
        self.assertFalse(report["ok"])

    def test_exact_and_nonfinite_storage(self):
        for size, payload in [(4, 0x7FC00000), (8, 0x7FF8000000000000)]:
            dtype = np.dtype(f"<f{size}")
            with self.subTest(dtype=dtype):
                source = np.array([0.0, -0.0, -1, 2, 2, 9, 3, np.nan, np.nan, np.inf, -np.inf, 3, -1], dtype=dtype)
                source.view(f"<u{size}")[7:9] = payload + 1
                result = source.copy()
                result[[1, 2, 3, 4, 5, 10, 11]] = [0.0, -2, -1, 9, 3, np.inf, np.nan]
                result.view(f"<u{size}")[7] = payload + 2
                target = Target("ln(x)", np.log, Tolerance("ABS", 10))
                exact = lambda x: x > 8
                report = check_arrays(source, result, [target], exact=exact, chunk_values=3)
                self.assertEqual(report["violating_values"], 8)
                self.assertEqual(report["exact_violations"], 2)
                self.assertEqual(report["data"], dict(max_abs_error=7, violations=3, nonfinite_violations=3))
                self.assertEqual(report["targets"][0]["errors"]["nonfinite_violations"], 3)
                self.assertTrue(check_arrays(source, source.copy(), [target], exact=exact)["ok"])

    def test_masked_and_nonfinite_ranges(self):
        source = np.array([-5, 1, 4, 1e36, np.inf], dtype="<f4")
        exact = lambda x: x > 1e30
        targets = [
            Target("sqrt(x)", np.sqrt, Tolerance("REL", .125)),
            Target("2", lambda x: 2, Tolerance("REL", .25)),
            Target("nan", lambda x: np.nan, Tolerance("REL", .5)),
        ]
        report = check_arrays(source, source, targets, Tolerance("REL", .125), exact, chunk_values=1)
        self.assertTrue(report["ok"])
        self.assertEqual(report["data_bound_abs"], 9 * .125)
        self.assertEqual([target["tolerance_abs"] for target in report["targets"]], [.125, .25, .5])

    def test_match_in_storage_type_then_evaluate_in_double(self):
        source, result = np.array([1], dtype="<f4"), np.array([4 / 3], dtype="<f4")
        targets = [Target("x", lambda x: x, Tolerance("ABS", 1 / 3))]
        report = check_arrays(source, result, targets)
        self.assertFalse(report["ok"])
        self.assertEqual(report["targets"][0]["errors"]["max_abs_error"], float(result[0]) - 1.0)

    def test_comparison_slack_and_extremes(self):
        source = np.array([0.0])
        result = np.array([np.nextafter(1.0, 2.0)])
        self.assertTrue(check_arrays(source, result, data_bound=Tolerance("ABS", 1))["ok"])
        self.assertFalse(check_arrays(source, result, data_bound=Tolerance("ABS", 1), relative_slack=0)["ok"])
        self.assertFalse(check_arrays(source, np.array([1e-300]), data_bound=Tolerance("ABS", 0))["ok"])
        huge = np.finfo(np.float64).max
        extremes = np.array([-huge, huge])
        report = check_arrays(extremes, extremes[::-1], data_bound=Tolerance("REL", .25))
        self.assertEqual(report["data_bound_abs"], huge / 2)
        self.assertEqual(report["data"]["max_abs_error"], float("inf"))
        self.assertEqual(report["data"]["violations"], 2)
        with self.assertRaises(ValueError):
            check_arrays(extremes, extremes, data_bound=Tolerance("REL", 1))

    def test_rounding_and_division(self):
        half = np.array([-1.5, -0.5, np.nextafter(-0.5, 0), np.nextafter(0.5, 0), 0.5, 1.5, 2.0**52 + 1])
        np.testing.assert_array_equal(c_round(half), [-2., -1., -0., 0., 1., 2., 2.0**52 + 1])
        self.assertTrue(np.signbit(c_round(half)[2]))
        source = np.array([1.2])
        after = np.array([np.nextafter(1.2, 2.0)])
        targets = [Target("floor(x/0.05)", lambda x: np.floor(x / .05), Tolerance("ABS", 0))]
        report = check_arrays(source, after, targets)
        self.assertEqual(report["targets"][0]["errors"]["max_abs_error"], 1)

    def test_read_little_endian_fields_and_reject_wrong_sizes(self):
        with tempfile.TemporaryDirectory() as scratch:
            original, result = Path(scratch) / "source.f64", Path(scratch) / "output.f64"
            np.array([[1., 2.], [3., 4.]], dtype="<f8").tofile(original)
            np.array([[1., 2.25], [3., 4.]], dtype="<f8").tofile(result)
            report = check_files(original, result, np.float64, (2, 2), data_bound=Tolerance("ABS", .25))
            self.assertEqual(report["values"], 4)
            self.assertTrue(report["ok"])
            self.assertEqual(report["data"]["max_abs_error"], .25)
            for shape in ((0, 4), (5,), (), (1, 1, 1, 1, 4)):
                with self.assertRaises(ValueError):
                    check_files(original, result, "<f8", shape)
            with self.assertRaises(ValueError):
                check_files(original, result, "<i8", (2, 2))
            with result.open("ab") as file:
                file.write(b"\0")
            with self.assertRaises(ValueError):
                check_files(original, result, "<f8", (2, 2))


if __name__ == "__main__":
    unittest.main()
