#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <ranges>

#include "copy_collector.h"
#include "copy_collector.inl"
#include "gc_base.h"
#include "gc_scan.h"

void alloc_test(size_t total_size, std::function<size_t()> get_size,
                uint8_t num_allocs)
{
    gcpp::CopyingCollector collector(total_size, std::nullopt);

    std::vector<std::tuple<FatPtr, size_t, uint8_t>> ptrs;
    for (int i = 0; i < num_allocs; ++i) {
        const auto obj_size = get_size();
        auto ptr = collector.alloc(obj_size);
        auto data = collector.access(ptr);
        memset(data, i + 1, obj_size);
        ptrs.emplace_back(ptr, obj_size, i + 1);
    }
    std::shuffle(ptrs.begin(), ptrs.end(),
                 std::mt19937{std::random_device{}()});
    for (auto [ptr, obj_size, obj_data] : ptrs) {
        const auto data = collector.access(ptr);
        for (auto j = decltype(obj_size){0}; j < obj_size; ++j) {
            ASSERT_EQ(static_cast<const uint8_t*>(data)[j], obj_data);
        }
    }
}
TEST(CopyTest, Alloc)
{
    alloc_test(
        128, []() { return 16; }, 4);
}

TEST(CopyTest, AllocLarge)
{
    alloc_test(
        1024000, []() { return 1024; }, 20);
}

TEST(CopyTest, AllocRandom)
{
    alloc_test(
        5120000, []() { return rand() % 5000 + 1; }, 100);
}

TEST(CopyTest, Collect)
{
    gcpp::CopyingCollector collector(1024, std::nullopt);
    auto persist1 = collector.alloc(16);
    const auto data1 = collector.access(persist1);
    memset(data1, 1, 16);
    for (int i = 0; i < 10; ++i) {
        auto ptr = collector.alloc(16);
        const auto data = collector.access(ptr);
        memset(data, 10 + i, 16);
    }
    auto persist2 = collector.alloc(16);
    const auto data2 = collector.access(persist2);
    memset(data2, 2, 16);
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    ASSERT_LE(collector.free_space(), 512 - 17 * 2);
    ASSERT_GT(collector.free_space(), 512 - 17 * 12);
    const auto new_data1 = collector.access(persist1);
    for (int i = 0; i < 16; ++i) {
        ASSERT_EQ(static_cast<const uint8_t*>(new_data1)[i], 1);
    }
    const auto new_data2 = collector.access(persist2);
    for (int i = 0; i < 16; ++i) {
        ASSERT_EQ(static_cast<const uint8_t*>(new_data2)[i], 2);
    }
    ASSERT_NE(new_data1, data1);
    ASSERT_NE(new_data2, data2);
}

TEST(CopyTest, LinkedList)
{
    gcpp::CopyingCollector collector(1024, std::nullopt);
    constexpr auto size = sizeof(FatPtr) + sizeof(int);
    static_assert(alignof(int) <= alignof(FatPtr));
    auto node = collector.alloc(size, std::align_val_t{alignof(FatPtr)});
    const auto head = node;
    for (int i = 0; i < 16; ++i) {
        auto next = collector.alloc(size, std::align_val_t{alignof(FatPtr)});
        auto data = collector.access(node);
        memcpy(data, &next, sizeof(next));
        memcpy(reinterpret_cast<uint8_t*>(data) + sizeof(next), &i, sizeof(i));
        node = next;
    }
    auto data = collector.access(node);
    const auto null = FatPtr{0};
    memcpy(data, &null, sizeof(node));
    int num = 16;
    memcpy(reinterpret_cast<uint8_t*>(data) + sizeof(node), &num, sizeof(num));
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    int i = 0;
    node = head;
    while (node != null) {
        data = collector.access(node);
        memcpy(&node, data, sizeof(node));
        memcpy(&num, reinterpret_cast<uint8_t*>(data) + sizeof(node),
               sizeof(num));
        ASSERT_EQ(num, i++);
    }
    ASSERT_EQ(i, 17);
}

TEST(CopyTest, AlignedAlloc)
{
    gcpp::CopyingCollector collector(1024, std::nullopt);
    auto ptr = collector.alloc(64, std::align_val_t{64});
    const auto data = collector.access(ptr);
    memset(data, 1, 64);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(data) % 64, 0);
    std::vector<FatPtr*> roots;
    GC_GET_ROOTS(roots);
    collector.collect(std::ranges::transform_view(
        roots, [](auto ptr) -> FatPtr& { return *ptr; }));
    const auto new_data = collector.access(ptr);
    ASSERT_NE(data, new_data);
    for (int i = 0; i < 64; ++i) {
        ASSERT_EQ(static_cast<const uint8_t*>(new_data)[i], 1);
    }
    ASSERT_EQ(reinterpret_cast<uintptr_t>(new_data) % 64, 0);
}