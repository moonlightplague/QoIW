"""Read complete F6 reports as a strict JSON consumer."""
import json
import subprocess
import sys


def unique_keys(pairs):
    result = dict(pairs)
    assert len(result) == len(pairs), pairs
    return result


def reject_constant(token):
    raise ValueError(token)


rows = subprocess.check_output([sys.argv[1], "--json"], text=True).splitlines()
tie, tighter, fallback, fixed, edge = [
    json.loads(row, parse_constant=reject_constant, object_pairs_hook=unique_keys)
    for row in rows
]
assert tie["binding"] == "TIE" and tighter["binding"] == "QOI"
assert tie["bound"] == [tie["reference_bound"]] == [1]
assert tie["correction_mode"] == "lossless" and tie["data_bound_abs"] == 1
assert tie["targets"] == [{"expression": "x^2", "tolerance_abs": 0.1}]
assert tie["exact_condition"] is None and tie["shortcut_reason"] is None
assert tie["flags"] == dict(whole_input_sampled=True, prior_free=False,
                            rare_outlier_fallback=False, shortcut_taken=False)
first, second = tie["trace"]
assert second["bound"] == tighter["bound"][0] < first["bound"]
assert first == dict(bound=1, sample_values=16, restored=0, core_bytes=129,
                     overhead_bytes=0, lossless_bytes=0, input_sensitive_share=1,
                     sample_sensitive_share=1, correction_weight=1, outlier_share=0, rare_outlier_fallback=False,
                     correction_bytes_per_value=0, bytes_per_value=129/16, sample_psnr_db="+inf")
assert tie["predicted"] == dict(core_bytes_per_value=129/16, correction_bytes_per_value=0,
                                bytes_per_value=129/16, outlier_share=0, sample_psnr_db="+inf",
                                indicative_compression_ratio=128/129)
assert fallback["binding"] == "DATA_BOUND" and fallback["exact_condition"] == "x < 0"
assert fallback["flags"]["rare_outlier_fallback"] and fallback["predicted"]["outlier_share"] == 0.00025
assert fixed["flags"]["shortcut_taken"] and fixed["shortcut_reason"] == "fixed_bound"
assert fixed["data_bound_abs"] is None and fixed["binding"] is None
assert fixed["predicted"] is None and fixed["trace"] == []
assert edge["targets"] == [{"expression": '"\n\\\0', "tolerance_abs": float.fromhex("0x1p-1074")}]
assert edge["trace"][0]["sample_psnr_db"] == "-inf" and edge["trace"][0]["restored"] == 123456
