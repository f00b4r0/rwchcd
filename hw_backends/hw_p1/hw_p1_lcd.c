//
//  hw_backends/hw_p1/hw_p1_lcd.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 LCD driver.
 *
 * @warning most of this is a gross hack, XXX REVIEW.
 */

#include <string.h>
#include <stdio.h>
#include <string.h>

#include "lib.h"	// for temp_to_celsius
#include "runtime.h"
#include "alarms.h"
#include "hw_p1.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"

/**
 * Grab LCD control from the device firmware.
 * @param spi HW P1 spi private data
 * @return exec status
 */
static int hw_p1_lcd_grab(struct s_hw_p1_spi * const spi)
{
	return (hw_p1_spi_lcd_acquire(spi));
}

/**
 * Release LCD control from the device firmware.
 * @param lcd HW P1 LCD internal data
 * @param spi HW P1 spi private data
 * @return exec status
 */
static int hw_p1_lcd_release(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_spi * const spi)
{
	if (lcd->L2mngd)
		return (ALL_OK);	// never relinquish if L2 is managed

	return (hw_p1_spi_lcd_relinquish(spi));
}

/**
 * Request LCD fadeout from firmware.
 * @param spi HW P1 spi private data
 * @return exec status
 */
int hw_p1_lcd_fade(struct s_hw_p1_spi * const spi)
{
	return (hw_p1_spi_lcd_fade(spi));
}

/**
 * Clear LCD display
 * @param lcd HW P1 LCD internal data
 * @param spi HW P1 spi private data
 * @return exec status
 */
static int hw_p1_lcd_dispclear(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_spi * const spi)
{
	memset(lcd->Line1Cur, ' ', LCD_LINELEN);
	memset(lcd->Line1Cur, ' ', LCD_LINELEN);

	return (hw_p1_spi_lcd_cmd_w(spi, 0x01));
}

/**
 * Clear internal buffer line.
 * @param lcd HW P1 LCD internal data
 * @param linenb target line to clear (from 0)
 * @return exec status
 */
static int hw_p1_lcd_buflclear(struct s_hw_p1_lcd * const lcd, const uint_fast8_t linenb)
{
	switch (linenb) {
		case 0:
			memset(lcd->Line1Buf, ' ', LCD_LINELEN);
			break;
		case 1:
			memset(lcd->Line2Buf, ' ', LCD_LINELEN);
			break;
		default:
			return (-EINVALID);
	}
	
	return (ALL_OK);
}

/**
 * Select whether 2nd line is under our control or not
 * @param lcd HW P1 LCD internal data
 * @param on true if under our control
 * @return exec status
 */
static inline void hw_p1_lcd_handle2ndline(struct s_hw_p1_lcd * const lcd, const bool on)
{
	lcd->L2mngd = on;

	// handle reset of "L2 previously under management" flag, set in hw_p1_lcd_update()
	if (!on)
		lcd->L2mngd_prev = on;
}

/**
 * Write lcd data to a line buffer
 * @param lcd HW P1 LCD internal data
 * @param data the data to write
 * @param len the data length
 * @param linenb the target line number (from 0)
 * @param pos the target character position in the target line (from 0)
 * @return exec status
 */
static int hw_p1_lcd_wline(struct s_hw_p1_lcd * const lcd, const uint8_t * restrict const data, const uint_fast8_t len,
	      const uint_fast8_t linenb, const uint_fast8_t pos)
{
	int ret = ALL_OK;
	uint_fast8_t maxlen, calclen;
	uint8_t * restrict line;
	
	if ((!data) || (len > LCD_LINELEN))
		return (-EINVALID);
	
	if (pos >= LCD_LINELEN)
		return (-EINVALID);
	
	switch (linenb) {
		case 0:
			line = lcd->Line1Buf;
			break;
		case 1:
			if (lcd->L2mngd) {
				line = lcd->Line2Buf;
				break;
			}
			// fallthrough
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

	//dbgmsg("pos: %d, calclen: %d", pos, calclen);
	
	// update the buffer from the selected position
	memcpy(line+pos, data, calclen);
	
	return (ret);
}

/**
 * Update an LCD line
 * @param lcd HW P1 LCD internal data
 * @param spi HW P1 spi private data
 * @param linenb the target line to update (from 0)
 * @param force force refresh of entire line
 * @return exec status
 */
static int hw_p1_lcd_uline(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_spi * const spi, const uint_fast8_t linenb, const bool force)
{
	int ret = ALL_OK;
	uint_fast8_t id;
	uint8_t addr;
	uint8_t * restrict buf, * restrict cur;
	
	switch (linenb) {
		case 0:
			buf = lcd->Line1Buf;
			cur = lcd->Line1Cur;
			addr = 0x00;
			break;
		case 1:
			if (lcd->L2mngd) {
				buf = lcd->Line2Buf;
				cur = lcd->Line2Cur;
				addr = 0x40;
				break;
			}
			// fallthrough
		default:
			return (-EINVALID);
	}

	if (!force) {
		// find the first differing character between buffer and current
		for (id = 0; id < LCD_LINELEN; id++) {
			if (buf[id] != cur[id])
				break;
		}
		
		if (LCD_LINELEN == id)
			return (ret);	// buffer and current are identical, stop here
	}
	else
		id = 0;

	//dbgmsg("update line %d from pos %d", linenb+1, id+1);
	
	// grab LCD
	ret = hw_p1_lcd_grab(spi);
	if (ret)
		return (ret);
	
	// set target address
	addr += id;
	addr |= 0b10000000;	// DDRAM op
	
	ret = hw_p1_spi_lcd_cmd_w(spi, addr);
	if (ret)
		return (ret);
	
	// write data from Buf and update Cur - id already set
	for (; id < LCD_LINELEN; id++) {
		ret = hw_p1_spi_lcd_data_w(spi, buf[id]);
		if (ret)
			return (ret);
		//dbgmsg("sending: %c", buf[id]);
		
		cur[id] = buf[id];
	}
	
	// release LCD
	ret = hw_p1_lcd_release(lcd, spi);
	
	return (ret);
}

/**
 * LCD subsystem initialization.
 * @param lcd HW P1 LCD internal data
 * @return exec status
 */
int hw_p1_lcd_init(struct s_hw_p1_lcd * const lcd)
{
	lcd->online = false;
	lcd->reset = false;
	lcd->L2mngd = false;
	lcd->L2mngd_prev = false;
	lcd->sysmchg = false;
	lcd->sensor = 1;

	memset(lcd->Line1Buf, ' ', LCD_LINELEN);
	memset(lcd->Line1Cur, ' ', LCD_LINELEN);
	memset(lcd->Line2Buf, ' ', LCD_LINELEN);
	memset(lcd->Line1Cur, ' ', LCD_LINELEN);

	return (ALL_OK);
}

/**
 * Bring LCD subsystem online.
 * @note requires the hardware layer to be operational (SPI connection)
 * @param lcd HW P1 LCD internal data
 * @return exec status
 */
int hw_p1_lcd_online(struct s_hw_p1_lcd * const lcd)
{
	lcd->online = true;

	return (ALL_OK);
}

/**
 * Update LCD display
 * @param lcd HW P1 LCD internal data
 * @param spi HW P1 spi private data
 * @param force force refresh of entire display
 * @return exec status
 */
int hw_p1_lcd_update(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_spi * const spi, const bool force)
{
	bool l2force = force;
	int ret;

	if (!lcd->online)
		return (-EOFFLINE);
	
	ret = hw_p1_lcd_uline(lcd, spi, 0, force);
	if (ret)
		goto out;
	
	if (lcd->L2mngd) {
		if (!lcd->L2mngd_prev) {
			l2force = true;
			lcd->L2mngd_prev = true;
		}
		ret = hw_p1_lcd_uline(lcd, spi, 1, l2force);
	}

out:
	return (ret);
}

// XXX quick hack
static const char * hw_p1_lcd_disp_sysmode(enum e_systemmode sysmode)
{
	const char * restrict msg;

	switch (sysmode) {
		case SYS_OFF:
			msg = _("Off ");
			break;
		case SYS_AUTO:
			msg = _("Auto");
			break;
		case SYS_COMFORT:
			msg = _("Conf");
			break;
		case SYS_ECO:
			msg = _("Eco ");
			break;
		case SYS_FROSTFREE:
			msg = _("Prot");
			break;
		case SYS_DHWONLY:
			msg = _("ECS ");
			break;
		case SYS_TEST:
			msg = _("Test");
			break;
		case SYS_MANUAL:
			msg = _("Man ");
			break;
		case SYS_NONE:
		case SYS_UNKNOWN:
		default:
			dbgerr("Unhandled systemmode");
			msg = NULL;
	}
	return msg;
}

int hw_p1_sensor_clone_temp(void * priv, const sid_t id, temp_t * const tclone);
//* XXX quick hack for LCD
static const char * hw_p1_temp_to_str(struct s_hw_p1_pdata * restrict const hw, const sid_t tempid)
{
	static char snpbuf[10];	// xXX.XC, null-terminated (first x negative sign or positive hundreds)
	temp_t temp;
	float celsius;
	int ret;

	ret = hw_p1_sensor_clone_temp(hw, tempid, &temp);

#if (RWCHCD_TEMPMIN < ((-99 + 273) * KPRECISION))
 #error Non representable minimum temperature
#endif

	snprintf(snpbuf, 4, "%2d:", tempid);	// print in human readable

	if (-ESENSORDISCON == ret)
		strncpy(snpbuf+3, _("DISCON"), 6);
	else if (-ESENSORSHORT == ret)
		strncpy(snpbuf+3, _("SHORT "), 6);	// must be 6 otherwith buf[6] might be garbage
	else {
		celsius = temp_to_celsius(temp);
		snprintf(snpbuf+3, 7, "%3.0f C ", celsius);	// handles rounding
	}

	return (snpbuf);
}

// XXX quick hack
static int hw_p1_lcd_line1(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_pdata * restrict const hw)
{
	const enum e_systemmode systemmode = runtime_get()->systemmode;
	static uint8_t buf[LCD_LINELEN];

	memset(buf, ' ', LCD_LINELEN);

	memcpy(buf, hw_p1_lcd_disp_sysmode(systemmode), 4);

	if (lcd->sysmchg) {
		if (systemmode != lcd->newsysmode) {
			buf[5] = '-';
			buf[6] = '>';
			memcpy(buf+8, hw_p1_lcd_disp_sysmode(lcd->newsysmode), 4);
		}
		else
			lcd->sysmchg = false;
	}
	else
		memcpy(buf+6, hw_p1_temp_to_str(hw, lcd->sensor), 9);

	return (hw_p1_lcd_wline(lcd, buf, LCD_LINELEN, 0, 0));
}

/**
 * Force full refresh of the lcd display.
 * @param lcd HW P1 LCD internal data
 * @return exec status
 */
int hw_p1_lcd_reset(struct s_hw_p1_lcd * const lcd)
{
	if (!lcd->online)
		return (-EOFFLINE);

	lcd->reset = true;

	return (ALL_OK);
}

/**
 * Set current sensor displayed on LCD.
 * @param lcd HW P1 LCD internal data
 * @param tempid target sensor id
 * @return exec status
 * @warning no sanitization on tempid
 */
int hw_p1_lcd_set_tempid(struct s_hw_p1_lcd * const lcd, const sid_t tempid)
{
	if (!lcd->online)
		return (-EOFFLINE);

	lcd->sensor = tempid;

	return (ALL_OK);
}

/**
 * Indicate a system mode change has been requested.
 * @param lcd HW P1 LCD internal data
 * @param newsysmode the upcoming system mode
 * @return exec status
 */
int hw_p1_lcd_sysmode_change(struct s_hw_p1_lcd * const lcd, enum e_systemmode newsysmode)
{
	lcd->newsysmode = newsysmode;
	lcd->sysmchg = true;

	return (ALL_OK);
}

/**
 * Run the LCD subsystem.
 * @param lcd HW P1 LCD internal data
 * @param spi HW P1 spi private data
 * @return exec status
 */
int hw_p1_lcd_run(struct s_hw_p1_lcd * const lcd, struct s_hw_p1_spi * const spi, void * restrict const hwpriv)
{
	static char alarml1[LCD_LINELEN];
	struct s_hw_p1_pdata * restrict const hw = hwpriv;
	const char * alarm_msg16;
	int alcnt;
	size_t len;

	if (!lcd->online)
		return (-EOFFLINE);

	alcnt = alarms_count();
	if (alcnt) {
		snprintf(alarml1, sizeof(alarml1), _("ALARMS: %d"), alcnt);
		hw_p1_lcd_buflclear(lcd, 0);
		hw_p1_lcd_wline(lcd, (const uint8_t *)alarml1, strlen(alarml1), 0, 0);
		alarm_msg16 = alarms_last_msg(true);
		len = strlen(alarm_msg16);
		hw_p1_lcd_handle2ndline(lcd, true);
		hw_p1_lcd_buflclear(lcd, 1);
		hw_p1_lcd_wline(lcd, (const uint8_t *)alarm_msg16, (len > LCD_LINELEN) ? LCD_LINELEN : len, 1, 0);
	}
	else {
		hw_p1_lcd_handle2ndline(lcd, false);
		hw_p1_lcd_line1(lcd, hw);
	}

	hw_p1_lcd_update(lcd, spi, lcd->reset);

	lcd->reset = false;

	return (ALL_OK);
}

/**
 * Take LCD subsystem offline.
 * @param spi HW P1 spi private data
 * @return exec status
 */
int hw_p1_lcd_offline(struct s_hw_p1_lcd * const lcd)
{
	lcd->online = false;
	return (ALL_OK);
}

/**
 * LCD exit routine
 * @param spi HW P1 spi private data
 */
void hw_p1_lcd_exit(struct s_hw_p1_lcd * const lcd)
{
	return;
}
