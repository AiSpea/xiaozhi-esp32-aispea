#include "system_info.h"

#include <freertos/task.h>
#include <esp_log.h>
#include <esp_flash.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>


#define TAG "SystemInfo"

size_t SystemInfo::GetFlashSize() {  // 获取闪存大小
    uint32_t flash_size;  // 闪存大小
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {  // 获取闪存大小
        ESP_LOGE(TAG, "Failed to get flash size");  // 如果失败
        return 0;
    }
    return (size_t)flash_size;
}

size_t SystemInfo::GetMinimumFreeHeapSize() {  // 获取最小空闲堆大小
    return esp_get_minimum_free_heap_size();  // 获取最小空闲堆大小
}

size_t SystemInfo::GetFreeHeapSize() {  // 获取空闲堆大小
    return esp_get_free_heap_size();  // 获取空闲堆大小
}

std::string SystemInfo::GetMacAddress() {  // 获取MAC地址
    uint8_t mac[6];  // 6字节MAC地址
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // 读取MAC地址
    char mac_str[18];  // 18字节MAC地址字符串
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);  // 格式化MAC地址字符串
    return std::string(mac_str);
}

std::string SystemInfo::GetChipModelName() {  // 获取芯片型号
    return std::string(CONFIG_IDF_TARGET);  // 返回芯片型号
}

esp_err_t SystemInfo::PrintRealTimeStats(TickType_t xTicksToWait) {  // 打印实时统计    
    #define ARRAY_SIZE_OFFSET 5
    TaskStatus_t *start_array = NULL, *end_array = NULL;  // 任务状态数组
    UBaseType_t start_array_size, end_array_size;  // 任务状态数组大小
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;  // 任务运行时间
    esp_err_t ret;  // 错误码
    uint32_t total_elapsed_time;  // 总运行时间

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;  // 获取任务数量
    start_array = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * start_array_size);  // 分配任务状态数组
    if (start_array == NULL) {  // 如果分配失败
        ret = ESP_ERR_NO_MEM;  // 设置错误码
        goto exit;  // 跳转到退出
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);  // 获取任务状态
    if (start_array_size == 0) {  // 如果任务状态数组大小为0
        ret = ESP_ERR_INVALID_SIZE;  // 设置错误码
        goto exit;  // 跳转到退出
    }

    vTaskDelay(xTicksToWait);  // 延迟

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;  // 获取任务数量
    end_array = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * end_array_size);  // 分配任务状态数组
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);  // 获取任务状态
    if (end_array_size == 0) {  // 如果任务状态数组大小为0
        ret = ESP_ERR_INVALID_SIZE;  // 设置错误码
        goto exit;  // 跳转到退出
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    total_elapsed_time = (end_run_time - start_run_time);  // 计算总运行时间    
    if (total_elapsed_time == 0) {  // 如果总运行时间为0
        ret = ESP_ERR_INVALID_STATE;  // 设置错误码
        goto exit;  // 跳转到退出
    }

    printf("| Task | Run Time | Percentage\n");  // 打印任务、运行时间和百分比
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {  // 遍历任务状态数组   
        int k = -1;  // 匹配任务索引
        for (int j = 0; j < end_array_size; j++) {  // 遍历任务状态数组
            if (start_array[i].xHandle == end_array[j].xHandle) {  // 如果任务句柄匹配
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {  // 如果匹配任务索引大于等于0
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;  // 计算任务运行时间
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * CONFIG_FREERTOS_NUMBER_OF_CORES);  // 计算任务百分比 
            printf("| %-16s | %8lu | %4lu%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);  // 打印任务、运行时间和百分比
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {  // 遍历任务状态数组
        if (start_array[i].xHandle != NULL) {  // 如果任务句柄不为空
            printf("| %s | Deleted\n", start_array[i].pcTaskName);  // 打印任务
        }
    }
    for (int i = 0; i < end_array_size; i++) {  // 遍历任务状态数组
        if (end_array[i].xHandle != NULL) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

