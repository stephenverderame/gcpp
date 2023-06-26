#include "mem_prot.h"

#include <unistd.h>

#include <array>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstring>
#include <mutex>
#include <new>
#include <sstream>
#include <utility>
namespace
{
std::array<std::pair<const void*, const void*>, 128> g_heaps;
std::atomic<int> g_heap_count = 0;

void segfault_handler(int, siginfo_t* si, void*)
{
    const int heap_counts = g_heap_count;
    for (int i = 0; i < heap_counts; ++i) {
        const auto& heap = g_heaps[static_cast<size_t>(i)];
        if (si->si_addr >= heap.first && si->si_addr < heap.second) {
            return;
        }
    }
    abort();
}

bool is_segfault_handler_registered()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    assert(sigaction(SIGSEGV, nullptr, &sa) == 0);
    return sa.sa_sigaction == segfault_handler;
}

void* page_aligned_floor(const void* addr)
{
    return reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(addr) &
        ~static_cast<uintptr_t>(gcpp::page_size() - 1));
}

// void* page_aligned_ceil(const void* addr)
// {
//     return reinterpret_cast<void*>(
//         (reinterpret_cast<uintptr_t>(addr) +
//          static_cast<uintptr_t>(gcpp::page_size()) - 1) &
//         ~static_cast<uintptr_t>(gcpp::page_size() - 1));
// }
}  // namespace

int gcpp::page_size()
{
    static int page_size = 4096;
    static std::once_flag page_size_flag;
    std::call_once(page_size_flag, []() { page_size = getpagesize(); });
    return page_size;
}

void gcpp::register_heap(const void* start, size_t len)
{
    const void* end = static_cast<const uint8_t*>(start) + len;
    for (size_t i = 0; static_cast<int>(i) < g_heap_count; ++i) {
        if (g_heaps[i].first == start && g_heaps[i].second == end) {
            return;
        }
    }
    auto idx = static_cast<size_t>(g_heap_count++);
    g_heaps.at(idx) = std::make_pair(start, end);
    if (!is_segfault_handler_registered()) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = segfault_handler;
        assert(sigaction(SIGSEGV, &sa, nullptr) == 0);
    }
}

size_t gcpp::page_size_ceil(size_t size)
{
    const auto page_size = static_cast<size_t>(gcpp::page_size());
    return ((size + page_size - 1) / page_size) * page_size;
}

gcpp::RegionProtection::RegionProtection(void* start, const void* end,
                                         ProtectionMode mode)
    : m_region_start(page_aligned_floor(start)),
      m_region_size(
          static_cast<size_t>(reinterpret_cast<uintptr_t>(end) -
                              reinterpret_cast<uintptr_t>(m_region_start))),
      m_old_prot(ProtectionMode::ReadWrite),
      m_new_prot(mode)
{
    lock();
}

void gcpp::RegionProtection::lock() const
{
    if (m_region_start != nullptr) {
        if (!m_locked && mprotect(m_region_start, m_region_size,
                                  static_cast<int>(m_new_prot)) != 0) {
            std::stringstream ss;
            ss << "Could not protect region at " << std::hex << m_region_start
               << " with size " << std::dec << m_region_size
               << ". Error code: " << errno;
            throw std::runtime_error(ss.str());
        }
        m_locked = true;
    }
}

void gcpp::RegionProtection::unlock() const
{
    if (m_region_start != nullptr) {
        if (m_locked && mprotect(m_region_start, m_region_size,
                                 static_cast<int>(m_old_prot)) != 0) {
            std::stringstream ss;
            ss << "Could not unprotect region at " << std::hex << m_region_start
               << " with size " << std::dec << m_region_size
               << ". Error code: " << errno;
            throw std::runtime_error(ss.str());
        }
        m_locked = false;
    }
}

gcpp::RegionProtection::~RegionProtection() { unlock(); }

std::align_val_t gcpp::page_size_align()
{
    return static_cast<std::align_val_t>(gcpp::page_size());
}

gcpp::RegionProtection::RegionProtection(RegionProtection&& other) noexcept
    : m_region_start(other.m_region_start),
      m_region_size(other.m_region_size),
      m_old_prot(other.m_old_prot),
      m_new_prot(other.m_new_prot),
      m_locked(other.m_locked)
{
    other.m_locked = false;
    other.m_region_start = nullptr;
}

gcpp::RegionProtection& gcpp::RegionProtection::operator=(
    RegionProtection&& other) noexcept
{
    auto tmp = std::move(other);
    std::swap(*this, tmp);
    return *this;
}

gcpp::RegionProtection gcpp::region_readonly(void* start, size_t len)
{
    return {start, static_cast<const uint8_t*>(start) + len,
            ProtectionMode::ReadOnly};
}

gcpp::RegionProtection gcpp::region_writeonly(void* start, size_t len)
{
    return {start, static_cast<const uint8_t*>(start) + len,
            ProtectionMode::WriteOnly};
}
