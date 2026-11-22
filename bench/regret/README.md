# M1 SZ3 regret benchmark

The manifest fixes the 20 cases from the research tuning study, with raw
float32 inputs in C order. The harness resolves both relative tolerances
using NumPy and supplies the same absolute values to QoIW and QPET. Use the
pinned QPET build and local data layout from the reference guide.
[Cases](../cases/regret.csv) · [Reference build and data](../reference/README.md)

Build the QoIW CLI and the helper that exposes the smallest positive
sampled F1 radius and scan cap. Build both compressors against the same
SZ3 checkout and zstd. The example pins the system zstd CMake package on
Ubuntu x86-64.

```sh
cmake -S . -B build-regret -DCMAKE_BUILD_TYPE=Release \
  -DQOIW_BUILD_BENCHMARKS=ON -DQOIW_BUILD_TESTS=OFF \
  -Dzstd_DIR=/usr/lib/x86_64-linux-gnu/cmake/zstd
cmake --build build-regret --parallel 4

TMPDIR=/dev/shm python3 -B bench/regret/run.py \
  --qoiw build-regret/qoiw --prior build-regret/qoiw_regret_prior \
  --qpet build-reference/qpet/qpet \
  --qpet-revision a80e6861fc15e7a66eb1f36b6172911a541b9d3c \
  --data build-reference/data --results build-regret/m1-results \
  --threads 16 --jobs 4 > build-regret/full.json
```

Use `--case hur-u-x2` to select a single case, or repeat `--case` to
select several. `--parity-only` measures the tuned pair first. Running
again without that option and with the same settings resumes from the
saved work. A changed binary, input digest, or sweep setting requires a
fresh results directory. Pass `--qoiw-revision` when the supplied binaries
come from a different commit than the checkout running the harness.
`TMPDIR` must have room for a saved stream and decoded field per worker.

The oracle measures up to 21 log-spaced full-field bounds from the smallest
positive sampled radius to the data cap. The lower endpoint is floored at
`cap × 1e-9` as in the research. Three refinement rounds try the geometric
midpoints on either side of the smallest valid stream. The tuned stream
also participates so regret cannot fall below 1.0.

Each trial saves its container, decodes it in a separate process, and
checks the raw values with the independent NumPy checker. The journals
record commands, tool output tails, stream and correction sizes, PSNR, the
error-to-tolerance ratio, stage and wall times, and the load at the start of
each trial. The summary reports both build revisions and the size ratios.

Exit status 0 means the measured cases passed the requested gates. Status 2
means a completed measurement failed a gate. Status 1 means a case failed
to complete. The parity gate checks the 3% geometric-mean band and the 10%
per-case band in both directions. The regret gate checks a geometric mean
of at most 1.07. A result from a subset applies to that subset.

## Observed M1 comparison

The local run used QoIW `f7e6196`, the pinned QPET revision, SZ3
`28387bee81d54f00d4644b1e3f52424b1dcafcc1`, and the same system zstd
library. Each case used 16 threads with up to four concurrent case
workers. These wall times are indicative on the shared machine.

All 20 tuned pairs passed the independent checks. The geometric mean of
QoIW size divided by QPET size was 0.98083, within the 3% band. The
symmetric per-case gate failed for two smaller QoIW streams.

| Case | QoIW tuned bytes | QPET tuned bytes | QPET bytes at the QoIW bound |
| --- | ---: | ---: | ---: |
| `cesm-cldhgh-x2` | 2,025,876 | 2,329,731 | 2,025,836 |
| `cesm-precsc-x3` | 20,079 | 27,992 | 20,039 |

At the same bound, both tools produced the same SZ3 payload sizes in these
two cases and both decoded fields passed the independent check. The
remaining 40-byte differences are in the containers and correction layers.
