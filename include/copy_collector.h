#pragma once
#include <array>
#include <vector>

#include "collector.h"
#include "gc_base.h"

namespace gcpp
{
class CopyingCollector
{
  private:
    /** The two spaces of the heap */
    std::array<std::vector<std::byte>, 2> m_spaces;
    /** Next index to allocate an object */
    size_t m_next = 0;
    /** Next index of object to move onto the to space */
    size_t m_scan = 0;
    /** Index of the space in which we allocate new objects */
    unsigned char m_space_idx = 0;
    std::optional<uint8_t> m_promotion_threshold;

  public:
    /**
     * @brief Collector static interface
     * @see Collector
     * @{
     */
    CopyingCollector(size_t size, std::optional<uint8_t> promotion_threshold)
        : m_spaces(
              {std::vector<std::byte>(size), std::vector<std::byte>(size)}),
          m_promotion_threshold(promotion_threshold)
    {
    }

    [[nodiscard]] FatPtr alloc(size_t size);
    std::vector<FatPtr> collect(std::vector<FatPtr>& roots) noexcept;
    [[nodiscard]] bool contains(void* ptr) const noexcept;
    void* access(const FatPtr& ptr) noexcept;
    [[nodiscard]] size_t free_space() const noexcept;
    /** @} */

  private:
    /**
     * @brief Copies the object pointed to by `ptr` to the other space
     *
     * @param ptr pointer to object to copy
     * @return FatPtr pointer to the copy of the object
     */
    [[nodiscard]] FatPtr copy(const FatPtr& ptr) noexcept;
};

static_assert(Collector<CopyingCollector>);
}  // namespace gcpp