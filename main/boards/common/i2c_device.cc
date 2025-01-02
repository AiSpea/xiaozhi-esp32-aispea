#include "i2c_device.h"

#include <esp_log.h>

#define TAG "I2cDevice"


I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr) {   // 构造函数，用于初始化I2C设备
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,   // 设置I2C设备地址长度为7位
        .device_address = addr,   // 设置I2C设备地址
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_));   // 将I2C设备添加到I2C总线
    assert(i2c_device_ != NULL);   // 断言确保i2c_device_不为空
}

void I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};   // 创建一个包含寄存器地址和值的缓冲区
    ESP_ERROR_CHECK(i2c_master_transmit(i2c_device_, buffer, 2, 100));   // 向I2C设备发送数据
}

uint8_t I2cDevice::ReadReg(uint8_t reg) {
    uint8_t buffer[1];   // 创建一个包含寄存器地址的缓冲区
    ESP_ERROR_CHECK(i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, 1, 100));   // 从I2C设备读取数据
    return buffer[0];   // 返回读取到的值
}

void I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {   // 从I2C设备读取多个寄存器的数据
    ESP_ERROR_CHECK(i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100));   // 从I2C设备读取数据 
}