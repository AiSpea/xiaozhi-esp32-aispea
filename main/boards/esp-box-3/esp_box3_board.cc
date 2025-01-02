#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/no_display.h"
#include "application.h"
#include "button.h"
#include "led.h"
#include "config.h"
#include "iot/thing_manager.h"

#include <esp_log.h>
#include <driver/i2c_master.h>

#define TAG "EspBox3Board"

class EspBox3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {  // I2C总线配置
            .i2c_port = (i2c_port_t)1,  // I2C端口
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,  // SDA引脚
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,  // SCL引脚
            .clk_source = I2C_CLK_SRC_DEFAULT,  // 时钟源
            .glitch_ignore_cnt = 7,  // 抖动忽略计数
            .intr_priority = 0,  // 中断优先级
            .trans_queue_depth = 0,  // 传输队列深度
            .flags = {
                .enable_internal_pullup = 1,  // 启用内部上拉
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));  // 创建I2C总线
    }

    void InitializeButtons() {
        // boot_button_.OnClick([this]() {  // 设置按钮点击事件
        //     Application::GetInstance().ToggleChatState();  // 切换聊天状态
        // });
        // boot_button_.OnPressUp([this](){
        //     Application::GetInstance().ToggleChatState();  // 切换聊天状态 
        // });
        boot_button_.OnLangStart([this](){
            ESP_LOGW(TAG, "按键长按开始");  // 打印活动任务数
        });
        boot_button_.OnPressDown([this](){
            Application::GetInstance().ToggleChatState();  // 切换聊天状态 
            ESP_LOGW(TAG, "按键按下");  // 打印活动任务数
        }); 
        boot_button_.OnPressUp([this](){
            ESP_LOGW(TAG, "按键松手");  // 打印活动任务数
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
    }

public:
    EspBox3Board() : boot_button_(BOOT_BUTTON_GPIO) {  // 初始化按钮
        InitializeI2c();  // 初始化I2C
        InitializeButtons();  // 初始化按钮
        InitializeIot();  // 初始化物联网
    }

    virtual Led* GetBuiltinLed() override {     
        static Led led(BUILTIN_LED_GPIO);  // 获取内置LED
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec* audio_codec = nullptr;
        if (audio_codec == nullptr) {
            audio_codec = new BoxAudioCodec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8156_ADDR, AUDIO_CODEC_ES7243E_ADDR, AUDIO_INPUT_REFERENCE);
            audio_codec->SetOutputVolume(AUDIO_DEFAULT_OUTPUT_VOLUME);
        }
        return audio_codec;
    }

    virtual Display* GetDisplay() override {
        static NoDisplay display;
        return &display;
    }
};

DECLARE_BOARD(EspBox3Board);
