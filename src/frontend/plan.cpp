#include "frontend/plan.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "common/shape.hpp"

namespace qoiw::frontend {
namespace {
std::optional<Binding> binding(const ResolvedPointRequest& request, double bound) {
    const bool has_qoi = !request.targets.empty() || request.exact.has_value();
    if (request.data_bound_abs && (!has_qoi || bound == *request.data_bound_abs)) return Binding::DataBound;
    if (has_qoi) return Binding::Qoi;
    return {};
}
}  // namespace

Plan plan_pointwise(const Core& core, const PointPreparation& prepared, CoreParams params,
                    const SearchOptions& options, double rare_outlier_share) {
    auto selection = select_pointwise_bound(core, prepared, std::move(params), options, rare_outlier_share);
    Plan plan;
    plan.bound = {selection.bound};
    plan.reference_bound = selection.bound;
    const auto& request = prepared.resolved.request;
    plan.data_bound_abs = request.data_bound_abs;
    for (const auto& target : request.targets)
        plan.targets.push_back({target.expression.text(), target.tolerance_abs});
    if (const auto* shortcut = std::get_if<PointShortcut>(&prepared.decision)) {
        plan.flags.shortcut_taken = true;
        plan.shortcut_reason = shortcut->reason;
        if (shortcut->reason == ShortcutReason::SingleCandidate) plan.binding = binding(request, selection.bound);
        return plan;
    }

    const auto& sample = std::get<PointSearch>(prepared.decision).sample;
    plan.flags.whole_input_sampled = sample.layout.whole_input_sampled;
    plan.trace = std::move(selection.trace);
    const auto chosen = std::find_if(plan.trace.begin(), plan.trace.end(),
                                     [&](const auto& trial) { return trial.bound == selection.bound; });
    if (chosen == plan.trace.end()) throw std::logic_error("chosen point-wise bound missing from trace");
    plan.flags.rare_outlier_fallback = chosen->rare_outlier_fallback;
    plan.binding = binding(request, selection.bound);
    // F5 can prefer a clean data bound over a strictly cheaper, tighter QoI
    // candidate inside the configured cost band. Attribute this choice to both.
    if (plan.data_bound_abs == selection.bound && (!request.targets.empty() || request.exact) &&
        chosen->restored == 0 && std::any_of(plan.trace.begin(), plan.trace.end(), [&](const auto& trial) {
            return trial.bound < chosen->bound && trial.bytes_per_value < chosen->bytes_per_value;
        })) plan.binding = Binding::Tie;

    const double outlier_share = chosen->rare_outlier_fallback ? rare_outlier_share * chosen->input_sensitive_share
        : std::min(1., chosen->correction_weight * chosen->restored / chosen->sample_values);
    std::optional<double> ratio;
    if (chosen->bytes_per_value > 0) ratio = common::element_bytes(sample.dtype()) / chosen->bytes_per_value;
    plan.predicted = PlanPrediction{chosen->core_bytes_per_value(), chosen->correction_bytes_per_value,
                                    chosen->bytes_per_value, outlier_share, chosen->sample_psnr_db, ratio};
    return plan;
}

}  // namespace qoiw::frontend
