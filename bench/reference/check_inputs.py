"""Check the readability and byte lengths of local benchmark fields."""

import argparse
import csv
import math
import os
from pathlib import Path
import sys


def check_file(path, byte_count):
    with path.open("rb") as source:
        size = os.fstat(source.fileno()).st_size
        if size != byte_count:
            raise ValueError(f"expected {byte_count} bytes, found {size}")
        if not source.read(1):
            raise ValueError("empty file")
        source.seek(-1, os.SEEK_END)
        if not source.read(1):
            raise ValueError("cannot read final byte")


def main():
    with Path(__file__).with_name("inputs.csv").open(newline="") as source:
        rows = list(csv.DictReader(source))
    datasets = list(dict.fromkeys(row["dataset"] for row in rows))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", choices=datasets, action="append", help="check one dataset (repeatable)")
    args = parser.parse_args()
    if not os.environ.get("QOIW_DATA_DIR"):
        parser.error("set QOIW_DATA_DIR to the data root described in docs/benchmarks/reference.md")
    root = Path(os.environ["QOIW_DATA_DIR"])
    ok = True
    for dataset in args.dataset or datasets:
        passed = total = 0
        for row in rows:
            if row["dataset"] != dataset:
                continue
            size = math.prod(map(int, row["c_shape"].split("x"))) * {"<f4": 4, "<f8": 8}[row["dtype"]]
            for field in row["fields"].split() or [""]:
                path = root / row["path"].format(field=field)
                total += 1
                try:
                    check_file(path, size)
                except (OSError, ValueError) as error:
                    print(f"{path}: {error}", file=sys.stderr)
                    ok = False
                else:
                    passed += 1
        print(f"{dataset}: {passed}/{total} fields readable at the expected length")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
