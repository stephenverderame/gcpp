#include <gc_scan.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gc_base.h"
#include "gmock/gmock.h"
using testing::IsSupersetOf;
using testing::UnorderedElementsAre;

const auto test_ptr = FatPtr{ptr_header(), ptr_tag + 0x1000};
// NOLINTNEXTLINE
auto test_ptr2 = FatPtr{ptr_header(), ptr_tag + 0x2000};
const auto not_ptr = 0x1000;
// NOLINTNEXTLINE
auto not_ptr_2 = 0x2000;

TEST(ScanTest, GlobalTest)
{
    std::vector<ptr_size_t> roots;
    GC_GET_ROOTS(roots);
    ASSERT_THAT(roots, UnorderedElementsAre(0x1000, 0x2000));
    // avoid unused variable warning
    (void)test_ptr;
    (void)test_ptr2;
    (void)not_ptr;
}

TEST(ScanTest, LocalsTest)
{
    auto not_ptr2 = 0xDEADBEEF;
    auto ptr = FatPtr{ptr_header(), ptr_tag + 0x5000};
    const auto ptr2 = FatPtr{ptr_header(), ptr_tag + 0x6000};
    std::vector<ptr_size_t> roots;
    GC_GET_ROOTS(roots);
    ASSERT_THAT(roots, UnorderedElementsAre(0x1000, 0x2000, 0x5000, 0x6000));
    (void)not_ptr2;
    (void)ptr;
    (void)ptr2;
}

__attribute__((noinline)) void foo()
{
    auto ptr = FatPtr{ptr_header(), ptr_tag + 0x7000};
    const auto ptr2 = FatPtr{ptr_header(), ptr_tag + 0x8000};
    std::vector<ptr_size_t> roots;
    GC_GET_ROOTS(roots);
    ASSERT_THAT(roots, UnorderedElementsAre(0x1000, 0x2000, 0x7000, 0x8000));
    (void)ptr;
    (void)ptr2;
}

__attribute__((noinline)) void bar()
{
    auto ptr = FatPtr{ptr_header(), ptr_tag + 0x700};
    const auto ptr2 = FatPtr{ptr_header(), ptr_tag + 0x800};
    std::vector<ptr_size_t> roots;
    GC_GET_ROOTS(roots);
    ASSERT_THAT(roots, IsSupersetOf({0x1000, 0x2000, 0x700, 0x800}));
    (void)ptr;
    (void)ptr2;
}

TEST(ScanTest, NestedLocals)
{
    foo();
    bar();
    std::vector<ptr_size_t> roots;
    GC_GET_ROOTS(roots);
    ASSERT_THAT(roots, IsSupersetOf({0x1000, 0x2000}));
}

void rec_left(ptr_size_t i, ptr_size_t max_size)
{
    if (i == max_size) {
        return;
    }
    auto ptr = FatPtr{ptr_header(), ptr_tag + (i * 0x10000)};
    std::vector<ptr_size_t> roots;
    std::vector<ptr_size_t> expected_roots;
    GC_GET_ROOTS(roots);
    expected_roots.push_back(0x1000);
    expected_roots.push_back(0x2000);
    for (ptr_size_t j = 1; j <= i; ++j) {
        expected_roots.push_back(j * 0x10000);
    }
    ASSERT_THAT(roots, IsSupersetOf(expected_roots));
    ASSERT_THAT(expected_roots, IsSupersetOf(roots));
    rec_left(i + 1, max_size);
    (void)ptr;
}

TEST(ScanTest, LeftRecursiveTest) { rec_left(1, 101); }

void rec_right(ptr_size_t i, ptr_size_t max_size)
{
    if (i == max_size) {
        return;
    }
    auto ptr = FatPtr{ptr_header(), ptr_tag + (i * 0x100000)};
    std::vector<ptr_size_t> roots;
    const std::array<ptr_size_t, 3> expected_roots = {0x1000, 0x2000,
                                                      i * 0x100000};
    rec_right(i + 1, max_size);
    GC_GET_ROOTS(roots);
    ASSERT_THAT(roots, IsSupersetOf(expected_roots));
    // about 48 bytes per function, 128 byte redzone
    ASSERT_LT(roots.size(), 2 + i * 3);
    (void)ptr;
}
TEST(ScanTest, RightRecursiveTest) { rec_right(1, 101); }