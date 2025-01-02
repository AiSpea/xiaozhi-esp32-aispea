#include "led.h"
#include "board.h"

#include <cstring>
#include <esp_log.h>

#define TAG "Led"

Led::Led(gpio_num_t gpio) {
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGI(TAG, "Builtin LED not connected");
        return;
    }
    
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = 1;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    // 使用RMT(Remote Control)驱动创建新的LED灯带设备
    // 如果初始化失败会触发错误检查宏
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    
    // 清除LED灯带的当前显示状态（关闭所有LED）
    led_strip_clear(led_strip_);

    SetGrey();  // 设置LED灯带为灰色

    esp_timer_create_args_t blink_timer_args = {
        .callback = [](void *arg) {
            auto led = static_cast<Led*>(arg);
            led->OnBlinkTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "Blink Timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&blink_timer_args, &blink_timer_));    // 创建一个周期性定时器，用于控制LED的闪烁
}

Led::~Led() {
    esp_timer_stop(blink_timer_);
    if (led_strip_ != nullptr) {
        led_strip_del(led_strip_);
    }
}

void Led::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
}

void Led::TurnOn() {
    if (led_strip_ == nullptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
    led_strip_refresh(led_strip_);
}

void Led::TurnOff() {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    led_strip_clear(led_strip_);
}

void Led::BlinkOnce() {
    Blink(1, 100);
}

void Led::Blink(int times, int interval_ms) {
    StartBlinkTask(times, interval_ms);
}

void Led::StartContinuousBlink(int interval_ms) {
    StartBlinkTask(BLINK_INFINITE, interval_ms);
}

void Led::StartBlinkTask(int times, int interval_ms) {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);   // 锁定互斥锁，防止多个线程同时修改LED状态
    esp_timer_stop(blink_timer_);   // 停止当前的闪烁定时器
    
    led_strip_clear(led_strip_);   // 清除LED灯带的当前显示状态（关闭所有LED）  
    blink_counter_ = times * 2;   // 设置闪烁次数
    blink_interval_ms_ = interval_ms;   // 设置闪烁间隔时间
    esp_timer_start_periodic(blink_timer_, interval_ms * 1000);   // 启动周期性定时器，用于控制LED的闪烁
}

void Led::OnBlinkTimer() {   // 定时器回调函数，用于控制LED的闪烁
    std::lock_guard<std::mutex> lock(mutex_);   // 锁定互斥锁，防止多个线程同时修改LED状态
    blink_counter_--;   // 减少闪烁计数器
    if (blink_counter_ & 1) {   // 如果闪烁计数器为奇数，则点亮LED
        led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
        led_strip_refresh(led_strip_);   // 刷新LED灯带，使新的颜色显示
    } else {
        led_strip_clear(led_strip_);   // 如果闪烁计数器为偶数，则熄灭LED   

        if (blink_counter_ == 0) {
            esp_timer_stop(blink_timer_);
        }
    }
}
