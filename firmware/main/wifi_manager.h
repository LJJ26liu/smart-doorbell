#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect_saved(void);
esp_err_t wifi_manager_start_provisioning(void);
esp_err_t wifi_manager_stop_provisioning(void);

// STA 连接状态
int wifi_manager_is_connected(void);
const char* wifi_manager_get_ip_str(void);
const char* wifi_manager_get_sta_ssid(void);

// AP 热点客户端连接状态
bool wifi_manager_is_ap_connected(void);

bool wifi_manager_is_sta_retry_failed(void);   // 返回 true 表示 STA 重试已失败（放弃重连）
void wifi_manager_reset_retry_counter(void);   // 重置重试计数（用于重新触发连接）

#endif
