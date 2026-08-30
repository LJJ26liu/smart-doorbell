#include "cloud_upload.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "device_id.h"

static const char *TAG = "云存储";

static const char* get_device_id_str(void)
{
    return device_id_get();
}

static char* oss_signature(const char *method, const char *resource, const char *date, const char *content_type, const char *content_md5)
{
    char string_to_sign[512];
    snprintf(string_to_sign, sizeof(string_to_sign),
             "%s\n%s\n%s\n%s\n%s",
             method,
             content_md5 ? content_md5 : "",
             content_type ? content_type : "",
             date,
             resource);
    ESP_LOGD(TAG, "待签名字符串:\n%s", string_to_sign);

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)OSS_ACCESS_SECRET, strlen(OSS_ACCESS_SECRET));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)string_to_sign, strlen(string_to_sign));
    unsigned char hmac_out[20];
    mbedtls_md_hmac_finish(&ctx, hmac_out);
    mbedtls_md_free(&ctx);

    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, hmac_out, 20);
    char *b64 = malloc(b64_len + 1);
    if (!b64) return NULL;
    mbedtls_base64_encode((unsigned char*)b64, b64_len, &b64_len, hmac_out, 20);
    b64[b64_len] = '\0';
    return b64;
}

esp_err_t cloud_upload_jpeg(const char *filename, const uint8_t *data, size_t len, const char *type)
{
    if (!filename || !data || len == 0 || !type) {
        ESP_LOGE(TAG, "参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    const char *dev_id = get_device_id_str();
    if (!dev_id || strlen(dev_id) == 0) {
        ESP_LOGE(TAG, "设备ID不可用");
        return ESP_FAIL;
    }

    char resource[256];
    snprintf(resource, sizeof(resource), "/%s/%s/%s", OSS_BUCKET, dev_id, filename);

    time_t now = time(NULL);
    struct tm tm_gmt;
    gmtime_r(&now, &tm_gmt);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", &tm_gmt);

    char *signature = oss_signature("PUT", resource, date_str, "image/jpeg", NULL);
    if (!signature) {
        ESP_LOGE(TAG, "签名生成失败");
        return ESP_FAIL;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "OSS %s:%s", OSS_ACCESS_KEY, signature);
    free(signature);

    char url[512];
    snprintf(url, sizeof(url), "http://%s.%s/%s/%s", OSS_BUCKET, OSS_ENDPOINT, dev_id, filename);

    ESP_LOGI(TAG, "正在上传到 %s，大小 %d 字节", url, len);
    ESP_LOGD(TAG, "日期: %s", date_str);
    ESP_LOGD(TAG, "授权: %s", auth_header);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 10000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP客户端初始化失败");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Date", date_str);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_http_client_set_post_field(client, (const char*)data, len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            char *etag = NULL;
            esp_http_client_get_header(client, "ETag", &etag);
            ESP_LOGI(TAG, "✅ 上传成功！ETag: %s", etag ? etag : "无");
            update_photos_index(filename, type);
        } else {
            ESP_LOGE(TAG, "上传失败，状态码: %d", status);
            int content_len = esp_http_client_get_content_length(client);
            if (content_len > 0) {
                char *response = malloc(content_len + 1);
                if (response) {
                    int read_len = esp_http_client_read_response(client, response, content_len);
                    if (read_len > 0) {
                        response[read_len] = '\0';
                        ESP_LOGE(TAG, "响应: %s", response);
                    }
                    free(response);
                }
            } else {
                char *response = malloc(256);
                if (response) {
                    int read_len = esp_http_client_read_response(client, response, 255);
                    if (read_len > 0) {
                        response[read_len] = '\0';
                        ESP_LOGE(TAG, "响应(部分): %s", response);
                    }
                    free(response);
                }
            }
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP执行错误: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t update_photos_index(const char *filename, const char *type)
{
    if (!filename || !type) return ESP_ERR_INVALID_ARG;

    const char *dev_id = get_device_id_str();
    if (!dev_id || strlen(dev_id) == 0) {
        ESP_LOGE(TAG, "设备ID不可用");
        return ESP_FAIL;
    }

    char json_path[256];
    snprintf(json_path, sizeof(json_path), "%s/%s.json", dev_id, dev_id);

    char json_url[512];
    snprintf(json_url, sizeof(json_url), "http://%s.%s/%s", OSS_BUCKET, OSS_ENDPOINT, json_path);

    char resource[512];
    snprintf(resource, sizeof(resource), "/%s/%s", OSS_BUCKET, json_path);

    time_t now = time(NULL);
    struct tm tm_gmt;
    gmtime_r(&now, &tm_gmt);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", &tm_gmt);

    char *sig_get = oss_signature("GET", resource, date_str, NULL, NULL);
    if (!sig_get) {
        ESP_LOGE(TAG, "GET签名生成失败");
        return ESP_FAIL;
    }
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "OSS %s:%s", OSS_ACCESS_KEY, sig_get);
    free(sig_get);

    esp_http_client_config_t get_config = {
        .url = json_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .buffer_size = 2048,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&get_config);
    if (!client) return ESP_FAIL;
    esp_http_client_set_header(client, "Date", date_str);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开HTTP连接失败: %d", err);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GET %s 状态=%d, 内容长度=%d", json_path, status, content_length);

    char *existing_data = NULL;
    int data_len = 0;

    if (status == 200 && content_length > 0) {
        existing_data = malloc(content_length + 1);
        if (existing_data) {
            int total_read = 0;
            int read_len;
            while (total_read < content_length) {
                read_len = esp_http_client_read(client, existing_data + total_read, content_length - total_read);
                if (read_len < 0) {
                    ESP_LOGE(TAG, "读取错误: %d", read_len);
                    free(existing_data);
                    existing_data = NULL;
                    break;
                } else if (read_len == 0) {
                    break;
                }
                total_read += read_len;
            }
            if (existing_data) {
                data_len = total_read;
                existing_data[data_len] = '\0';
                ESP_LOGI(TAG, "从响应中读取了 %d 字节", data_len);
            }
        }
    } else if (status == 404) {
        ESP_LOGI(TAG, "%s 未找到 (404)，将创建新文件", json_path);
    } else {
        ESP_LOGE(TAG, "获取 %s 失败，状态=%d", json_path, status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (data_len == 0 && status != 404) {
        ESP_LOGW(TAG, "读取了0字节但状态=200，视为空");
    }

    long long timestamp_ms = (long long)(time(NULL) * 1000);
    char new_entry[512];
    snprintf(new_entry, sizeof(new_entry),
            "{\"filename\":\"%s\",\"type\":\"%s\",\"timestamp\":%lld,\"deviceId\":\"%s\"}",
            filename, type, timestamp_ms, dev_id);

    char *final_json = NULL;
    if (existing_data && data_len > 0) {
        if (existing_data[0] == '[' && existing_data[data_len - 1] == ']') {
            existing_data[data_len - 1] = '\0';
            int new_len = data_len + strlen(new_entry) + 4;
            final_json = malloc(new_len);
            if (final_json) {
                sprintf(final_json, "%s,%s]", existing_data, new_entry);
            }
        } else {
            ESP_LOGW(TAG, "现有JSON格式无效，覆盖");
            final_json = malloc(strlen(new_entry) + 4);
            if (final_json) sprintf(final_json, "[%s]", new_entry);
        }
    } else {
        final_json = malloc(strlen(new_entry) + 4);
        if (final_json) sprintf(final_json, "[%s]", new_entry);
    }
    if (existing_data) free(existing_data);
    if (!final_json) {
        ESP_LOGE(TAG, "JSON内存不足");
        return ESP_ERR_NO_MEM;
    }

    time_t now2 = time(NULL);
    gmtime_r(&now2, &tm_gmt);
    char date_str2[64];
    strftime(date_str2, sizeof(date_str2), "%a, %d %b %Y %H:%M:%S GMT", &tm_gmt);

    char *sig_put = oss_signature("PUT", resource, date_str2, "application/json", NULL);
    if (!sig_put) {
        free(final_json);
        return ESP_FAIL;
    }
    char auth_header2[256];
    snprintf(auth_header2, sizeof(auth_header2), "OSS %s:%s", OSS_ACCESS_KEY, sig_put);
    free(sig_put);

    esp_http_client_config_t put_config = {
        .url = json_url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 10000,
    };
    client = esp_http_client_init(&put_config);
    if (!client) {
        free(final_json);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Date", date_str2);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header2);
    esp_http_client_set_post_field(client, final_json, strlen(final_json));

    err = esp_http_client_perform(client);
    int put_status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && put_status == 200) {
        ESP_LOGI(TAG, "✅ %s 更新成功", json_path);
    } else {
        ESP_LOGE(TAG, "更新 %s 失败，状态=%d", json_path, put_status);
        err = ESP_FAIL;
    }
    esp_http_client_cleanup(client);
    free(final_json);
    return err;
}