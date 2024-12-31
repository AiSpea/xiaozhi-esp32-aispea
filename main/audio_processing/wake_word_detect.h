#ifndef WAKE_WORD_DETECT_H
#define WAKE_WORD_DETECT_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_afe_sr_models.h>
#include <esp_nsn_models.h>

#include <list>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>


class WakeWordDetect {
public:
    WakeWordDetect();
    ~WakeWordDetect();

    void Initialize(int channels, bool reference);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void OnVadStateChange(std::function<void(bool speaking)> callback);
    void StartDetection();
    void StopDetection();
    bool IsDetectionRunning();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }

private:
    esp_afe_sr_data_t* afe_detection_data_ = nullptr;  // AFE检测数据
    char* wakenet_model_ = NULL;  // Wakenet模型
    std::vector<std::string> wake_words_;  // 唤醒词列表
    std::vector<int16_t> input_buffer_;  // 输入缓冲区
    EventGroupHandle_t event_group_;  // 事件组
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;  // 唤醒词检测回调函数
    std::function<void(bool speaking)> vad_state_change_callback_;  // VAD状态变化回调函数
    bool is_speaking_ = false;  // 是否正在说话
    int channels_;  // 通道数
    bool reference_;  // 是否为参考通道
    std::string last_detected_wake_word_;  // 最后一次检测到的唤醒词

    TaskHandle_t wake_word_encode_task_ = nullptr;  // 唤醒词编码任务句柄
    StaticTask_t wake_word_encode_task_buffer_;  // 唤醒词编码任务缓冲区
    StackType_t* wake_word_encode_task_stack_ = nullptr;  // 唤醒词编码任务栈
    std::list<std::vector<int16_t>> wake_word_pcm_;  // 唤醒词PCM数据列表
    std::list<std::vector<uint8_t>> wake_word_opus_;  // 唤醒词Opus数据列表
    std::mutex wake_word_mutex_;  // 唤醒词互斥锁
    std::condition_variable wake_word_cv_;  // 唤醒词条件变量

    void StoreWakeWordData(uint16_t* data, size_t size);  // 存储唤醒词数据
    void AudioDetectionTask();  // 音频检测任务 
};

#endif
