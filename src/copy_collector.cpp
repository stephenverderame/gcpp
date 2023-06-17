#include "copy_collector.inl"

#include <cstddef>
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
#include "gc_base.h"
#include "gc_scan.h"

template <gcpp::CollectorLockingPolicy L>
uint8_t gcpp::CopyingCollector<L>::get_space_num(const FatPtr& ptr) const
{
    // safe w/o lock bc we never reallocate m_spaces
    const auto addr = ptr.as_ptr();
    for (uint8_t i = 0; i < 2; ++i) {
        if (addr >= m_spaces[i].data() &&
            addr < m_spaces[i].data() + m_spaces[i].size()) {
            return i;
        }
    }
    throw std::runtime_error("Collector does not manage given ptr");
}

template <gcpp::CollectorLockingPolicy L>
size_t gcpp::CopyingCollector<L>::free_space() const noexcept
{
    // safe w/o lock (never update m_spaces)
    return m_spaces[m_space_num].size() - m_next;
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
FatPtr gcpp::CopyingCollector<L>::alloc(const size_t size,
                                        std::align_val_t alignment)
{
    if (size == 0) {
        throw std::bad_alloc();
    }
    auto lk = m_lock.lock();
    const auto alignment_bytes =
        calc_alignment_bytes(&m_spaces[m_space_num][m_next], alignment);

    if (size + alignment_bytes > free_space()) {
        collect();
        if (size + alignment_bytes > free_space()) {
            throw std::bad_alloc();
        }
    }
    auto ptr = FatPtr{reinterpret_cast<uintptr_t>(
        &m_spaces[m_space_num][m_next + alignment_bytes])};
    // store size + padding in heap
    m_metadata.emplace(ptr, MetaData{.size = size, .alignment = alignment});
    m_next += size + alignment_bytes;
    (void)lk;
    return ptr;
}
template <gcpp::CollectorLockingPolicy L>
FatPtr gcpp::CopyingCollector<L>::copy(uint8_t to_space,
                                       const FatPtr& ptr) noexcept
{
    if (get_space_num(ptr) == to_space) {
        // root is in to_space
        return ptr;
    }
    const auto old_data =
        m_lock.do_concurrent([this, ptr]() { return m_metadata.at(ptr); });
    auto new_obj = alloc(old_data.size, old_data.alignment);
    memcpy(new_obj, ptr, old_data.size);
    auto lk = m_lock.lock();
    m_metadata.erase(ptr);
    (void)lk;
    return new_obj;
}
template <gcpp::CollectorLockingPolicy L>
void gcpp::CopyingCollector<L>::forward_ptr(
    uint8_t to_space, FatPtr& ptr, std::unordered_map<FatPtr, FatPtr>& visited)
{
    std::stack<std::reference_wrapper<FatPtr>> stack;
    stack.emplace(ptr);
    while (!stack.empty()) {
        auto p = stack.top();
        stack.pop();
        if (visited.contains(p)) {
            p.get().atomic_update(visited.at(p));
            continue;
        } else if (m_lock.do_concurrent(
                       [this, p]() { return !m_metadata.contains(p); }) ||
                   get_space_num(p) == to_space) {
            continue;
        }
        {
            auto meta_data =
                m_lock.do_concurrent([this, p]() { return m_metadata.at(p); });
            scan_memory(static_cast<uintptr_t>(p.get()),
                        static_cast<uintptr_t>(p.get()) + meta_data.size,
                        [&stack](auto ptr) { stack.emplace(*ptr); });
        }
        // meta_data& is invalidated by copy
        auto new_ptr = copy(to_space, p);
        visited.emplace(p, new_ptr);
        p.get().atomic_update(new_ptr);
    }
}

template <gcpp::CollectorLockingPolicy Lock>
void gcpp::CopyingCollector<Lock>::collect() noexcept
{
    // TODO
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    (void)collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
}

template class gcpp::CopyingCollector<gcpp::SerialGCPolicy>;
template class gcpp::CopyingCollector<gcpp::ConcurrentGCPolicy>;
