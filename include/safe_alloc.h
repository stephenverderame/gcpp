#pragma once
#include "gc_base.h"
namespace gcpp
{

FatPtr alloc(size_t size, std::align_val_t alignment = std::align_val_t{1});
void collect();
}  // namespace gcpp