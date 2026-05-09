#include "thread_pool.h"
#include <functional>
#include <mutex>



ThreadPool::ThreadPool(int num_threads){
    workers.reserve(num_threads);
    for(size_t i = 0; i < num_threads; i++){
        workers.emplace_back([this] {
            worker();
        });
    }
}

void ThreadPool::enqueue(std::function<void()> task){
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(std::move(task));
        active_tasks++;
    }
    cv.notify_one();
}

void ThreadPool::wait(){
    std::unique_lock<std::mutex> lock(queue_mutex);

    cv_done.wait(lock, [this] {
        return task_queue.empty() && active_tasks == 0;
    });
}

void ThreadPool::worker(){
    while(true){
        std::function<void()> task;
        {
            std::unique_lock lock(queue_mutex);

            cv.wait(lock, [this]{
                return !task_queue.empty() || stop == true
            });

            if (stop == true || task_queue.empty()){
                return;
            }

            task = std::move(task_queue.front());
            task_queue.pop();
        }

        task();
        active_tasks--;
        cv_done.notify_all();
    }
}

ThreadPool::~ThreadPool(){
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
    cv.notify_all();

    for (std::thread &worker : workers){
        if (worker.joinable()){
            worker.join();
        }
    }
}

