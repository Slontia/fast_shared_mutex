#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>

namespace slontia {

namespace internal {

class timed_atomic_uint32_t : public std::atomic_ref<std::uint32_t>
{
  public:
    timed_atomic_uint32_t() : std::atomic_ref<std::uint32_t>{value_} {}

    timed_atomic_uint32_t(const std::uint32_t value)
        : value_{std::move(value)}, std::atomic_ref<std::uint32_t>{value_} {}

    void wait(std::uint32_t value)
    {
        WaitOnAddress(&value_, &value, sizeof(std::uint32_t), INFINITE);
    }

    template <typename Rep, typename Period>
    bool wait_for(std::uint32_t value, const std::chrono::duration<Rep, Period>& timeout_duration)
    {
        return WaitOnAddress(&value_, &value, sizeof(std::uint32_t), 
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout_duration).count());
    }

    template <typename Clock, typename Duration>
    bool wait_until(const std::uint32_t value, const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        const auto timeout_duration = timeout_time - Clock::now();
        if (timeout_duration > decltype(timeout_duration)::zero()) {
            return wait_for(value, timeout_duration);
        }
        return *this == value;
    }

    void notify_one() { WakeByAddressSingle(&value_); }

    void notify_all() { WakeByAddressAll(&value_); }

  private:
    alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t value_;
};

}

}