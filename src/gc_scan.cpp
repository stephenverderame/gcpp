#include "gc_scan.h"

#include <algorithm>
#include <fstream>
#include <type_traits>
#include <unordered_set>

#include "gc_base.h"

namespace
{
template <typename Func>
inline void scan_memory(ptr_size_t begin, ptr_size_t p_end, Func f)
{
    for (auto ptr = begin & gc_ptr_alignment_mask; ptr < p_end;
         ptr += gc_ptr_alignment) {
        if (maybe_ptr(reinterpret_cast<ptr_size_t*>(ptr))) {
            f(reinterpret_cast<ptr_size_t*>(ptr));
        }
    }
}

auto get_proc_name()
{
    std::ifstream proc("/proc/self/cmdline");
    std::string proc_name;
    proc >> proc_name;
    if (!proc_name.empty() && proc_name[proc_name.size() - 1] == '\0') {
        proc_name.pop_back();
    }
    return proc_name;
}
}  // namespace

gcpp::GCRoots::GCRoots() : m_global_roots(), m_local_roots()
{
    const auto proc_name = get_proc_name();
    std::ifstream proc("/proc/self/maps");
    std::string line;
    while (std::getline(proc, line)) {
        if (line.find(proc_name) != std::string::npos) {
            const auto access = line.substr(line.find(' ') + 1, 4);
            if (access.find('r') != std::string::npos &&
                access.find('x') == std::string::npos) {
                const auto addr_midpt = line.find('-');
                const auto addr_start = line.substr(0, addr_midpt);

                const auto addr_end = line.substr(
                    addr_midpt + 1, line.find(' ') - addr_midpt - 1);
                const auto data_start = std::stoull(addr_start, nullptr, 16);
                const auto data_end = std::stoull(addr_end, nullptr, 16);
                scan_memory(data_start, data_end, [this](auto val) {
                    m_global_roots.push_back(*(val + 1) & ptr_mask);
                });
            }
        }
    }
}

gcpp::GCRoots& gcpp::GCRoots::get_instance()
{
    if (g_instance == nullptr) {
        g_instance = std::unique_ptr<GCRoots>(new GCRoots());
    }
    return *g_instance;
}

std::vector<ptr_size_t> gcpp::GCRoots::get_roots(ptr_size_t base_ptr)
{
    ptr_size_t stack_ptr = 0;
    // move rsp into stack_ptr, AT&T syntax
    asm("mov %%rsp, %0 \n" : "=r"(stack_ptr));
    auto heap_begin = m_local_roots.begin();
    auto heap_end = m_local_roots.end();
    // remove anything with an address less than the stack pointer
    while (!m_local_roots.empty() && *heap_begin < stack_ptr - red_zone_size) {
        std::pop_heap(heap_begin, heap_end, std::greater<>());
        --heap_end;
    }
    // check remaining locals still contain the GC ptr metadata
    std::unordered_set<ptr_size_t> to_remove;
    for (auto it = heap_begin; it != heap_end; ++it) {
        if (!maybe_ptr(reinterpret_cast<const ptr_size_t*>(*it))) {
            to_remove.insert(*it);
        }
    }
    // remove locals that don't contain GC ptr metadata
    heap_end = std::remove_if(
        heap_begin, heap_end,
        [&to_remove](const auto& val) { return to_remove.contains(val); });
    if (heap_end < heap_begin) {
        throw std::runtime_error("heap_end < heap_begin");
    }
    m_local_roots.resize(
        static_cast<size_t>(std::distance(heap_begin, heap_end)));

    // add new locals
    scan_memory(stack_ptr - red_zone_size, base_ptr, [this](auto ptr) {
        m_local_roots.push_back(reinterpret_cast<ptr_size_t>(ptr));
        std::push_heap(m_local_roots.begin(), m_local_roots.end(),
                       std::greater<>());
    });
    auto res = m_global_roots;
    for (auto ptr : m_local_roots) {
        res.push_back(*(reinterpret_cast<ptr_size_t*>(ptr) + 1) & ptr_mask);
    }
    return res;
}