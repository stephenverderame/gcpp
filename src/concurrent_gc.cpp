#include "concurrent_gc.h"

namespace
{
void seq_cst_cpy_byte(std::byte* dst, const std::byte* src)
{
    asm("movb (%0), %%al\n"
        "lock xchgb %%al, (%1)\n"
        :
        : "r"(src), "r"(dst)
        : "al", "memory");
}

void seq_cst_cpy_short(std::byte* dst, const std::byte* src)
{
    asm("movw (%0), %%ax\n"
        "lock xchgw %%ax, (%1)\n"
        :
        : "r"(src), "r"(dst)
        : "ax", "memory");
}

void seq_cst_cpy_long(std::byte* dst, const std::byte* src)
{
    asm("movl (%0), %%eax\n"
        "lock xchgl %%eax, (%1)\n"
        :
        : "r"(src), "r"(dst)
        : "eax", "memory");
}

void seq_cst_cpy_qword(std::byte* dst, const std::byte* src)
{
    asm("movq (%0), %%rax\n"
        "lock xchgq %%rax, (%1)\n"
        :
        : "r"(src), "r"(dst)
        : "rax", "memory");
}

bool is_aligned_to(const void* ptr, size_t alignment)
{
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void gcpp::seq_cst_cpy(void* dst, const void* src, size_t size)
{
    const auto byte_dst = reinterpret_cast<std::byte*>(dst);
    const auto byte_src = reinterpret_cast<const std::byte*>(src);
    size_t i = 0;
    // we need proper alignment of src for acquire memory ordering
    while (i < size) {
        if (is_aligned_to(byte_src + i, 8)) {
            seq_cst_cpy_qword(byte_dst + i, byte_src + i);
            i += 8;
        } else if (is_aligned_to(byte_src + i, 4)) {
            seq_cst_cpy_long(byte_dst + i, byte_src + i);
            i += 4;
        } else if (is_aligned_to(byte_src + i, 2)) {
            seq_cst_cpy_short(byte_dst + i, byte_src + i);
            i += 2;
        } else {
            seq_cst_cpy_byte(byte_dst + i, byte_src + i);
            ++i;
        }
    }
    for (; i < size; ++i) {
        seq_cst_cpy_byte(byte_dst + i, byte_src + i);
    }
}