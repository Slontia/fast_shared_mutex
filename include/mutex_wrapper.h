// Copyright (c) 2024, Chang Liu <github.com/slontia>. All rights reserved.
//
// This source code is licensed under MIT (found in the LICENSE file).

#pragma once

#include <chrono>
#include <utility>

namespace slontia {

class mutex_wrapper_base
{
    template <typename T, typename Mutex>
    friend class mutex_wrapper;

  public:
    enum class lock_type { unique_mutable, unique_const, shared_const };

  private:
    template <lock_type k_type>
    class lock_helper;

    mutex_wrapper_base() = default;
};

template <>
struct mutex_wrapper_base::lock_helper<mutex_wrapper_base::lock_type::shared_const>
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

template <mutex_wrapper_base::lock_type k_type>
requires (k_type == mutex_wrapper_base::lock_type::unique_mutable || k_type == mutex_wrapper_base::lock_type::unique_const)
struct mutex_wrapper_base::lock_helper<k_type>
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

template <typename T, typename Mutex>
class mutex_wrapper : private mutex_wrapper_base
{
    template <lock_type k_type>
    class locked_ptr_template;

  public:
    using locked_ptr = locked_ptr_template<lock_type::unique_mutable>;
    using const_locked_ptr = locked_ptr_template<lock_type::unique_const>;
    using shared_locked_ptr = locked_ptr_template<lock_type::shared_const>;

    using element_type = T;
    using mutex_type = Mutex;

    template <typename ...Args>
    explicit mutex_wrapper(Args&& ...args) : obj_{std::forward<Args>(args)...}
    {
    }

    mutex_wrapper(const mutex_wrapper&) = delete;
    mutex_wrapper(mutex_wrapper&&) = delete;

    mutex_wrapper& operator=(const mutex_wrapper&) = delete;
    mutex_wrapper& operator=(mutex_wrapper&&) = delete;

    // access the mutable object with a unique lock

    auto lock() { return lock_<lock_type::unique_mutable>(); }

    auto try_lock() { return try_lock_<lock_type::unique_mutable>(); }

    template <typename Rep, class Period>
    auto try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_<lock_type::unique_mutable>(timeout_duration);
    }

    template <typename Clock, class Duration>
    auto try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return try_lock_<lock_type::unique_mutable>(timeout_time);
    }

    // access the constant object with a unique lock

    auto lock_const() { return lock_<lock_type::unique_const>(); }

    auto try_lock_const() { return try_lock_<lock_type::unique_const>(); }

    template <typename Rep, class Period>
    auto try_lock_const_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_<lock_type::unique_const>(timeout_duration);
    }

    template <typename Clock, class Duration>
    auto try_lock_const_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return try_lock_<lock_type::unique_const>(timeout_time);
    }

    // access the constant object with a shared lock

    auto lock_shared() { return lock_<lock_type::shared_const>(); }

    auto try_lock_shared() { return try_lock_<lock_type::shared_const>(); }

    template <typename Rep, class Period>
    auto try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_<lock_type::shared_const>(timeout_duration);
    }

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

template <typename T, typename Mutex>
template <mutex_wrapper_base::lock_type k_type>
class mutex_wrapper<T, Mutex>::locked_ptr_template
{
    template <mutex_wrapper_base::lock_type>
    friend class locked_ptr_template;

    friend class mutex_wrapper;

  public:
    locked_ptr_template() noexcept = default;

    locked_ptr_template(std::nullptr_t) noexcept : locked_ptr_template{} {}

    locked_ptr_template(const locked_ptr_template&) = delete;

    locked_ptr_template(const locked_ptr_template& o)
        requires (k_type == lock_type::shared_const)
        : locked_ptr_template{o.mutex_wrapper_}
    {
        if (mutex_wrapper_) {
            mutex_wrapper_->mutex_.lock_shared();
        }
    }

    locked_ptr_template(locked_ptr_template&& o) noexcept { swap(o); }

    locked_ptr_template(locked_ptr_template<lock_type::unique_mutable>&& o) noexcept
        requires (k_type == lock_type::unique_const)
        : locked_ptr_template{o.mutex_wrapper_}
    {
        o.mutex_wrapper_ = nullptr;
    }

    ~locked_ptr_template()
    {
        if (mutex_wrapper_) {
            lock_helper<k_type>::unlock(mutex_wrapper_->mutex_);
        }
    }

    operator bool() const noexcept { return mutex_wrapper_ != nullptr; }

    bool operator==(std::nullptr_t) const noexcept { return mutex_wrapper_ == nullptr; }

    locked_ptr_template& operator=(const locked_ptr_template&) = delete;

    locked_ptr_template& operator=(locked_ptr_template&& o)
    {
        locked_ptr_template(std::move(o)).swap(*this);
        return *this;
    }

    locked_ptr_template& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    void reset() { locked_ptr_template{}.swap(*this); }

    auto& operator*() const noexcept { return mutex_wrapper_->obj_; }

    auto* operator->() const noexcept { return mutex_wrapper_ ? &mutex_wrapper_->obj_ : nullptr; }

    void swap(locked_ptr_template& o) noexcept { std::swap(mutex_wrapper_, o.mutex_wrapper_); }

  private:
    explicit locked_ptr_template(mutex_wrapper* const wrapper) noexcept : mutex_wrapper_{wrapper} {}

    explicit locked_ptr_template(const mutex_wrapper* const wrapper) noexcept
        requires (k_type == lock_type::unique_const || k_type == lock_type::shared_const)
        : mutex_wrapper_{wrapper} {}

    std::conditional_t<k_type == lock_type::unique_mutable, mutex_wrapper, const mutex_wrapper>* mutex_wrapper_{nullptr};
};

}
