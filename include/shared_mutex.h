#if __linux__
#include "timed_atomic_uint32/timed_atomic_uint32_linux.h"
#elif _WIN32
#include "timed_atomic_uint32/timed_atomic_uint32_windows.h"
#else
#error "Not supported platform"
#endif

#include <atomic>
#include <chrono>

namespace slontia {
namespace internal {

template <typename AtomicUInt32>
class shared_mutex_base
{
  public:
    void lock() noexcept
    {
        writing_num_.fetch_add(1, std::memory_order::acquire);
        atomic_wait_until_zero([this] { return try_lock_internal_(); }, working_num_);
    }

    bool try_lock() noexcept
    {
        writing_num_.fetch_add(1, std::memory_order::acquire);
        if (try_lock_internal_() > 0) {
            atomic_sub_and_notify_all_if_zero(writing_num_);
            return false;
        }
        return true;
    }

    void unlock() noexcept
    {
        working_num_.fetch_sub(k_writing_state, std::memory_order::release);
        if (!atomic_sub_and_notify_all_if_zero(writing_num_)) {
            working_num_.notify_one();
        }
    }

    void lock_shared() noexcept
    {
        atomic_wait_until_zero([this] { return try_lock_shared_internal_(); }, writing_num_);
    }

    bool try_lock_shared() noexcept
    {
        return try_lock_shared_internal_() == 0;
    }

    void unlock_shared() noexcept
    {
        if (working_num_.fetch_sub(1, std::memory_order::release) == 1 &&
                writing_num_.load(std::memory_order::release) > 0) {
            working_num_.notify_one();
        }
    }

  protected:
    static bool atomic_sub_and_notify_all_if_zero(AtomicUInt32& atom) noexcept
    {
        if (atom.fetch_sub(1, std::memory_order::release) == 1) {
            atom.notify_all();
            return true;
        }
        return false;
    }

    std::uint32_t try_lock_internal_() noexcept
    {
        std::uint32_t working_num = 0;
        working_num_.compare_exchange_strong(working_num, k_writing_state, std::memory_order::acquire);
        return working_num;
    }

    std::uint32_t try_lock_shared_internal_() noexcept
    {
        auto writing_num = writing_num_.load(std::memory_order::acquire);
        if (writing_num == 0) {
            working_num_.fetch_add(1, std::memory_order::acquire);
            if ((writing_num = writing_num_.load(std::memory_order::acquire)) > 0) [[unlikely]] {
                unlock_shared();
            }
        }
        return writing_num;
    }

    AtomicUInt32 writing_num_{0};
    AtomicUInt32 working_num_{0};

  private:
    static constexpr std::uint32_t k_writing_state = 0x1000'0000;

    template <typename Fn>
    static void atomic_wait_until_zero(const Fn fn, AtomicUInt32& atom) noexcept
    {
        std::uint32_t current_value = 0;
        while ((current_value = fn()) > 0) {
            atom.wait(current_value, std::memory_order::acquire);
        }
    }
};

template <typename AtomicUInt32>
class shared_timed_mutex_base : public shared_mutex_base<AtomicUInt32>
{
  public:
    template <typename Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration) noexcept
    {
        return try_lock_timeout_(timeout_duration);
    }

    template <typename Clock, class Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time) noexcept
    {
        return try_lock_timeout_(timeout_time);
    }

    template <typename Rep, class Period>
    bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration) noexcept
    {
        return try_lock_shared_timeout_(timeout_duration);
    }

    template <typename Clock, class Duration>
    bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& timeout_time) noexcept
    {
        return try_lock_shared_timeout_(timeout_time);
    }

  private:
    using shared_mutex_base<AtomicUInt32>::try_lock_internal_;
    using shared_mutex_base<AtomicUInt32>::try_lock_shared_internal_;
    using shared_mutex_base<AtomicUInt32>::writing_num_;
    using shared_mutex_base<AtomicUInt32>::working_num_;

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

    bool try_lock_timeout_(const auto& timeout) noexcept
    {
        writing_num_.fetch_add(1, std::memory_order::acquire);
        if (!atomic_wait_until_zero_with_timeout_(
                    [this] { return try_lock_internal_(); }, working_num_, timeout)) {
            shared_mutex_base<AtomicUInt32>::atomic_sub_and_notify_all_if_zero(writing_num_);
            return false;
        }
        return true;
    }

    bool try_lock_shared_timeout_(const auto& timeout) noexcept
    {
        return atomic_wait_until_zero_with_timeout_(
                [this] { return try_lock_shared_internal_(); }, writing_num_, timeout);
    }
};

}

using shared_mutex = internal::shared_mutex_base<std::atomic<std::uint32_t>>;
using shared_timed_mutex = internal::shared_timed_mutex_base<internal::timed_atomic_uint32_t>;

}

