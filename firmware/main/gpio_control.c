#include "gpio_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPIO";

typedef struct {
    int last_state;
    TickType_t press_start_time;
    int is_pressed;
} button_state_t;

static button_state_t s_builtin_btn = {1, 0, 0};
static button_state_t s_ext_btn = {1, 0, 0};

esp_err_t gpio_control_init(void)
{
    gpio_config_t pir_cfg = {
        .pin_bit_mask = (1ULL << GPIO_PIR_INPUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pir_cfg));

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON_BUILTIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    gpio_config_t ext_btn_cfg = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON_EXT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ext_btn_cfg));

    ESP_LOGI(TAG, "GPIO初始化完成: PIR=GPIO%d (下拉), 内置按键=GPIO%d, 外接按键=GPIO%d",
             GPIO_PIR_INPUT, GPIO_BUTTON_BUILTIN, GPIO_BUTTON_EXT);
    return ESP_OK;
}

int gpio_get_pir_status(void)
{
    return gpio_get_level(GPIO_PIR_INPUT) ? 1 : 0;
}

static int read_button_with_debounce(int pin, button_state_t *state)
{
    int current = gpio_get_level(pin);
    TickType_t now = xTaskGetTickCount();

    if (current != state->last_state) {
        state->last_state = current;
        return state->is_pressed;
    }

    state->is_pressed = (current == 0);
    return state->is_pressed;
}

int gpio_is_builtin_button_pressed(void)
{
    return read_button_with_debounce(GPIO_BUTTON_BUILTIN, &s_builtin_btn);
}

int gpio_is_ext_button_pressed(void)
{
    return read_button_with_debounce(GPIO_BUTTON_EXT, &s_ext_btn);
}

button_event_t gpio_get_button_event(void)
{
    static int last_builtin_state = 1;
    static TickType_t press_start = 0;
    int current = gpio_is_builtin_button_pressed();

    if (current == 1 && last_builtin_state == 0) {
        press_start = xTaskGetTickCount();
        last_builtin_state = current;
        return BUTTON_EVENT_NONE;
    }

    if (current == 0 && last_builtin_state == 1) {
        last_builtin_state = current;
        TickType_t pressed_time = xTaskGetTickCount() - press_start;
        if (pressed_time >= pdMS_TO_TICKS(3000)) {
            return BUTTON_EVENT_LONG_PRESS;
        } else if (pressed_time >= pdMS_TO_TICKS(50)) {
            return BUTTON_EVENT_SHORT_PRESS;
        }
        return BUTTON_EVENT_NONE;
    }

    last_builtin_state = current;
    return BUTTON_EVENT_NONE;
}