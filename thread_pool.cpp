#include "thread_pool.h"
#include <functional>
#include <mutex>



void ThreadPool::ThreadPool(int num_threads){
    workers.reserve(num_threads);
    for(size_t i = 0; i < num_threads; i++){
        workers.emplace_back([this] {worker();});
    }
}

void ThreadPool::enqueue(std::function<void()> task){
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(std:move(task));
        active_tasks++;
    }
    cv.notify_one();
}

