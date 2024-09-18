#include "timed_atomic.h"

#include <atomic>
#include <chrono>

namespace slontia {
namespace internal {

template <typename AtomicUInt32>
class shared_mutex_base
{
  public:
    void lock()
    {
        writing_num_.fetch_add(1);
        atomic_wait_until_zero([this] { return try_lock_internal_(); }, working_num_);
    }

    bool try_lock()
    {
        writing_num_.fetch_add(1);
        if (try_lock_internal_() > 0) {
            atomic_sub_and_notify_all_if_zero(writing_num_);
            return false;
        }
        return true;
    }

    void unlock()
    {
        working_num_.fetch_sub(k_writing_state);
        if (!atomic_sub_and_notify_all_if_zero(writing_num_)) {
            working_num_.notify_one();
        }
    }

    void lock_shared()
    {
        atomic_wait_until_zero([this] { return try_lock_shared_internal_(); }, writing_num_);
    }

    bool try_lock_shared()
    {
        return try_lock_shared_internal_() == 0;
    }

    void unlock_shared()
    {
        if (working_num_.fetch_sub(1) == 1 && writing_num_.load() > 0) {
            working_num_.notify_one();
        }
    }

  protected:
    static bool atomic_sub_and_notify_all_if_zero(AtomicUInt32& atom)
    {
        if (atom.fetch_sub(1) == 1) {
            atom.notify_all();
            return true;
        }
        return false;
    }

    uint32_t try_lock_internal_()
    {
        uint32_t working_num = 0;
        working_num_.compare_exchange_strong(working_num, k_writing_state);
        return working_num;
    }

    uint32_t try_lock_shared_internal_()
    {
        auto writing_num = writing_num_.load();
        if (writing_num == 0) {
            working_num_.fetch_add(1);
            if ((writing_num = writing_num_.load()) > 0) {
                unlock_shared();
            }
        }
        return writing_num;
    }

    AtomicUInt32 writing_num_{0};
    AtomicUInt32 working_num_{0};

  private:
    static constexpr uint32_t k_writing_state = 0x1000'0000;

    template <typename Fn>
    static void atomic_wait_until_zero(const Fn fn, AtomicUInt32& atom)
    {
        uint32_t current_value = 0;
        while ((current_value = fn()) > 0) {
            atom.wait(current_value);
        }
    }

};

template <typename AtomicUInt32>
class shared_timed_mutex_base : public shared_mutex_base<AtomicUInt32>
{
  public:
    template <typename Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_until(std::chrono::steady_clock::now() + timeout_duration);
    }

    template <typename Clock, class Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        writing_num_.fetch_add(1);
        if (!atomic_wait_until_zero_with_timeout(
                    [this] { return try_lock_internal_(); }, working_num_, timeout_time)) {
            shared_mutex_base<AtomicUInt32>::atomic_sub_and_notify_all_if_zero(writing_num_);
            return false;
        }
        return true;
    }

    template <typename Rep, class Period>
    bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return try_lock_shared_until(std::chrono::steady_clock::now() + timeout_duration);
    }

    template <typename Clock, class Duration>
    bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        return atomic_wait_until_zero_with_timeout(
                [this] { return try_lock_shared_internal_(); }, writing_num_, timeout_time);
    }

  private:
    using shared_mutex_base<AtomicUInt32>::try_lock_internal_;
    using shared_mutex_base<AtomicUInt32>::try_lock_shared_internal_;
    using shared_mutex_base<AtomicUInt32>::writing_num_;
    using shared_mutex_base<AtomicUInt32>::working_num_;

    template <typename Fn, typename Clock, class Duration>
    static bool atomic_wait_until_zero_with_timeout(
            const Fn fn, AtomicUInt32& atom, const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        uint32_t current_value = 0;
        while ((current_value = fn()) > 0) {
            if (!atom.wait_until(current_value, timeout_time)) {
                return false;
            }
        }
        return true;
    }
};

}

using shared_mutex = internal::shared_mutex_base<std::atomic<uint32_t>>;
using shared_timed_mutex = internal::shared_timed_mutex_base<internal::timed_atomic_uint32_t>;

}

