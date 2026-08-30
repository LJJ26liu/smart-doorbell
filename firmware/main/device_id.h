#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include "esp_err.h"
#include <stdint.h>
#include <string.h>

// 设备 ID 长度（MAC 地址字符串长度：12 位十六进制 + 1 位结束符 = 13）
#define DEVICE_ID_LEN 13

// 初始化设备 ID（从 NVS 读取，如果不存在则从 MAC 地址生成并保存）
esp_err_t device_id_init(void);

// 获取设备 ID（返回字符串指针）
const char* device_id_get(void);

#endif
