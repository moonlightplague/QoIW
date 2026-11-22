#pragma once

#include <algorithm>
#include <cstddef>
#include <exception>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace qoiw::common {

// Each iteration owns its output. Exceptions must leave the OpenMP region on
// the calling thread. A zero thread count uses the runtime's default.
template <class Work>
void parallel_for(std::size_t count, int threads, Work work) {
#ifdef _OPENMP
    const auto limit = threads ? threads : omp_get_max_threads();
    const auto workers = static_cast<int>(std::min<std::size_t>(count, limit));
    if (workers > 1) {
        std::exception_ptr failure;
#pragma omp parallel for schedule(static) num_threads(workers)
        for (std::size_t i = 0; i < count; ++i) {
            try {
                work(i);
            } catch (...) {
#pragma omp critical(qoiw_parallel_exception)
                {
                    if (!failure) failure = std::current_exception();
                }
            }
        }
        if (failure) std::rethrow_exception(failure);
        return;
    }
#else
    (void)threads;
#endif
    for (std::size_t i = 0; i < count; ++i) work(i);
}

}  // namespace qoiw::common
