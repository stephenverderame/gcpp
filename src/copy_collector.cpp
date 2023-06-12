#include "copy_collector.inl"

#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <stack>
#include <tuple>
#include <unordered_map>

#include "copy_collector.h"
#include "gc_base.h"

/*  The least signficant bit denotes which space the pointer is in

*/
namespace
{

inline uintptr_t get_heap_idx(const FatPtr& ptr) noexcept
{
    return (ptr.get_gc_ptr().ptr >> 1) - 8;
}

}  // namespace

/** Gets the space number that `ptr` belongs to */
inline uint8_t get_space_num(const FatPtr& ptr) noexcept
{
    return ptr.get_gc_ptr().ptr & 1;
}

size_t gcpp::CopyingCollector::free_space() const noexcept
{
    return m_spaces[m_space_num].size() - m_next;
}

bool gcpp::CopyingCollector::contains(void* ptr) const noexcept
{
    return (ptr >= m_spaces[0].data() &&
            ptr < m_spaces[0].data() + m_spaces[0].size()) ||
           (ptr >= m_spaces[1].data() &&
            ptr < m_spaces[1].data() + m_spaces[1].size());
}

void* gcpp::CopyingCollector::access(const FatPtr& ptr) noexcept
{
    auto& space = m_spaces[get_space_num(ptr)];
    // strip space idx bit
    uintptr_t idx = get_heap_idx(ptr);
    return &space[idx];
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

/*  Dynamic Size Format
    Little Endian (least significant byte first)
    MSB is 1 if there are more bytes to read

    |1..|  <- GC idx | first size byte
    |1..|  <- size
      .
      .
      .
    |0..|  <- size
    |..1|  <- alignment
      .
      .
      .
    |000|  <- last alignment
    |...|  <- object data
      .
      .
      .
*/
FatPtr gcpp::CopyingCollector::alloc(const size_t size,
                                     std::align_val_t alignment)
{
    if (size == 0) {
        throw std::bad_alloc();
    }
    const auto alignment_bytes =
        calc_alignment_bytes(&m_spaces[m_space_num][m_next], alignment);

    if (size + alignment_bytes > free_space()) {
        throw std::bad_alloc();
    }
    auto ptr = FatPtr((m_next + alignment_bytes + 8) << 1 | m_space_num);
    // store size + padding in heap
    m_metadata.emplace(ptr, MetaData{.size = size, .alignment = alignment});
    m_next += size + alignment_bytes;
    return ptr;
}

FatPtr gcpp::CopyingCollector::copy(const FatPtr& ptr) noexcept
{
    if (get_space_num(ptr) == m_space_num) {
        // root is in to_space
        return ptr;
    }
    const auto old_data = m_metadata.at(ptr);
    auto old_obj_data = access(ptr);
    auto new_obj = alloc(old_data.size, old_data.alignment);
    memcpy(access(new_obj), old_obj_data, old_data.size);
    m_metadata.erase(ptr);
    return new_obj;
}

void gcpp::CopyingCollector::forward_ptr(
    FatPtr& ptr, std::unordered_map<FatPtr, FatPtr>& visited)
{
    std::stack<std::reference_wrapper<FatPtr>> stack;
    stack.emplace(ptr);
    while (!stack.empty()) {
        auto p = stack.top();
        stack.pop();
        if (visited.contains(p)) {
            p.get() = visited.at(p);
            continue;
        } else if (get_space_num(p) == m_space_num || !m_metadata.contains(p)) {
            continue;
        }
        {
            auto& meta_data = m_metadata.at(p);
            auto data_ptr = access(p);
            scan_memory(reinterpret_cast<uintptr_t>(data_ptr),
                        reinterpret_cast<uintptr_t>(data_ptr) + meta_data.size,
                        [&stack](auto ptr) { stack.emplace(*ptr); });
        }
        // meta_data& is invalidated by copy
        auto new_ptr = copy(p);
        visited.emplace(p, new_ptr);
        p.get() = new_ptr;
    }
}
