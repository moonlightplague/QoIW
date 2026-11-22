#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

#include "core/adapters/mock.hpp"
#include "frontend/plan.hpp"

namespace {
using namespace qoiw;
using namespace qoiw::frontend;
using Mode = Tolerance::Mode;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A clean core whose data-bound stream costs one extra byte.
class PaddedIdentity final : public Core {
   public:
    std::string_view name() const override { return "padded"; }
    std::string_view version() const override { return "1"; }
    std::string_view format_version() const override { return "1"; }
    bool supports(DType t, const std::vector<std::uint64_t>& dims) const override { return core.supports(t, dims); }
    CoreOutput compress(const void* data, DType t, const std::vector<std::uint64_t>& dims,
                        const CoreParams& p) const override {
        ++encodes;
        auto out = core.compress(data, t, dims, p);
        if (p.bound == 1) out.decode_params.push_back(0);
        return out;
    }
    void decompress(Bytes stream, Bytes, DType t, const std::vector<std::uint64_t>& dims,
                    int threads, void* out, std::size_t bytes) const override {
        core.decompress(stream, {}, t, dims, threads, out, bytes);
    }
    mutable std::size_t encodes = 0;
   private:
    const Core& core = test::mock_core("mock:identity");
};

Plan tie_plan(double tie_bytes) {
    PaddedIdentity core;
    std::vector<double> data(16, 2);
    data.front() = -2;
    const PointRequest spec{{{Expr("x^2"), {Mode::Absolute, .1}}}, Tolerance{Mode::Absolute, 1}, {}};
    const auto prep = prepare_pointwise(DType::Float64, data.data(), data.size() * sizeof(double),
                                        {data.size()}, spec, {}, 1);
    auto plan = plan_pointwise(core, prep, {}, {0, tie_bytes});
    check(plan.flags.whole_input_sampled && !plan.flags.prior_free && !plan.flags.shortcut_taken &&
          !plan.flags.rare_outlier_fallback && plan.trace.size() == 2 && core.encodes == plan.trace.size(),
          "whole-field trace should cost one encode per distinct bound");
    check(plan.trace[0].bound == 1. && plan.trace[1].bytes_per_value < plan.trace[0].bytes_per_value,
          "cap and tighter candidate in measurement order");
    return plan;
}

Plan fallback_plan() {
    const auto& core = test::mock_core("mock:identity");
    std::vector<float> field(1000, 4);
    field.front() = -1;
    field.back() = 2;
    const PointRequest spec{{}, Tolerance{Mode::Absolute, 1}, Expr("x < 0")};
    const auto prep = prepare_pointwise(DType::Float32, field.data(), field.size() * sizeof(float),
                                        {field.size()}, spec, core.traits(DType::Float32, 1), 1, {}, {},
                                        {.01, 40, 40, 1, 1});
    return plan_pointwise(core, prep, {}, {}, .25);
}

class GroupedNumbers : public std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto tie = tie_plan(.025);
        check(tie.bound[0] == 1 && tie.binding == Binding::Tie && tie.predicted->outlier_share == 0,
              "clean cap won within tie band");
        const auto tighter = tie_plan(0);
        check(tighter.bound[0] < 1 && tighter.binding == Binding::Qoi, "cheaper tighter QoI candidate won");
        const auto fallback = fallback_plan();
        check(fallback.flags.rare_outlier_fallback && !fallback.flags.whole_input_sampled &&
              fallback.trace.size() == 1 && fallback.trace[0].restored == 0 &&
              fallback.predicted->outlier_share == .25 / 1000 &&
              fallback.predicted->correction_bytes_per_value == .25 / 1000 * 5, "rare-outlier prediction");
        std::locale::global(std::locale(std::locale::classic(), new GroupedNumbers));
        const double data[]{1, 2};
        const auto prep = prepare_pointwise(DType::Float64, data, sizeof(data), {2}, {}, {}, 1, .5);
        const auto fixed = plan_pointwise(test::mock_core("mock:identity"), prep);
        check(fixed.to_json().find("\"predicted\":null,\"trace\":[]") != std::string::npos,
              "unmeasured JSON prediction");
        if (argc == 2 && std::string_view(argv[1]) == "--json") {
            // Exercise string escapes and numeric boundaries through the public serializer.
            Plan edge;
            edge.targets.push_back({std::string("\"\n\\\0", 4), std::numeric_limits<double>::denorm_min()});
            edge.trace.resize(1);
            edge.trace[0].restored = 123456;
            edge.trace[0].sample_psnr_db = -std::numeric_limits<double>::infinity();
            const Plan* plans[]{&tie, &tighter, &fallback, &fixed, &edge};
            for (const auto* p : plans) std::cout << p->to_json() << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
