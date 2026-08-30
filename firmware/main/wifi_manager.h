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

#endif
