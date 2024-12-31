#ifndef BACKGROUND_TASK_H
#define BACKGROUND_TASK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <list>
#include <condition_variable>
#include <atomic>

class BackgroundTask {
public:
    BackgroundTask(uint32_t stack_size = 4096 * 2);  // 构造函数
    ~BackgroundTask();  // 析构函数

    void Schedule(std::function<void()> callback);  // 调度任务
    void WaitForCompletion();  // 等待任务完成  

private:
    std::mutex mutex_;  // 互斥锁
    std::list<std::function<void()>> main_tasks_;  // 主任务列表
    std::condition_variable condition_variable_;  // 条件变量
    TaskHandle_t background_task_handle_ = nullptr;  // 背景任务句柄
    std::atomic<size_t> active_tasks_{0};  // 活动任务数

    TaskHandle_t task_ = nullptr;  // 任务句柄  
    StaticTask_t task_buffer_;  // 任务缓冲区
    StackType_t* task_stack_ = nullptr;  // 任务栈

    void BackgroundTaskLoop();  // 背景任务循环
};

#endif
