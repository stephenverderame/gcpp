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

inline ptr_size_t get_heap_idx(const FatPtr& ptr) noexcept
{
    return (ptr.get_gc_ptr().ptr >> 1) - 8;
}
/**
 * @brief Get the size of an object in the heap using the dynamic size format
 *
 * @param space current heap
 * @param ptr pointer to the object
 * @return tuple containing the size of the object and the index of the first
 * byte of the object in the heap
 */
template <class Alloc>
inline auto get_data_and_idx(const std::vector<std::byte, Alloc>& space,
                             const FatPtr& ptr)
{
    size_t size = 0;
    size_t shift = 0;
    auto idx = get_heap_idx(ptr);
    do {
        size |= (static_cast<size_t>(space[idx]) & 127) << shift;
        shift += 7;
    } while ((static_cast<uint8_t>(space[idx++]) & 128) != 0);
    // skip padding
    uint8_t padding_bytes = 1;
    while (space[idx] != static_cast<std::byte>(0)) {
        ++padding_bytes;
        ++idx;
    }
    return std::make_tuple(
        gcpp::MetaData{
            .size = size,
            .alignment = static_cast<std::align_val_t>(padding_bytes)},
        idx + 1);
}
}  // namespace

/** Gets the space number that `ptr` belongs to */
inline uint8_t get_space_num(const FatPtr& ptr) noexcept
{
    return ptr.get_gc_ptr().ptr & 1;
}

std::tuple<gcpp::MetaData, void*> gcpp::CopyingCollector::access_with_data(
    const FatPtr& ptr) noexcept
{
    auto& space = m_spaces[get_space_num(ptr)];
    auto [data, idx] = get_data_and_idx(space, ptr);
    return std::make_tuple(data, &space[idx]);
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
    ptr_size_t idx = get_heap_idx(ptr);
    while ((static_cast<uint8_t>(space[idx]) & 0b10000000) != 0) {
        ++idx;
    }
    // idx now points to last byte of size
    ++idx;
    while (space[idx] != static_cast<std::byte>(0)) {
        ++idx;
    }
    // idx now points to last byte of padding
    return &space[idx + 1];
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
    uint8_t alignment_bytes = 1;
    while ((reinterpret_cast<size_t>(cur_ptr + alignment_bytes) &
            (static_cast<size_t>(alignment) - 1)) != 0) {
        ++alignment_bytes;
    }
    return alignment_bytes;
}

/**
 * @brief Set the metadata of an object in the heap (size and padding)
 *
 * @param space_ptr
 * @param size_buf
 * @param size_bytes
 * @param alignment_bytes
 */
void set_metadata(std::byte* space_ptr, const std::array<uint8_t, 10>& size_buf,
                  uint8_t size_bytes, uint8_t alignment_bytes)
{
    memcpy(space_ptr, size_buf.data(), size_bytes);
    if (alignment_bytes > 1) {
        memset(space_ptr + size_bytes, 1, alignment_bytes - 1);
    }
    space_ptr[size_bytes + alignment_bytes - 1] = static_cast<std::byte>(0);
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
    std::array<uint8_t, 10> size_buf;
    uint8_t size_bytes = 1;
    auto size_left = size;
    do {
        size_buf[size_bytes - 1] = size_left & 127;
        size_left >>= 7;
    } while (size_left > 0 && (size_buf[size_bytes++ - 1] |= 128) != 0);

    const auto alignment_bytes = calc_alignment_bytes(
        &m_spaces[m_space_num][m_next + size_bytes], alignment);

    if (size + size_bytes + alignment_bytes > free_space()) {
        throw std::bad_alloc();
    }
    auto ptr = FatPtr((m_next + 8) << 1 | m_space_num);
    // store size + padding in heap
    set_metadata(&m_spaces[m_space_num][m_next], size_buf, size_bytes,
                 alignment_bytes);
    m_next += size + size_bytes + alignment_bytes;
    return ptr;
}

FatPtr gcpp::CopyingCollector::copy(const FatPtr& ptr) noexcept
{
    if (get_space_num(ptr) == m_space_num) {
        // root is in to_space
        return ptr;
    }
    auto [old_data, old_obj_data] = access_with_data(ptr);
    auto new_obj = alloc(old_data.size, old_data.alignment);
    memcpy(access(new_obj), old_obj_data, old_data.size);
    return new_obj;
}

void gcpp::CopyingCollector::forward_ptr(
    FatPtr& ptr, std::unordered_map<FatPtr, FatPtr>& visited)
{
    if (visited.contains(ptr)) {
        ptr = visited.at(ptr);
        return;
    }
    std::stack<std::reference_wrapper<FatPtr>> stack;
    stack.emplace(ptr);
    while (!stack.empty()) {
        auto p = stack.top();
        stack.pop();
        if (visited.contains(p) || get_space_num(p) == m_space_num) {
            continue;
        }
        auto [meta_data, data_ptr] = access_with_data(p);
        scan_memory(reinterpret_cast<ptr_size_t>(data_ptr),
                    reinterpret_cast<ptr_size_t>(data_ptr) + meta_data.size,
                    [&stack](auto ptr) { stack.emplace(*ptr); });
        auto new_ptr = copy(p);
        visited.emplace(p, new_ptr);
        p.get() = new_ptr;
    }
}
