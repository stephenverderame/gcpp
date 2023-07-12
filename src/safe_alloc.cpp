#include "safe_alloc.h"

#include <mutex>

#include "concurrent_gc.h"
#include "copy_collector.h"
#include "gc_scan.h"
using collector_t = gcpp::CopyingCollector<gcpp::ConcurrentGCPolicy, gcpp::FinalGenerationPolicy>;
constexpr uintptr_t heap_size = 51200;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static collector_t g_collector(heap_size);

FatPtr gcpp::GC::alloc(size_t size, std::align_val_t alignment)
{
    if (g_collector.free_space() < size) {
        collect();
        if (g_collector.free_space() < size) {
            throw std::bad_alloc();
        }
    }
    return g_collector.alloc(size, alignment);
}

void gcpp::GC::collect() noexcept
{
    GC_UPDATE_STACK_RANGE();
    g_collector.collect();
}

std::unique_lock<std::mutex> gcpp::test_lock()
{
    return g_collector.test_lock();
}