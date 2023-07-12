#pragma once
#include <concepts>
#include <cstddef>
#include <future>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "gc_base.h"
#include "generational_gc.h"
#include "mem_prot.h"
namespace gcpp
{
using CollectionResultT = std::vector<FatPtr>;
/** Static interface for a garbage collector */
template <typename T>
concept Collector = requires(T t) {
    /**
     * @brief Allocates a new object on the heap
     *
     * @param size size of the object to allocate
     * @param alignment alignment of the object to allocate
     */
    {
        t.alloc(std::declval<std::size_t>(), std::declval<std::align_val_t>())
    } -> std::same_as<FatPtr>;

    /**
     * @brief Collects all garbage on the heap
     *
     * @return std::vector<FatPtr> objects to be promoted to the next
     * generation. These objects will be removed from the heap.
     */
    {
        t.async_collect(std::declval<std::vector<FatPtr*>&>())
    } noexcept -> std::same_as<std::future<CollectionResultT>>;

    /**
     * @brief Construct a new Collector object
     *
     * @param size size of the heap
     * @param promotion_threshold number of collections an object must survive
     * to be promoted to the next generation, or none if the object should never
     * be promoted
     */
    T(std::declval<std::size_t>());

    /**
     * @brief Determines if a pointer is in the heap managed by this collector
     */
    {
        static_cast<const T&>(t).contains(std::declval<void*>())
    } noexcept -> std::same_as<bool>;

    /**
     * @brief Gets the amount of free space on the heap before a collection is
     * required
     */
    {
        static_cast<const T&>(t).free_space()
    } noexcept -> std::same_as<std::size_t>;
};

template <typename T>
concept CollectorLockingPolicy = requires(T t) {
    typename T::lock_t;
    /**
     * @brief Locks the collector
     */
    {
        t.lock()
    } noexcept -> std::same_as<typename T::lock_t>;

    // {
    //     t.notify_alloc(std::declval<const FatPtr&>())
    // } -> std::same_as<void>;

    // {
    //     t.notify_collect(std::declval<const FatPtr&>())
    // } noexcept -> std::same_as<void>;

    typename T::gc_size_t;
    typename T::gc_uint8_t;

    {
        t.do_with_lock(std::declval<std::function<int()>>)
    };

    {
        t.do_collection(std::declval<std::function<CollectionResultT()>>())
    } -> std::same_as<std::future<CollectionResultT>>;

    {
        T::acquire(std::declval<typename T::lock_t&>())
    };

    {
        T::release(std::declval<typename T::lock_t&>())
    };
};

/**
 * @brief Heap allocator that aligns all allocations to a given alignment
 *
 * @tparam T type of element in the heap
 * @tparam Alignment alignment of the heap or 0 to use the system page size
 */
template <typename T,
          std::align_val_t Alignment =
              static_cast<std::align_val_t>(alignof(std::max_align_t))>
struct AlignedAllocator {
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;

    template <typename U>
    // NOLINTNEXTLINE(readability-identifier-naming)
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    template <typename U>
    // NOLINTNEXTLINE(google-explicit-constructor)
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept
    {
    }

    AlignedAllocator() noexcept = default;

    [[nodiscard]] T* allocate(std::size_t n)
    {
        if constexpr (Alignment == std::align_val_t{0}) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            return new (page_size_align()) T[n];
        } else {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            return new (Alignment) T[n];
        }
    }

    void deallocate(T* p, std::size_t) noexcept
    {
        // NOLINTNEXTLINE(cppcoreguidelines-*)
        delete[] p;
    }
};
/** Special align_t value to indicate to align on page sizes */
constexpr auto page_size_alignment = std::align_val_t{0};
}  // namespace gcpp