set(IDF_COMPONENT_MANAGER OFF)

set(EXTRA_COMPONENT_DIRS
    components/esp-dl
    components/who_detect
    components/who_frame_cap
    components/who_task
    components/esp_jpeg
)

set(SUPPORTED_TARGETS esp32s3)
set(PARTITION_TABLE_SOURCE "partitions.csv")
set(PARTITION_TABLE_OFFSET 0x8000)

idf_component_register(SRCS
    "main.cpp"
    "gpio_control.c"
    "nvs_storage.c"
    "wifi_manager.c"
    "web_server.c"
    "camera_driver.c"
    "cloud_upload.c"
    "pwm_control.c"
    "ai_detection.c"
    "device_id.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp-dl
        who_detect
        who_frame_cap
        who_task
        driver
        nvs_flash
        esp_wifi
        esp_http_client
        mbedtls
        esp_mm
        esp32-camera
        esp_new_jpeg
)