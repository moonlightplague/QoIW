"""Reproduce the QPET/SZ3 Hurricane reference case in PLAN.md section 13.4."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np


FIELD = Path("Hurricane/Uf48.bin.f32")
SHAPE = (100, 500, 500)
DTYPE = np.dtype("<f4")
INPUT_SHA256 = "d60dc2722819be5fb40afd115185de6aeca8b49281129a03dd50f26755285b37"
REL_TOLERANCE = 1e-3


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def check_decoded(original, decoded):
    expected_bytes = math.prod(SHAPE) * DTYPE.itemsize
    if decoded.stat().st_size != expected_bytes:
        raise ValueError(f"expected {expected_bytes} decoded bytes")
    source = np.memmap(original, dtype=DTYPE, mode="r", shape=SHAPE)
    result = np.memmap(decoded, dtype=DTYPE, mode="r", shape=SHAPE)
    qoi_min, qoi_max = math.inf, -math.inf
    max_error = squared_error_sum = 0.0
    for plane, recovered in zip(source, result):
        x = plane.astype(np.float64)
        y = recovered.astype(np.float64)
        if not np.isfinite(y).all():
            raise ValueError("non-finite value in decoded Hurricane field")
        qoi = x * x
        qoi_min = min(qoi_min, qoi.min())
        qoi_max = max(qoi_max, qoi.max())
        max_error = max(max_error, np.abs(qoi - y * y).max())
        squared_error_sum += np.square(x - y).sum()
    tolerance = REL_TOLERANCE * (qoi_max - qoi_min)
    if max_error > tolerance:
        raise ValueError(f"maximum squared-field error {max_error} exceeds tolerance {tolerance}")
    rmse = math.sqrt(squared_error_sum / source.size)
    span = float(source.max()) - float(source.min())
    return {
        "qoi_tolerance_abs": tolerance,
        "max_qoi_error": max_error,
        "max_qoi_error_over_tolerance": max_error / tolerance,
        "rmse": rmse,
        "psnr_db": 20 * math.log10(span / rmse) if rmse else None,
    }


def run(qpet, data_root):
    source = data_root / FIELD
    if source.stat().st_size != math.prod(SHAPE) * DTYPE.itemsize or sha256(source) != INPUT_SHA256:
        raise ValueError(f"{source} differs from the Hurricane reference input")
    with tempfile.TemporaryDirectory(prefix="qoiw-anchor-") as scratch:
        stream = Path(scratch) / "U.qpet"
        decoded = Path(scratch) / "U.dec.f32"
        # QPET's CLI takes the fastest-varying dimension first.
        subprocess.run(
            [qpet, "-b", "sz3", "-f", "-3", *map(str, reversed(SHAPE)), "-i", source,
             "-q", "x^2", "-m", "REL", str(REL_TOLERANCE), "-a", "-t", "16", "-z", stream],
            check=True, stdout=sys.stderr,
        )
        subprocess.run([qpet, "-z", stream, "-o", decoded, "-t", "16"], check=True, stdout=sys.stderr)
        report = check_decoded(source, decoded)
        size = stream.stat().st_size
        report.update(input_sha256=INPUT_SHA256, qpet_binary_sha256=sha256(qpet), stream_bytes=size,
                      stream_sha256=sha256(stream), compression_ratio=source.stat().st_size / size)
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qpet", type=Path, help="QPET reference executable")
    args = parser.parse_args()
    if not os.environ.get("QOIW_DATA_DIR"):
        parser.error("set QOIW_DATA_DIR to the directory containing Hurricane/Uf48.bin.f32")
    try:
        report = run(args.qpet.resolve(), Path(os.environ["QOIW_DATA_DIR"]))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"reference anchor failed: {error}\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
