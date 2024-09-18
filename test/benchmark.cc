#include "shared_mutex.h"
#include "mutex_wrapper.h"

#include <algorithm>
#include <numeric>
#include <latch>
#include <thread>
#include <shared_mutex>

#include <gtest/gtest.h>
#include <gflags/gflags.h>

DEFINE_uint32(read_threads, 100, "Number of threads to read");
DEFINE_uint32(try_read_threads, 100, "Number of threads to try to read");
DEFINE_uint32(try_read_1ms_threads, 100, "Number of threads to try to read for 1 millisecond");
DEFINE_uint32(write_threads, 2, "Number of threads to write");
DEFINE_uint32(try_write_threads, 2, "Number of threads to try to write");
DEFINE_uint32(try_write_1ms_threads, 2, "Number of threads to try to write for 1 millisecond");
DEFINE_uint32(operation_num, 100000, "Number of operations for each thread");
DEFINE_string(mutex, "slontia",
        "The kind of shared mutex to test, can be 'std', 'std_timed', 'slontia', 'slontia_timed'");

class object
{
  public:
    uint32_t read() const noexcept
    {
        EXPECT_EQ(a_, b_);
        return a_;
    }

    void write() noexcept
    {
        ++a_;
        ++b_;
    }

  private:
    uint32_t a_{0};
    uint32_t b_{0};
};

static bool read_object(auto& obj)
{
    obj.lock_shared()->read();
    return true;
}

static bool try_read_object(auto& obj)
{
    auto locked_obj = obj.try_lock_shared();
    if (locked_obj) {
        locked_obj->read();
    }
    return locked_obj;
}

static bool try_read_object_for_1ms(auto& obj)
{
    auto locked_obj = obj.try_lock_shared_for(std::chrono::milliseconds(1));
    if (locked_obj) {
        locked_obj->read();
    }
    return locked_obj;
}

static bool write_object(auto& obj)
{
    obj.lock()->write();
    return true;
}

static bool try_write_object(auto& obj)
{
    auto locked_obj = obj.try_lock();
    if (locked_obj) {
        locked_obj->write();
    }
    return locked_obj;
}

static bool try_write_object_for_1ms(auto& obj)
{
    auto locked_obj = obj.try_lock_for(std::chrono::milliseconds(1));
    if (locked_obj) {
        locked_obj->write();
    }
    return locked_obj;
}

template <typename T>
class dynamic_array
{
  public:
    dynamic_array() = default;

    template <typename ...Args>
    dynamic_array(const size_t size, const Args& ...args)
    {
    }

  private:
    std::unique_ptr<std::aligned_storage_t<sizeof(T), alignof(T)>[]> buffer_;
    size_t size_{0};
};

class thread_group
{
    struct thread_result
    {
        std::chrono::microseconds duration_;
        uint32_t operate_count_{0};
    };

  public:
    template <typename Task>
    thread_group(const char* const name, const uint32_t thread_num, std::latch& latch, const Task task)
        : name_{name}, thread_results_(std::make_unique<thread_result[]>(thread_num))
    {
        assert(thread_num > 0);
        for (uint32_t i = 0; i < thread_num; ++i) {
            threads_.emplace_back(&thread_group::thread_main_<Task>, std::ref(latch), task, std::ref(thread_results_[i]));
        }
    }

    void print_result()
    {
        std::ranges::for_each(threads_, &std::thread::join);
        std::cout << name_ << ": " << threads_.size() << " threads";
        print_result_("duration", &thread_result::duration_,
                [](const std::chrono::microseconds duration)
                {
                    std::cout << (static_cast<double>(duration.count()) / 1000) << "ms";
                });
        print_result_("operate rate", &thread_result::operate_count_,
                [](const uint32_t actual_operate_count)
                {
                    std::cout << (static_cast<double>(actual_operate_count) / FLAGS_operation_num * 100) << "%";
                });
        std::cout << "\n";
    }

    template <typename T>
    auto sum_item_(T thread_result::*const member_ptr) const
    {
        return std::accumulate(result_begin_(), result_end_(), T{},
                [&](const T value, const thread_result& result) { return value + result.*member_ptr; });
    }

    uint32_t actual_operate_count() const { return sum_item_(&thread_result::operate_count_); }

  private:
    template <typename Task>
    static void thread_main_(std::latch& latch, const Task task, thread_result& result)
    {
        latch.count_down();
        latch.wait();
        const auto start_ts = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < FLAGS_operation_num; ++i) {
            result.operate_count_ += task();
        }
        result.duration_ =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_ts);
    }

    thread_result* result_begin_() { return &thread_results_[0]; }
    const thread_result* result_begin_() const { return &thread_results_[0]; }

    thread_result* result_end_() { return result_begin_() + threads_.size(); }
    const thread_result* result_end_() const { return result_begin_() + threads_.size(); }

    template <typename T>
    void print_result_(const char* const item_name, T thread_result::*const member_ptr, const auto output_item)
    {
        std::sort(result_begin_(), result_end_(),
                [&](const thread_result& _1, const thread_result& _2) { return _1.*member_ptr < _2.*member_ptr; });
        std::cout << "\n  - [" << item_name << "]\tavg: ";
        output_item(sum_item_(member_ptr) / threads_.size());
        std::cout << ",\tmin: ";
        output_item(thread_results_[0].*member_ptr);
        std::cout << ",\t90%: ";
        output_item(thread_results_[threads_.size() * 0.9].*member_ptr);
        std::cout << ",\t95%: ";
        output_item(thread_results_[threads_.size() * 0.95].*member_ptr);
        std::cout << ",\t99%: ";
        output_item(thread_results_[threads_.size() * 0.99].*member_ptr);
        std::cout << ",\tmax: ";
        output_item(thread_results_[threads_.size() - 1].*member_ptr);
    }

    const char* name_{nullptr};
    std::unique_ptr<thread_result[]> thread_results_;
    std::vector<std::thread> threads_;
};

template <typename SharedMutex>
void test_main()
{
    constexpr bool support_try_lock_for =
        requires(SharedMutex mutex) { mutex.try_lock_for(std::chrono::milliseconds(1)); };
    slontia::mutex_wrapper<object, SharedMutex> obj;
    std::latch latch{
        FLAGS_read_threads + FLAGS_try_read_threads + FLAGS_write_threads + FLAGS_try_write_threads +
            (support_try_lock_for ? (FLAGS_try_read_1ms_threads + FLAGS_try_write_1ms_threads) : 0)};

    std::vector<thread_group> read_thread_groups;
    std::vector<thread_group> write_thread_groups;

    const auto insert_threads = [&](const char* const name, const uint32_t thread_num, const auto task,
            std::vector<thread_group>& thread_groups)
        {
            if (thread_num > 0) {
                thread_groups.emplace_back(name, thread_num, latch, task);
            }
        };

    insert_threads("read", FLAGS_read_threads, [&] { return read_object(obj); }, read_thread_groups);
    insert_threads("write", FLAGS_write_threads, [&] { return write_object(obj); }, write_thread_groups);
    insert_threads("try to read", FLAGS_try_read_threads, [&] { return try_read_object(obj); }, read_thread_groups);
    insert_threads("try to write", FLAGS_try_write_threads, [&] { return try_write_object(obj); }, write_thread_groups);
    if constexpr (support_try_lock_for) {
        insert_threads("try to read for 1ms", FLAGS_try_read_1ms_threads, [&] { return try_read_object_for_1ms(obj); },
                read_thread_groups);
        insert_threads("try to write for 1ms", FLAGS_try_write_1ms_threads, [&] { return try_write_object_for_1ms(obj); },
                write_thread_groups);
    }

    std::cout << "## " << typeid(SharedMutex).name() << "\n";
    std::ranges::for_each(read_thread_groups, &thread_group::print_result);
    std::ranges::for_each(write_thread_groups, &thread_group::print_result);
    std::cout << "\n";

    EXPECT_EQ(obj.lock()->read(),
            std::accumulate(write_thread_groups.begin(), write_thread_groups.end(), 0u,
                [](uint32_t sum, const thread_group& group) { return sum + group.actual_operate_count(); }));
}

TEST(TestSharedMutex, benchmark)
{
    test_main<slontia::shared_mutex>();
    test_main<slontia::shared_timed_mutex>();
    test_main<std::shared_mutex>();
    test_main<std::shared_timed_mutex>();
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
