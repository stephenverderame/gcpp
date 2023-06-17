#pragma once
#include <ranges>

#include "copy_collector.h"
namespace gcpp
{
/**
 * @brief Gets the index of the other space
 */
inline uint8_t other_space_num(uint8_t space_num) noexcept
{
    return space_num == 0 ? 1 : 0;
}

template <CollectorLockingPolicy LockPolicy>
template <std::ranges::range T>
requires std::same_as<std::ranges::range_reference_t<T>, FatPtr&>
std::vector<FatPtr> CopyingCollector<LockPolicy>::collect(T&& roots) noexcept
{
    std::vector<FatPtr> promoted;
    const uint8_t to_space = [this]() {
        auto lk = m_lock.lock();
        m_space_num = other_space_num(m_space_num);
        (void)lk;
        return m_space_num;
    }();
    m_next = 0;
    std::unordered_map<FatPtr, FatPtr> visited;
    for (auto& it : std::ranges::filter_view(
             roots, [this](auto ptr) { return contains(ptr.as_ptr()); })) {
        forward_ptr(to_space, it, visited);
    }
    // TODO: promotions
    // maybe use a callback to the user to determine if an object should be
    // promoted
    return promoted;
}
}  // namespace gcpp