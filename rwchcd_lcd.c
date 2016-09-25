//
//  rwchcd_lcd.c
//  rwchcd
//
//  Created by Thibaut Varene on 24/09/2016.
//  Copyright © 2016 Slashdirt. All rights reserved.
//

#include <string.h>

#include "rwchcd_spi.h"
#include "rwchcd_lib.h"
#include "rwchcd_lcd.h"

#define LCD_LINELEN	16

// buffer is one char longer to accomodate for the final '\0'. The rest of the code assumes that this '\0' is always present
static char Line1Buf[LCD_LINELEN], Line1Cur[LCD_LINELEN];
static char Line2Buf[LCD_LINELEN], Line2Cur[LCD_LINELEN];
static bool L2mngd = false;

/**
 * LCD subsystem initialization.
 * @return exec status
 */
int lcd_subsys_init(void)
{
	memset(Line1Buf, ' ', LCD_LINELEN);
	memset(Line1Cur, ' ', LCD_LINELEN);
	memset(Line2Buf, ' ', LCD_LINELEN);
	memset(Line1Cur, ' ', LCD_LINELEN);
	
	return (ALL_OK);
}

/**
 * Grab LCD control from the device firmware.
 * @return exec status
 */
static int lcd_grab(void)
{
	int ret, i = 0;
	
	do {
		ret = rwchcd_spi_lcd_acquire();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
	
	return (ret);
}

/**
 * Release LCD control from the device firmware.
 * @return exec status
 */
static int lcd_release(void)
{
	int ret, i = 0;
	
	if (L2mngd)
		return (ALL_OK);	// never relinquish if L2 is managed
	
	do {
		ret = rwchcd_spi_lcd_relinquish();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
	
	return (ret);
}

static int lcd_dispclear(void)
{
	int ret, i = 0;
	
	memset(Line1Cur, ' ', LCD_LINELEN);
	memset(Line1Cur, ' ', LCD_LINELEN);

	do {
		ret = rwchcd_spi_lcd_cmd_w(0x01);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
	
	return (ret);
}

/**
 * Clear internal buffer line.
 * @param linenb target line to clear (from 0)
 * @return exec status
 */
int lcd_buflclear(uint_fast8_t linenb)
{
	switch (linenb) {
		case 0:
			memset(Line1Buf, ' ', LCD_LINELEN);
			break;
		case 1:
			memset(Line2Buf, ' ', LCD_LINELEN);
			break;
		default:
			return (-EINVALID);
	}
	
	ret (ALL_OK);
}

/**
 * Select whether 2nd line is under our control or not
 * @param on true if under our control
 * @return exec status
 */
int lcd_handle2ndline(const bool on)
{
	L2mngd = on;
	
	return (ALL_OK);
}

/**
 * Write a string to LCD.
 * @warning No boundary checks
 * @param str string to send
 * @return exec status
 */
int lcd_wstr(const char * str)
{
	int ret = -ESPI;
	
	while (*str != '\0') {
		if (rwchcd_spi_lcd_data_w(*str))
			goto out;
		str++;
		//usleep(100); DISABLED: SPI_rw8bit() already sleeps
	}
	
	ret = ALL_OK;
out:
	return (ret);
}

/**
 * Write lcd data to a line buffer
 * @param data the data to write
 * @param len the data length
 * @param linenb the target line number (from 0)
 * @param pos the target character position in the target line (from 0)
 * @return exec status
 */
int lcd_wline(const uint8_t * restrict data, const uint_fast8_t len,
	      const uint_fast8_t linenb, const uint_fast8_t pos)
{
	int ret = ALL_OK;
	uint_fast8_t maxlen, calclen;
	char * restrict line;
	
	if ((!data) || (len > LCD_LINELEN))
		return (-EINVALID);
	
	if (pos >= LCD_LINELEN)
		return (-EINVALID);
	
	switch (linenb) {
		case 0:
			line = Line1Buf;
			break;
		case 1:
			if (L2mngd) {
				line = Line2Buf;
				break;
			}
		default:
			return (-EINVALID);
	}
	
	// calculate maximum available length
	maxlen = LCD_LINELEN - pos;
	
	// select applicable length
	if (len > maxlen) {
		ret = -ETRUNC;	// signal we're going to truncate output
		calclen = maxlen;
	}
	else
		calclen = len;
	
	// update the buffer from the selected position
	memcpy(line+pos, data, len);
	
	return (ret);
}

/**
 * Update an LCD line
 * @param linenb the target line to update (from 0)
 * @return exec status
 */
int lcd_uline(const uint_fast8_t linenb)
{
	int ret = ALL_OK;
	uint_fast8_t id, i;
	uint8_t addr;
	char * restrict buf, * restrict cur;
	
	switch (linenb) {
		case 0:
			buf = Line1Buf;
			cur = Line1Cur;
			addr = 0x00;
			break;
		case 1:
			if (L2mngd) {
				buf = Line2Buf;
				cur = Line2Cur;
				addr = 0x40;
				break;
			}
		default:
			return (-EINVALID);
	}
	
	// find the first differing character between buffer and current
	for (id = 0; i<LCD_LINELEN; id++) {
		if (buf[i] != cur[i])
			break;
	}
	
	if (LCD_LINELEN == id)
		return (ret);	// buffer and current are identical, stop here
	
	dbgmsg("update line %d from pos %d", linenb+1, pos+1);
	
	// grab LCD
	ret = lcd_grab();
	if (ret)
		return (ret);
	
	// set target address
	addr += id;
	addr |= 0b10000000;	// DDRAM op
	
	i = 0;
	do {
		ret = rwchcd_spi_lcd_cmd_w(addr);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		return (ret);
	
	// write data from Buf and update Cur - id already set
	for (; id<LCD_LINELEN; id++) {
		i = 0;
		do {
			ret = rwchcd_spi_lcd_data_w(buf[id]);
		} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
		
		if (ret)
			return (ret);
		
		cur[id] = buf[id];
	}
	
	// release LCD
	ret = lcd_release();
	
	return (ret);
}

/**
 * Update LCD display
 * @return exec status
 */
int lcd_update(void)
{
	int ret;
	
	ret = lcd_uline(0);
	if (ret)
		goto out;
	
	if (L2mngd)
		ret = lcd_uline(1);
	
out:
	return (ret);
}

const char * const temp_to_str(temp_t temp)
{
	static char snpbuf[7];	// xXX.XC, null-terminated (first x negative sign or positive hundreds)
	float celsius = temp_to_celsius(temp);
	
#if (RWCHCD_TEMPMIN < ((-99 + 273) * 100))
#error Non representable minimum temperature
#endif

	snprintf(snpbuf, 6, "%5.1fC", celsius);

	return (&snpbuf);
}

const char * const lcd_disp_sysmode(void)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	char * restrict msg;

	switch (runtime->systemmode) {
		case SYS_OFF:
			msg = "Off ";
			break;
		case SYS_AUTO:
			msg = "Auto";
			break;
		case SYS_COMFORT:
			msg = "Comf";
			break;
		case SYS_ECO:
			msg = "Eco ";
			break;
		case SYS_FROSTFREE:
			msg = "Prot";
			break;
		case SYS_DHWONLY:
			msg = "DHW ";
			break;
		case SYS_MANUAL:
			msg = "Man ";
			break;
	}

}

int lcd_line1(void)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	char buf[LCD_LINELEN];
	memset(buf, ' ', LCD_LINELEN);
	
	strncpy(buf, lcd_disp_sysmode(), 4);
	
	buf[5] = 'O';
	buf[6] = 'u';
	buf[7] = 't';
	buf[8] = ':';
	
	strncpy(buf+9, temp_to_str(runtime->t_outdoor), 6);
	
	lcd_wline(buf, 15, 0, 0);
}