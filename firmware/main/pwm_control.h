#ifndef PWM_CONTROL_H
#define PWM_CONTROL_H

#include "esp_err.h"

#define GPIO_BUZZER_PWM    40

esp_err_t pwm_control_init(void);
void pwm_buzzer_play(int freq_hz, int duration_ms);
void pwm_buzzer_play_melody(void);
void pwm_buzzer_stop(void);

#endif
