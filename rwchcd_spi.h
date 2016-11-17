//
//  rwchcd_spi.h
//  
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * SPI backend API.
 */

#ifndef rwchcd_spi_h
#define rwchcd_spi_h

#include <stdint.h>
#include <stdbool.h>
#include "rwchc_export.h"

int rwchcd_spi_keepalive_once(void) __attribute__((warn_unused_result));
int rwchcd_spi_lcd_acquire(void) __attribute__((warn_unused_result));
int rwchcd_spi_lcd_relinquish(void) __attribute__((warn_unused_result));
int rwchcd_spi_lcd_fade(void) __attribute__((warn_unused_result));
int rwchcd_spi_lcd_cmd_w(const uint8_t cmd) __attribute__((warn_unused_result));
int rwchcd_spi_lcd_data_w(const uint8_t data) __attribute__((warn_unused_result));
int rwchcd_spi_lcd_bl_w(const uint8_t percent) __attribute__((warn_unused_result));
int rwchcd_spi_peripherals_r(union rwchc_u_outperiphs * const outperiphs) __attribute__((warn_unused_result));
int rwchcd_spi_peripherals_w(const union rwchc_u_outperiphs * const outperiphs) __attribute__((warn_unused_result));
int rwchcd_spi_relays_r(union rwchc_u_relays * const relays) __attribute__((warn_unused_result));
int rwchcd_spi_relays_w(const union rwchc_u_relays * const relays) __attribute__((warn_unused_result));
int rwchcd_spi_sensor_r(uint16_t tsensors[], const uint8_t sensor) __attribute__((warn_unused_result));
int rwchcd_spi_ref_r(uint16_t * const refval, const uint8_t refn) __attribute__((warn_unused_result));
int rwchcd_spi_settings_r(struct rwchc_s_settings * const settings) __attribute__((warn_unused_result));
int rwchcd_spi_settings_w(const struct rwchc_s_settings * const settings) __attribute__((warn_unused_result));
int rwchcd_spi_settings_s(void) __attribute__((warn_unused_result));
int rwchcd_spi_calibrate(void);
int rwchcd_spi_reset(void);
int rwchcd_spi_init(void) __attribute__((warn_unused_result));

#endif /* rwchcd_spi_h */
