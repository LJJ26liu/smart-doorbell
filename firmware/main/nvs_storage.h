#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "esp_err.h"

// WiFi配置结构（保留）
typedef struct {
    char ssid[64];
    char password[64];
    int is_configured;
} saved_wifi_config_t;

// 新增：AP（热点）配置结构
typedef struct {
    char ssid[33];      // 最大32字节 + '\0'
    char password[65];  // 最大64字节 + '\0'
    int is_configured;  // 1=已配置，0=未配置
} ap_config_t;

esp_err_t nvs_storage_init(void);

// WiFi 配置（STA模式）
esp_err_t nvs_load_wifi_config(saved_wifi_config_t *config);
esp_err_t nvs_save_wifi_config(const saved_wifi_config_t *config);
esp_err_t nvs_clear_wifi_config(void);

// 新增：AP 配置（SoftAP模式）
esp_err_t nvs_load_ap_config(ap_config_t *config);
esp_err_t nvs_save_ap_config(const ap_config_t *config);
esp_err_t nvs_clear_ap_config(void);

#endif
