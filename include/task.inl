#pragma once
#include <mutex>
#include <stop_token>

#include "task.h"

template <typename R>
void gcpp::Task<R>::do_work(std::stop_token stop_token)
{
    while (!stop_token.stop_requested()) {
        std::unique_lock lk(m_in_mut);
        while (!m_has_input) {
            m_input.wait(lk);
            if (stop_token.stop_requested()) {
                return;
            }
        }
        m_has_input = false;
        m_task();
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
    return m_has_input;
}

template <typename R>
std::future<R> gcpp::Task<R>::push_work(std::function<R()> f)
{
    {
        auto lk = std::unique_lock{m_in_mut};
        m_task = std::packaged_task<R()>{f};
        m_has_input = true;
    }
    m_input.notify_one();
    return m_task.get_future();
}