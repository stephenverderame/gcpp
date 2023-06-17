#pragma once
#include <atomic>
#include <future>
#include <mutex>

#include "collector.h"
#include "gc_base.h"
#include "task.inl"
namespace gcpp
{
class ConcurrentGCPolicy
{
  public:
    using gc_size_t = std::atomic<size_t>;
    using gc_uint8_t = std::atomic<uint8_t>;

  private:
    std::mutex m_mutex;
    Task<CollectionResultT> m_collect_task;
    std::future<CollectionResultT> m_collect_fut;

  public:
    auto lock() noexcept { return std::unique_lock(m_mutex); }

    template <typename T>
    requires std::invocable<T>
    auto do_concurrent(T fn)
    {
        lock();
        return fn();
    }

    void wait_for_collection()
    {
        if (m_collect_fut.valid()) {
            m_collect_fut.wait();
        }
    }

    void do_collection(std::function<CollectionResultT()> collect)
    {
        m_collect_fut = m_collect_task.push_work(collect);
    }
};
static_assert(CollectorLockingPolicy<ConcurrentGCPolicy>);

class SerialGCPolicy
{
  public:
    using gc_size_t = size_t;
    using gc_uint8_t = uint8_t;

  public:
    auto lock() noexcept { return 0; }

    template <typename T>
    requires std::invocable<T>
    auto do_concurrent(T fn)
    {
        return fn();
    }

    void wait_for_collection() {}

    void do_collection(std::function<CollectionResultT()> collect)
    {
        collect();
    }
};
static_assert(CollectorLockingPolicy<SerialGCPolicy>);
}  // namespace gcpp