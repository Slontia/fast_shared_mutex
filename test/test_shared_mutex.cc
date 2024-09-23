// Copyright (c) 2024, Chang Liu <github.com/slontia>. All rights reserved.
//
// This source code is licensed under MIT (found in the LICENSE file).

#include "shared_mutex.h"

#include <gtest/gtest.h>
#include <gflags/gflags.h>

#include <thread>
#include <shared_mutex>

enum class lock_mode { k_lock, k_try_lock, k_try_lock_for, k_try_lock_until };

template <lock_mode Mode>
struct try_lock_func
{
    template <typename Mutex>
    static bool try_lock(Mutex& mutex);

    template <typename Mutex>
    static bool try_lock_shared(Mutex& mutex);
};

template <>
template <typename Mutex>
bool try_lock_func<lock_mode::k_try_lock>::try_lock(Mutex& mutex)
{
    return mutex.try_lock();
}

template <>
template <typename Mutex>
bool try_lock_func<lock_mode::k_try_lock_for>::try_lock(Mutex& mutex)
{
    return mutex.try_lock_for(std::chrono::milliseconds(1));
}

template <>
template <typename Mutex>
bool try_lock_func<lock_mode::k_try_lock_until>::try_lock(Mutex& mutex)
{
    return mutex.try_lock_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
}

template <>
template <typename Mutex>
bool try_lock_func<lock_mode::k_try_lock>::try_lock_shared(Mutex& mutex)
{
    return mutex.try_lock_shared();
}

template <>
template <typename Mutex>
bool try_lock_func<lock_mode::k_try_lock_for>::try_lock_shared(Mutex& mutex)
{
    return mutex.try_lock_shared_for(std::chrono::milliseconds(1));
}

template <>
template <typename Mutex>
bool try_lock_func<lock_mode::k_try_lock_until>::try_lock_shared(Mutex& mutex)
{
    return mutex.try_lock_shared_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
}

template <lock_mode Mode>
struct lock_func
{
    template <typename Mutex>
    static void lock(Mutex& mutex)
    {
        ASSERT_TRUE((try_lock_func<Mode>::try_lock(mutex)));
    }

    template <typename Mutex>
    static void lock_shared(Mutex& mutex)
    {
        ASSERT_TRUE((try_lock_func<Mode>::try_lock_shared(mutex)));
    }
};

template <>
template <typename Mutex>
void lock_func<lock_mode::k_lock>::lock(Mutex& mutex)
{
    mutex.lock();
}

template <>
template <typename Mutex>
void lock_func<lock_mode::k_lock>::lock_shared(Mutex& mutex)
{
    mutex.lock_shared();
}

// The `shared_mutex_wrapper` class template behaves as a common shared mutex.
template <typename SharedMutex, lock_mode LockMode, lock_mode TryLockMode>
class shared_mutex_wrapper
{
  public:
    void lock() { lock_func<LockMode>::lock(mutex_); }

    void lock_shared() { lock_func<LockMode>::lock_shared(mutex_); }

    bool try_lock() { return try_lock_func<TryLockMode>::try_lock(mutex_); }

    bool try_lock_shared() { return try_lock_func<TryLockMode>::try_lock_shared(mutex_); }

    void unlock() { mutex_.unlock(); }

    void unlock_shared() { mutex_.unlock_shared(); }

  private:
    SharedMutex mutex_;
};

template <typename ...Tuples>
struct merge_tuples;

template <typename ...Tuple>
using merge_tuples_t = merge_tuples<Tuple...>::type;

template <typename ...Types1, typename ...Types2, typename ...Tuples>
struct merge_tuples<testing::Types<Types1...>, testing::Types<Types2...>, Tuples...>
{
    using type = merge_tuples<testing::Types<Types1..., Types2...>, Tuples...>::type;
};

template <typename Tuple>
struct merge_tuples<Tuple> { using type = Tuple; };

template <lock_mode ...LockModes>
using lock_mode_sequence = std::integer_sequence<lock_mode, LockModes...>;

template <typename SharedMutexTuple, typename LockModeSequence, typename TryLockModeSequence>
struct shared_mutex_tuple;

template <typename SharedMutexTuple, typename LockModeSequence, typename TryLockModeSequence>
using shared_mutex_tuple_t = shared_mutex_tuple<SharedMutexTuple, LockModeSequence, TryLockModeSequence>::type;

template <typename SharedMutex, lock_mode LockMode, lock_mode ...TryLockModes>
struct shared_mutex_tuple<SharedMutex, lock_mode_sequence<LockMode>, lock_mode_sequence<TryLockModes...>>
{
    using type = testing::Types<shared_mutex_wrapper<SharedMutex, LockMode, TryLockModes>...>;
};

template <typename SharedMutex, lock_mode ...LockModes, typename TryLockModeSequence>
requires (sizeof...(LockModes) > 1)
struct shared_mutex_tuple<SharedMutex, lock_mode_sequence<LockModes...>, TryLockModeSequence>
{
    using type = merge_tuples_t<shared_mutex_tuple_t<SharedMutex, lock_mode_sequence<LockModes>, TryLockModeSequence>...>;
};

using test_shared_mutex_tuple = merge_tuples_t<
        shared_mutex_tuple_t<
            slontia::shared_mutex,
            lock_mode_sequence<lock_mode::k_lock, lock_mode::k_try_lock>,
            lock_mode_sequence<lock_mode::k_try_lock>>,
        shared_mutex_tuple_t<
            slontia::shared_timed_mutex,
            lock_mode_sequence<lock_mode::k_lock, lock_mode::k_try_lock, lock_mode::k_try_lock_for, lock_mode::k_try_lock_until>,
            lock_mode_sequence<lock_mode::k_try_lock, lock_mode::k_try_lock_for, lock_mode::k_try_lock_until>>
    >;

template <typename SharedMutex>
struct test_shared_mutex : protected SharedMutex, public testing::Test {};

TYPED_TEST_SUITE(test_shared_mutex, test_shared_mutex_tuple);

TYPED_TEST(test_shared_mutex, shared_lock_for_several_times)
{
    this->lock_shared();
    ASSERT_TRUE(this->try_lock_shared());
}

TYPED_TEST(test_shared_mutex, cannot_shared_lock_when_unique_locked)
{
    this->lock();
    ASSERT_FALSE(this->try_lock_shared());
}

TYPED_TEST(test_shared_mutex, cannot_unique_lock_when_unique_locked)
{
    this->lock();
    ASSERT_FALSE(this->try_lock());
}

TYPED_TEST(test_shared_mutex, cannot_unique_lock_when_shared_locked)
{
    this->lock();
    ASSERT_FALSE(this->try_lock_shared());
}

TYPED_TEST(test_shared_mutex, cannot_shared_lock_when_unique_unlocked)
{
    this->lock();
    this->unlock();
    ASSERT_TRUE(this->try_lock_shared());
}

TYPED_TEST(test_shared_mutex, cannot_unique_lock_until_unique_unlocked)
{
    this->lock();
    this->unlock();
    ASSERT_TRUE(this->try_lock());
}

TYPED_TEST(test_shared_mutex, cannot_unique_lock_until_all_shared_unlocked)
{
    this->lock_shared();
    this->lock_shared();
    this->unlock_shared();
    EXPECT_FALSE(this->try_lock());
    this->unlock_shared();
    EXPECT_TRUE(this->try_lock());
}

TYPED_TEST(test_shared_mutex, lock_and_unlock_in_different_thread)
{
    this->lock();
    std::jthread{[this] { this->unlock(); }};
    EXPECT_TRUE(this->try_lock());
}

TYPED_TEST(test_shared_mutex, lock_and_unlock_shared_in_different_thread)
{
    this->lock_shared();
    std::jthread{[this] { this->unlock_shared(); }};
    EXPECT_TRUE(this->try_lock());
}

