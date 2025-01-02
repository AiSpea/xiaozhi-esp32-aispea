#ifndef _LED_H_
#define _LED_H_

#include <led_strip.h>
#include <esp_timer.h>
#include <atomic>
#include <mutex>

#define BLINK_INFINITE -1  // 无限闪烁
#define BLINK_TASK_STOPPED_BIT BIT0  // 闪烁任务停止位
#define BLINK_TASK_RUNNING_BIT BIT1  // 闪烁任务运行位

#define DEFAULT_BRIGHTNESS 16  // 默认亮度
#define HIGH_BRIGHTNESS 255  // 高亮度
#define LOW_BRIGHTNESS 2  // 低亮度

class Led {
public:
    Led(gpio_num_t gpio);
    ~Led();

    void BlinkOnce();  // 闪烁一次
    void Blink(int times, int interval_ms);  // 闪烁
    void StartContinuousBlink(int interval_ms);  // 开始连续闪烁
    void TurnOn();  // 打开
    void TurnOff();  // 关闭
    void SetColor(uint8_t r, uint8_t g, uint8_t b);  // 设置颜色
    void SetWhite(uint8_t brightness = DEFAULT_BRIGHTNESS) { SetColor(brightness, brightness, brightness); }  // 设置白色
    void SetGrey(uint8_t brightness = DEFAULT_BRIGHTNESS) { SetColor(brightness, brightness, brightness); }  // 设置灰色
    void SetRed(uint8_t brightness = DEFAULT_BRIGHTNESS) { SetColor(brightness, 0, 0); }  // 设置红色
    void SetGreen(uint8_t brightness = DEFAULT_BRIGHTNESS) { SetColor(0, brightness, 0); }
    void SetBlue(uint8_t brightness = DEFAULT_BRIGHTNESS) { SetColor(0, 0, brightness); }  // 设置蓝色

private:
    std::mutex mutex_;  // 互斥锁
    TaskHandle_t blink_task_ = nullptr;  // 闪烁任务句柄
    led_strip_handle_t led_strip_ = nullptr;  // LED条带句柄
    uint8_t r_ = 0, g_ = 0, b_ = 0;  // 颜色
    int blink_counter_ = 0;  // 闪烁计数器
    int blink_interval_ms_ = 0;  // 闪烁间隔时间
    esp_timer_handle_t blink_timer_ = nullptr;  // 闪烁定时器句柄

    void StartBlinkTask(int times, int interval_ms);
    void OnBlinkTimer();
};

#endif // _LED_H_
