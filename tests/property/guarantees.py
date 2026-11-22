"""Run the M1 point-wise guarantee matrix against the test round-trip probe."""

import argparse
from collections import Counter
from dataclasses import dataclass
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

try:
    import numpy as np
except ModuleNotFoundError as error:
    if error.name != "numpy":
        raise
    print("NumPy is required for the guarantee matrix")
    sys.exit(77)

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reference"))
from qoi_numpy import Target, Tolerance, c_round, check_files


FORMULAS = {
    "smooth": (("x", lambda x: x), ("x^2", np.square), ("x^3", lambda x: x**3), ("2x+1", lambda x: 2 * x + 1)),
    "oscillating": (("sin(10x)", lambda x: np.sin(10 * x)), ("cos(3*x)", lambda x: np.cos(3 * x)),
                    ("tan(x/16)", lambda x: np.tan(x / 16)), ("tanh(x)", np.tanh)),
    "singular": (("ln(x)", np.log), ("log2(x)", np.log2), ("1/x", lambda x: 1 / x), ("sqrt(x)", np.sqrt)),
    "other_math": (("log10(x)", np.log10), ("log(x,3)", lambda x: np.log(x) / np.log(3.)),
                   ("cbrt(x)", np.cbrt), ("exp(x/32)", lambda x: np.exp(x / 32))),
    "piecewise": (("x < 0 ? 0 : x^2", lambda x: np.where(x < 0, 0, x * x)),
                  ("if(x > 0, ln(x), 0)", lambda x: np.where(x > 0, np.log(x), 0)),
                  ("abs(x)", np.abs), ("min(max(x,-1),1)", lambda x: np.clip(x, -1, 1))),
    "steps": (("floor(x/0.05)", lambda x: np.floor(x / .05)), ("ceil(x)", np.ceil),
              ("round(x/0.05)", lambda x: c_round(x / .05)), ("sign(x)", np.sign)),
    "comparisons": (("x > 88.16", lambda x: x > 88.16), ("x < 0.05", lambda x: x < .05),
                    ("not (x < 0 or x > 1)", lambda x: ~((x < 0) | (x > 1))),
                    ("x != 0 and x <= 1", lambda x: (x != 0) & (x <= 1))),
    "curved": (("sigmoid(x)", lambda x: 1 / (1 + np.exp(-x))), ("1/(x-0.5)", lambda x: 1 / (x - .5)),
               ("pow(x,0.75)", lambda x: np.power(x, .75)), ("x*sqrt(abs(x))", lambda x: x * np.sqrt(np.abs(x)))),
}

SHAPES = {
    "smooth": ((65,), (8193,)),
    "noisy": ((17, 19), (67, 71)),
    "sparse": ((9, 1, 11), (17, 1, 347)),
    "heavy": ((3, 5, 7, 2), (5, 7, 3, 29)),
    "constant": ((1,), (4, 21, 9)),
    "nonfinite": ((33,), (4, 17, 73)),
    "fill": ((3, 17), (11, 1, 53, 17)),
    "denormal": ((2, 3, 1, 7), (7, 11, 1, 61)),
    "threshold": ((2, 4, 13), (5, 13, 137)),
}

EXACT = {
    "fill": ("x > 1e30", lambda x: x > 1e30),
    "sparse": ("x == 0", lambda x: x == 0),
}

MOCKS = ("identity", "quantizer", "noise", "garbage", "signflip", "nan")


@dataclass(frozen=True)
class Policy:
    tolerance: Tolerance
    data_bound: Tolerance | None
    fixed_bound: float | None = None


POLICIES = {
    "relative": Policy(Tolerance("REL", .001), None),
    "tight": Policy(Tolerance("ABS", 1e-8), Tolerance("ABS", 2e-5), .125),
    "loose": Policy(Tolerance("ABS", .125), Tolerance("REL", .02)),
    "steps": Policy(Tolerance("ABS", 0), None, .075),
    "lossless": Policy(Tolerance("REL", .001), Tolerance("ABS", 0), 0.0),
}

SMALL = (
    ("smooth", "smooth", "relative"),
    ("noisy", "oscillating", "loose"),
    ("sparse", "singular", "tight"),
    ("heavy", "other_math", "relative"),
    ("constant", "smooth", "lossless"),
    ("nonfinite", "piecewise", "tight"),
    ("fill", "curved", "loose"),
    ("denormal", "singular", "relative"),
    ("threshold", "steps", "steps"),
    ("threshold", "comparisons", "steps"),
    ("smooth", None, "relative"),
)


def make_field(name, dtype, shape):
    n = math.prod(shape)
    i = np.arange(n, dtype=np.float64)
    values = np.sin(i / 7) + i / n - .5
    if name == "noisy":
        values = np.random.default_rng(104729).normal(0., 1.5, size=n)
    elif name == "sparse":
        values.fill(0.)
        values[::17] = np.sin(i[::17])
        values[5::17] = -0.
    elif name == "heavy":
        rng = np.random.default_rng(104729)
        values = np.exp(np.clip(rng.normal(0., 3., size=n), -5., 5.)) * np.where(i % 3 == 0, -1, 1)
    elif name == "constant":
        values.fill(.75)
    elif name == "fill":
        values[n // 4 : n // 2] = 1e36
    elif name == "threshold":
        point = dtype.type(88.16)
        lower = np.nextafter(point, dtype.type(-np.inf))
        upper = np.nextafter(point, dtype.type(np.inf))
        values = np.resize([lower, point, upper, -0., -.5, .5, 1.2, math.nextafter(1.2, 2.), .05, 1.], n)

    values = values.astype(dtype)
    if name == "nonfinite":
        values[:6] = [-0., 0., np.inf, -np.inf, np.nan, np.nan]
        bits = values.view(f"<u{dtype.itemsize}")
        bits[4] |= bits.dtype.type(0x15)
        bits[5] |= bits.dtype.type(0x28)
    elif name == "denormal":
        small = np.nextafter(dtype.type(0), dtype.type(1))
        values[:9] = [-3 * small, -2 * small, -small, -0., 0., small, 2 * small, 3 * small, .5]
    return values.reshape(shape)


def scenarios(full):
    yield from ((False, *case) for case in SMALL)
    if full:
        numerical = ("relative", "tight", "loose")
        for i, field in enumerate(SHAPES):
            for j, group in enumerate(FORMULAS):
                first = (i + j) % len(numerical)
                second = "steps" if group in ("steps", "comparisons") else numerical[(first + 1) % len(numerical)]
                yield True, field, group, numerical[first]
                yield True, field, group, second


def same_number(stored, checked):
    if stored is None or checked is None:
        return stored is checked
    # NumPy and libm can differ in the last bits of an evaluated range.
    return math.isclose(stored, checked, rel_tol=1e-12, abs_tol=0.)


def check_case(probe, scratch, core, dtype, shape, field, group, policy, workers):
    original, stream, decoded = (scratch / name for name in ("original.raw", "field.qoiw", "decoded.raw"))
    make_field(field, dtype, shape).tofile(original)
    targets = [Target(expr, evaluate, policy.tolerance) for expr, evaluate in FORMULAS.get(group, ())]
    exact_text, exact = EXACT.get(field, ("", None))
    data = policy.data_bound
    fixed = "auto" if policy.fixed_bound is None else repr(policy.fixed_bound)
    dtype_flag = "f32" if dtype.itemsize == 4 else "f64"
    request = [
        f"{dtype_flag} {core} {workers} {fixed} {len(shape)} " + " ".join(map(str, shape)),
        f"{data.mode} {data.value}" if data else "NONE 0",
        f"{json.dumps(exact_text)} {len(targets)}",
        *(f"{json.dumps(t.expression)} {t.tolerance.mode} {t.tolerance.value}" for t in targets),
    ]
    process = subprocess.run([probe, original, stream, decoded], input="\n".join(request), text=True, capture_output=True)
    if process.returncode:
        raise AssertionError(process.stderr or process.stdout or f"probe returned {process.returncode}")
    restored, metadata = process.stdout.splitlines()
    info = json.loads(metadata)
    report = check_files(original, decoded, dtype, shape, targets, data, exact)
    if not report["ok"]:
        raise AssertionError(report)
    if (info["core_name"] != core or tuple(info["shape"]) != shape or info["num"] != report["values"]
            or info["dtype"] != dtype.name or info["exact_condition"] != (exact_text or None)
            or not same_number(info["fields"][0]["data_bound_abs"], report["data_bound_abs"])
            or len(info["targets"]) != len(report["targets"])):
        raise AssertionError(f"stored request or layout differs from the independent check: {info}")
    for stored, checked in zip(info["targets"], report["targets"]):
        if stored["expression"] != checked["expression"] or not same_number(stored["tolerance_abs"], checked["tolerance_abs"]):
            raise AssertionError(f"stored target disagrees with independent tolerance: {stored}, {checked}")
    return report["values"], int(restored)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--profile", choices=("small", "full"), default="small")
    parser.add_argument("--sz3", choices=("0", "1"), required=True)
    args = parser.parse_args()
    cores = ["mock:" + name for name in MOCKS]
    if args.sz3 == "1":
        cores.append("sz3")
    cells = values = 0
    restored = Counter()
    with tempfile.TemporaryDirectory(prefix="qoiw-matrix-") as work:
        for expanded, field, group, policy in scenarios(args.profile == "full"):
            shape = SHAPES[field][expanded]
            for dtype in (np.dtype("<f4"), np.dtype("<f8")):
                for core in cores:
                    workers = (2, 7, 16)[cells % 3]
                    try:
                        checked, corrections = check_case(args.probe.resolve(), Path(work), core, dtype, shape, field,
                                                          group, POLICIES[policy], workers)
                    except (AssertionError, ValueError, OSError) as error:
                        raise AssertionError(f"{core} {dtype.name} {field}{shape} {group} {policy}: {error}") from error
                    values += checked
                    restored[core] += corrections
                    cells += 1
    for mock in ("mock:noise", "mock:garbage", "mock:signflip", "mock:nan"):
        if not restored[mock]:
            raise AssertionError(f"{mock} did not exercise the correction layer")
    print(json.dumps({"profile": args.profile, "cells": cells, "values_checked": values, "restored": dict(restored)}))


if __name__ == "__main__":
    main()
