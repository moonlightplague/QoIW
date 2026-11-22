#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qoiw {

struct Bytes {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    constexpr Bytes() = default;
    constexpr Bytes(const std::uint8_t* data, std::size_t size) : data(data), size(size) {}
    Bytes(const std::vector<std::uint8_t>& bytes) : Bytes(bytes.data(), bytes.size()) {}
};

enum class DType : std::uint8_t { Float32 = 1, Float64 = 2 };

// The caller owns a contiguous array containing the product of the dimensions.
struct ArrayView {
    const void* data;
    DType dtype;
    std::vector<std::uint64_t> dims;

    ArrayView(const float* data, std::vector<std::uint64_t> dims)
        : data(data), dtype(DType::Float32), dims(std::move(dims)) {}
    ArrayView(const double* data, std::vector<std::uint64_t> dims)
        : data(data), dtype(DType::Float64), dims(std::move(dims)) {}
};

struct Tol {
    enum class Mode { Absolute, Relative, None };
    Mode mode;
    double value;

    static constexpr Tol abs(double value) { return {Mode::Absolute, value}; }
    static constexpr Tol rel(double value) { return {Mode::Relative, value}; }
    static constexpr Tol none() { return {Mode::None, 0}; }
};

enum class ErrorCode { InvalidArgument, Unsupported, CorruptStream, CoreFailure, Internal };

class Error : public std::runtime_error {
   public:
    Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    ErrorCode code() const noexcept { return code_; }

   private:
    ErrorCode code_;
};

}  // namespace qoiw
