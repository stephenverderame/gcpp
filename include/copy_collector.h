#pragma once
#include <array>
#include <new>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "collector.h"
#include "concurrent_gc.h"
#include "gc_base.h"

namespace gcpp
{
template <CollectorLockingPolicy LockPolicy>
class CopyingCollector
{
    using MemStore = std::vector<std::byte, AlignedAllocator<std::byte>>;

  private:
    /** The two spaces of the heap */
    std::array<MemStore, 2> m_spaces;
    /** Next index to allocate an object */
    typename LockPolicy::gc_size_t m_next = 0;
    /** Next index of object to move onto the to space */
    // size_t m_scan = 0;
    /** Index of the space in which we allocate new objects */
    typename LockPolicy::gc_uint8_t m_space_num = 0;
    std::unordered_map<FatPtr, MetaData> m_metadata;
    LockPolicy m_lock;

  public:
    /**
     * @brief Collector static interface
     * @see Collector
     * @{
     */
    explicit CopyingCollector(size_t size)
        : m_spaces({MemStore(size >> 1), MemStore(size >> 1)}),
          m_metadata(m_spaces[0].size() / 4)
    {
        if (size >= ptr_mask) {
            throw std::runtime_error("Heap size too large");
        }
    }

    [[nodiscard]] FatPtr alloc(size_t size,
                               std::align_val_t alignment = std::align_val_t{
                                   1});

    template <std::ranges::range T>
    requires std::same_as<std::ranges::range_reference_t<T>, FatPtr&>
    std::vector<FatPtr> collect(T&& roots) noexcept;

    [[nodiscard]] bool contains(void* ptr) const noexcept;

    [[nodiscard]] size_t free_space() const noexcept;
    /** @} */

  private:
    /**
     * @brief Copies the object pointed to by `ptr` to the other space
     *
     * @param ptr pointer to object to copy
     * @return FatPtr pointer to the copy of the object
     */
    [[nodiscard]] FatPtr copy(uint8_t to_space, const FatPtr& ptr) noexcept;

    /**
     * @brief Forwards a pointer to the other space
     * Forwards the pointer and all members via depth-first traversal
     *
     * @param ptr pointer to forward
     * @param visited set of pointers that have already been forwarded (black
     * nodes in the graph)
     */
    void forward_ptr(uint8_t to_space, FatPtr& ptr,
                     std::unordered_map<FatPtr, FatPtr>& visited);

    /**
     * @brief Get the space num a pointer belongs to
     *
     * @param ptr
     * @return uint8_t 0 or 1, depending on which space `ptr` belongs in
     * @throws `std::runtime_error` if `ptr` does not belong to any space
     */
    uint8_t get_space_num(const FatPtr& ptr) const;
};

static_assert(Collector<CopyingCollector<SerialGCPolicy>>);
static_assert(Collector<CopyingCollector<ConcurrentGCPolicy>>);
}  // namespace gcpp
