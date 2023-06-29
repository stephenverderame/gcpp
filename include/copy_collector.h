#pragma once
#include <array>
#include <mutex>
#include <new>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "collector.h"
#include "concurrent_gc.h"
#include "gc_base.h"
#include "mem_prot.h"

namespace gcpp
{
enum class SpaceNum : uint8_t { Zero = 0, One = 1 };
template <CollectorLockingPolicy LockPolicy>
class CopyingCollector
{
    using MemStore = std::unique_ptr<std::byte[]>;

  private:
    /** Size of each mem store*/
    size_t m_heap_size;
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
    std::shared_future<CollectionResultT> m_collect_result;
    mutable LockPolicy m_lock;
    /** Debugging: thread count in async_collect */
    std::atomic<size_t> m_tcount = 0;
    std::mutex m_test_mu;

  public:
    /**
     * @brief Collector static interface
     * @see Collector
     * @{
     */
    explicit CopyingCollector(size_t size)
        : m_heap_size(page_size_ceil(size)),
          m_spaces(
              {MemStore(new(page_size_align()) std::byte[page_size_ceil(size)]),
               MemStore(new(page_size_align())
                            std::byte[page_size_ceil(size)])}),
          m_metadata(m_heap_size / 4),
          m_max_alloc_size(size / 2)
    {
        if (size >= ptr_mask) {
            throw std::runtime_error("Heap size too large");
        }
        register_heap(m_spaces[0].get(), m_heap_size);
        register_heap(m_spaces[1].get(), m_heap_size);
        assert((reinterpret_cast<uintptr_t>(m_spaces[0].get()) &
                ~static_cast<uintptr_t>(page_size() - 1)) !=
               (reinterpret_cast<uintptr_t>(m_spaces[1].get()) &
                ~static_cast<uintptr_t>(page_size() - 1)));
    }

    [[nodiscard]] FatPtr alloc(size_t size,
                               std::align_val_t alignment = std::align_val_t{
                                   1});

    std::future<std::vector<FatPtr>> async_collect(
        const std::vector<FatPtr*>& extra_roots) noexcept;

    [[nodiscard]] bool contains(void* ptr) const noexcept;

    [[nodiscard]] size_t free_space() const noexcept;
    /** @} */

    /**
     * @brief Dispatches an async collection task
     * Waits for the current collection to finish before starting a new one
     * if one is already in progress
     *
     * @param needed_space amount of space needed to be free. Avoids collection
     * if there is already enough space. Any sufficiently large value will
     * always trigger a collection
     */
    void collect(
        size_t needed_space = std::numeric_limits<size_t>::max()) noexcept;

    auto test_lock() { return std::unique_lock{m_test_mu}; }

  private:
    /**
     * @brief Copies the object pointed to by `ptr` to the other space
     *
     * @param to_update pointer to update to point to the copy if it equals `ptr`
     * @param to_space space to copy the object to
     * @param ptr pointer to object to copy
     * @return FatPtr pointer to the copy of the object
     */
    [[nodiscard]] FatPtr copy(FatPtr& to_update, SpaceNum to_space, const FatPtr& ptr);

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
    [[nodiscard]] std::optional<size_t> reserve_space(
        size_t size, SpaceNum to_space, std::align_val_t alignment,
        size_t max_alloc_size);

    /**
     * @brief Checks if a new allocation of the given size, starting
     * (excluding padding) at the given index in the given space overlaps
     * with any existing allocations. If so, throws.
     * Requires having a lock.
     *
     * @param index
     * @param space
     * @param size
     */
    void check_overlapping_alloc(const std::optional<size_t>& index,
                                 SpaceNum space, size_t size) const;
};

static_assert(Collector<CopyingCollector<SerialGCPolicy>>);
static_assert(Collector<CopyingCollector<ConcurrentGCPolicy>>);
}  // namespace gcpp
