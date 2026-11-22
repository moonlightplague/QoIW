"""Measure paired QoIW and plain SZ3 times on the M1 reference cases."""

import argparse
from dataclasses import asdict
import json
import math
import os
from pathlib import Path
from statistics import median
import subprocess
import sys

from journal import open_journal
from run import ROOT, invoke, load_cases, provenance, resolve_tolerances


THREADS = (1, 16)
REPETITIONS = 3
LIMITS = {"compression_ratio": 3.5, "frontend_share": .35, "decompression_ratio": 1.1}


def timing_ratios(wrapped, plain):
    return {
        "compression_ratio": wrapped["encode_seconds"] / plain["encode_seconds"],
        "frontend_share": wrapped["planning_seconds"] / wrapped["encode_seconds"],
        "decompression_ratio": wrapped["decode_seconds"] / plain["decode_seconds"],
    }


def medians(rows, keys):
    return {key: median(row[key] for row in rows) for key in keys}


def budgets(cases):
    results = {}
    for threads in THREADS:
        groups = {}
        for key, limit in LIMITS.items():
            values = {case["case"]: case["threads"][threads]["medians"][key] for case in cases}
            worst = max(values, key=values.__getitem__)
            gate = {
                "corpus_median": median(values.values()), "max_case_median": values[worst], "worst_case": worst,
                "cases_over_limit": [name for name, value in values.items() if value > limit], "limit": limit,
            }
            statistic = "max_case_median" if key == "decompression_ratio" else "corpus_median"
            groups[key] = {**gate, "statistic": statistic, "pass": gate[statistic] <= limit}
        results[threads] = groups
    return results


def measure_pair(probe, source, case, bounds, threads, previous):
    def measure(mode, *request):
        output, command = invoke([probe, mode, source, threads, *request, *case.shape])
        return {**json.loads(output), "command": command}

    def tuned():
        return measure("wrapped", case.qoi, repr(bounds["qoi_abs"]), repr(bounds["data_abs"]))

    load_before = os.getloadavg()
    # The first pair supplies the bound. Reverse the order for the second.
    if previous is not None:
        plain = measure("plain", repr(previous["qoiw"]["bound"]))
        wrapped = tuned()
        order = ["plain", "qoiw"]
    else:
        wrapped = tuned()
        plain = measure("plain", repr(wrapped["bound"]))
        order = ["qoiw", "plain"]
    if wrapped["bound"] != plain["bound"] or wrapped["core_bytes"] != plain["core_bytes"]:
        raise ValueError("plain SZ3 and QoIW used different bounds or produced different core payload lengths")
    for row in (wrapped, plain):
        if any(not math.isfinite(row[key]) or row[key] <= 0 for key in ("encode_seconds", "decode_seconds")):
            raise ValueError("codec timings must be finite and positive")
    ratios = timing_ratios(wrapped, plain)
    input_bytes = source.stat().st_size
    return {
        "qoiw": wrapped, "plain": plain, "order": order, "input_bytes": input_bytes,
        "ratios": ratios,
        "qoiw_peak_rss_over_input": 1024 * wrapped["compress_peak_rss_kib"] / input_bytes,
        "plain_peak_rss_over_input": 1024 * plain["compress_peak_rss_kib"] / input_bytes,
        "load_before": list(load_before), "load_after": list(os.getloadavg()),
    }


def measure_case(args, journal, case, source):
    bounds = journal.measure(f"{case.name}/tolerances", lambda: resolve_tolerances(source, case))
    configurations = {}
    for threads in THREADS:
        trials = []
        for repetition in range(REPETITIONS):
            key = f"{case.name}/{threads}/{repetition + 1}"
            previous = trials[0] if repetition % 2 else None
            trials.append(journal.measure(key, lambda: measure_pair(args.probe, source, case, bounds, threads, previous)))
        rss_keys = ("qoiw_peak_rss_over_input", "plain_peak_rss_over_input")
        values = {**medians([trial["ratios"] for trial in trials], LIMITS), **medians(trials, rss_keys)}
        configurations[threads] = {"medians": values, "trials": trials}
        print(f"{case.name} threads={threads} encode={values['compression_ratio']:.3f} "
              f"frontend={values['frontend_share']:.3f} decode={values['decompression_ratio']:.3f} "
              f"peak/input={values['qoiw_peak_rss_over_input']:.3f}", file=sys.stderr, flush=True)

    streams = {(trial["qoiw"]["bound"], trial["qoiw"]["stream_bytes"])
               for configuration in configurations.values() for trial in configuration["trials"]}
    if len(streams) != 1:
        raise ValueError(f"selected bound or stream length changed across samples for {case.name}")
    return {"case": case.name, "tolerances": bounds, "threads": configurations}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True, help="qoiw_performance_probe executable")
    parser.add_argument("--revision", help="source revision used to build the probe, defaults to Git HEAD")
    parser.add_argument("--data", type=Path, default=os.environ.get("QOIW_DATA_DIR"))
    parser.add_argument("--results", type=Path, required=True, help="directory for the append-only timing journal")
    parser.add_argument("--manifest", type=Path, default=ROOT / "bench" / "cases" / "regret.csv")
    parser.add_argument("--case", action="append", help="select a case by exact name; repeat to select several")
    args = parser.parse_args()
    if args.data is None:
        parser.error("set QOIW_DATA_DIR or pass --data")
    args.probe = args.probe.resolve()
    if not args.probe.is_file() or not os.access(args.probe, os.X_OK):
        parser.error("--probe must name an executable file")
    return args


def main():
    args = parse_args()
    try:
        corpus = load_cases(args.manifest)
        selected = set(args.case or corpus)
        if selected - corpus.keys():
            raise ValueError("unknown cases " + ", ".join(sorted(selected - corpus.keys())))
        cases = [case for case in corpus.values() if case.name in selected]
        revision = args.revision or subprocess.check_output(["git", "-C", ROOT, "rev-parse", "HEAD"], text=True).strip()
        sources = {case.name: (args.data / case.path).resolve() for case in cases}
        inputs = {path: provenance(path, None) for path in dict.fromkeys(sources.values())}
        os.environ["OMP_DYNAMIC"] = "FALSE"
        config = {
            "schema": 1, "probe": provenance(args.probe, revision), "threads": list(THREADS),
            "repetitions": REPETITIONS, "workers": 1, "host": list(os.uname()),
            "cpu_affinity": sorted(os.sched_getaffinity(0)),
            "openmp": {key: value for key, value in os.environ.items() if key.startswith(("OMP_", "GOMP_"))},
            "cases": [{**asdict(case), "shape": list(case.shape), "source": inputs[sources[case.name]]} for case in cases],
        }
        args.results.mkdir(parents=True, exist_ok=True)
        with open_journal(args.results / "timings.jsonl", config) as journal:
            rows = [measure_case(args, journal, case, sources[case.name]) for case in cases]
        scores = budgets(rows)
        print(json.dumps({"config": config, "budgets": scores, "cases": rows}, indent=2, allow_nan=False))
        return 0 if all(gate["pass"] for groups in scores.values() for gate in groups.values()) else 2
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"performance benchmark: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
