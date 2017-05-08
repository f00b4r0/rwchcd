//
//  rwchcd_lcd.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * LCD API.
 */

#ifndef rwchcd_lcd_h
#define rwchcd_lcd_h

#include "rwchcd.h"

int lcd_init(void);
int lcd_online(void);
int lcd_reset(void);
int lcd_set_tempid(const tempid_t tempid);
int lcd_fade(void);
int lcd_run(void);
int lcd_offline(void);
void lcd_exit(void);

#endif /* rwchcd_lcd_h */
