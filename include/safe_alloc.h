#pragma once
#include <mutex>

#include "gc_base.h"
namespace gcpp
{

template <typename T>
concept GCFrontEnd = requires(T a) {
    {
        T::collect()
    } noexcept;
    {
        T::alloc(std::declval<size_t>(), std::declval<std::align_val_t>())
    } -> std::same_as<FatPtr>;
};

struct GC {
    static FatPtr alloc(size_t size,
                        std::align_val_t alignment = std::align_val_t{1});
    static void collect() noexcept;
};

[[nodiscard]] std::unique_lock<std::mutex> test_lock();

static_assert(GCFrontEnd<GC>);

}  // namespace gcpp