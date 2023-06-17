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

  public:
    auto lock() noexcept { return std::unique_lock(m_mutex); }

    template <typename T>
    requires std::invocable<T>
    auto do_concurrent(T fn)
    {
        lock();
        return fn();
    }

    auto do_collection(std::function<CollectionResultT()> collect)
    {
        return m_collect_task.push_work(collect);
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

    auto do_collection(std::function<CollectionResultT()> collect)
    {
        auto pt = std::packaged_task<CollectionResultT()>{collect};
        auto fut = pt.get_future();
        pt();
        return fut;
    }
};
static_assert(CollectorLockingPolicy<SerialGCPolicy>);
}  // namespace gcpp