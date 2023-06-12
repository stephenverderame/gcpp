#include <gc_scan.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gc_base.h"
#include "gmock/gmock.h"
using testing::IsSupersetOf;
using testing::UnorderedElementsAre;

const auto test_ptr = FatPtr{0x1000};
// NOLINTNEXTLINE
auto test_ptr2 = FatPtr{0x2000};
const auto not_ptr = 0x1000;
// NOLINTNEXTLINE
auto not_ptr_2 = 0x2000;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GC_GET_ROOT_VALS(out_vec)                                       \
    {                                                                   \
        std::vector<FatPtr*> root_addrs;                                \
        GC_GET_ROOTS(root_addrs);                                       \
        (out_vec).resize(root_addrs.size());                            \
        std::transform(root_addrs.begin(), root_addrs.end(),            \
                       (out_vec).begin(),                               \
                       [](auto ptr) { return ptr->get_gc_ptr().ptr; }); \
    }

TEST(ScanTest, GlobalTest)
{
    std::vector<uintptr_t> roots;
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, UnorderedElementsAre(0x1000, 0x2000));
    // avoid unused variable warning
    (void)test_ptr;
    (void)test_ptr2;
    (void)not_ptr;
}

TEST(ScanTest, LocalsTest)
{
    auto not_ptr2 = 0xDEADBEEF;
    auto ptr = FatPtr{0x5000};
    const auto ptr2 = FatPtr{0x6000};
    std::vector<uintptr_t> roots;
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, UnorderedElementsAre(0x1000, 0x2000, 0x5000, 0x6000));
    (void)not_ptr2;
    (void)ptr;
    (void)ptr2;
}

__attribute__((noinline)) void foo()
{
    auto ptr = FatPtr{0x7000};
    const auto ptr2 = FatPtr{0x8000};
    std::vector<uintptr_t> roots;
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, UnorderedElementsAre(0x1000, 0x2000, 0x7000, 0x8000));
    (void)ptr;
    (void)ptr2;
}

__attribute__((noinline)) void bar()
{
    auto ptr = FatPtr{0x700};
    const auto ptr2 = FatPtr{0x800};
    std::vector<uintptr_t> roots;
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, IsSupersetOf({0x1000, 0x2000, 0x700, 0x800}));
    (void)ptr;
    (void)ptr2;
}

TEST(ScanTest, NestedLocals)
{
    foo();
    bar();
    std::vector<uintptr_t> roots;
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, IsSupersetOf({0x1000, 0x2000}));
}

void rec_left(uintptr_t i, uintptr_t max_size)
{
    if (i == max_size) {
        return;
    }
    auto ptr = FatPtr{(i * 0x10000)};
    std::vector<uintptr_t> roots;
    std::vector<uintptr_t> expected_roots;
    GC_GET_ROOT_VALS(roots);
    expected_roots.push_back(0x1000);
    expected_roots.push_back(0x2000);
    for (uintptr_t j = 1; j <= i; ++j) {
        expected_roots.push_back(j * 0x10000);
    }
    ASSERT_THAT(roots, IsSupersetOf(expected_roots));
    ASSERT_THAT(expected_roots, IsSupersetOf(roots));
    rec_left(i + 1, max_size);
    (void)ptr;
}

TEST(ScanTest, LeftRecursiveTest) { rec_left(1, 101); }

void rec_right(uintptr_t i, uintptr_t max_size)
{
    if (i == max_size) {
        return;
    }
    auto ptr = FatPtr{(i * 0x100000)};
    std::vector<uintptr_t> roots;
    const std::array<uintptr_t, 3> expected_roots = {0x1000, 0x2000,
                                                     i * 0x100000};
    rec_right(i + 1, max_size);
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, IsSupersetOf(expected_roots));
    // about 48 bytes per function, 128 byte redzone
    ASSERT_LT(roots.size(), 2 + i * 3);
    (void)ptr;
}
TEST(ScanTest, RightRecursiveTest) { rec_right(1, 101); }
