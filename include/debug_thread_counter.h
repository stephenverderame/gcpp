#pragma once
#include <atomic>
#include <stdexcept>

class ThreadCounter
{
  private:
    std::atomic<size_t>& m_counter;
    const size_t m_max_threads;

  public:
    explicit ThreadCounter(std::atomic<size_t>& counter, size_t max_threads)
        : m_counter(counter), m_max_threads(max_threads)
    {
        ++m_counter;
        if (m_counter > m_max_threads) {
            throw std::runtime_error("Too many threads");
        }
    }

    ~ThreadCounter() { --m_counter; }
    ThreadCounter(const ThreadCounter&) = delete;
    ThreadCounter& operator=(const ThreadCounter&) = delete;
    ThreadCounter(ThreadCounter&&) = delete;
    ThreadCounter& operator=(ThreadCounter&&) = delete;
};