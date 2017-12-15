#include <afina/Executor.h>
#include <iostream>
#include <pthread.h>
#include <stdexcept>

namespace Afina {

void perform(Executor *executor) {
    while (true) {
        bool is_task = false;
        std::function<void()> task;
        std::unique_lock<std::mutex> queue_lock(executor->queue_mutex);
        if (!executor->tasks.empty()) {
            task = executor->tasks.front();
            executor->tasks.pop_front();
            is_task = true;
        } else {
            std::unique_lock<std::mutex> state_lock(executor->state_mutex);
            switch (executor->state) {
            case Executor::State::kRun: {
                if (executor->tasks.empty()) {
                    executor->empty_condition.wait(state_lock);
                }
            } break;
            case Executor::State::kStopping: {
                if (executor->tasks.empty()) {
                    executor->state = Executor::State::kStopped;
                    return;
                }
            } break;
            case Executor::State::kStopped: {
                return;
            } break;
            default: { throw std::runtime_error("Executor::State error"); }
            }
        }
        queue_lock.unlock();
        if (is_task) {
            task();
        }
    }
}

Executor::Executor(std::string name, int size) {
    state = State::kRun;
    threads.reserve(size);
    for (int i = 0; i < size; ++i) {
        threads.emplace_back(std::thread(perform, this));
    }
}

Executor::~Executor() {
    std::unique_lock<std::mutex> state_lock(state_mutex);
    state = State::kStopped;
    empty_condition.notify_all();
    state_lock.unlock();
    for (auto &thread : threads) {
        thread.join();
    }
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> state_lock(state_mutex);
    if (tasks.empty()) {
        state = State::kStopped;
        empty_condition.notify_all();
    } else {
        state = State::kStopping;
        empty_condition.notify_all();
    }
    state_lock.unlock();
    if (await) {
        auto iter = threads.begin();
        while (iter != threads.end()) {
            iter->join();
            threads.erase(iter);
        }
    }
}

} // namespace Afina