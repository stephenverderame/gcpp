#include <gtest/gtest.h>

#include <limits>
#include <thread>

#include "gc_scan.h"
#include "safe_alloc.h"
#include "safe_ptr.h"

std::mutex g_mu;

void thread_alloc(size_t thread_id)
{
    GC_UPDATE_STACK_RANGE();
    for (size_t i = 0; i < 1000; ++i) {
        auto array = gcpp::make_safe<int[]>(1000u);
        ASSERT_NE(array, nullptr);
        ASSERT_EQ(array.size(), 1000u);
        {
            // auto lk = std::unique_lock{g_mu};
            for (size_t j = 0; j < 1000; ++j) {
                array[j] = static_cast<int>((thread_id + 1) * i * j);
            }
        }
        size_t j = 0;
        for (auto e : array) {
            ASSERT_EQ(e, (thread_id + 1) * i * j++);
        }
    }
}

TEST(MtTest, DISABLED_MultithreadedAlloc)
{
    auto t1 = std::jthread(thread_alloc, 0);

    auto t2 = std::jthread(thread_alloc, 1);

    auto t3 = std::jthread(thread_alloc, 2);

    auto t4 = std::jthread(thread_alloc, 3);
}

TEST(MtTest, DataChanging)
{
    auto array = gcpp::make_safe<int[]>(100u);
    memset(array.get(), 0, 100 * sizeof(int));
    std::array<int, 100> local_array;
    memset(local_array.data(), 0, 100 * sizeof(int));
    auto t = std::jthread(thread_alloc, 0);
    for (int i = 0; i < 1000; ++i) {
        auto lk = std::unique_lock{g_mu};
        for (int j = 0; j < 64; ++j) {
            const auto idx =
                static_cast<size_t>(rand() % static_cast<int>(array.size()));
            const auto val = rand() % std::numeric_limits<int>::max();
            ASSERT_EQ(array[idx], local_array[idx]);
            array[idx] = val;
            local_array[idx] = val;
            ASSERT_EQ(array[idx], local_array[idx]);
        }
        for (size_t j = 0; j < array.size(); ++j) {
            ASSERT_EQ(array[j], local_array[j])
                << "For j = " << j << " and i = " << i;
        }
    }
}