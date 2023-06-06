#include "gc_scan.h"

#include <type_traits>

#include "gc_base.h"

extern const char etext, edata, end;

std::vector<ptr_size_t> gcpp::get_roots()
{
    const auto data_start = reinterpret_cast<ptr_size_t>(&edata);
    const auto data_end = reinterpret_cast<ptr_size_t>(&end);
    const auto data_text = reinterpret_cast<ptr_size_t>(&etext);
    std::vector<ptr_size_t> roots;
    for (auto ptr = data_start & ~(std::alignment_of_v<FatPtr> - 1);
         ptr < data_end; ptr += gc_ptr_size) {
        if (maybe_ptr(reinterpret_cast<ptr_size_t*>(ptr))) {
            roots.push_back(ptr);
        }
    }
    (void)data_text;
    return roots;
}