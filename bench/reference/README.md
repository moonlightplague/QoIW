# M0 reference baseline

Use the public QPET revision and the SZ3 gitlink from the QoIW checkout to
reproduce the Hurricane result in section 13.4 of [PLAN.md](../../PLAN.md).
The reference needs a C++17 compiler, OpenMP, pkg-config, zstd, CMake, and
Python with NumPy. From the QoIW repository root, run

```sh
git -C research worktree add --detach ../../qoiw-qpet-a80e686 a80e6861fc15e7a66eb1f36b6172911a541b9d3c
git submodule update --init SZ3
cmake -S ../qoiw-qpet-a80e686 -B build-reference/qpet -DCMAKE_BUILD_TYPE=Release \
    -DQPET_SZ3_SOURCE_DIR="$PWD/SZ3" -DQPET_WITH_SZ3=ON \
    -DQPET_WITH_SZO=OFF -DQPET_WITH_ZFP=OFF -DQPET_WITH_SPERR=OFF
cmake --build build-reference/qpet --parallel 4
OMP_NUM_THREADS=4 ctest --test-dir build-reference/qpet --output-on-failure
```

Set `QOIW_DATA_DIR` to the directory containing `Hurricane/Uf48.bin.f32`.
The anchor checks the input SHA-256, compresses with 16 threads, then invokes
a separate decoder on the saved stream. NumPy reads all 25 million decoded
float32 values, evaluates the squared field in float64, and checks the largest
error against 0.001 times the original squared-field range. The output JSON
also records PSNR, stream size, and executable and stream digests. QPET's own
diagnostics go to standard error. On Linux with space in shared memory, run

```sh
TMPDIR=/dev/shm python3 bench/reference/hurricane_anchor.py build-reference/qpet/qpet
```

This anchor uses the public QPET default of an unbounded data error. M1 parity
cases supply the same explicit data bound to both implementations.

### Observed anchor

On the M0 checkout with SZ3 `28387bee81d54f00d4644b1e3f52424b1dcafcc1`,
all three QPET tests passed. QPET selected 0.0863667 after 18 candidates and
reported 334,063 outliers. The saved stream was 4,586,625 bytes (21.8025×).
The independent check found a maximum squared-field error of 2.8113910487 at
an absolute tolerance of 2.8113926907, a ratio of 0.999999416, and a PSNR of
67.09285 dB. These are reference measurements for comparison with M1.

### Benchmark inputs

Set `QOIW_DATA_DIR` to a local directory with the dataset folders in
[inputs.csv](inputs.csv). Directory symlinks can point to existing read-only
SDRBench files. In particular, `Miranda` holds the 256 × 384 × 384 float64
fields, `NWChem` holds the three raw float64 arrays, and `JHTDB` holds the
`velocity` and `pressure` directories of the iso1024 cutout. The field list
covers inputs for the 20 regret cases and selected section 14.3 fields. Shapes
are in C order and the raw values are little-endian.

```sh
python3 bench/reference/prepare_miranda.py
python3 bench/reference/check_inputs.py
python3 bench/reference/check_inputs.py --dataset Hurricane --dataset Miranda_f32
```

The preparation step uses about 432 MiB under `$QOIW_DATA_DIR/Miranda_f32`.
It casts density, pressure, and x-velocity in chunks and checks every output
bit against the float32 cast of the source. A repeat run verifies existing
files. The inventory check opens each named file, checks its exact byte length,
and reads both ends. It exits unsuccessfully if any selected file fails.

The M0 run verified 106 files, including the three converted Miranda fields.
The eight JHTDB fields for steps 0500 and 5000 await read access on this machine.
All inputs for the 20 M1 regret cases passed the inventory check.

Use the same data root and reference build for the
[M1 regret and parity benchmark](../regret/README.md).
