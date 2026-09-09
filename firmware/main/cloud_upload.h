#ifndef CLOUD_UPLOAD_H
#define CLOUD_UPLOAD_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * 上传 JPEG 图片到云端（通过 FC 代理）
 * @param filename  文件名（仅用于日志，实际由 FC 生成）
 * @param data      图片数据
 * @param len       数据长度
 * @param type      类型："pass" 或 "stay"
 * @return ESP_OK 成功，其他失败
 */

// esp_err_t update_photos_index(const char *filename, const char *type);
esp_err_t cloud_upload_jpeg(const char *filename, const uint8_t *data, size_t len, const char *type);

#endif
