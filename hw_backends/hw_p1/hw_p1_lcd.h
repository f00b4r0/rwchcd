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

#define LCD_LINELEN	16	///< width of LCD display line

/** HW P1 LCD internal data structure */
struct s_hw_p1_lcd {
	bool online;
	bool reset;		///< true if full refresh of the display is necessary
	bool L2mngd;		///< true if 2nd line is managed by software
	bool L2mngd_prev;	///< this flag is necessary to account for the fact that the firmware will modify the 2nd line
	bool sysmchg;		///< true if sysmode change in progress
	uint_fast8_t sensor;	///< current sensor displayed on LCD
	enum e_systemmode newsysmode;	///< upcoming system mode
	uint8_t Line1Buf[LCD_LINELEN], Line1Cur[LCD_LINELEN];
	uint8_t Line2Buf[LCD_LINELEN], Line2Cur[LCD_LINELEN];
};

int hw_p1_lcd_init(struct s_hw_p1_lcd * const lcd);
int hw_p1_lcd_online(struct s_hw_p1_lcd * const lcd);
int hw_p1_lcd_reset(struct s_hw_p1_lcd * const lcd);
int hw_p1_lcd_set_tempid(struct s_hw_p1_lcd * const lcd, const uint_fast8_t tempid);
int hw_p1_lcd_sysmode_change(struct s_hw_p1_lcd * const lcd, enum e_systemmode newsysmode);
int hw_p1_lcd_fade(struct s_hw_p1_spi * const spi);
int hw_p1_lcd_run(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_spi * const spi, void * restrict const hwpriv);
int hw_p1_lcd_offline(struct s_hw_p1_lcd * const lcd);
void hw_p1_lcd_exit(struct s_hw_p1_lcd * const lcd);

#endif /* hw_p1_lcd_h */
