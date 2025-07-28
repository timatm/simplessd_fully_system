#include "thread.hh"

ThreadPool::ThreadPool(size_t thread_count) : stop_(false) {
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this]() {
                        return stop_ || !tasks_.empty();
                    });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                    ++active_tasks_;
                }

                task();

                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    --active_tasks_;
                    if (tasks_.empty() && active_tasks_ == 0) {
                        wait_condition_.notify_all();  // 通知 WaitForAll 可以結束了
                    }
                }
            }
        });
    }

}

void ThreadPool::Shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& thread : workers_) {
        if (thread.joinable()) thread.join();
    }
}
void ThreadPool::WaitForAll() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    wait_condition_.wait(lock, [this]() {
        return tasks_.empty() && active_tasks_ == 0;
    });
}

ThreadPool::~ThreadPool() {
    Shutdown();
}
