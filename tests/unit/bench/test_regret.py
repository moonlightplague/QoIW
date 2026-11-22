from concurrent.futures import ThreadPoolExecutor
import math
from pathlib import Path
import sys
import tempfile
from threading import Barrier
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "bench" / "regret"))
from journal import open_journal
from oracle import grid, scan, scores


def result(bound, size, ok=True):
    return {"bound": bound, "bytes": size, "verification": {"ok": ok}}


class OracleTests(unittest.TestCase):
    def test_grid_includes_endpoints_and_degenerate_caps(self):
        self.assertEqual(grid(0, 0, 21), [0])
        self.assertEqual(grid(2, 2, 21), [2])
        points = grid(1., 16., 5)
        self.assertEqual(points[0], 1.)
        self.assertEqual(points[-1], 16.)
        for measured, expected in zip(points, [1., 2., 4., 8., 16.]):
            self.assertAlmostEqual(measured, expected)
        with self.assertRaises(ValueError):
            grid(0, 1., 21)

    def test_refines_both_neighbours_without_repeating_trials(self):
        seen = []

        def measure(bound):
            self.assertNotIn(bound, seen)
            seen.append(bound)
            return result(bound, 100 + abs(math.log2(bound) - 2.75))

        best, trials = scan(1., 16., result(16., 102.), measure, points=5, rounds=3)
        self.assertEqual(len(trials), 11)
        self.assertNotIn(16., seen)
        self.assertAlmostEqual(best["bound"], 2**2.75)
        self.assertAlmostEqual(best["bytes"], 100.)

    def test_include_tuned_and_exclude_invalid_streams(self):
        tuned = result(0.125, 10)
        best, trials = scan(1., 4., tuned,
                            lambda bound: result(bound, 2 if bound == 4 else 30, bound != 4), 3, 0)
        self.assertIs(best, tuned)
        self.assertTrue(any(not row["verification"]["ok"] for row in trials))

    def test_gate_thresholds(self):
        def case(tuned, reference, best, ok=True):
            return {"qoiw": {"bytes": tuned}, "qpet": {"bytes": reference},
                    "oracle": {"bytes": best}, "all_bounds_hold": ok}

        report = scores([case(100, 100, 100), case(104, 100, 100)])
        self.assertTrue(report["parity"]["pass"])
        self.assertTrue(report["regret"]["pass"])
        self.assertEqual(report["regret"]["above_1_05"], 0)
        report = scores([case(110, 100, 100), case(90, 100, 90)])
        self.assertTrue(report["parity"]["pass"])
        self.assertEqual(report["regret"]["above_1_05"], 1)
        report = scores([case(111, 100, 90, ok=False), case(100, 100, 100)])
        self.assertFalse(report["all_bounds_hold"])
        self.assertFalse(report["parity"]["pass"])
        self.assertFalse(report["regret"]["pass"])


class JournalTests(unittest.TestCase):
    def test_resume_and_recover_interrupted_append(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "case.jsonl"
            config = {"source": "sha256", "threads": 16}
            with open_journal(path, config) as journal:
                journal.measure("tuned", lambda: {"bytes": 1024})
                with self.assertRaises(RuntimeError):
                    journal.measure("failed", lambda: self.fail_trial())
            with path.open("ab") as file:
                file.write(b'{"key": "incomplete')
            with open_journal(path, config) as journal:
                self.assertEqual(journal.measure("tuned", lambda: self.fail_trial())["bytes"], 1024)
                self.assertNotIn("failed", journal.rows)
                journal.measure("oracle", lambda: {"bytes": 1000})
            with open_journal(path, config) as journal:
                self.assertEqual(set(journal.rows), {"config", "tuned", "oracle"})
            with self.assertRaisesRegex(ValueError, "settings changed"):
                with open_journal(path, {"source": "sha256", "threads": 1}):
                    self.fail("changed benchmark configuration was accepted")

    @staticmethod
    def fail_trial():
        raise RuntimeError("failed invocation")

    def test_workers_share_completed_measurement(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "case.jsonl"
            starting = Barrier(2, timeout=10)
            invocations = []

            def measure():
                invocations.append("run")
                return {"bytes": 100}

            def worker():
                starting.wait()
                with open_journal(path, {}) as journal:
                    return journal.measure("tuned", measure)

            with ThreadPoolExecutor(max_workers=2) as pool:
                jobs = [pool.submit(worker) for _ in range(2)]
                self.assertEqual([job.result()["bytes"] for job in jobs], [100, 100])
            self.assertEqual(invocations, ["run"])


if __name__ == "__main__":
    unittest.main()
