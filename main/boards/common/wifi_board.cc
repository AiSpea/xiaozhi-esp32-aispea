#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http.h>
#include <esp_mqtt.h>
#include <esp_udp.h>
#include <tcp_transport.h>
#include <tls_transport.h>
#include <web_socket.h>
#include <esp_log.h>

#include <wifi_station.h>
#include <wifi_configuration_ap.h>

static const char *TAG = "WifiBoard";

static std::string rssi_to_string(int rssi) {
    if (rssi >= -55) {
        return "Very good";
    } else if (rssi >= -65) {
        return "Good";
    } else if (rssi >= -75) {
        return "Fair";
    } else if (rssi >= -85) {
        return "Poor";
    } else {
        return "No network";
    }
}

void WifiBoard::StartNetwork() {   // 启动网络
    auto& application = Application::GetInstance();  // 获取应用程序实例
    auto display = Board::GetInstance().GetDisplay();  // 获取显示
    auto builtin_led = Board::GetInstance().GetBuiltinLed();  // 获取内置LED

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    auto& wifi_station = WifiStation::GetInstance();  // 获取WiFi连接实例
    display->SetStatus(std::string("正在连接 ") + wifi_station.GetSsid());  // 设置显示状态
    wifi_station.Start();   // 启动WiFi连接 
    if (!wifi_station.IsConnected()) {
        builtin_led->SetBlue();
        builtin_led->Blink(1000, 500);
        auto& wifi_ap = WifiConfigurationAp::GetInstance();  // 获取WiFi配置AP实例
        wifi_ap.SetSsidPrefix("Xiaozhi");  // 设置WiFi配置AP的SSID前缀
        wifi_ap.Start();  // 启动WiFi配置AP
        
        // 播报配置 WiFi 的提示
        application.Alert("Info", "Configuring WiFi");

        // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
        std::string hint = "请在手机上连接热点 ";  // 设置提示信息
        hint += wifi_ap.GetSsid();  // 添加WiFi配置AP的SSID
        hint += "，然后打开浏览器访问 ";  // 添加提示信息
        hint += wifi_ap.GetWebServerUrl();  // 添加提示信息

        display->SetStatus(hint);  // 设置显示状态
        
        // Wait forever until reset after configuration
        while (true) {
            int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
            ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);  // 打印内部内存使用情况
            vTaskDelay(pdMS_TO_TICKS(10000));  // 延迟10秒
        }
    }
}

Http* WifiBoard::CreateHttp() {
    return new EspHttp();  // 创建HTTP
}

WebSocket* WifiBoard::CreateWebSocket() {
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET  // 如果定义了WEBSOCKET连接类型
    std::string url = CONFIG_WEBSOCKET_URL;  // 获取WEBSOCKET URL
    if (url.find("wss://") == 0) {  // 如果URL以wss://开头
        return new WebSocket(new TlsTransport());  // 创建TLS传输
    } else {
        return new WebSocket(new TcpTransport());
    }
#endif
    return nullptr;
}

Mqtt* WifiBoard::CreateMqtt() {
    return new EspMqtt();  // 创建MQTT
}

Udp* WifiBoard::CreateUdp() {
    return new EspUdp();
}

bool WifiBoard::GetNetworkState(std::string& network_name, int& signal_quality, std::string& signal_quality_text) {  // 获取网络状态    
    if (wifi_config_mode_) {  // 如果WiFi配置模式为true
        auto& wifi_ap = WifiConfigurationAp::GetInstance();  // 获取WiFi配置AP实例
        network_name = wifi_ap.GetSsid();  // 获取WiFi配置AP的SSID
        signal_quality = -99;  // 设置信号质量
        signal_quality_text = wifi_ap.GetWebServerUrl();  // 获取WiFi配置AP的Web服务器URL
        return true;  // 返回true
    }
    auto& wifi_station = WifiStation::GetInstance();  // 获取WiFi连接实例
    if (!wifi_station.IsConnected()) {  // 如果WiFi连接未连接
        return false;  // 返回false
    }
    network_name = wifi_station.GetSsid();  // 获取WiFi连接的SSID
    signal_quality = wifi_station.GetRssi();  // 获取WiFi连接的RSSI
    signal_quality_text = rssi_to_string(signal_quality);  // 获取WiFi连接的信号质量文本
    return signal_quality != -1;  // 返回信号质量是否有效
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {  // 如果WiFi配置模式为true
        return FONT_AWESOME_WIFI;  // 返回WiFi图标
    }
    auto& wifi_station = WifiStation::GetInstance();  // 获取WiFi连接实例   
    if (!wifi_station.IsConnected()) {  // 如果WiFi连接未连接
        return FONT_AWESOME_WIFI_OFF;  // 返回WiFi断开图标
    }
    int8_t rssi = wifi_station.GetRssi();  // 获取WiFi连接的RSSI    
    if (rssi >= -55) {  
        return FONT_AWESOME_WIFI;  // 返回WiFi良好图标  
    } else if (rssi >= -65) {
        return FONT_AWESOME_WIFI_FAIR;  // 返回WiFi一般图标
    } else {
        return FONT_AWESOME_WIFI_WEAK;  // 返回WiFi弱图标
    }
}

std::string WifiBoard::GetBoardJson() {  // 获取板子JSON
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();  // 获取WiFi连接实例
    std::string board_type = BOARD_TYPE;  // 获取板子类型
    std::string board_json = std::string("{\"type\":\"" + board_type + "\",");  // 创建板子JSON
    if (!wifi_config_mode_) {  // 如果WiFi配置模式为false
        board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";  // 添加WiFi连接的SSID
        board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";  // 添加WiFi连接的RSSI
        board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";  // 添加WiFi连接的频道
        board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";  // 添加WiFi连接的MAC地址
    return board_json;  // 返回板子JSON
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();  // 获取WiFi连接实例
    wifi_station.SetPowerSaveMode(enabled);  // 设置WiFi连接的电源管理模式
}

void WifiBoard::ResetWifiConfiguration() {  // 重置WiFi配置
    // Reset the wifi station
    {
        Settings settings("wifi", true);  // 创建设置   
        settings.EraseAll();  // 擦除所有设置
    }
    GetDisplay()->ShowNotification("已重置 WiFi...");  // 显示通知
    vTaskDelay(pdMS_TO_TICKS(1000));  // 延迟1秒
    // Reboot the device
    esp_restart();  // 重启设备
}
