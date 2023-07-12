#pragma once

#include <sys/types.h>

#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "gc_base.h"

namespace gcpp
{
/**
 * @brief Singleton class to fetch the roots of a program
 *
 */
class GCRoots
{
  private:
    /** Pointer to addresses of global roots */
    const std::vector<uintptr_t> m_global_roots;
    /** Set of the largest stack range for each thread. Each pair is the
     * earliest (numerically greatest) stack start and the latest (numerically
     * smallest) stack end. (start, end)
     */
    std::unordered_map<std::thread::id, std::pair<uintptr_t, uintptr_t>>
        m_stack_ranges;
    // NOLINTNEXTLINE(cppcoreguidelines-*)
    inline static std::unique_ptr<GCRoots> g_instance;
    /** Mutex for creation of g_instance */
    inline static std::once_flag g_instance_flag;
    /**
     * Mutex for access to local_roots, scanned_ranges, and stack_ranges.
     * Exclusive ownership is needed for adding a new thread entry or adding new
     * local roots to a thread entry. Shared onwership for everything else.
     */
    std::shared_mutex m_mutex;
    GCRoots() noexcept;

  public:
    /** Gets the singleton instance of the GCRoots object */
    static GCRoots& get_instance();
    /**
     * @brief Get the root nodes of the object graph.
     * Mutates the GCRoots instance with the strong guaruntee
     *
     * @param base_ptr rbp of current function
     * @return std::vector<ptr_t>
     */
    std::vector<FatPtr*> get_roots(uintptr_t base_ptr);

    /**
     * @brief Updates the min and max stack range for the current thread.
     * Should be called on a new allocation.
     *
     * @param base_ptr
     */
    void update_stack_range(uintptr_t base_ptr);

  private:
    /**
     * @brief Scans the stack of the given thread for for local roots.
     * Scans the difference between `m_scanned_ranges` and `m_stack_ranges` for
     * `id`.
     * Requires unique lock on `m_mutex`
     * Requires an entry in `m_scanned_ranges` and `m_stack_ranges` for `id`
     *
     */
    std::vector<uintptr_t> scan_locals(std::thread::id id);
};

/**
 * @def GC_GET_ROOTS(out_vec)
 * @brief Conservatively gets the GC pointers of all roots
 *
 * @param out_vec [out] std::vector<FatPtr*> to store the roots
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GC_GET_ROOTS(out_vec)                                          \
    {                                                                  \
        volatile uintptr_t base_ptr;                                   \
        /* move rbp into `base_ptr`, AT&T syntax */                    \
        asm("mov " RBP ", %0 \n" : "=r"(base_ptr));                      \
        (out_vec) = gcpp::GCRoots::get_instance().get_roots(base_ptr); \
    }
/**
 * @def GC_UPDATE_STACK_RANGE()
 * @brief Updates the stack range for the current thread
 *
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GC_UPDATE_STACK_RANGE()                                     \
    {                                                               \
        volatile uintptr_t base_ptr;                                \
        asm("mov " RBP ", %0 \n" : "=r"(base_ptr));                   \
        gcpp::GCRoots::get_instance().update_stack_range(base_ptr); \
    }
/**
 * @def GC_UPDATE_STACK_RANGE_NESTED_1()
 * @brief Updates the stack range to include the caller of the current function
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GC_UPDATE_STACK_RANGE_NESTED_1()                                   \
    {                                                                      \
        volatile uintptr_t caller_base_ptr;                                \
        asm("mov (" RBP "), %0 \n" : "=r"(caller_base_ptr));                 \
        gcpp::GCRoots::get_instance().update_stack_range(caller_base_ptr); \
    }
}  // namespace gcpp