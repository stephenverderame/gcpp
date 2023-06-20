#include "gc_scan.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_set>

#include "gc_base.h"

namespace
{

/** Get's the path to the process executable */
auto get_proc_name()
{
    // null-terminator separated list of arguments
    std::ifstream proc("/proc/self/cmdline");
    std::string proc_name;
    proc >> proc_name;
    if (!proc_name.empty() && proc_name[proc_name.size() - 1] == '\0') {
        proc_name.pop_back();
    }
    return proc_name;
}
/** Gets the current stack pointer */
inline auto get_sp()
{
    volatile uintptr_t stack_ptr = 0;
    // move rsp into stack_ptr, AT&T syntax
    asm("mov %%rsp, %0 \n" : "=r"(stack_ptr));
    return stack_ptr;
}

auto scan_globals() noexcept
{
    // scans the "data segment" for all possible global GC ptrs
    const auto proc_name = get_proc_name();
    // contains lines of the form:
    // <start_addr>-<end_addr> <access> <other_stuff>           <process_name>
    std::ifstream proc("/proc/self/maps");
    std::string line;
    std::vector<uintptr_t> vals;
    while (std::getline(proc, line)) {
        if (line.find(proc_name) != std::string::npos) {
            // section is not a library the executable is linked with or
            // non-data segment
            const auto access = line.substr(line.find(' ') + 1, 4);
            if (access.find('r') != std::string::npos &&
                access.find('x') == std::string::npos) {
                // section is readable and not executable
                const auto addr_midpt = line.find('-');
                const auto addr_start = line.substr(0, addr_midpt);

                const auto addr_end = line.substr(
                    addr_midpt + 1, line.find(' ') - addr_midpt - 1);
                const auto data_start = std::stoull(addr_start, nullptr, 16);
                const auto data_end = std::stoull(addr_end, nullptr, 16);
                scan_memory(data_start, data_end, [&vals](auto val) {
                    vals.push_back(reinterpret_cast<uintptr_t>(val));
                });
            }
        }
    }
    return vals;
}
}  // namespace

gcpp::GCRoots::GCRoots() noexcept : m_global_roots(scan_globals()) {}

gcpp::GCRoots& gcpp::GCRoots::get_instance()
{
    if (g_instance == nullptr) {
        std::call_once(g_instance_flag, []() {
            g_instance = std::unique_ptr<GCRoots>(new GCRoots());
        });
    }
    return *g_instance;
}

/**
 * @brief Ensures all elements on the heap are still valid GC ptrs considering
 * the current stack pointer.
 *
 * @param heap_begin start of the min heap of addresses of GC ptrs on the stack
 * @param heap_end end of the min heap
 * @param stack_ptr rsp
 * @return the new end of the min heap, which will be less than or equal to
 * `heap_end`
 */
auto recheck_locals(std::vector<uintptr_t>::iterator heap_begin,
                    std::vector<uintptr_t>::iterator heap_end,
                    uintptr_t stack_ptr)
{
    // remove anything with an address less than the stack pointer
    while (heap_begin != heap_end && *heap_begin < stack_ptr - red_zone_size) {
        std::pop_heap(heap_begin, heap_end, std::greater<>());
        --heap_end;
    }
    // check remaining locals still contain the GC ptr metadata
    std::unordered_set<uintptr_t> to_remove;
    for (auto it = heap_begin; it != heap_end; ++it) {
        if (!FatPtr::maybe_ptr(reinterpret_cast<const uintptr_t*>(*it))) {
            to_remove.insert(*it);
        }
    }
    // remove locals that don't contain GC ptr metadata
    heap_end = std::remove_if(
        heap_begin, heap_end,
        [&to_remove](const auto& val) { return to_remove.contains(val); });
    // sanity check postcondition
    if (heap_end < heap_begin) {
        throw std::runtime_error("heap_end < heap_begin");
    }
    return heap_end;
}

void gcpp::GCRoots::remove_dead_locals(std::thread::id id)
{
    auto& local_roots = m_local_roots[id];
    const auto thread_sp = m_stack_ranges[id].second;
    const auto heap_begin = local_roots.begin();
    auto heap_end = local_roots.end();
    heap_end = recheck_locals(heap_begin, heap_end, thread_sp);

    // noexcept bc we are reducing size
    local_roots.resize(
        static_cast<size_t>(std::distance(heap_begin, heap_end)));
}

void gcpp::GCRoots::scan_locals(std::vector<uintptr_t>& local_roots,
                                std::thread::id id)
{
    // auto& [scanned_start, scanned_end] = m_scanned_ranges.at(id);
    const auto [stack_start, stack_end] = m_stack_ranges.at(id);
    const auto scan_callback = [&local_roots](auto ptr) {
        local_roots.push_back(reinterpret_cast<uintptr_t>(ptr));
        std::push_heap(local_roots.begin(), local_roots.end(),
                       std::greater<>());
    };
    /*if (scanned_start == 0 && scanned_end == 0) {
        // nothing scanned yet
        scan_memory(stack_end - red_zone_size, stack_start + 1, scan_callback);
    } else {
        if (stack_start > scanned_start) {
            // stack_start -- larger
            // ...
            // scanned_start -- smaller
            scan_memory(scanned_start - sizeof(FatPtr), stack_start + 1,
                        scan_callback);
        }
        if (stack_end < scanned_end) {
            // scanned_end -- larger
            // ...
            // stack_end -- smaller
            scan_memory(stack_end - red_zone_size, scanned_end + 1,
                        scan_callback);
        }
    }
    scanned_start = std::max(scanned_start, stack_start);
    scanned_end = stack_end;*/
    local_roots.clear();
    scan_memory(stack_end - red_zone_size, stack_start + 1, scan_callback);
}

std::vector<FatPtr*> gcpp::GCRoots::get_roots(uintptr_t base_ptr)
{
    update_stack_range(base_ptr);
    auto lk = std::unique_lock{m_mutex};

    size_t total_local_roots = 0;
    for (auto& roots : m_local_roots) {
        remove_dead_locals(roots.first);
        scan_locals(roots.second, roots.first);
        total_local_roots += roots.second.size();
    }
    lk.unlock();
    auto res = std::vector<FatPtr*>();
    res.reserve(m_global_roots.size() + total_local_roots);
    for (auto val : m_global_roots) {
        res.push_back(reinterpret_cast<FatPtr*>(val));
    }
    auto reader_lk = std::shared_lock{m_mutex};
    for (const auto& vals : m_local_roots) {
        for (auto ptr : vals.second) {
            res.push_back(reinterpret_cast<FatPtr*>(ptr));
        }
    }
    return res;
}

void gcpp::GCRoots::update_stack_range(uintptr_t base_ptr)
{
    auto sp = get_sp();
    auto lk = std::shared_lock{m_mutex};
    bool existing_record = false;
    if (m_stack_ranges.contains(std::this_thread::get_id())) {
        const auto [stack_start, stack_end] =
            m_stack_ranges.at(std::this_thread::get_id());
        if (base_ptr <= stack_start && sp == stack_end) {
            // stack_start -- biggest
            // ...
            // base_ptr
            // ...
            // sp
            // ...
            // stack_end  -- smallest
            return;
        }
        existing_record = true;
    }
    if (existing_record) {
        auto& [stack_start, stack_end] =
            m_stack_ranges.at(std::this_thread::get_id());
        stack_start = std::max(stack_start, base_ptr);
        stack_end = sp;
    } else {
        lk.unlock();
        // gap in locks is safe because each thread has its own entry
        auto writer_lk = std::unique_lock{m_mutex};
        m_stack_ranges.insert(
            {std::this_thread::get_id(), std::make_pair(base_ptr, sp)});
        m_scanned_ranges.insert(
            {std::this_thread::get_id(), std::make_pair(0, 0)});
        m_local_roots.insert({std::this_thread::get_id(), {}});
    }
}