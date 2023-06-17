#include <new>

#include "gtest/gtest.h"
#include "safe_alloc.h"
#include "safe_ptr.h"

// TODO

TEST(SafePtr, Basic)
{
    gcpp::SafePtr<int> ptr;
    ASSERT_EQ(ptr, nullptr);
    // ASSERT_EQ(nullptr, ptr);
    ptr = gcpp::SafePtr<int>(5);
    ASSERT_NE(ptr, nullptr);
    // ASSERT_NE(nullptr, ptr);
    ASSERT_EQ(*ptr, 5);
    *ptr = 10;
    ASSERT_EQ(*ptr, 10);
    ASSERT_LT(ptr, gcpp::SafePtr<int>(0));
}

struct LinkedList {
    int val;
    gcpp::SafePtr<LinkedList> next;
};

uint32_t len(const LinkedList& n)
{
    uint32_t size = 1;
    auto ptr = n.next;
    while (ptr != nullptr) {
        ++size;
        ptr = ptr->next;
    }
    return size;
}

int sum(const LinkedList& n)
{
    int sum = n.val;
    auto ptr = n.next;
    while (ptr != nullptr) {
        sum += ptr->val;
        ptr = ptr->next;
    }
    return sum;
}

TEST(SafePtr, LinkedList)
{
    gcpp::collect();
    auto head = gcpp::make_safe<LinkedList>();
    ASSERT_EQ(
        reinterpret_cast<uintptr_t>(head.get()) & (alignof(LinkedList) - 1), 0);
    head->val = 10;
    head->next = nullptr;
    ASSERT_EQ(len(*head), 1);
    ASSERT_EQ(sum(*head), 10);
    auto n = &head->next;
    for (int i = 0; i < 10; ++i) {
        auto ptr = gcpp::make_safe<LinkedList>();
        ptr->val = i;
        ptr->next = nullptr;
        *n = ptr;
        n = &ptr->next;
    }
    ASSERT_EQ(len(*head), 11);
    ASSERT_EQ(sum(*head), 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10);
}