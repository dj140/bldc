/*
	Copyright 2018 Benjamin Vedder	benjamin@vedder.se
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

// V2

#include "hw.h"

#include "ch.h"
#include "hal.h"
#include "stm32f4xx_conf.h"
#include "utils.h"
#include <math.h>
#include "mc_interface.h"
#include "pwm_buzzer.h"

// Variables
static volatile bool i2c_running = false;
static mutex_t shutdown_mutex;
static float bt_diff = 0.0;
static float bt_lastval = 0.0;
static float bt_unpressed = 0.0;
static bool will_poweroff = false;
static bool force_poweroff = false;
static unsigned int bt_hold_counter = 0;

// Melodies
const char* MEL_JET_CONNECTED = "C5 E5 G5/2";
const char* MEL_JET_DISCONNECTED = "G5 E5 C5/2";
const char* MEL_ERROR = "C/1 P/1 C/1";

// I2C configuration
static const I2CConfig i2cfg = {
		OPMODE_I2C,
		100000,
		STD_DUTY_CYCLE
};

// static PWMConfig pwmcfg = {
// 		1000000,
// 		1000000 / BUZZER_FREQ_HZ,
// 		0,
// 		{
// 				{PWM_OUTPUT_ACTIVE_HIGH, NULL},
// 				{PWM_OUTPUT_ACTIVE_HIGH, NULL},
// 				{PWM_OUTPUT_ACTIVE_HIGH, NULL},
// 				{PWM_OUTPUT_ACTIVE_HIGH, NULL}
// 		},
// 		0,
// 		0
// };

#define EXT_BUZZER_ON()    pwm_buzzer_set_buzzer_out(0.5)
#define EXT_BUZZER_OFF()   pwm_buzzer_set_buzzer_out(0)

void buzzer_init(void) {
    // External Buzzer 
    palSetPadMode(HW_BUZZER_GPIO, HW_BUZZER_PIN,
                  PAL_MODE_OUTPUT_PUSHPULL |
                  PAL_STM32_OSPEED_HIGHEST);
	pwm_buzzer_init_buzzer();
	// pwmStart(&BUZZER_PWM, &pwmcfg);
	// // palSetLineMode(BUZZER_LINE, PAL_MODE_ALTERNATE(BUZZER_GPIO_AF));
	// palSetPadMode(BUZZER_GPIO, BUZZER_PIN, PAL_MODE_ALTERNATE(BUZZER_GPIO_AF));
	// BUZZER_OFF();
    play_melody(MEL_JET_CONNECTED);
}

static void beep_off(void)
{
	EXT_BUZZER_OFF();
}

static void beep_on(void)
{
	 EXT_BUZZER_ON();
}

// Private functions
static void terminal_shutdown_now(int argc, const char **argv);
static void terminal_shutdown_hold_on(int argc, const char **argv);
static void terminal_button_test(int argc, const char **argv);
static void terminal_buzzer_test(int argc, const char **argv);

void hw_init_gpio(void) {
	chMtxObjectInit(&shutdown_mutex);
	// GPIO clock enable
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	// LEDs
	palSetPadMode(LED_GREEN_GPIO, LED_GREEN_PIN,
			PAL_MODE_OUTPUT_PUSHPULL |
			PAL_STM32_OSPEED_HIGHEST);
	palClearPad(LED_GREEN_GPIO, LED_GREEN_PIN);
	palSetPadMode(LED_RED_GPIO, LED_RED_PIN,
			PAL_MODE_OUTPUT_PUSHPULL |
			PAL_STM32_OSPEED_HIGHEST);
	palClearPad(LED_RED_GPIO, LED_RED_PIN);

	// GPIOA Configuration: Channel 1 to 3 as alternate function push-pull
	palSetPadMode(GPIOA, 8, PAL_MODE_ALTERNATE(GPIO_AF_TIM1) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_FLOATING);
	palSetPadMode(GPIOA, 9, PAL_MODE_ALTERNATE(GPIO_AF_TIM1) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_FLOATING);
	palSetPadMode(GPIOA, 10, PAL_MODE_ALTERNATE(GPIO_AF_TIM1) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_FLOATING);

	palSetPadMode(GPIOB, 13, PAL_MODE_ALTERNATE(GPIO_AF_TIM1) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_FLOATING);
	palSetPadMode(GPIOB, 14, PAL_MODE_ALTERNATE(GPIO_AF_TIM1) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_FLOATING);
	palSetPadMode(GPIOB, 15, PAL_MODE_ALTERNATE(GPIO_AF_TIM1) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_FLOATING);

	// Hall sensors
	palSetPadMode(HW_HALL_ENC_GPIO1, HW_HALL_ENC_PIN1, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(HW_HALL_ENC_GPIO2, HW_HALL_ENC_PIN2, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(HW_HALL_ENC_GPIO3, HW_HALL_ENC_PIN3, PAL_MODE_INPUT_PULLUP);

	// Phase filters
	// palSetPadMode(PHASE_FILTER_GPIO, PHASE_FILTER_PIN,
	// 		PAL_MODE_OUTPUT_PUSHPULL |
	// 		PAL_STM32_OSPEED_HIGHEST);
	// PHASE_FILTER_OFF();

	// palSetPadMode(NRF5x_SWDIO_GPIO, NRF5x_SWDIO_PIN,
	// 	PAL_MODE_OUTPUT_PUSHPULL);
	// palSetPad(NRF5x_SWDIO_GPIO, NRF5x_SWDIO_PIN);

	// palSetPadMode(NRF5x_SWCLK_GPIO, NRF5x_SWCLK_PIN,
	// 	PAL_MODE_OUTPUT_PUSHPULL );
	// palClearPad(NRF5x_SWCLK_GPIO, NRF5x_SWCLK_PIN);

	// 	// Current filter
	// palSetPadMode(GPIOD, 2,
	// 		PAL_MODE_OUTPUT_PUSHPULL |
	// 		PAL_STM32_OSPEED_HIGHEST);

	// CURRENT_FILTER_OFF();

	// AUX pin
	AUX_OFF();
	palSetPadMode(AUX_GPIO, AUX_PIN,
			PAL_MODE_OUTPUT_PUSHPULL |
			PAL_STM32_OSPEED_HIGHEST);
			
	// ShutDown
	palSetPadMode(HW_SHUTDOWN_GPIO, HW_SHUTDOWN_PIN, PAL_MODE_OUTPUT_PUSHPULL);
	palSetPadMode(HW_SHUTDOWN_SENSE_GPIO, HW_SHUTDOWN_SENSE_PIN, PAL_MODE_INPUT_ANALOG);
	buzzer_init();

	// ADC Pins
	palSetPadMode(GPIOA, 0, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOA, 1, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOA, 2, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOA, 3, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOA, 5, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOA, 6, PAL_MODE_INPUT_ANALOG);

	palSetPadMode(GPIOB, 0, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOB, 1, PAL_MODE_INPUT_ANALOG);

	palSetPadMode(GPIOC, 0, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOC, 1, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOC, 2, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOC, 3, PAL_MODE_INPUT_ANALOG);
	palSetPadMode(GPIOC, 4, PAL_MODE_INPUT_ANALOG);
	
		terminal_register_command_callback(
		"shutdown",
		"Shutdown VESC now.",
		0,
		terminal_shutdown_now);
		
	terminal_register_command_callback(
		"shutdown hold on",
		"Pull shutdown pin high",
		0,
		terminal_shutdown_hold_on);
		
	terminal_register_command_callback(
		"test_button",
		"Try sampling the shutdown button",
		0,
		terminal_button_test);

	terminal_register_command_callback(
		"buzzer_test",
		"Test the buzzer",
		0,
		terminal_buzzer_test);

}

void hw_setup_adc_channels(void) {
	// ADC1 regular channels
	ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 2, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC1, ADC_Channel_5, 3, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC1, ADC_Channel_14, 4, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC1, ADC_Channel_Vrefint, 5, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 6, ADC_SampleTime_15Cycles);

	// ADC2 regular channels
	ADC_RegularChannelConfig(ADC2, ADC_Channel_1, 1, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC2, ADC_Channel_11, 2, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC2, ADC_Channel_6, 3, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC2, ADC_Channel_15, 4, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC2, ADC_Channel_0, 5, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC2, ADC_Channel_9, 6, ADC_SampleTime_15Cycles);

	// ADC3 regular channels
	ADC_RegularChannelConfig(ADC3, ADC_Channel_2, 1, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_12, 2, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_3, 3, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_13, 4, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_1, 5, ADC_SampleTime_15Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_2, 6, ADC_SampleTime_15Cycles);

	// Injected channels
	ADC_InjectedChannelConfig(ADC1, ADC_Channel_10, 1, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC2, ADC_Channel_11, 1, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC3, ADC_Channel_12, 1, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC1, ADC_Channel_10, 2, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC2, ADC_Channel_11, 2, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC3, ADC_Channel_12, 2, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC1, ADC_Channel_10, 3, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC2, ADC_Channel_11, 3, ADC_SampleTime_15Cycles);
	ADC_InjectedChannelConfig(ADC3, ADC_Channel_12, 3, ADC_SampleTime_15Cycles);
}

void hw_start_i2c(void) {
	i2cAcquireBus(&HW_I2C_DEV);

	if (!i2c_running) {
		palSetPadMode(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN,
				PAL_MODE_ALTERNATE(HW_I2C_GPIO_AF) |
				PAL_STM32_OTYPE_OPENDRAIN |
				PAL_STM32_OSPEED_MID1 |
				PAL_STM32_PUDR_PULLUP);
		palSetPadMode(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN,
				PAL_MODE_ALTERNATE(HW_I2C_GPIO_AF) |
				PAL_STM32_OTYPE_OPENDRAIN |
				PAL_STM32_OSPEED_MID1 |
				PAL_STM32_PUDR_PULLUP);

		i2cStart(&HW_I2C_DEV, &i2cfg);
		i2c_running = true;
	}

	i2cReleaseBus(&HW_I2C_DEV);
}

void hw_stop_i2c(void) {
	i2cAcquireBus(&HW_I2C_DEV);

	if (i2c_running) {
		palSetPadMode(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN, PAL_MODE_INPUT);
		palSetPadMode(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN, PAL_MODE_INPUT);

		i2cStop(&HW_I2C_DEV);
		i2c_running = false;

	}

	i2cReleaseBus(&HW_I2C_DEV);
}

/**
 * Try to restore the i2c bus
 */
void hw_try_restore_i2c(void) {
	if (i2c_running) {
		i2cAcquireBus(&HW_I2C_DEV);

		palSetPadMode(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN,
				PAL_STM32_OTYPE_OPENDRAIN |
				PAL_STM32_OSPEED_MID1 |
				PAL_STM32_PUDR_PULLUP);

		palSetPadMode(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN,
				PAL_STM32_OTYPE_OPENDRAIN |
				PAL_STM32_OSPEED_MID1 |
				PAL_STM32_PUDR_PULLUP);

		palSetPad(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN);
		palSetPad(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN);

		chThdSleep(1);

		for(int i = 0;i < 16;i++) {
			palClearPad(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN);
			chThdSleep(1);
			palSetPad(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN);
			chThdSleep(1);
		}

		// Generate start then stop condition
		palClearPad(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN);
		chThdSleep(1);
		palClearPad(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN);
		chThdSleep(1);
		palSetPad(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN);
		chThdSleep(1);
		palSetPad(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN);

		palSetPadMode(HW_I2C_SCL_PORT, HW_I2C_SCL_PIN,
				PAL_MODE_ALTERNATE(HW_I2C_GPIO_AF) |
				PAL_STM32_OTYPE_OPENDRAIN |
				PAL_STM32_OSPEED_MID1 |
				PAL_STM32_PUDR_PULLUP);

		palSetPadMode(HW_I2C_SDA_PORT, HW_I2C_SDA_PIN,
				PAL_MODE_ALTERNATE(HW_I2C_GPIO_AF) |
				PAL_STM32_OTYPE_OPENDRAIN |
				PAL_STM32_OSPEED_MID1 |
				PAL_STM32_PUDR_PULLUP);

		HW_I2C_DEV.state = I2C_STOP;
		i2cStart(&HW_I2C_DEV, &i2cfg);

		i2cReleaseBus(&HW_I2C_DEV);
	}
}

float hw75_100_get_temp(void) {
	float t1 = (1.0 / ((logf(NTC_RES(ADC_Value[ADC_IND_TEMP_MOS]) / 10000.0) / 3380.0) + (1.0 / 298.15)) - 273.15);
	float t2 = (1.0 / ((logf(NTC_RES(ADC_Value[ADC_IND_TEMP_MOS_2]) / 10000.0) / 3380.0) + (1.0 / 298.15)) - 273.15);
	float t3 = (1.0 / ((logf(NTC_RES(ADC_Value[ADC_IND_TEMP_MOS_3]) / 10000.0) / 3380.0) + (1.0 / 298.15)) - 273.15);
	float res = 0.0;

	if (t1 > t2 && t1 > t3) {
		res = t1;
	} else if (t2 > t1 && t2 > t3) {
		res = t2;
	} else {
		res = t3;
	}

	return res;
}

#define RISING_EDGE_THRESHOLD 0.09
#define TIME_500MS 50
#define TIME_3S 300
#define ERPM_THRESHOLD 100

/**
 * hw_sample_shutdown_button - return false if shutdown is requested, true otherwise
 *
 * Behavior: after determining the unpressed level, look for rising edges or values
 * that are clearly above the unpressed level (2 x Threshold higher), triggering a counter.
 *
 * Once triggered, the counter keeps incrementing as long as the level is 2 x Threshold higher
 * than the normal/unpressed value, otherwise it gets reset to zero.
 *
 * Once the counter reaches the threshold the button is considered pressed, provided that
 * the erpm is below 100. A very short (20ms) beep will go off.
 * Shutdown actually happens on the falling edge when the press is over.
 *
 * If the motor is spinning faster, then a 3s press is required. Buzzer will beep once the
 * time has been reached. Again, shutdown happens on the falling edge.
 *
 * Normal shutdown time:    0.5s
 * Emergency shutdown time: 3.0s
 */

bool hw_sample_shutdown_button(void) {
    chMtxLock(&shutdown_mutex);
    float newval = ADC_VOLTS(ADC_IND_SHUTDOWN);
    chMtxUnlock(&shutdown_mutex);
    if (bt_lastval == 0) {
        bt_lastval = newval;
        return true;
    }
    bt_diff = (newval - bt_lastval);

    bool is_steady = fabsf(bt_diff) < 0.02;  // filter out noise above 20mV
    bool is_rising_edge = (bt_diff > RISING_EDGE_THRESHOLD);

    bt_lastval = newval;

    if (bt_unpressed == 0.0) {
        // initializing bt_unpressed
        if (is_steady) {
            bt_unpressed = newval;
        }
        // return true regardless (this happens only after boot)
        return true;
    }

    if (will_poweroff) {
        if (!force_poweroff && (fabsf(mc_interface_get_rpm()) > ERPM_THRESHOLD)) {
            will_poweroff = false;
            bt_hold_counter = 0;
            beep_off();
            return true;
        }

        // Now we look for a falling edge to shut down
        if ((bt_diff < -RISING_EDGE_THRESHOLD) || (newval < bt_unpressed + RISING_EDGE_THRESHOLD / 2)) {
            bt_hold_counter++;
            beep_off();
            return false;
        }
        return true;
    }

    if (bt_hold_counter == 0) {
        if (is_rising_edge) {
            // trigger by edge and by level!
            bt_hold_counter = 1;
        }
        else {
            if (is_steady && (newval < bt_unpressed + RISING_EDGE_THRESHOLD / 2)) {
                // pickup drifts due to temperature
                bt_unpressed = bt_unpressed * 0.9 + newval * 0.1;
            }
        }
    }
    else {
        // we've had a rising edge and are now checking for a steady hold
        if (newval > bt_unpressed + RISING_EDGE_THRESHOLD * 1.5) {
            bt_hold_counter++;

            if (bt_hold_counter > TIME_500MS) {
                if (fabsf(mc_interface_get_rpm()) < ERPM_THRESHOLD) {
                    // after 150ms, power-down is triggered by the falling edge (releasing the button)
                    will_poweroff = true;
                    bt_hold_counter = 0;

                    // super short beep to let the user know they can let go of the button now
                    // beep_on();
                    // chThdSleepMilliseconds(50);
                    // beep_off();
					play_melody(MEL_JET_DISCONNECTED);
                }
                else {
                    if (bt_hold_counter  > TIME_3S) {
                        // Emergency Power-Down - beep to let the user know it's ready
                        // beep_on();
						play_melody(MEL_ERROR);
						play_melody(MEL_JET_DISCONNECTED);
                        will_poweroff = true;
                        force_poweroff = true;
                        bt_hold_counter = 0;
                        return true;
                    }
                }
            }
        }
        else {
            // press is too short, abort
            bt_hold_counter = 0;
            beep_off();
        }
    }
    return true;
}


static void terminal_shutdown_now(int argc, const char **argv) {
	(void)argc;
	(void)argv;
	DISABLE_GATE();
	HW_SHUTDOWN_HOLD_OFF();
}

static void terminal_shutdown_hold_on(int argc, const char **argv) {
	(void)argc;
	(void)argv;
	//shutdown_set_sampling_disabled(true);
	palSetPadMode(HW_SHUTDOWN_GPIO, HW_SHUTDOWN_PIN, PAL_MODE_OUTPUT_PUSHPULL);
	HW_SHUTDOWN_HOLD_ON();
}

static void terminal_button_test(int argc, const char **argv) {
	(void)argc;
	(void)argv;

	for (int i = 0;i < 40;i++) {
		commands_printf("BT: %d:%d [%.2fV], %.2fV, %.2fV, OFF=%d", HW_SAMPLE_SHUTDOWN(), bt_hold_counter,
                        (double)bt_diff, (double)bt_unpressed, (double)bt_lastval, (int)will_poweroff);
		chThdSleepMilliseconds(100);
	}
}

static void terminal_buzzer_test(int argc, const char **argv) {
	for (size_t i = 1; i < (size_t)argc; i++) {
		play_melody(argv[i]);
	}
}
