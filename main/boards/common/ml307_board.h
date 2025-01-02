#ifndef ML307_BOARD_H
#define ML307_BOARD_H

#include "board.h"
#include <ml307_at_modem.h>

class Ml307Board : public Board {
protected:
    Ml307AtModem modem_;

    virtual std::string GetBoardJson() override;
    void WaitForNetworkReady();

public:
    Ml307Board(gpio_num_t tx_pin, gpio_num_t rx_pin, size_t rx_buffer_size = 4096);
    virtual void StartNetwork() override;  // 启动网络
    virtual Http* CreateHttp() override;  // 创建HTTP
    virtual WebSocket* CreateWebSocket() override;  // 创建WebSocket
    virtual Mqtt* CreateMqtt() override;  // 创建MQTT
    virtual Udp* CreateUdp() override;  // 创建UDP
    virtual bool GetNetworkState(std::string& network_name, int& signal_quality, std::string& signal_quality_text) override;  // 获取网络状态
    virtual const char* GetNetworkStateIcon() override;  // 获取网络状态图标
    virtual void SetPowerSaveMode(bool enabled) override;  // 设置省电模式
};

#endif // ML307_BOARD_H
