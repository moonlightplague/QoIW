"""Materialize the three float32 Miranda fields used by the reference regret cases."""

import os
from pathlib import Path
import sys
import tempfile

import numpy as np


def prepare(root, field):
    count = 256 * 384 * 384
    source = root / "Miranda" / f"{field}.d64"
    target = root / "Miranda_f32" / f"{field}.f32"
    if source.stat().st_size != count * 8:
        raise ValueError(f"unexpected source length: {source}")
    values = np.memmap(source, mode="r", dtype="<f8", shape=(count,))
    target.parent.mkdir(exist_ok=True)
    # Publish a complete file in one operation. Never replace a pre-existing field.
    if not target.exists():
        with tempfile.NamedTemporaryFile(dir=target.parent) as stage:
            for start in range(0, count, 1 << 20):
                stage.write(values[start:start + (1 << 20)].astype("<f4").tobytes())
            stage.flush()
            try:
                os.link(stage.name, target)
            except FileExistsError:
                pass  # Another writer won the race. Verify its result below.
    if target.stat().st_size != count * 4:
        raise ValueError(f"unexpected output length: {target}")
    actual = np.memmap(target, mode="r", dtype="<u4", shape=(count,))
    for start in range(0, count, 1 << 20):
        chunk = slice(start, start + (1 << 20))
        expected = values[chunk].astype("<f4").view("<u4")
        if not np.array_equal(actual[chunk], expected):
            raise ValueError(f"float32 copy differs from source: {target}")
    print(f"{target}: verified {count} float32 values")


if __name__ == "__main__":
    if not os.environ.get("QOIW_DATA_DIR"):
        sys.exit("set QOIW_DATA_DIR to your local benchmark root")
    try:
        for name in ("density", "pressure", "velocityx"):
            prepare(Path(os.environ["QOIW_DATA_DIR"]), name)
    except (OSError, ValueError) as error:
        sys.exit(str(error))
