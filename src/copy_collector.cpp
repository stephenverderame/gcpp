#include "copy_collector.h"

#include <bits/types/siginfo_t.h>
#include <sys/mman.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <stack>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "collector.h"
#include "concurrent_gc.h"
#include "copy_collector.h"
#include "debug_thread_counter.h"
#include "gc_base.h"
#include "gc_scan.h"
#include "generational_gc.h"
#include "mem_prot.h"

/*
I've been thinking for a bit on how best to implement a concurrent copying
collector that is relatively efficient with short pause times.

The main issue is synchronization of copying and flipping of pointers.
One approach was Nettles' replication-based approach, but the problem is we
need to, on each write, check if there is a forwarding pointer and if so, write
to the replica as well as the original. This is less overhead than using locks
but the problem is:
- mutators gets a reference to the original
- collector sets the forwarding pointer
- mutator still has the reference to the original and invokes a long, mutating
operation on it

The Sapphire algorithm is another option, but I think it suffers a similar
issue.

The general issue is I don't think we can install an access barrier on the
object data without compiler support. We can do them on the pointers themselves
via copy/move constructors and copy/move assignments, but not on the object
data.

Problem with locks is that they might deadlock.
*/

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

template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
gcpp::SpaceNum gcpp::CopyingCollector<L, G>::get_space_num(
    const FatPtr& ptr) const
{
    // safe w/o lock bc we never reallocate m_spaces
    const auto addr = ptr.as_ptr();
    for (uint8_t i = 0; i < 2; ++i) {
        if (addr >= m_spaces[i].get() &&
            addr < m_spaces[i].get() + m_heap_size) {
            return SpaceNum{i};
        }
    }
    throw std::runtime_error("Collector does not manage given ptr");
}

template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
size_t gcpp::CopyingCollector<L, G>::free_space() const noexcept
{
    // safe w/o lock (never update m_spaces)
    const auto sp_num = load(m_space_num);
    const auto next = load(m_nexts[sp_num]);
    if (next >= m_max_alloc_size) {
        return 0;
    }
    return m_max_alloc_size - next;
}

template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
bool gcpp::CopyingCollector<L, G>::contains(void* ptr) const noexcept
{
    // safe w/o lock
    return (ptr >= m_spaces[0].get() &&
            ptr < m_spaces[0].get() + m_heap_size) ||
           (ptr >= m_spaces[1].get() && ptr < m_spaces[1].get() + m_heap_size);
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
template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
FatPtr gcpp::CopyingCollector<L, G>::alloc_no_constraints(
    SpaceNum to_space_num, const MetaData& meta_data, size_t index)
{
    if (meta_data.size + index >= m_heap_size) {
        throw std::bad_alloc();
    }
    auto ptr = FatPtr{reinterpret_cast<uintptr_t>(
        &m_spaces[static_cast<uint8_t>(to_space_num)][index])};
    [[maybe_unused]] auto lk = m_lock.lock();
    m_metadata.emplace(ptr, meta_data);
    m_gen_policy.init(ptr);
    return ptr;
}
template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
FatPtr gcpp::CopyingCollector<L, G>::alloc_attempt(const size_t size,
                                                   std::align_val_t alignment,
                                                   uint8_t attempts)
{
    const auto [to_space, alloc_index] = [this, alignment, size]() {
        [[maybe_unused]] auto lk = m_lock.lock();
        const auto to = SpaceNum{load(m_space_num)};
        const auto index = reserve_space(size, to, alignment, m_max_alloc_size);
        check_overlapping_alloc(index, to, size);
        return std::make_tuple(to, index);
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
template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
std::optional<size_t> gcpp::CopyingCollector<L, G>::reserve_space(
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
template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
FatPtr gcpp::CopyingCollector<L, G>::copy(FatPtr& to_update, SpaceNum to_space,
                                          const FatPtr& ptr)
{
    {
        [[maybe_unused]] auto lk = m_lock.lock();
        if (get_space_num(ptr) == to_space) {
            // root is in to_space
            return ptr;
        }
    }
    const auto old_data =
        m_lock.do_with_lock([this, ptr]() { return m_metadata.at(ptr); });
    auto index =
        reserve_space(old_data.size, to_space, old_data.alignment, m_heap_size);
    m_lock.do_with_lock([this, index, to_space, size = old_data.size]() {
        check_overlapping_alloc(index, to_space, size);
    });
    if (!index) {
        throw std::bad_alloc();
    }
    auto new_obj = alloc_no_constraints(to_space, old_data, index.value());
    // ISSUE: ptr object data could be updated during the memcpy
    {
        // auto lk2 = std::unique_lock{m_test_mu};
        // SANITY CHECK
        // auto mem_lock = region_readonly(ptr, old_data.size);
        seq_cst_cpy(new_obj, ptr, old_data.size);
        to_update.compare_exchange(ptr, new_obj);
    }
    [[maybe_unused]] auto lk = m_lock.lock();
    m_metadata.erase(ptr);
    return new_obj;
}
template <gcpp::CollectorLockingPolicy L, gcpp::GCGenerationPolicy G>
void gcpp::CopyingCollector<L, G>::forward_ptr(
    SpaceNum to_space, FatPtr& ptr, std::unordered_map<FatPtr, FatPtr>& visited)
{
    std::stack<std::reference_wrapper<FatPtr>> stack;
    stack.emplace(ptr);
    while (!stack.empty()) {
        auto p = stack.top();
        auto maybe_ptr_val = FatPtr::test_ptr(&p.get());
        stack.pop();
        if (!maybe_ptr_val) {
            continue;
        }
        auto ptr_val = maybe_ptr_val.value();
        if (visited.contains(ptr_val)) {
            p.get().compare_exchange(ptr_val, visited.at(ptr_val));
            continue;
        } else if (m_lock.do_with_lock([this, ptr_val, to_space]() {
                       return !m_metadata.contains(ptr_val) ||
                              get_space_num(ptr_val) == to_space;
                   })) {
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
        const auto need_promotion = m_lock.do_with_lock(
            [this, ptr_val]() { return m_gen_policy.need_promotion(ptr_val); });
        auto new_ptr = need_promotion ? copy(p.get(), to_space, ptr_val) :
            m_lock.do_with_lock([this, ptr_val]() {
                return m_gen_policy.promote(ptr_val, m_metadata.at(ptr_val));
            });
        visited.emplace(ptr_val, new_ptr);
    }
}

template <gcpp::CollectorLockingPolicy LockPolicy, gcpp::GCGenerationPolicy G>
std::future<std::vector<FatPtr>>
gcpp::CopyingCollector<LockPolicy, G>::async_collect(
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
                            const auto opt = FatPtr::test_ptr(ptr);
                            return opt && contains(opt.value());
                        })) {
            forward_ptr(to_space, *it, visited);
        }
        m_lock.do_with_lock([this, &visited, to_space]() {
            std::vector<FatPtr> to_remove = {};
            for (auto& [ptr, _] : m_metadata) {
                if (get_space_num(ptr) != to_space && !visited.contains(ptr)) {
                    to_remove.push_back(ptr);
                }
            }
            for (auto ptr : to_remove) {
                m_metadata.erase(ptr);
                m_gen_policy.collected(ptr);
            }
        });
        return promoted;
    });
}

template <gcpp::CollectorLockingPolicy Lock, gcpp::GCGenerationPolicy G>
void gcpp::CopyingCollector<Lock, G>::collect(size_t needed_space) noexcept
{
    while (m_collect_result.valid() &&
           m_collect_result.wait_for(std::chrono::seconds(0)) ==
               std::future_status::timeout &&
           free_space() < needed_space) {
        m_collect_result.wait();
    }
    [[maybe_unused]] auto lk = m_lock.lock();
    if (free_space() < needed_space &&
        (!m_collect_result.valid() ||
         m_collect_result.wait_for(std::chrono::seconds(0)) ==
             std::future_status::ready)) {
        m_collect_result = async_collect({});
    }
}

template <gcpp::CollectorLockingPolicy Lock, gcpp::GCGenerationPolicy G>
FatPtr gcpp::CopyingCollector<Lock, G>::alloc(size_t size,
                                              std::align_val_t alignment)
{
    GC_UPDATE_STACK_RANGE_NESTED_1();
    if (size == 0 || size > m_max_alloc_size) {
        throw std::bad_alloc();
    }
    return alloc_attempt(size, alignment, 0);
}

template <gcpp::CollectorLockingPolicy Lock, gcpp::GCGenerationPolicy G>
void gcpp::CopyingCollector<Lock, G>::check_overlapping_alloc(
    const std::optional<size_t>& index, SpaceNum space, size_t size) const
{
    for (auto& [f_ptr, data] : m_metadata) {
        const auto addr = &m_spaces[static_cast<size_t>(space)][index.value()];
        const auto ptr_v = f_ptr.as_ptr();
        if ((ptr_v <= addr && ptr_v + data.size > addr) ||
            (addr <= ptr_v && addr + size > ptr_v)) {
            throw std::runtime_error("Heap corruption");
        }
    }
}

template class gcpp::CopyingCollector<gcpp::SerialGCPolicy,
                                      gcpp::FinalGenerationPolicy>;
template class gcpp::CopyingCollector<gcpp::ConcurrentGCPolicy,
                                      gcpp::FinalGenerationPolicy>;
