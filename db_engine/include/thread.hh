#ifndef THREAD_POOL_HH
#define THREAD_POOL_HH

#include <vector>
#include <thread>
#include <queue>
#include <future>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t thread_count);
    ~ThreadPool();

    template <class F, class... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    void WaitForAll(); 
    void Shutdown();
    
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;

    std::atomic<size_t> active_tasks_{0};
    std::condition_variable wait_condition_;
};

// Submit implementation must be in header (template)
template <class F, class... Args>
auto ThreadPool::Submit(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task_ptr->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) throw std::runtime_error("Submit on stopped ThreadPool");
        tasks_.emplace([task_ptr]() { (*task_ptr)(); });
    }
    condition_.notify_one();
    return res;
}

#endif // THREAD_POOL_HH
