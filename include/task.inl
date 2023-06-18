#pragma once
#include <mutex>
#include <stop_token>

#include "task.h"

template <typename R>
void gcpp::Task<R>::do_work(std::stop_token stop_token)
{
    while (!stop_token.stop_requested()) {
        std::unique_lock lk(m_in_mut);
        while (m_tasks.empty()) {
            m_input.wait(lk);
            if (stop_token.stop_requested()) {
                return;
            }
        }
        auto& task = m_tasks.front();
        lk.unlock();
        printf("Collecting\n");
        task();
        printf("Collected\n");
        lk.lock();
        m_tasks.pop();
    }
}

template <typename R>
gcpp::Task<R>::~Task<R>()
{
    m_thread.request_stop();
    m_input.notify_all();
}

template <typename R>
bool gcpp::Task<R>::has_work() const
{
    std::lock_guard g(m_in_mut);
    return !m_tasks.empty();
}

template <typename R>
std::future<R> gcpp::Task<R>::push_work(std::function<R()> f)
{
    std::future<R> res;
    {
        auto lk = std::unique_lock{m_in_mut};
        m_tasks.emplace(f);
        res = m_tasks.back().get_future();
    }
    m_input.notify_one();
    return res;
}