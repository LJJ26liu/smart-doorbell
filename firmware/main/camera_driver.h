#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// ===== 摄像头引脚配置（ESP32-S3-DevKitC-1 + OV2640 实际接线）=====
#define CAM_PIN_PWDN   17
#define CAM_PIN_RESET  16
#define CAM_PIN_XCLK   21 
#define CAM_PIN_SIOD   15          // SCCB SDA
#define CAM_PIN_SIOC   14          // SCCB SCL

#define CAM_PIN_D7     10
#define CAM_PIN_D6     9
#define CAM_PIN_D5     8
#define CAM_PIN_D4     7
#define CAM_PIN_D3     6
#define CAM_PIN_D2     5
#define CAM_PIN_D1     4
#define CAM_PIN_D0     3

#define CAM_PIN_VSYNC  11
#define CAM_PIN_HREF   12
#define CAM_PIN_PCLK   13

typedef struct {
    uint8_t *data;
    size_t len;
    char filename[64];
} camera_photo_t;

esp_err_t camera_init(void);
esp_err_t camera_capture(camera_photo_t *photo);
void camera_free_photo(camera_photo_t *photo);
int camera_is_ready(void);

#endif
