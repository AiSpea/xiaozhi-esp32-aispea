#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <nvs_flash.h>

class Settings {  // 设置
public:
    Settings(const std::string& ns, bool read_write = false);  // 构造函数
    ~Settings();  // 析构函数

    std::string GetString(const std::string& key, const std::string& default_value = "");  // 获取字符串
    void SetString(const std::string& key, const std::string& value);  // 设置字符串
    int32_t GetInt(const std::string& key, int32_t default_value = 0);  // 获取整数
    void SetInt(const std::string& key, int32_t value);  // 设置整数
    void EraseKey(const std::string& key);  // 删除键
    void EraseAll();  // 删除所有键

private:
    std::string ns_;  // 命名空间   
    nvs_handle_t nvs_handle_ = 0;  // NVS句柄
    bool read_write_ = false;  // 是否可写
    bool dirty_ = false;  // 是否脏
};

#endif
