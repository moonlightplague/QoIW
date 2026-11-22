import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def cli(*args, status=0):
    command = [sys.argv[1], *map(str, args)]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
    if status == 1:
        assert result.stderr.startswith("qoiw error:"), result.stderr
    return result.stdout


def read_json(*args, status=0):
    def reject_nonfinite(token):
        raise ValueError(token)

    return json.loads(cli(*args, "--json", status=status), parse_constant=reject_nonfinite)


def round_trip_f32(root):
    raw, blob, decoded = (root / name for name in ("x.f32", "x.qoiw", "decoded.f32"))
    values = [-0.0, 1e36] + [(i + 1.5) / 64 for i in range(75)]
    source = struct.pack("<77f", *values)
    raw.write_bytes(source)
    request = ["-q", "x^2\n", "-m", "REL", ".01", "-q", "x > 0", "-m", "ABS", "0",
               "-M", "ABS", ".15", "-X", "x == 0 or x > 1e30"]
    plan = read_json("compress", "-c", "sz3", "-f", "-2", 11, 7, "-i", raw, "-z", blob,
                     "-t", 2, "--fixed-bound", ".5", "--core-option", "algo=nopred", *request, "-a")
    assert plan["report"]["plan"]["shortcut_reason"] == "fixed_bound"
    assert plan["report"]["restored"] > 0
    assert plan["report"]["verification"]["ok"] and plan["post_decode"]["ok"]
    header = read_json("inspect", "-z", blob)
    assert header == plan["report"]["stream"]
    assert header["shape"] == [7, 11] and header["dtype"] == "float32" and header["num"] == 77
    assert header["targets"][0]["expression"] == "x^2\n" and header["targets"][1]["tolerance_abs"] == 0
    assert header["fields"][0]["data_bound_abs"] == .15
    assert header["stream_bytes"] == blob.stat().st_size
    assert read_json("decompress", "--stream", blob, "--output", decoded, "--threads", 2) == header

    output = decoded.read_bytes()
    assert len(output) == len(source) and output[:8] == source[:8]
    for a, b in zip(struct.unpack("<77f", source)[2:], struct.unpack("<77f", output)[2:]):
        assert b > 0 and abs(a - b) <= .15
        assert abs(a * a - b * b) <= header["targets"][0]["tolerance_abs"]
    verification = read_json("verify", "-i", raw, "-o", decoded, "-f", "--shape", "7,11", *request)
    assert verification == plan["post_decode"]

    alias = root / "alias"
    os.link(raw, alias)
    cli("compress", "-i", raw, "-f", "--shape", "7,11", "-z", alias, status=1)
    assert alias.read_bytes() == source and raw.read_bytes() == source
    cli("decompress", "-z", blob, "-o", blob, status=1)
    assert read_json("inspect", "-z", blob) == header

    edited = bytearray(output)
    struct.pack_into("<f", edited, 8, -42)
    decoded.write_bytes(edited)
    bad = read_json("verify", "-i", raw, "-o", decoded, "-f", "--shape", "7,11", *request, status=2)
    assert not bad["ok"] and bad["violating_values"] == 1 and bad["data"]["violations"] == 1
    assert [target["errors"]["violations"] for target in bad["targets"]] == [1, 1]


def round_trip_f64(root):
    raw, blob, decoded = (root / name for name in ("x.f64", "double.qoiw", "decoded.f64"))
    values = [(i + 1.125) / 100 for i in range(96)]
    raw.write_bytes(struct.pack("<96d", *values))
    options = ["--input", raw, "--float64", "--shape", "3,4,8", "--qoi", "x^2",
               "--tolerance", "abs", ".001", "--data-bound", "NONE", "--verbose"]
    assert "Trial bound=" in cli("compress", *options)
    result = read_json("compress", *options, "--stream", blob, "--verify")
    report = result["report"]
    assert report["stream"]["shape"] == [3, 4, 8] and report["stream"]["dtype"] == "float64"
    assert report["plan"]["trace"] and result["post_decode"]["ok"]
    assert report["stream"]["fields"][0]["data_bound_abs"] is None
    cli("decompress", "-z", blob, "-o", decoded)
    output = struct.unpack("<96d", decoded.read_bytes())
    assert all(abs(a * a - b * b) <= .001 for a, b in zip(values, output))


def malformed_inputs(root):
    raw, decoded, broken = (root / name for name in ("max.f64", "min.f64", "broken.qoiw"))
    biggest = float.fromhex("0x1.fffffffffffffp+1023")
    raw.write_bytes(struct.pack("<d", biggest))
    decoded.write_bytes(struct.pack("<d", -biggest))
    verify = ["verify", "-i", raw, "-o", decoded, "-d"]
    failed = read_json(*verify, "--shape", "1", "-M", "ABS", biggest, status=2)
    assert failed["data"] == {"max_abs_error": "+inf", "violations": 1, "nonfinite_violations": 0}
    cli(*verify, "--shape", "1", "-1", 1, status=1)
    cli(*verify, "--shape", "1,0", status=1)
    cli(*verify, "--shape", "1,", status=1)
    cli(*verify, "--shape", "18446744073709551615,2", status=1)
    cli(*verify, "--shape", "2", status=1)
    cli(*verify, "--shape", "1", "-f", status=1)
    cli(*verify, "--shape", "1", "-m", "ABS", "1", status=1)
    cli(*verify, "--shape", "1", "-q", "x", status=1)
    cli(*verify, "--shape", "1", "-q", "x", "-m", "ABS", "nan", status=1)
    cli(*verify, "--shape", "1", "-t", "-1", status=1)
    cli(*verify, "--shape", "1", "-z", "stream.qoiw", status=1)
    broken.write_bytes(b"QOIW")
    cli("inspect", "-z", broken, status=1)
    cli("decompress", "-z", broken, "-o", decoded, status=1)
    assert decoded.read_bytes() == struct.pack("<d", -biggest)


assert "Usage" in cli("--help")
assert "Usage" in cli("verify", "--help")
cli("unknown", "--help", status=1)
golden = Path(sys.argv[3])
assert read_json("inspect", "-z", golden)["shape"] == [16, 32]
with tempfile.TemporaryDirectory(prefix="qoiw-cli-") as directory:
    root = Path(directory)
    malformed_inputs(root)
    if sys.argv[2] == "1":
        assert "sz3 version=" in cli("cores")
        round_trip_f32(root)
        round_trip_f64(root)
    else:
        assert not cli("cores")
        unavailable_output = root / "unavailable.f32"
        cli("decompress", "-z", golden, "-o", unavailable_output, status=1)
        assert not unavailable_output.exists()
    assert not list(root.glob(".qoiw-*"))
