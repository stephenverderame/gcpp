#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>

using ptr_t = void*;
/** Size of a regular pointer */
constexpr uintptr_t ptr_size = sizeof(ptr_t);
/** Size of the address space */
constexpr auto addr_space_size = static_cast<uintptr_t>(1)
                                 << (ptr_size - 1) * 8;

#if defined(__x86_64__) && __x86_64__ || defined(_M_X64) && _M_X64
#define REG_PREFIX "r"
#define SIZE_SUFFIX "q"
#elif defined(__i386__) || defined(_M_IX86)
#define REG_PREFIX "e"
#define SIZE_SUFFIX "l"
#else
#error "Unsupported architecture"
#endif

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
        return static_cast<uintptr_t>('poin') << 32 | 'ters';
    } else {
        throw "Unsupported word size";
    }
}
constexpr uint8_t ptr_tag_byte = 0x9F;
/** MSB of a GC pointer to indicate it's a pointer */
constexpr auto ptr_tag = static_cast<uintptr_t>(ptr_tag_byte)
                         << (ptr_size - 1) * 8;
/** Mask to AND a ptr to retrieve only the tag */
constexpr auto ptr_tag_mask = static_cast<uintptr_t>(0xFF)
                              << (ptr_size - 1) * 8;
/** Mask to AND a ptr of a GC pointer to remove the tag */
constexpr auto ptr_mask = (static_cast<uintptr_t>(1) << (ptr_size - 1) * 8) - 1;

/**
 * @brief The actual pointer data used by a GC
 */
struct GCPtr {
    uintptr_t ptr;
};

static_assert(std::is_standard_layout_v<GCPtr> &&
              std::is_trivially_copyable_v<GCPtr> &&
              sizeof(GCPtr) == sizeof(uintptr_t));

/**
 * @brief The underlying pointer type used by the GC
 *
 */
struct FatPtr {
  private:
    uintptr_t m_header = ptr_header();
    mutable uintptr_t m_ptr;
    friend struct std::hash<FatPtr>;

  public:
    /**
     * @brief Gets the pointer (address of data) with sequential consistency
     *
     */
    /*__attribute__((no_sanitize("thread")))*/ auto atomic_load() const
    {
        volatile uintptr_t read_ptr = 0;
        const auto ptr_addr = &m_ptr;
        asm("xor %%rax, %%rax\n"
            "movq %1, %%rcx\n"
            "lock xadd %%rax, (%%rcx)\n"
            "mov %%rax, %0"
            : "=rm"(read_ptr)
            : "rm"(ptr_addr)
            : "rax", "rcx", "memory");
        if ((read_ptr & ptr_tag_mask) != ptr_tag) {
            throw std::runtime_error("Invalid pointer");
        }
        return read_ptr;
        // assert((m_ptr & ptr_tag_mask) == ptr_tag);
        // return m_ptr;
    }
    /**
     * @brief Construct a new Fat Ptr object
     *
     * @param ptr the GC ptr this FatPtr should point to. `ptr` need not contain
     * the tag and should not have any bits set in the most significant byte
     */
    explicit FatPtr(uintptr_t ptr = 0) : m_ptr((ptr & ptr_mask) | ptr_tag) {}
    /**
     * @brief Get the gc ptr (without the tag)
     */
    inline auto get_gc_ptr() const { return GCPtr{atomic_load() & ptr_mask}; }

    auto operator==(const FatPtr& other) const { return m_ptr == other.m_ptr; }

    /**
     * @brief Determines if a value may be a pointer.
     * Requires `ptr` and `ptr + 1` are valid addresses and are aligned
     * to `FatPtr`
     *
     * Acquire semantics
     *
     * @param ptr
     * @return true if `ptr` may be a pointer
     */
    __attribute__((no_sanitize("thread"))) inline static auto maybe_ptr(
        // NOLINTNEXTLINE(readability-non-const-parameter)
        uintptr_t* ptr, [[maybe_unused]] bool read_only = false)
    {
        // volatile uintptr_t read_header = 0;
        // volatile uintptr_t read_ptr = 0;
        // if (read_only) {
        //     if ((reinterpret_cast<uintptr_t>(ptr) & 7) != 0) {
        //         throw std::runtime_error("ptr not aligned");
        //     }
        //     read_header = *ptr;
        //     read_ptr = *(ptr + 1);
        // } else {
        //     asm("xor %%rcx, %%rcx\n"
        //         "xor %%rdx, %%rdx\n"
        //         "movq %2, %%rax\n"
        //         "lock xaddq %%rdx, (%%rax)\n"
        //         "lock xaddq %%rcx, 8(%%rax)\n"
        //         "movq %%rcx, %0\n"
        //         "movq %%rdx, %1"
        //         : "=r"(read_header), "=r"(read_ptr)
        //         : "r"(ptr)
        //         : "rax", "rcx", "rdx", "memory");
        // }
        // return read_header == ptr_header() &&
        //        (read_ptr & ptr_tag_mask) == ptr_tag;
        asm("mfence" ::: "memory");
        return *ptr == ptr_header() && (*(ptr + 1) & ptr_tag_mask) == ptr_tag;
        // only check the header since that is never modified
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator const std::byte*() const
    {
        return reinterpret_cast<const std::byte*>(get_gc_ptr().ptr);
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::byte* const() const
    {
        return reinterpret_cast<std::byte* const>(get_gc_ptr().ptr);
    }

    explicit operator uintptr_t() const { return get_gc_ptr().ptr; }

    inline std::byte* as_ptr() const
    {
        return reinterpret_cast<std::byte*>(get_gc_ptr().ptr);
    }

    /**
     * @brief Atomically updates the current pointer to point do `other`.
     * Reading of `other` and updating of `m_ptr` do not necessarily happen in
     * one atomic instructions, however each one is atomic.
     *
     * Sequentially consistent
     *
     * @param other
     */
    /*__attribute__((no_sanitize("thread")))*/
    inline void atomic_update(FatPtr other) noexcept
    {
        /*
            On x86, the move instruction to an aligned address is atomic release
            and the move from an aligned address is atomic acquire.
            To make sure the compiler directly uses a single move instruction
            to update `m_ptr`, we write the assembly directly.

            Another option is to have the FatPtr templated on a locking policy
            and contain a lock. Since we have other x86 assembly inlined, I'll
           try this first.

           We use xchgq for sequential consistency.
        */
        asm("lock xchgq %1, %0" : "=m"(m_ptr) : "r"(other.m_ptr) : "memory");
    }

    /**
     * @brief Atomically compares the current pointer to `expected` and if they
     * are equal, updates the current pointer to `desired`. Otherwise, returns
     * the current pointer.
     *
     * Sequentially consistent
     *
     * @param expected the expected value of the pointer
     * @param desired the value to update the pointer to if it is equal to
     * `expected`
     * @return nullopt if the pointer was updated, otherwise the current value
     * of the pointer
     */
    /*__attribute__((no_sanitize("thread")))*/ inline std::optional<FatPtr>
    compare_exchange(const FatPtr& expected, FatPtr desired)
    {
        // lock cmpxchg cannot fail supriously
        volatile bool success = false;
        volatile uintptr_t new_ptr = 0;
        asm("movq %3, %%rax\n"
            "movq %4, %%rcx\n"
            "lock cmpxchgq %%rcx, %0\n"
            "setz %1\n"
            "movq %%rax, %2"
            // outputs
            : "=m"(m_ptr), "=r"(success), "=rm"(new_ptr)
            // inputs
            : "rm"(expected.m_ptr), "rm"(desired.m_ptr)
            // clobbers: things we overwrite ("clobber")
            : "rax", "rcx", "memory");
        if (success) {
            return std::nullopt;
        } else {
            return std::make_optional(FatPtr{new_ptr});
        }
    }

    /**
     * @brief Tests if the given pointer is still a GC pointer, and if so
     * returns a copy. Loads with acquire semantics.
     * Requires `ptr` and `ptr + 1` are valid addresses and are aligned to
     * `alignof(FatPtr)`
     *
     * @param ptr
     * @return std::optional<FatPtr>
     */
    /*__attribute__((no_sanitize("thread")))*/ static std::optional<FatPtr>
    test_ptr(const FatPtr* ptr)
    {
        FatPtr val;
        // use mov to ensure 8-byte move for atomicity
        asm("mfence\n"
            "mov (%2), %%rax\n"
            "mov 8(%2), %%rcx\n"
            "mov %%rax, %0\n"
            "mov %%rcx, %1"
            : "=rm"(val.m_header), "=rm"(val.m_ptr)
            : "r"(ptr)
            : "rax", "rcx", "memory");
        if (FatPtr::maybe_ptr(reinterpret_cast<uintptr_t*>(&val))) {
            return val;
        }
        return {};
    }
};
// must be trivially copyable to memcpy it
// must be standard layout so ptr to it is same as ptr to header
static_assert(std::is_standard_layout_v<FatPtr> &&
              std::is_trivially_copyable_v<FatPtr> &&
              sizeof(FatPtr) == sizeof(uintptr_t) * 2);

namespace std
{
template <>
struct std::hash<FatPtr> {
    auto operator()(const FatPtr& ptr) const noexcept
    {
        return std::hash<uintptr_t>{}(ptr.m_ptr);
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
inline void scan_memory(uintptr_t begin, uintptr_t end, Func f,
                        bool read_only = false) noexcept
{
    const auto aligned_start = begin & gc_ptr_alignment_mask;
    begin = aligned_start == begin ? aligned_start
                                   : aligned_start + gc_ptr_alignment;
    for (auto ptr = begin; ptr + gc_ptr_size < end; ptr += gc_ptr_alignment) {
        if (FatPtr::maybe_ptr(reinterpret_cast<uintptr_t*>(ptr), read_only)) {
            f(reinterpret_cast<FatPtr*>(ptr));
        }
    }
}
