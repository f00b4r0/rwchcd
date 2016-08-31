//
//  rwchcd_spi.h
//  
//
//  Created by Thibaut Varene on 25/08/2016.
//
//

#ifndef rwchcd_spi_h
#define rwchcd_spi_h

#include <stdint.h>
#include "rwchc_export.h"

int rwchcd_spi_lcd_acquire(void);
int rwchcd_spi_lcd_relinquish(void);
int rwchcd_spi_lcd_cmd_w(const uint8_t cmd);
int rwchcd_spi_lcd_data_w(uint8_t data);
int rwchcd_spi_lcd_bl_w(uint8_t percent);
int rwchcd_spi_peripherals_r(union u_outperiphs * const outperiphs);
int rwchcd_spi_peripherals_w(const union u_outperiphs * const outperiphs);
int rwchcd_spi_relays_r(union u_relays * const relays);
int rwchcd_spi_relays_w(const union u_relays * const relays);
int rwchcd_spi_sensor_r(uint16_t tsensors[], int sensor);
int rwchcd_spi_settings_r(struct s_settings * const settings);
int rwchcd_spi_settings_w(const struct s_settings * const settings);
int rwchcd_spi_settings_s(void);
void rwchcd_spi_reset(void);
int rwchcd_spi_init(void);

#endif /* rwchcd_spi_h */
