#include <gtest/gtest.h>

#include <algorithm>
#include <new>
#include <optional>
#include <random>
#include <ranges>

#include "concurrent_gc.h"
#include "copy_collector.h"
#include "copy_collector.inl"
#include "gc_base.h"
#include "gc_scan.h"

template <typename T>
class CopyTest : public testing::Test
{
};

using CollectorT = gcpp::CopyingCollector<gcpp::SerialGCPolicy>;

template <typename T>
void alloc_test(size_t total_size, std::function<size_t()> get_size,
                uint8_t num_allocs)
{
    auto collector = gcpp::CopyingCollector<T>{total_size};

    std::vector<std::tuple<FatPtr, size_t, std::byte>> ptrs;
    for (uint8_t i = 0; i < num_allocs; ++i) {
        const auto obj_size = get_size();
        auto ptr = collector.alloc(obj_size);
        memset(ptr, i + 1, obj_size);
        ptrs.emplace_back(ptr, obj_size, static_cast<std::byte>(i + 1));
    }
    std::shuffle(ptrs.begin(), ptrs.end(),
                 std::mt19937{std::random_device{}()});
    for (auto [ptr, obj_size, obj_data] : ptrs) {
        for (auto j = decltype(obj_size){0}; j < obj_size; ++j) {
            ASSERT_EQ(ptr.as_ptr()[j], obj_data);
        }
    }
}
TYPED_TEST_SUITE(CopyTest, testing::Types<gcpp::SerialGCPolicy>);
TYPED_TEST(CopyTest, Alloc)
{
    alloc_test<TypeParam>(
        128, []() { return 16; }, 4);
}

TYPED_TEST(CopyTest, AllocLarge)
{
    alloc_test<TypeParam>(
        1024000, []() { return 1024; }, 20);
}

TYPED_TEST(CopyTest, AllocRandom)
{
    alloc_test<TypeParam>(
        5120000, []() { return rand() % 5000 + 1; }, 100);
}

TYPED_TEST(CopyTest, Collect)
{
    auto collector = gcpp::CopyingCollector<TypeParam>{1024};
    auto persist1 = collector.alloc(16);
    const auto data1 = persist1.as_ptr();
    memset(persist1, 1, 16);
    for (int i = 0; i < 10; ++i) {
        auto ptr = collector.alloc(16);
        memset(ptr, 10 + i, 16);
    }
    auto persist2 = collector.alloc(16);
    const auto data2 = persist2.as_ptr();
    memset(persist2, 2, 16);
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    ASSERT_LE(collector.free_space(), 512 - 17 * 2);
    ASSERT_GT(collector.free_space(), 512 - 17 * 12);
    const auto new_data1 = persist1.as_ptr();
    for (int i = 0; i < 16; ++i) {
        ASSERT_EQ(new_data1[i], std::byte{1});
    }
    const auto new_data2 = persist2.as_ptr();
    for (int i = 0; i < 16; ++i) {
        ASSERT_EQ(new_data2[i], std::byte{2});
    }
    ASSERT_NE(new_data1, data1);
    ASSERT_NE(new_data2, data2);
}

TYPED_TEST(CopyTest, LinkedList)
{
    auto collector = gcpp::CopyingCollector<TypeParam>{1024};
    constexpr auto size = sizeof(FatPtr) + sizeof(int);
    static_assert(alignof(int) <= alignof(FatPtr));
    auto node = collector.alloc(size, std::align_val_t{alignof(FatPtr)});
    const auto head = node;
    for (int i = 0; i < 16; ++i) {
        auto next = collector.alloc(size, std::align_val_t{alignof(FatPtr)});
        memcpy(node, &next, sizeof(next));
        memcpy(node.as_ptr() + sizeof(next), &i, sizeof(i));
        node = next;
    }
    const auto null = FatPtr{0};
    memcpy(node, &null, sizeof(node));
    int num = 16;
    memcpy(node.as_ptr() + sizeof(node), &num, sizeof(num));
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    int i = 0;
    node = head;
    while (node != null) {
        memcpy(&num, node.as_ptr() + sizeof(node), sizeof(num));
        memcpy(&node, node, sizeof(node));
        ASSERT_EQ(num, i++);
    }
    ASSERT_EQ(i, 17);
}

TYPED_TEST(CopyTest, AlignedAlloc)
{
    auto collector = gcpp::CopyingCollector<TypeParam>{1024};
    auto ptr = collector.alloc(64, std::align_val_t{64});
    memset(ptr, 1, 64);
    ASSERT_EQ(static_cast<uintptr_t>(ptr) % 64, 0);
    const auto data = ptr.as_ptr();
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    const auto new_data = ptr.as_ptr();
    ASSERT_NE(data, new_data);
    for (int i = 0; i < 64; ++i) {
        ASSERT_EQ(new_data[i], std::byte{1});
    }
    ASSERT_EQ(reinterpret_cast<uintptr_t>(new_data) % 64, 0);
}

__attribute__((noinline)) void alloc_array(CollectorT& collector)
{
    constexpr auto array_size = 13;
    auto int_array2 = collector.alloc(sizeof(int) * array_size,
                                      std::align_val_t{alignof(int)});
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    for (int j = 0; j < array_size; ++j) {
        reinterpret_cast<int*>(int_array2.as_ptr())[j] = 1000 + j;
    }
    for (int j = 0; j < array_size; ++j) {
        ASSERT_EQ(reinterpret_cast<int*>(int_array2.as_ptr())[j], 1000 + j);
    }
}

TYPED_TEST(CopyTest, ArrayCollect)
{
    auto collector = gcpp::CopyingCollector<TypeParam>{1024};
    auto int_array1 =
        collector.alloc(sizeof(int) * 100, std::align_val_t{alignof(int)});
    for (int i = 0; i < 100; ++i) {
        reinterpret_cast<int*>(int_array1.as_ptr())[i] = i;
    }

    for (int i = 0; i < 64; ++i) {
        alloc_array(collector);
    }

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(reinterpret_cast<int*>(int_array1.as_ptr())[i], i);
    }
}