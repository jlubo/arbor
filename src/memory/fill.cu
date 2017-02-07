#include <iostream>
#include <cstdlib>
#include <cstdint>

namespace nest {
namespace mc {
namespace memory {
namespace gpu {
    template <typename T, typename I>
    __global__
    void fill_kernel(T* v, T value, I n) {
        auto tid = threadIdx.x + blockDim.x*blockIdx.x;

        if(tid < n) {
            v[tid] = value;
        }
    }

    unsigned grid_dim(std::size_t n, unsigned block_dim) {
        return (n+block_dim-1)/block_dim;
    }

    void fill8(uint8_t* v, uint8_t value, std::size_t n) {
        unsigned block_dim = 192;
        fill_kernel<<<grid_dim(n, block_dim), block_dim>>>(v, value, n);
    };

    void fill16(uint16_t* v, uint16_t value, std::size_t n) {
        unsigned block_dim = 192;
        fill_kernel<<<grid_dim(n, block_dim), block_dim>>>(v, value, n);
    };

    void fill32(uint32_t* v, uint32_t value, std::size_t n) {
        unsigned block_dim = 192;
        fill_kernel<<<grid_dim(n, block_dim), block_dim>>>(v, value, n);
    };

    void fill64(uint64_t* v, uint64_t value, std::size_t n) {
        unsigned block_dim = 192;
        fill_kernel<<<grid_dim(n, block_dim), block_dim>>>(v, value, n);
    };
} // namespace gpu
} // namespace memory
} // namespace nest
} // namespace mc