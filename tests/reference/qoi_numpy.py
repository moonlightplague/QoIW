"""Independent M1 point-wise verification of stored float32 and float64 fields."""

from dataclasses import dataclass
import math
import operator
from pathlib import Path
from typing import Callable

import numpy as np


@dataclass(frozen=True)
class Tolerance:
    mode: str
    value: float

    def __post_init__(self):
        value = float(self.value)
        if self.mode not in ("ABS", "REL") or not math.isfinite(value) or value < 0:
            raise ValueError("expected ABS or REL with a finite nonnegative tolerance")
        object.__setattr__(self, "value", value)

    def resolve(self, bounds):
        if self.mode == "ABS" or self.value == 0 or bounds is None:
            return self.value
        lo, hi = bounds
        if hi == lo:
            return self.value
        span = hi - lo
        result = self.value * span if math.isfinite(span) else self.value * hi - self.value * lo
        if not math.isfinite(result):
            raise ValueError("resolved tolerance exceeds float64 range")
        return result


@dataclass(frozen=True)
class Target:
    expression: str
    evaluate: Callable
    tolerance: Tolerance


def c_round(x):
    """Round binary64 values with C's half-away-from-zero rule."""
    x = np.asarray(x, dtype=np.float64)
    integral = np.trunc(x)
    with np.errstate(invalid="ignore"):
        half_or_more = np.abs(x - integral) >= 0.5
    return np.where(half_or_more, integral + np.copysign(1.0, x), integral)


def _evaluate(evaluate, values):
    result = np.asarray(evaluate(values), dtype=np.float64)
    if result.ndim == 0:
        return np.broadcast_to(result, values.shape)
    if result.shape != values.shape:
        raise ValueError("a point-wise formula must return a scalar or one value per input")
    return result


def _extend_range(bounds, values):
    values = values[np.isfinite(values)]
    if not values.size:
        return bounds
    lo, hi = float(values.min()), float(values.max())
    return (lo, hi) if bounds is None else (min(lo, bounds[0]), max(hi, bounds[1]))


def _resolve(original, targets, data_bound, exact, chunk_values):
    relative_targets = [i for i, target in enumerate(targets) if target.tolerance.mode == "REL"]
    relative_data = data_bound is not None and data_bound.mode == "REL"
    data_range, target_ranges = None, [None] * len(targets)
    if relative_data or relative_targets:
        for start in range(0, original.size, chunk_values):
            block = original[start : start + chunk_values]
            values = block[np.isfinite(block)].astype(np.float64)
            if exact is not None and values.size:
                values = values[_evaluate(exact, values) == 0]
            if not values.size:
                continue
            if relative_data:
                data_range = _extend_range(data_range, values)
            for index in relative_targets:
                qoi = _evaluate(targets[index].evaluate, values)
                target_ranges[index] = _extend_range(target_ranges[index], qoi)
    return (
        None if data_bound is None else data_bound.resolve(data_range),
        [target.tolerance.resolve(bounds) for target, bounds in zip(targets, target_ranges)],
    )


@dataclass
class _Errors:
    max_abs_error: float = 0.0
    violations: int = 0
    nonfinite_violations: int = 0

    def compare(self, a, b, tolerance, relative_slack):
        finite = np.isfinite(a) & np.isfinite(b)
        error = np.abs(a[finite] - b[finite])
        bad = ~finite
        if error.size:
            self.max_abs_error = max(self.max_abs_error, float(error.max()))
        if tolerance is not None:
            # Compare the excess so a large finite tolerance plus slack cannot overflow.
            bad[finite] = (error > tolerance) & (error - tolerance > tolerance * relative_slack)
        self.violations += int(np.count_nonzero(bad))
        self.nonfinite_violations += int(bad.size - np.count_nonzero(finite))
        return bad


def _storage_type(dtype):
    dtype = np.dtype(dtype)
    if dtype.kind != "f" or dtype.itemsize not in (4, 8):
        raise ValueError("expected float32 or float64 storage")
    return dtype


def _shape(shape):
    dims = tuple(operator.index(dim) for dim in shape)
    if not 1 <= len(dims) <= 4 or any(dim <= 0 for dim in dims):
        raise ValueError("expected one to four positive extents")
    return dims


def check_arrays(original, decoded, targets=(), data_bound=None, exact=None, *, relative_slack=1e-9,
                 chunk_values=1 << 18):
    """Check two matching arrays using caller-supplied NumPy formulas.

    Each Target pairs a label with an independent formula and a tolerance.
    Callbacks receive flat batches of finite binary64 values. Exact bits are
    compared in the storage type before any conversion.
    """
    original, decoded = np.asarray(original), np.asarray(decoded)
    dtype = _storage_type(original.dtype)
    _shape(original.shape)
    if decoded.dtype != dtype or original.shape != decoded.shape:
        raise ValueError("original and decoded fields must have matching shapes and storage types")
    chunk_values = operator.index(chunk_values)
    if chunk_values <= 0 or not math.isfinite(relative_slack) or not 0 <= relative_slack <= 1:
        raise ValueError("expected a positive chunk size and relative slack between zero and one")

    original, decoded = original.ravel(), decoded.ravel()
    targets = tuple(targets)
    integer = np.dtype(f"u{dtype.itemsize}").newbyteorder(dtype.byteorder)
    data_errors = _Errors()
    target_errors = [_Errors() for _ in targets]
    violating_values = exact_violations = 0
    with np.errstate(divide="ignore", invalid="ignore", over="ignore", under="ignore"):
        bound, tolerances = _resolve(original, targets, data_bound, exact, chunk_values)
        for start in range(0, original.size, chunk_values):
            section = slice(start, start + chunk_values)
            a, b = original[section], decoded[section]
            changed = a.view(integer) != b.view(integer)
            if not changed.any():
                continue
            a = a[changed].astype(np.float64)
            b = b[changed].astype(np.float64)
            bad_data = data_errors.compare(a, b, bound, relative_slack)

            # A change to a non-finite storage value fails the data check.
            # Expression checks apply to the remaining, finite pairs.
            finite_pairs = np.isfinite(a) & np.isfinite(b)
            left, right = a[finite_pairs], b[finite_pairs]
            bad = bad_data[finite_pairs]
            violating_values += int(bad_data.size - left.size)
            if left.size and exact is not None:
                bad_exact = (_evaluate(exact, left) != 0) | (_evaluate(exact, right) != 0)
                exact_violations += int(np.count_nonzero(bad_exact))
                bad |= bad_exact
            if left.size:
                for target, tolerance, errors in zip(targets, tolerances, target_errors):
                    bad |= errors.compare(_evaluate(target.evaluate, left), _evaluate(target.evaluate, right),
                                          tolerance, relative_slack)
            violating_values += int(np.count_nonzero(bad))

    return {
        "ok": violating_values == 0,
        "values": original.size,
        "violating_values": violating_values,
        "exact_violations": exact_violations,
        "data_bound_abs": bound,
        "data": vars(data_errors),
        "targets": [
            {"expression": target.expression, "tolerance_abs": tolerance, "errors": vars(errors)}
            for target, tolerance, errors in zip(targets, tolerances, target_errors)
        ],
    }


def check_files(original, decoded, dtype, shape, targets=(), data_bound=None, exact=None, **options):
    """Check little-endian C-order raw files. Working arrays are bounded by the batch size."""
    dtype, shape = _storage_type(dtype).newbyteorder("<"), _shape(shape)
    expected = math.prod(shape) * dtype.itemsize
    paths = (Path(original), Path(decoded))
    for path in paths:
        if path.stat().st_size != expected:
            raise ValueError(f"expected {expected} bytes in {path}")
    source, result = (np.memmap(path, dtype=dtype, shape=shape, mode="r") for path in paths)
    return check_arrays(source, result, targets, data_bound, exact, **options)
