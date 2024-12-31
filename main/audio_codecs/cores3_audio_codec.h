#ifndef _BOX_AUDIO_CODEC_H
#define _BOX_AUDIO_CODEC_H

#include "audio_codec.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

class CoreS3AudioCodec : public AudioCodec {  // 核心S3音频编解码器
private:
    const audio_codec_data_if_t* data_if_ = nullptr;  // 数据接口
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;  // 输出控制接口
    const audio_codec_if_t* out_codec_if_ = nullptr;  // 输出编解码器接口
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;  // 输入控制接口
    const audio_codec_if_t* in_codec_if_ = nullptr;  // 输入编解码器接口
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;  // GPIO接口

    esp_codec_dev_handle_t output_dev_ = nullptr;  // 输出设备句柄
    esp_codec_dev_handle_t input_dev_ = nullptr;  // 输入设备句柄

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);  // 创建全双工通道

    virtual int Read(int16_t* dest, int samples) override;  // 读取数据
    virtual int Write(const int16_t* data, int samples) override;  // 写入数据

public:
    CoreS3AudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,  // 构造函数
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,   // 参数
        uint8_t aw88298_addr, uint8_t es7210_addr, bool input_reference);  // 参数
    virtual ~CoreS3AudioCodec();  // 析构函数

    virtual void SetOutputVolume(int volume) override;  // 设置输出音量
    virtual void EnableInput(bool enable) override;  // 启用输入    
    virtual void EnableOutput(bool enable) override;  // 启用输出
};

#endif // _BOX_AUDIO_CODEC_H
