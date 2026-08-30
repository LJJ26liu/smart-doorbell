#include "device_id.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "设备ID";
static char s_device_id[DEVICE_ID_LEN] = {0};

static void mac_to_hex_string(const uint8_t *mac, char *hex_str, size_t len)
{
    snprintf(hex_str, len, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t device_id_init(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("doorbell", NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        size_t len = DEVICE_ID_LEN;
        ret = nvs_get_str(handle, "device_id", s_device_id, &len);
        nvs_close(handle);
        if (ret == ESP_OK && strlen(s_device_id) > 0) {
            ESP_LOGI(TAG, "从NVS加载设备ID: %s", s_device_id);
            return ESP_OK;
        }
    }

    uint8_t mac[6];
    ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取MAC地址失败");
        return ret;
    }

    mac_to_hex_string(mac, s_device_id, DEVICE_ID_LEN);
    ESP_LOGI(TAG, "根据MAC生成设备ID: %s", s_device_id);

    ret = nvs_open("doorbell", NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_set_str(handle, "device_id", s_device_id);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "设备ID已保存到NVS");
    }

    return ESP_OK;
}

const char* device_id_get(void)
{
    return s_device_id;
}