#include <concurrent_gc.h>
#include <gtest/gtest.h>

struct Foo {
    int a;
    int64_t b;
    uint8_t c;
    void* next;
};

TEST(AtomicUtils, SeqCstCpy)
{
    std::array<int, 5> nums = {1, 2, 3, 4, 5};
    std::array<int, 5> nums2 = {0, 0, 0, 0, 0};
    gcpp::seq_cst_cpy(nums2.data(), nums.data(), sizeof(int) * nums.size());
    ASSERT_EQ(nums, nums2);

    Foo a = {0x1000, 0xDEADBEEF, 0x12, reinterpret_cast<void*>(0x1234)};
    Foo b{};
    gcpp::seq_cst_cpy(&b, &a, sizeof(Foo));
    ASSERT_EQ(a.a, b.a);
    ASSERT_EQ(a.b, b.b);
    ASSERT_EQ(a.c, b.c);
    ASSERT_EQ(a.next, b.next);
}