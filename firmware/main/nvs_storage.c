#include "nvs_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS";
#define NVS_NAMESPACE "doorbell"

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS初始化完成");
    return ESP_OK;
}

esp_err_t nvs_load_wifi_config(saved_wifi_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(saved_wifi_config_t));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS打开失败");
        return ret;
    }

    size_t len = sizeof(config->ssid);
    ret = nvs_get_str(handle, "ssid", config->ssid, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        config->is_configured = 0;
        nvs_close(handle);
        return ESP_OK;
    }
    ESP_ERROR_CHECK(ret);

    len = sizeof(config->password);
    ret = nvs_get_str(handle, "password", config->password, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        config->is_configured = 0;
        nvs_close(handle);
        return ESP_OK;
    }
    ESP_ERROR_CHECK(ret);

    config->is_configured = 1;
    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi配置已加载: SSID=%s", config->ssid);
    return ESP_OK;
}

esp_err_t nvs_save_wifi_config(const saved_wifi_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));

    ESP_ERROR_CHECK(nvs_set_str(handle, "ssid", config->ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "password", config->password));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi配置已保存: SSID=%s", config->ssid);
    return ESP_OK;
}

esp_err_t nvs_clear_wifi_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ESP_ERROR_CHECK(nvs_erase_key(handle, "ssid"));
    ESP_ERROR_CHECK(nvs_erase_key(handle, "password"));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi配置已清除");
    return ESP_OK;
}

esp_err_t nvs_load_ap_config(ap_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(ap_config_t));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS打开失败");
        return ret;
    }

    size_t len = sizeof(config->ssid);
    ret = nvs_get_str(handle, "ap_ssid", config->ssid, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        config->is_configured = 0;
        nvs_close(handle);
        return ESP_OK;
    }
    ESP_ERROR_CHECK(ret);

    len = sizeof(config->password);
    ret = nvs_get_str(handle, "ap_password", config->password, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        config->is_configured = 0;
        nvs_close(handle);
        return ESP_OK;
    }
    ESP_ERROR_CHECK(ret);

    config->is_configured = 1;
    nvs_close(handle);
    ESP_LOGI(TAG, "AP配置已加载: SSID=%s", config->ssid);
    return ESP_OK;
}

esp_err_t nvs_save_ap_config(const ap_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));

    ESP_ERROR_CHECK(nvs_set_str(handle, "ap_ssid", config->ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "ap_password", config->password));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "AP配置已保存: SSID=%s", config->ssid);
    return ESP_OK;
}

esp_err_t nvs_clear_ap_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ESP_ERROR_CHECK(nvs_erase_key(handle, "ap_ssid"));
    ESP_ERROR_CHECK(nvs_erase_key(handle, "ap_password"));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "AP配置已清除");
    return ESP_OK;
}