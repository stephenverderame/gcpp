#pragma once
#include <cstddef>
#include <new>
#include <type_traits>

#include "safe_alloc.h"
namespace gcpp
{
template <typename T,
          std::align_val_t AlignmentVal = std::align_val_t{alignof(T)},
          typename AllocFunc = decltype(alloc), AllocFunc Alloc = alloc>
requires std::is_trivially_destructible_v<std::remove_all_extents_t<T>> &&
         std::is_invocable_v<AllocFunc, size_t, std::align_val_t>
class SafePtr
{
  private:
    FatPtr m_ptr;

  public:
    template <typename... Args>
    explicit SafePtr(Args&&... args)
        : m_ptr(reinterpret_cast<uintptr_t>(new(Alloc(sizeof(T), AlignmentVal))
                                                T(std::forward<Args>(args)...)))
    {
    }

    SafePtr() = default;

    auto operator<=>(const SafePtr& other) const
    {
        return m_ptr.get_gc_ptr().ptr <=> other.m_ptr.get_gc_ptr().ptr;
    }

    auto operator==(std::nullptr_t) const { return m_ptr.as_ptr() == nullptr; }
    auto operator!=(std::nullptr_t) const { return m_ptr.as_ptr() != nullptr; }

    T& operator*() { return *reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T& operator*() const
    {
        return *reinterpret_cast<const T*>(m_ptr.as_ptr());
    }
    T* operator->() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T* operator->() const
    {
        return reinterpret_cast<const T*>(m_ptr.as_ptr());
    }

    T* get() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T* get() const { return reinterpret_cast<const T*>(m_ptr.as_ptr()); }

    explicit operator bool() const { return m_ptr.as_ptr() != nullptr; }
};

template <typename T, std::align_val_t AlignmentVal, typename AllocFunc,
          AllocFunc Alloc>
class SafePtr<T[], AlignmentVal, AllocFunc, Alloc>
{
  private:
    FatPtr m_ptr;
    size_t m_size = 0;

  public:
    explicit SafePtr(std::size_t size)
        : m_ptr(new(Alloc(sizeof(T) * size, AlignmentVal)) T[size]),
          m_size(size)
    {
    }

    SafePtr() = default;

    auto operator==(std::nullptr_t) const { return m_ptr.as_ptr() == nullptr; }
    auto operator!=(std::nullptr_t) const { return m_ptr.as_ptr() != nullptr; }

    T& operator[](std::size_t i)
    {
        return reinterpret_cast<T*>(m_ptr.as_ptr())[i];
    }
    const T& operator[](std::size_t i) const
    {
        return reinterpret_cast<const T*>(m_ptr.as_ptr())[i];
    }

    T* data() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T* data() const { return reinterpret_cast<const T*>(m_ptr.as_ptr()); }

    T& operator*() { return *reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T& operator*() const
    {
        return *reinterpret_cast<const T*>(m_ptr.as_ptr());
    }

    size_t size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }

    T* begin() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T* begin() const
    {
        return reinterpret_cast<const T*>(m_ptr.as_ptr());
    }
    T* end() { return reinterpret_cast<T*>(m_ptr.as_ptr()) + m_size; }
    const T* end() const
    {
        return reinterpret_cast<const T*>(m_ptr.as_ptr()) + m_size;
    }

    explicit operator bool() const { return m_ptr.as_ptr() != nullptr; }
};
}  // namespace gcpp

template <typename T, std::align_val_t AlignmentVal, typename AllocFunc,
          AllocFunc Alloc>
auto operator==(const std::nullptr_t&,
                const gcpp::SafePtr<T, AlignmentVal, AllocFunc, Alloc>& ptr)
{
    return ptr == nullptr;
}

template <typename T, std::align_val_t AlignmentVal, typename AllocFunc,
          AllocFunc Alloc>
auto operator!=(const std::nullptr_t&,
                const gcpp::SafePtr<T, AlignmentVal, AllocFunc, Alloc>& ptr)
{
    return ptr != nullptr;
}