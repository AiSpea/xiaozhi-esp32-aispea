#include "button.h"

#include <esp_log.h>

static const char* TAG = "Button";  // 标签

Button::Button(gpio_num_t gpio_num, bool active_high) : gpio_num_(gpio_num) {  // 按钮构造函数
    if (gpio_num == GPIO_NUM_NC) {  // 如果GPIO引脚为空
        return;  // 返回
    }
    button_config_t button_config = {  // 按钮配置
        .type = BUTTON_TYPE_GPIO,  // 按钮类型为GPIO
        .long_press_time = 1000,  // 长按时间
        .short_press_time = 50,  // 短按时间
        .gpio_button_config = {  // GPIO按钮配置
            .gpio_num = gpio_num,  // GPIO引脚  
            .active_level = static_cast<uint8_t>(active_high ? 1 : 0)  // 激活级别
        }
    };
    button_handle_ = iot_button_create(&button_config);  // 创建按钮句柄
    if (button_handle_ == NULL) {  // 如果按钮句柄为空
        ESP_LOGE(TAG, "Failed to create button handle");  // 打印错误日志
        return;  // 返回
    }
}

Button::~Button() {
    if (button_handle_ != NULL) {  // 如果按钮句柄不为空
        iot_button_delete(button_handle_);  // 删除按钮
    }
}

void Button::OnPressDown(std::function<void()> callback) {
    if (button_handle_ == nullptr) {  // 如果按钮句柄为空
        return;  // 返回
    }
    on_press_down_ = callback;  // 设置按下事件
    iot_button_register_cb(button_handle_, BUTTON_PRESS_DOWN, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_press_down_) {  // 如果按下事件不为空
            button->on_press_down_();  // 调用按下事件
        }
    }, this);
}

void Button::OnPressUp(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_press_up_ = callback;  // 设置抬起事件
    iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, [](void* handle, void* usr_data) {  // 注册抬起事件
        Button* button = static_cast<Button*>(usr_data);  // 获取按钮实例
        if (button->on_press_up_) {  // 如果抬起事件不为空
            button->on_press_up_();  // 调用抬起事件
        }
    }, this);  // 注册抬起事件
}

void Button::OnLongPress(std::function<void()> callback) {  // 长按事件
    if (button_handle_ == nullptr) {  // 如果按钮句柄为空
        return;  // 返回
    }
    on_long_press_ = callback;  // 设置长按事件
    iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, [](void* handle, void* usr_data) {  // 注册长按事件
        Button* button = static_cast<Button*>(usr_data);  // 获取按钮实例
        if (button->on_long_press_) {  // 如果长按事件不为空
            button->on_long_press_();  // 调用长按事件
        }
    }, this);  // 注册长按事件
}

void Button::OnClick(std::function<void()> callback) {  // 单击事件
    if (button_handle_ == nullptr) {
        return;
    }
    on_click_ = callback;  // 设置单击事件
    iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, [](void* handle, void* usr_data) {  // 注册单击事件
        Button* button = static_cast<Button*>(usr_data);  // 获取按钮实例
        if (button->on_click_) {
            button->on_click_();  // 调用单击事件
        }
    }, this);
}

void Button::OnDoubleClick(std::function<void()> callback) {  // 双击事件
    if (button_handle_ == nullptr) {  // 如果按钮句柄为空
        return;  // 返回
    }
    on_double_click_ = callback;  // 设置双击事件
    iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, [](void* handle, void* usr_data) {  // 注册双击事件
        Button* button = static_cast<Button*>(usr_data);  // 获取按钮实例
        if (button->on_double_click_) {  // 如果双击事件不为空
            button->on_double_click_();  // 调用双击事件
        }
    }, this);  // 注册双击事件
}

void Button::OnLangStart(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_lang_start_ = callback; 
    iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_lang_start_) {
            button->on_lang_start_();
        }
    }, this);
}
