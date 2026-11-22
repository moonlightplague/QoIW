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
The two runs in each pair must agree on the bound and core payload
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

## Observed M1 results

The run completed all 120 pairs with QoIW `1266c34`, SZ3 `28387bee`,
GCC 13.3, and system zstd 1.5.5. The host was a shared Linux workstation
with a 64-core AMD Ryzen Threadripper PRO 7985WX and 128 logical CPUs.
The one-minute load average before each pair ranged from 2.14 to 3.42.
The runner used one worker and had no exclusive reservation of the host.

| Measure | Limit | 1 thread | 16 threads |
| --- | ---: | ---: | ---: |
| Median compression time / plain SZ3 | 3.50× | 5.32× | 2.46× |
| Median frontend share | 35% | 46.2% | 25.2% |
| Median decompression time / plain SZ3 | | 1.00× | 1.01× |
| Highest case median decompression time / plain SZ3 | 1.10× | 1.74× | 1.77× |
| Median QoIW compression peak RSS / input | | 4.14× | 4.34× |
| Median plain SZ3 compression peak RSS / input | | 4.13× | 4.13× |

The one-thread compression median remains 4.55× after subtracting the
report's measured verification interval. The only case above the
decompression limit was `scale-rh-tanh-3` at both thread settings. Its
correction layer restores 24,457,211 of 141,120,000 values. Case medians
for QoIW peak RSS ranged from 4.07× to 4.80× the input size.
