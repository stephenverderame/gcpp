#include "safe_alloc.h"

#include "copy_collector.inl"
#include "gc_scan.h"
using collector_t = gcpp::CopyingCollector;
constexpr uintptr_t heap_size = 51200;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static thread_local collector_t g_collector(heap_size, std::nullopt);

FatPtr gcpp::alloc(size_t size, std::align_val_t alignment)
{
    if (g_collector.free_space() < size) {
        collect();
        if (g_collector.free_space() < size) {
            throw std::bad_alloc();
        }
    }
    return g_collector.alloc(size, alignment);
}

void gcpp::collect()
{
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    g_collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
}