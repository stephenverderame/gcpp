#pragma once
#include <vector>

#include "gc_base.h"

namespace gcpp
{
/**
 * @brief Get the root nodes of the object graph
 *
 * @return std::vector<ptr_t>
 */
std::vector<ptr_size_t> get_roots();
}  // namespace gcpp