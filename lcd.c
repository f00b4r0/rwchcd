//
//  lcd.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * LCD implementation.
 *
 * @todo this implementation isn't link-agnostic: it calls the SPI subsystem.
 * This should be reworked so that it becomes abstracted like the rest of the
 * hardware subsystem.
 * @todo most of this is a gross hack, XXX REVIEW.
 */

#include <string.h>

#include "spi.h"
#include "config.h"
#include "runtime.h"
#include "lib.h"
#include "hardware.h"
#include "alarms.h"
#include "lcd.h"

#define LCD_LINELEN	16	///< width of LCD display line

static struct {
	bool online;
	bool reset;	///< true if full refresh of the display is necessary
	bool L2mngd;	///< true if 2nd line is managed by software
	bool L2mngd_prev;	///< this flag is necessary to account for the fact that the firmware will modify the 2nd line
	bool sysmchg;	///< true if sysmode change in progress
	tempid_t sensor;	///< current sensor displayed on LCD
	enum e_systemmode newsysmode;	///< upcoming system mode
	uint8_t Line1Buf[LCD_LINELEN], Line1Cur[LCD_LINELEN];
	uint8_t Line2Buf[LCD_LINELEN], Line2Cur[LCD_LINELEN];
} LCD = {
	.online = false,
	.reset = false,
	.L2mngd = false,
	.L2mngd_prev = false,
	.sysmchg = false,
	.sensor = 1,
};

/**
 * Grab LCD control from the device firmware.
 * @return exec status
 */
static int lcd_grab(void)
{
	return (spi_lcd_acquire());
}

/**
 * Release LCD control from the device firmware.
 * @return exec status
 */
static int lcd_release(void)
{
	if (LCD.L2mngd)
		return (ALL_OK);	// never relinquish if L2 is managed

	return (spi_lcd_relinquish());
}

/**
 * Request LCD fadeout from firmware.
 * @return exec status
 */
int lcd_fade(void)
{
	return (spi_lcd_fade());
}

/**
 * Clear LCD display
 * @return exec status
 */
static int lcd_dispclear(void)
{
	memset(LCD.Line1Cur, ' ', LCD_LINELEN);
	memset(LCD.Line1Cur, ' ', LCD_LINELEN);

	return (spi_lcd_cmd_w(0x01));
}

/**
 * Clear internal buffer line.
 * @param linenb target line to clear (from 0)
 * @return exec status
 */
static int lcd_buflclear(const uint_fast8_t linenb)
{
	switch (linenb) {
		case 0:
			memset(LCD.Line1Buf, ' ', LCD_LINELEN);
			break;
		case 1:
			memset(LCD.Line2Buf, ' ', LCD_LINELEN);
			break;
		default:
			return (-EINVALID);
	}
	
	return (ALL_OK);
}

/**
 * Select whether 2nd line is under our control or not
 * @param on true if under our control
 * @return exec status
 */
static inline void lcd_handle2ndline(const bool on)
{
	LCD.L2mngd = on;

	// handle reset of "L2 previously under management" flag, set in lcd_update()
	if (!on)
		LCD.L2mngd_prev = on;
}

/**
 * Write lcd data to a line buffer
 * @param data the data to write
 * @param len the data length
 * @param linenb the target line number (from 0)
 * @param pos the target character position in the target line (from 0)
 * @return exec status
 */
static int lcd_wline(const uint8_t * restrict const data, const uint_fast8_t len,
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
			line = LCD.Line1Buf;
			break;
		case 1:
			if (LCD.L2mngd) {
				line = LCD.Line2Buf;
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

	//dbgmsg("pos: %d, calclen: %d", pos, calclen);
	
	// update the buffer from the selected position
	memcpy(line+pos, data, calclen);
	
	return (ret);
}

/**
 * Update an LCD line
 * @param linenb the target line to update (from 0)
 * @param force force refresh of entire line
 * @return exec status
 */
static int lcd_uline(const uint_fast8_t linenb, const bool force)
{
	int ret = ALL_OK;
	uint_fast8_t id;
	uint8_t addr;
	uint8_t * restrict buf, * restrict cur;
	
	switch (linenb) {
		case 0:
			buf = LCD.Line1Buf;
			cur = LCD.Line1Cur;
			addr = 0x00;
			break;
		case 1:
			if (LCD.L2mngd) {
				buf = LCD.Line2Buf;
				cur = LCD.Line2Cur;
				addr = 0x40;
				break;
			}
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
	ret = lcd_grab();
	if (ret)
		return (ret);
	
	// set target address
	addr += id;
	addr |= 0b10000000;	// DDRAM op
	
	ret = spi_lcd_cmd_w(addr);
	if (ret)
		return (ret);
	
	// write data from Buf and update Cur - id already set
	for (; id < LCD_LINELEN; id++) {
		ret = spi_lcd_data_w(buf[id]);
		if (ret)
			return (ret);
		//dbgmsg("sending: %c", buf[id]);
		
		cur[id] = buf[id];
	}
	
	// release LCD
	ret = lcd_release();
	
	return (ret);
}

/**
 * LCD subsystem initialization.
 * @return exec status
 */
int lcd_init(void)
{
	memset(LCD.Line1Buf, ' ', LCD_LINELEN);
	memset(LCD.Line1Cur, ' ', LCD_LINELEN);
	memset(LCD.Line2Buf, ' ', LCD_LINELEN);
	memset(LCD.Line1Cur, ' ', LCD_LINELEN);

	return (ALL_OK);
}

/**
 * Bring LCD subsystem online.
 * @note requires the hardware layer to be operational (SPI connection)
 * @return exec status
 */
int lcd_online(void)
{
	if (!hardware_is_online())
		return (-EOFFLINE);

	LCD.online = true;

	return (ALL_OK);
}

/**
 * Update LCD display
 * @param force force refresh of entire display
 * @return exec status
 */
int lcd_update(const bool force)
{
	bool l2force = force;
	int ret;

	if (!LCD.online)
		return (-EOFFLINE);
	
	ret = lcd_uline(0, force);
	if (ret)
		goto out;
	
	if (LCD.L2mngd) {
		if (!LCD.L2mngd_prev) {
			l2force = true;
			LCD.L2mngd_prev = true;
		}
		ret = lcd_uline(1, l2force);
	}

out:
	return (ret);
}

// XXX quick hack
static const char * temp_to_str(const tempid_t tempid)
{
	static char snpbuf[10];	// xXX.XC, null-terminated (first x negative sign or positive hundreds)
	const temp_t temp = get_temp(tempid);
	float celsius;

#if (RWCHCD_TEMPMIN < ((-99 + 273) * KPRECISIONI))
 #error Non representable minimum temperature
#endif

	snprintf(snpbuf, 4, "%2d:", tempid);	// print in human readable

	if (TEMPDISCON == temp)
		strncpy(snpbuf+3, _("DISCON"), 6);
	else if (TEMPSHORT == temp)
		strncpy(snpbuf+3, _("SHORT "), 6);	// must be 6 otherwith buf[6] might be garbage
	else {
		celsius = temp_to_celsius(temp);
		snprintf(snpbuf+3, 7, "%5.1fC", celsius);	// handles rounding
	}

	return (&snpbuf);
}

// XXX quick hack
static const char * lcd_disp_sysmode(enum e_systemmode sysmode)
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
		case SYS_MANUAL:
			msg = _("Man ");
			break;
		case SYS_UNKNOWN:
		default:
			dbgerr("Unhandled systemmode");
			msg = NULL;
	}
	return msg;
}

// XXX quick hack
static int lcd_line1(void)
{
	const enum e_systemmode systemmode = get_runtime()->systemmode;
	static uint8_t buf[LCD_LINELEN];

	memset(buf, ' ', LCD_LINELEN);

	memcpy(buf, lcd_disp_sysmode(systemmode), 4);

	if (LCD.sysmchg) {
		if (systemmode != LCD.newsysmode) {
			buf[5] = '-';
			buf[6] = '>';
			memcpy(buf+8, lcd_disp_sysmode(LCD.newsysmode), 4);
		}
		else
			LCD.sysmchg = false;
	}
	else
		memcpy(buf+6, temp_to_str(LCD.sensor), 9);

	return (lcd_wline(buf, LCD_LINELEN, 0, 0));
}

/**
 * Force full refresh of the lcd display.
 */
int lcd_reset(void)
{
	if (!LCD.online)
		return (-EOFFLINE);

	LCD.reset = true;

	return (ALL_OK);
}

/**
 * Set current sensor displayed on LCD.
 * @param tempid target sensor id
 * @return exec status
 */
int lcd_set_tempid(const tempid_t tempid)
{
	const struct s_config * restrict const config = get_runtime()->config;

	if (!LCD.online)
		return (-EOFFLINE);

	if (tempid > config->nsensors)
		return (-EINVALID);

	LCD.sensor = tempid;

	return (ALL_OK);
}

/**
 * Indicate a system mode change has been requested.
 * @param newsysmode the upcoming system mode
 * @return exec status
 */
int lcd_sysmode_change(enum e_systemmode newsysmode)
{
	LCD.newsysmode = newsysmode;
	LCD.sysmchg = true;

	return (ALL_OK);
}

/**
 * Run the LCD subsystem.
 * @return exec status
 */
int lcd_run(void)
{
	static char alarml1[LCD_LINELEN];
	const char * alarm_msg16;
	int alcnt;
	size_t len;

	if (!LCD.online)
		return (-EOFFLINE);

	alcnt = alarms_count();
	if (alcnt) {
		snprintf(alarml1, sizeof(alarml1), _("ALARMS: %d"), alcnt);
		lcd_buflclear(0);
		lcd_wline((const uint8_t *)alarml1, strlen(alarml1), 0, 0);
		alarm_msg16 = alarms_msg_iterator(true);
		len = strlen(alarm_msg16);
		lcd_handle2ndline(true);
		lcd_buflclear(1);
		lcd_wline((const uint8_t *)alarm_msg16, (len > LCD_LINELEN) ? LCD_LINELEN : len, 1, 0);
	}
	else {
		lcd_handle2ndline(false);
		lcd_line1();
	}

	lcd_update(LCD.reset);

	LCD.reset = false;

	return (ALL_OK);
}

/**
 * Take LCD subsystem offline.
 * @return exec status
 */
int lcd_offline(void)
{
	LCD.online = false;
	return (ALL_OK);
}

/**
 * LCD exit routine
 */
void lcd_exit(void)
{
	return;
}
