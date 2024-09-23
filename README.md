## Description

fast_shared_mutex is a header-only library which provides:
- Synchronization primitive classes of `slontia::shared_mutex` and `slontia::shared_timed_mutex`.
- A wrapper class of `slontia::mutex_wrapper` to ensure thread-safety of the access to the object.

### `slontia::shared_mutex` and `slontia::shared_timed_mutex`

The `slontia::shared_mutex` and `slontia::shared_timed_mutex` class are synchronization primitives that can be used to protect shared data from being simultaneously accessed by multiple threads, in a manner similar to `std::shared_mutex` and `std::shared_timed_mutex`.

However, `slontia::shared_mutex` and `slontia::shared_timed_mutex` have some characteristics which may be different from the standard library:
- Concurrent acquisition for shared ownerships can be efficient;
- Locking and unlocking in different threads is allowed;
- Acquisition for exclusive ownership has higher priority than shared ownership;
- Requirements of `StandardLayoutType` are not satisfied.

Here is an example of `slontia::shared_mutex`.

``` c++
#include "shared_mutex.h"

#include <cassert>
#include <thread>
#include <vector>

int main()
{
    struct object { int a_{0}; int b_{0}; } obj;
    slontia::shared_mutex mtx;

    const auto read_fn = [&]
    {
        for (uint32_t i = 0; i < 100000; ++i) {
            mtx.lock_shared();
            assert(obj.a_ == obj.b_);
            mtx.unlock_shared();
        }
    };

    const auto write_fn = [&]
    {
        for (uint32_t i = 0; i < 100000; ++i) {
            mtx.lock();
            ++obj.a_;
            ++obj.b_;
            mtx.unlock();
        }
    };

    std::vector<std::jthread> threads;
    threads.emplace_back(write_fn);
    for (uint32_t i = 0; i < 10; ++i) {
        threads.emplace_back(read_fn);
    }

    return 0;
}
```

### `slontia::mutex_protect_wrapper`

The `mutex_protect_wrapper` class template wraps an object and a mutex. If one threads aims to visit the wrapped object, it must retrieve an locked pointer first, which indicates the threads has held the mutex in exclusive or shared mode. The ownership of the mutex will remain held until the locked pointer is destructed. This mechanism guarantees thread safety for concurrently accessing the object.

Here is an example of `slontia::mutex_protect_wrapper`.

``` c++
#include "mutex_protect_wrapper.h"

#include <iostream>
#include <shared_mutex>

int main()
{
    struct object { int a_{0}; int b_{0}; };
    slontia::mutex_protect_wrapper<object, std::shared_mutex> obj;

    // Lock the mutex in exclusive mode.
    obj.lock()->a_ = 1;

    // Try to lock the mutex in exclusive mode.
    if (auto locked_obj = obj.try_lock()) {
        // Lock successfully.
        locked_obj->a_ = 2;
    }

    // Lock the mutex in shared mode.
    std::cout << obj.lock_shared()->a_ << std::endl;

    // Callers can access more members by storing the returned locked pointer.
    {
        auto locked_obj = obj.lock_shared();  // the lock is held
        std::cout << locked_obj->a_ << " " << locked_obj->b_ << std::endl;
    } // the lock is released

    // It is not allowed to modify the object with a shared ownership of the mutex.
    // obj.shared_lock()->a_ = 1;

    return 0;
}
```

## Build unittest cases

Before building the unittest cases, gflags and googletest libraries should be installed first. Besides, the C++ compiler should support C++20 standard.

When all these requirements are satisfied, you can build the unittest cases by the following instructions.

``` bash
# make the building directory
mkdir build
cd build

# compile
cmake ../test/
make

# run test cases
ctest
```

