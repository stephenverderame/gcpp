#include <gc_scan.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stop_token>

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
    GC_UPDATE_STACK_RANGE();
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
    GC_UPDATE_STACK_RANGE();
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
    GC_UPDATE_STACK_RANGE();
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
    GC_UPDATE_STACK_RANGE();
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
    GC_UPDATE_STACK_RANGE();
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
    GC_UPDATE_STACK_RANGE();
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, IsSupersetOf(expected_roots));
    // about 48 bytes per function, 128 byte redzone
    ASSERT_LT(roots.size(), 2 + i * 3);
    (void)ptr;
}
TEST(ScanTest, RightRecursiveTest) { rec_right(1, 101); }

TEST(FatPtrTest, AtomicOps)
{
    FatPtr ptr{0x1000};
    FatPtr ptr2{0x2000};
    FatPtr ptr3{0x3000};
    FatPtr ptr4{0x4000};
    struct {
        uintptr_t a = 0;
        uintptr_t b = 0;
    } dummy_ptr;
    ptr.atomic_update(ptr2);
    ASSERT_EQ(ptr, ptr2);
    auto res = ptr.compare_exchange(ptr2, ptr3);
    ASSERT_EQ(ptr, ptr3);
    ASSERT_TRUE(!res.has_value());
    res = ptr.compare_exchange(ptr2, ptr4);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res.value_or(FatPtr{0x0}), ptr3);
    ASSERT_EQ(ptr, ptr3);
    ASSERT_EQ(ptr.atomic_load() & ptr_mask,
              reinterpret_cast<uintptr_t>(ptr3.as_ptr()));
    ASSERT_EQ(ptr.atomic_load() & ptr_tag_mask, ptr_tag);
    ASSERT_TRUE(FatPtr::maybe_ptr(reinterpret_cast<uintptr_t*>(&ptr4)));
    ASSERT_FALSE(FatPtr::maybe_ptr(reinterpret_cast<uintptr_t*>(&dummy_ptr)));
}

TEST(ScanTest, MTScan)
{
    std::atomic<uint8_t> count = 0;
    std::jthread t1([&](std::stop_token st) {
        FatPtr ptr{0x1022};
        GC_UPDATE_STACK_RANGE();
        count += 1;
        while (!st.stop_requested()) {
        }
    });

    std::jthread t2([&count](std::stop_token st) {
        FatPtr ptr{0x1011};
        GC_UPDATE_STACK_RANGE();
        count += 1;
        while (!st.stop_requested()) {
        }
    });

    while (count < 2) {
    }

    GC_UPDATE_STACK_RANGE();
    std::vector<uintptr_t> roots;
    GC_GET_ROOT_VALS(roots);
    ASSERT_THAT(roots, IsSupersetOf({0x1000, 0x1011, 0x1022}));
}
