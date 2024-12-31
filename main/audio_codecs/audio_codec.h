#ifndef _AUDIO_CODEC_H
#define _AUDIO_CODEC_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <driver/i2s_std.h>

#include <vector>
#include <string>
#include <functional>

#include "board.h"

class AudioCodec {  // 音频编解码器
public:
    AudioCodec();  // 构造函数
    virtual ~AudioCodec();  // 析构函数
    
    virtual void SetOutputVolume(int volume);  // 设置输出音量
    virtual void EnableInput(bool enable);  // 启用输入
    virtual void EnableOutput(bool enable);  // 启用输出

    void Start();  // 开始
    void OutputData(std::vector<int16_t>& data);  // 输出数据
    bool InputData(std::vector<int16_t>& data);  // 输入数据
    void OnOutputReady(std::function<bool()> callback);  // 输出准备回调
    void OnInputReady(std::function<bool()> callback);  // 输入准备回调

    inline bool duplex() const { return duplex_; }  // 是否为全双工
    inline bool input_reference() const { return input_reference_; }  // 是否为输入参考
    inline int input_sample_rate() const { return input_sample_rate_; }  // 输入采样率
    inline int output_sample_rate() const { return output_sample_rate_; }  // 输出采样率
    inline int input_channels() const { return input_channels_; }  // 输入通道数
    inline int output_channels() const { return output_channels_; }  // 输出通道数
    inline int output_volume() const { return output_volume_; }  // 输出音量

private:
    std::function<bool()> on_input_ready_;  // 输入准备回调
    std::function<bool()> on_output_ready_;  // 输出准备回调
    
    IRAM_ATTR static bool on_recv(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);  // 接收回调
    IRAM_ATTR static bool on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);  // 发送回调

protected:
    i2s_chan_handle_t tx_handle_ = nullptr;  // 发送句柄
    i2s_chan_handle_t rx_handle_ = nullptr;  // 接收句柄

    bool duplex_ = false;  // 是否为全双工
    bool input_reference_ = false;  // 是否为输入参考
    bool input_enabled_ = false;  // 是否启用输入
    bool output_enabled_ = false;  // 是否启用输出
    int input_sample_rate_ = 0;  // 输入采样率
    int output_sample_rate_ = 0;  // 输出采样率
    int input_channels_ = 1;  // 输入通道数
    int output_channels_ = 1;  // 输出通道数
    int output_volume_ = 70;  // 输出音量

    virtual int Read(int16_t* dest, int samples) = 0;  // 读取数据
    virtual int Write(const int16_t* data, int samples) = 0;  // 写入数据
};

#endif // _AUDIO_CODEC_H
