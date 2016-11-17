//
//  rwchcd_lcd.h
//  rwchcd
//
//  Created by Thibaut Varene on 24/09/2016.
//  Copyright © 2016 Slashdirt. All rights reserved.
//

/**
 * @file
 * LCD API.
 */

#ifndef rwchcd_lcd_h
#define rwchcd_lcd_h

#include "rwchcd.h"

int lcd_subsys_init(void);
int lcd_buflclear(uint_fast8_t linenb);
int lcd_wline(const uint8_t * restrict data, const uint_fast8_t len,
	      const uint_fast8_t linenb, const uint_fast8_t pos);
int lcd_uline(const uint_fast8_t linenb, const bool force);
int lcd_update(const bool force);
int lcd_line1(const tempid_t tempid);
int lcd_fade(void);

#endif /* rwchcd_lcd_h */
