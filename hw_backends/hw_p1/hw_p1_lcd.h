//
//  hw_backends/hw_p1/hw_p1_lcd.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 LCD driver API.
 */

#ifndef hw_p1_lcd_h
#define hw_p1_lcd_h

#include "rwchcd.h"

int hw_p1_lcd_init(void);
int hw_p1_lcd_online(void);
int hw_p1_lcd_reset(void);
int hw_p1_lcd_set_tempid(const sid_t tempid);
int hw_p1_lcd_sysmode_change(enum e_systemmode newsysmode);
int hw_p1_lcd_fade(void);
int hw_p1_lcd_run(void);
int hw_p1_lcd_offline(void);
void hw_p1_lcd_exit(void);

#endif /* hw_p1_lcd_h */
