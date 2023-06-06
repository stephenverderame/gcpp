#pragma once
#include <cstdint>
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

/** MSB of a GC pointer to indicate it's a pointer */
constexpr auto ptr_tag = static_cast<ptr_size_t>(0x9F) << (ptr_size - 1) * 8;
constexpr auto ptr_mask =
    (static_cast<ptr_size_t>(1) << (ptr_size - 1) * 8) - 1;

/**
 * @brief Determines if a value may be a pointer.
 * Requires `ptr` and `ptr + 1` are valid addresses.
 *
 * @param ptr
 * @return true if `ptr` may be a pointer
 */
auto maybe_ptr(const ptr_size_t* ptr)
{
    return *ptr == ptr_header() && (*(ptr + 1) & ptr_tag) == ptr_tag;
}

struct FatPtr {
    ptr_size_t header = ptr_header();
    ptr_size_t ptr = ptr_tag;
};

/** Size of a GC pointer */
constexpr auto gc_ptr_size = sizeof(FatPtr);
