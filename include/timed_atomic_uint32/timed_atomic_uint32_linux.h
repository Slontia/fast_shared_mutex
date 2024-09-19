#pragma once

#include <linux/futex.h>
#include <sys/syscall.h>
#include <string.h>

#include <atomic>
#include <chrono>

namespace slontia {

namespace internal {

template <typename Clock>
struct futex_wait_operator;

template <>
struct futex_wait_operator<std::chrono::steady_clock>
{
    static constexpr int value = FUTEX_WAIT;
};

template <>
struct futex_wait_operator<std::chrono::system_clock>
{
    static constexpr int value = FUTEX_WAIT | FUTEX_CLOCK_REALTIME;
};

class timed_atomic_uint32_t : public std::atomic_ref<uint32_t>
{
  public:
    timed_atomic_uint32_t() : std::atomic_ref<uint32_t>{value_} {}

    timed_atomic_uint32_t(const uint32_t value)
        : value_{std::move(value)}, std::atomic_ref<uint32_t>{value_} {}

    void wait(const uint32_t value)
    {
        syscall(SYS_futex, &value_, FUTEX_WAIT, value, nullptr, nullptr, 0);
    }

    void notify_one()
    {
        syscall(SYS_futex, &value_, FUTEX_WAKE, 1, nullptr, nullptr, 0);
    }

    void notify_all()
    {
        syscall(SYS_futex, &value_, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
    }

    template <typename Clock, class Duration>
    bool wait_until(const uint32_t value, const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(timeout_time);
        const auto ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(timeout_time) -
            std::chrono::time_point_cast<std::chrono::nanoseconds>(secs);
        timespec timeout{secs.time_since_epoch().count(), ns.count()};
        return syscall(SYS_futex, &value_, futex_wait_operator<Clock>::value, value, timeout, nullptr, 0) == 0;
    }

  private:
    alignas(std::atomic_ref<uint32_t>::required_alignment) uint32_t value_;
};

}

}
