"""Run the NumPy checker on a fresh command-line decode."""

from pathlib import Path
import subprocess
import sys
import tempfile

try:
    import numpy as np
except ModuleNotFoundError as error:
    if error.name != "numpy":
        raise
    print("NumPy is required for the independent checker tests")
    sys.exit(77)

from qoi_numpy import Target, Tolerance, check_files


def check_round_trip(cli, scratch, dtype, flag):
    shape = (21, 31)
    original = scratch / "source.raw"
    stream = scratch / "result.qoiw"
    decoded = scratch / "decoded.raw"
    samples = np.linspace(-2, 3, num=shape[0] * shape[1]).astype(dtype)
    samples[:3] = [-0., 1e36, np.nan]
    samples.tofile(original)

    subprocess.run(
        [cli, "compress", flag, "--shape", "21,31", "-i", original, "-z", stream,
         "-q", "x^2", "-m", "REL", ".001", "-q", "ln(x)", "-m", "ABS", ".04",
         "-M", "REL", ".01", "-X", "x > 1e30", "--fixed-bound", ".125", "-t", "4"],
        check=True, stdout=subprocess.DEVNULL,
    )
    subprocess.run([cli, "decompress", "-z", stream, "-o", decoded, "-t", "2"],
                   check=True, stdout=subprocess.DEVNULL)

    targets = [
        Target("x^2", np.square, Tolerance("REL", .001)),
        Target("ln(x)", np.log, Tolerance("ABS", .04)),
    ]
    data_bound = Tolerance("REL", .01)

    def check():
        return check_files(original, decoded, dtype, shape, targets, data_bound, lambda x: x > 1e30)

    report = check()
    if not report["ok"]:
        raise AssertionError(report)
    altered = np.fromfile(decoded, dtype=dtype)
    altered[10] = 1e-6  # Changes a position whose original logarithm is not finite.
    altered.tofile(decoded)
    report = check()
    if report["violating_values"] != 1 or report["targets"][1]["errors"]["nonfinite_violations"] != 1:
        raise AssertionError(report)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="qoiw-numpy-") as directory:
        for dtype, flag in (("<f4", "-f"), ("<f8", "-d")):
            check_round_trip(sys.argv[1], Path(directory), dtype, flag)
