#pragma once
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

template <typename T, std::align_val_t AlignmentVal = AlignmentOf<T>::value,
          typename AllocFunc = decltype(alloc), AllocFunc Alloc = alloc>
requires std::is_invocable_v<AllocFunc, size_t, std::align_val_t>
class SafePtr
{
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator==(
        std::nullptr_t, const SafePtr<U, AlignmentValF, AllocFuncF, AllocF>&);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator!=(
        std::nullptr_t, const SafePtr<U, AlignmentValF, AllocFuncF, AllocF>&);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator==(const SafePtr<U, AlignmentValF, AllocFuncF, AllocF>&,
                           std::nullptr_t);
    template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
              AllocFuncF AllocF>
    friend bool operator!=(const SafePtr<U, AlignmentValF, AllocFuncF, AllocF>&,
                           std::nullptr_t);

  private:
    FatPtr m_ptr;

  public:
    template <typename... Args>
    explicit SafePtr(Args&&... args)
        : m_ptr(reinterpret_cast<uintptr_t>(new(Alloc(sizeof(T), AlignmentVal))
                                                T(std::forward<Args>(args)...)))
    {
    }

    template <typename... Args>
    static auto make(Args&&... args)
    {
        SafePtr<T> res;
        res.m_ptr = FatPtr{reinterpret_cast<uintptr_t>(new (
            Alloc(sizeof(T), AlignmentVal)) T(std::forward<Args>(args)...))};
        return res;
    }

    SafePtr() = default;

    SafePtr(std::nullptr_t) : m_ptr() {}

    auto& operator=(std::nullptr_t)
    {
        m_ptr = FatPtr{};
        return *this;
    }

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

template <typename T, typename... Args>
auto make_safe(Args&&... args)
{
    return SafePtr<T>::make(std::forward<T>(args)...);
}

// template <typename T, std::align_val_t AlignmentVal, typename AllocFunc,
//           AllocFunc Alloc>
// class SafePtr<T[], AlignmentVal, AllocFunc, Alloc>
// {
//   private:
//     FatPtr m_ptr;
//     size_t m_size = 0;

//   public:
//     explicit SafePtr(std::size_t size)
//         : m_ptr(new(Alloc(sizeof(T) * size, AlignmentVal)) T[size]),
//           m_size(size)
//     {
//     }

//     SafePtr() = default;

//     auto operator==(std::nullptr_t) const { return m_ptr.as_ptr() == nullptr;
//     } auto operator!=(std::nullptr_t) const { return m_ptr.as_ptr() !=
//     nullptr; }

//     T& operator[](std::size_t i)
//     {
//         return reinterpret_cast<T*>(m_ptr.as_ptr())[i];
//     }
//     const T& operator[](std::size_t i) const
//     {
//         return reinterpret_cast<const T*>(m_ptr.as_ptr())[i];
//     }

//     T* data() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
//     const T* data() const { return reinterpret_cast<const
//     T*>(m_ptr.as_ptr()); }

//     T& operator*() { return *reinterpret_cast<T*>(m_ptr.as_ptr()); }
//     const T& operator*() const
//     {
//         return *reinterpret_cast<const T*>(m_ptr.as_ptr());
//     }

//     size_t size() const noexcept { return m_size; }
//     bool empty() const noexcept { return m_size == 0; }

//     T* begin() { return reinterpret_cast<T*>(m_ptr.as_ptr()); }
//     const T* begin() const
//     {
//         return reinterpret_cast<const T*>(m_ptr.as_ptr());
//     }
//     T* end() { return reinterpret_cast<T*>(m_ptr.as_ptr()) + m_size; }
//     const T* end() const
//     {
//         return reinterpret_cast<const T*>(m_ptr.as_ptr()) + m_size;
//     }

//     explicit operator bool() const { return m_ptr.as_ptr() != nullptr; }
// };

template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator==(std::nullptr_t,
                const gcpp::SafePtr<U, AlignmentValF, AllocFuncF, AllocF>& ptr)
{
    return FatPtr{} == ptr.m_ptr;
}
template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator!=(std::nullptr_t,
                const gcpp::SafePtr<U, AlignmentValF, AllocFuncF, AllocF>& ptr)
{
    return FatPtr{} != ptr;
}
template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator==(const gcpp::SafePtr<U, AlignmentValF, AllocFuncF, AllocF>& ptr,
                std::nullptr_t)
{
    return FatPtr{} == ptr;
}
template <typename U, std::align_val_t AlignmentValF, typename AllocFuncF,
          AllocFuncF AllocF>
bool operator!=(const gcpp::SafePtr<U, AlignmentValF, AllocFuncF, AllocF>& ptr,
                std::nullptr_t)
{
    return FatPtr{} == ptr;
}
}  // namespace gcpp