#pragma once

#include <memory>
#include <type_traits>

namespace Ship {

template <typename T, typename Alloc = std::allocator<T>>
struct default_init_allocator : Alloc {
    using Alloc::Alloc;

    template <typename U>
    struct rebind {
        using other = default_init_allocator<U, typename std::allocator_traits<Alloc>::template rebind_alloc<U>>;
    };

    template <typename U>
    void construct(U* p) noexcept(std::is_nothrow_default_constructible_v<U>) {
        ::new (static_cast<void*>(p)) U;
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        std::allocator_traits<Alloc>::construct(static_cast<Alloc&>(*this), p, std::forward<Args>(args)...);
    }
};

} // namespace Ship
