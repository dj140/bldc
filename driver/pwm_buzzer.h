/*
	Copyright 2024 Benjamin Vedder	benjamin@vedder.se

	This file is part of the VESC firmware.

	The VESC firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The VESC firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#ifndef DRIVER_PWM_BUZZER_H_
#define DRIVER_PWM_BUZZER_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Buzzer output driver
 */
#define BUZZER_OUT_PULSE_MIN_US		0	   // Minimum pulse length in microseconds
#define BUZZER_OUT_PULSE_MAX_US		250	   // Maximum pulse length in microseconds
#define BUZZER_OUT_RATE_HZ			4000   // Update rate in Hz

uint32_t pwm_buzzer_init(uint32_t freq_hz, float duty);
void pwm_buzzer_init_buzzer(void);
void pwm_buzzer_set_buzzer_out(float output);
void pwm_buzzer_stop(void);
void pwm_buzzer_set_buzzer_freq_duty(uint32_t freq_hz, float duty);
int play_melody(const char *melody_string);
void play_tone(float freq, uint32_t duration_ms);

#endif /* DRIVER_PWM_BUZZER_H_ */
