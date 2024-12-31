#include "wake_word_detect.h"
#include "application.h"

#include <esp_log.h>
#include <model_path.h>
#include <arpa/inet.h>
#include <sstream>

#define DETECTION_RUNNING_EVENT 1

static const char* TAG = "WakeWordDetect";

WakeWordDetect::WakeWordDetect()
    : afe_detection_data_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {

    event_group_ = xEventGroupCreate();
}

WakeWordDetect::~WakeWordDetect() {
    if (afe_detection_data_ != nullptr) {
        esp_afe_sr_v1.destroy(afe_detection_data_);
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    vEventGroupDelete(event_group_);
}

void WakeWordDetect::Initialize(int channels, bool reference) {  // 初始化
    channels_ = channels;  // 设置通道数
    reference_ = reference;  // 设置参考通道
    int ref_num = reference_ ? 1 : 0;  // 设置参考通道数

    srmodel_list_t *models = esp_srmodel_init("model");  // 初始化模型
    for (int i = 0; i < models->num; i++) {  // 遍历模型
        ESP_LOGI(TAG, "Model %d: %s", i, models->model_name[i]);  // 打印模型
        if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {  // 如果模型名包含ESP_WN_PREFIX
            wakenet_model_ = models->model_name[i];  // 设置wakenet_model_
            auto words = esp_srmodel_get_wake_words(models, wakenet_model_);  // 获取唤醒词
            // split by ";" to get all wake words
            std::stringstream ss(words);  // 创建字符串流
            std::string word;
            while (std::getline(ss, word, ';')) {  // 遍历字符串流
                wake_words_.push_back(word);  // 将唤醒词添加到唤醒词列表
            }
        }
    }

    afe_config_t afe_config = {  // 创建AFE配置
        .aec_init = reference_,  // 设置AEC初始化
        .se_init = true,  // 设置SE初始化
        .vad_init = true,  // 设置VAD初始化
        .wakenet_init = true,  // 设置Wakenet初始化
        .voice_communication_init = false,  // 设置语音通信初始化
        .voice_communication_agc_init = false,  // 设置语音通信AGC初始化
        .voice_communication_agc_gain = 10,  // 设置语音通信AGC增益
        .vad_mode = VAD_MODE_3,  // 设置VAD模式 
        .wakenet_model_name = wakenet_model_,  // 设置Wakenet模型名
        .wakenet_model_name_2 = NULL,  // 设置Wakenet模型名2
        .wakenet_mode = DET_MODE_90,  // 设置Wakenet模式
        .afe_mode = SR_MODE_HIGH_PERF,  // 设置AFE模式
        .afe_perferred_core = 1,  // 设置AFE首选核心
        .afe_perferred_priority = 1,  // 设置AFE首选优先级
        .afe_ringbuf_size = 50,  // 设置AFE环形缓冲区大小
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM,  // 设置内存分配模式
        .afe_linear_gain = 1.0,  // 设置AFE线性增益
        .agc_mode = AFE_MN_PEAK_AGC_MODE_2,  // 设置AGC模式
        .pcm_config = {  // 创建PCM配置
            .total_ch_num = channels_,  // 设置总通道数
            .mic_num = channels_ - ref_num,  // 设置麦克风通道数
            .ref_num = ref_num,  // 设置参考通道数
            .sample_rate = 16000  // 设置采样率
        },
        .debug_init = false,  // 设置调试初始化
        .debug_hook = {{ AFE_DEBUG_HOOK_MASE_TASK_IN, NULL }, { AFE_DEBUG_HOOK_FETCH_TASK_IN, NULL }},  // 设置调试钩子
        .afe_ns_mode = NS_MODE_SSP,  // 设置NS模式
        .afe_ns_model_name = NULL,  // 设置NS模型名
        .fixed_first_channel = true,  // 设置固定第一个通道
    };

    afe_detection_data_ = esp_afe_sr_v1.create_from_config(&afe_config);  // 创建AFE检测数据

    xTaskCreate([](void* arg) {  // 创建音频检测任务    
        auto this_ = (WakeWordDetect*)arg;  // 将任务指针转换为WakeWordDetect指针
        this_->AudioDetectionTask();  // 执行音频检测任务
        vTaskDelete(NULL);  // 删除任务
    }, "audio_detection", 4096 * 2, this, 1, nullptr);  // 创建音频检测任务
}

void WakeWordDetect::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {  // 设置唤醒词检测回调函数
    wake_word_detected_callback_ = callback;  // 设置唤醒词检测回调函数
}

void WakeWordDetect::OnVadStateChange(std::function<void(bool speaking)> callback) {  // 设置VAD状态变化回调函数
    vad_state_change_callback_ = callback;  // 设置VAD状态变化回调函数
}

void WakeWordDetect::StartDetection() {  // 开始检测
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);  // 设置检测事件
}

void WakeWordDetect::StopDetection() {  // 停止检测
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);  // 清除检测事件
}

bool WakeWordDetect::IsDetectionRunning() {
    return xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT;
}

void WakeWordDetect::Feed(const std::vector<int16_t>& data) {  // 输入音频数据
    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());  // 将音频数据添加到输入缓冲区

    auto chunk_size = esp_afe_sr_v1.get_feed_chunksize(afe_detection_data_) * channels_;  // 获取输入缓冲区大小
    while (input_buffer_.size() >= chunk_size) {  // 如果输入缓冲区大小大于等于输入缓冲区大小
        esp_afe_sr_v1.feed(afe_detection_data_, input_buffer_.data());  // 将输入缓冲区数据添加到AFE检测数据
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_size);  // 删除输入缓冲区数据
    }
}

void WakeWordDetect::AudioDetectionTask() {  // 音频检测任务
    auto chunk_size = esp_afe_sr_v1.get_fetch_chunksize(afe_detection_data_);  // 获取输入缓冲区大小
    ESP_LOGI(TAG, "Audio detection task started, chunk size: %d", chunk_size);  // 打印音频检测任务开始

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = esp_afe_sr_v1.fetch(afe_detection_data_);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            if (res != nullptr) {
                ESP_LOGI(TAG, "Error code: %d", res->ret_value);
            }
            continue;;
        }

        // Store the wake word data for voice recognition, like who is speaking
        StoreWakeWordData((uint16_t*)res->data, res->data_size / sizeof(uint16_t));

        // VAD state change
        if (vad_state_change_callback_) {
            if (res->vad_state == AFE_VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                vad_state_change_callback_(true);
            } else if (res->vad_state == AFE_VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                vad_state_change_callback_(false);
            }
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            StopDetection();
            last_detected_wake_word_ = wake_words_[res->wake_word_index - 1];

            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
        }
    }
}

void WakeWordDetect::StoreWakeWordData(uint16_t* data, size_t samples) {  // 存储唤醒词数据
    // store audio data to wake_word_pcm_
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));  // 将音频数据添加到唤醒词PCM数据列表      
    // keep about 2 seconds of data, detect duration is 32ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 32) {  // 如果唤醒词PCM数据列表大小大于2000/32
        wake_word_pcm_.pop_front();  // 删除唤醒词PCM数据列表第一个元素 
    }
}

void WakeWordDetect::EncodeWakeWordData() {
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(4096 * 8, MALLOC_CAP_SPIRAM);
    }
    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (WakeWordDetect*)arg;
        {
            auto start_time = esp_timer_get_time();
            auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
            encoder->SetComplexity(0); // 0 is the fastest

            for (auto& pcm: this_->wake_word_pcm_) {
                encoder->Encode(std::move(pcm), [this_](std::vector<uint8_t>&& opus) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(std::move(opus));
                    this_->wake_word_cv_.notify_all();
                });
            }
            this_->wake_word_pcm_.clear();

            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %zu packets in %lld ms",
                this_->wake_word_opus_.size(), (end_time - start_time) / 1000);

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_detect_packets", 4096 * 8, this, 1, wake_word_encode_task_stack_, &wake_word_encode_task_buffer_);
}

bool WakeWordDetect::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
