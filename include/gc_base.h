#pragma once
#include <cstdint>
#include <functional>
#include <type_traits>

using ptr_t = void*;
/** Size of a regular pointer */
constexpr auto ptr_size = sizeof(ptr_t);
/** Numeric pointer type */
using ptr_size_t = std::remove_cv_t<decltype(ptr_size)>;
/** Size of the address space */
constexpr auto addr_space_size = static_cast<ptr_size_t>(1)
                                 << (ptr_size - 1) * 8;

/**
 * We use a system of fat pointers to identify pointers.
 * Every GC pointer will have a header which is a word that comes right before
 * the actual pointer. Then, the pointer itself reserves the most significant
 * byte to store a tag. This tag is used to identify the pointer as a GC
 * pointer. Both a header and tag makes it very unlikely to pin garbage
 * and allows us to not have to rescan the entire address space.
 */

/**
 * @brief Returns the header which indicates a value could be a pointer
 *
 * @return constexpr auto
 */
constexpr auto ptr_header()
{
    static_assert(ptr_size >= 4, "word size too small");
    if constexpr (ptr_size == 4) {
        return 'ptrs';
    } else if constexpr (ptr_size == 8) {
        return static_cast<ptr_size_t>('poin') << 32 | 'ters';
    } else {
        throw "Unsupported word size";
    }
}
constexpr uint8_t ptr_tag_byte = 0x9F;
/** MSB of a GC pointer to indicate it's a pointer */
constexpr auto ptr_tag = static_cast<ptr_size_t>(ptr_tag_byte)
                         << (ptr_size - 1) * 8;
/** Mask to AND a ptr to retrieve only the tag */
constexpr auto ptr_tag_mask = static_cast<ptr_size_t>(0xFF)
                              << (ptr_size - 1) * 8;
/** Mask to AND a ptr of a GC pointer to remove the tag */
constexpr auto ptr_mask =
    (static_cast<ptr_size_t>(1) << (ptr_size - 1) * 8) - 1;

/**
 * @brief Determines if a value may be a pointer.
 * Requires `ptr` and `ptr + 1` are valid addresses.
 *
 * @param ptr
 * @return true if `ptr` may be a pointer
 */
inline auto maybe_ptr(const ptr_size_t* ptr)
{
    return *ptr == ptr_header() && (*(ptr + 1) & ptr_tag_mask) == ptr_tag;
}

/**
 * @brief The actual pointer data used by a GC
 */
struct GCPtr {
    ptr_size_t ptr;
};

static_assert(std::is_standard_layout_v<GCPtr> &&
              std::is_trivially_copyable_v<GCPtr> &&
              sizeof(GCPtr) == sizeof(ptr_size_t));

/**
 * @brief The underlying pointer type used by the GC
 *
 */
struct FatPtr {
  private:
    ptr_size_t m_header = ptr_header();
    ptr_size_t m_ptr;
    friend struct std::hash<FatPtr>;

  public:
    /**
     * @brief Construct a new Fat Ptr object
     *
     * @param ptr the GC ptr this FatPtr should point to. `ptr` need not contain
     * the tag and should not have any bits set in the most significant byte
     */
    explicit FatPtr(ptr_size_t ptr) : m_ptr((ptr & ptr_mask) | ptr_tag) {}
    /**
     * @brief Get the gc ptr (without the tag)
     */
    inline auto get_gc_ptr() const { return GCPtr{m_ptr & ptr_mask}; }

    auto operator==(const FatPtr& other) const { return m_ptr == other.m_ptr; }
};
// must be trivially copyable to memcpy it
// must be standard layout so ptr to it is same as ptr to header
static_assert(std::is_standard_layout_v<FatPtr> &&
              std::is_trivially_copyable_v<FatPtr> &&
              sizeof(FatPtr) == sizeof(ptr_size_t) * 2);

namespace std
{
template <>
struct std::hash<FatPtr> {
    auto operator()(const FatPtr& ptr) const noexcept
    {
        return std::hash<ptr_size_t>{}(ptr.m_ptr);
    }
};
}  // namespace std

/** Size of a GC pointer */
constexpr auto gc_ptr_size = sizeof(FatPtr);
/** Alignment of the GC pointer */
constexpr auto gc_ptr_alignment = std::alignment_of_v<FatPtr>;
/** Mask to AND a value with to get it onto a `gc_ptr_alignment` byte boundary
 */
constexpr auto gc_ptr_alignment_mask = ~(gc_ptr_alignment - 1);
/** Size of the redzone */
constexpr auto red_zone_size = 128;

/**
 * @brief Scans the memory between `begin` and `end` looking for GC pointers
 * Requires `begin` and `end` denote a readable area of memory
 *
 * @tparam Func callable object which takes an address containing a GC pointer
 * @param begin inclusive start of the memory region
 * @param end exclusive end of the memory region
 * @param f
 */
template <typename Func>
requires std::invocable<Func, FatPtr*>
inline void scan_memory(ptr_size_t begin, ptr_size_t end, Func f) noexcept
{
    const auto aligned_start = begin & gc_ptr_alignment_mask;
    begin = aligned_start == begin ? aligned_start
                                   : aligned_start + gc_ptr_alignment;
    for (auto ptr = begin; ptr + gc_ptr_size < end; ptr += gc_ptr_alignment) {
        if (maybe_ptr(reinterpret_cast<ptr_size_t*>(ptr))) {
            f(reinterpret_cast<FatPtr*>(ptr));
        }
    }
}
