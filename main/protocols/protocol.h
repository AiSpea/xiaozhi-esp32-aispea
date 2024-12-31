#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cJSON.h>
#include <string>
#include <functional>

struct BinaryProtocol3 {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t payload[];
} __attribute__((packed));

enum AbortReason {
    kAbortReasonNone,
    kAbortReasonWakeWordDetected
};

enum ListeningMode {
    kListeningModeAutoStop,
    kListeningModeManualStop,
    kListeningModeAlwaysOn // 需要 AEC 支持
};

class Protocol {
public:
    virtual ~Protocol() = default;

    inline int server_sample_rate() const {
        return server_sample_rate_;  // 返回服务器采样率
    }

    void OnIncomingAudio(std::function<void(std::vector<uint8_t>&& data)> callback);  // 接收到音频数据
    void OnIncomingJson(std::function<void(const cJSON* root)> callback);  // 接收到JSON数据
    void OnAudioChannelOpened(std::function<void()> callback);  // 音频通道打开
    void OnAudioChannelClosed(std::function<void()> callback);  // 音频通道关闭
    void OnNetworkError(std::function<void(const std::string& message)> callback);  // 网络错误

    virtual bool OpenAudioChannel() = 0;  // 打开音频通道
    virtual void CloseAudioChannel() = 0;  // 关闭音频通道
    virtual bool IsAudioChannelOpened() const = 0;  // 是否打开音频通道
    virtual void SendAudio(const std::vector<uint8_t>& data) = 0;  // 发送音频  
    virtual void SendWakeWordDetected(const std::string& wake_word);  // 发送唤醒词检测
    virtual void SendStartListening(ListeningMode mode);  // 发送开始监听
    virtual void SendStopListening();  // 发送停止监听
    virtual void SendAbortSpeaking(AbortReason reason);  // 发送中止说话
    virtual void SendIotDescriptors(const std::string& descriptors);  // 发送物联网设备描述符
    virtual void SendIotStates(const std::string& states);  // 发送物联网状态   

protected:
    std::function<void(const cJSON* root)> on_incoming_json_;  // 接收到JSON数据
    std::function<void(std::vector<uint8_t>&& data)> on_incoming_audio_;  // 接收到音频数据
    std::function<void()> on_audio_channel_opened_;  // 音频通道打开    
    std::function<void()> on_audio_channel_closed_;  // 音频通道关闭
    std::function<void(const std::string& message)> on_network_error_;  // 网络错误

    int server_sample_rate_ = 16000;
    std::string session_id_;

    virtual void SendText(const std::string& text) = 0;
};

#endif // PROTOCOL_H

