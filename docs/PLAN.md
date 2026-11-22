# QoIW: a QoI-preserving wrapper for fixed-grid lossy compressors

**Project plan. Draft for review, 2026-11-21. No code exists yet.**

`qoiw` ("QoI Wrapper") is a working name; see decision D1 in [section 17](#17-decisions-needed-from-reviewers).

## Contents

1. [Summary](#1-summary)
2. [Goals and non-goals](#2-goals-and-non-goals)
3. [Vocabulary](#3-vocabulary)
4. [What the research already settled](#4-what-the-research-already-settled)
5. [Architecture](#5-architecture)
6. [The QoI model: how requirements are stated](#6-the-qoi-model-how-requirements-are-stated)
7. [Frontend: predicting the global error bound](#7-frontend-predicting-the-global-error-bound)
8. [Core adapter layer: connecting compressors without touching them](#8-core-adapter-layer-connecting-compressors-without-touching-them)
9. [Backend: verification and outlier correction](#9-backend-verification-and-outlier-correction)
10. [Container format](#10-container-format)
11. [Public API](#11-public-api)
12. [Repository layout and build](#12-repository-layout-and-build)
13. [Testing and verification](#13-testing-and-verification)
14. [Benchmarking and evaluation](#14-benchmarking-and-evaluation)
15. [Performance and memory budget](#15-performance-and-memory-budget)
16. [Milestones](#16-milestones)
17. [Decisions needed from reviewers](#17-decisions-needed-from-reviewers)
18. [Risks](#18-risks)
- [Appendix A: default parameters](#appendix-a-default-parameters)
- [Appendix B: reading map of `research/`](#appendix-b-reading-map-of-research)
- [Appendix C: SZ3 integration notes](#appendix-c-sz3-integration-notes)

### How to read this document

- Statements about measured behaviour come from `research/` (the QPET research handoff) and cite the file. The
  QPET source itself is not in `research/`'s working tree: it lives on the branches `origin/qpet/main` (public
  release) and `origin/qpet/qpet-unified` (newest research version) of the `research/` git repository. Read a file
  with `git -C research show origin/qpet/main:docs/tuning.md`.
- Everything described as "proposed" or "new in this plan" is a design decision made here and is open to review.
- Sections 5 to 12 are the design. Sections 13 to 16 say how we know it works and in what order to build it.
  Section 17 lists the decisions I could not make for you.

---

## 1. Summary

**What we build.** A library and command-line tool that wraps any fixed-grid error-bounded lossy compressor, without
modifying it, so that a user-stated quantity of interest (QoI) stays within a user-stated tolerance. It has three
stages:

| Stage | Job | Input | Output |
|---|---|---|---|
| **Frontend** | Predict one global error bound for the compressor | data, QoI, QoI tolerance | a bound (one number per field) |
| **Core** | The unmodified compressor, driven through an adapter | data, bound | the compressor's own stream |
| **Backend** | Check the decompressed data against the original and repair every outlier | original, decompressed, QoI | correction layers |

Decompression runs the compressor's decompression and applies the corrections. It needs neither the QoI nor the
original.

**What is guaranteed.** Every stated QoI tolerance and the data bound hold on the decompressed data, whatever the
compressor did and however wrong the prediction was. The guarantee comes only from the backend's check on the
values the decoder will produce. The frontend affects size and speed, never correctness.

**First compressor core.** SZ3, from the `SZ3/` submodule, through its public header `SZ3/api/sz.hpp`.

**How other compressors connect.** Four mechanisms, none of which changes compressor source
([section 8.4](#84-four-ways-to-connect-a-core)): an in-tree adapter file, a C plugin loaded at run time, a
libpressio bridge, and a command-line bridge. A fifth path needs no adapter at all: the frontend and backend are
usable on their own, so a user can run any compressor themselves between `predict` and `guard`.

**How QoIs are accepted.** A text expression language, lists of targets with separate tolerances, and a native
interface for QoIs the language cannot express ([section 6](#6-the-qoi-model-how-requirements-are-stated)).

**What to expect**, from the research's final study (`research/HANDOFF.md`, 148,411 rows, every row re-checked with
NumPy):

- The plain compressor at the data bound breaks the QoI in 66 % of runs. The wrapped pipeline had 0 violations in
  14,412 runs.
- Compression costs a median 3.4 times one plain compression. A search for the best uniform bound costs 15.6 times.
- At equal QoI tolerance the streams are 1.4 to 1.9 times smaller than the best uniform bound, with a PSNR about
  8 dB lower. At equal PSNR the uniform bound is as good or slightly better. The wrapper's value is the guarantee,
  the one-pass speed, and QoIs that no uniform-bound search can express. It is not fewer bits at equal PSNR.

These figures are for the research's newest version over four compressors. Version 1 of this project leaves out
some of its mechanisms ([section 16.3](#163-deliberately-left-out-of-version-1)) and should land between the public
QPET release and that version, which is 1.03 to 1.13 times smaller than the public release.

One number was reproduced on this machine while writing this plan: the public QPET release, built against the
`SZ3/` submodule, compresses Hurricane U with `x^2` at a relative tolerance of 1e-3 to a ratio of 21.8 in 0.67 s,
2.4 times the SZ3 compression inside it ([section 13.4](#134-parity-gates)).

**Order of work** ([section 16](#16-milestones)): an SZ3-only version for point-wise QoIs first, with parity against
the public QPET release as its acceptance test; then the core adapter layer and the cheaper corrections in
parallel; then broader QoIs; then bindings and scale.

---

## 2. Goals and non-goals

### Goals

1. **Any fixed-grid lossy compressor as the core, unmodified.** Regular arrays of 1 to 4 dimensions, `float32` and
   `float64`. Adding a compressor is one adapter file or one plugin, with no change to the frontend or backend.
2. **QoI requirements accepted freely.** Any expression of the data at a grid point, of several fields, of regional
   aggregates and of functions of aggregates; several requirements at once; a path for QoIs that are not
   expressible in the language.
3. **A sampling frontend** that predicts the global error bound from the QoI and its tolerance in about one extra
   compression's worth of time.
4. **A backend that copes with outliers**: verifies everything and stores compact corrections.
5. **A hard guarantee** that never depends on the prediction or on the compressor honouring its bound.
6. **Stages usable separately**: `predict` (frontend only), `guard` and `apply` (backend only), and the integrated
   `compress` and `decompress`.
7. **Interfaces**: C++17 library, C API, Python package, CLI.

### Non-goals for version 1

- Changing or forking any compressor. In-compressor per-value bounds were measured at about 4 % gain at equal PSNR
  for 2 times the compression time and were not adopted (`research/sz3_pointwise/REPORT.md`).
- Unstructured meshes, AMR hierarchies and particle data. A 1-D array of particles can be passed, but nothing is
  designed for it.
- GPU compressors. The adapter interface does not preclude them.
- An LLM or script-tracing step that derives the QoI from user analysis code (`research/docs/DECISIONS.md`,
  principle 8).
- Reading streams written by QPET.
- The mechanisms that the research found to trade data quality for bytes: the change of variables, per-block
  steps, and coded layers for PSNR and SSIM targets. The container reserves space for them
  ([section 16.3](#163-deliberately-left-out-of-version-1)).

---

## 3. Vocabulary

The research documents call the compressor the "backend". This plan follows the wording of the task instead. The
two conflict, so the mapping is spelled out once here.

| Term in this plan | Meaning | Term in `research/` |
|---|---|---|
| **Core** | the unmodified lossy compressor (SZ3, ZFP, SPERR, ...) | backend |
| **Core adapter** | the code that calls a core through its public API | backend adapter / wrapper |
| **Frontend** | QoI analysis, sampling and prediction of the bound | estimate + tuner |
| **Backend** | verification and correction of outliers | check and correct; correction layers |
| **Container** | the stream format that holds the core's stream and the corrections | stream format |
| **Target** | one QoI with its regions and its tolerance | target |
| **Request** | all targets, the data bound and the exact-value condition | request / Config |
| **Bound** | the single number given to the core for one field | backend error bound, `eb` |
| **Data bound** | the user's limit on the point-wise error of the data itself | data bound, `eps`, `-M` |
| **Prior** | the per-value estimate of how much error a value tolerates | per-value estimate |
| **Outlier** | a value or region that breaks a target after the core's decompression | outlier |
| **Correction** | what is stored to repair an outlier | patch, residual, correction layer |

---

## 4. What the research already settled

The research turned QPET (VLDB 2025) into a black-box layer over unmodified compressors and measured it at scale.
This project is an engineering generalisation of that design, so we start from its evidence and do not re-derive
it.

| Finding | Evidence | Consequence for this plan |
|---|---|---|
| Guarantees must come from a check of the decoded values, never from an estimate | 0 violations in 14,412 runs; the paper's artifact exceeded the tolerance in 16 % of its runs, by small amounts (`HANDOFF.md`) | Section 5.4; invariants I1 to I6 |
| A compressor's own bound cannot be trusted | plain SPERR exceeds its own bound by 2e-7 to 2e-5 relative on two Hurricane fields (`paper_eval_pilot/README.md`); the paper artifact's SZ3 path reached 2.0 times its data bound (`paper_eval_full/analysis/violations.md`) | Adapter contract does not require the bound to hold; adversarial mock cores in the tests |
| One-pass tuning on a 1 % sample is enough | same bound as a full scan in 79 % of cases; regret 1.07 (SZ3, SZo), 1.01 (SPERR), 1.00 (ZFP) (`sample_prediction/REPORT.md`) | Frontend design of section 7 |
| The prior is worth keeping, but only needs the right order of magnitude | a tuner without it reaches 0.93 of the ratio (`docs/branches/tuner-search-exp.md`); an exact prior gains 0.3 %; a prior 4 times too small costs 18 % (`estimate_sensitivity/REPORT.md`) | Cheap Taylor prior; a prior-free search exists only as a fallback |
| The absolute byte count of a block sample is off by 1 to 6 times, by field | `sample_prediction/REPORT.md`, finding 1 | Never compare alternatives by absolute sample bytes; never calibrate with a constant |
| What a faithful sample looks like depends on the compressor | ZFP codes 4^d cells independently and reads only the exponent of the tolerance; its cell sample predicts corrections within 5 % (`qpet/main:docs/tuning.md`) | Core traits drive the sampler and the search (section 8.3) |
| Quantised residuals are an efficient correction; an entropy coder for them is not worth it | streams 1.36 times smaller than with lossless values, transform off; positions within 10 to 20 % of entropy; the best alternative coding changes the stream by under 1 % in the median (`correction_layer/REPORT.md`, `residual_coding/README.md`) | Backend codec of section 9.3 |
| Cheap corrections push the bound up and the data quality down | PSNR loss comes from the raised bound, not from the correction (`correction_efficiency/findings.md`, 15,864 runs) | Fidelity limit and tie rule (section 7.4) |
| A QoI tolerance alone leaves the data unusable for anything else | block means let single values drift by a quarter of the range (`qpet/qpet-unified:docs/concepts.md`) | Data bound on by default, 1 % of the range (decision D3) |
| The change of variables rarely pays at the default fidelity | automatic against off: 1.01; forced on: 0.87 (`HANDOFF.md`, result 6) | Not in version 1 |
| Real QoIs: of 62 from 9 domains, about 46 fit point-wise, multi-field and regional forms; the rest need stencils, spectra or pipelines | `real_qois/REPORT.md`, `auto_analysis/design.md`; a classification by reading, not a measurement | Expression language first; footprint interface for the rest (section 6.3) |
| Regions that span a whole dimension cannot be sampled in blocks | tuning becomes 80 to 90 % of the time (`qpet/qpet-unified:docs/tuning.md`) | Supported but slow; warned about; listed as a risk |

Principles of the research owner that this plan adopts (`research/docs/DECISIONS.md`): one functional framework
instead of per-QoI schemes; one-pass tuning; data usability beside the QoI; a firm and smooth point-wise error; no
computational overhead without a measured gain; generality over special handling; every result checked
independently.

---

## 5. Architecture

### 5.1 Stages and data flow

```
 compress(fields, request, options)

   fields, request
         |
         v
 +--------------------------------------------------------------+
 | FRONTEND                                                     |
 |  resolve   ranges, absolute tolerances, exact-value mask     |
 |  prior     per-value tolerable error on ~1e6 spread points   |
 |  sample    blocks or cells, shaped by the core's traits      |
 |  measure   core round trip on the sample + the real backend  |<---+
 |  search    candidates -> one bound per field                 |    |
 +--------------------------------------------------------------+    | uses the same
         | Plan (bound, trace)                                       | code on sample
         v                                                           | blocks
 +--------------------------------------------------------------+    |
 | CORE ADAPTER   compress(whole field, bound) -> core stream   |    |
 |                decompress(core stream)      -> x'            |    |
 +--------------------------------------------------------------+    |
         | x'  (exactly what a decoder will get)                     |
         v                                                           |
 +--------------------------------------------------------------+    |
 | BACKEND                                                      |----+
 |  verify    every target, the data bound, exact values        |
 |  correct   quantised residual -> lossless value (last word)  |
 |  encode    correction layers                                 |
 +--------------------------------------------------------------+
         | corrections, report            ^
         v                                | second look: if corrections dominate,
 +----------------------+                 | halve the bound and repeat (rare)
 | CONTAINER  header    |-----------------+
 |  core stream, layers |
 +----------------------+

 decompress(stream) = container reader -> core adapter decompress -> apply corrections
```

### 5.2 Operating modes

| Mode | Calls | When to use |
|---|---|---|
| **Integrated** | `compress`, `decompress` | the normal case: one self-describing stream |
| **Advisor** | `predict` | the user only wants the bound, and runs the compressor inside their own I/O stack (HDF5 filter, ADIOS, in situ) |
| **Guard** | `guard`, then `apply` on the reading side | the data was compressed elsewhere by any compressor; a sidecar correction file makes the result QoI-safe |
| **Fixed bound** | `compress` with `fixed_bound` | experiments, oracle scans, reproducing a result |

Advisor and guard together are the loosest way to "connect" a compressor: no adapter, no plugin, the compressor is
not even linked. `guard` needs the decompressed array, so the user decompresses once on the writing side.
With a core available, `predict` runs the full measured search. Without one it falls back to the prior alone and
returns a chosen quantile of it, which is cheap and less accurate. This mode split is new in this plan; the
research code offered only the integrated mode.

### 5.3 Module dependency rules

```
common  <-  expr  <-  qoi  <-  backend  <-  frontend  <-  pipeline  <-  capi, cli, python
common  <-  container  <-------------------------------  pipeline
common  <-  core (interface, registry)  <-  frontend, pipeline
core/adapters/*  ->  core interface only
decode library   =   common + container + core + backend/apply
```

- The **backend does not depend on the frontend or on any core**. It works on an original array, a decoded array
  and a resolved request. This is what lets the frontend call the real backend on sample blocks and lets `guard`
  exist.
- The **decode path is its own small library** (`qoiw_decode`): container reader, core registry, `apply`. It links
  no expression parser and no frontend. Readers and I/O filters embed only this.
- The research code has one 188 kB `core.cpp` that includes six `.ipp` files. We split by stage so that the three
  stages can be built and tested by different people.

### 5.4 The guarantee, and the invariants that keep it

For every target, the data bound and the exact-value condition, on the array the decoder produces:

| What | Statement |
|---|---|
| Point-wise target | `\|f(x) - f(x')\| <= tau` at every grid point |
| Regional target | `\|g(region of x) - g(region of x')\| <= tau` for every region |
| Data bound | `\|x - x'\| <= eps` at every value |
| Non-finite QoI | where `f(x)` is NaN or infinite, `x' == x` bit for bit |
| Exact-value condition | values that satisfy it are reproduced bit for bit, and no other value comes to satisfy it |

Invariants. A change that breaks one of these is a guarantee bug, whatever the tests say.

- **I1. Nothing is trusted that was not checked on decoder-identical values.** The backend checks the array as the
  decoder will compute it, with the same arithmetic, after rounding to the storage type. The prior, the sample and
  the search are never inputs to correctness.
- **I2. The last word is always "put the original value back".** Every cheaper correction is tried, checked, and
  dropped if it fails. A restored value is exact, and in the worst case every value an output depends on is
  restored and its error is zero, so the correction always terminates.
- **I3. Corrections use exactly representable arithmetic.** An integer times a power of two, one IEEE-754 addition
  in double precision, one rounding to the storage type. No fused multiply-add can change the result.
- **I4. The decoder needs no QoI, no tolerance and no math library.** Decoding cannot disagree with encoding about
  what `log2` returns.
- **I5. Comparisons are written so that NaN fails.** The test is `!(err <= tol)`, in double precision.
- **I6. The one assumption about the core is that its decompression is deterministic**: the same core stream
  decodes to the same bits at compression time and later, on any thread count and build. The container stores a
  fingerprint of the final array so that a decoder can detect a core that breaks this
  ([section 10](#10-container-format)).

Limits of the guarantee, to be documented for users as the research does: the QoI is evaluated in double precision
with the C math library. An analysis that evaluates the same formula in single precision, or a mathematically
equal but different formula, can differ by rounding. One known case is handled explicitly: thresholds on `float32`
data ([section 6.5](#65-rules-that-are-easy-to-get-wrong)).

---

## 6. The QoI model: how requirements are stated

### 6.1 A request

```
Request
  targets[]       one or more; all must hold at once
    qoi           expression text, or a native QoI object
    region        point-wise | blocks | sliding windows | labels   (+ optional weights)
    tolerance     ABS value | REL share of the QoI's value range
  data_bound      ABS | REL share of the data's value range | NONE      default: REL 0.01
  exact           optional condition marking values to keep bit-exact  e.g. "x > 1e30"
  field_names     for several fields: the variable names used in expressions
  known_ranges    optional precomputed value ranges, for distributed use
```

A single QoI is a list of one target. The research restructured its code around "list of targets plus data bound"
as the primary path and found the single-target path to be a special case of it; we start there.

### 6.2 Three ways to give a QoI

1. **Expression text.** Portable across C++, C, Python and the CLI, and recorded in the stream for provenance.
   Derivatives come from automatic differentiation, so the user derives nothing. The grammar is the research's
   (`qpet/qpet-unified:docs/qoi.md`):

   | Element | Forms |
   |---|---|
   | arithmetic | `+ - * / ^`, unary minus, parentheses, `\|x\|`, implicit product `10x` |
   | functions | `sin cos tan asin acos atan sinh cosh tanh exp log ln log2 log10 sqrt cbrt abs sigmoid`, `log(x, b)`, `pow`, `min`, `max` |
   | steps | `floor ceil round sign` |
   | piecewise | `c ? a : b`, `if(c, a, b)`, comparisons, `and or not` |
   | exactness | constants `nan`, `inf` mark values that must be exact |
   | several fields | named variables: `sqrt(u^2+v^2+w^2)` |
   | aggregates | `mean(f)`, `sum(f)`, `var(f)` over a region; functions of them: `mean(x^2) - mean(x)^2` |

2. **Target lists.** Several expressions, each with its own tolerance and regions. This replaces the hand-encoded
   "accuracy plus walls" formulas that scientists otherwise write (`research/real_qois/COOKBOOK.md`).

3. **Native QoI** (new in this plan, milestone M5). A C++ class, or a Python callable through the C API, for what
   the language cannot say: stencils, spectra, custom pipelines. It implements the interface of the next section.

### 6.3 One internal interface for every kind of target

Every target, however it was given, compiles to the same interface. The frontend and the backend are written
against this interface only. Supporting a new class of QoI means implementing it, not editing the stages.

```cpp
// Proposed. One compiled target, as the frontend and the backend see it.
class TargetOps {
 public:
  enum class Kind { Point, Regions, Windows, Labels, Stencil, Global };
  virtual Kind kind() const = 0;

  // Errors of the target's outputs inside a box of the array (a sample block or the whole input).
  // Called with the original and the current decoded values. Batched; no per-value virtual call.
  virtual void errors(const Box& box, const Fields& orig, const Fields& dec, ErrorSink& out) const = 0;

  // Locality: which input values one output depends on, and which outputs one input value touches.
  // The backend uses the first to choose what to repair and the second to know what to re-check.
  virtual Footprint footprint(OutputId out) const = 0;
  virtual void outputs_touching(size_t flat_index, OutputList& outs) const = 0;

  // What a sample block must look like to contain whole outputs: alignment and halo width per dimension.
  virtual SampleShape sample_shape() const = 0;

  // Optional prior: the largest error this grid point tolerates, moving alone. Absent for black boxes.
  virtual bool has_prior() const { return false; }
  virtual double tolerable_error(const double* x_at_point, const PriorContext&) const;

  // Which input to restore first inside an output that is out of tolerance (largest contribution first).
  virtual void repair_order(OutputId out, const Fields& orig, const Fields& dec, IndexList& order) const = 0;
};
```

| Kind | Footprint of one output | Sample block must | Backend repair | Prior |
|---|---|---|---|---|
| Point | the grid point | nothing special | residual, else lossless, at that point | Taylor, second order |
| Regions (aligned blocks) | the block | start on a region boundary and hold whole regions | restore values, largest contribution first | Taylor, enlarged for cancellation |
| Windows (overlapping) | the window | hold at least one window | same, in rounds with a dirty set | as regions |
| Labels | all points with the label | cannot be met; sees parts of regions | same | as regions, weak |
| Stencil | the neighbourhood | carry a halo of the stencil radius | refine the largest contributor, re-check touched outputs | worst-case split of the tolerance |
| Global | the whole array | cannot be met | restore by contribution; or second look | none in general |

The first four rows are what the research library implements. The stencil row follows the prototype outside the
library, which reached regret 1.00 to 1.02 against a full scan on smooth stencil QoIs
(`research/stencil_qoi/REPORT.md`). The global row is deliberately weak: a tolerance on a few outputs does not
constrain the bound, so such a target is accepted only together with a data bound
(`research/spectral/REPORT.md`, `research/auto_analysis/REPORT.md`).

### 6.4 Tolerances

| | ABS | REL |
|---|---|---|
| point-wise target | `tau` | `tau * (max f - min f)` over the input, where `f` is finite |
| regional target | `tau` | `tau * (max - min)` of the regional values |
| data bound | `eps` | `eps * (max x - min x)` |

- A zero range makes the relative value an absolute one.
- Values marked by `exact` and non-finite values are left out of every range.
- The absolute numbers actually used are reported and stored in the stream.
- **New in this plan:** `known_ranges` lets a caller supply the ranges. A relative tolerance otherwise depends on
  the extremes of whatever piece of the data one call sees, which differs between MPI ranks, tiles and time steps.

### 6.5 Rules that are easy to get wrong

Each of these was a real defect or surprise in the research and becomes a regression test
([section 13](#13-testing-and-verification)).

- **Thresholds on `float32` data.** `x > 88.16` evaluated in double differs from `x > 88.16f` at exactly one
  value, the `float32` nearest the constant. For `float32` arrays a comparison of a bare variable with such a
  constant is treated as non-finite at that value, which keeps it exact and lets no other value become it. On NYX
  baryon density 13,000 to 18,000 values landed on that value before the fix, with the change of variables in use.
  Without it none were observed, but nothing prevented it (`research/reports/qpet_full_features.typ`, section on
  float32 thresholds).
- **Fill values are a condition, not a value.** A `float32` file holds `9.99999962e35`, not `1e36`. `exact`
  takes `x > 1e30`. Masked values are replaced by their predecessor before the core sees them, so a block of fill
  values is not a cliff and cores that fail on huge numbers (SPERR) still work.
- **Statistics count only the values present.** A mean over a region with masked or non-finite points divides by
  the number of points that remain.
- **Variance needs centred arithmetic.** `mean(x^2) - mean(x)^2` loses all digits on a flat block. `var(f)` is a
  primitive for that reason.
- **Sign flips near zero.** A value of `5.5e-14` decoded as `-5.5e-14` makes `ln` non-finite on the decoded side.
  The check must treat a non-finite decoded QoI as a violation.
- **`round` is half-away-from-zero**, as in C. NumPy rounds half to even. Document it; consider adding `rint`.
- **Relative tolerance on heavy-tailed QoIs** can be enormous (NYX block variance: 2,700 times the median). The
  report prints the absolute tolerance so that users see it.

### 6.6 Coverage roadmap

Classes are those of `research/real_qois/REPORT.md` (62 QoIs from 9 domains).

| Class of QoI | Examples | Milestone |
|---|---|---|
| Point-wise, one field | `x^2`, `log2(x)`, relative error as `ln(x)`, saturation vapour pressure | M1 |
| Thresholds, bins, ranges of interest | `x > c ? 1 : 0`, `floor(x/c)`, halo-finder walls | M1 (single), M4 (lists) |
| Exact fill values and masks | `exact = "x > 1e30"` | M3 |
| Several requirements at once | value within 0.05 and threshold kept | M4 |
| Regional means over blocks, rows, levels; weighted | block means, zonal means, area weights | M4 |
| Functions of aggregates | variance, covariance, Reynolds stress | M4 |
| Several fields at a point | velocity magnitude, kinetic energy, Mach number | M4 |
| Sliding windows, labelled regions | all 8^3 windows; land and ocean means | M5 |
| Stencils | vorticity, divergence, Q-criterion | M5 |
| Native and black-box QoIs | anything callable | M5 |
| Error metrics as targets | PSNR, SSIM | after version 1 |
| Spectra, order statistics, cross-file QoIs | P(k) per shell, quantiles | after version 1; spectra have a prototype |

---

## 7. Frontend: predicting the global error bound

The core takes one bound per array. A small bound leaves few outliers but compresses little; a large one does the
opposite. The frontend looks for the bound where the core's stream plus the corrections is smallest, subject to
the rules of section 7.4. The algorithm is the research's (`qpet/main:docs/tuning.md`,
`qpet/qpet-unified:docs/tuning.md`), restructured into replaceable steps.

### 7.1 Steps

```
predict(fields, request, core, options) -> Plan

F0 resolve     One parallel pass over the input: value ranges, QoI ranges, absolute tolerances,
               exact-value mask, count of non-finite values.
F1 prior       Read ~2^20 values at a regular stride over the whole input. For each, estimate the largest
               error it tolerates under every target; keep the smallest. Sort: this is the distribution
               that candidate bounds come from.
F2 shortcut    A bound was given            -> use it.
               No value tolerates any error -> tiny bound; everything will be restored.
               One candidate, strict core, point-wise targets only (a linear QoI) -> use it, no compression.
F3 sample      Copy blocks (or cells) out of the input, shaped by the targets and by the core's traits.
F4 measure     For a candidate bound: core round trip on every sample block, then the real backend, then the
               real codec. Cost = bytes per value. Decoded blocks are cached per bound.
F5 search      Evaluate the candidates, refine around the best, apply the rules of 7.4.
F6 plan        Return the bound per field, the reference bound, the correction mode, and the full trace.
```

### 7.2 The prior (F1)

For a point-wise target with `a = |f'(x)|`, `b = |f''(x)|`:

1. `e = 2 tau / (a + sqrt(a^2 + 2 b tau))`, the largest `e` with `a e + b e^2 / 2 <= tau`.
2. Not beyond the distance to the nearest singularity or breakpoint of `f`, the data bound, or the value range.
3. Evaluate `f` at `x - e` and `x + e`; halve `e` while either end is out of tolerance.
4. Where `f` or a derivative is not finite: `e = 0`, the value must be exact.

For a regional target over `n` values, `tau` is first multiplied by `max(1, 2 sqrt(n / (2 ln(2 / 0.001))))`, about
4.1 for a 4x4x4 block, because errors partly cancel in a mean. For several fields the prior is the scale of a box
of errors along the first-order split of the tolerance. For a function of aggregates it is a joint model of shift
and scatter. All of these are seeds for the search and are never trusted.

For a native target without derivatives the prior is found numerically on the same points: bisection on a
power-of-two ladder for the largest `e` with `|f(x +- e) - f(x)| <= tau`. The research measured this "exact" prior
at 1.64 times the estimate time with no gain in ratio, so it is a fallback, not the default. A target that
offers no prior at all switches the search to the prior-free strategy of section 7.4.

### 7.3 Sample and cost (F3, F4)

**Blocks**, the default. Each dimension is split into equal tiles of at most `block_edge`; tiles are picked with a
Halton sequence, one base per dimension, so every part of the input is equally likely. The count is
`sample_rate * input / block size`, clamped to `[min_blocks, max_blocks]`. On a small input the edge shrinks to
`min_edge`. If the sample would reach half the input, the whole input is used. Block origins and edges respect each
target's `sample_shape()`: multiples of the region edge, or a halo for stencils.

**Cells**, for a core that declares `independent_cell = c`. Thousands of single `c^d` cells are laid side by side
in one array. They compress to exactly the bytes they take in the whole input and cover it far more evenly.

**Cost of a candidate bound:**

```
cost(bound) = ( core_bytes - overhead + weight * correction_bytes ) / sample_values
```

- `weight` = share of values whose prior is below the bound in the whole input (from F1), divided by their share in
  the blocks. Sensitive values cluster, and blocks over- or under-represent them.
- If the blocks hold almost none of them while the input does (ratio above 20), the measured corrections say
  nothing. Then `rare_outlier_share * (share in the input) * (bytes of a value + 1)` per value is used instead.
- `overhead` is the size of an empty block, subtracted only for cores that declare a fixed cost per call.

The sample is corrected **by the backend's own code and priced by the real codec**. A cheaper correction therefore
moves the choice without any rule having to say so. This is why the backend must be callable on a block.

### 7.4 Search and rules (F5)

1. **Candidates**: the prior's distribution at quantiles `0, 0.0025, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1`,
   without zeros and duplicates.
2. **Refine**: for `refine_rounds` rounds, cost the geometric midpoints between the best and its neighbours; move
   to a cheaper midpoint or narrow the interval; stop when it is narrower than 5 %.
3. **Regional targets and several fields**: errors cancel by an amount only measurement can tell. If the best
   candidate is the largest, double the bound while the cost falls; if it is the smallest, halve it. A direction is
   given up after two steps that do not pay, because the cost is not always unimodal.
4. **Cores with a power-of-two bound** (trait): candidates are rounded down to powers of two, and the exponent is
   walked instead of step 2, up to 4096 times the largest candidate.

Rules that hold for every search:

| Rule | Statement | Why |
|---|---|---|
| **Never above the data bound** | no candidate exceeds a data bound in force | the data should be inside the bound because the core keeps them there, not because they were pushed back |
| Exception by trait | a core whose bound is a nominal tolerance (ZFP) may be given a tolerance above the data bound; the errors are still checked against it | ZFP's actual error is typically a quarter of its tolerance or less |
| **Near-ties go to the smaller bound** | among bounds whose cost is within 2.5 % of the cheapest, the smallest is taken | which of two near-equal candidates wins on a sample is noise, and the smaller bound leaves better data |
| **Bound-decided requests** | if the core at the data bound leaves nothing to correct on the sample, that bound is a candidate of its own and wins unless a smaller one is cheaper by more than 2.5 % | otherwise the near-tie rule doubles such streams |
| **Fidelity** (M3) | the reference bound `E` is tuned with lossless corrections; cheaper corrections and bounds up to `k E`, `k = 10^(d/20)`, may be chosen only while the sample PSNR stays within `d` dB of the reference; no value ends further than `min(k E, eps)` from its original. Default `d = 1` | cheap corrections otherwise push the bound to the data bound: smaller streams, 3 to 5 dB worse data in the median |

**Prior-free strategy.** For targets without a prior: an adaptive walk in log scale starting from the data bound,
with a budget of 8 measurements. The research measured 0.93 to 0.99 of the default's ratio. It also serves cores
whose knob is not an error bound ([section 8.3](#83-traits)).

### 7.5 What the frontend returns

```
Plan
  bound[field]            what the core will be given
  reference_bound         E, tuned with lossless corrections
  correction_mode         lossless | residual
  binding                 QOI | DATA_BOUND | TIE      which constraint decided the bound
  predicted               bytes per value: core, corrections; share of outliers; sample PSNR
  trace[]                 every candidate: bound, core bytes, correction bytes, weight, outlier shares
  flags                   whole_input_sampled, prior_free, rare_outlier_fallback, shortcut_taken
```

The trace is printed by `-v` and returned as JSON. In the research, reading it was the main way tuning problems
were found. The predicted compression ratio is reported as indicative only, because the absolute sample bytes are
biased.

### 7.6 Known weaknesses and planned experiments

These are measured weaknesses of the inherited design (`research/sample_prediction/REPORT.md`,
`qpet/main:docs/tuning.md`). None affects correctness. Each is an experiment with a pass criterion, not a
commitment.

| Weakness | Evidence | Experiment | Adopt if |
|---|---|---|---|
| Blocks cost a core more than the same data inside the whole array, by 1 to 6 times | SZ3's interpolation loses most on small blocks | none: do not use absolute bytes | n/a |
| Rare, clustered sensitive values are missed by every block | NYX 512^3: blocks saw no outlier, the input had 5e-5 | **E1. Outlier model from the prior.** Predict the outlier share on the whole input from the prior's distribution (spread evenly) and the core's error distribution measured on the blocks, instead of scaling the blocks' own count | regret on the 20-case benchmark improves and no case is more than 2 % worse |
| The correction term wants many small pieces; the core term wants large ones | split sample: SZ3 1.066 -> 1.023, SZo unchanged (1.063), SPERR worse (1.043 against 1.008) | **E2. Split sample per core trait** | better for every core, or made a trait |
| Few blocks are noisy, most of all in 2-D | 48 blocks instead of 12 halved the loss for 3 times the tuning time | **E3. Adaptive block count** until the argmin is stable | regret falls at under 1.3 times the tuning time |
| A second look after the whole-input compression | useful in 2 % of cases, overall regret 1.030 -> 1.025 | keep off for point-wise targets | n/a |
| Regions that span a whole dimension | the sample is the whole input; slabs are 20 to 70 times faster but unreliable | out of scope for version 1; warn | n/a |

One more idea from the research is cheap once several cores exist: **choosing the core from the sample**
(`core = auto`). On 20 cases the choice by sample cost was 1.037 of the best core per case, against 1.363 for
always SZ3 (`research/free_exploration/MEMO.md`, section 2.5). It is listed after version 1 because the sample-byte
bias differs per core and decided the wrong picks.

---

## 8. Core adapter layer: connecting compressors without touching them

### 8.1 Contract

A core is connected by an adapter that calls the compressor's **public API only**. The contract:

1. Take one bound for the whole array, for `float32` and/or `float64`, for some set of dimensionalities.
2. Leave the input unchanged. If the compressor overwrites its input (SZo does), the adapter copies.
3. Be callable from several threads at once on different buffers. The frontend compresses sample blocks in
   parallel.
4. Decompress deterministically (invariant I6).
5. Put everything the decoder needs beyond the stream, the type and the dimensions into an opaque
   `decode_params` blob. The container stores it. ZFP's accuracy mode needs its tolerance to decode.
6. Report failure by an error. Never print, never exit.

It does **not** need to honour its bound. The backend checks every value.

### 8.2 Interface

```cpp
// Proposed: include/qoiw/core.hpp
namespace qoiw {

struct CoreParams {
  double bound = 0;                 // the knob; an absolute error bound for most cores
  int    threads = 1;
  bool   allow_stream_changing_parallelism = false;   // e.g. SZ3's OpenMP mode: faster, lower ratio
  std::map<std::string, std::string> options;         // core-specific, e.g. {"algo","interp"}
};

class Core {
 public:
  virtual ~Core() = default;
  virtual std::string name() const = 0;               // "sz3"
  virtual std::string version() const = 0;            // stored in every stream
  virtual bool supports(DType, const std::vector<size_t>& dims) const = 0;
  virtual CoreTraits traits(DType, size_t ndims) const = 0;
  virtual FrontendHints frontend_hints(size_t ndims) const { return {}; }

  // dims in C order (slowest-varying first).
  virtual void compress(const void* data, DType, const std::vector<size_t>& dims, const CoreParams&,
                        std::vector<uint8_t>& stream, std::vector<uint8_t>& decode_params) const = 0;
  virtual void decompress(const uint8_t* stream, size_t size, const uint8_t* decode_params, size_t params_size,
                          DType, const std::vector<size_t>& dims, int threads, void* out) const = 0;
};

const Core& get_core(const std::string& name);         // throws if the build or plugins have no such core
std::vector<std::string> available_cores();
void register_core(std::unique_ptr<Core>);             // in-process registration
void load_core_plugin(const std::string& path);        // a shared library exporting qoiw_core_plugin()

}  // namespace qoiw
```

### 8.3 Traits

The research hard-coded what each compressor needs as per-backend tuning defaults. Here the adapter declares facts
about the compressor, and the frontend derives its behaviour from them. A new core then gets sensible sampling
without anyone touching the frontend.

| Trait | Values | What it changes | SZ3 | ZFP (accuracy) | SPERR |
|---|---|---|---|---|---|
| `bound_kind` | `StrictAbs`, `NominalAbs`, `Monotone` | whether the bound may exceed the data bound; whether the prior maps to the knob | StrictAbs | NominalAbs | StrictAbs |
| `pow2_bound` | bool | search walks exponents | no | yes | no |
| `independent_cell` | 0 or `c` | cell sample instead of blocks | 0 | 4 | 0 |
| `error_fill` | how much of the bound the errors use | `rare_outlier_share` default; second-look rules | most of it | a quarter or less | a small fraction for most values |
| `fixed_call_overhead` | bool | subtract an empty block from sample bytes | no | no | no |
| `mutates_input` | bool | the adapter copies | no | no | no (SZo: yes) |
| `thread_safe` | bool | parallel sample measurement, or serial | yes | yes | yes |
| `stream_depends_on_threads` | bool | forces one thread unless allowed | only in OpenMP mode | no | no |
| `accepts_nonfinite`, `max_magnitude` | bool, number | pre-masking of NaN, infinities and huge values | to be measured | to be measured | no; about 2e31 |
| `ndims`, `dtypes` | sets | request validation | 1 to 4; f32, f64 | 1 to 4 | 2 and 3 |
| `decode_needs_params` | bool | what goes into `decode_params` | no | yes | partly |

The ZFP and SPERR columns describe how the research adapters behaved. They are measured again by `core-check`
when those adapters are written (M2).

`Monotone` covers compressors whose knob is not an error bound: a rate, a precision, an RMSE or PSNR target. The
search of section 7.4 only needs a knob that orders quality, because it measures cost and the backend verifies. The
prior does not map to such a knob, so the prior-free strategy is used. This is designed for and deferred: version 1
ships `StrictAbs` and `NominalAbs`.

`FrontendHints` remain as an override for what traits cannot express. Every frontend parameter is taken from the
first place that sets it: the user, the core's hints, the default derived from traits and dimensionality.

### 8.4 Four ways to connect a core

| Mechanism | What the integrator writes | Compressor is | Cost | Use for |
|---|---|---|---|---|
| **In-tree adapter** | a `.cpp` and matching factory header in `src/core/adapters/`, with the source added to the CMake adapter list | linked in; source from a submodule, `find_package`, or fetched | none | the supported cores: SZ3 first, then ZFP, SPERR, SZo |
| **Plugin (C ABI)** | a shared library exporting one function that returns a table of function pointers | linked into the plugin; `qoiw` is not rebuilt | one `dlopen` | in-house or licensed compressors; cores written in other languages; Python prototypes through callbacks |
| **libpressio bridge** | nothing: `core = "pressio:<id>"` | whatever libpressio was built with | negligible | breadth: one adapter reaches every libpressio compressor that takes an absolute error bound |
| **Command-line bridge** | a small text template with the compress and decompress commands | an executable, even closed-source | process start and temporary files per call | compressors that only exist as a tool; slow, so the sample uses few blocks |

And without any of them: **advisor and guard mode** (section 5.2).

One translation unit per in-tree adapter is a requirement, not a style choice: compressor headers do not all
coexist. SZ3 and SZo both define `versionInt()` and `versionStr()` in the global namespace
(`qpet/main:docs/backends.md`).

The plugin ABI, proposed:

```c
/* include/qoiw/core_plugin.h */
typedef struct qoiw_core_plugin_v1 {
  uint32_t abi_version;                               /* 1 */
  const char *name;
  const char *(*version)(void);
  int  (*supports)(int dtype, size_t ndims, const size_t *dims);
  int  (*traits)(int dtype, size_t ndims, qoiw_core_traits *out);
  int  (*compress)(const void *data, int dtype, size_t ndims, const size_t *dims,
                   double bound, int threads, const char *options,
                   unsigned char **stream, size_t *stream_size,
                   unsigned char **decode_params, size_t *decode_params_size);
  int  (*decompress)(const unsigned char *stream, size_t stream_size,
                     const unsigned char *decode_params, size_t decode_params_size,
                     int dtype, size_t ndims, const size_t *dims, int threads, void *out);
  void (*release)(void *buffer);                      /* frees what compress handed out */
  const char *(*last_error)(void);
} qoiw_core_plugin_v1;

const qoiw_core_plugin_v1 *qoiw_core_plugin(void);    /* the one exported symbol */
```

Plugins are found through explicit `load_core_plugin(path)` or the directories in `QOIW_CORE_PATH`. A stream
records the core's name and version; decoding fails with a clear message if the core is not available.

### 8.5 The SZ3 adapter

- **Build**: `add_subdirectory(SZ3 EXCLUDE_FROM_ALL)` and link `SZ3::SZ3`. As a subproject SZ3 installs nothing and
  builds no tools. Details and pitfalls are in [Appendix C](#appendix-c-sz3-integration-notes).
- **Mode**: `EB_ABS` with `absErrorBound = bound`. The other modes are conversions that SZ3 does itself.
- **Options**: `algo`, one of `interp_lorenzo` (SZ3's default), `interp`, `lorenzo_reg`, `nopred`.
- **Threads**: `conf.openmp = false` unless `allow_stream_changing_parallelism`. SZ3's OpenMP mode splits the array
  and lowers the ratio. Our own passes are parallel regardless.
- **Buffers**: size the output with `SZ3::SZ_compress_size_bound<T>(conf)` and compress into it. The research
  adapter used the allocating overload and copied the result.
- **Decode**: `SZ_decompress<T>(conf, stream, size, out)` into our buffer. SZ3's stream is self-describing, so
  `decode_params` is empty.
- **Version**: `SZ3_VER` for the program, `SZ3_DATA_VER` for the stream format. Both go into the container.
- **Traits**: as in the table above. `accepts_nonfinite` is measured by the conformance tool, not assumed.

### 8.6 Conformance and calibration tool

`qoiw core-check <core>` is how an integrator learns whether an adapter is correct and what to declare. It is new
in this plan. The research found these properties one incident at a time.

| Check | Method | Result |
|---|---|---|
| Round trip | synthetic and small real arrays; every supported type and dimensionality; tiny extents; constant arrays | pass or fail |
| Input immutability | hash the input before and after | pass, or `mutates_input` |
| Thread safety | N threads compress different blocks; compare with serial | pass, or `thread_safe = false` |
| Stream determinism | same call twice; different thread counts | `stream_depends_on_threads` |
| Decode determinism | decode twice, and with different thread counts | pass required (I6) |
| Bound behaviour | largest error over the bound, across a ladder of bounds | `bound_kind`, `error_fill`, `pow2_bound` |
| Special values | NaN, infinities, `1e36`, denormals | `accepts_nonfinite`, `max_magnitude` |
| Size against bound | scan; report non-monotone stretches | informational; SZ3 is not monotone on some fields |
| Call overhead | size of an empty block | `fixed_call_overhead` |
| Sample bias | bytes of default sample blocks over the same data in the whole array | informational; documents the 1 to 6 times bias |

`--calibrate` prints a suggested traits and hints block. The same checks run in CI for every in-tree adapter.

### 8.7 Core roadmap

| Core | Mechanism | Milestone | What it proves |
|---|---|---|---|
| mock cores (identity, quantiser, noise, garbage) | in-tree, tests only | M0 | the guarantee does not depend on the core |
| SZ3 | in-tree, submodule | M1 | the pipeline |
| ZFP (fixed accuracy) | in-tree | M2 | nominal tolerance, power-of-two bounds, cell sampling, decode parameters |
| SPERR (point-wise error mode) | in-tree | M2 | restricted dimensionality, no non-finite values |
| SZo | in-tree | M2 | a core that mutates its input |
| any | plugin ABI, command-line bridge | M2 | no-rebuild integration |
| libpressio | bridge | M2, optional dependency | breadth |
| MGARD, TTHRESH, fixed-rate modes | as `Monotone` cores | after version 1 | knobs that are not error bounds |

---

## 9. Backend: verification and outlier correction

### 9.1 Flow

The backend receives the original, the core's decoded array and the resolved request. It edits the decoded array in
place into the final values and returns the correction layers.

```
correct(orig, dec, request) -> Corrections, Stats

A. Point pass, parallel over fixed chunks.
   Skip chunks that are bit-identical to the original.
   For every value that differs, in double precision:
     exact-value condition, non-finite QoI      -> must be lossless
     data bound, every point-wise target        -> if all hold, keep the value
   A value out of bounds is repaired by the cheapest correction that passes the same checks:
     quantised residual, coarsest level first   (9.2)
     else the original value, stored losslessly
   No other value may come to satisfy the exact-value condition; one that does is restored.

B. Regional passes, one target after another, on the values as they now are.
   For every region out of tolerance: put original values back, largest contribution to the error first,
   until the region is within tolerance.
   Repeat over the regional targets until a full sweep changes nothing.

C. Optional: a value put back in B becomes a residual if that moves the error of each of its regions by no
   more than its share of the region's remaining slack. Run B again. Undo if the layers are not smaller.

D. Encode the layers (9.3).
```

**Why it terminates.** A and C run a bounded number of trials and their results are not trusted. B only turns
values into exact ones, which cannot break a point-wise target or the data bound, and each sweep either restores
at least one value or ends. This is the argument the research used for lists of targets
(`research/reports/qpet_full_features.typ`, unified correction flow); it requires that the last-word step only
restores originals.

**Overlapping footprints** (windows, stencils). Restoring a value changes several outputs. B then runs in rounds
with a dirty set: only the outputs that touch a changed value are re-checked. If a round restores nothing while a
violation remains, one point per violating output is forced. The research saw 1 to 16 rounds for sliding windows.

### 9.2 Kinds of correction

| Kind | Stored per corrected value | Decoder computes | Cost (`float32`) | Milestone |
|---|---|---|---|---|
| Lossless value | index, original bits | `x' = x` | about 4.4 bytes | M1 |
| Quantised residual | index, level `L`, integer `q` | `x' <- T(double(x') + q * 2^(e0 - L))` | well under a byte plus the index for most values | M3 |
| Region step (reserved) | per block: offset or scale code | `x' + k 2^e` or `x' (1 + k 2^e)` over the block | a byte or two per block | not in version 1 |

Finding a residual: `2^e0` is about the median error of the outliers of the field. For `L = 0, 1, 2, ...` take `q`
as the rounded quotient, compute the value exactly as the decoder will, and keep the first level that passes every
check of pass A. Most values pass at level 0 or 1. Values under `exact`, values where a QoI is not finite, NaN and
infinities are never quantised.

Region steps gave 3.4 times smaller streams on regional requests at 17 dB lower PSNR. The research owner made them
opt-in for that reason (`research/docs/DECISIONS.md`, 10-11), and the default path does not need them.

### 9.3 Codec

| Layer | Content | Coding |
|---|---|---|
| Lossless | flat indices, increasing; original values | index gaps as unsigned LEB128, then zstd; raw values, then zstd |
| Residual | `e0`; index gaps; one level byte per value; `zigzag(q)` as LEB128 | each part through zstd |

The research priced the alternatives offline: positions as gaps plus zstd are within 10 to 20 % of the gap entropy,
a dense bitmap wins only above a correction density of 10 %, and replacing everything by the best option changes
the whole stream by under 1 % in the median. An arithmetic coder is not justified
(`research/residual_coding/README.md`). We keep the codec simple and put it behind an interface so that it can be
swapped.

### 9.4 Decode order and bit-exactness

Decode order: core decompression, then residuals, then lossless values written over the result. Reserved layers
slot in before the residuals. A lossless value is exact whatever the layers before it did.

Rules for the code on the decode path:

- Every correction is one IEEE-754 addition in double precision and one rounding to the storage type.
  `q * 2^(e0 - L)` is an exact product.
- The translation units that hold this arithmetic are compiled with `-ffp-contract=off`. `-ffast-math` is refused
  at configure time. SZ3 refuses it too.
- The encoder computes corrected values by calling the decoder's function, not a copy of its formula.
- The output does not depend on the thread count: chunks are a fixed partition of the index range, and per-chunk
  results are merged in index order.

### 9.5 Second look at the whole input

For regional targets and several fields, how much errors cancel was measured on blocks, and a core can behave
differently on the whole input. If the lossless corrections take more than `retreat_share` (0.3) of the stream, the
input is compressed again with half the bound, at most `retreat_rounds` (2) times, and the smallest stream is kept.
Each retreat costs one more compression, decompression and check. It does not run for point-wise targets of one
field, nor when the bound was given. It is rare: 6 of 1,140 runs in one research study
(`research/merge_trial/FINAL_qpet_unified.md`).

**Lossless fallback.** If the final stream is larger than the raw input, the container stores the zstd-compressed
raw input instead, with core `none`. A stream is then never much larger than the input, even with a broken core.

### 9.6 Guard and apply

`guard(orig, dec, request)` runs sections 9.1 to 9.3 on an array that some other tool decompressed and returns a
container with correction layers and no core section. `apply(corrections, dec)` is the decode path without a core.
A guard container also stores the fingerprint of the array `dec` that `guard` saw, so `apply` refuses a different
input.

---

## 10. Container format

Proposed. The final specification is written at M1 as `docs/format.md` and is frozen with golden streams.

Design rules:

- Self-describing: decoding needs only the stream and a core of the recorded name.
- Little-endian on every host, through explicit read and write helpers. The research format did not convert byte
  order.
- Sectioned, so that new correction layers do not need a new linear layout. The research format went through four
  versions as fields were appended.
- The stream is untrusted input: every length, count and index is checked against the bytes that remain before it
  is used to allocate, index or loop. Allocations are bounded by the input, not by a number the input declares.
- Checksummed header; fingerprint of the result.

```
stream   := magic header section* end
magic    := "QOIW"  u8 major  u8 minor  u16 flags
header   := u32 length  body  u32 crc32c
  body:
    dtype u8 | ndims u8 | nfields u8 | reserved u8 | dims u64[ndims]        C order
    wrapper_version str | core_name str | core_version str | core_format_version str
    per field:
      name str | bound f64 | data_bound_abs f64 | field_flags u8 | decode_params bytes
      fingerprint u64                         xxh3 of the final array of the field, as decoded
      input_fingerprint u64                   guard containers only: xxh3 of the array guard was given
    request record, informational only:
      ntargets u8, per target: kind u8 | region spec | tol_abs f64 | qoi text str
      exact condition str
section  := u8 type | u8 field | u16 flags | u64 length | payload
  0x01 CORE        the core's stream, byte for byte as the core produced it
  0x10 RESIDUAL    quantised residuals
  0x11 LOSSLESS    exact values
  0x12 STEPS       reserved: region steps
  0x20 TRANSFORM   reserved: change of variables
  0x7E NOTE        informational, skippable
  0xFF END
str      := u16 length, bytes        bytes := u32 length, bytes
```

- A reader refuses an unknown section type unless its `skippable` flag is set. Correction layers are never
  skippable: ignoring one would silently void the guarantee.
- The request record is provenance. Labels, weights and native QoIs are not stored; a native QoI stores the
  description string its author gave it.
- **Fingerprint.** Written at compression from the array the backend verified. Checked at decompression by
  default; one hash pass, a few per cent of the decode time. A mismatch means the core did not reproduce what was
  verified (invariant I6), and the decoder fails instead of returning unverified data.
- **Compatibility.** A reader of major version N reads every minor version of N. The core's stream is whatever
  the core produced; whether a later core version reads it is the core's policy. `core_version` and
  `core_format_version` record what wrote it.

---

## 11. Public API

All sketches are proposals. Names are placeholders pending decision D1.

### 11.1 C++

```cpp
#include <qoiw/qoiw.hpp>

std::vector<float> u = read("Uf48.bin.f32");                 // 100 x 500 x 500

qoiw::Request req;
req.targets.push_back(qoiw::Target::pointwise("x^2").rel(1e-3));
req.targets.push_back(qoiw::Target::blocks("mean(x^2) - mean(x)^2", {4, 4, 4}).abs(1e-4));
req.data_bound = qoiw::Tol::rel(1e-2);                       // the default; Tol::none() switches it off
req.exact = "x > 1e30";

qoiw::Options opt;
opt.core = "sz3";
opt.core_options["algo"] = "interp";
opt.threads = 16;

qoiw::Report report;
std::vector<uint8_t> blob = qoiw::compress(qoiw::ArrayView(u.data(), {100, 500, 500}), req, opt, &report);

qoiw::StreamInfo info = qoiw::inspect(blob);
std::vector<float> back(info.num);
qoiw::decompress(blob, back.data());
```

The stages on their own:

```cpp
qoiw::Plan plan = qoiw::predict(view, req, opt);             // frontend only
// ... the caller compresses with any tool at plan.bound[0] and obtains `dec`
std::vector<uint8_t> fix = qoiw::guard(view, dec_view, req, opt);     // backend only
qoiw::apply(fix, dec_mutable_view);                          // on the reading side
```

Functions and types:

| Function | Purpose |
|---|---|
| `compress(fields, request, options, report*)` | integrated compression; one or several fields |
| `decompress(stream, out, threads)` | needs no request |
| `inspect(stream)` | header: shape, type, core, bounds, targets, layer sizes |
| `verify(orig, dec, request, threads)` | independent check of two arrays against a request |
| `predict(fields, request, options)` | the plan, without compressing the whole input |
| `guard(orig, dec, request, options, report*)`, `apply(corrections, dec)` | backend alone |
| `get_core`, `available_cores`, `register_core`, `load_core_plugin` | core registry |

| Type | Content |
|---|---|
| `ArrayView` | pointer, `DType`, dimensions in C order; non-owning |
| `Tol` | `abs(v)`, `rel(v)`, `none()` |
| `Target` | QoI (text or native), region, tolerance |
| `Request` | section 6.1 |
| `Options` | core, core options, threads, `FrontendOptions`, `BackendOptions`, `fixed_bound`, log sink |
| `Plan` | section 7.5 |
| `Report` | the plan; resolved tolerances; sizes per section; outliers per kind; largest errors per target; times per stage |
| `Error` | exception with a code: invalid argument, unsupported by core, corrupt stream, core failure, internal |

Conventions: dimensions are in C order, slowest-varying first, everywhere in the library. The library never prints
and never exits; diagnostics go to a caller-supplied log sink. C++17, so no `std::span`; a minimal `Span<T>` is
provided.

### 11.2 C

The C API is the stable ABI and the base of the Python package. Options are `name=value` text, as in the research
code, because it survives every binding. Reports are JSON text.

```c
#include <qoiw/qoiw_c.h>

qoiw_request *r = qoiw_request_new();
qoiw_request_add_target(r, "x^2", /*region*/ NULL, "rel", 1e-3);
qoiw_request_set(r, "data_bound", "rel:1e-2");
qoiw_request_set(r, "exact", "x > 1e30");

qoiw_array field = { data, QOIW_F32, 3, dims, "x" };
unsigned char *blob; size_t size; char *report_json;
if (qoiw_compress(&field, 1, r, "core=sz3\nthreads=16\n", &blob, &size, &report_json))
    fprintf(stderr, "%s\n", qoiw_last_error());

qoiw_decompress(blob, size, out, out_bytes, /*threads*/ 0);
qoiw_free(report_json); qoiw_free(blob); qoiw_request_free(r);
```

Also: `qoiw_inspect`, `qoiw_verify`, `qoiw_predict`, `qoiw_guard`, `qoiw_apply`, `qoiw_cores`,
`qoiw_register_core` (takes a `qoiw_core_plugin_v1`), `qoiw_last_error` (thread-local), `qoiw_free`. Every function
returns 0 on success.

### 11.3 Python

A thin layer over the C API on NumPy arrays (decision D5: `ctypes` over the C library, as the research package
does, which keeps Python out of the build).

```python
import numpy as np, qoiw

u = np.fromfile("Uf48.bin.f32", dtype=np.float32).reshape(100, 500, 500)

blob, report = qoiw.compress(
    u,
    targets=[qoiw.Target("x^2", rel=1e-3)],
    data_bound=qoiw.rel(1e-2),           # the default; qoiw.none() switches it off
    exact="x > 1e30",
    core="sz3",
    return_report=True,
)
back = qoiw.decompress(blob)             # shape and dtype of the original
qoiw.inspect(blob)

plan = qoiw.predict(u, targets=[qoiw.Target("x^2", rel=1e-3)], core="sz3")
fix  = qoiw.guard(u, dec_from_elsewhere, targets=[...])
qoiw.apply(fix, dec_from_elsewhere)      # in place

# several fields
blob = qoiw.compress({"u": u, "v": v, "w": w}, targets=[qoiw.Target("sqrt(u^2+v^2+w^2)", rel=1e-3)])
```

### 11.4 Command line

Subcommands with long options. The short flags of the SZ3 and QPET tools are kept as aliases, because that is what
users of these compressors type from memory.

```
qoiw compress   -c sz3 -f --shape 100,500,500 -i U.f32 -q "x^2" -m REL 1e-3 [-M REL 1e-2 | -M NONE]
                [-X "x > 1e30"] [-z U.qoiw] [-a] [-v] [--json]
qoiw decompress -z U.qoiw -o U.out.f32
qoiw inspect    -z U.qoiw
qoiw verify     -f --shape 100,500,500 -i U.f32 -o U.out.f32 -q "x^2" -m REL 1e-3
qoiw predict    (the request flags of compress)            prints the plan and the trace
qoiw guard      (the request flags) -i U.f32 --dec U.other.f32 -g U.fix
qoiw apply      --dec U.other.f32 -g U.fix -o U.final.f32
qoiw cores                                                 lists cores, versions and traits
qoiw core-check <core> [--calibrate]
```

- `--shape` is C order. `-3 500 500 100` is accepted as well and is **fastest-varying first**, the convention of
  the SZ3 tool. The two are never mixed in one call. This is the most common user error with these tools.
- Each `-q` starts a target; the `-m`, `-r` (blocks), `-W` (windows), `-L` (labels), `-G` (weights) that follow
  belong to it.
- `-a` decompresses and verifies with code independent of the compression path. `-v` prints the trace.
- Exit code 0 only if everything asked for holds.

---

## 12. Repository layout and build

### 12.1 Layout

```
QoI/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── cmake/
│   ├── QoiwCores.cmake             core dependencies and built-in registry generation
│   ├── QoiwOptions.cmake           warnings, sanitizers, floating-point flags
│   └── qoiwConfig.cmake.in
├── SZ3/                            git submodule, unmodified
├── include/qoiw/
│   ├── qoiw.hpp                    compress, decompress, inspect, verify
│   ├── stages.hpp                  predict, guard, apply
│   ├── types.hpp                   DType, ArrayView, Span, Tol, Error
│   ├── request.hpp                 Target, Region, Request
│   ├── options.hpp                 Options, FrontendOptions, BackendOptions
│   ├── report.hpp                  Plan, Report, StreamInfo
│   ├── expr.hpp                    expression: parse, evaluate, differentiate
│   ├── target_ops.hpp              the interface of section 6.3; base for native QoIs
│   ├── core.hpp                    Core, CoreTraits, FrontendHints, registry
│   ├── core_plugin.h               C ABI for core plugins
│   └── qoiw_c.h                    C API
├── src/
│   ├── common/                     little-endian I/O, LEB128, zigzag, zstd, xxh3, crc32c, parallel_for
│   ├── expr/                       lexer, parser, bytecode, batch evaluation, jets, analysis
│   ├── qoi/                        target compilation, ranges, exact mask, regions, aggregates
│   ├── frontend/                   resolve, prior, sampler, measure, search, fidelity, plan
│   ├── core/                       registry, plugin loader, traits
│   │   └── adapters/               sz3.cpp, zfp.cpp, sperr.cpp, szo.cpp, pressio.cpp, exec.cpp, mock.cpp
│   ├── backend/                    verify, point_correct, region_correct, codecs, apply
│   ├── container/                  reader, writer, section definitions
│   ├── pipeline/                   compress (with the second look), decompress, inspect
│   └── capi/
├── tools/
│   ├── qoiw/                       command-line tool
│   └── core_check/                 conformance and calibration
├── python/                         package and pyproject.toml
├── tests/
│   ├── unit/                       one directory per src/ module
│   ├── property/                   the guarantee matrix of section 13.2
│   ├── golden/                     stored streams per format version
│   ├── fuzz/                       decoder fuzz targets
│   ├── cores/                      conformance runs per adapter
│   └── reference/                  qoi_numpy.py, the independent checker
├── bench/
│   ├── cases/                      case lists: regret, parity, end to end
│   ├── regret/                     oracle scan and scoring
│   └── harness/                    resumable runner, table builder
├── docs/                           concepts, QoI language, adding a core, format, tuning, API
└── examples/                       C++, C, Python
```

`research/` and `data` stay untracked, as `.gitignore` already has them.

### 12.2 Build targets

| Target | Kind | Contents |
|---|---|---|
| `qoiw_decode` | static | common, container reader, core registry, apply |
| `qoiw` | static | everything; alias `QOIW::qoiw` |
| `qoiw_c` | shared | the C API with the cores linked in; what Python loads |
| `qoiw_cli` | executable `qoiw` | |
| `qoiw_core_check` | executable | |

### 12.3 Requirements and options

- CMake 3.19 or newer and C++17: SZ3's own minimums.
- zstd: the system library, or fetched at a pinned tag with `QOIW_FETCH_DEPS=ON`. SZ3's bundled zstd is a private
  target with hidden symbols and cannot be shared.
- OpenMP for our own parallel passes, behind a `parallel_for` helper so that a serial build works.
- Linux on x86-64 and aarch64 first; macOS next. Windows is not a version 1 target, but only the plugin loader and
  the command-line bridge may use platform APIs.

| Option | Default | Effect |
|---|---|---|
| `QOIW_CORE_PRESSIO` | OFF | libpressio bridge; needs libpressio installed |
| `QOIW_CORE_EXEC`, `QOIW_CORE_PLUGINS` | ON | command-line bridge; plugin loader |
| `QOIW_BUILD_TOOLS`, `QOIW_BUILD_TESTS`, `QOIW_BUILD_C_API`, `QOIW_BUILD_PYTHON` | ON | |
| `QOIW_SANITIZE` | none | address, undefined, thread |

Built-in adapters are listed in `QOIW_CORE_SOURCES` and built by default.
SZ3 comes from the pinned `./SZ3` submodule.

```bash
git submodule update --init SZ3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
build/qoiw compress -c sz3 -f --shape 100,500,500 -i data/Hurricane-Dataset-for-Compressors/Uf48.bin.f32 \
      -q "x^2" -m REL 1e-3 -z /tmp/U.qoiw -a -v
```

### 12.4 Code conventions

- One compressor per translation unit. Compressor headers are included nowhere else.
- No global mutable state. Everything is re-entrant.
- Options have an "unset" state and resolve in the order user, core hints, defaults.
- Tunable constants live in one table (Appendix A) with the measurement that set them. The research ended with more
  than 40 tuning parameters, several fitted on small samples; a new constant needs a benchmark result.
- Warnings as errors in CI. Formatting by `.clang-format`; SZ3's file is a reasonable starting point.

---

## 13. Testing and verification

The research's rule was that every result is recomputed by an independent checker, never taken from the tool's own
check. That rule found defects in baselines, in compressors and in the checker itself. We keep it.

### 13.1 Layers of tests

| Layer | What | Runs |
|---|---|---|
| Unit | expression parsing, evaluation and derivatives against finite differences; LEB128; codecs; sampler geometry; search on synthetic cost curves; container round trip | every commit |
| Guarantee matrix | section 13.2 | every commit (small), nightly (full) |
| Independent checker | `tests/reference/qoi_numpy.py` recomputes every QoI error and data error from the decompressed file | with the guarantee matrix, through Python |
| Golden streams | stored streams decode to stored bytes, for every format version | every commit |
| Cross-build decode | a build with FMA (`-march=x86-64-v3`) and one without decode each other's streams to the same bits; aarch64 when CI has it | nightly |
| Thread independence | identical streams and outputs with 1, 2, 7 and 16 threads | every commit |
| Decoder fuzzing | libFuzzer on `decompress`, `inspect`, `apply` under address and undefined-behaviour sanitizers | nightly, plus a stored corpus |
| Core conformance | `core-check` for every adapter in the build | every commit |
| Mutation checks | hand-made mutants of the backend (a `<=` turned into `<`, a skipped check, a wrong rounding) must fail a test | before each release |
| Regression list | section 13.3 | every commit |

### 13.2 The guarantee matrix

The cross product, sampled, of:

- **Data**: smooth, noisy, sparse with long runs of zeros, heavy-tailed, constant, with NaN and infinities, with
  `1e36` fill values, with denormals; shapes down to an extent of 1; `float32` and `float64`; 1-D to 4-D.
- **QoIs**: about 30 expressions covering smooth, singular (`ln`, `1/x`, `sqrt`), oscillating (`sin(10x)`),
  piecewise, steps, thresholds at non-representable constants; later regional and multi-field ones.
- **Tolerances and data bounds**: loose to tight, absolute and relative, data bound on and off.
- **Cores**: SZ3 and the **mock cores**. The mocks are the point. `mock:quantizer` is a perfectly predictable core
  for exact expected values. `mock:noise(k)` exceeds its bound by a factor `k`. `mock:garbage` returns arbitrary
  values. `mock:signflip` flips signs near zero. `mock:nan` injects NaN.

For every cell: compress, decompress, check with the independent checker, and assert that every bound holds and
that the decoded array equals, bit for bit, the array the backend verified. With a garbage core the stream is
large and the guarantee still holds. A test that passes only with well-behaved cores does not test the guarantee.

### 13.3 Regression list from the research

Each row was a real failure in some system (`research/paper_eval_full/analysis/violations.md`,
`research/paper_eval_full/progress.md`).

| Case | What went wrong | Test |
|---|---|---|
| Output rounded to `float32` after the check | tolerance exceeded by up to 1.14 times on tight tolerances | check runs on values after rounding to the storage type |
| "Close enough" accepted | excess of 1 + 4e-5 passed a tool's own check | comparisons are exact; the checker's slack is 1e-9 relative |
| Regional bound from a confidence level, not checked | block mean exceeded by 1.21 times | every region is checked exactly |
| A compressor path breaks its bound at a tiny absolute bound | data error 2.0 times the bound in the paper artifact's SZ3 path | `mock:noise`; data bound verified |
| Sign flip near zero | `ln` not finite on the decoded value | `mock:signflip` with `ln(x)` |
| Exact zeros under `ln` or `log2` | QoI not finite on the original | zeros come back exact |
| Fill values `1e36` | QoI error 1.7e5 times the tolerance; another scheme destroyed them | `exact` with the true `float32` value |
| Huge magnitudes on SPERR | core refuses the data | pre-masking by trait |
| Flat blocks under a variance | cancellation; an offset step moved values by 1e13 while the check passed on rounding | centred `var`; rounding margin in aggregate checks |
| Thresholds on `float32` | 13,000 to 18,000 values on the wrong side for a single-precision reader (with the change of variables) | the rule of section 6.5 |
| The checker itself | numexpr rewrote `x/c` as `x * (1/c)` and step QoIs flipped at bin edges | the checker uses plain NumPy for division and C-style `round` |

### 13.4 Parity gates

The fastest way to know the re-implementation is right is to compare it with the code that produced the
measurements. The public release builds from the research repository with only SZ3:

```bash
git -C research worktree add ../../qpet-main origin/qpet/main      # outside this repository
cmake -S ../qpet-main -B ../qpet-main/build -DQPET_SZ3_SOURCE_DIR=$PWD/SZ3 \
      -DQPET_WITH_SZO=OFF -DQPET_WITH_ZFP=OFF -DQPET_WITH_SPERR=OFF
```

Both sides then use the same SZ3 commit. Gates are in section 16.

Checked on this machine on 2026-11-21, from an exported copy of the branch in `/tmp` with the same `cmake`
arguments and the submodule at `28387be`: it configures and builds, and its three tests pass. One case as an
anchor for M1:

```
qpet -b sz3 -f -3 500 500 100 -i Uf48.bin.f32 -q "x^2" -m REL 1e-3 -a -t 16
```

| Quantity | Reference result |
|---|---|
| bound chosen | 0.0863667, after 18 candidates |
| outliers | 334,063 values (1.34 %) |
| stream | 4,586,625 bytes: 3,230,946 from SZ3, 1,355,590 of outliers |
| compression ratio, PSNR | 21.80, 67.09 dB |
| largest QoI error over tolerance | 0.999999 |
| compression time | 0.666 s: prior 0.071, search 0.141, SZ3 compress 0.274, SZ3 decompress 0.166, check 0.008 |
| decompression time | 0.139 s |

The ratio, bound and PSNR agree with what the research documents for this case
(`research/reports/qpet_full_features.typ`: bound 0.086, 1.47 bits per value, 67.1 dB). Times are one run on a
shared machine. The public release has no default data bound, so parity runs give both sides the same explicit
one.

---

## 14. Benchmarking and evaluation

### 14.1 Regret benchmark

The measure of the frontend. For each case the **oracle** compresses the whole input at about 21 log-spaced bounds
between the smallest prior and the cap, refines three rounds around the best, and keeps the smallest stream
(power-of-two cores: every exponent). **Regret** is the tuned stream size over the oracle's; 1.00 is perfect.
Report the geometric mean, the worst case and the count above 1.05 per core.

The research's 20 cases and scores (`research/tuning/`, `research/evaluation_notes.md`):

| Core | Geometric mean | Worst | Cases above 1.05 |
|---|---|---|---|
| SZ3 | 1.068 | 2.13 | 4 |
| SZo | 1.039 | 1.24 | 6 |
| ZFP | 1.004 | 1.06 | 1 |
| SPERR | 1.033 | 1.19 | 5 |

### 14.2 End-to-end evaluation

Rules, from `research/docs/DECISIONS.md` principle 7 and the final study:

- Vary the data bound in every evaluation: 1e-2, 1e-3, 1e-4 of the value range.
- Give every system the same absolute tolerance, computed by the harness.
- Report PSNR and the largest point-wise error next to every compression ratio.
- Say per case which constraint binds. If the plain core at the data bound already meets the QoI, the data bound
  binds; such cases are marked and left out of ratio comparisons.
- Baselines: the plain core at the data bound; a search for the best uniform bound (root finding on
  `max(QoI error / tolerance, data error / data bound)`, budget 16 compressions); the same search on a sample; the
  research QPET builds.
- Record per row: sizes, ratio, PSNR, largest error, QoI error over tolerance, which bounds hold, bound chosen,
  outliers, correction bytes, time per stage, threads, machine load, the exact command.

### 14.3 Data

| Dataset | On this machine (`data/`) | Shape, type | Notes |
|---|---|---|---|
| Hurricane ISABEL, step 48 | yes: 13 fields, and log10 copies of 7 of them | 100x500x500 `float32` | the default development set |
| S3D | yes, 5 time steps | 11x500x500x500 `float64` | one file holds 11 variables; split per variable first |
| Miranda | yes, large version | 3072x3072x3072 `float32` in slabs of 32 planes | the research used the small 256x384x384 `float64` version; download it for parity |
| NWChem, HACC, EXAALT | yes | 1-D | 1-D coverage; HACC is particle data |
| NYX, SCALE, CESM, JHTDB, EXAFEL, Basilisk | no | see `research/docs/SETUP.md` | `research/datasets/download_sdrbench.sh` |

Shapes above were read from file sizes and dataset descriptions and must be confirmed before use. Data paths come
from an environment variable (`QOIW_DATA_DIR`), never from the scripts.

**Hard cases** worth having from the first day, because they are where the research found the frontend or the
guarantee under stress:

| Case | Stresses |
|---|---|
| CESM PRECSC `x^3`; other sparse precipitation fields | rare clustered sensitive values; the worst regret on every core |
| CESM TROP_Z `x^2` | smooth blocks hide outliers; 0.06 % seen in the sample, 3.2 % in the input |
| Miranda velocity `x^3` | among the worst regrets; five blocks ranked two alternatives wrongly where 24 blocks did not |
| NYX baryon density `x^2`, `x^3`, thresholds at 88.16 | five orders of magnitude; `float32` thresholds |
| Hurricane QVAPOR `ln(x)` | exact zeros |
| CESM PRECT `ln(x)` | sign flips near zero |
| SCALE T `x^2` at tolerance 1e-6 | errors of a few `float32` steps |
| CESM AEROD_v, ODV_dust1 | `1e36` fill values |
| Basilisk 2-D turbulence | sample bytes 3.7 to 6.1 times the whole input's; non-monotone size |

### 14.4 Harness

Lessons from running 148,411 rows (`research/paper_eval_full/RUNBOOK.md`):

- One ordered job list and one entry point. Every row has a unique key; keys already present are skipped, so a run
  resumes.
- Rows are append-only JSON lines per worker, merged into one table. Each row links to the exact command and the
  tail of the tool output.
- Every row names the build commit. Builds are frozen copies per commit.
- Decompressed data lives in `/dev/shm` and is deleted after each check.
- Cap threads per job and jobs per machine by the load average.
- **Times measured on a loaded shared machine are indicative only.** The research never got its quiet timing pass.
  Plan one: fixed thread counts (1 and 16), three repetitions, nothing else running.

---

## 15. Performance and memory budget

### 15.1 Time

Reference points from the research at 16 threads on a loaded machine (`research/HANDOFF.md`, result 7):
compression at a median 3.4 times one plain compression, of which tuning is 13 to 20 % and check plus correction
2 to 6 %; decompression about the plain core's. The one case measured here (section 13.4) took 2.4 times the SZ3
compression inside it, with 32 % of the time in the frontend.

| Budget | Target |
|---|---|
| Compression, point-wise targets, SZ3 | at most 3.5 times one plain SZ3 compression (median) |
| Share of the frontend (prior plus search) | at most 35 % of compression time (median) |
| Decompression | at most 1.1 times the plain core, fingerprint check included |
| Advisor mode (`predict`) | at most 1 time one plain compression |

Where the time goes, and what to do about it:

- The floor is one compression plus one decompression of the whole input, since the backend must see what the
  decoder will see. That alone is about 2 times a plain compression.
- Passes over the input are fused. F0 computes all ranges in one pass; backend pass A checks the data bound and
  every point-wise target in one pass, skipping bit-identical chunks by `memcmp`.
- Expressions are evaluated in batches of a few thousand values, not through a call per value.
- Sample blocks are compressed in parallel, one thread each. Candidates share decoded blocks through a cache.

### 15.2 Memory

Peak during compression of one field of `N` values of `s` bytes with SZ3. These are estimates from reading the
SZ3 headers, to be measured at M1:

| Buffer | Size | Owner |
|---|---|---|
| input | `N s` | caller |
| SZ3's internal copy (interpolation algorithms copy the input) | `N s` | SZ3 |
| SZ3's working arrays (quantisation codes) | about `4 N` bytes | SZ3 |
| output buffer sized by `SZ_compress_size_bound` | about `N s` | adapter |
| decoded copy for verification | `N s` | pipeline |
| pre-masked copy, only if `exact` is used | `N s` | pipeline |

So about 4 to 5 times the input at the peak. The pipeline frees the oversized output buffer and SZ3's working
memory before decoding, which brings the verification phase to about 2 to 3 times. Data that does not fit is a
milestone M7 item: tiles compressed independently under one shared plan. Small tiles are costly at high ratios
(64^3 tiles lost up to 4 times in `research/free_exploration/MEMO.md`), so tiles must be large.

---

## 16. Milestones

Effort figures are rough, in engineer-weeks, and should be re-estimated after M1. They add up to about 28 to 30.
M2 and M3 are independent of each other and can run in parallel after M1. M6 can start after M1 and follow the
others.

| | Milestone | Delivers | Effort |
|---|---|---|---|
| M0 | Foundations | build, CI, layout, mock cores, expression engine, container skeleton | 1 to 2 |
| M1 | Point-wise QoIs on SZ3 | the whole pipeline end to end; parity with the public QPET release | 3 to 4 |
| M2 | Core generality | traits, registry, plugin ABI, `core-check`, ZFP, SPERR, SZo, bridges, advisor and guard modes | 4 |
| M3 | Backend 2 and policy | residuals, default data bound, fidelity, exact values, second look, fingerprint | 3 |
| M4 | QoI generality 1 | target lists, regional targets on blocks, functions of aggregates, several fields | 5 |
| M5 | QoI generality 2 | windows, labels, native targets, stencil operators | 5 |
| M6 | Bindings | C API freeze, Python package, documentation, examples | 3 |
| M7 | Scale and release | tiled mode, performance work, full evaluation, version 1.0 | 4 |

### 16.1 Milestone detail and exit criteria

**M0. Foundations.**
- CMake project with the SZ3 submodule as a subproject; CI with warnings as errors and sanitizers.
- `common/`, the container reader and writer, mock cores.
- Expression engine: parser, bytecode, batch evaluation, second-order forward-mode differentiation, detection of
  linearity, singularities and breakpoints.
- The benchmark data of section 14.3 that is not on this machine, and the reference build of section 13.4.
- *Exit*: unit tests pass; an empty request round-trips through the container with a mock core.

**M1. Point-wise QoIs on SZ3.**
- Frontend steps F0 to F6 with lossless corrections. Backend pass A with lossless values. SZ3 adapter. `compress`,
  `decompress`, `inspect`, `verify`, the CLI.
- The independent NumPy checker; the guarantee matrix; the regret benchmark.
- *Exit*:
  1. zero violations on the guarantee matrix, mock cores included;
  2. parity with `qpet/main` on its 20 regret cases at identical settings: stream sizes within 3 % in the geometric
     mean and 10 % in every case;
  3. regret against the oracle at most 1.07 (geometric mean) for SZ3;
  4. the time budgets of section 15.1.

**M2. Core generality.**
- `CoreTraits` driving the sampler and the search: cells, power-of-two walk, nominal tolerance.
- Registry, plugin ABI with an example plugin, command-line bridge, libpressio bridge.
- Adapters for ZFP, SPERR and SZo. `core-check` with calibration.
- `predict`, `guard`, `apply`.
- *Exit*: zero violations with every core; `core-check` passes for every adapter; regret within 0.02 of the
  research's score per core (section 14.1); a compressor wrapped through the plugin ABI and through the bridge
  without rebuilding `qoiw`; a guard-mode round trip verified by the checker.

**M3. Backend 2 and policy.**
- Quantised residuals; pass C. Default data bound. The rules of section 7.4, fidelity included. `exact` with
  pre-masking. The `float32` threshold rule. Second look. Lossless fallback. Fingerprint.
- *Exit*: zero violations; on the 20 regret cases, residuals without the fidelity limit give streams about 1.3
  times smaller than lossless values alone (the research measured 1.36, `research/correction_layer/REPORT.md`), and
  a result below 1.2 needs an explanation; with fidelity 1 dB, no case loses more than 2 dB of PSNR against the
  reference; cross-build decode is bit-exact.

**M4. QoI generality 1.**
- Target lists. `TargetOps` for regions on aligned blocks, with per-dimension shapes and weights. Functions of
  aggregates with the joint prior and centred `var`. Several fields with one bound per field.
- *Exit*: zero violations; parity with `qpet/qpet-unified` (transform off, corrections restricted to residuals) on a
  fixed list of regional, aggregate and multi-field cases, within 5 % in the geometric mean.

**M5. QoI generality 2.**
- Sliding windows and labels. The native target interface in C++ and through the C API. Stencil operators (`dx`,
  `dy`, `dz`, `lap` and macros) on the footprint interface, with halo-aware sampling.
- *Exit*: zero violations; stencil results within 10 % of the prototype's ratios in
  `research/stencil_qoi/REPORT.md`; a Python callable used as a target end to end.

**M6. Bindings.** C API frozen and versioned; Python package with wheels; documentation; examples.
*Exit*: every feature reachable from C, Python and the CLI; the documentation examples run in CI.

**M7. Scale and release.** Tiled mode with a shared plan; `known_ranges` for distributed use; profiling against the
budgets; the full evaluation of section 14.2; version 1.0. *Exit*: the evaluation report; zero violations over the
whole matrix.

### 16.2 What to port and what to write

| Component | Recommendation | Reason |
|---|---|---|
| Expression parser and automatic differentiation | port from `qpet/main` (`src/expr.cpp`, about 30 kB, public, BSD-3) with its tests | small, self-contained, well tested |
| Prior (`pointwise_eb`), Halton block sampler, cost and search | port the algorithm, restructure into the steps of section 7 | this is what parity is measured against |
| Outlier codec | port, behind the codec interface | trivial and proven |
| Pipeline, container, adapters, API | write new | the research code is a monolith with a linear format and four hard-wired adapters |
| Regional, aggregate, multi-field code | re-implement on `TargetOps`, with `qpet/qpet-unified` as the reference | it lives in `.ipp` files tied to the monolith; and that branch is not public (decision D2) |

### 16.3 Deliberately left out of version 1

| Feature | Why not now | Where it would go |
|---|---|---|
| Change of variables `G(x)` | 1.01 at the default fidelity; 0.87 when forced; costs PSNR | a conditioner between frontend and core; section `0x20` |
| Region steps | 3.4 times smaller, 17 dB worse; narrow applicability | backend layer `0x12` |
| PSNR and SSIM targets, coded layers | block SSIM without a data bound is an open problem in the research (0.52 of a search) | `dec()` and `err()` in the language; a predictor in the frontend |
| Spectral targets | need a data bound beside them; low-k is not predictable from samples | a `Global` target with its own correction layer |
| `core = auto` | sample-byte bias differs per core | frontend, after M2 |
| Tiles with different bounds | 1.1 to 2.7 times where tolerance varies in space, up to 4 times worse at high ratios | after M7, decided on a sample |

---

## 17. Decisions needed from reviewers

| | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Name, namespace, stream magic, file extension | `qoiw` as used here; a QPET-lineage name; other | Decide before the first tag. Avoid `qoi` and `.qoi`: they are the name and extension of the "Quite OK Image" format, and the Python package name is likely taken |
| D2 | Licence, and which research code may be ported | BSD-3 to match SZ3 and public QPET; port from `qpet/main` only, or also from the private `qpet/qpet-unified` | BSD-3; port from `qpet/main`; ask the QPET owner before copying from the private branch, otherwise re-implement from its documentation |
| D3 | Data bound on by default at 1 % of the range | on (research principle 3); off | On. It changes results for anyone expecting "QoI only"; the report states the bound in force and that it is the default |
| D4 | Fidelity limit, default 1 dB | keep as in the research; drop it and always take the smallest stream | Keep. Without it cheap corrections push every bound to the data bound |
| D5 | Python binding | `ctypes` over the C library; pybind11 | `ctypes`: no Python at build time, and it forces the C API to be complete |
| D6 | Scope of "freely accepts QoI requirements" for version 1.0 | expression language and lists only; plus native targets and stencils (M5) | Include M5: the footprint interface is the only general answer to "any QoI" |
| D7 | Which cores after SZ3, and whether to take the optional libpressio dependency | ZFP, SPERR, SZo; MGARD; libpressio bridge | ZFP first, since it exercises every trait; libpressio as an optional bridge |
| D8 | SZ3 pin | the submodule's current commit `28387be`, 5 commits after `v3.4.0` and titled "3.4.1" but not tagged; or a release tag | Keep `28387be` for now, and move to the next release tag when it exists. SZ3's own guide says to depend on a release tag, never on a `master` commit |
| D9 | Fingerprint check at decode on by default | on; off | On: it is the only way a reader learns that a core did not reproduce what was verified |
| D10 | Command-line style | subcommands with long options and SZ3-style aliases; SZ3-style only | Both, as in section 11.4 |
| D11 | Stream compatibility with QPET | none; a reader for QPET formats | None |
| D12 | Is a paper planned, and with what claim? | | The research's own open problem 2: at equal PSNR a uniform bound is as good. Defensible claims are the guarantee, generality across cores and QoIs, tighter QoI at equal PSNR, and one-pass speed |

D1, D2, D3 and D8 are needed before M0 and M1 start. The others can wait for the milestone that uses them.

---

## 18. Risks

| Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|
| The sample mispredicts on sparse or clustered fields | certain on some fields | larger streams, never wrong ones; worst regret about 2 times | hard cases in the benchmark from the first day; experiments E1 to E3; the trace makes it visible |
| A core is not deterministic across builds or thread counts | low for SZ3 3.4, unknown for others | the guarantee is void on the reading side | `core-check`; fingerprint; cross-build CI |
| A core is not thread-safe | medium for new cores | crashes or corrupt samples | trait; serial sampling; conformance test |
| Regions that span a whole dimension | certain when asked for | tuning takes 80 to 90 % of the time | documented; a warning in the report; slabs remain an experiment |
| Parity with QPET is missed for reasons hidden in its heuristics | medium | M1 slips | port the algorithm, not a description of it; compare traces candidate by candidate |
| Too many knobs | high by drift | untestable combinations | one table of constants; each needs a measurement |
| Native Python targets are slow | certain | minutes instead of seconds | batched callbacks; documented; C++ native targets for production |
| Memory at 4 to 5 times the input | certain | large fields do not fit | free buffers early; tiled mode in M7 |
| Upstream SZ3 changes its API or format | low within 3.x | adapter breaks | pin; a weekly CI job against upstream `master` |
| Scope creep toward the research's full feature set | high | no release | section 16.3 is the answer to "why not also" |

---

## Appendix A: default parameters

Values are the research's defaults (`qpet/main:docs/tuning.md`, `qpet/qpet-unified:docs/tuning.md`). Each is taken
from the user, else the core's hints, else this table.

| Parameter | Default | Meaning |
|---|---|---|
| `data_bound` | REL 0.01 | limit on the point-wise error of the data |
| `fidelity` | 1 dB | PSNR the frontend may spend below the reference for a smaller stream |
| `point_samples` | 2^20 | values the prior is computed for |
| `sample_rate` | 0.01 | share of the input compressed per candidate |
| `block_edge` | 262144 (1-D), 512 (2-D), 64 (3-D), 24 (4-D) | edge of a sample block |
| `min_edge` | `block_edge`; 128 in 2-D | smallest edge on a small input |
| `min_blocks`, `max_blocks` | 5 and 64; 12 and 64 in 2-D | bounds on the number of blocks |
| `quantiles` | 0, 0.0025, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1 | first candidates, as quantiles of the prior |
| `refine_rounds` | 4 | rounds of narrowing around the best candidate |
| `tie_bytes` | 0.025 | near-ties within this share go to the smaller bound |
| `rare_outlier_share` | 0.1; ZFP 0.01; SPERR 0.02 | share of sensitive values assumed corrected when the blocks missed them |
| `retreat_share`, `retreat_rounds` | 0.3, 2 | second look, regional and multi-field targets only |
| regional enlargement | `max(1, 2 sqrt(n / (2 ln(2 / 0.001))))` | tolerance of one value when `n` errors are taken as independent |
| residual levels | 0 to 30; `\|q\| <= 2^15` | ladder of the quantised residual |
| verification batch | 4096 values | batch of the point pass |

## Appendix B: reading map of `research/`

| To understand | Read |
|---|---|
| The state of the research and its open problems | `HANDOFF.md`, `docs/DECISIONS.md` |
| The algorithm this plan starts from | `git show origin/qpet/main:docs/concepts.md`, `:docs/tuning.md`, `:docs/backends.md`, `:docs/format.md`, `:src/core.cpp` |
| The newest research version | `docs/branches/qpet-unified.md`; `origin/qpet/qpet-unified:docs/` (`concepts.md`, `tuning.md`, `fidelity.md`, `targets.md`, `regions.md`, `aggregates.md`, `multifield.md`) |
| How well the sample predicts | `sample_prediction/REPORT.md`, `tuning/`, `evaluation_notes.md` |
| Whether the prior matters | `estimate_sensitivity/REPORT.md`, `tuner_search/REPORT.md`, `estimation_theory/REPORT.md` |
| Corrections | `correction_layer/REPORT.md`, `residual_coding/README.md`, `correction_efficiency/findings.md`, `fidelity/README.md` |
| Which QoIs scientists use | `real_qois/REPORT.md`, `real_qois/COOKBOOK.md` |
| QoIs outside the language | `stencil_qoi/REPORT.md`, `spectral/REPORT.md`, `auto_analysis/REPORT.md` |
| Per-value control inside a compressor | `sz3_pointwise/REPORT.md` |
| How to evaluate | `paper_eval_full/RUNBOOK.md`, `paper_eval_full/analysis/violations.md`, `paper_eval_full/scripts/qoi_numpy.py` |
| Ideas not yet built | `free_exploration/MEMO.md` (Chinese) |
| Data, builds, environment | `docs/SETUP.md`, `docs/DATA.md` |

## Appendix C: SZ3 integration notes

Read from the submodule at commit `28387be` (`SZ3/README.md`, `SZ3/docs/advanced-guide.md`,
`SZ3/include/SZ3/api/sz.hpp`, `SZ3/include/SZ3/utils/Config.hpp`).

- **Header-only C++17.** `add_subdirectory(SZ3)` defines `SZ3::SZ3core` (headers and zstd) and `SZ3::SZ3` (the same
  plus OpenMP when found). As a subproject SZ3 installs nothing, builds no tools and no tests.
- **Options appear in the parent's cache.** Set `SZ3_INSTALL`, `BUILD_SZ3_BINARY`, `BUILD_MDZ`,
  `BUILD_PARAVIEW_PLUGIN` to `OFF` as normal variables before adding it.
- **API.** `SZ_compress<T>(conf, data, buffer, capacity)` with `capacity >= SZ3::SZ_compress_size_bound<T>(conf)`;
  `SZ_decompress<T>(conf, stream, size, out)`. Dimensions are given slowest-varying first, as in our library.
- **`Config::setDims` drops dimensions of extent 1.** Our container stores the true dimensions, so this is
  harmless, but the adapter must not read dimensions back from SZ3's config.
- **The input is not modified**; the interpolation algorithms work on an internal copy, which counts in the memory
  budget.
- **A bound that is zero, negative or not finite compresses losslessly.** SZ3 also switches to zstd alone when that
  is smaller.
- **OpenMP is used only when `conf.openmp = true`.** It splits the array along the slowest dimension and stores the
  number of parts. Decoding is correct with any thread count.
- **Floating point.** SZ3 stops the build under GCC's `-ffast-math`. Include `SZ3/api/sz.hpp` before any other SZ3
  header. Builds with and without FMA decode to the same values; this is what makes invariant I6 hold for SZ3.
- **Versions.** `SZ3_VER` is the program version and `SZ3_DATA_VER` the stream format version; SZ3 reads formats
  from `3.3.2`. The submodule's `CMakeLists.txt` still says 3.4.0 for both.
- **Integers.** SZ3 also compresses integer types by converting to floating point. Not used in version 1.
- **Do not edit anything under `SZ3/`.** If the adapter needs something SZ3 does not expose, raise it upstream.
