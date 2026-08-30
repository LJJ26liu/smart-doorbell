#ifndef AI_DETECTION_H
#define AI_DETECTION_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// AI检测结果
typedef struct {
    int face_detected;      // 1=检测到人脸，0=未检测到
    float confidence;       // 置信度 0.0 ~ 1.0
    int stay_seconds;       // 停留时长（秒），用于判断"路过"还是"可疑"
} ai_result_t;

// 初始化AI引擎
esp_err_t ai_init(void);

// 执行人脸检测（传入JPEG数据）
esp_err_t ai_detect_face(const uint8_t *jpeg_data, size_t jpeg_len, ai_result_t *result);

// 获取AI状态
int ai_is_ready(void);

#endif
