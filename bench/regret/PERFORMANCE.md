# M1 timing and memory

Run the benchmark on a quiet Linux machine with an OpenMP-enabled Release
build and NumPy. The runner uses the 20 float32 C-order cases and the same
independently resolved absolute tolerances as the regret sweep.
[Cases](../cases/regret.csv) · [Reference data](../reference/README.md)

```sh
cmake -S . -B build-performance -DCMAKE_BUILD_TYPE=Release \
  -DQOIW_BUILD_BENCHMARKS=ON -DQOIW_BUILD_TESTS=OFF
cmake --build build-performance --target qoiw_performance_probe --parallel 4

OMP_PROC_BIND=close OMP_PLACES=cores python3 -B bench/regret/performance.py \
  --probe build-performance/qoiw_performance_probe \
  --data build-reference/data --results build-performance/m1-results \
  > build-performance/summary.json
```

The runner processes one case at a time with three paired measurements
each at 1 and 16 threads. A pair uses separate fresh processes for QoIW
and plain SZ3. QoIW tunes the bound, and plain SZ3 uses exactly that bound
through the same adapter. Both use the default serial full-field SZ3
mode, which keeps payloads independent of the thread count. The thread
setting applies to QoIW's parallel work. The first and third repetitions
run QoIW first. The second runs plain SZ3 first at the first pair's bound.
The two codecs in each pair must agree on the bound and core payload
length. Each case must retain its chosen bound and container length across
repetitions and thread settings.

The probe times the in-memory compression and decompression calls. It
reads the input before timing and allocates the public decode output
before timing decode. QoIW compression includes generating the public
report and verifying the corrected reconstruction. The report records
the planning and verification intervals separately. Decode includes
parsing the container and applying the lossless corrections. The
fingerprint and advisor become measurable when M3 and M2 add them.

The probe reads its Linux `VmHWM` just after compression, before allocating
the output buffer for the public decode. This measures peak resident
memory in the probe's address space, including the input and process
overhead. Resident pages can differ from the total capacity allocated
by the codec.

The summary takes the median of the three paired ratios for each case
and thread setting. The corpus median must be at most 3.5 for compression
time relative to plain SZ3 and 35% for frontend time as a share of QoIW
compression. The decompression limit is 1.1 times plain SZ3 and applies
to each case median, since §15.1 specifies a median for the first two
budgets. The summary includes the corpus median, the largest case
median, and cases over each limit. Status 0 means every reported budget
passed. Status 2 means a completed run exceeded at least one budget.
Status 1 means the run failed to complete.

`--case` selects cases by name for shorter runs. Results from a subset
apply to that subset. Rerun with the same command to resume from
`timings.jsonl`. The journal records executable and input digests, the
build revision, CPU affinity, OpenMP settings, commands, and load
averages. Use a fresh directory when the recorded settings change or
when measuring again under different load. Pass `--revision` when the
probe was built from a commit other than the checkout running the harness.
