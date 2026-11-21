#include <SZ3/api/sz.hpp>

int main() {
    // Exercises the public headers, generated configuration and transitive zstd linkage.
    const SZ3::Config config(8, 16);
    return SZ3::SZ_compress_size_bound<float>(config) >= 128 * sizeof(float) ? 0 : 1;
}
