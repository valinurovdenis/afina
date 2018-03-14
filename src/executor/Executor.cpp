#include "../../include/afina/Executor.h"
#include <iostream>
#include <functional>

namespace Afina {
void perform(Executor *executor) {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    std::function<void()> task;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(executor->tasks_mutex);
            if (!executor->empty_condition.wait_for(lock, std::chrono::seconds(executor->idle_time),
                                                    [executor] { return !(executor->tasks.empty() && executor->state == Executor::State::kRun); })) {
                std::unique_lock<std::mutex> working_lock(executor->working_mutex);
                if (executor->working_thread_cnt > executor->low_watermark){
                    executor->working_thread_cnt--;
                    for (size_t i = 0; i < executor->threads.size(); i++)
                        if (executor->threads[i] == std::this_thread::get_id()) {
                            executor->threads.erase(executor->threads.begin() + i);
                            break;
                        }

                    return;
                } else
                    continue;
            }
            if (executor->state == Executor::State::kStopped ||
                (executor->state == Executor::State::kStopping && executor->tasks.empty())) {
                executor->empty_condition.notify_all();
                return;
            }
            {
                std::unique_lock<std::mutex> lock(executor->working_mutex);
                executor->working_thread_cnt++;
            }

            task = executor->tasks.front();
            executor->tasks.pop_front();
        }

        task();

        {
            std::unique_lock<std::mutex> lock(executor->working_mutex);
            executor->working_thread_cnt--;
        }
    }
}

void Executor::add_thread(){
    std::cout << "pool: " << " " << __PRETTY_FUNCTION__ << std::endl;
    std::unique_lock<std::mutex> lock(working_mutex);
    std::thread t(perform, this);
    auto id = t.get_id();
    t.detach();
    threads.emplace_back(id);
}

Executor::Executor(std::string name, size_t _low_watermark, size_t _hight_watermark, size_t _max_queue_size, size_t _idle_time)
    :low_watermark(_low_watermark),  hight_watermark(_hight_watermark), max_queue_size(_max_queue_size), idle_time(_idle_time)
{
    std::cout << "pool: " << name << " " << __PRETTY_FUNCTION__ << std::endl;
    state = State ::kRun;

    for (int i = 0; i < low_watermark; i++) {
        add_thread();
    }
}

void Executor::Stop(bool await) {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    {
        std::unique_lock<std::mutex> lock(tasks_mutex);
        if (state == State::kRun){
            state = State::kStopping;
        }
    }

    empty_condition.notify_all();
    if (await) {
        std::unique_lock<std::mutex> lock(tasks_mutex);
        empty_condition.wait(lock, [this] { return !(tasks.empty() && state == Executor::State::kRun); });

        state = State::kStopped;
    }
}

Executor::~Executor() {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    Stop(true);
}

} // namespace Afina
