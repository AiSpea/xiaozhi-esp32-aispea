#include "mqtt_protocol.h"
#include "board.h"
#include "application.h"
#include "settings.h"

#include <esp_log.h>
#include <ml307_mqtt.h>
#include <ml307_udp.h>
#include <cstring>
#include <arpa/inet.h>

#define TAG "MQTT"

MqttProtocol::MqttProtocol() {  // MQTT协议构造函数
    event_group_handle_ = xEventGroupCreate();  // 创建事件组

    StartMqttClient();  // 开始MQTT客户端
}

MqttProtocol::~MqttProtocol() {  // MQTT协议析构函数
    ESP_LOGI(TAG, "MqttProtocol deinit");  // 打印MQTT协议析构信息
    if (udp_ != nullptr) {  // 如果UDP不为空
        delete udp_;  // 删除UDP
    }
    if (mqtt_ != nullptr) {  // 如果MQTT不为空
        delete mqtt_;  // 删除MQTT
    }
    vEventGroupDelete(event_group_handle_);  // 删除事件组
}

bool MqttProtocol::StartMqttClient() {  // 开始MQTT客户端
    if (mqtt_ != nullptr) {  // 如果MQTT不为空
        ESP_LOGW(TAG, "Mqtt client already started");  // 打印MQTT客户端已启动信息
        delete mqtt_;  // 删除MQTT
    }

    Settings settings("mqtt", false);  // 设置MQTT
    endpoint_ = settings.GetString("endpoint");  // 获取MQTT端点
    client_id_ = settings.GetString("client_id");  // 获取MQTT客户端ID
    username_ = settings.GetString("username");  // 获取MQTT用户名
    password_ = settings.GetString("password");  // 获取MQTT密码
    subscribe_topic_ = settings.GetString("subscribe_topic");  // 获取MQTT订阅主题
    publish_topic_ = settings.GetString("publish_topic");  // 获取MQTT发布主题

    if (endpoint_.empty()) {
        ESP_LOGE(TAG, "MQTT endpoint is not specified");
        return false;
    }

    mqtt_ = Board::GetInstance().CreateMqtt();  // 创建MQTT
    mqtt_->SetKeepAlive(90);  // 设置MQTT保持连接时间

    mqtt_->OnDisconnected([this]() {  // 设置MQTT断开连接事件
        ESP_LOGI(TAG, "Disconnected from endpoint");  // 打印MQTT断开连接信息
    });

    mqtt_->OnMessage([this](const std::string& topic, const std::string& payload) {  // 设置MQTT消息事件
        cJSON* root = cJSON_Parse(payload.c_str());  // 解析JSON消息
        if (root == nullptr) {  // 如果解析失败
            ESP_LOGE(TAG, "Failed to parse json message %s", payload.c_str());  // 打印错误信息
            return;
        }
        cJSON* type = cJSON_GetObjectItem(root, "type");  // 获取消息类型
        if (type == nullptr) {  // 如果消息类型为空
            ESP_LOGE(TAG, "Message type is not specified");  // 打印错误信息
            cJSON_Delete(root);  // 删除JSON
            return;
        }

        if (strcmp(type->valuestring, "hello") == 0) {  // 如果消息类型为hello
            ParseServerHello(root);  // 解析服务器hello消息
        } else if (strcmp(type->valuestring, "goodbye") == 0) {  // 如果消息类型为goodbye
            auto session_id = cJSON_GetObjectItem(root, "session_id");  // 获取会话ID
            if (session_id == nullptr || session_id_ == session_id->valuestring) {  // 如果会话ID为空或会话ID等于session_id->valuestring
                Application::GetInstance().Schedule([this]() {  // 调度关闭音频通道
                    CloseAudioChannel();  // 关闭音频通道
                });
            }
        } else if (on_incoming_json_ != nullptr) {  // 如果on_incoming_json_不为空
            on_incoming_json_(root);  // 调用on_incoming_json_
        }
        cJSON_Delete(root);  // 删除JSON
    });

    ESP_LOGI(TAG, "Connecting to endpoint %s", endpoint_.c_str());  // 打印连接到端点信息
    if (!mqtt_->Connect(endpoint_, 8883, client_id_, username_, password_)) {  // 连接到端点
        ESP_LOGE(TAG, "Failed to connect to endpoint");  // 打印连接失败信息
        if (on_network_error_ != nullptr) {  // 如果on_network_error_不为空
            on_network_error_("无法连接服务");  // 调用on_network_error_
        }
        return false;
    }

    ESP_LOGI(TAG, "Connected to endpoint");  // 打印连接到端点信息
    if (!subscribe_topic_.empty()) {  // 如果订阅主题不为空
        mqtt_->Subscribe(subscribe_topic_, 2);  // 订阅主题
    }
    return true;  // 返回true
}

void MqttProtocol::SendText(const std::string& text) {  // 发送文本消息
    if (publish_topic_.empty()) {  // 如果发布主题为空
        return;  // 返回
    }
    mqtt_->Publish(publish_topic_, text);  // 发布消息
}

void MqttProtocol::SendAudio(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(channel_mutex_);
    if (udp_ == nullptr) {
        return;
    }

    std::string nonce(aes_nonce_);
    *(uint16_t*)&nonce[2] = htons(data.size());
    *(uint32_t*)&nonce[12] = htonl(++local_sequence_);

    std::string encrypted;
    encrypted.resize(aes_nonce_.size() + data.size());
    memcpy(encrypted.data(), nonce.data(), nonce.size());

    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};
    if (mbedtls_aes_crypt_ctr(&aes_ctx_, data.size(), &nc_off, (uint8_t*)nonce.c_str(), stream_block,
        (uint8_t*)data.data(), (uint8_t*)&encrypted[nonce.size()]) != 0) {
        ESP_LOGE(TAG, "Failed to encrypt audio data");
        return;
    }
    udp_->Send(encrypted);
}

void MqttProtocol::CloseAudioChannel() {
    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        if (udp_ != nullptr) {
            delete udp_;
            udp_ = nullptr;
        }
    }

    std::string message = "{";
    message += "\"session_id\":\"" + session_id_ + "\",";
    message += "\"type\":\"goodbye\"";
    message += "}";
    SendText(message);

    if (on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
}

bool MqttProtocol::OpenAudioChannel() {
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {  // 如果MQTT未连接
        ESP_LOGI(TAG, "MQTT is not connected, try to connect now");  // 打印MQTT未连接
        if (!StartMqttClient()) {  // 尝试连接MQTT
            return false;  // 返回失败
        }
    }

    session_id_ = "";

    // 发送 hello 消息申请 UDP 通道
    std::string message = "{";
    message += "\"type\":\"hello\",";
    message += "\"version\": 3,";
    message += "\"transport\":\"udp\",";
    message += "\"audio_params\":{";
    message += "\"format\":\"opus\", \"sample_rate\":16000, \"channels\":1, \"frame_duration\":" + std::to_string(OPUS_FRAME_DURATION_MS);
    message += "}}";
    SendText(message);

    // 等待服务器响应
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & MQTT_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        if (on_network_error_ != nullptr) {
            on_network_error_("等待响应超时");
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(channel_mutex_);
    if (udp_ != nullptr) {
        delete udp_;
    }
    udp_ = Board::GetInstance().CreateUdp();
    udp_->OnMessage([this](const std::string& data) {
        if (data.size() < sizeof(aes_nonce_)) {
            ESP_LOGE(TAG, "Invalid audio packet size: %zu", data.size());
            return;
        }
        if (data[0] != 0x01) {
            ESP_LOGE(TAG, "Invalid audio packet type: %x", data[0]);
            return;
        }
        uint32_t sequence = ntohl(*(uint32_t*)&data[12]);
        if (sequence < remote_sequence_) {
            ESP_LOGW(TAG, "Received audio packet with old sequence: %lu, expected: %lu", sequence, remote_sequence_);
            return;
        }
        if (sequence != remote_sequence_ + 1) {
            ESP_LOGW(TAG, "Received audio packet with wrong sequence: %lu, expected: %lu", sequence, remote_sequence_ + 1);
        }

        std::vector<uint8_t> decrypted;
        size_t decrypted_size = data.size() - aes_nonce_.size();
        size_t nc_off = 0;
        uint8_t stream_block[16] = {0};
        decrypted.resize(decrypted_size);
        auto nonce = (uint8_t*)data.data();
        auto encrypted = (uint8_t*)data.data() + aes_nonce_.size();
        int ret = mbedtls_aes_crypt_ctr(&aes_ctx_, decrypted_size, &nc_off, nonce, stream_block, encrypted, (uint8_t*)decrypted.data());
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to decrypt audio data, ret: %d", ret);
            return;
        }
        if (on_incoming_audio_ != nullptr) {
            on_incoming_audio_(std::move(decrypted));
        }
        remote_sequence_ = sequence;
    });

    udp_->Connect(udp_server_, udp_port_);

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    return true;
}

void MqttProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "udp") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (session_id != nullptr) {
        session_id_ = session_id->valuestring;
    }

    // Get sample rate from hello message
    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (audio_params != NULL) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (sample_rate != NULL) {
            server_sample_rate_ = sample_rate->valueint;
        }
    }

    auto udp = cJSON_GetObjectItem(root, "udp");
    if (udp == nullptr) {
        ESP_LOGE(TAG, "UDP is not specified");
        return;
    }
    udp_server_ = cJSON_GetObjectItem(udp, "server")->valuestring;
    udp_port_ = cJSON_GetObjectItem(udp, "port")->valueint;
    auto key = cJSON_GetObjectItem(udp, "key")->valuestring;
    auto nonce = cJSON_GetObjectItem(udp, "nonce")->valuestring;

    // auto encryption = cJSON_GetObjectItem(udp, "encryption")->valuestring;
    // ESP_LOGI(TAG, "UDP server: %s, port: %d, encryption: %s", udp_server_.c_str(), udp_port_, encryption);
    aes_nonce_ = DecodeHexString(nonce);
    mbedtls_aes_init(&aes_ctx_);
    mbedtls_aes_setkey_enc(&aes_ctx_, (const unsigned char*)DecodeHexString(key).c_str(), 128);
    local_sequence_ = 0;
    remote_sequence_ = 0;
    xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);
}

static const char hex_chars[] = "0123456789ABCDEF";
// 辅助函数，将单个十六进制字符转换为对应的数值
static inline uint8_t CharToHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;  // 对于无效输入，返回0
}

std::string MqttProtocol::DecodeHexString(const std::string& hex_string) {
    std::string decoded;
    decoded.reserve(hex_string.size() / 2);
    for (size_t i = 0; i < hex_string.size(); i += 2) {
        char byte = (CharToHex(hex_string[i]) << 4) | CharToHex(hex_string[i + 1]);
        decoded.push_back(byte);
    }
    return decoded;
}

bool MqttProtocol::IsAudioChannelOpened() const {
    return udp_ != nullptr;
}
