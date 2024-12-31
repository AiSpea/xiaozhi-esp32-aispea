#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <mqtt.h>
#include <udp.h>
#include <string>

#include "led.h"

void* create_board();
class AudioCodec;
class Display;
class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作
    virtual std::string GetBoardJson() = 0;

protected:
    Board();

public:
    static Board& GetInstance() {
        static Board* instance = nullptr;   // 静态实例
        if (nullptr == instance) {  // 如果实例为空
            instance = static_cast<Board*>(create_board());  // 创建实例
        }
        return *instance;  // 返回实例
    }

    virtual void StartNetwork() = 0;  // 启动网络
    virtual ~Board() = default;  // 默认析构函数
    virtual Led* GetBuiltinLed() = 0;  // 获取内置LED
    virtual AudioCodec* GetAudioCodec() = 0;  // 获取音频编解码器
    virtual Display* GetDisplay();  // 获取显示
    virtual Http* CreateHttp() = 0;  // 创建HTTP
    virtual WebSocket* CreateWebSocket() = 0;  // 创建WebSocket
    virtual Mqtt* CreateMqtt() = 0;  // 创建MQTT
    virtual Udp* CreateUdp() = 0;  // 创建UDP
    virtual bool GetNetworkState(std::string& network_name, int& signal_quality, std::string& signal_quality_text) = 0;  // 获取网络状态
    virtual const char* GetNetworkStateIcon() = 0;  // 获取网络状态图标
    virtual bool GetBatteryLevel(int &level, bool& charging);  // 获取电池电量
    virtual std::string GetJson();  // 获取JSON
    virtual void SetPowerSaveMode(bool enabled) = 0;
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H
