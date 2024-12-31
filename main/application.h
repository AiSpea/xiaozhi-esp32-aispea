#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <string>
#include <mutex>
#include <list>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>

#include "protocol.h"
#include "ota.h"
#include "background_task.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "wake_word_detect.h"
#include "audio_processor.h"
#endif

#define SCHEDULE_EVENT (1 << 0)  // 调度事件
#define AUDIO_INPUT_READY_EVENT (1 << 1)  // 音频输入事件
#define AUDIO_OUTPUT_READY_EVENT (1 << 2)  // 音频输出事件

enum ChatState {
    kChatStateUnknown,  // 未知状态
    kChatStateIdle,  // 空闲状态
    kChatStateConnecting,  // 连接中
    kChatStateListening,  // 监听中
    kChatStateSpeaking,  // 说话中
    kChatStateUpgrading  // 升级中
};

#define OPUS_FRAME_DURATION_MS 60  // 每个Opus帧的持续时间（毫秒）

class Application {
public:
    static Application& GetInstance() {
        static Application instance;  // 单例实例   
        return instance;  // 返回单例实例
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;  // 删除拷贝构造函数
    Application& operator=(const Application&) = delete;  // 删除赋值运算符

    void Start();  // 启动应用程序
    ChatState GetChatState() const { return chat_state_; }  // 获取聊天状态
    void Schedule(std::function<void()> callback);  // 调度任务
    void SetChatState(ChatState state);  // 设置聊天状态
    void Alert(const std::string& title, const std::string& message);  // 显示错误消息
    void AbortSpeaking(AbortReason reason);  // 中止说话
    void ToggleChatState();  // 切换聊天状态
    void StartListening();  // 开始监听
    void StopListening();  // 停止监听
    void UpdateIotStates();  // 更新物联网状态

private:
    Application();
    ~Application();

#if CONFIG_IDF_TARGET_ESP32S3
    WakeWordDetect wake_word_detect_;
    AudioProcessor audio_processor_;
#endif
    Ota ota_;
    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_;
    volatile ChatState chat_state_ = kChatStateUnknown;  // 聊天状态
    bool keep_listening_ = false;  // 保持监听
    bool aborted_ = false;  // 是否中止
    std::string last_iot_states_;  // 最后物联网状态

    // Audio encode / decode
    BackgroundTask background_task_;  // 背景任务
    std::chrono::steady_clock::time_point last_output_time_;  // 最后输出时间
    std::list<std::vector<uint8_t>> audio_decode_queue_;  // 音频解码队列

    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    int opus_decode_sample_rate_ = -1;  // 音频解码采样率
    OpusResampler input_resampler_;  // 输入重采样器
    OpusResampler reference_resampler_;  // 参考重采样器
    OpusResampler output_resampler_;  // 输出重采样器

    void MainLoop();
    void InputAudio();
    void OutputAudio();
    void ResetDecoder();
    void SetDecodeSampleRate(int sample_rate);
    void CheckNewVersion();

    void PlayLocalFile(const char* data, size_t size);
};

#endif // _APPLICATION_H_
