// Copyright (c) 2024, Chang Liu <github.com/slontia>. All rights reserved.
//
// This source code is licensed under MIT (found in the LICENSE file).

#include "mutex_protect_wrapper.h"

#include <chrono>
#include <shared_mutex>

#include <gtest/gtest.h>
#include <gflags/gflags.h>


TEST(test_lock_wrapper, type_assert)
{
    slontia::mutex_protect_wrapper<int, std::shared_timed_mutex> obj;

    static_assert(std::is_same_v<int&, decltype(*obj.lock())>);
    static_assert(std::is_same_v<const int&, decltype(*obj.lock_const())>);
    static_assert(std::is_same_v<const int&, decltype(*obj.lock_shared())>);

    static_assert(std::is_same_v<int&, decltype(*obj.try_lock())>);
    static_assert(std::is_same_v<const int&, decltype(*obj.try_lock_const())>);
    static_assert(std::is_same_v<const int&, decltype(*obj.try_lock_shared())>);

    static_assert(std::is_same_v<int&, decltype(*obj.try_lock_for(std::chrono::seconds(1)))>);
    static_assert(std::is_same_v<const int&, decltype(*obj.try_lock_const_for(std::chrono::seconds(1)))>);
    static_assert(std::is_same_v<const int&, decltype(*obj.try_lock_shared_for(std::chrono::seconds(1)))>);

    static_assert(std::is_same_v<int&, decltype(*obj.try_lock_until(std::chrono::system_clock::time_point{}))>);
    static_assert(std::is_same_v<const int&, decltype(*obj.try_lock_const_until(std::chrono::system_clock::time_point{}))>);
    static_assert(std::is_same_v<const int&, decltype(*obj.try_lock_shared_until(std::chrono::system_clock::time_point{}))>);
}

TEST(test_lock_wrapper, try_lock_succeed)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;

    ASSERT_TRUE(obj.try_lock());
}

TEST(test_lock_wrapper, try_lock_failed)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;

    auto locked_obj = obj.lock();

    ASSERT_FALSE(obj.try_lock());
}

TEST(test_lock_wrapper, try_lock_shared_double_succeed)
{
    slontia::mutex_protect_wrapper<int, std::shared_mutex> obj;

    auto locked_obj = obj.lock_shared();

    ASSERT_TRUE(obj.try_lock_shared());
}

TEST(test_lock_wrapper, reset_locked_ptr)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;

    auto ptr = obj.lock();
    ptr.reset();

    ASSERT_TRUE(obj.try_lock());
}

template <template <typename T, typename ...Args> typename requirement>
void static_assert_move()
{
    using mutex_protect_wrapper = slontia::mutex_protect_wrapper<int, std::shared_mutex>;

    static_assert(requirement<mutex_protect_wrapper::locked_ptr, mutex_protect_wrapper::locked_ptr&&>::value);
    static_assert(!requirement<mutex_protect_wrapper::locked_ptr, mutex_protect_wrapper::const_locked_ptr&&>::value);
    static_assert(!requirement<mutex_protect_wrapper::locked_ptr, mutex_protect_wrapper::shared_locked_ptr&&>::value);

    static_assert(requirement<mutex_protect_wrapper::const_locked_ptr, mutex_protect_wrapper::locked_ptr&&>::value);
    static_assert(requirement<mutex_protect_wrapper::const_locked_ptr, mutex_protect_wrapper::const_locked_ptr&&>::value);
    static_assert(!requirement<mutex_protect_wrapper::const_locked_ptr, mutex_protect_wrapper::shared_locked_ptr&&>::value);

    static_assert(!requirement<mutex_protect_wrapper::shared_locked_ptr, mutex_protect_wrapper::locked_ptr&&>::value);
    static_assert(!requirement<mutex_protect_wrapper::shared_locked_ptr, mutex_protect_wrapper::const_locked_ptr&&>::value);
    static_assert(requirement<mutex_protect_wrapper::shared_locked_ptr, mutex_protect_wrapper::shared_locked_ptr&&>::value);
}

TEST(test_lock_wrapper, move_locked_ptr_static_assert)
{
    static_assert_move<std::is_constructible>();
    static_assert_move<std::is_assignable>();
}

TEST(test_lock_wrapper, move_construct_locked_ptr)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;

    auto ptr = obj.lock();
    auto ptr_2 = std::move(ptr);

    ASSERT_FALSE(obj.try_lock());
}

TEST(test_lock_wrapper, move_construct_locked_ptr_mutable_to_const)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;

    slontia::mutex_protect_wrapper<int, std::mutex>::const_locked_ptr ptr = obj.lock();

    ASSERT_FALSE(obj.try_lock());
}

TEST(test_lock_wrapper, move_assign_locked_ptr)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;
    slontia::mutex_protect_wrapper<int, std::mutex> obj_2;

    auto ptr = obj.lock();
    auto ptr_2 = obj_2.lock();
    ptr_2 = std::move(ptr);

    ASSERT_FALSE(obj.try_lock());
    ASSERT_TRUE(obj_2.try_lock());
}

TEST(test_lock_wrapper, move_assign_locked_ptr_mutable_to_const)
{
    slontia::mutex_protect_wrapper<int, std::mutex> obj;
    slontia::mutex_protect_wrapper<int, std::mutex> obj_2;

    auto ptr = obj.lock();
    auto ptr_2 = obj_2.lock_const();
    ptr_2 = std::move(ptr);

    ASSERT_FALSE(obj.try_lock());
    ASSERT_TRUE(obj_2.try_lock());
}

TEST(test_lock_wrapper, unique_locked_ptr_not_copyable)
{
    slontia::mutex_protect_wrapper<int, std::shared_mutex> obj;

    static_assert(!std::is_copy_constructible_v<decltype(obj.lock())>);
    static_assert(!std::is_copy_constructible_v<decltype(obj.lock_const())>);

    static_assert(!std::is_copy_assignable_v<decltype(obj.lock())>);
    static_assert(!std::is_copy_assignable_v<decltype(obj.lock_const())>);
}

TEST(test_lock_wrapper, shared_locked_ptr_is_copyable)
{
    slontia::mutex_protect_wrapper<int, std::shared_mutex> obj;

    auto ptr = obj.lock_shared();
    auto ptr_2 = ptr;

    ptr_2.reset();
    ASSERT_FALSE(obj.try_lock());

    ptr.reset();
    ASSERT_TRUE(obj.try_lock());
}

