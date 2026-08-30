#include "pwm_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PWM";

#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523

#define LOW_DUTY_RATIO  0.01

static void play_tone(int freq_hz, int duration_ms)
{
    if (freq_hz <= 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }
    int period_us = 1000000 / freq_hz;
    int low_us = (int)(period_us * LOW_DUTY_RATIO);
    int high_us = period_us - low_us;
    int cycles = (duration_ms * 1000) / period_us;
    for (int i = 0; i < cycles; i++) {
        gpio_set_level(GPIO_BUZZER_PWM, 0);
        esp_rom_delay_us(low_us);
        gpio_set_level(GPIO_BUZZER_PWM, 1);
        esp_rom_delay_us(high_us);
    }
    gpio_set_level(GPIO_BUZZER_PWM, 1);
}

void pwm_buzzer_play(int freq_hz, int duration_ms)
{
    if (freq_hz <= 0 || duration_ms <= 0) {
        pwm_buzzer_stop();
        return;
    }
    play_tone(freq_hz, duration_ms);
}

void pwm_buzzer_stop(void)
{
    gpio_set_level(GPIO_BUZZER_PWM, 1);
}

static const struct {
    int freq;
    int duration;
} melody[] = {
    { NOTE_C4, 300 }, { NOTE_C4, 300 }, { NOTE_G4, 300 }, { NOTE_G4, 300 },
    { NOTE_A4, 300 }, { NOTE_A4, 300 }, { NOTE_G4, 600 },
    { NOTE_F4, 300 }, { NOTE_F4, 300 }, { NOTE_E4, 300 }, { NOTE_E4, 300 },
    { NOTE_D4, 300 }, { NOTE_D4, 300 }, { NOTE_C4, 600 },
};
#define MELODY_LEN (sizeof(melody)/sizeof(melody[0]))

void pwm_buzzer_play_melody(void)
{
    ESP_LOGI(TAG, "播放小星星旋律 (14个音符)");
    for (int i = 0; i < MELODY_LEN; i++) {
        play_tone(melody[i].freq, melody[i].duration);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    pwm_buzzer_stop();
    ESP_LOGI(TAG, "旋律播放结束");
}

esp_err_t pwm_control_init(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << GPIO_BUZZER_PWM),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    gpio_set_level(GPIO_BUZZER_PWM, 1);
    ESP_LOGI(TAG, "蜂鸣器已初始化 (GPIO%d)", GPIO_BUZZER_PWM);
    return ESP_OK;
}