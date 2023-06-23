#pragma once
#include <sys/types.h>

#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>

#include "safe_alloc.h"
namespace gcpp
{
/**
 * @brief Gets the alignment of a given type. If the type is incomplete, returns
 * the maximum alignment for regular types.
 *
 * Contains a static constant `value` member which has the value of the
 * alignment of `T`
 *
 * @tparam T
 * @tparam typename
 * @{
 */
template <typename T, typename = void>
struct AlignmentOf {
    static constexpr auto value = std::align_val_t{alignof(std::max_align_t)};
};

template <typename T>
struct AlignmentOf<T, std::void_t<decltype(alignof(T))>> {
    static constexpr auto value = std::align_val_t{alignof(T)};
};
/** @} */

template <typename T, std::align_val_t AlignmentVal, typename AllocFunc,
          AllocFunc Alloc>
requires std::is_invocable_v<AllocFunc, size_t, std::align_val_t>
class SafePtrBase
{
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator==(
        std::nullptr_t,
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator!=(
        std::nullptr_t,
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator==(
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&,
        std::nullptr_t);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator!=(
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&,
        std::nullptr_t);

  private:
    FatPtr m_ptr;

  public:
    template <typename... Args>
    explicit SafePtrBase(Args&&... args)
        : m_ptr(reinterpret_cast<uintptr_t>(new(Alloc(sizeof(T), AlignmentVal))
                                                T(std::forward<Args>(args)...)))
    {
    }

    template <typename... Args>
    static auto make(Args&&... args)
    {
        SafePtrBase res;
        res.m_ptr = FatPtr{reinterpret_cast<uintptr_t>(new (
            Alloc(sizeof(T), AlignmentVal)) T(std::forward<Args>(args)...))};
        return res;
    }

    SafePtrBase() = default;

    SafePtrBase(std::nullptr_t) : m_ptr() {}

    auto& operator=(std::nullptr_t)
    {
        m_ptr = FatPtr{};
        return *this;
    }

    auto operator<=>(const SafePtrBase& other) const
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

    SafePtrBase clone() const
    {
        SafePtrBase res;
        res.m_ptr = FatPtr{reinterpret_cast<uintptr_t>(
            new (Alloc(sizeof(T), AlignmentVal)) T(*get()))};
        return res;
    }
};

template <typename T, std::align_val_t AlignmentVal, typename AllocFunc,
          AllocFunc Alloc>
requires std::is_invocable_v<AllocFunc, size_t, std::align_val_t>
class SafePtrBase<T[], AlignmentVal, AllocFunc, Alloc>
{
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator==(
        std::nullptr_t,
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator!=(
        std::nullptr_t,
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator==(
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&,
        std::nullptr_t);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator!=(
        const SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>&,
        std::nullptr_t);

  private:
    FatPtr m_ptr;
    size_t m_size;

  public:
    explicit SafePtrBase(size_t size)
        : m_ptr(reinterpret_cast<uintptr_t>(
              new(Alloc(sizeof(T) * size, AlignmentVal)) T[size])),
          m_size(size)
    {
    }

    static auto make(size_t size) { return SafePtrBase{size}; }

    SafePtrBase() : m_ptr(), m_size(0){};

    SafePtrBase(std::nullptr_t) : m_ptr() {}

    auto& operator=(std::nullptr_t)
    {
        m_ptr = FatPtr{};
        return *this;
    }

    auto operator<=>(const SafePtrBase& other) const
    {
        return m_ptr.get_gc_ptr().ptr <=> other.m_ptr.get_gc_ptr().ptr;
    }

    auto operator==(std::nullptr_t) const { return m_ptr.as_ptr() == nullptr; }
    auto operator!=(std::nullptr_t) const { return m_ptr.as_ptr() != nullptr; }

    T& operator[](size_t index)
    {
        return reinterpret_cast<T*>(m_ptr.as_ptr())[index];
    }
    const T& operator[](size_t index) const
    {
        return reinterpret_cast<const T*>(m_ptr.as_ptr())[index];
    }

    T* get() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
    const T* get() const { return reinterpret_cast<const T*>(m_ptr.as_ptr()); }

    explicit operator bool() const { return m_ptr.as_ptr() != nullptr; }

    auto size() const { return m_size; }
    const T* begin() const { return get(); }
    const T* end() const { return get() + m_size; }
    T* begin() { return get(); }
    T* end() { return get() + m_size; }

    T& at(size_t index)
    {
        if (index >= m_size) {
            throw std::out_of_range("Index out of range");
        }
        return (*this)[index];
    }

    const T& at(size_t index) const
    {
        if (index >= m_size) {
            throw std::out_of_range("Index out of range");
        }
        return (*this)[index];
    }

    SafePtrBase clone() const
    {
        SafePtrBase res;
        res.m_ptr = FatPtr{reinterpret_cast<uintptr_t>(
            new (Alloc(sizeof(T) * m_size, AlignmentVal)) T[m_size])};
        res.m_size = m_size;
        for (size_t i = 0; i < m_size; ++i) {
            res[i] = (*this)[i];
        }
        return res;
    }
};

template <typename T, std::align_val_t AlignmentVal = AlignmentOf<T>::value,
          typename AllocFunc = decltype(&alloc), AllocFunc Alloc = alloc>
using SafePtr = SafePtrBase<T, AlignmentVal, AllocFunc, Alloc>;

template <typename T, typename... Args>
auto make_safe(Args&&... args)
{
    return SafePtr<T>::make(std::forward<Args>(args)...);
}

template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator==(
    std::nullptr_t,
    const gcpp::SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>& ptr)
{
    return FatPtr{} == ptr.m_ptr;
}
template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator!=(
    std::nullptr_t,
    const gcpp::SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>& ptr)
{
    return FatPtr{} != ptr;
}
template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator==(
    const gcpp::SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>& ptr,
    std::nullptr_t)
{
    return FatPtr{} == ptr;
}
template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator!=(
    const gcpp::SafePtrBase<U, AlignmentValF, AllocFuncF, AllocF>& ptr,
    std::nullptr_t)
{
    return FatPtr{} == ptr;
}
}  // namespace gcpp