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

inline uint8_t load(uint8_t a) { return a; }
inline uint8_t load(const std::atomic<uint8_t>& a) { return a.load(); }

template <CollectorLockingPolicy LockPolicy>
template <std::ranges::range T>
requires std::same_as<std::ranges::range_reference_t<T>, FatPtr&>
std::vector<FatPtr> CopyingCollector<LockPolicy>::collect(T&& roots) noexcept
{
    const uint8_t to_space = [this]() {
        auto lk = m_lock.lock();
        m_space_num = other_space_num(m_space_num);
        (void)lk;
        return load(m_space_num);
    }();
    m_next = 0;
    // wait for last collection to be done if one is currently running
    m_lock.wait_for_collection();
    m_lock.do_collection([this, roots = std::forward<T>(roots), to_space]() {
        std::vector<FatPtr> promoted;
        std::unordered_map<FatPtr, FatPtr> visited;
        for (auto& it : std::ranges::filter_view(
                 roots, [this](auto ptr) { return contains(ptr.as_ptr()); })) {
            forward_ptr(to_space, it, visited);
        }
        return promoted;
    });
    // TODO: promotions
    // maybe use a callback to the user to determine if an object should be
    // promoted
    return {};
}
}  // namespace gcpp