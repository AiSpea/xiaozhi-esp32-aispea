#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

#define TAG "Application"

extern const char p3_err_reg_start[] asm("_binary_err_reg_p3_start");
extern const char p3_err_reg_end[] asm("_binary_err_reg_p3_end");
extern const char p3_err_pin_start[] asm("_binary_err_pin_p3_start");
extern const char p3_err_pin_end[] asm("_binary_err_pin_p3_end");
extern const char p3_err_wificonfig_start[] asm("_binary_err_wificonfig_p3_start");
extern const char p3_err_wificonfig_end[] asm("_binary_err_wificonfig_p3_end");

static const char* const STATE_STRINGS[] = {
    "unknown",  // 未知状态
    "idle",  // 空闲状态
    "connecting",  // 连接状态
    "listening",  // 监听状态
    "speaking",  // 说话状态
    "upgrading",  // 升级状态
    "invalid_state"  // 无效状态
};

Application::Application() : background_task_(4096 * 8) {  // 初始化背景任务
    event_group_ = xEventGroupCreate();  // 创建事件组

    ota_.SetCheckVersionUrl(CONFIG_OTA_VERSION_URL);  // 设置检查版本URL
    ota_.SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
}

Application::~Application() {
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion() {   // 检查新版本
    auto& board = Board::GetInstance();  // 获取板子实例
    auto display = board.GetDisplay();  // 获取显示实例
    // Check if there is a new firmware version available
    ota_.SetPostData(board.GetJson());  // 设置POST数据

    while (true) {
        if (ota_.CheckVersion()) {
            if (ota_.HasNewVersion()) {
                // Wait for the chat state to be idle
                do {
                    vTaskDelay(pdMS_TO_TICKS(3000));
                } while (GetChatState() != kChatStateIdle);

                SetChatState(kChatStateUpgrading);
                
                display->SetIcon(FONT_AWESOME_DOWNLOAD);
                display->SetStatus("新版本 " + ota_.GetFirmwareVersion());

                // 预先关闭音频输出，避免升级过程有音频操作
                board.GetAudioCodec()->EnableOutput(false);

                ota_.StartUpgrade([display](int progress, size_t speed) {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "%d%% %zuKB/s", progress, speed / 1024);
                    display->SetStatus(buffer);
                });

                // If upgrade success, the device will reboot and never reach here
                ESP_LOGI(TAG, "Firmware upgrade failed...");
                SetChatState(kChatStateIdle);
            } else {
                ota_.MarkCurrentVersionValid();
                display->ShowNotification("版本 " + ota_.GetCurrentVersion());
            }
            return;
        }

        // Check again in 60 seconds
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void Application::Alert(const std::string& title, const std::string& message) {
    ESP_LOGW(TAG, "Alert: %s, %s", title.c_str(), message.c_str());
    auto display = Board::GetInstance().GetDisplay();
    display->ShowNotification(message);

    if (message == "PIN is not ready") {
        PlayLocalFile(p3_err_pin_start, p3_err_pin_end - p3_err_pin_start);
    } else if (message == "Configuring WiFi") {
        PlayLocalFile(p3_err_wificonfig_start, p3_err_wificonfig_end - p3_err_wificonfig_start);
    } else if (message == "Registration denied") {
        PlayLocalFile(p3_err_reg_start, p3_err_reg_end - p3_err_reg_start);
    }
}

void Application::PlayLocalFile(const char* data, size_t size) {
    ESP_LOGI(TAG, "PlayLocalFile: %zu bytes", size);
    SetDecodeSampleRate(16000);
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        std::vector<uint8_t> opus;
        opus.resize(payload_size);
        memcpy(opus.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(opus));
    }
}

void Application::ToggleChatState() {  // 切换聊天状态
    Schedule([this]() {
        if (!protocol_) {  // 如果协议未初始化
            ESP_LOGE(TAG, "Protocol not initialized");  // 打印错误日志
            return;
        }

        if (chat_state_ == kChatStateIdle) {
            SetChatState(kChatStateConnecting);  // 设置聊天状态为连接中
            if (!protocol_->OpenAudioChannel()) {  // 如果无法打开音频通道
                Alert("Error", "Failed to open audio channel");  // 显示错误消息
                SetChatState(kChatStateIdle);  // 设置聊天状态为空闲
                return;
            }

            keep_listening_ = true;  // 保持监听
            protocol_->SendStartListening(kListeningModeAutoStop);  // 发送开始监听命令
            SetChatState(kChatStateListening);  // 设置聊天状态为监听中
        } else if (chat_state_ == kChatStateSpeaking) {  // 如果聊天状态为说话中
            AbortSpeaking(kAbortReasonNone);  // 中止说话
        } else if (chat_state_ == kChatStateListening) {  // 如果聊天状态为监听中
            protocol_->CloseAudioChannel();  // 关闭音频通道
        }
    });
}

void Application::StartListening() {  // 开始监听
    Schedule([this]() {
        if (!protocol_) {  // 如果协议未初始化
            ESP_LOGE(TAG, "Protocol not initialized");  // 打印错误日志
            return;
        }
        
        keep_listening_ = false;  // 保持监听
        if (chat_state_ == kChatStateIdle) {  // 如果聊天状态为空闲
            if (!protocol_->IsAudioChannelOpened()) {  // 如果音频通道未打开
                SetChatState(kChatStateConnecting);  // 设置聊天状态为连接中
                if (!protocol_->OpenAudioChannel()) {  // 如果无法打开音频通道
                    SetChatState(kChatStateIdle);  // 设置聊天状态为空闲
                    Alert("Error", "Failed to open audio channel");  // 显示错误消息
                    return;
                }
            }
            protocol_->SendStartListening(kListeningModeManualStop);  // 发送开始监听命令
            SetChatState(kChatStateListening);  // 设置聊天状态为监听中
        } else if (chat_state_ == kChatStateSpeaking) {  // 如果聊天状态为说话中
            AbortSpeaking(kAbortReasonNone);  // 中止说话
            protocol_->SendStartListening(kListeningModeManualStop);  // 发送开始监听命令
            // FIXME: Wait for the speaker to empty the buffer
            vTaskDelay(pdMS_TO_TICKS(120));  // 延迟120ms
            SetChatState(kChatStateListening);  // 设置聊天状态为监听中
        }
    });
}

void Application::StopListening() {
    Schedule([this]() {
        if (chat_state_ == kChatStateListening) {
            protocol_->SendStopListening();
            SetChatState(kChatStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();  // 获取板子实例
    auto builtin_led = board.GetBuiltinLed();  // 获取内置LED
    builtin_led->SetBlue();  // 设置LED颜色为蓝色
    builtin_led->StartContinuousBlink(100);  // 开始连续闪烁

    /* Setup the display */
    auto display = board.GetDisplay();  // 获取显示实例

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();  // 获取音频编解码器
    opus_decode_sample_rate_ = codec->output_sample_rate();  // 设置解码采样率
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(opus_decode_sample_rate_, 1);  // 创建Opus解码器
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);  // 创建Opus编码器
    if (codec->input_sample_rate() != 16000) {  // 如果输入采样率不为16000，则需要重采样
        input_resampler_.Configure(codec->input_sample_rate(), 16000);  // 配置输入重采样器
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);  // 配置参考重采样器
    }
    codec->OnInputReady([this, codec]() {  // 设置输入准备事件
        BaseType_t higher_priority_task_woken = pdFALSE;    // 更高优先级的任务是否被唤醒
        xEventGroupSetBitsFromISR(event_group_, AUDIO_INPUT_READY_EVENT, &higher_priority_task_woken);  // 从ISR中设置事件组位
        return higher_priority_task_woken == pdTRUE;  // 返回更高优先级的任务是否被唤醒
    });
    codec->OnOutputReady([this]() {  // 设置输出准备事件
        BaseType_t higher_priority_task_woken = pdFALSE;  // 更高优先级的任务是否被唤醒
        xEventGroupSetBitsFromISR(event_group_, AUDIO_OUTPUT_READY_EVENT, &higher_priority_task_woken);  // 从ISR中设置事件组位
        return higher_priority_task_woken == pdTRUE;  // 返回更高优先级的任务是否被唤醒
    });
    codec->Start();  // 启动音频编解码器

    /* Start the main loop */
    xTaskCreate([](void* arg) {  // 创建主循环任务
        Application* app = (Application*)arg;  // 获取应用程序实例
        app->MainLoop();  // 运行主循环
        vTaskDelete(NULL);  // 删除任务
    }, "main_loop", 4096 * 2, this, 2, nullptr);  // 创建主循环任务

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Check for new firmware version or get the MQTT broker address
    xTaskCreate([](void* arg) {  // 创建检查新版本任务
        Application* app = (Application*)arg;  // 获取应用程序实例
        app->CheckNewVersion();  // 检查新版本
        vTaskDelete(NULL);  // 删除任务
    }, "check_new_version", 4096 * 2, this, 1, nullptr);  // 创建检查新版本任务

#if CONFIG_IDF_TARGET_ESP32S3
    audio_processor_.Initialize(codec->input_channels(), codec->input_reference());
    audio_processor_.OnOutput([this](std::vector<int16_t>&& data) {
        background_task_.Schedule([this, data = std::move(data)]() mutable {
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                Schedule([this, opus = std::move(opus)]() {
                    protocol_->SendAudio(opus);
                });
            });
        });
    });

    wake_word_detect_.Initialize(codec->input_channels(), codec->input_reference());
    wake_word_detect_.OnVadStateChange([this](bool speaking) {
        Schedule([this, speaking]() {
            auto builtin_led = Board::GetInstance().GetBuiltinLed();
            if (chat_state_ == kChatStateListening) {
                if (speaking) {
                    builtin_led->SetRed(HIGH_BRIGHTNESS);
                } else {
                    builtin_led->SetRed(LOW_BRIGHTNESS);
                }
                builtin_led->TurnOn();
            }
        });
    });

    wake_word_detect_.OnWakeWordDetected([this](const std::string& wake_word) {
        Schedule([this, &wake_word]() {
            if (chat_state_ == kChatStateIdle) {
                SetChatState(kChatStateConnecting);
                wake_word_detect_.EncodeWakeWordData();

                if (!protocol_->OpenAudioChannel()) {
                    ESP_LOGE(TAG, "Failed to open audio channel");
                    SetChatState(kChatStateIdle);
                    wake_word_detect_.StartDetection();
                    return;
                }
                
                std::vector<uint8_t> opus;
                // Encode and send the wake word data to the server
                while (wake_word_detect_.GetWakeWordOpus(opus)) {
                    protocol_->SendAudio(opus);
                }
                // Set the chat state to wake word detected
                protocol_->SendWakeWordDetected(wake_word);
                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
                keep_listening_ = true;
                SetChatState(kChatStateListening);
            } else if (chat_state_ == kChatStateSpeaking) {
                AbortSpeaking(kAbortReasonWakeWordDetected);
            }

            // Resume detection
            wake_word_detect_.StartDetection();
        });
    });
    wake_word_detect_.StartDetection();
#endif

    // Initialize the protocol
    display->SetStatus("初始化协议");  // 设置显示状态为“初始化协议”
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    protocol_ = std::make_unique<WebsocketProtocol>();  // 使用WebSocket协议
#else
    protocol_ = std::make_unique<MqttProtocol>();  // 使用MQTT协议
#endif
    protocol_->OnNetworkError([this](const std::string& message) {  // 网络错误
        Alert("Error", std::move(message));  // 显示错误消息
    });
    protocol_->OnIncomingAudio([this](std::vector<uint8_t>&& data) {  // 接收到音频数据
        std::lock_guard<std::mutex> lock(mutex_);
        if (chat_state_ == kChatStateSpeaking) {
            audio_decode_queue_.emplace_back(std::move(data));  // 将音频数据添加到解码队列中
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);  // 设置电源保存模式
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {  // 如果服务器的音频采样率与设备输出的采样率不一致，则重采样后可能会失真
            ESP_LOGW(TAG, "服务器的音频采样率 %d 与设备输出的采样率 %d 不一致，重采样后可能会失真",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
        SetDecodeSampleRate(protocol_->server_sample_rate());  // 设置解码采样率
        // 物联网设备描述符
        last_iot_states_.clear();
        auto& thing_manager = iot::ThingManager::GetInstance();
        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
    });
    protocol_->OnAudioChannelClosed([this, &board]() {  // 音频通道关闭
        board.SetPowerSaveMode(true);  // 设置电源保存模式
        Schedule([this]() {
            SetChatState(kChatStateIdle);  // 设置聊天状态为空闲
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {  // 处理接收到的JSON数据 
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");  // 获取JSON数据中的类型
        if (strcmp(type->valuestring, "tts") == 0) {  // 如果类型为tts
            auto state = cJSON_GetObjectItem(root, "state");  // 获取JSON数据中的状态
            if (strcmp(state->valuestring, "start") == 0) {  // 如果状态为start
                Schedule([this]() {  // 调度任务
                    aborted_ = false;  // 设置中止标志为false
                    if (chat_state_ == kChatStateIdle || chat_state_ == kChatStateListening) {  // 如果聊天状态为空闲或监听中
                        SetChatState(kChatStateSpeaking);  // 设置聊天状态为说话中
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {  // 如果状态为stop
                Schedule([this]() {  // 调度任务
                    if (chat_state_ == kChatStateSpeaking) {  // 如果聊天状态为说话中
                        background_task_.WaitForCompletion();  // 等待任务完成
                        if (keep_listening_) {
                            protocol_->SendStartListening(kListeningModeAutoStop);
                            SetChatState(kChatStateListening);
                        } else {
                            SetChatState(kChatStateIdle);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {  // 如果状态为sentence_start
                auto text = cJSON_GetObjectItem(root, "text");  // 获取JSON数据中的文本
                if (text != NULL) {  // 如果文本不为空  
                    ESP_LOGI(TAG, "<< %s", text->valuestring);  // 打印文本
                    display->SetChatMessage("assistant", text->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {  // 如果类型为stt
            auto text = cJSON_GetObjectItem(root, "text");  // 获取JSON数据中的文本
            if (text != NULL) {  // 如果文本不为空
                ESP_LOGI(TAG, ">> %s", text->valuestring);  // 打印文本
                display->SetChatMessage("user", text->valuestring);  // 设置聊天消息
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {  // 如果类型为llm
            auto emotion = cJSON_GetObjectItem(root, "emotion");  // 获取JSON数据中的情绪
            if (emotion != NULL) {  // 如果情绪不为空
                display->SetEmotion(emotion->valuestring);  // 设置情绪
            }
        } else if (strcmp(type->valuestring, "iot") == 0) {  // 如果类型为iot
            auto commands = cJSON_GetObjectItem(root, "commands");  // 获取JSON数据中的命令
            if (commands != NULL) {  // 如果命令不为空
                auto& thing_manager = iot::ThingManager::GetInstance();  // 获取物联网管理器实例
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {  // 遍历命令数组   
                    auto command = cJSON_GetArrayItem(commands, i);  // 获取命令
                    thing_manager.Invoke(command);  // 调用命令
                }
            }
        }
    });

    // Blink the LED to indicate the device is running
    display->SetStatus("待命");  // 设置显示状态为“待命”
    builtin_led->SetGreen();  // 设置LED为绿色
    builtin_led->BlinkOnce();  // 闪烁一次

    SetChatState(kChatStateIdle);  // 设置聊天状态为空闲
}

void Application::Schedule(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainLoop() {  // 主循环
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_,
            SCHEDULE_EVENT | AUDIO_INPUT_READY_EVENT | AUDIO_OUTPUT_READY_EVENT,    // 等待事件组中的事件
            pdTRUE, pdFALSE, portMAX_DELAY);  // 等待事件组中的事件 

        if (bits & AUDIO_INPUT_READY_EVENT) {  // 如果音频输入事件
            InputAudio();  // 输入音频
        }
        if (bits & AUDIO_OUTPUT_READY_EVENT) {  // 如果音频输出事件
            OutputAudio();  // 输出音频
        }
        if (bits & SCHEDULE_EVENT) {  // 如果调度事件
            mutex_.lock();  // 锁定互斥锁
            std::list<std::function<void()>> tasks = std::move(main_tasks_);  // 移动主任务列表
            mutex_.unlock();  // 解锁互斥锁
            for (auto& task : tasks) {  // 遍历任务列表
                task();  // 执行任务
            }
        }
    }
}

void Application::ResetDecoder() {  // 重置解码器
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    last_output_time_ = std::chrono::steady_clock::now();
    Board::GetInstance().GetAudioCodec()->EnableOutput(true);
}

void Application::OutputAudio() {
    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty()) {
        // Disable the output if there is no audio data for a long time
        if (chat_state_ == kChatStateIdle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds) {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    if (chat_state_ == kChatStateListening) {
        audio_decode_queue_.clear();
        return;
    }

    last_output_time_ = now;
    auto opus = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();

    background_task_.Schedule([this, codec, opus = std::move(opus)]() mutable {
        if (aborted_) {
            return;
        }

        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(opus), pcm)) {
            return;
        }

        // Resample if the sample rate is different
        if (opus_decode_sample_rate_ != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
        
        codec->OutputData(pcm);
    });
}

void Application::InputAudio() {  // 输入音频
    auto codec = Board::GetInstance().GetAudioCodec();  // 获取音频编解码器
    std::vector<int16_t> data;  // 音频数据
    if (!codec->InputData(data)) {  // 如果无法输入音频数据
        return;
    }

    if (codec->input_sample_rate() != 16000) {  // 如果输入采样率不为16000  
        if (codec->input_channels() == 2) {  // 如果输入通道为2
            auto mic_channel = std::vector<int16_t>(data.size() / 2);  // 麦克风通道
            auto reference_channel = std::vector<int16_t>(data.size() / 2);  // 参考通道
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {  // 遍历音频数据
                mic_channel[i] = data[j];  // 将音频数据添加到麦克风通道
                reference_channel[i] = data[j + 1];  // 将音频数据添加到参考通道
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));  // 重采样麦克风通道
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));  // 重采样参考通道
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());  // 处理麦克风通道
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());  // 处理参考通道
            data.resize(resampled_mic.size() + resampled_reference.size());  // 调整音频数据大小
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {  // 遍历重采样后的音频数据
                data[j] = resampled_mic[i];  // 将重采样后的音频数据添加到音频数据
                data[j + 1] = resampled_reference[i];  // 将重采样后的音频数据添加到音频数据
            }
        } else {  // 如果输入通道为1
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));  // 重采样音频数据
            input_resampler_.Process(data.data(), data.size(), resampled.data());  // 处理音频数据
            data = std::move(resampled);  // 将重采样后的音频数据赋值给音频数据
        }
    }
    
#if CONFIG_IDF_TARGET_ESP32S3
    if (audio_processor_.IsRunning()) {
        audio_processor_.Input(data);
    }
    if (wake_word_detect_.IsDetectionRunning()) {
        wake_word_detect_.Feed(data);
    }
#else
    if (chat_state_ == kChatStateListening) {  // 如果聊天状态为监听中
        background_task_.Schedule([this, data = std::move(data)]() mutable {  // 调度任务
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {  // 编码音频数据
                Schedule([this, opus = std::move(opus)]() {  // 调度任务
                    protocol_->SendAudio(opus);  // 发送音频数据
                });
            });
        });
    }
#endif
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");  // 打印“中止说话”
    aborted_ = true;  // 设置中止标志为true
    protocol_->SendAbortSpeaking(reason);  // 发送中止说话命令
}

void Application::SetChatState(ChatState state) {   // 设置聊天状态
    if (chat_state_ == state) {
        return;
    }
    
    chat_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[chat_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_.WaitForCompletion();

    auto display = Board::GetInstance().GetDisplay();
    auto builtin_led = Board::GetInstance().GetBuiltinLed();
    switch (state) {
        case kChatStateUnknown:
        case kChatStateIdle:
            builtin_led->TurnOff();
            display->SetStatus("待命");
            display->SetEmotion("neutral");
#ifdef CONFIG_IDF_TARGET_ESP32S3
            audio_processor_.Stop();
#endif
            break;
        case kChatStateConnecting:
            builtin_led->SetBlue();
            builtin_led->TurnOn();
            display->SetStatus("连接中...");
            break;
        case kChatStateListening:
            builtin_led->SetRed();
            builtin_led->TurnOn();
            display->SetStatus("聆听中...");
            display->SetEmotion("neutral");
            ResetDecoder();
            opus_encoder_->ResetState();
#if CONFIG_IDF_TARGET_ESP32S3
            audio_processor_.Start();
#endif
            UpdateIotStates();
            break;
        case kChatStateSpeaking:  // 说话状态
            builtin_led->SetGreen();
            builtin_led->TurnOn();
            display->SetStatus("说话中...");
            ResetDecoder();
#if CONFIG_IDF_TARGET_ESP32S3
            audio_processor_.Stop();
#endif
            break;
        case kChatStateUpgrading:  // 升级状态
            builtin_led->SetGreen();
            builtin_led->StartContinuousBlink(100);
            break;
        default:
            ESP_LOGE(TAG, "Invalid chat state: %d", chat_state_);
            return;
    }
}

void Application::SetDecodeSampleRate(int sample_rate) {
    if (opus_decode_sample_rate_ == sample_rate) {
        return;
    }

    opus_decode_sample_rate_ = sample_rate;
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(opus_decode_sample_rate_, 1);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decode_sample_rate_ != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decode_sample_rate_, codec->output_sample_rate());
        output_resampler_.Configure(opus_decode_sample_rate_, codec->output_sample_rate());
    }
}

void Application::UpdateIotStates() {
    auto& thing_manager = iot::ThingManager::GetInstance();
    auto states = thing_manager.GetStatesJson();
    if (states != last_iot_states_) {
        last_iot_states_ = states;
        protocol_->SendIotStates(states);
    }
}
