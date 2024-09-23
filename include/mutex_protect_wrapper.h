// Copyright (c) 2024, Chang Liu <github.com/slontia>. All rights reserved.
//
// This source code is licensed under MIT (found in the LICENSE file).

#pragma once

#include <chrono>
#include <utility>

namespace slontia {

class mutex_protect_wrapper_base
{
    template <typename T, typename Mutex>
    friend class mutex_protect_wrapper;

  public:
    enum class lock_type { unique_mutable, unique_const, shared_const };

  private:
    template <lock_type k_type>
    class lock_helper;

    mutex_protect_wrapper_base() = default;
};

template <>
struct mutex_protect_wrapper_base::lock_helper<mutex_protect_wrapper_base::lock_type::shared_const>
{
    static void lock(auto& mutex) { mutex.lock_shared(); }

    static bool try_lock(auto& mutex) { return mutex.try_lock_shared(); }

    template <typename Rep, class Period>
    static bool try_lock(auto& mutex, const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return mutex.try_lock_shared_for(timeout_duration);
    }

    template <typename Clock, class Duration>
    static bool try_lock(auto& mutex, const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return mutex.try_lock_shared_until(timeout_time);
    }

    static void unlock(auto& mutex) { mutex.unlock_shared(); }
};

template <mutex_protect_wrapper_base::lock_type k_type>
requires (k_type == mutex_protect_wrapper_base::lock_type::unique_mutable || k_type == mutex_protect_wrapper_base::lock_type::unique_const)
struct mutex_protect_wrapper_base::lock_helper<k_type>
{
    static void lock(auto& mutex) { mutex.lock(); }

    static bool try_lock(auto& mutex) { return mutex.try_lock(); }

    template <typename Rep, class Period>
    static bool try_lock(auto& mutex, const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return mutex.try_lock_for(timeout_duration);
    }

    template <typename Clock, class Duration>
    static bool try_lock(auto& mutex, const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return mutex.try_lock_until(timeout_time);
    }

    static void unlock(auto& mutex) { mutex.unlock(); }
};

// The `mutex_protect_wrapper` class template wraps an object and a mutex. If one threads aims to visit the wrapped
// object, it must retrieve an locked pointer first, which indicates the threads has held the mutex in exclusive or
// shared mode. The ownership of the mutex will remain held until the locked pointer is destructed. This mechanism
// guarantees thread safety for concurrently accessing the object.
// `mutex_protect_wrapper` is neither copyable nor movable.
template <typename T, typename Mutex>
class mutex_protect_wrapper : private mutex_protect_wrapper_base
{
    template <lock_type k_type>
    class locked_ptr_template;

  public:
    // `locked_ptr` locks the mutex in exclusive mode.
    using locked_ptr = locked_ptr_template<lock_type::unique_mutable>;

    // `const_locked_ptr` locks the mutex in exclusive mode. The reference dereferenced by the pointer is immutable.
    using const_locked_ptr = locked_ptr_template<lock_type::unique_const>;

    // `shared_locked_ptr` locks the mutex in shared mode. The reference dereferenced by the pointer is immutable.
    using shared_locked_ptr = locked_ptr_template<lock_type::shared_const>;

    using object_type = T;

    using mutex_type = Mutex;

    // Constructs a `mutex_protect_wrapper` object. The object of type `T` contains in `*this` is initialized from the
    // arguments `std::forward<Args>(args)...`. The mutex of type `Mutex` is default-initialized.
    template <typename ...Args>
    explicit mutex_protect_wrapper(Args&& ...args) : obj_{std::forward<Args>(args)...} {}

    mutex_protect_wrapper(const mutex_protect_wrapper&) = delete;
    mutex_protect_wrapper(mutex_protect_wrapper&&) = delete;

    mutex_protect_wrapper& operator=(const mutex_protect_wrapper&) = delete;
    mutex_protect_wrapper& operator=(mutex_protect_wrapper&&) = delete;

    // Locks the mutex in exclusive mode and returns a `locked_ptr` which points to the object. The returned
    // `locked_ptr` is never null.
    auto lock() { return lock_<lock_type::unique_mutable>(); }

    // Tries to lock the mutex in exclusive mode without blocking. On successful lock acquisition returns a `locked_ptr`
    // which points to the object, otherwise returns a null `locked_ptr`.
    auto try_lock() { return try_lock_<lock_type::unique_mutable>(); }

    // Tries to lock the mutex in exclusive mode. Blocks until specified `timeout_duration` has elapsed or the lock is
    // acquired, whichever comes first. On successful lock acquisition returns a `locked_ptr` which points to the
    // object, otherwise returns a null `locked_ptr`.
    template <typename Rep, class Period>
    auto try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_<lock_type::unique_mutable>(timeout_duration);
    }

    // Tries to lock the associated mutex in exclusive mode. Blocks until specified `timeout_time` has been reached or
    // the lock is acquired, whichever comes first. On successful lock acquisition returns a `locked_ptr` which points
    // to the object, otherwise returns a null `locked_ptr`.
    template <typename Clock, class Duration>
    auto try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return try_lock_<lock_type::unique_mutable>(timeout_time);
    }

    // Locks the mutex in exclusive mode and returns a `const_locked_ptr` which points to the object. The returned
    // `const_locked_ptr` is never null.
    auto lock_const() { return lock_<lock_type::unique_const>(); }

    // Tries to lock the mutex in exclusive mode without blocking. On successful lock acquisition returns a
    // `const_locked_ptr` which points to the object, otherwise returns a null `const_locked_ptr`.
    auto try_lock_const() { return try_lock_<lock_type::unique_const>(); }

    // Tries to lock the mutex in exclusive mode. Blocks until specified `timeout_duration` has elapsed or the lock is
    // acquired, whichever comes first. On successful lock acquisition returns a `const_locked_ptr` which points to the
    // object, otherwise returns a null `const_locked_ptr`.
    template <typename Rep, class Period>
    auto try_lock_const_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_<lock_type::unique_const>(timeout_duration);
    }

    // Tries to lock the mutex in exclusive mode. Blocks until specified `timeout_time` has been reached or the lock is
    // acquired, whichever comes first. On successful lock acquisition returns a `const_locked_ptr` which points to the
    // object, otherwise returns a null `const_locked_ptr`.
    template <typename Clock, class Duration>
    auto try_lock_const_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return try_lock_<lock_type::unique_const>(timeout_time);
    }

    // Locks the mutex in shared mode and returns a `shared_locked_ptr` which points to the object. The returned
    // `shared_locked_ptr` is never null.
    auto lock_shared() { return lock_<lock_type::shared_const>(); }

    // Tries to lock the mutex in shared mode without blocking. On successful lock acquisition returns a
    // `shared_locked_ptr` which points to the object, otherwise returns a null `shared_locked_ptr`.
    auto try_lock_shared() { return try_lock_<lock_type::shared_const>(); }

    // Tries to lock the mutex in shared mode. Blocks until specified `timeout_duration` has elapsed or the lock is
    // acquired, whichever comes first. On successful lock acquisition returns a `shared_locked_ptr` which points to the
    // object, otherwise returns a null `shared_locked_ptr`.
    template <typename Rep, class Period>
    auto try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_<lock_type::shared_const>(timeout_duration);
    }

    // Tries to lock the mutex in shared mode. Blocks until specified `timeout_time` has been reached or the lock is
    // acquired, whichever comes first. On successful lock acquisition returns a `shared_locked_ptr` which points to the
    // object, otherwise returns a null `shared_locked_ptr`.
    template <typename Clock, class Duration>
    auto try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return try_lock_<lock_type::shared_const>(timeout_time);
    }

  private:
    template <lock_type k_type>
    auto lock_()
    {
        lock_helper<k_type>::lock(mutex_);
        return locked_ptr_template<k_type>{this};
    }

    template <lock_type k_type, typename ...Args>
    auto try_lock_(Args&& ...args)
    {
        return locked_ptr_template<k_type>{
            lock_helper<k_type>::try_lock(mutex_, std::forward<Args>(args)...) ? this : nullptr};
    }

    mutable Mutex mutex_;
    T obj_;
};

// The `locked_ptr_template` class template wraps a mutex ownership and a pointer which points to the object. The held
// ownership of the mutex is exclusive when `k_type` is `unique_mutable` or `unique_const`, and shared when `k_type` is
// `shared_const`. `locked_ptr_template` does not hold any ownerships of the pointed object.
// `locked_ptr_template` is movable, and only copyable when it locks the mutex in shared mode.
template <typename T, typename Mutex>
template <mutex_protect_wrapper_base::lock_type k_type>
class mutex_protect_wrapper<T, Mutex>::locked_ptr_template
{
    template <mutex_protect_wrapper_base::lock_type>
    friend class locked_ptr_template;

    friend class mutex_protect_wrapper;

  public:
    // Constructs a null locked pointer, which does not hold any ownerships of the mutex.
    locked_ptr_template() noexcept = default;
    locked_ptr_template(std::nullptr_t) noexcept : locked_ptr_template{} {}

    // Copy constructor is disabled for a `locked_ptr_template` holding the mutex in exclusive mode.
    locked_ptr_template(const locked_ptr_template&) = delete;

    // Constructs a `locked_ptr_template` which shares ownership of the mutex managed by `o`. The constructed
    // `locked_ptr_template` points to the same object as `o`.
    locked_ptr_template(const locked_ptr_template& o)
        requires (k_type == lock_type::shared_const)
        : locked_ptr_template{o.mutex_protect_wrapper_}
    {
        if (mutex_protect_wrapper_) {
            mutex_protect_wrapper_->mutex_.lock_shared();
        }
    }

    // Move-construct a `locked_ptr_template` from `o`. Transfers the mutex ownership and assign the object pointer from
    // `o` to `this`. After the construction, `o` does not hold the mutex ownership anymore, and its stored pointer is
    // null.
    locked_ptr_template(locked_ptr_template&& o) noexcept { swap(o); }
    locked_ptr_template(locked_ptr_template<lock_type::unique_mutable>&& o) noexcept
        requires (k_type == lock_type::unique_const)
        : locked_ptr_template{o.mutex_protect_wrapper_}
    {
        o.mutex_protect_wrapper_ = nullptr;
    }

    // If `*this` points to an object, it releases the mutex ownership.
    ~locked_ptr_template()
    {
        if (mutex_protect_wrapper_) {
            lock_helper<k_type>::unlock(mutex_protect_wrapper_->mutex_);
        }
    }

    // Returns true if `*this` stores a non-null pointer, false otherwise.
    operator bool() const noexcept { return mutex_protect_wrapper_ != nullptr; }

    // Returns true if `*this` stores a null pointer, false otherwise.
    bool operator==(std::nullptr_t) const noexcept { return mutex_protect_wrapper_ == nullptr; }

    // Copy assignment operator is disabled for a `locked_ptr_template` holding the mutex in exclusive mode.
    locked_ptr_template& operator=(const locked_ptr_template&) = delete;

    // Move-assign a `locked_ptr_template` from `o`. Transfers the mutex ownership and assign the object pointer from
    // `o` to `this`. The previous mutex ownership held by `*this` is released. After the construction, `o` does not
    // hold the mutex ownership anymore, and its stored pointer is null.
    locked_ptr_template& operator=(locked_ptr_template&& o)
    {
        locked_ptr_template(std::move(o)).swap(*this);
        return *this;
    }

    // Assign a null pointer to `*this` as if by calling `reset()`.
    locked_ptr_template& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    // Dereferences the stored pointer. The behavior is undefined if the stored pointer is null.
    auto& operator*() const noexcept { return mutex_protect_wrapper_->obj_; }
    auto* operator->() const noexcept { return mutex_protect_wrapper_ ? &mutex_protect_wrapper_->obj_ : nullptr; }

    // Releases the previous mutex ownership held by `*this` and set the object pointer to a null pointer.
    void reset() { locked_ptr_template{}.swap(*this); }

    // Exchanges the mutex ownership and stored pointer values of `*this` and `r`.
    void swap(locked_ptr_template& o) noexcept { std::swap(mutex_protect_wrapper_, o.mutex_protect_wrapper_); }

  private:
    // Constructs with a `mutex_protect_wrapper`. After the construction, `*this` will lock the mutex and stores a
    // pointer to the object owned by `wrapper`.
    explicit locked_ptr_template(mutex_protect_wrapper* const wrapper) noexcept : mutex_protect_wrapper_{wrapper} {}
    explicit locked_ptr_template(const mutex_protect_wrapper* const wrapper) noexcept
        requires (k_type == lock_type::unique_const || k_type == lock_type::shared_const)
        : mutex_protect_wrapper_{wrapper} {}

    std::conditional_t<k_type == lock_type::unique_mutable, mutex_protect_wrapper, const mutex_protect_wrapper>*
        mutex_protect_wrapper_{nullptr};
};

}
