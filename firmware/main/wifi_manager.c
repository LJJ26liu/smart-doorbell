#include "wifi_manager.h"
#include "nvs_storage.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "string.h"
#include <stdbool.h>

static const char *TAG = "WIFI";

static int s_is_connected = 0;
static char s_ip_str[16] = {0};
static char s_sta_ssid[64] = {0};
static volatile bool s_ap_sta_connected = false;

// ===== 重试控制 =====
static int s_retry_count = 0;
#define MAX_RETRY 3
static bool s_retry_failed = false;

// 事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "WiFi事件 ID: %ld", event_id);

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA 启动，开始连接...");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                s_is_connected = 0;
                if (s_retry_count < MAX_RETRY) {
                    s_retry_count++;
                    ESP_LOGW(TAG, "WiFi断开，重试第 %d 次...", s_retry_count);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "⚠️ 重试次数已达上限（%d次），停止重连。请通过AP热点配网。", MAX_RETRY);
                    s_retry_failed = true;
                }
                break;

            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "✅ 客户端连接到AP热点");
                s_ap_sta_connected = true;
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "客户端断开AP热点");
                s_ap_sta_connected = false;
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
        s_is_connected = 1;
        s_retry_count = 0;
        s_retry_failed = false;
        ESP_LOGI(TAG, "✅ 获取到IP地址: %s", s_ip_str);

        // ===== 手动设置 DNS =====
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_dns_info_t dns;
            // 主 DNS：Google Public DNS
            dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
            // 备用 DNS：国内公共 DNS
            dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("114.114.114.114");
            esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
            ESP_LOGI(TAG, "手动设置 DNS: 8.8.8.8, 114.114.114.114");
        } else {
            ESP_LOGW(TAG, "获取网络接口失败，无法设置 DNS");
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "初始化WiFi (AP+STA模式)...");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 1. 设置模式为 AP+STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // 2. 配置 AP（始终开启）
    ap_config_t ap_cfg;
    esp_err_t ret = nvs_load_ap_config(&ap_cfg);
    if (ret != ESP_OK || !ap_cfg.is_configured) {
        strcpy(ap_cfg.ssid, "Doorbell_AP");
        strcpy(ap_cfg.password, "12345678");
        ESP_LOGI(TAG, "使用默认AP配置");
    } else {
        ESP_LOGI(TAG, "加载自定义AP配置: SSID=%s", ap_cfg.ssid);
    }

    wifi_config_t ap_wifi_config = {
        .ap = {
            .ssid_len = 0,
            .channel = 6,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    strncpy((char *)ap_wifi_config.ap.ssid, ap_cfg.ssid, sizeof(ap_wifi_config.ap.ssid) - 1);
    ap_wifi_config.ap.ssid_len = strlen(ap_cfg.ssid);
    strncpy((char *)ap_wifi_config.ap.password, ap_cfg.password, sizeof(ap_wifi_config.ap.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_wifi_config));

    // 3. 配置 STA 为“无效”网络（防止自动连接）
    wifi_config_t sta_default = {0};
    strcpy((char *)sta_default.sta.ssid, "Disable_SSID");
    sta_default.sta.password[0] = '\0';
    sta_default.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_default));

    // 4. 启动 WiFi（AP 立即生效，STA 不会自动连接）
    ESP_ERROR_CHECK(esp_wifi_start());

    // 5. 如果存在保存的 STA 配置，则立即尝试连接
    saved_wifi_config_t saved_sta;
    ret = nvs_load_wifi_config(&saved_sta);
    if (ret == ESP_OK && saved_sta.is_configured) {
        ESP_LOGI(TAG, "发现保存的WiFi配置，立即连接...");
        wifi_manager_connect_saved();
    } else {
        ESP_LOGW(TAG, "未发现保存的WiFi配置，仅开启AP模式");
    }

    ESP_LOGI(TAG, "✅ WiFi初始化完成，AP SSID: %s", ap_cfg.ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_connect_saved(void)
{
    // ===== 重置重试计数器 =====
    s_retry_count = 0;
    s_retry_failed = false;

    saved_wifi_config_t saved_cfg;
    esp_err_t ret = nvs_load_wifi_config(&saved_cfg);
    if (ret != ESP_OK || !saved_cfg.is_configured) {
        ESP_LOGW(TAG, "没有找到已保存的WiFi配置");
        return ESP_ERR_NOT_FOUND;
    }

    // 先断开当前连接（如果有）
    esp_wifi_disconnect();

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, saved_cfg.ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, saved_cfg.password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    strncpy(s_sta_ssid, saved_cfg.ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';

    esp_wifi_connect();
    ESP_LOGI(TAG, "正在连接WiFi: %s", saved_cfg.ssid);
    return ESP_OK;
}

// 保留兼容函数
esp_err_t wifi_manager_start_provisioning(void)
{
    ESP_LOGW(TAG, "配网模式已废弃，AP始终开启，请连接热点后访问 192.168.4.1");
    return ESP_OK;
}

esp_err_t wifi_manager_stop_provisioning(void)
{
    ESP_LOGW(TAG, "无需停止配网");
    return ESP_OK;
}

int wifi_manager_is_connected(void)
{
    return s_is_connected;
}

const char* wifi_manager_get_ip_str(void)
{
    return s_ip_str;
}

const char* wifi_manager_get_sta_ssid(void)
{
    return s_sta_ssid;
}

bool wifi_manager_is_ap_connected(void)
{
    return s_ap_sta_connected;
}

bool wifi_manager_is_sta_retry_failed(void)
{
    return s_retry_failed;
}

void wifi_manager_reset_retry_counter(void)
{
    s_retry_count = 0;
    s_retry_failed = false;
    if (!s_is_connected) {
        esp_wifi_connect();
    }
}
