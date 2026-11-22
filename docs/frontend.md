# Frontend: choosing the core's bound

The frontend turns a request into one absolute bound for the core. It never
decides correctness: whatever it picks, the backend checks the whole field.
A bad choice costs bytes, not guarantees. The steps follow plan section 7;
the code is in `src/frontend/`.

```
prepare_pointwise   F0 resolve -> F2 fixed bound? -> F1 prior -> F2 shortcuts -> F3 sample
plan_pointwise      select_pointwise_bound (F4 + F5) -> F6 plan
```

`prepare_pointwise` returns either a `PointShortcut` (bound and reason) or a
`PointSearch` (prior and sample). Both carry the resolved request, which the
pipeline uses for the full-field check.

## F0: resolve tolerances

`resolve_pointwise` (`resolve.cpp`) scans the original field once, in
parallel chunks of 4096 values.

For each value `x`:

1. Non-finite `x`: counted in `nonfinite_inputs` and marked exact.
2. Exact predicate non-zero: marked exact.
3. Otherwise `x` joins the data range, and for each target `f(x)` joins that
   target's range if finite. If `f(x)` is not finite, `x` is marked exact; the
   other targets still use it.

The mark is a bit per value (`exact_mask`), used by F1 to give those values a
radius of zero.

Absolute tolerances pass through. A relative value `r` becomes
`r * (max - min)` of the matching range. If the range is empty, constant, or
`r` is 0, `r` is used as an absolute value. When `max - min` overflows, the
width is computed as `r*max - r*min`; a result that is still infinite raises
`std::overflow_error` (reported as `InvalidArgument`).

## F1: prior

`estimate_point_prior` (`prior.cpp`) reads values at a stride of
`max(1, n / point_samples)` and computes a radius for each: an estimate of the
largest error the value tolerates under every target at once.

The radius is capped by `cap = min(data range width, data bound)`. If the cap
is 0, every radius is 0. Marked values get 0. For each target, with jet
`(v, f', f'', dist, brk)` from [the expression engine](expressions.md#derivatives):

1. Non-finite `v`, `f'` or `f''`: radius 0.
2. Start from `min(cap, dist, e)` where `e` is the positive root of
   `|f'| e + |f''| e^2 / 2 = tau`, computed with rescaling when the squares
   leave the normal range.
3. If a breakpoint is closer than the radius, probe just across it (margin
   `1e-9 * max(brk, |x|)`). If the jump exceeds `tau`, stop before the
   breakpoint; otherwise double outwards until a probe fails.
4. Check `|f(x ± e) - f(x)| <= tau`. On failure shrink by
   `max(0.5, 0.999 tau / error)` for up to eight tries, then halve, until
   both ends pass or the radius is 0.

The value's radius is the smallest over targets. A request without targets
gives every unmarked value the cap.

Radii are sorted. The candidates are the positive values at the configured
quantiles (index `min(n-1, q n)`), sorted and deduplicated. If every quantile
selects 0 but some radius is positive, the smallest positive radius is the
only candidate.

The estimator and default quantiles follow public QPET `a80e686`.

## F2: shortcuts

| Condition | Bound | `shortcut_reason` |
| --- | --- | --- |
| `Options::fixed_bound` is set (checked before F1) | that value, which may exceed the data bound | `fixed_bound` |
| every sampled radius is 0 | `1e-9 * data range width` (at least the smallest positive double), or 1 for a constant or empty range, capped by the data bound | `zero_radii` |
| one candidate, the core is `StrictAbs`, no exact predicate, at least one constraint, and every target is affine | the candidate | `single_candidate` |

A shortcut skips F3 to F5. The plan has an empty trace and no prediction.

## F3: sample blocks

`plan_blocks` and `sample_blocks` (`sample.cpp`) copy contiguous blocks of
the original into owned arrays of the storage type.

| Rank | `block_edge` | `min_edge` | `min_blocks` | `max_blocks` |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 262144 | 262144 | 5 | 64 |
| 2 | 512 | 128 | 12 | 64 |
| 3 | 64 | 64 | 5 | 64 |
| 4 | 24 | 24 | 5 | 64 |

1. The edge is `min(block_edge, max(fit, min_edge))`, where
   `fit = (sample_rate * n / min_blocks)^(1/rank)`.
2. Each dimension is split into `ceil(d / edge)` equal tiles; the block
   extent is the tile size.
3. The count is `ceil(sample_rate * n / block values)`, clamped to
   `[min_blocks, max_blocks]`.
4. Block `k` takes, in dimension `j`, tile `floor(halton(k, base_j) * tiles)`
   with bases 2, 3, 5, 7; the last tile is shifted back to fit.
5. If the blocks would cover half the input or more, the whole field is one
   block and `whole_input_sampled` is set.

Block order and contents do not depend on the thread count. On Hurricane
`100 × 500 × 500` the defaults give five `50 × 63 × 63` blocks, the same
locations as QPET.

## F4: measure candidates

`PointMeasurer::measure(bound)` (`measure.cpp`) is cached per bound. For each
block, in parallel with one core thread per call (serially if the core is not
`thread_safe`):

1. `core.compress` and `core.decompress` the block at the bound.
2. Run `backend::correct_pointwise` on a copy of the reconstruction, with the
   full-field resolved request.
3. Record core bytes (stream plus decode parameters), the LOSSLESS payload
   size, the restored count, and squared errors of the corrected block.

The cost model:

| Quantity | Definition |
| --- | --- |
| `input_sensitive_share` | Share of F1 radii strictly below the bound. |
| `sample_sensitive_share` | The same over radii of strided points in the blocks (about 2^20 in total), or the input share for a whole-field sample. |
| `correction_weight` | `input share / sample share`, or 1 if the sample share is 0. |
| rare-outlier fallback | If `input share > 20 × sample share`: `outlier_share = rare_outlier_share × input share` and correction bytes per value `= outlier_share × (value bytes + 1)`. |
| otherwise | `outlier_share = min(1, weight × restored / sample values)`, correction bytes per value `= weight × lossless bytes / sample values`. |
| `overhead_bytes` | For `fixed_call_overhead` cores on multi-block samples: the size of a constant block at the same shape and bound, deducted per block (capped at its bytes). |
| `bytes_per_value` | `(core bytes - overhead) / sample values + correction bytes per value`. |
| `sample_psnr_db` | From the corrected blocks' mean squared error over finite, unmasked originals and the full-field data range. `+inf` for zero error, `-inf` for a zero range with error, empty if no value qualifies. |

## F5: search

`search_pointwise_bounds` (`search.cpp`):

1. Drop zero candidates. For a `StrictAbs` core with a data bound, clamp
   candidates to the data bound. Sort and deduplicate.
2. If there is a data bound, measure it first (the cap probe). If it needs no
   restorations it joins the candidates as a "clean cap".
3. Measure every candidate; the cheapest is the current best.
4. Refine for up to `refine_rounds`: measure the geometric midpoints between
   the best and each neighbour, move to a cheaper midpoint or narrow to the
   two midpoints. Stop once the interval is within 5 % or cannot shrink.
5. Cutoff `= cheapest × (1 + tie_bytes)`. A clean cap within the cutoff wins.
   Otherwise the smallest measured bound within the cutoff wins, among
   candidates and midpoints. A cap probe that needed corrections competes only
   if a candidate or midpoint equals it.

The trace keeps every distinct measured bound in measurement order.

## F6: plan

`plan_pointwise` (`plan.cpp`) assembles the [`Plan`](api.md#plan). Binding:

| Case | `binding` |
| --- | --- |
| Data bound present and no target or exact predicate, or the chosen bound equals the data bound | `DATA_BOUND` |
| The clean cap was chosen although a smaller measured bound was strictly cheaper | `TIE` |
| A target or exact predicate exists and the bound differs from the data bound | `QOI` |
| `fixed_bound`, `zero_radii`, or a request without constraints | empty |

The prediction is the chosen trial's costs, outlier share and PSNR, and the
indicative ratio `value bytes / bytes_per_value` (empty for zero cost). The
absolute sample bytes are biased by field and core (research: 1 to 6 times),
so the ratio is indicative only.
