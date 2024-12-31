#include "background_task.h"

#include <esp_log.h>

#define TAG "BackgroundTask"

BackgroundTask::BackgroundTask(uint32_t stack_size) {  // 构造函数
#if CONFIG_IDF_TARGET_ESP32S3
    task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
    background_task_handle_ = xTaskCreateStatic([](void* arg) {
        BackgroundTask* task = (BackgroundTask*)arg;
        task->BackgroundTaskLoop();
    }, "background_task", stack_size, this, 1, task_stack_, &task_buffer_);
#else
    xTaskCreate([](void* arg) {     //创建任务
        BackgroundTask* task = (BackgroundTask*)arg;    //将任务指针转换为BackgroundTask指针
        task->BackgroundTaskLoop();  //执行背景任务循环
    }, "background_task", stack_size, this, 1, &background_task_handle_);   //创建任务
#endif
}

BackgroundTask::~BackgroundTask() {  // 析构函数
    if (background_task_handle_ != nullptr) {  // 如果背景任务句柄不为空
        vTaskDelete(background_task_handle_);  // 删除背景任务
    }
}

void BackgroundTask::Schedule(std::function<void()> callback) {  // 调度任务
    std::lock_guard<std::mutex> lock(mutex_);  // 锁定互斥锁
    if (active_tasks_ >= 30) {  // 如果活动任务数大于等于30
        ESP_LOGW(TAG, "active_tasks_ == %u", active_tasks_.load());  // 打印活动任务数
    }
    active_tasks_++;  // 增加活动任务数
    main_tasks_.emplace_back([this, cb = std::move(callback)]() {  // 添加任务到主任务列表
        cb();  // 执行回调函数
        {
            std::lock_guard<std::mutex> lock(mutex_);  // 锁定互斥锁
            active_tasks_--;  // 减少活动任务数
            if (main_tasks_.empty() && active_tasks_ == 0) {  // 如果主任务列表为空且活动任务数为0
                condition_variable_.notify_all();  // 通知所有等待的线程
            }
        }
    });
    condition_variable_.notify_all();  // 通知所有等待的线程
}

void BackgroundTask::WaitForCompletion() {  // 等待任务完成
    std::unique_lock<std::mutex> lock(mutex_);  // 锁定互斥锁
    condition_variable_.wait(lock, [this]() {  // 等待条件变量
        return main_tasks_.empty() && active_tasks_ == 0;  // 如果主任务列表为空且活动任务数为0
    });
}

void BackgroundTask::BackgroundTaskLoop() {  // 背景任务循环
    ESP_LOGI(TAG, "background_task started");  // 打印“背景任务开始”
    while (true) {  // 无限循环
        std::unique_lock<std::mutex> lock(mutex_);  // 锁定互斥锁
        condition_variable_.wait(lock, [this]() { return !main_tasks_.empty(); });  // 等待条件变量
        
        std::list<std::function<void()>> tasks = std::move(main_tasks_);  // 移动主任务列表 
        lock.unlock();  // 解锁互斥锁

        for (auto& task : tasks) {  // 遍历任务列表
            task();  // 执行任务
        }
    }
}
