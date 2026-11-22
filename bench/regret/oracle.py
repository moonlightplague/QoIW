"""Choose full-field oracle trials and score regret and parity."""

import math


def grid(lower, cap, points):
    if not (math.isfinite(lower) and math.isfinite(cap) and 0 <= lower <= cap) or points < 2:
        raise ValueError("invalid oracle interval or point count")
    if lower == cap:
        return [cap]
    if lower == 0:
        raise ValueError("a nonzero cap requires a positive starting bound")
    start, stop = math.log(lower), math.log(cap)
    interior = (math.exp(start + (stop - start) * i / (points - 1)) for i in range(1, points - 1))
    return sorted({lower, cap, *(min(cap, max(lower, value)) for value in interior)})


def midpoints(bounds, best):
    ordered = sorted(bounds)
    index = ordered.index(best)
    neighbours = ordered[max(0, index - 1):index] + ordered[index + 1:index + 2]
    for other in neighbours:
        if other > 0 and best > 0:
            bound = math.exp((math.log(other) + math.log(best)) * .5)
            if min(other, best) < bound < max(other, best) and bound not in bounds:
                yield bound


def scan(lower, cap, tuned, measure, points=21, rounds=3):
    # Include the already measured tuned stream so the discrete oracle never
    # scores a better result from tuning as regret below one.
    trials = {tuned["bound"]: tuned}
    bounds = set(grid(lower, cap, points))
    if lower <= tuned["bound"] <= cap:
        bounds.add(tuned["bound"])
    for bound in sorted(bounds):
        if bound not in trials:
            trials[bound] = measure(bound)

    def best_bound(pool):
        eligible = (bound for bound in pool if trials[bound]["verification"]["ok"])
        return min(eligible, key=lambda bound: (trials[bound]["bytes"], bound), default=None)

    for _ in range(rounds):
        best = best_bound(bounds)
        if best is None:
            break
        extra = list(midpoints(bounds, best))
        if not extra:
            break
        for bound in extra:
            trials[bound] = measure(bound)
            bounds.add(bound)
    best = best_bound(trials)
    return (None if best is None else trials[best]), list(trials.values())


def geomean(values):
    return math.exp(math.fsum(math.log(value) for value in values) / len(values))


def scores(cases):
    if not cases:
        raise ValueError("at least one completed case is required")
    parity = [case["qoiw"]["bytes"] / case["qpet"]["bytes"] for case in cases]
    ratios = [case["qoiw"]["bytes"] / case["oracle"]["bytes"] for case in cases if case["oracle"]]
    verification = all(case["all_bounds_hold"] for case in cases)
    result = {
        "cases": len(cases),
        "all_bounds_hold": verification,
        "parity": {
            "geomean": geomean(parity),
            "worst_deviation": max(abs(ratio - 1) for ratio in parity),
            "pass": .97 <= geomean(parity) <= 1.03 and all(.90 <= ratio <= 1.10 for ratio in parity),
        },
        "regret": None,
    }
    if ratios:
        result["regret"] = {
            "geomean": geomean(ratios),
            "worst": max(ratios),
            "above_1_05": sum(ratio > 1.05 for ratio in ratios),
            "pass": len(ratios) == len(cases) and geomean(ratios) <= 1.07,
        }
    return result
