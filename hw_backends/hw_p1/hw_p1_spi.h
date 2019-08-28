//
//  hw_backends/hw_p1/hw_p1_spi.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Prototype 1 SPI protocol API.
 */

#ifndef hw_p1_spi_h
#define hw_p1_spi_h

#include <stdint.h>
#include "rwchc_export.h"

struct s_hw_p1_spi {
	struct {
		uint_least32_t clock;	///< SPI clock (1MHz recommended)
		uint_least8_t chan;	///< rWCHC SPI channel (normally 0)
	} set;
	struct {
		int_fast16_t spitout;	///< timeout counter used for SPI_RESYNC (pun not intended)
		int FWversion;		///< detected firmware version
	} run;
};

int hw_p1_spi_keepalive(struct s_hw_p1_spi * const spi) __attribute__((warn_unused_result));
int hw_p1_spi_fwversion(struct s_hw_p1_spi * const spi);
int hw_p1_spi_lcd_acquire(struct s_hw_p1_spi * const spi) __attribute__((warn_unused_result));
int hw_p1_spi_lcd_relinquish(struct s_hw_p1_spi * const spi) __attribute__((warn_unused_result));
int hw_p1_spi_lcd_fade(struct s_hw_p1_spi * const spi) __attribute__((warn_unused_result));
int hw_p1_spi_lcd_cmd_w(struct s_hw_p1_spi * const spi, const uint8_t cmd) __attribute__((warn_unused_result));
int hw_p1_spi_lcd_data_w(struct s_hw_p1_spi * const spi, const uint8_t data) __attribute__((warn_unused_result));
int hw_p1_spi_lcd_bl_w(struct s_hw_p1_spi * const spi, const uint8_t percent) __attribute__((warn_unused_result));
int hw_p1_spi_peripherals_r(struct s_hw_p1_spi * const spi, union rwchc_u_periphs * const periphs) __attribute__((warn_unused_result));
int hw_p1_spi_peripherals_w(struct s_hw_p1_spi * const spi, const union rwchc_u_periphs * const periphs) __attribute__((warn_unused_result));
int hw_p1_spi_relays_r(struct s_hw_p1_spi * const spi, union rwchc_u_relays * const relays) __attribute__((warn_unused_result));
int hw_p1_spi_relays_w(struct s_hw_p1_spi * const spi, const union rwchc_u_relays * const relays) __attribute__((warn_unused_result));
int hw_p1_spi_sensor_r(struct s_hw_p1_spi * const spi, rwchc_sensor_t tsensors[], const uint8_t sensor) __attribute__((warn_unused_result));
int hw_p1_spi_ref_r(struct s_hw_p1_spi * const spi, rwchc_sensor_t * const refval, const uint8_t refn) __attribute__((warn_unused_result));
int hw_p1_spi_settings_r(struct s_hw_p1_spi * const spi, struct rwchc_s_settings * const settings) __attribute__((warn_unused_result));
int hw_p1_spi_settings_w(struct s_hw_p1_spi * const spi, const struct rwchc_s_settings * const settings) __attribute__((warn_unused_result));
int hw_p1_spi_settings_s(struct s_hw_p1_spi * const spi) __attribute__((warn_unused_result));
int hw_p1_spi_reset(struct s_hw_p1_spi * const spi);
int hw_p1_spi_init(struct s_hw_p1_spi * const spi) __attribute__((warn_unused_result));

#endif /* hw_p1_spi_h */
