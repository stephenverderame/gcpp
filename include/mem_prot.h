#pragma once
#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <new>
namespace gcpp
{
enum class ProtectionMode : int {
    ReadOnly = PROT_READ,
    WriteOnly = PROT_WRITE,
    ReadWrite = PROT_READ | PROT_WRITE,
};

/**
 * @brief RAII type to make protect a region of memory
 * The protected area will be all pages that overlap with the region
 */
class RegionProtection
{
  private:
    void* m_region_start;
    size_t m_region_size;
    ProtectionMode m_old_prot;
    ProtectionMode m_new_prot;
    mutable bool m_locked = false;

  public:
    RegionProtection(void* start, const void* end, ProtectionMode mode);
    ~RegionProtection();
    RegionProtection(const RegionProtection&) = delete;
    RegionProtection& operator=(const RegionProtection&) = delete;
    RegionProtection(RegionProtection&&) noexcept;
    RegionProtection& operator=(RegionProtection&&) noexcept;
    void unlock() const;
    void lock() const;
};

/** Gets the page size in bytes */
int page_size();
/** Registers a heap so that it can be protected */
void register_heap(const void* start, size_t len);
/** Gets a size that is a multiple of page_size which is >= `size` */
size_t page_size_ceil(size_t size);
/**
 * Gets a RAII type to make a region of memory read_only.
 * Will lock all pages that contain the region.
 */
[[nodiscard]] RegionProtection region_readonly(void* start, size_t len);
/** Gets the page size in bytes as `std::align_val_t` */
std::align_val_t page_size_align();

}  // namespace gcpp