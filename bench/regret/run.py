"""Run the M1 SZ3 regret sweep and compare tuned stream sizes with public QPET."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
from dataclasses import asdict, dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

import numpy as np

from journal import open_journal
from oracle import scan, scores


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "reference"))
from qoi_numpy import Target, Tolerance, check_files


FORMULAS = {
    "x^2": np.square,
    "x^3": lambda x: x**3,
    "log2(x)": np.log2,
    "log2(x+1)": lambda x: np.log2(x + 1),
    "tanh(x)": np.tanh,
    "tanh(x/300)": lambda x: np.tanh(x / 300),
    "sin(10x)": lambda x: np.sin(10 * x),
}


@dataclass(frozen=True)
class Case:
    name: str
    path: str
    shape: tuple
    qoi: str
    qoi_rel: float
    data_rel: float

    def target(self, mode, tolerance):
        return Target(self.qoi, FORMULAS[self.qoi], Tolerance(mode, tolerance))


def load_cases(manifest):
    cases = {}
    with manifest.open(newline="") as file:
        for row in csv.DictReader(file):
            name = row["case"]
            if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", name) or name in cases:
                raise ValueError(f"invalid or duplicate case name {name!r}")
            shape = tuple(map(int, row["shape"].split("x")))
            if not 1 <= len(shape) <= 4 or any(n < 1 for n in shape):
                raise ValueError(f"invalid shape for {name}")
            path = Path(row["path"])
            if path.is_absolute() or ".." in path.parts:
                raise ValueError(f"case {name} needs a path relative to the data root")
            if row["qoi"] not in FORMULAS:
                raise ValueError(f"no NumPy formula for {row['qoi']!r}")
            qoi = Tolerance("REL", row["qoi_rel"])
            data = Tolerance("REL", row["data_rel"])
            cases[name] = Case(name, str(path), shape, row["qoi"], qoi.value, data.value)
    if not cases:
        raise ValueError("the case manifest is empty")
    return cases


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def provenance(path, revision):
    return {"path": str(path), "sha256": sha256(path), "revision": revision}


def invoke(command):
    command = list(map(str, command))
    started = time.perf_counter()
    process = subprocess.run(command, capture_output=True, text=True, check=False)
    details = {
        "argv": command,
        "wall_seconds": time.perf_counter() - started,
        "stdout_tail": process.stdout[-4096:],
        "stderr_tail": process.stderr[-4096:],
    }
    if process.returncode:
        message = (process.stderr or process.stdout).strip()
        raise RuntimeError(f"{command[0]} exited {process.returncode}: {message[-2000:]}")
    return process.stdout, details


def finite_json(value):
    if isinstance(value, float) and not math.isfinite(value):
        if math.isnan(value):
            return None
        return "+inf" if value > 0 else "-inf"
    if isinstance(value, dict):
        return {key: finite_json(item) for key, item in value.items()}
    if isinstance(value, list):
        return list(map(finite_json, value))
    return value


def quality(source, decoded):
    """Range-based PSNR on finite float32 storage values, accumulated in binary64."""
    a = np.memmap(source, dtype="<f4", mode="r")
    b = np.memmap(decoded, dtype="<f4", mode="r")
    low, high = math.inf, -math.inf
    squared_error, count = 0., 0
    for start in range(0, a.size, 1 << 20):
        section = slice(start, start + (1 << 20))
        x, y = a[section], b[section]
        keep = np.isfinite(x)
        if not keep.any():
            continue
        left, right = x[keep].astype(np.float64), y[keep].astype(np.float64)
        if not np.isfinite(right).all():
            return None
        low, high = min(low, float(left.min())), max(high, float(left.max()))
        error = left - right
        squared_error += float(np.square(error).sum(dtype=np.float64))
        count += error.size
    if not count:
        return None
    if squared_error == 0:
        return math.inf
    if high == low:
        return -math.inf
    return 20 * math.log10((high - low) / math.sqrt(squared_error / count))


def qpet_stats(output):
    fields = dict(line.split(" = ", 1) for line in output.splitlines() if " = " in line)
    return {
        "bound": float(fields["backend error bound"]),
        "core_bytes": int(fields["backend bytes"]),
        "correction_bytes": int(fields["outlier bytes"]),
        "outliers": int(fields["outliers"].split()[0]),
        "trials": int(fields["tuning candidates"]),
        "seconds": {label: float(fields[key].split()[0]) for label, key in {
            "prior": "time estimate", "search": "time tune", "core_encode": "time backend compress",
            "core_decode": "time backend decompress", "correction": "time validate", "total": "compression time",
        }.items()},
    }


def qoiw_stats(output, size, qoi_abs, data_abs, fixed):
    report = json.loads(output)["report"]
    stream, plan = report["stream"], report["plan"]
    field = stream["fields"][0]
    if (stream["stream_bytes"] != size or field["data_bound_abs"] != data_abs
            or stream["targets"][0]["tolerance_abs"] != qoi_abs or (fixed is not None and field["bound"] != fixed)):
        raise ValueError("QoIW stream metadata disagrees with the benchmark request")
    return {
        "bound": field["bound"],
        "core_bytes": field["core_bytes"],
        "correction_bytes": field["lossless_bytes"],
        "outliers": report["restored"],
        "trials": len(plan["trace"]),
        "tuner_binding": plan["binding"] if fixed is None else None,
        "seconds": report["seconds"],
    }


def measure(args, case, source, qoi_abs, data_abs, tool, fixed=None):
    threads = str(args.threads)
    shared = ["-i", source, "-f", "-q", case.qoi, "-m", "ABS", repr(qoi_abs),
              "-M", "ABS", repr(data_abs), "-t", threads]
    load = os.getloadavg()
    with tempfile.TemporaryDirectory(prefix=f"qoiw-regret-{case.name}-") as scratch:
        stream, decoded = (Path(scratch) / name for name in ("stream.bin", "decoded.f32"))
        if tool == "qoiw":
            prefix = [args.qoiw, "compress", "-c", "sz3", "--shape", ",".join(map(str, case.shape)), "--json"]
            if fixed is not None:
                prefix.extend(["--fixed-bound", repr(fixed)])
            decoder = [args.qoiw, "decompress"]
        else:
            prefix = [args.qpet, "-b", "sz3", f"-{len(case.shape)}", *map(str, reversed(case.shape))]
            decoder = [args.qpet]
        output, encode = invoke([*prefix, *shared, "-z", stream])
        _, decode = invoke([*decoder, "-z", stream, "-o", decoded, "-t", threads])
        size = stream.stat().st_size
        if not size:
            raise ValueError("encoder produced an empty stream")
        stats = qoiw_stats(output, size, qoi_abs, data_abs, fixed) if tool == "qoiw" else qpet_stats(output)
        verification = check_files(source, decoded, "<f4", case.shape, [case.target("ABS", qoi_abs)],
                                   Tolerance("ABS", data_abs))
        error = verification["targets"][0]["errors"]["max_abs_error"]
        error_ratio = error / qoi_abs if qoi_abs else (0. if error == 0 else math.inf)
        return finite_json({
            **stats, "tool": tool, "bytes": size, "compression_ratio": source.stat().st_size / size,
            "psnr_db": quality(source, decoded), "max_qoi_error_over_tolerance": error_ratio,
            "verification": verification, "load_average": list(load), "commands": [encode, decode],
        })


def compare_case(args, case, builds):
    source = (args.data / case.path).resolve()
    config = {
        "case": {**asdict(case), "shape": list(case.shape)},
        "source": provenance(source, None),
        "builds": builds,
        "threads": args.threads,
        "jobs": args.jobs,
        "grid_points": args.grid_points,
        "refine_rounds": args.refine_rounds,
        "schema": 1,
    }
    with open_journal(args.results / f"{case.name}.jsonl", config) as journal:
        def resolve():
            report = check_files(source, source, "<f4", case.shape, [case.target("REL", case.qoi_rel)],
                                 Tolerance("REL", case.data_rel))
            return {"qoi_abs": report["targets"][0]["tolerance_abs"], "data_abs": report["data_bound_abs"]}

        bounds = journal.measure("tolerances", resolve)
        qoi_abs, data_abs = bounds["qoi_abs"], bounds["data_abs"]
        tuned = journal.measure("qoiw/tuned", lambda: measure(args, case, source, qoi_abs, data_abs, "qoiw"))
        baseline = journal.measure("qpet/tuned", lambda: measure(args, case, source, qoi_abs, data_abs, "qpet"))
        oracle, trials = None, []
        if not args.parity_only:
            def prior_interval():
                output, command = invoke([args.prior, source, case.qoi, repr(qoi_abs), repr(data_abs),
                                          args.threads, *case.shape])
                lower, cap = map(float, output.split())
                return {"lower": lower, "cap": cap, "command": command}

            interval = journal.measure("oracle_interval", prior_interval)

            def measure_bound(bound):
                return journal.measure(f"qoiw/fixed/{bound.hex()}",
                                       lambda: measure(args, case, source, qoi_abs, data_abs, "qoiw", bound))

            oracle, trials = scan(interval["lower"], interval["cap"], tuned, measure_bound,
                                  points=args.grid_points, rounds=args.refine_rounds)

    def summary(row):
        if row is None:
            return None
        return {key: value for key, value in row.items() if key not in ("commands", "load_average")}

    return {
        "case": case.name, "shape": list(case.shape), "expression": case.qoi,
        "source": config["source"], "builds": builds, "threads": args.threads,
        "qoi_abs": qoi_abs, "data_abs": data_abs,
        "qoiw": summary(tuned), "qpet": summary(baseline), "oracle": summary(oracle),
        "oracle_trials": len(trials),
        "parity_ratio": tuned["bytes"] / baseline["bytes"],
        "regret": None if oracle is None else tuned["bytes"] / oracle["bytes"],
        "all_bounds_hold": all(row["verification"]["ok"] for row in [tuned, baseline, *trials]),
    }


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qoiw", type=Path, required=True, help="QoIW CLI executable")
    parser.add_argument("--qpet", type=Path, required=True, help="public QPET CLI executable")
    parser.add_argument("--prior", type=Path, required=True, help="qoiw_regret_prior executable from the QoIW build")
    parser.add_argument("--qoiw-revision", help="source revision used to build the supplied QoIW binaries")
    parser.add_argument("--qpet-revision", required=True, help="source revision used to build the supplied QPET binary")
    parser.add_argument("--data", type=Path, default=os.environ.get("QOIW_DATA_DIR"))
    parser.add_argument("--results", type=Path, required=True, help="directory for append-only per-case journals")
    parser.add_argument("--manifest", type=Path, default=ROOT / "bench" / "cases" / "regret.csv")
    parser.add_argument("--case", action="append", help="select a case by exact name; repeat to select several")
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--jobs", type=int, default=1, help="simultaneous case workers")
    parser.add_argument("--grid-points", type=int, default=21)
    parser.add_argument("--refine-rounds", type=int, default=3)
    parser.add_argument("--parity-only", action="store_true", help="measure the tuned pair without the full-data scan")
    args = parser.parse_args()
    if args.threads < 1 or args.jobs < 1 or args.grid_points < 2 or args.refine_rounds < 0:
        parser.error("expected positive threads and jobs, at least two grid points and nonnegative refinement rounds")
    if args.data is None:
        parser.error("set QOIW_DATA_DIR or pass --data")
    for key in ("qoiw", "qpet", "prior"):
        path = getattr(args, key).resolve()
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"{key} must name an executable file")
        setattr(args, key, path)
    return args


def main():
    args = parse_args()
    try:
        corpus = load_cases(args.manifest)
        selected = set(args.case or corpus)
        if selected - corpus.keys():
            raise ValueError("unknown cases " + ", ".join(sorted(selected - corpus.keys())))
        cases = [case for case in corpus.values() if case.name in selected]
        revision = args.qoiw_revision or subprocess.check_output(
            ["git", "-C", ROOT, "rev-parse", "HEAD"], text=True
        ).strip()
        builds = {
            "qoiw": provenance(args.qoiw, revision), "prior": provenance(args.prior, revision),
            "qpet": provenance(args.qpet, args.qpet_revision),
        }
        args.results.mkdir(parents=True, exist_ok=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"regret benchmark: {error}", file=sys.stderr)
        return 1

    def run(case):
        try:
            row = compare_case(args, case, builds)
            print(f"{case.name} parity={row['parity_ratio']:.4f} regret={row['regret']}", file=sys.stderr, flush=True)
            return row
        except (OSError, ValueError, RuntimeError, KeyError) as error:
            print(f"{case.name}: {error}", file=sys.stderr, flush=True)
            return {"case": case.name, "error": str(error)}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(run, cases))
    completed = [row for row in results if "error" not in row]
    report = scores(completed) if completed else None
    print(json.dumps({"scores": report, "rows": results}, indent=2, allow_nan=False))
    if len(completed) != len(cases):
        return 1
    regret_pass = args.parity_only or bool(report["regret"] and report["regret"]["pass"])
    return 0 if report["all_bounds_hold"] and report["parity"]["pass"] and regret_pass else 2


if __name__ == "__main__":
    sys.exit(main())
