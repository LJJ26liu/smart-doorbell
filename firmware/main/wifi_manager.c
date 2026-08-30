#include "wifi_manager.h"
#include "nvs_storage.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "string.h"
#include <stdbool.h>

static const char *TAG = "WIFI";

static int s_is_connected = 0;
static char s_ip_str[16] = {0};
static char s_sta_ssid[64] = {0};
static volatile bool s_ap_sta_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "WiFi事件 ID: %ld", event_id);

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA模式启动，正在连接WiFi...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_is_connected = 0;
                ESP_LOGW(TAG, "WiFi断开连接，正在重试...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "✅ 设备已连接到热点");
                s_ap_sta_connected = true;
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "设备已断开热点");
                s_ap_sta_connected = false;
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
        s_is_connected = 1;
        ESP_LOGI(TAG, "获取到IP地址: %s", s_ip_str);
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "正在初始化WiFi...");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

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

    ESP_LOGI(TAG, "WiFi初始化完成");
    return ESP_OK;
}

esp_err_t wifi_manager_connect_saved(void)
{
    saved_wifi_config_t saved_cfg;
    esp_err_t ret = nvs_load_wifi_config(&saved_cfg);
    if (ret != ESP_OK || !saved_cfg.is_configured) {
        ESP_LOGW(TAG, "没有找到已保存的WiFi配置");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_cfg.sta.ssid, saved_cfg.ssid, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, saved_cfg.password, sizeof(wifi_cfg.sta.password));

    strncpy(s_sta_ssid, saved_cfg.ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "正在连接已保存的WiFi: %s", saved_cfg.ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_start_provisioning(void)
{
    ESP_LOGI(TAG, "========== 强制进入配网模式 ==========");

    ESP_LOGI(TAG, "[1/6] 强制停止WiFi...");
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "[2/6] 清理所有网络接口...");
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        ESP_LOGI(TAG, "[2/6] 已删除STA接口");
    }
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_destroy(ap_netif);
        ESP_LOGI(TAG, "[2/6] 已删除旧AP接口");
    }

    ESP_LOGI(TAG, "[3/6] 重新创建AP接口...");
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "[3/6] 创建AP接口失败！");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[3/6] AP接口创建成功");

    ap_config_t ap_cfg;
    esp_err_t ret = nvs_load_ap_config(&ap_cfg);
    if (ret != ESP_OK || !ap_cfg.is_configured) {
        strcpy(ap_cfg.ssid, "Doorbell_Config");
        strcpy(ap_cfg.password, "12345678");
        ESP_LOGI(TAG, "[4/6] 使用默认热点配置 (未自定义)");
    } else {
        ESP_LOGI(TAG, "[4/6] 加载自定义热点配置: SSID=%s", ap_cfg.ssid);
    }

    ESP_LOGI(TAG, "[5/6] 配置热点参数...");
    wifi_config_t wifi_config = {
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
    strncpy((char *)wifi_config.ap.ssid, ap_cfg.ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(ap_cfg.ssid);
    strncpy((char *)wifi_config.ap.password, ap_cfg.password, sizeof(wifi_config.ap.password));

    ESP_LOGI(TAG, "[5/6] SSID: %s, 密码: %s, 信道: 6", ap_cfg.ssid, ap_cfg.password);

    ESP_LOGI(TAG, "[6/6] 设置AP模式并启动...");
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[6/6] 设置AP模式失败！错误码: %d", ret);
        return ret;
    }
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[6/6] 设置AP配置失败！错误码: %d", ret);
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[6/6] 启动热点失败！错误码: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✅ 热点已启动！");
    ESP_LOGI(TAG, "📶 SSID: %s", ap_cfg.ssid);
    ESP_LOGI(TAG, "🔑 密码: %s", ap_cfg.password);
    ESP_LOGI(TAG, "🌐 AP IP: 192.168.4.1");
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}

esp_err_t wifi_manager_stop_provisioning(void)
{
    ESP_LOGI(TAG, "正在停止配网模式...");
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_LOGI(TAG, "配网模式已停止");
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