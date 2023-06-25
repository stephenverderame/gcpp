#include "copy_collector.h"

#include <bits/types/siginfo_t.h>
#include <sys/mman.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <stack>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "collector.h"
#include "copy_collector.h"
#include "debug_thread_counter.h"
#include "gc_base.h"
#include "gc_scan.h"
#include "mem_prot.h"

namespace
{

template <typename T>
inline T load(T a)
{
    return a;
}
template <typename T>
inline T load(const std::atomic<T>& a)
{
    return a.load();
}

template <typename T>
inline T fetch_add(std::atomic<T>& a, T b)
{
    return a.fetch_add(b);
}

template <typename T>
inline T fetch_add(T& a, T b)
{
    auto tmp = a;
    a += b;
    return tmp;
}

template <typename T>
inline bool compare_exchange(std::atomic<T>& a, T& expected, T desired)
{
    return a.compare_exchange_strong(expected, desired);
}

template <typename T>
inline bool compare_exchange(T& a, T& expected, T desired)
{
    if (a == expected) {
        a = desired;
        return true;
    }
    expected = a;
    return false;
}

template <typename T>
inline T fetch_xor(std::atomic<T>& a, T b)
{
    return a.fetch_xor(b);
}

template <typename T>
inline T fetch_xor(T& a, T b)
{
    auto tmp = a;
    a ^= b;
    return tmp;
}
/**
 * @brief Atomically flips the given space number
 *
 * @tparam T
 * @param space_num [in/out] current space number
 * @return gcpp::SpaceNum pair of the old and new space number
 */
template <typename T>
inline std::pair<gcpp::SpaceNum, gcpp::SpaceNum> flip_space(
    T& space_num) noexcept
{
    const auto old = fetch_xor(space_num, static_cast<uint8_t>(1));
    return std::make_pair(gcpp::SpaceNum{old},
                          static_cast<gcpp::SpaceNum>(old ^ 1));
}

}  // namespace

template <gcpp::CollectorLockingPolicy L>
gcpp::SpaceNum gcpp::CopyingCollector<L>::get_space_num(const FatPtr& ptr) const
{
    // safe w/o lock bc we never reallocate m_spaces
    const auto addr = ptr.as_ptr();
    for (uint8_t i = 0; i < 2; ++i) {
        if (addr >= m_spaces[i].data() &&
            addr < m_spaces[i].data() + m_spaces[i].size()) {
            return SpaceNum{i};
        }
    }
    throw std::runtime_error("Collector does not manage given ptr");
}

template <gcpp::CollectorLockingPolicy L>
size_t gcpp::CopyingCollector<L>::free_space() const noexcept
{
    // safe w/o lock (never update m_spaces)
    const auto sp_num = load(m_space_num);
    const auto next = load(m_nexts[sp_num]);
    if (next >= m_max_alloc_size) {
        return 0;
    }
    return m_max_alloc_size - next;
}

template <gcpp::CollectorLockingPolicy L>
bool gcpp::CopyingCollector<L>::contains(void* ptr) const noexcept
{
    // safe w/o lock
    return (ptr >= m_spaces[0].data() &&
            ptr < m_spaces[0].data() + m_spaces[0].size()) ||
           (ptr >= m_spaces[1].data() &&
            ptr < m_spaces[1].data() + m_spaces[1].size());
}
/**
 * @brief Determines the number of bytes of padding required to align the
 * address to the given alignment
 *
 * @param cur_ptr pointer to the start of the object (after the size bytes)
 * @param alignment alignment of the object
 */
uint8_t calc_alignment_bytes(const std::byte* cur_ptr,
                             std::align_val_t alignment)
{
    uint8_t alignment_bytes = 0;
    while ((reinterpret_cast<size_t>(cur_ptr + alignment_bytes) &
            (static_cast<size_t>(alignment) - 1)) != 0) {
        ++alignment_bytes;
    }
    return alignment_bytes;
}
template <gcpp::CollectorLockingPolicy L>
FatPtr gcpp::CopyingCollector<L>::alloc_no_constraints(
    SpaceNum to_space_num, const MetaData& meta_data, size_t index)
{
    if (meta_data.size + index >= m_spaces[0].size()) {
        throw std::bad_alloc();
    }
    auto ptr = FatPtr{reinterpret_cast<uintptr_t>(
        &m_spaces[static_cast<uint8_t>(to_space_num)][index])};
    // store size + padding in heap
    auto lk = m_lock.lock();
    m_metadata.emplace(ptr, meta_data);
    (void)lk;
    return ptr;
}
template <gcpp::CollectorLockingPolicy L>
FatPtr gcpp::CopyingCollector<L>::alloc_attempt(const size_t size,
                                                std::align_val_t alignment,
                                                uint8_t attempts)
{
    const auto [to_space, alloc_index] = [this, alignment, size]() {
        auto lk = m_lock.lock();
        const auto to = SpaceNum{load(m_space_num)};
        return std::make_tuple(
            to, reserve_space(size, to, alignment, m_max_alloc_size));
        (void)lk;
    }();
    if (!alloc_index) {
        if (attempts < 1) {
            collect(size);
            return alloc_attempt(size, alignment, attempts + 1);
        } else {
            throw std::bad_alloc();
        }
    }
    return alloc_no_constraints(to_space, {size, alignment}, *alloc_index);
}
template <gcpp::CollectorLockingPolicy L>
std::optional<size_t> gcpp::CopyingCollector<L>::reserve_space(
    size_t size, SpaceNum to_space, std::align_val_t alignment,
    size_t max_alloc_size)
{
    auto to_space_num = static_cast<uint8_t>(to_space);
    size_t next = m_nexts[to_space_num];
    uint8_t padding_bytes = 0;
    do {
        padding_bytes =
            calc_alignment_bytes(&m_spaces[to_space_num][next], alignment);
        if (next + size + padding_bytes > max_alloc_size) {
            return std::nullopt;
        }
    } while (!compare_exchange(m_nexts[to_space_num], next,
                               next + size + padding_bytes));
    return next + padding_bytes;
}
template <gcpp::CollectorLockingPolicy L>
FatPtr gcpp::CopyingCollector<L>::copy(SpaceNum to_space, const FatPtr& ptr)
{
    if (get_space_num(ptr) == to_space) {
        // root is in to_space
        return ptr;
    }
    const auto old_data =
        m_lock.do_with_lock([this, ptr]() { return m_metadata.at(ptr); });
    auto index = reserve_space(old_data.size, to_space, old_data.alignment,
                               m_spaces[0].size());
    if (!index) {
        throw std::bad_alloc();
    }
    auto new_obj = alloc_no_constraints(to_space, old_data, index.value());
    // ISSUE: ptr object data could be updated during the memcpy
    {
        auto mem_lock = region_readonly(ptr, old_data.size);
        memcpy(new_obj, ptr, old_data.size);
    }
    auto lk = m_lock.lock();
    m_metadata.erase(ptr);
    (void)lk;
    return new_obj;
}
template <gcpp::CollectorLockingPolicy L>
void gcpp::CopyingCollector<L>::forward_ptr(
    SpaceNum to_space, FatPtr& ptr, std::unordered_map<FatPtr, FatPtr>& visited)
{
    std::stack<std::reference_wrapper<FatPtr>> stack;
    stack.emplace(ptr);
    while (!stack.empty()) {
        auto p = stack.top();
        auto ptr_val = p.get();
        stack.pop();
        if (visited.contains(ptr_val)) {
            p.get().compare_exchange(ptr_val, visited.at(ptr_val));
            continue;
        } else if (m_lock.do_with_lock([this, ptr_val]() {
                       return !m_metadata.contains(ptr_val);
                   }) ||
                   get_space_num(ptr_val) == to_space) {
            continue;
        }
        {
            auto meta_data = m_lock.do_with_lock(
                [this, ptr_val]() { return m_metadata.at(ptr_val); });
            scan_memory(static_cast<uintptr_t>(ptr_val),
                        static_cast<uintptr_t>(ptr_val) + meta_data.size,
                        [&stack](auto ptr) { stack.emplace(*ptr); });
        }
        // meta_data& is invalidated by copy
        auto new_ptr = copy(to_space, ptr_val);
        visited.emplace(ptr_val, new_ptr);
        p.get().compare_exchange(ptr_val, new_ptr);
    }
}

template <gcpp::CollectorLockingPolicy LockPolicy>
std::future<std::vector<FatPtr>>
gcpp::CopyingCollector<LockPolicy>::async_collect(
    const std::vector<FatPtr*>& extra_roots) noexcept
{
    auto tc = ThreadCounter{m_tcount, 1};
    const auto [from_space, to_space] = flip_space(m_space_num);
    m_nexts[static_cast<uint8_t>(from_space)] = 0;
    return m_lock.do_collection([this, extra_roots, to_space]() {
        std::vector<FatPtr> promoted;
        std::unordered_map<FatPtr, FatPtr> visited;
        std::vector<FatPtr*> roots;
        GC_GET_ROOTS(roots);
        roots.insert(roots.end(), extra_roots.begin(), extra_roots.end());
        for (auto* it : roots | std::views::filter([this](auto ptr) {
                            return contains(ptr->as_ptr());
                        })) {
            forward_ptr(to_space, *it, visited);
        }
        return promoted;
    });
}

template <gcpp::CollectorLockingPolicy Lock>
void gcpp::CopyingCollector<Lock>::collect(size_t needed_space) noexcept
{
    while (m_collect_result.valid() &&
           m_collect_result.wait_for(std::chrono::seconds(0)) ==
               std::future_status::timeout &&
           free_space() < needed_space) {
        m_collect_result.wait();
    }
    auto lk = m_lock.lock();
    if (free_space() < needed_space &&
        (!m_collect_result.valid() ||
         m_collect_result.wait_for(std::chrono::seconds(0)) ==
             std::future_status::ready)) {
        m_collect_result = async_collect({});
    }
    (void)lk;
}

template <gcpp::CollectorLockingPolicy Lock>
FatPtr gcpp::CopyingCollector<Lock>::alloc(size_t size,
                                           std::align_val_t alignment)
{
    {
        volatile uintptr_t caller_base_ptr = 0;
        asm("mov (%%rbp), %0" : "=r"(caller_base_ptr));
        GCRoots::get_instance().update_stack_range(caller_base_ptr);
    }
    if (size == 0 || size > m_max_alloc_size) {
        throw std::bad_alloc();
    }
    return alloc_attempt(size, alignment, 0);
}

template class gcpp::CopyingCollector<gcpp::SerialGCPolicy>;
template class gcpp::CopyingCollector<gcpp::ConcurrentGCPolicy>;
