#include "camera_driver.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "driver/i2c.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "CAMERA";

// ===== 模拟JPEG数据（1x1灰色方块）=====
static const uint8_t FAKE_JPEG[] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
    0x01, 0x01, 0x00, 0x48, 0x00, 0x48, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
    0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09,
    0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12,
    0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20,
    0x24, 0x2E, 0x27, 0x20, 0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29,
    0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32,
    0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00,
    0x00, 0x3F, 0x00, 0x37, 0xFF, 0xD9
};
#define FAKE_JPEG_SIZE sizeof(FAKE_JPEG)

static camera_config_t camera_config = {
    .pin_pwdn  = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,

    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_SVGA,
    .jpeg_quality = 8,
    .fb_count = 4,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
};

static bool s_use_real_camera = false;

esp_err_t camera_init(void)
{
    ESP_LOGI(TAG, "正在初始化摄像头...");

    esp_err_t err = esp_camera_init(&camera_config);
    if (err == ESP_OK) {
        s_use_real_camera = true;
        ESP_LOGI(TAG, "✅ 真实摄像头初始化成功！");
        ESP_LOGI(TAG, "   分辨率: QVGA (320x240)");
        ESP_LOGI(TAG, "   格式: JPEG");
        return ESP_OK;
    }

    s_use_real_camera = false;
    ESP_LOGW(TAG, "⚠️ 真实摄像头不可用（错误码: 0x%x），切换到模拟模式", err);
    ESP_LOGW(TAG, "   请检查接线或供电，模拟模式下将生成测试图片");
    return ESP_OK;
}

esp_err_t camera_capture(camera_photo_t *photo)
{
    if (!photo) return ESP_ERR_INVALID_ARG;

    if (s_use_real_camera) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "获取图像帧失败！切换到模拟模式");
            s_use_real_camera = false;
        } else {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            if (tm_info) {
                strftime(photo->filename, sizeof(photo->filename),
                         "%Y-%m-%d_%H-%M-%S.jpg", tm_info);
            } else {
                strcpy(photo->filename, "capture.jpg");
            }
            photo->len = fb->len;
            photo->data = (uint8_t *)malloc(photo->len);
            if (!photo->data) {
                ESP_LOGE(TAG, "内存分配失败");
                esp_camera_fb_return(fb);
                return ESP_ERR_NO_MEM;
            }
            memcpy(photo->data, fb->buf, photo->len);
            esp_camera_fb_return(fb);
            ESP_LOGI(TAG, "📷 真实拍照成功: %s (%d 字节)", photo->filename, photo->len);
            return ESP_OK;
        }
    }

    // 模拟模式
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(photo->filename, sizeof(photo->filename),
                 "%Y-%m-%d_%H-%M-%S.jpg", tm_info);
    } else {
        strcpy(photo->filename, "capture.jpg");
    }
    photo->len = FAKE_JPEG_SIZE;
    photo->data = (uint8_t *)malloc(photo->len);
    if (!photo->data) {
        ESP_LOGE(TAG, "模拟内存分配失败");
        return ESP_ERR_NO_MEM;
    }
    memcpy(photo->data, FAKE_JPEG, photo->len);
    ESP_LOGI(TAG, "📷 模拟拍照成功: %s (%d 字节)", photo->filename, photo->len);
    return ESP_OK;
}

void camera_free_photo(camera_photo_t *photo)
{
    if (photo && photo->data) {
        free(photo->data);
        photo->data = NULL;
        photo->len = 0;
    }
}

int camera_is_ready(void)
{
    return s_use_real_camera;
}