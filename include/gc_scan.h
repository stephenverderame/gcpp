#pragma once

#include <memory>
#include <queue>
#include <vector>

#include "gc_base.h"

namespace gcpp
{
class GCRoots
{
  private:
    /** GC addresses of all global roots */
    std::vector<ptr_size_t> m_global_roots;
    /** Pointer to stack location of all local roots */
    std::vector<ptr_size_t> m_local_roots;
    // NOLINTNEXTLINE(cppcoreguidelines-*)
    inline static std::unique_ptr<GCRoots> g_instance;
    // NOLINTNEXTLINE(modernize-use-equals-default)
    GCRoots();

  public:
    /** Gets the singleton instance of the GCRoots object */
    static GCRoots& get_instance();
    /**
     * @brief Get the root nodes of the object graph
     *
     * @param base_ptr rbp of current function
     * @return std::vector<ptr_t>
     */
    std::vector<ptr_size_t> get_roots(ptr_size_t base_ptr);
};

/**
 * @def GC_GET_ROOTS(out_vec)
 * @brief Conservatively gets the GC pointers of all roots
 *
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GC_GET_ROOTS(out_vec)                                          \
    {                                                                  \
        ptr_size_t base_ptr;                                           \
        /* move rbp into base_ptr, AT&T syntax */                      \
        asm("mov %%rbp, %0 \n" : "=r"(base_ptr));                      \
        (out_vec) = gcpp::GCRoots::get_instance().get_roots(base_ptr); \
    }
}  // namespace gcpp