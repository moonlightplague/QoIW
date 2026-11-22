#include "qoiw/qoiw.hpp"

#include <new>

#include "backend/pointwise.hpp"
#include "common/shape.hpp"
#include "pipeline/compile_request.hpp"

namespace qoiw {

Verification verify(const ArrayView& original, const ArrayView& reconstructed, const Request& request,
                    int threads) try {
    const auto bytes = common::array_bytes(original.dtype, original.dims);
    if (!bytes || !original.data || !reconstructed.data || threads < 0)
        throw std::invalid_argument("invalid verification field or thread count");
    if (original.dims != reconstructed.dims || original.dtype != reconstructed.dtype)
        throw std::invalid_argument("verification fields must have matching shapes and types");
    const auto resolved = frontend::resolve_pointwise(original.dtype, original.data, bytes,
                                                      pipeline::compile_point_request(request), threads);
    return backend::verify_pointwise(original.dtype, original.data, reconstructed.data, bytes, resolved.request,
                                     threads);
} catch (const std::bad_alloc&) {
    throw;
} catch (const std::invalid_argument& error) {
    throw Error(ErrorCode::InvalidArgument, error.what());
} catch (const std::overflow_error& error) {
    throw Error(ErrorCode::InvalidArgument, error.what());
} catch (const std::exception& error) {
    throw Error(ErrorCode::Internal, error.what());
}

}  // namespace qoiw
