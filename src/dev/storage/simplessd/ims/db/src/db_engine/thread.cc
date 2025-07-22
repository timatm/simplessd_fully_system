#include "thread.hh"

ThreadPool::ThreadPool(size_t thread_count) : stop_(false) {
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);
                    this->condition_.wait(lock, [this] {
                        return this->stop_ || !this->tasks_.empty();
                    });

                    if (this->stop_ && this->tasks_.empty()) return;
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }

                task();
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

ThreadPool::~ThreadPool() {
    Shutdown();
}
