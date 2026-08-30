#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "driver/gpio.h"

// ===== AI 推理相关 =====
#include "pedestrian_detect.hpp"
#include "dl_image.hpp"
#include "jpeg_decoder.h"
#include "esp_jpeg_enc.h"
#include "esp_jpeg_common.h"
#include <algorithm>

extern "C" {
#include "camera_driver.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "nvs_storage.h"
#include "gpio_control.h"
#include "cloud_upload.h"
#include "pwm_control.h"
#include "ai_detection.h"
#include "device_id.h"
}

static const char *TAG = "MAIN";

#define PIR_GPIO        GPIO_PIR_INPUT
#define KEY_GPIO        GPIO_BUTTON_EXT

static bool wifi_connected = false;
static char current_time_str[20] = "未知时间";
static bool pir_triggered = false;

static uint8_t *g_latest_jpeg = NULL;
static size_t g_latest_jpeg_len = 0;
static SemaphoreHandle_t g_photo_mutex = NULL;

static pedestrian_detect::Pico *g_detector = nullptr;

// ===== 解码 JPEG 为 RGB888 =====
static uint8_t* decode_jpeg_to_rgb(const uint8_t *jpeg_data, size_t jpeg_len, int &out_width, int &out_height) {
    if (!jpeg_data || jpeg_len == 0) return NULL;

    esp_jpeg_image_cfg_t info_cfg = {
        .indata = (uint8_t *)jpeg_data,
        .indata_size = jpeg_len,
        .outbuf = NULL,
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB888,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {0}
    };
    esp_jpeg_image_output_t info = {0};
    esp_err_t err = esp_jpeg_get_image_info(&info_cfg, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "获取JPEG信息失败: %d", err);
        return NULL;
    }

    out_width = info.width;
    out_height = info.height;
    size_t rgb_size = out_width * out_height * 3;

    uint8_t *rgb_data = (uint8_t *)malloc(rgb_size);
    if (!rgb_data) {
        ESP_LOGE(TAG, "分配RGB缓冲区内存失败");
        return NULL;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpeg_data,
        .indata_size = jpeg_len,
        .outbuf = rgb_data,
        .outbuf_size = rgb_size,
        .out_format = JPEG_IMAGE_FORMAT_RGB888,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {0}
    };

    esp_jpeg_image_output_t out = {0};
    err = esp_jpeg_decode(&cfg, &out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG解码失败: %d", err);
        free(rgb_data);
        return NULL;
    }

    ESP_LOGI(TAG, "JPEG解码完成: %dx%d RGB, 输出长度=%d", out_width, out_height, out.output_len);
    return rgb_data;
}

// ===== 在 RGB 图像上绘制矩形框 =====
static void draw_rectangle_on_rgb(uint8_t *rgb_data, int width, int height,
                                   int x1, int y1, int x2, int y2,
                                   uint8_t r, uint8_t g, uint8_t b, int thickness = 2) {
    x1 = std::max(0, std::min(x1, width - 1));
    y1 = std::max(0, std::min(y1, height - 1));
    x2 = std::max(0, std::min(x2, width - 1));
    y2 = std::max(0, std::min(y2, height - 1));

    for (int t = 0; t < thickness; t++) {
        for (int x = x1; x <= x2; x++) {
            int py = y1 + t;
            if (py < height) {
                int idx = (py * width + x) * 3;
                rgb_data[idx] = r;
                rgb_data[idx + 1] = g;
                rgb_data[idx + 2] = b;
            }
        }
        for (int x = x1; x <= x2; x++) {
            int py = y2 - t;
            if (py >= 0) {
                int idx = (py * width + x) * 3;
                rgb_data[idx] = r;
                rgb_data[idx + 1] = g;
                rgb_data[idx + 2] = b;
            }
        }
        for (int y = y1; y <= y2; y++) {
            int px = x1 + t;
            if (px < width) {
                int idx = (y * width + px) * 3;
                rgb_data[idx] = r;
                rgb_data[idx + 1] = g;
                rgb_data[idx + 2] = b;
            }
        }
        for (int y = y1; y <= y2; y++) {
            int px = x2 - t;
            if (px >= 0) {
                int idx = (y * width + px) * 3;
                rgb_data[idx] = r;
                rgb_data[idx + 1] = g;
                rgb_data[idx + 2] = b;
            }
        }
    }
}

// ===== 将 RGB888 编码为 JPEG =====
static uint8_t* encode_rgb_to_jpeg(const uint8_t *rgb_data, int width, int height,
                                   size_t *out_len, int quality = 80) {
    jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
    config.width = width;
    config.height = height;
    config.src_type = JPEG_PIXEL_FORMAT_RGB888;
    config.subsampling = JPEG_SUBSAMPLE_420;
    config.quality = quality;
    config.task_enable = false;

    jpeg_enc_handle_t handle = NULL;
    jpeg_error_t err = jpeg_enc_open(&config, &handle);
    if (err != JPEG_ERR_OK || handle == NULL) {
        ESP_LOGE(TAG, "打开JPEG编码器失败: %d", err);
        return NULL;
    }

    size_t max_out_size = width * height * 2 + 1024;
    uint8_t *out_buf = (uint8_t*)malloc(max_out_size);
    if (!out_buf) {
        ESP_LOGE(TAG, "分配JPEG输出缓冲区失败");
        jpeg_enc_close(handle);
        return NULL;
    }

    int out_size = 0;
    err = jpeg_enc_process(handle, rgb_data, width * height * 3,
                           out_buf, max_out_size, &out_size);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG编码处理失败: %d", err);
        free(out_buf);
        jpeg_enc_close(handle);
        return NULL;
    }

    jpeg_enc_close(handle);

    uint8_t *result = (uint8_t*)realloc(out_buf, out_size);
    if (!result) {
        free(out_buf);
        return NULL;
    }
    *out_len = out_size;
    ESP_LOGD(TAG, "JPEG编码完成: %d 字节 (质量=%d)", out_size, quality);
    return result;
}

// ===== 获取最新照片 =====
bool get_latest_photo(uint8_t **out_data, size_t *out_len) {
    if (!g_photo_mutex) return false;
    xSemaphoreTake(g_photo_mutex, portMAX_DELAY);
    if (g_latest_jpeg && g_latest_jpeg_len > 0) {
        *out_data = g_latest_jpeg;
        *out_len = g_latest_jpeg_len;
        xSemaphoreGive(g_photo_mutex);
        return true;
    }
    xSemaphoreGive(g_photo_mutex);
    return false;
}

static void update_current_time(void) {
    if (wifi_connected) {
        time_t now = time(NULL);
        if (now > 1000000000) {
            struct tm *tm_info = localtime(&now);
            strftime(current_time_str, sizeof(current_time_str), "%H:%M:%S", tm_info);
        } else {
            strcpy(current_time_str, "未同步");
        }
    } else {
        strcpy(current_time_str, "离线");
    }
}

// ===== 按键任务 =====
static void key_task(void *pvParameters) {
    int last_state = 1;
    while (1) {
        int current = gpio_get_level(KEY_GPIO);
        if (current == 0 && last_state == 1) {
            ESP_LOGI(TAG, "🔔 门铃按下！播放旋律...");
            pwm_buzzer_play_melody();
        }
        last_state = current;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ===== 判断检测框是否符合人形特征 =====
static bool is_valid_person_box(const dl::detect::result_t &r, int img_w, int img_h) {
    int x1 = r.box[0], y1 = r.box[1];
    int x2 = r.box[2], y2 = r.box[3];
    int w = x2 - x1;
    int h = y2 - y1;
    if (w <= 0 || h <= 0) return false;

    float aspect = (float)w / h;
    float area_ratio = (float)(w * h) / (img_w * img_h);

    if (aspect < 0.25 || aspect > 0.9) {
        ESP_LOGD(TAG, "宽高比过滤: %.2f", aspect);
        return false;
    }
    if (area_ratio < 0.005 || area_ratio > 0.80) {
        ESP_LOGD(TAG, "面积占比过滤: %.3f", area_ratio);
        return false;
    }
    if (y2 < img_h * 0.25) {
        ESP_LOGD(TAG, "位置过滤: y2=%d < %.0f", y2, img_h * 0.25);
        return false;
    }
    return true;
}

// ===== PIR 检测任务 =====
static void pir_detect_task(void *pvParameters) {
    TickType_t last_trigger_time = 0;
    const TickType_t cooldown = pdMS_TO_TICKS(12000);
    int samples[10];
    int valid_high = 0;
    int last_stable_level = 0;

    ESP_LOGI(TAG, "PIR检测任务已启动 (使用esp-dl行人检测)");

    while (1) {
        // ---- 1. 读取PIR状态（防抖） ----
        for (int i = 0; i < 10; i++) {
            samples[i] = gpio_get_level(PIR_GPIO);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        valid_high = 0;
        for (int i = 0; i < 10; i++) {
            if (samples[i] == 1) valid_high++;
        }
        int current_stable = (valid_high >= 8) ? 1 : 0;

        TickType_t now = xTaskGetTickCount();

        // ---- 2. 冷却检查 ----
        if ((now - last_trigger_time) < cooldown) {
            last_stable_level = current_stable;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ---- 3. 上升沿触发 ----
        if (current_stable == 1 && last_stable_level == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (gpio_get_level(PIR_GPIO) == 1) {
                last_trigger_time = xTaskGetTickCount();

                // ---- 等待时间同步 ----
                int wait_cnt = 0;
                const int max_wait = 30;
                while (wait_cnt < max_wait) {
                    time_t now_sec = time(NULL);
                    if (now_sec > 1000000000) {
                        break;
                    }
                    ESP_LOGI(TAG, "⏳ 等待时间同步... (%d/%d)", wait_cnt+1, max_wait);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    wait_cnt++;
                }
                if (wait_cnt >= max_wait) {
                    ESP_LOGW(TAG, "⚠️ 时间同步超时，使用当前时间");
                }

                update_current_time();
                ESP_LOGI(TAG, "🔴 PIR触发！ (时间: %s)", current_time_str);

                // ---------- 阶段1：立即抓拍并上传 (标记为 "pass") ----------
                camera_photo_t photo1;
                if (camera_capture(&photo1) == ESP_OK) {
                    bool person_detected = false;
                    int width = 0, height = 0;
                    uint8_t *rgb_data = decode_jpeg_to_rgb(photo1.data, photo1.len, width, height);

                    if (rgb_data && g_detector) {
                        dl::image::img_t img = {
                            .data = rgb_data,
                            .width = (uint16_t)width,
                            .height = (uint16_t)height,
                            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888
                        };
                        auto results = g_detector->run(img);

                        for (auto &r : results) {
                            if (is_valid_person_box(r, width, height)) {
                                person_detected = true;
                                draw_rectangle_on_rgb(rgb_data, width, height,
                                                      r.box[0], r.box[1], r.box[2], r.box[3],
                                                      255, 0, 0, 2);
                                ESP_LOGI(TAG, "检测框: x=%d, y=%d, w=%d, h=%d, 置信度=%.3f",
                                         r.box[0], r.box[1],
                                         r.box[2] - r.box[0], r.box[3] - r.box[1],
                                         r.score);
                                break;
                            }
                        }

                        if (person_detected) {
                            size_t new_len = 0;
                            uint8_t *new_data = encode_rgb_to_jpeg(rgb_data, width, height, &new_len, 60);
                            if (new_data && new_len > 0) {
                                free(photo1.data);
                                photo1.data = new_data;
                                photo1.len = new_len;
                            } else {
                                ESP_LOGW(TAG, "重新编码失败，使用原始图片");
                            }

                            esp_err_t err = cloud_upload_jpeg(photo1.filename, photo1.data, photo1.len, "pass");
                            if (err == ESP_OK) {
                                ESP_LOGI(TAG, "✅ 第一张照片上传成功并更新索引 (pass)");
                            } else {
                                ESP_LOGE(TAG, "❌ 第一张上传失败");
                            }

                            xSemaphoreTake(g_photo_mutex, portMAX_DELAY);
                            if (g_latest_jpeg) free(g_latest_jpeg);
                            g_latest_jpeg = photo1.data;
                            g_latest_jpeg_len = photo1.len;
                            xSemaphoreGive(g_photo_mutex);

                            // ---------- 阶段2：等待5秒后二次确认 ----------
                            ESP_LOGI(TAG, "⏳ 等待5秒进行二次确认...");
                            vTaskDelay(pdMS_TO_TICKS(5000));

                            camera_photo_t photo2;
                            if (camera_capture(&photo2) == ESP_OK) {
                                bool still_person = false;
                                int w2 = 0, h2 = 0;
                                uint8_t *rgb2 = decode_jpeg_to_rgb(photo2.data, photo2.len, w2, h2);

                                if (rgb2 && g_detector) {
                                    dl::image::img_t img2 = {
                                        .data = rgb2,
                                        .width = (uint16_t)w2,
                                        .height = (uint16_t)h2,
                                        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888
                                    };
                                    auto results2 = g_detector->run(img2);

                                    for (auto &r : results2) {
                                        if (is_valid_person_box(r, w2, h2)) {
                                            still_person = true;
                                            draw_rectangle_on_rgb(rgb2, w2, h2,
                                                                  r.box[0], r.box[1], r.box[2], r.box[3],
                                                                  255, 0, 0, 2);
                                            ESP_LOGI(TAG, "确认框: x=%d, y=%d, w=%d, h=%d",
                                                     r.box[0], r.box[1],
                                                     r.box[2] - r.box[0], r.box[3] - r.box[1]);
                                            break;
                                        }
                                    }

                                    if (still_person) {
                                        size_t new_len2 = 0;
                                        uint8_t *new_data2 = encode_rgb_to_jpeg(rgb2, w2, h2, &new_len2, 60);
                                        if (new_data2 && new_len2 > 0) {
                                            free(photo2.data);
                                            photo2.data = new_data2;
                                            photo2.len = new_len2;
                                        }

                                        esp_err_t err2 = cloud_upload_jpeg(photo2.filename, photo2.data, photo2.len, "stay");
                                        if (err2 == ESP_OK) {
                                            ESP_LOGW(TAG, "⚠️ 5秒后人员仍然存在！上传为STAY并更新索引。");
                                            pir_triggered = true;
                                        } else {
                                            ESP_LOGE(TAG, "❌ 停留照片上传失败");
                                        }
                                        camera_free_photo(&photo2);
                                    } else {
                                        ESP_LOGI(TAG, "人员在5秒内离开，仅路过。");
                                        camera_free_photo(&photo2);
                                    }
                                    free(rgb2);
                                } else {
                                    if (rgb2) free(rgb2);
                                    ESP_LOGE(TAG, "第二次解码或检测器失败，丢弃");
                                    camera_free_photo(&photo2);
                                }
                            } else {
                                ESP_LOGE(TAG, "第二次抓拍失败");
                            }

                            photo1.data = NULL;
                            photo1.len = 0;
                        } else {
                            ESP_LOGI(TAG, "第一次抓拍未检测到人，丢弃");
                            camera_free_photo(&photo1);
                        }
                        free(rgb_data);
                    } else {
                        if (rgb_data) free(rgb_data);
                        ESP_LOGE(TAG, "第一次解码或检测器失败");
                        camera_free_photo(&photo1);
                    }
                } else {
                    ESP_LOGE(TAG, "❌ 第一次摄像头抓拍失败");
                }
            }
        }

        last_stable_level = current_stable;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ===== 主函数 =====
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "智能门铃系统启动中...");
    ESP_LOGI(TAG, "==========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS初始化完成");

    ESP_ERROR_CHECK(device_id_init());
    ESP_LOGI(TAG, "✅ 设备ID初始化完成");

    gpio_control_init();
    ESP_LOGI(TAG, "✅ GPIO初始化完成");

    pwm_control_init();
    ESP_LOGI(TAG, "✅ 蜂鸣器初始化完成");

    wifi_manager_init();
    ESP_LOGI(TAG, "✅ WiFi管理器初始化完成");

    bool sta_connected = false;
    ret = wifi_manager_connect_saved();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "⏳ 正在连接已保存的WiFi...");
        int timeout = 100;
        while (timeout-- && !wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (wifi_manager_is_connected()) {
            sta_connected = true;
            wifi_connected = true;
            ESP_LOGI(TAG, "✅ STA已连接，IP: %s", wifi_manager_get_ip_str());

            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            setenv("TZ", "CST-8", 1);
            tzset();

            int wait_cnt = 0;
            while (wait_cnt < 100) {
                time_t now = time(NULL);
                if (now > 1000000000) {
                    ESP_LOGI(TAG, "✅ SNTP时间同步成功: %s", ctime(&now));
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                wait_cnt++;
            }
            if (wait_cnt >= 100) {
                ESP_LOGW(TAG, "⚠️ SNTP同步超时，时间可能不准确");
            }
        } else {
            ESP_LOGW(TAG, "⚠️ STA连接超时");
        }
    } else {
        ESP_LOGW(TAG, "⚠️ 未找到已保存的WiFi配置");
    }

    if (!sta_connected) {
        esp_err_t ap_ret = wifi_manager_start_provisioning();
        if (ap_ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ AP模式已启动");
        } else {
            ESP_LOGE(TAG, "❌ AP模式启动失败");
        }
    }

    if (camera_init() == ESP_OK) {
        ESP_LOGI(TAG, "✅ 摄像头初始化完成");
    } else {
        ESP_LOGW(TAG, "⚠️ 摄像头初始化失败，进入模拟模式");
    }

    // ---- 初始化行人检测模型 ----
    ESP_LOGI(TAG, "正在加载行人检测模型...");
    g_detector = new pedestrian_detect::Pico("pedestrian_detect_pico_s8_v1.espdl", 0.5, 0.3);
    if (g_detector) {
        ESP_LOGI(TAG, "✅ 行人检测器创建成功 (阈值=0.3)");
    } else {
        ESP_LOGE(TAG, "❌ 创建检测器失败");
    }

    g_photo_mutex = xSemaphoreCreateMutex();

    xTaskCreate(key_task, "key_task", 4096, NULL, 7, NULL);
    xTaskCreate(pir_detect_task, "pir_detect", 8192, NULL, 6, NULL);

    web_server_init();

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "✅ 所有系统就绪。");
    ESP_LOGI(TAG, "==========================================");
}