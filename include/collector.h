#pragma once
#include <concepts>
#include <optional>
#include <utility>
#include <vector>

#include "gc_base.h"
namespace gcpp
{
template <typename T>
concept Collector = requires(T t) {
    /**
     * @brief Allocates a new object on the heap
     *
     * @param size size of the object to allocate
     */
    {
        t.alloc(std::declval<std::size_t>())
    } -> std::same_as<FatPtr>;

    /**
     * @brief Collects all garbage on the heap
     *
     * @return std::vector<FatPtr> objects to be promoted to the next
     * generation. These objects will be removed from the heap.
     */
    {
        t.collect(std::declval<std::vector<FatPtr>&>())
    } noexcept -> std::same_as<std::vector<FatPtr>>;

    /**
     * @brief Construct a new Collector object
     *
     * @param size size of the heap
     * @param promotion_threshold number of collections an object must survive
     * to be promoted to the next generation, or none if the object should never
     * be promoted
     */
    T(std::declval<std::size_t>(), std::declval<std::optional<unsigned>>());

    /**
     * @brief Determines if a pointer is in the heap managed by this collector
     */
    {
        static_cast<const T&>(t).contains(std::declval<void*>())
    } noexcept -> std::same_as<bool>;

    /**
     * @brief Gets the object pointed to by a FatPtr
     */
    {
        t.access(std::declval<const FatPtr&>())
    } noexcept -> std::convertible_to<void*>;

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
    /**
     * @brief Locks the collector
     */
    {
        t.lock_self()
    } noexcept;

    /**
     * @brief Unlocks the collector
     */
    {
        t.unlock_self()
    } noexcept -> std::same_as<void>;

    /**
     * @brief Locks the a given GC object
     */
    {
        t.lock(std::declval<const FatPtr&>())
    } noexcept;

    /**
     * @brief Unlocks the given GC object
     */
    {
        t.unlock(std::declval<const FatPtr&>())
    } noexcept -> std::same_as<void>;

    {
        t.notify_alloc(std::declval<const FatPtr&>())
    } -> std::same_as<void>;

    {
        t.notify_collect(std::declval<const FatPtr&>())
    } noexcept -> std::same_as<void>;
};
}  // namespace gcpp