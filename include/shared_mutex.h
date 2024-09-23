// Copyright (c) 2024, Chang Liu <github.com/slontia>. All rights reserved.
//
// This source code is licensed under MIT (found in the LICENSE file).

#pragma once

#if __linux__
#include "timed_atomic_uint32/timed_atomic_uint32_linux.h"
#elif _WIN32
#include "timed_atomic_uint32/timed_atomic_uint32_windows.h"
#else
#error "Unsupported platform"
#endif

#include <atomic>
#include <chrono>

namespace slontia {
namespace internal {

// The `slontia::internal::shared_mutex` class template is a synchronization primitive that can be used to protect
// shared data from being simultaneously accessed by multiple threads. In contrast to other mutex types which facilitate
// exclusive access, a `slontia::internal::shared_mutex` has two levels of access:
// - shared: several threads can share ownership of the same mutex.
// - exclusive: only one thread can own the mutex.
//
// `slontia::internal::shared_mutex` is similar to `std::shared_mutex`, but has the following characteristics:
// - Concurrent acquisition for shared ownerships can be efficient;
// - Locking and unlocking in different threads is allowed;
// - Acquisition for exclusive ownership has higher priority than shared ownership;
// - Requirements of `StandardLayoutType` are not satisfied.
template <typename AtomicUInt32>
class shared_mutex
{
  public:
    // Acquires an exclusive ownership of the `shared_mutex`. If another thread is holding an exclusive lock or a shared
    // lock on the same `shared_mutex` the a call to lock will block execution until all such locks are released. While
    // `shared_mutex` is locked in an exclusive mode, no other lock of any kind can also be held.
    void lock() noexcept
    {
        // Ensure that no readers can hold shared ownerships anymore.
        increase_writing_num_();

        // Acquire an exclusive ownership.
        atomic_wait_until_zero([this] { return try_set_writing_state_to_holding_num_(); }, holding_num_);
    }

    // Tries to lock the mutex. Returns immediately. On successful lock acquisition returns true, otherwise returns
    // false.
    bool try_lock() noexcept
    {
        // Ensure that no readers can hold shared ownerships anymore.
        increase_writing_num_();

        // Try to acquire an exclusive ownership.
        if (try_set_writing_state_to_holding_num_() > 0) {
            // Fail to acquire.
            decrease_writing_num_();
            return false;
        }
        return true;
    }

    // Unlocks the mutex.
    // The mutex must be locked by a thread. The thread need not be the current thread of execution.
    void unlock() noexcept
    {
        // We should subtract `k_writing_state` from `holding_num_` rather than set zero to `holding_num_` directly.
        // The reason is that some readers can temporarily increase `holding_num_` to a higher value. If we set zero to
        // `holding_num_` here, the value of `holding_num_` will be caused downward overflow by there readers.
        holding_num_.fetch_sub(k_writing_state, std::memory_order::release);

        // Notify all waiting readers if there are no waiting writers.
        if (!decrease_writing_num_()) {
            // There are some waiting writers, so we notify one of them.
            holding_num_.notify_one();
        }
    }

    // Acquires shared ownership of the mutex. If another thread is acquiring or holding the mutex in exclusive
    // ownership, a call to `lock_shared_base` will block execution until shared ownership can be acquired.
    void lock_shared() noexcept
    {
        atomic_wait_until_zero([this] { return try_lock_shared_internal_(); }, writing_num_);
    }

    // Tries to lock the mutex in shared mode. Returns immediately. On successful lock acquisition returns true,
    // otherwise returns false.
    bool try_lock_shared() noexcept { return try_lock_shared_internal_() == 0; }

    // Releases the mutex from shared ownership by the calling thread.
    // The mutex must be locked by a thread in shared mode. The thread need not be the current thread of execution.
    void unlock_shared() noexcept
    {
        if (holding_num_.fetch_sub(1, std::memory_order::release) == 1 &&
                writing_num_.load(std::memory_order::release) > 0) {
            holding_num_.notify_one();
        }
    }

  protected:
    void increase_writing_num_() noexcept { writing_num_.fetch_add(1, std::memory_order::acquire); }

    // Notify all waiting readers if there are no waiting writers.
    // We can only decrease `writing_num_` by invoking this function. Otherwise, threads acquiring shared ownerships can
    // be blocked infinitly.
    bool decrease_writing_num_() noexcept
    {
        if (writing_num_.fetch_sub(1, std::memory_order::release) == 1) {
            // Now, there are no threads acquiring exclusive ownerships. We can notify all threads acquiring shared
            // ownerships.
            writing_num_.notify_all();
            return true;
        }
        return false;
    }

    // Set `k_writing_state` to `holding_num_` if the value of `holding_num_` is 0.
    // Return the current value of `holding_num_`. The value of 0 indicates we lock successfully.
    std::uint32_t try_set_writing_state_to_holding_num_() noexcept
    {
        // The value 0 of `holding_num_` indicates no threads are holding the shared or exclusive ownership, so we can
        // lock successfully.
        std::uint32_t holding_num = 0;
        holding_num_.compare_exchange_strong(holding_num, k_writing_state, std::memory_order::acquire);
        return holding_num;
    }

    // Increase `holding_num_` by 1 if the value of `writing_num` is 0.
    // Return the current value of `writing_num_`. The value of 0 indicates we lock in shared mode successfully.
    std::uint32_t try_lock_shared_internal_() noexcept
    {
        auto writing_num = writing_num_.load(std::memory_order::acquire);
        // Writers have higher priority than readers. Readers can hold the mutex in shared mode only when there are no
        // waiting writeres.
        if (writing_num == 0) {
            holding_num_.fetch_add(1, std::memory_order::acquire);
            // No more writers can lock the mutex since we have increased the `holding_num_`.
            // However, there can be some threads which had increased `writing_num_` before we increased `holding_num_`,
            // so we must check the value of `writing_num_` again. Otherwise, a reader and a writer can both hold the
            // mutex unexpectedly.
            if ((writing_num = writing_num_.load(std::memory_order::acquire)) > 0) [[unlikely]] {
                unlock_shared();
            }
        }
        return writing_num;
    }

    // The number of threads that are acquiring the mutex for exclusive ownership.
    AtomicUInt32 writing_num_{0};

    // The number of threads that are holding the mutex for shared ownership.
    // Besides, if the mutex is being locked for exclusive ownership, the value will be equal or greater than
    // `k_writing_state`.
    AtomicUInt32 holding_num_{0};

  private:
    // We assume that the number of readers acquiring the mutex concurrently should be less than (1 << 31). Otherwise,
    // the mutex will behave unexpectedly.
    static constexpr std::uint32_t k_writing_state = (1 << 31);

    // Blocks until the value returned by `fn` becomes 0.
    // The value returned by `fn` should be loaded from `atom`.
    template <typename Fn>
    static void atomic_wait_until_zero(const Fn fn, AtomicUInt32& atom) noexcept
    {
        std::uint32_t current_value = 0;
        while ((current_value = fn()) > 0) {
            atom.wait(current_value, std::memory_order::acquire);
        }
    }
};

// The `slontia::internal::shared_timed_mutex` class template is a synchronization primitive that can be used to protect
// shared data from being simultaneously accessed by multiple threads. In contrast to other mutex types which facilitate
// exclusive access, a `slontia::internal::shared_timed_mutex has two levels of access:
// - exclusive: only one thread can own the mutex.
// - shared: several threads can share ownership of the same mutex.
//
// `slontia::internal::shared_timed_mutex` is similar to `std::shared_timed_mutex`, but has the following
// characteristics:
// - Concurrent acquisition for shared ownerships can be efficient;
// - Locking and unlocking in different threads is allowed;
// - Acquisition for exclusive ownership has higher priority than shared ownership;
// - Requirements of `StandardLayoutType` are not satisfied.
template <typename AtomicUInt32>
class shared_timed_mutex : public shared_mutex<AtomicUInt32>
{
  public:
    // Tries to lock the mutex. Blocks until the specified duration `timeout_duration` has elapsed (timeout) or the lock
    // is acquired (owns the mutex), whichever comes first. On successful lock acquisition returns true, otherwise
    // returns false.
    template <typename Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration) noexcept
    {
        return try_lock_timeout_(timeout_duration);
    }

    // Tries to lock the mutex. Blocks until specified `timeout_time` has been reached (timeout) or the lock is acquired
    // (owns the mutex), whichever comes first. On successful lock acquisition returns true, otherwise returns false.
    template <typename Clock, class Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time) noexcept
    {
        return try_lock_timeout_(timeout_time);
    }

    // Tries to lock the mutex in shared mode. Blocks until specified `timeout_duration` has elapsed or the shared lock
    // is acquired, whichever comes first. On successful lock acquisition returns true, otherwise returns false.
    template <typename Rep, class Period>
    bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration) noexcept
    {
        return try_lock_shared_timeout_(timeout_duration);
    }

    // Tries to lock the mutex in shared mode. Blocks until specified `timeout_time` has been reached or the lock is
    // acquired, whichever comes first. On successful lock acquisition returns true, otherwise returns false.
    template <typename Clock, class Duration>
    bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& timeout_time) noexcept
    {
        return try_lock_shared_timeout_(timeout_time);
    }

  private:
    using shared_mutex<AtomicUInt32>::try_set_writing_state_to_holding_num_;
    using shared_mutex<AtomicUInt32>::try_lock_shared_internal_;
    using shared_mutex<AtomicUInt32>::writing_num_;
    using shared_mutex<AtomicUInt32>::holding_num_;

    template <typename Rep, typename Period>
    static bool atomic_wait_timeout_(
            AtomicUInt32& atom,
            const std::uint32_t expected_value,
            const std::chrono::duration<Rep, Period>& timeout_duration) noexcept
    {
        return atom.wait_for(expected_value, timeout_duration, std::memory_order::acquire);
    }

    template <typename Clock, typename Duration>
    static bool atomic_wait_timeout_(
        AtomicUInt32& atom,
        const std::uint32_t expected_value,
        const std::chrono::time_point<Clock, Duration>& timeout_time) noexcept
    {
        return atom.wait_until(expected_value, timeout_time, std::memory_order::acquire);
    }

    // Blocks until specified `timeout` has elapsed or been reached or the value returned by `fn` becomes 0.
    // The value returned by `fn` should be loaded from `atom`.
    static bool atomic_wait_until_zero_with_timeout_(
            const auto fn, AtomicUInt32& atom, const auto& timeout) noexcept
    {
        std::uint32_t current_value = 0;
        while ((current_value = fn()) > 0) {
            if (!atomic_wait_timeout_(atom, current_value, timeout)) {
                return false;
            }
        }
        return true;
    }

    // The generic function invoked by `try_lock_for` and `try_lock_until`.
    bool try_lock_timeout_(const auto& timeout) noexcept
    {
        // Ensure that no readers can hold shared ownerships anymore.
        this->increase_writing_num_();

        // Try to acquire an exclusive ownership.
        if (!atomic_wait_until_zero_with_timeout_(
                    [this] { return try_set_writing_state_to_holding_num_(); }, holding_num_, timeout)) {
            // Fail to acquire.
            this->decrease_writing_num_();
            return false;
        }
        return true;
    }

    // The generic function invoked by `try_lock_shared_for` and `try_lock_shared_until`.
    bool try_lock_shared_timeout_(const auto& timeout) noexcept
    {
        return atomic_wait_until_zero_with_timeout_(
                [this] { return try_lock_shared_internal_(); }, writing_num_, timeout);
    }
};

}

struct shared_mutex : public internal::shared_mutex<std::atomic<std::uint32_t>> {};

// Till C++23, `std::atomic<std::uint32_t>` has not support waiting with a timeout timepoint or duration yet. We use
// self-implemented `internal::timed_atomic_uint32_t` instead.
struct shared_timed_mutex : public internal::shared_timed_mutex<internal::timed_atomic_uint32_t> {};

}

