#include "safe_alloc.h"

#include "concurrent_gc.h"
#include "copy_collector.h"
#include "gc_scan.h"
using collector_t = gcpp::CopyingCollector<gcpp::SerialGCPolicy>;
constexpr uintptr_t heap_size = 51200;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static collector_t g_collector(heap_size);

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
    g_collector.async_collect(roots);
}