# QoIW technical documentation

These pages describe the implementation in this repository. The top-level
[README](../README.md) covers building, a quick start and the command-line
inputs. The design intent and the milestone schedule are in
[PLAN.md](../PLAN.md); where the code differs from the plan, these pages
describe the code and say so.

| Page | Contents |
| --- | --- |
| [Architecture](architecture.md) | Stages, data flow, libraries, source layout, threading, errors, guarantee |
| [Building](building.md) | Dependencies, every CMake option, OpenMP and sanitizer builds, linking QoIW |
| [C++ API](api.md) | Requests, options, compression, decoding, inspection, verification, reports |
| [Command line](cli.md) | Text and JSON reports, exit status, file handling |
| [Expressions](expressions.md) | QoI grammar, evaluation, derivatives and limits |
| [Frontend](frontend.md) | F0 to F6: tolerance resolution, prior, shortcuts, sampling, measurement, search, plan |
| [Backend](backend.md) | Point-wise check, lossless corrections, verification counters |
| [Container format](format.md) | Byte layout of a version 0.0 stream |
| [Cores](cores.md) | The core interface, traits, the SZ3 adapter, mock cores, adding a core |
| [Testing](testing.md) | Test suites, guarantee matrix, NumPy checker, golden streams, CI |
| [Benchmarks](benchmarks/) | [Reference baseline](benchmarks/reference.md), [regret and parity](benchmarks/regret.md), [timing and memory](benchmarks/performance.md) |

## Implementation status

Milestones M0 and M1 of the plan are complete: one field, point-wise targets,
SZ3 as the only production core, lossless corrections, and the C++ API and
CLI. The following parts of the plan are not implemented yet.

| Planned | Milestone | State in this code |
| --- | --- | --- |
| Quantised residual corrections, fidelity limit | M3 | Only lossless corrections. `Plan::correction_mode` is always `lossless`. The container rejects RESIDUAL sections. |
| Default data bound of 1 % of the range | M3 | `Request::data_bound` defaults to `Tol::none()`. |
| Decoded-value fingerprint | M3 | The field header has the slot. The encoder writes 0 and the decoder does not check it. |
| `float32` threshold rule (plan section 6.5) | M3 | Comparisons are evaluated in double precision only. |
| Lossless fallback for incompressible data | M3 | Not implemented. |
| Core traits beyond `bound_kind`, `fixed_call_overhead`, `thread_safe`; plugins; bridges; ZFP, SPERR, SZo | M2 | Built-in registry with `sz3` only. |
| `predict`, `guard`, `apply` | M2 | Not exposed. The frontend can be called internally. |
| Target lists with regions, aggregates, several fields | M4 | Lists of point-wise targets of one field only. |
| C API, Python package | M6 | Not implemented. |
