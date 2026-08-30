#include "ai_detection.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "AI";
static int s_ai_ready = 1;
esp_err_t ai_init(void)
{
    ESP_LOGI(TAG, "AI引擎初始化 (模拟模式)");
    ESP_LOGI(TAG, "⚠️  当前为模拟模式，真实硬件需部署 TFLite Micro");
    s_ai_ready = 1;
    return ESP_OK;
}

esp_err_t ai_detect_face(const uint8_t *jpeg_data, size_t jpeg_len, ai_result_t *result)
{
    if (!jpeg_data || !result) return ESP_ERR_INVALID_ARG;
    if (!s_ai_ready) return ESP_ERR_INVALID_STATE;

    static int call_count = 0;
    call_count++;

    // 每3次调用模拟一次"检测到人脸"
    int face = (call_count % 3 == 0) ? 1 : 0;

    result->face_detected = face;
    result->confidence = face ? (0.75 + (rand() % 200) / 1000.0) : 0.1;
    result->stay_seconds = face ? (2 + rand() % 8) : 0;

    if (face) {
        ESP_LOGI(TAG, "人脸检测: 检测到人脸 (置信度: %.2f, 停留: %d秒)",
                 result->confidence, result->stay_seconds);
        if (result->stay_seconds >= 5) {
            ESP_LOGW(TAG, "⚠️  可疑停留！");
        }
    } else {
        ESP_LOGI(TAG, "人脸检测: 未检测到人脸");
    }

    return ESP_OK;
}

int ai_is_ready(void)
{
    return s_ai_ready;
}
