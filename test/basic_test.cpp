#include <gc_scan.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gc_base.h"
#include "gmock/gmock.h"
using testing::UnorderedElementsAre;

const auto test_ptr = ptr_header() + 0x1000;
// NOLINTNEXTLINE
auto test_ptr2 = ptr_header() + 0x2000;
const auto not_ptr = 0x1000;
// NOLINTNEXTLINE
auto not_ptr_2 = 0x2000;

TEST(ScanTest, GlobalTest)
{
    ASSERT_THAT(gcpp::get_roots(), UnorderedElementsAre(test_ptr, test_ptr2));
    // just using `not_ptr
    ASSERT_EQ(not_ptr, not_ptr);
}