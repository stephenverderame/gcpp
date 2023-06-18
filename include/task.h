#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
namespace gcpp
{
using std::placeholders::_1;
/**
 * @brief A worker thread for tasks which return a value of type `R`
 *
 * @tparam R return type
 */
template <typename R>
class Task
{
    std::condition_variable m_input;
    mutable std::mutex m_in_mut;
    std::queue<std::packaged_task<R()>> m_tasks;
    std::jthread m_thread;

    void do_work(std::stop_token stop_token);

  public:
    Task() : m_thread(std::bind(&Task::do_work, this, _1)) {}
    ~Task();
    Task(Task&& other) noexcept = delete;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&&) noexcept = delete;

    /**
     * @brief Gives work to the worker thread
     *
     * @tparam R return type
     * @param f callable
     * @return std::future<R>
     */
    std::future<R> push_work(std::function<R()> f);

    bool has_work() const;
};
}  // namespace gcpp