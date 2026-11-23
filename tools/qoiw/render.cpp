#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

#include "common/shape.hpp"
#include "qoiw/core.hpp"

namespace qoiw::cli {
namespace {

using Align = Table::Align;
using Emphasis = Table::Emphasis;

const char* type_name(DType dtype) {
    return dtype == DType::Float32 ? "float32" : "float64";
}

std::uint64_t value_bytes(DType dtype) {
    return dtype == DType::Float32 ? 4 : 8;
}

std::string json_number(double value) {
    if (std::isinf(value)) return value > 0 ? "\"+inf\"" : "\"-inf\"";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}

std::string requested(const Tol& tol) {
    switch (tol.mode) {
        case Tol::Mode::Absolute: return "ABS " + significant(tol.value);
        case Tol::Mode::Relative: return "REL " + significant(tol.value);
        case Tol::Mode::None: return "none";
    }
    return "";
}

std::string throughput(std::uint64_t bytes, double seconds) {
    if (!(seconds > 0)) return "";
    return " (" + significant(static_cast<double>(bytes) / 1e6 / seconds, 4) + " MB/s)";
}

// Sub-second totals read better in milliseconds.
std::string seconds_text(double seconds) {
    if (seconds < 1) return fixed(1e3 * seconds, seconds < .01 ? 3 : 1) + " ms";
    return fixed(seconds, seconds < 10 ? 3 : 2) + " s";
}

// Times in a runtime table use one unit, chosen from the total.
class RuntimeTable {
   public:
    explicit RuntimeTable(double total)
        : total_(total), milliseconds_(total < 1),
          table_({{"Stage", Align::Left}, {milliseconds_ ? "ms" : "seconds", Align::Right}, {"share", Align::Right}}) {}

    // Top-level stages count towards "other"; nested ones are their parts.
    void stage(const std::string& name, double seconds, int depth = 0) {
        if (depth == 0) accounted_ += seconds;
        table_.row({name, time(seconds), share(seconds)}, depth, depth ? Emphasis::Dim : Emphasis::None);
    }

    void add_total() {
        const double other = std::max(0., total_ - accounted_);
        table_.row({"other (buffers, bookkeeping)", time(other), share(other)}, 0, Emphasis::Dim);
        table_.row({"total", time(total_), share(total_)}, 0, Emphasis::Bold);
    }

    void outside_total(const std::string& name, double seconds) {
        table_.row({name, time(seconds), ""}, 0, Emphasis::Dim);
    }

    void print(Console& console) const {
        console.section("Runtime");
        table_.print(console);
    }

   private:
    std::string time(double seconds) const {
        return milliseconds_ ? fixed(1e3 * seconds, total_ < .01 ? 3 : 2) : fixed(seconds, 3);
    }
    std::string share(double seconds) const {
        return total_ > 0 ? percent(seconds / total_) : "";
    }

    double total_, accounted_ = 0;
    bool milliseconds_;
    Table table_;
};

std::string binding_text(const Plan& plan) {
    if (plan.shortcut_reason) {
        switch (*plan.shortcut_reason) {
            case ShortcutReason::FixedBound: return "given by --fixed-bound";
            case ShortcutReason::ZeroRadii: return "fallback: no sampled value tolerates any error";
            case ShortcutReason::SingleCandidate: return "single candidate of an affine request, not measured";
        }
    }
    std::string text = "chosen by ";
    if (!plan.binding) text += "cost";
    else if (*plan.binding == Binding::Qoi) text += "the QoI targets";
    else if (*plan.binding == Binding::DataBound) text += "the data bound";
    else text += "a near-tie with the data bound";
    const auto trials = plan.trace.size();
    return text + " after " + std::to_string(trials) + " measured bound" + (trials == 1 ? "" : "s");
}

void print_data_description(Console& console, DType dtype, const std::vector<std::uint64_t>& dims) {
    std::uint64_t values = 1;
    for (auto d : dims) values *= d;
    console.field("type", std::string(type_name(dtype)) + ", " + shape(dims) + "  (" + group_digits(values) +
                              " values, " + size(values * value_bytes(dtype)) + ")");
}

void print_core(Console& console, std::string_view name, std::string_view version, std::string_view format) {
    console.field("core", std::string(name) + " " + std::string(version) + ", stream format " + std::string(format));
}

void print_request(Console& console, const Request& request, const std::vector<TargetTolerance>& resolved,
                   std::optional<double> data_bound_abs) {
    console.section("Request");
    for (std::size_t i = 0; i < request.targets.size(); ++i) {
        const auto& target = request.targets[i];
        std::string text = target.expression + "    " + requested(target.tolerance);
        if (i < resolved.size() && target.tolerance.mode == Tol::Mode::Relative)
            text += "  ->  " + significant(resolved[i].tolerance_abs) + " absolute";
        console.field("target " + std::to_string(i + 1), text);
    }
    if (request.targets.empty()) console.field("targets", "none");
    std::string bound = requested(request.data_bound);
    if (data_bound_abs && request.data_bound.mode == Tol::Mode::Relative)
        bound += "  ->  " + significant(*data_bound_abs) + " absolute";
    console.field("data bound", bound);
    if (request.exact) console.field("exact", *request.exact);
}

std::string status(Console& console, bool ok) {
    return ok ? console.good("PASSED") : console.bad("FAILED");
}

void print_checks(Console& console, const Verification& result, bool exact) {
    Table table({{"Check", Align::Left}, {"tolerance (abs)", Align::Right}, {"max error", Align::Right},
                 {"violations", Align::Right}, {"non-finite", Align::Right}});
    auto failing = [](const ErrorStats& errors) { return errors.violations ? Emphasis::Bold : Emphasis::None; };
    table.row({"data", result.data_bound_abs ? significant(*result.data_bound_abs) : "none",
               significant(result.data.max_abs_error), group_digits(result.data.violations),
               group_digits(result.data.nonfinite_violations)}, 0, failing(result.data));
    for (const auto& target : result.targets) {
        table.row({target.target.expression, significant(target.target.tolerance_abs),
                   significant(target.errors.max_abs_error), group_digits(target.errors.violations),
                   group_digits(target.errors.nonfinite_violations)}, 0, failing(target.errors));
    }
    if (exact) {
        table.row({"exact predicate", "bit-exact", "", group_digits(result.exact_violations), ""}, 0,
                  result.exact_violations ? Emphasis::Bold : Emphasis::None);
    }
    table.print(console);
}

std::string verification_line(Console& console, const Verification& result) {
    return status(console, result.ok()) + "  " + group_digits(result.violating_values) + " of " +
           group_digits(result.values) + " values violate a requirement";
}

void print_trace(Console& console, const Plan& plan) {
    console.section("Measured bounds (" + std::to_string(plan.trace.size()) + ", in measurement order)");
    if (plan.trace.empty()) {
        console.line("  none: the bound was chosen without measurement");
        return;
    }
    Table table({{"", Align::Left}, {"bound", Align::Right}, {"B/value", Align::Right}, {"core", Align::Right},
                 {"corrections", Align::Right}, {"weight", Align::Right}, {"restored", Align::Right},
                 {"sample PSNR", Align::Right}, {"note", Align::Left}});
    for (const auto& trial : plan.trace) {
        const bool chosen = trial.bound == plan.bound.at(0);
        table.row({chosen ? "*" : "", significant(trial.bound), fixed(trial.bytes_per_value, 4),
                   fixed(trial.core_bytes_per_value(), 4), fixed(trial.correction_bytes_per_value, 4),
                   fixed(trial.correction_weight, 2), group_digits(trial.restored),
                   trial.sample_psnr_db ? significant(*trial.sample_psnr_db, 4) + " dB" : "-",
                   trial.rare_outlier_fallback ? "rare-outlier estimate" : ""},
                  0, chosen ? Emphasis::Bold : Emphasis::None);
    }
    table.print(console);
    console.line(console.dim("  * chosen; B/value = core + weight x corrections, per sampled value"));
}

void print_stream_details(Console& console, const StreamInfo& info) {
    print_data_description(console, info.dtype, info.dims);
    print_core(console, info.core_name, info.core_version, info.core_format_version);
    for (const auto& field : info.fields) {
        console.field("field " + field.name, "bound " + significant(field.bound) + ", data bound " +
                                                 (field.data_bound_abs ? significant(*field.data_bound_abs) : "none"));
        console.field("", "core " + group_digits(field.core_bytes) + " B, corrections " +
                              group_digits(field.lossless_bytes) + " B, decode parameters " +
                              group_digits(field.decode_params_bytes) + " B");
    }
    for (std::size_t i = 0; i < info.targets.size(); ++i)
        console.field("target " + std::to_string(i + 1),
                      info.targets[i].expression + "    within " + significant(info.targets[i].tolerance_abs));
    if (info.exact_condition) console.field("exact", *info.exact_condition);
    console.field("size", group_digits(info.stream_bytes) + " B  (" + size(info.stream_bytes) + ")");
}

double ratio(const StreamInfo& info) {
    return static_cast<double>(info.num) * info.fields.size() * value_bytes(info.dtype) / info.stream_bytes;
}

std::string ratio_item(Console& console, double value) {
    return "Compression ratio " + console.accent(fixed(value, 2));
}

}  // namespace

void print_compression(Console& console, const Arguments& args, const Report& report,
                       const std::optional<Verification>& post_decode, const CompressionRun& run) {
    const auto& info = report.stream;
    const auto raw = static_cast<std::uint64_t>(info.num) * value_bytes(info.dtype);
    const bool ok = report.verification.ok() && (!post_decode || post_decode->ok());
    console.title("qoiw compress", args.input + (run.written ? "  ->  " + args.stream : "  (not saved)"));

    console.section("Input");
    print_data_description(console, info.dtype, info.dims);
    std::string core = info.core_name + " " + info.core_version + ", stream format " + info.core_format_version;
    for (const auto& [key, value] : args.options.core_options) core += ", " + key + "=" + value;
    console.field("core", core);
    console.field("threads", args.options.threads ? std::to_string(args.options.threads) : "OpenMP default");
    if (report.nonfinite_inputs) console.field("non-finite", group_digits(report.nonfinite_inputs) + " values, kept exact");

    print_request(console, args.request, report.plan.targets, report.plan.data_bound_abs);

    console.section("Plan");
    const auto& plan = report.plan;
    console.field("bound", significant(plan.bound.at(0)) + " absolute, " + binding_text(plan));
    if (plan.predicted) {
        const auto& p = *plan.predicted;
        std::string text = fixed(p.bytes_per_value, 4) + " B/value  (core " + fixed(p.core_bytes_per_value, 4) +
                           ", corrections " + fixed(p.correction_bytes_per_value, 4) + ")";
        if (p.indicative_compression_ratio) text += ", ratio ~" + fixed(*p.indicative_compression_ratio, 1);
        console.field("predicted", text);
        const auto chosen = std::find_if(plan.trace.begin(), plan.trace.end(),
                                         [&](const auto& trial) { return trial.bound == plan.bound[0]; });
        if (chosen != plan.trace.end())
            console.field("sample", group_digits(chosen->sample_values) + " values" +
                                        (plan.flags.whole_input_sampled ? " (the whole input)" : " in blocks"));
    }

    console.section("Stream");
    std::size_t core_bytes = 0, lossless = 0;
    for (const auto& field : info.fields) {
        core_bytes += field.core_bytes + field.decode_params_bytes;
        lossless += field.lossless_bytes;
    }
    const auto framing = info.stream_bytes - core_bytes - lossless;
    const auto share = [&](std::size_t bytes) { return percent(static_cast<double>(bytes) / info.stream_bytes); };
    Table stream({{"Part", Align::Left}, {"bytes", Align::Right}, {"share", Align::Right}, {"", Align::Left}});
    stream.row({"core stream", group_digits(core_bytes), share(core_bytes), ""});
    stream.row({"lossless corrections", group_digits(lossless), share(lossless),
                group_digits(report.restored) + " values restored (" +
                    fixed(100. * report.restored / std::max<std::size_t>(info.num, 1), 3) + " %)"});
    stream.row({"header and framing", group_digits(framing), share(framing), ""});
    stream.row({"total", group_digits(info.stream_bytes), share(info.stream_bytes),
                "from " + size(raw)}, 0, Emphasis::Bold);
    stream.print(console);

    console.section("Verification");
    print_checks(console, post_decode ? *post_decode : report.verification, args.request.exact.has_value());
    console.blank();
    console.field("on encode", verification_line(console, report.verification));
    if (post_decode) console.field("on decode (-a)", verification_line(console, *post_decode));

    if (args.verbose) print_trace(console, plan);

    const auto& seconds = report.seconds;
    RuntimeTable runtime(seconds.total);
    runtime.stage("frontend (tuning)", seconds.planning);
    runtime.stage("core", seconds.core_encode + seconds.core_decode);
    runtime.stage("compress", seconds.core_encode, 1);
    runtime.stage("decompress", seconds.core_decode, 1);
    runtime.stage("backend", seconds.correction + seconds.container);
    runtime.stage("check and correct", seconds.correction, 1);
    runtime.stage("assemble container", seconds.container, 1);
    runtime.stage("encoder check", seconds.verification);
    runtime.add_total();
    runtime.outside_total("read input", run.read_input);
    if (run.written) runtime.outside_total("write stream", run.write_stream);
    if (run.check_decode) {
        runtime.outside_total("decode container (-a)", run.check_decode->total);
        runtime.outside_total("verify decoded (-a)", run.check_verify);
    }
    runtime.print(console);

    console.summary({ratio_item(console, raw / static_cast<double>(info.stream_bytes)),
                     "Total time " + console.accent(seconds_text(seconds.total)) + throughput(raw, seconds.total),
                     status(console, ok)});
}

std::string compression_json(const Report& report, const std::optional<Verification>& post_decode,
                             const CompressionRun& run) {
    std::ostringstream out;
    out << "{\"report\":" << report.to_json() << ",\"post_decode\":" << (post_decode ? post_decode->to_json() : "null")
        << ",\"runtime\":{\"read_input\":" << json_number(run.read_input)
        << ",\"compress\":" << json_number(report.seconds.total)
        << ",\"post_decode\":";
    if (run.check_decode) {
        out << "{\"decompress\":" << run.check_decode->to_json() << ",\"verify\":" << json_number(run.check_verify)
            << '}';
    } else {
        out << "null";
    }
    out << ",\"write_stream\":" << (run.written ? json_number(run.write_stream) : "null")
        << ",\"total\":" << json_number(report.seconds.total) << "}}";
    return out.str();
}

void print_decompression(Console& console, const Arguments& args, const StreamInfo& info,
                         const DecompressionRun& run) {
    console.title("qoiw decompress", args.stream + "  ->  " + args.output);
    console.section("Stream");
    print_stream_details(console, info);

    const auto& seconds = run.decode;
    RuntimeTable runtime(seconds.total);
    runtime.stage("parse container", seconds.parse);
    runtime.stage("core decompress", seconds.core_decode);
    runtime.stage("apply corrections", seconds.correction);
    runtime.add_total();
    runtime.outside_total("read stream", run.read_stream);
    runtime.outside_total("write output", run.write_output);
    runtime.print(console);

    const auto raw = static_cast<std::uint64_t>(info.num) * info.fields.size() * value_bytes(info.dtype);
    console.summary({ratio_item(console, ratio(info)),
                     "Total time " + console.accent(seconds_text(seconds.total)) + throughput(raw, seconds.total)});
}

std::string decompression_json(const StreamInfo& info, const DecompressionRun& run) {
    std::ostringstream out;
    out << "{\"stream\":" << info.to_json() << ",\"runtime\":{\"read_stream\":" << json_number(run.read_stream)
        << ",\"decompress\":" << run.decode.to_json() << ",\"write_output\":" << json_number(run.write_output)
        << ",\"total\":" << json_number(run.decode.total) << "}}";
    return out.str();
}

void print_inspection(Console& console, const Arguments& args, const StreamInfo& info) {
    console.title("qoiw inspect", args.stream);
    console.section("Stream");
    print_stream_details(console, info);
    console.summary({ratio_item(console, ratio(info))});
}

void print_verification(Console& console, const Arguments& args, const Verification& result,
                        const VerificationRun& run) {
    console.title("qoiw verify", args.input + "  vs  " + args.output);
    console.section("Input");
    print_data_description(console, *args.dtype, args.dims);
    std::vector<TargetTolerance> resolved;
    for (const auto& target : result.targets) resolved.push_back(target.target);
    print_request(console, args.request, resolved, result.data_bound_abs);
    console.section("Verification");
    print_checks(console, result, args.request.exact.has_value());

    RuntimeTable runtime(run.total);
    runtime.stage("read inputs", run.read_inputs);
    runtime.stage("verify", run.verify);
    runtime.add_total();
    runtime.print(console);

    console.summary({status(console, result.ok()), group_digits(result.violating_values) + " of " +
                                                       group_digits(result.values) + " values violate a requirement",
                     "Total time " + console.accent(seconds_text(run.total))});
}

void print_cores(Console& console) {
    Table table({{"Core", Align::Left}, {"version", Align::Left}, {"format", Align::Left}, {"type", Align::Left},
                 {"ranks", Align::Left}, {"bound", Align::Left}, {"thread-safe", Align::Left},
                 {"call overhead", Align::Left}});
    for (const auto& name : available_cores()) {
        const auto& core = get_core(name);
        bool first = true;
        for (const auto dtype : {DType::Float32, DType::Float64}) {
            // Group consecutive ranks that share the same traits into one row.
            std::size_t rank = 1;
            while (rank <= 4) {
                if (!core.supports(dtype, std::vector<std::uint64_t>(rank, 2))) {
                    ++rank;
                    continue;
                }
                const auto traits = core.traits(dtype, rank);
                auto last = rank;
                while (last < 4 && core.supports(dtype, std::vector<std::uint64_t>(last + 1, 2))) {
                    const auto next = core.traits(dtype, last + 1);
                    if (next.bound_kind != traits.bound_kind || next.thread_safe != traits.thread_safe ||
                        next.fixed_call_overhead != traits.fixed_call_overhead) break;
                    ++last;
                }
                table.row({first ? std::string(name) : "", first ? std::string(core.version()) : "",
                           first ? std::string(core.format_version()) : "", type_name(dtype),
                           last == rank ? std::to_string(rank) : std::to_string(rank) + "-" + std::to_string(last),
                           traits.bound_kind == BoundKind::StrictAbs ? "strict" : "nominal",
                           traits.thread_safe ? "yes" : "no", traits.fixed_call_overhead ? "yes" : "no"});
                first = false;
                rank = last + 1;
            }
        }
    }
    table.print(console);
}

}  // namespace qoiw::cli
