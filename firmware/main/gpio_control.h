#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include "esp_err.h"

// ===== 引脚定义（根据实际接线修改）=====
#define GPIO_PIR_INPUT       ((gpio_num_t)18)   	 // PIR传感器输出引脚
#define GPIO_BUTTON_BUILTIN  ((gpio_num_t)0)     // 开发板BOOT按键
#define GPIO_BUTTON_EXT      ((gpio_num_t)39)      // 门铃按键

// ===== 初始化 =====
esp_err_t gpio_control_init(void);

// ===== PIR传感器 =====
int gpio_get_pir_status(void);       // 返回1:有人, 0:无人

// ===== 按键 =====
int gpio_is_builtin_button_pressed(void);  // BOOT按键是否按下
int gpio_is_ext_button_pressed(void);      // 外接按键是否按下

// ===== 按键事件（供FreeRTOS任务轮询）=====
typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,       // 短按（<3秒）
    BUTTON_EVENT_LONG_PRESS         // 长按（>=3秒）
} button_event_t;

button_event_t gpio_get_button_event(void);

#endif
