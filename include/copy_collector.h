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
enum class SpaceNum : uint8_t { Zero = 0, One = 1 };
template <CollectorLockingPolicy LockPolicy>
class CopyingCollector
{
    using MemStore = std::vector<std::byte, AlignedAllocator<std::byte>>;

  private:
    /** The two spaces of the heap */
    std::array<MemStore, 2> m_spaces;
    /** Next index to allocate an object */
    std::array<typename LockPolicy::gc_size_t, 2> m_nexts = {0, 0};
    /** Next index of object to move onto the to space */
    // size_t m_scan = 0;
    /** Index of the space in which we allocate new objects */
    typename LockPolicy::gc_uint8_t m_space_num = 0;
    std::unordered_map<FatPtr, MetaData> m_metadata;
    /** Maximum amount of data we can externally allocate */
    size_t m_max_alloc_size;
    std::future<CollectionResultT> m_collect_result;
    LockPolicy m_lock;

  public:
    /**
     * @brief Collector static interface
     * @see Collector
     * @{
     */
    explicit CopyingCollector(size_t size)
        : m_spaces({MemStore(size), MemStore(size)}),
          m_metadata(m_spaces[0].size() / 4),
          m_max_alloc_size(size / 2)
    {
        if (size >= ptr_mask) {
            throw std::runtime_error("Heap size too large");
        }
    }

    [[nodiscard]] FatPtr alloc(size_t size,
                               std::align_val_t alignment = std::align_val_t{
                                   1});

    std::future<std::vector<FatPtr>> async_collect(
        const std::vector<FatPtr*>& roots) noexcept;

    [[nodiscard]] bool contains(void* ptr) const noexcept;

    [[nodiscard]] size_t free_space() const noexcept;
    /** @} */

    /**
     * @brief Dispatches an async collection task
     * Waits for the current collection to finish before starting a new one
     * if one is already in progress
     */
    void collect() noexcept;

  private:
    /**
     * @brief Copies the object pointed to by `ptr` to the other space
     *
     * @param to_space space to copy the object to
     * @param ptr pointer to object to copy
     * @return FatPtr pointer to the copy of the object
     */
    [[nodiscard]] FatPtr copy(SpaceNum to_space, const FatPtr& ptr) noexcept;

    /**
     * @brief Forwards a pointer to the other space
     * Forwards the pointer and all members via depth-first traversal
     *
     * @param to_space space to forward the pointer to
     * @param ptr [in/out] pointer to forward
     * @param visited [in/out] set of pointers that have already been forwarded
     * (black nodes in the graph)
     */
    void forward_ptr(SpaceNum to_space, FatPtr& ptr,
                     std::unordered_map<FatPtr, FatPtr>& visited);

    /**
     * @brief Get the space num a pointer belongs to
     *
     * @param ptr
     * @return uint8_t 0 or 1, depending on which space `ptr` belongs in
     * @throws `std::runtime_error` if `ptr` does not belong to any space
     */
    SpaceNum get_space_num(const FatPtr& ptr) const;

    /**
     * @brief Allocates a new object on the heap without regard to
     * `m_max_alloc_size`.
     * Does not lock the collector.
     *
     * @param size
     * @param alignment
     * @return FatPtr
     */
    [[nodiscard]] FatPtr alloc_no_constraints(SpaceNum to_space_num,
                                              const MetaData& meta_data,
                                              size_t index);

    /**
     * @brief Attempts to allocate a new object on the heap.
     * If allocation fails, invokes a collection and tries again.
     * If the retry fails, throws `std::bad_alloc`.
     *
     * @param size size of the object to allocate
     * @param alignment alignment of the object to allocate
     * @param attempts number of times we have attempted to allocate
     * @return FatPtr
     */
    [[nodiscard]] FatPtr alloc_attempt(size_t size, std::align_val_t alignment,
                                       uint8_t attempts = 0);

    /**
     * @brief Reserves space for an object of the given size in the given space
     * and its padding to achieve its alignment.
     *
     * @param size size of the object to reserve space for
     * @param to_space space to reserve space in
     * @param alignment alignment of the object to reserve space for
     * @return size_t index of the start of the reserved space (past the
     * padding)
     */
    [[nodiscard]] size_t reserve_space(size_t size, SpaceNum to_space,
                                       std::align_val_t alignment);
};

static_assert(Collector<CopyingCollector<SerialGCPolicy>>);
static_assert(Collector<CopyingCollector<ConcurrentGCPolicy>>);
}  // namespace gcpp
