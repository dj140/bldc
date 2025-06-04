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

#include "pwm_buzzer.h"
#include "ch.h"
#include "hal.h"
#include "conf_general.h"
#include "utils.h"

#pragma GCC push_options
#pragma GCC optimize ("Os")

#ifdef HW_BUZZER_PIN

// Settings
#define TIM_CLOCK			2000000 // Hz

// Private variables
static volatile bool m_is_running = false;
static volatile float PULSE_MAX_US = BUZZER_OUT_PULSE_MAX_US;

uint32_t pwm_buzzer_init(uint32_t freq_hz, float duty) {
	// Ensure that there is no overflow and that the resolution is reasonable 31hz ~ 20khz
	utils_truncate_number_uint32(&freq_hz, TIM_CLOCK / 65000, TIM_CLOCK / 100);

	palSetPadMode(HW_BUZZER_GPIO, HW_BUZZER_PIN, PAL_MODE_ALTERNATE(HW_BUZZER_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUDR_FLOATING);

	HW_ICU_TIM_CLK_EN();

	HW_BUZZER_TIMER->CR1 = 0;
	HW_BUZZER_TIMER->ARR = (uint16_t)((uint32_t)TIM_CLOCK / (uint32_t)freq_hz);
	HW_BUZZER_TIMER->PSC = (uint16_t)((168000000 / 2) / TIM_CLOCK) - 1;
	HW_BUZZER_TIMER->EGR = TIM_PSCReloadMode_Immediate;

	utils_truncate_number(&duty, 0.0, 1.0);
	uint32_t output = (uint32_t)((float)HW_BUZZER_TIMER->ARR * duty);

	if (HW_BUZZER_CHANNEL == ICU_CHANNEL_1) {
		HW_BUZZER_TIMER->CCER = TIM_OutputState_Enable;
		HW_BUZZER_TIMER->CCMR1 = TIM_OCMode_PWM1 | TIM_OCPreload_Enable;
		HW_BUZZER_TIMER->CCR1 = output;
	} else if (HW_BUZZER_CHANNEL == ICU_CHANNEL_2) {
		HW_BUZZER_TIMER->CCER = (TIM_OutputState_Enable << 4);
		HW_BUZZER_TIMER->CCMR1 = (TIM_OCMode_PWM1 << 8) | (TIM_OCPreload_Enable << 8);
		HW_BUZZER_TIMER->CCR2 = output;
	}

	HW_BUZZER_TIMER->CR1 |= TIM_CR1_ARPE;

	pwm_buzzer_set_buzzer_out(0);

	HW_BUZZER_TIMER->CR1 |= TIM_CR1_CEN;

	return freq_hz;
}

void pwm_buzzer_init_buzzer(void) {
	pwm_buzzer_init(BUZZER_OUT_RATE_HZ, 0.0);
}

void pwm_buzzer_stop(void) {

	// palSetPadMode(HW_BUZZER_GPIO, HW_BUZZER_PIN, PAL_MODE_OUTPUT_PUSHPULL);
	palClearPad(HW_BUZZER_GPIO, HW_BUZZER_PIN);
}


void pwm_buzzer_set_buzzer_out(float output) {

	utils_truncate_number(&output, 0.0, 1.0);

	float us = (float)BUZZER_OUT_PULSE_MIN_US + output *
			(float)(PULSE_MAX_US - BUZZER_OUT_PULSE_MIN_US);
	us *= (float)TIM_CLOCK / 1000000.0;

	if (HW_BUZZER_CHANNEL == ICU_CHANNEL_1) {
		HW_BUZZER_TIMER->CCR1 = (uint32_t)us;
	} else if (HW_BUZZER_CHANNEL == ICU_CHANNEL_2) {
		HW_BUZZER_TIMER->CCR2 = (uint32_t)us;
	}
}

void pwm_buzzer_set_buzzer_freq_duty(uint32_t freq_hz, float duty) {

	pwm_buzzer_init(freq_hz, 0.0);
	PULSE_MAX_US = 1000000/freq_hz;
	pwm_buzzer_set_buzzer_out(duty);

}

void play_tone(float freq, uint32_t duration_ms) {
	if (freq > 0) {
		pwm_buzzer_set_buzzer_freq_duty(freq, 0.5);
		// pwmChangePeriod(&BUZZER_PWM, BUZZER_PWM_FREQ_HZ / freq);
		// BUZZER_ON();
	}
	if (duration_ms > 0) {
		chThdSleepMilliseconds(duration_ms);
	}
	pwm_buzzer_set_buzzer_out(0.0);
	// BUZZER_OFF();
}

// standard BPM is 120 beats per minute
// 1 beat = 1/4 note
const uint32_t whole_note_ms = 500;

// Parse a note specification
// The note specification is a string of the form:
//   [A-G,P](#)([0-9])[/[1-9]]
// where:
//   [A-G] is the note name
//   [P] is a pause
//   [#] is an optional sharp
//   [0-9] (optional) octave number, defaults to 4
//   [/ [1-9]] (optional) is an optional duration fraction, for example /2 is a half note
//   Default is /4, i.e a quarter note.
// 
// The note is parsed and the frequency and duration are returned
// in the freq and duration_ms parameters.
// Returns 0 on success, -1 on error
int parse_note(const char *note_str, float *freq, uint32_t *duration_ms) {
	const char* ptr = note_str;

	// Parse note or pause
	if (*ptr == 'P') {
		*freq = 0;
		ptr++;
	} else if (*ptr >= 'A' && *ptr <= 'G') {
		char name = *ptr++;
		char* endptr;
		unsigned int octave = strtoul(ptr, &endptr, 10);
		if (ptr == endptr){
			octave = 4;
		}
		ptr = endptr;
		int is_sharp = (*ptr == '#') ? (ptr++, 1) : 0;
		
		int note_index = name - 'A';
		int A_offset = ((note_index - 2 + 7) % 7) - 5;
		int semitone_offset_from_A4 = 
			(2 * A_offset)                 // 2 semitones per natural note
			+ (A_offset < -2)              // Adjust for omitted E/F semitone
			+ is_sharp                     // Add 1 if the note is sharp
			+ ((octave - 4) * 12);         // Adjust for octave relative to A4

		// Calculate the frequency based on the semitone offset from A4
		*freq = 440.0 * pow(2.0, semitone_offset_from_A4 / 12.0);
	} else {
		return -1;
	};
	
	// Parse the duration
	uint8_t frac = 4; // default to quarter note  
	if (*ptr++ == '/') {
		char* endptr;
		frac = strtoul(ptr, &endptr, 10);
		ptr = endptr;
		if (frac == 0) {
			return -1;
		}
	}
	*duration_ms = whole_note_ms / frac;

	return 0;
}

int play_note(const char *note_str) {
	float freq;
	uint32_t dur;
	if (parse_note(note_str, &freq, &dur)) {
		return -1;
	}
	play_tone(freq, dur);
	return 0; 
}

// Play a melody
// The melody is a string of notes separated by spaces
// Example: D D F D  F G C5 A  A3 A3 C A3  C D G E
int play_melody(const char *melody_string) {
	const char* ptr = melody_string;
	while (*ptr) {
		if (play_note(ptr)) {
			return 0;
		}
		while (*ptr && *ptr != ' ') {
			ptr++;
		}
		if (*ptr) {
			ptr++;
		}
	}
	return 0;
}

#endif
#pragma GCC pop_options
