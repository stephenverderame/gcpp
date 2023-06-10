#include "copy_collector.h"

#include <cstring>
#include <new>
#include <tuple>

#include "gc_base.h"

/*  The least signficant bit denotes which space the pointer is in

*/
namespace
{
/**
 * @brief Get the size of an object in the heap using the dynamic size format
 *
 * @param space current heap
 * @param idx index to object in heap
 * @return tuple containing the size of the object and the index of the first
 * byte of the object
 */
inline auto get_size(const std::vector<std::byte>& space, size_t idx)
{
    size_t size = 0;
    size_t shift = 0;
    do {
        size |= (static_cast<size_t>(space[idx++]) & 127) << shift;
        shift += 7;
    } while ((static_cast<uint8_t>(space[idx - 1]) & 128) != 0);
    return std::make_tuple(size, idx);
}

/**
 * @brief Gets the index of the other space
 */
inline unsigned char swap_space_idx(unsigned char space_idx) noexcept
{
    return space_idx == 0 ? 1 : 0;
}
}  // namespace

inline unsigned get_space_idx(const FatPtr& ptr) noexcept
{
    return ptr.ptr & 1;
}

size_t gcpp::CopyingCollector::free_space() const noexcept
{
    return m_spaces[m_space_idx].size() - m_next;
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
    auto& space = m_spaces[get_space_idx(ptr)];
    ptr_size_t idx = ptr.get_gc_ptr() & ~static_cast<ptr_size_t>(1);
    while ((static_cast<uint8_t>(space[idx]) & 127) != 0) {
        ++idx;
    }
    // idx now points to last byte of size
    return &space[idx + 1];
}

FatPtr gcpp::CopyingCollector::alloc(size_t size)
{
    /*  Dynamic Size Format
        Little Endian (least significant byte first)
        MSB is 1 if there are more bytes to read
    */
    std::array<uint8_t, 10> size_buf;
    uint8_t size_bytes = 1;
    do {
        size_buf[size_bytes - 1] = size & 127;
        size >>= 7;
    } while (size > 0 && (size_buf[size_bytes++ - 1] |= 128) != 0);
    if (size + size_bytes > free_space() || size == 0) {
        throw std::bad_alloc();
    }
    auto ptr = FatPtr(m_next << 1 | m_space_idx);
    memcpy(&m_spaces[m_space_idx][m_next], size_buf.data(), size_bytes);
    m_next += size + size_bytes;
    return ptr;
}

std::vector<FatPtr> gcpp::CopyingCollector::collect(
    std::vector<FatPtr>& roots) noexcept
{
    std::vector<FatPtr> promoted;
    m_space_idx = swap_space_idx(m_space_idx);
    m_next = 0;
    for (auto& root : roots) {
        root.ptr = copy(root).ptr;
    }
    // TODO: promotions
    // maybe use a callback to the user to determine if an object should be
    // promoted
    return promoted;
}

FatPtr gcpp::CopyingCollector::copy(const FatPtr& ptr) noexcept
{
    if (get_space_idx(ptr) == m_space_idx) {
        // root is in to_space
        return ptr;
    }
    auto root_ptr = ptr.get_gc_ptr();
    auto [root_obj_size, root_obj_idx] =
        get_size(m_spaces[get_space_idx(ptr)], root_ptr);
    auto new_obj = alloc(root_obj_size);
    memcpy(access(new_obj), access(ptr), root_obj_size);
    return new_obj;
}
