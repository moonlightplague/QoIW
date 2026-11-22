#include "pipeline/compress_field.hpp"

#include <chrono>
#include <new>

#include "backend/pointwise.hpp"
#include "common/shape.hpp"
#include "frontend/plan.hpp"
#include "pipeline/compile_request.hpp"
#include "pipeline/stream.hpp"

namespace qoiw {
namespace {
using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

const Core& resolve_core(const Options& options) {
    try {
        return get_core(options.core);
    } catch (const std::invalid_argument& error) {
        throw Error(ErrorCode::Unsupported, error.what());
    }
}

template <class Work>
auto call_core(Work work) {
    try {
        return work();
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        throw Error(ErrorCode::CoreFailure, error.what());
    }
}

template <class T>
backend::PointCorrections correct_field(const Core& core, const CoreOutput& payload, const ArrayView& input,
                                        std::size_t bytes, const ResolvedPointRequest& request, int threads,
                                        CompressionTimes& timings, Verification* verification,
                                        std::vector<std::uint8_t>* verified) {
    auto start = Clock::now();
    std::vector<T> decoded(bytes / sizeof(T));
    call_core([&] {
        core.decompress(payload.stream, payload.decode_params, input.dtype, input.dims, threads, decoded.data(), bytes);
    });
    timings.core_decode = seconds_since(start);
    start = Clock::now();
    auto corrections = backend::correct_pointwise(input.dtype, input.data, decoded.data(), bytes, request, threads);
    timings.correction = seconds_since(start);
    if (verification) {
        start = Clock::now();
        *verification = backend::verify_pointwise(input.dtype, input.data, decoded.data(), bytes, request, threads);
        timings.verification = seconds_since(start);
    }
    if (verified) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(decoded.data());
        verified->assign(begin, begin + bytes);
    }
    return corrections;
}
}  // namespace

std::vector<std::uint8_t> pipeline::compress_field(const ArrayView& input, const Request& request, const Options& options,
                                                 Report* report, const Core* supplied_core,
                                                 std::vector<std::uint8_t>* verified) try {
    const auto call_start = Clock::now();
    const auto bytes = common::array_bytes(input.dtype, input.dims);
    if (!input.data || !bytes || options.threads < 0) throw std::invalid_argument("invalid input or thread count");
    const Core& core = supplied_core ? *supplied_core : resolve_core(options);
    if (!core.supports(input.dtype, input.dims))
        throw Error(ErrorCode::Unsupported, "core does not support the input type or shape");

    Report result;
    CoreParams params{0, options.threads, options.allow_stream_changing_parallelism, options.core_options};
    ResolvedPointRequest resolved;
    auto start = Clock::now();
    {
        const auto traits = core.traits(input.dtype, input.dims.size());
        const auto& tuning = options.frontend;
        auto prepared = frontend::prepare_pointwise(input.dtype, input.data, bytes, input.dims,
                                                     pipeline::compile_point_request(request), traits, params.threads,
                                                     options.fixed_bound, tuning.prior, tuning.sampling);
        result.plan = call_core([&] {
            return frontend::plan_pointwise(core, prepared, params, tuning.search, tuning.rare_outlier_share);
        });
        resolved = std::move(prepared.resolved.request);
        result.nonfinite_inputs = prepared.resolved.nonfinite_inputs;
    }
    result.seconds.planning = seconds_since(start);

    start = Clock::now();
    params.bound = result.plan.bound.at(0);
    auto payload = call_core([&] { return core.compress(input.data, input.dtype, input.dims, params); });
    result.seconds.core_encode = seconds_since(start);
    auto* verification = report ? &result.verification : nullptr;
    auto corrections = input.dtype == DType::Float32
        ? correct_field<float>(core, payload, input, bytes, resolved, params.threads, result.seconds, verification, verified)
        : correct_field<double>(core, payload, input, bytes, resolved, params.threads, result.seconds, verification, verified);
    result.restored = corrections.restored;

    start = Clock::now();
    container::Stream stream;
    stream.dtype = input.dtype;
    stream.dims = input.dims;
    stream.wrapper_version = "0.0";
    stream.core_name = core.name();
    stream.core_version = core.version();
    stream.core_format_version = core.format_version();
    stream.fields = {{"x", params.bound, resolved.data_bound_abs, payload.decode_params, 0, payload.stream,
                      corrections.lossless}};
    for (const auto& target : result.plan.targets)
        stream.targets.push_back({target.expression, target.tolerance_abs});
    if (result.plan.exact_condition) stream.exact_condition = *result.plan.exact_condition;
    auto blob = container::write(stream);
    result.seconds.container = seconds_since(start);
    if (report) {
        result.stream = pipeline::describe_stream(stream, blob.size());
        result.seconds.total = seconds_since(call_start);
        *report = std::move(result);
    }
    return blob;
} catch (const Error&) {
    throw;
} catch (const std::bad_alloc&) {
    throw;
} catch (const std::invalid_argument& error) {
    throw Error(ErrorCode::InvalidArgument, error.what());
} catch (const std::overflow_error& error) {
    throw Error(ErrorCode::InvalidArgument, error.what());
} catch (const std::exception& error) {
    throw Error(ErrorCode::Internal, error.what());
}

std::vector<std::uint8_t> compress(const ArrayView& input, const Request& request, const Options& options,
                                   Report* report) {
    return pipeline::compress_field(input, request, options, report);
}

}  // namespace qoiw
