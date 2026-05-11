#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    void enqueue(std::function<void()> task);
    void wait();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void worker();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> task_queue;

    std::mutex queue_mutex;
    std::condition_variable cv;
    std::condition_variable cv_done;

    std::atomic<size_t> active_tasks{0};
    std::atomic<bool> stop{false};
};
