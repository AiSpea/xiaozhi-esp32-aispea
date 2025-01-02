#include "settings.h"

#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "Settings"

Settings::Settings(const std::string& ns, bool read_write) : ns_(ns), read_write_(read_write) {  // 构造函数
    nvs_open(ns.c_str(), read_write_ ? NVS_READWRITE : NVS_READONLY, &nvs_handle_);  // 打开句柄
}

Settings::~Settings() {  // 析构函数    
    if (nvs_handle_ != 0) {  // 如果句柄不为0
        if (read_write_ && dirty_) {  // 如果可写且脏
            ESP_ERROR_CHECK(nvs_commit(nvs_handle_));  // 提交更改
        }
        nvs_close(nvs_handle_);  // 关闭句柄
    }
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {  // 获取字符串
    if (nvs_handle_ == 0) {  // 如果句柄为0
        return default_value;  // 返回默认值
    }

    size_t length = 0;
    if (nvs_get_str(nvs_handle_, key.c_str(), nullptr, &length) != ESP_OK) {  // 获取字符串长度
        return default_value;  // 返回默认值
    }

    std::string value;
    value.resize(length);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle_, key.c_str(), value.data(), &length));  // 获取字符串
    return value;  // 返回字符串
}

void Settings::SetString(const std::string& key, const std::string& value) {  // 设置字符串
    if (read_write_) {  // 如果可写
        ESP_ERROR_CHECK(nvs_set_str(nvs_handle_, key.c_str(), value.c_str()));  // 设置字符串
        dirty_ = true;  // 设置脏
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());  // 如果不可写
    }
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    int32_t value;
    if (nvs_get_i32(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

void Settings::SetInt(const std::string& key, int32_t value) {  // 设置整数
    if (read_write_) {
        ESP_ERROR_CHECK(nvs_set_i32(nvs_handle_, key.c_str(), value));
        dirty_ = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
    }
}

void Settings::EraseKey(const std::string& key) {  // 擦除指定键
    if (read_write_) {
        ESP_ERROR_CHECK(nvs_erase_key(nvs_handle_, key.c_str()));
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
    }
}

void Settings::EraseAll() {  // 擦除所有设置
    if (read_write_) {  // 如果可写
        ESP_ERROR_CHECK(nvs_erase_all(nvs_handle_));  // 擦除所有设置
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());  // 如果不可写
    }
}
