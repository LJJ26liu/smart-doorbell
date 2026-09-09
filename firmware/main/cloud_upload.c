#include "cloud_upload.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <string.h>
#include <stdlib.h>
#include "device_id.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

static const char *TAG = "云存储";

#define FC_UPLOAD_URL "http://doorbell-api-jclvfiemao.cn-shenzhen.fcapp.run/upload"

static char* base64_encode(const uint8_t *data, size_t len) {
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, data, len);
    char *b64_str = malloc(b64_len + 1);
    if (!b64_str) return NULL;
    mbedtls_base64_encode((unsigned char*)b64_str, b64_len, &b64_len, data, len);
    b64_str[b64_len] = '\0';
    return b64_str;
}

esp_err_t cloud_upload_jpeg(const char *filename, const uint8_t *data, size_t len, const char *type)
{
    if (!data || len == 0) {
        ESP_LOGE(TAG, "图片数据无效");
        return ESP_ERR_INVALID_ARG;
    }

    const char *dev_id = device_id_get();
    if (!dev_id || strlen(dev_id) == 0) {
        ESP_LOGE(TAG, "设备ID不可用");
        return ESP_FAIL;
    }

    // 1. Base64 编码
    char *b64 = base64_encode(data, len);
    if (!b64) {
        ESP_LOGE(TAG, "Base64 编码失败");
        return ESP_ERR_NO_MEM;
    }

    // 2. 构建 JSON
    char *json_body = NULL;
    int json_len = asprintf(&json_body,
        "{\"deviceId\":\"%s\",\"type\":\"%s\",\"image\":\"%s\"}",
        dev_id, type ? type : "pass", b64);
    free(b64);

    if (json_len < 0 || !json_body) {
        ESP_LOGE(TAG, "构建 JSON 失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "正在上传图片到 FC，原始大小 %d 字节，JSON 大小 %d 字节，类型 %s",
             len, json_len, type ? type : "pass");

    // 3. 发送请求（重试 3 次）
    int retry_count = 3;
    esp_err_t err = ESP_FAIL;

    while (retry_count > 0) {
        esp_http_client_config_t config = {
            .url = FC_UPLOAD_URL,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 15000,
            .buffer_size = 4096,
            .keep_alive_enable = false,   // 禁用 keep-alive
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "HTTP客户端初始化失败");
            free(json_body);
            return ESP_FAIL;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "X-Device-Id", dev_id);
        esp_http_client_set_header(client, "X-Photo-Type", type ? type : "pass");

        esp_http_client_set_post_field(client, json_body, strlen(json_body));

        // 执行请求
        err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP 请求失败: %s (剩余重试 %d)", esp_err_to_name(err), retry_count - 1);
            esp_http_client_cleanup(client);
            retry_count--;
            if (retry_count > 0) {
                ESP_LOGW(TAG, "上传失败，1 秒后重试...");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }

        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP 状态码: %d", status);

        // ---- 读取响应体 ----
        // 直接使用 read 循环读取
        #define READ_BUFFER_SIZE 512
        char *resp_buf = NULL;
        int total_read = 0;
        char temp_buf[READ_BUFFER_SIZE];
        int read_len;

        // 循环读取，直到 read 返回 0（数据读完）或负数（出错）
        while (1) {
            read_len = esp_http_client_read(client, temp_buf, READ_BUFFER_SIZE);
            if (read_len < 0) {
                ESP_LOGE(TAG, "读取响应时出错: %d", read_len);
                break;
            }
            if (read_len == 0) {
                // 读取完毕
                break;
            }
            // 追加到 resp_buf
            char *new_buf = realloc(resp_buf, total_read + read_len + 1);
            if (!new_buf) {
                ESP_LOGE(TAG, "内存分配失败");
                free(resp_buf);
                esp_http_client_cleanup(client);
                retry_count--;
                goto next_retry;
            }
            resp_buf = new_buf;
            memcpy(resp_buf + total_read, temp_buf, read_len);
            total_read += read_len;
        }

        if (total_read > 0) {
            resp_buf[total_read] = '\0';
            ESP_LOGI(TAG, "FC 响应: %s", resp_buf);
        } else {
            ESP_LOGW(TAG, "未能读取到响应体（read 返回 0）");
            // 尝试从内容长度获取可能的数据
            int content_len = esp_http_client_get_content_length(client);
            ESP_LOGD(TAG, "Content-Length: %d", content_len);
            if (content_len > 0) {
                // 如果已知长度，再尝试一次读取（但通常情况下上面的循环已经读了）
                char *extra_buf = malloc(content_len + 1);
                if (extra_buf) {
                    int extra_read = esp_http_client_read_response(client, extra_buf, content_len);
                    if (extra_read > 0) {
                        extra_buf[extra_read] = '\0';
                        ESP_LOGI(TAG, "FC 响应(备选): %s", extra_buf);
                        resp_buf = extra_buf;
                        total_read = extra_read;
                    } else {
                        free(extra_buf);
                    }
                }
            }
        }

        // 4. 解析响应
        bool success = false;
        char objectKey[256] = {0};

        if (resp_buf && total_read > 0) {
            char *success_ptr = strstr(resp_buf, "\"success\":");
            if (success_ptr) {
                success_ptr += 10;
                while (*success_ptr == ' ' || *success_ptr == '\t') success_ptr++;
                if (strncmp(success_ptr, "true", 4) == 0) {
                    success = true;
                } else if (strncmp(success_ptr, "false", 5) == 0) {
                    success = false;
                }
            }

            if (success) {
                char *key_start = strstr(resp_buf, "\"objectKey\":\"");
                if (key_start) {
                    key_start += 13;
                    char *key_end = strstr(key_start, "\"");
                    if (key_end) {
                        int key_len = key_end - key_start;
                        if (key_len > 0 && key_len < sizeof(objectKey) - 1) {
                            strncpy(objectKey, key_start, key_len);
                            objectKey[key_len] = '\0';
                        }
                    }
                }
            }
            free(resp_buf);
        } else {
            ESP_LOGW(TAG, "响应体为空，但状态码为 %d，尝试从头部获取信息", status);
            if (status == 200) {
                success = true;
            }
        }

        esp_http_client_cleanup(client);

        if (status == 200 && success) {
            ESP_LOGI(TAG, "✅ 上传成功，OSS 路径: %s", objectKey[0] ? objectKey : "未知");
            free(json_body);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "FC 返回状态码: %d, success=%d", status, success);
            if (resp_buf) {
                char *err_start = strstr(resp_buf, "\"error\":\"");
                if (err_start) {
                    err_start += 9;
                    char *err_end = strstr(err_start, "\"");
                    if (err_end) {
                        int err_len = err_end - err_start;
                        char err_msg[128];
                        snprintf(err_msg, sizeof(err_msg), "%.*s", err_len, err_start);
                        ESP_LOGE(TAG, "FC 错误信息: %s", err_msg);
                    }
                }
            }
            err = ESP_FAIL;
        }

next_retry:
        retry_count--;
        if (retry_count > 0) {
            ESP_LOGW(TAG, "上传失败，1 秒后重试...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    free(json_body);
    ESP_LOGE(TAG, "上传最终失败");
    return ESP_FAIL;
}
