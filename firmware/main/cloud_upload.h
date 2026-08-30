#ifndef CLOUD_UPLOAD_H
#define CLOUD_UPLOAD_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// OSS 配置（请按实际情况修改）
#define OSS_ACCESS_KEY     "YOUR_ACCESS_KEY"      // 替换为实际参数
#define OSS_ACCESS_SECRET  "YOUR_ACCESS_SECRET"      // 替换为实际参数
#define OSS_ENDPOINT       "oss-cn-guangzhou.aliyuncs.com"
#define OSS_BUCKET         "YOUR_BUCKET_NAME"      // 替换为实际参数

esp_err_t update_photos_index(const char *filename, const char *type);
esp_err_t cloud_upload_jpeg(const char *filename, const uint8_t *data, size_t len, const char *type);

#endif
