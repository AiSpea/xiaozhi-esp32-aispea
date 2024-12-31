#include "protocol.h"

#include <esp_log.h>

#define TAG "Protocol"

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(std::function<void(std::vector<uint8_t>&& data)> callback) {  // 接收到音频数据
    on_incoming_audio_ = callback;  // 设置接收到音频数据回调
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {   
    on_audio_channel_opened_ = callback;  // 设置音频通道打开回调
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {  // 音频通道关闭
    on_audio_channel_closed_ = callback;  // 设置音频通道关闭回调
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {  // 网络错误
    on_network_error_ = callback;  // 设置网络错误回调
}   

void Protocol::SendAbortSpeaking(AbortReason reason) {  // 发送中止说话
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wake_word + "\"}";
    SendText(json);
}

void Protocol::SendStartListening(ListeningMode mode) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == kListeningModeAlwaysOn) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendStopListening() {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    SendText(message);
}

void Protocol::SendIotDescriptors(const std::string& descriptors) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"iot\",\"descriptors\":" + descriptors + "}";
    SendText(message);
}

void Protocol::SendIotStates(const std::string& states) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"iot\",\"states\":" + states + "}";
    SendText(message);
}

