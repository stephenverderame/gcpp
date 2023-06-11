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

template <std::ranges::range T>
requires std::same_as<std::ranges::range_reference_t<T>, FatPtr&>
std::vector<FatPtr> CopyingCollector::collect(T&& roots) noexcept
{
    std::vector<FatPtr> promoted;
    m_space_num = other_space_num(m_space_num);
    m_next = 0;
    std::unordered_map<FatPtr, FatPtr> visited;
    for (auto it = roots.begin(); it != roots.end(); ++it) {
        forward_ptr(*it, visited);
    }
    // TODO: promotions
    // maybe use a callback to the user to determine if an object should be
    // promoted
    return promoted;
}
}  // namespace gcpp