#ifndef BUTTON_H_
#define BUTTON_H_

#include <driver/gpio.h>
#include <iot_button.h>
#include <functional>

class Button {
public:
    Button(gpio_num_t gpio_num, bool active_high = false);  // 按钮构造函数
    ~Button();  // 按钮析构函数

    void OnPressDown(std::function<void()> callback);  // 按下事件
    void OnPressUp(std::function<void()> callback);  // 抬起事件
    void OnLongPress(std::function<void()> callback);  // 长按事件
    void OnClick(std::function<void()> callback);  // 单击事件
    void OnDoubleClick(std::function<void()> callback);  // 双击事件
    void OnLangStart(std::function<void()> callback);  //长按开始事件
private:
    gpio_num_t gpio_num_;
    button_handle_t button_handle_;


    std::function<void()> on_press_down_;  // 按下事件
    std::function<void()> on_press_up_;  // 抬起事件
    std::function<void()> on_long_press_;  // 长按事件
    std::function<void()> on_click_;  // 单击事件
    std::function<void()> on_double_click_;  // 双击事件
    std::function<void()> on_lang_start_;  //长按开始事件
};

#endif // BUTTON_H_
