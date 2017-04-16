//
//  rwchcd_lcd.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * LCD implementation.
 */

#include <string.h>

#include "rwchcd_spi.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_lcd.h"

#define LCD_LINELEN	16	///< width of LCD display line

// buffer is one char longer to accomodate for the final '\0'. The rest of the code assumes that this '\0' is always present
static uint8_t Line1Buf[LCD_LINELEN], Line1Cur[LCD_LINELEN];
static uint8_t Line2Buf[LCD_LINELEN], Line2Cur[LCD_LINELEN];
static bool L2mngd = false;	///< true if 2nd line is managed by software

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
	return (spi_lcd_acquire());
}

/**
 * Release LCD control from the device firmware.
 * @return exec status
 */
static int lcd_release(void)
{
	if (L2mngd)
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
	memset(Line1Cur, ' ', LCD_LINELEN);
	memset(Line1Cur, ' ', LCD_LINELEN);

	return (spi_lcd_cmd_w(0x01));
}

/**
 * Clear internal buffer line.
 * @param linenb target line to clear (from 0)
 * @return exec status
 */
int lcd_buflclear(const uint_fast8_t linenb)
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
	
	return (ALL_OK);
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
 * Write lcd data to a line buffer
 * @param data the data to write
 * @param len the data length
 * @param linenb the target line number (from 0)
 * @param pos the target character position in the target line (from 0)
 * @return exec status
 */
int lcd_wline(const uint8_t * restrict const data, const uint_fast8_t len,
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
int lcd_uline(const uint_fast8_t linenb, const bool force)
{
	int ret = ALL_OK;
	uint_fast8_t id;
	uint8_t addr;
	uint8_t * restrict buf, * restrict cur;
	
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

	if (!force) {
		// find the first differing character between buffer and current
		for (id = 0; id<LCD_LINELEN; id++) {
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
	for (; id<LCD_LINELEN; id++) {
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
 * Update LCD display
 * @param force force refresh of entire display
 * @return exec status
 */
int lcd_update(const bool force)
{
	int ret;
	
	ret = lcd_uline(0, force);
	if (ret)
		goto out;
	
	if (L2mngd)
		ret = lcd_uline(1, force);
	
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

	if (temp > RWCHCD_TEMPMAX)
		strncpy(snpbuf+3, _("DISCON"), 6);
	else if (temp < RWCHCD_TEMPMIN)
		strncpy(snpbuf+3, _("SHORT "), 6);	// must be 6 otherwith buf[6] might be garbage
	else {
		celsius = temp_to_celsius(temp);
		snprintf(snpbuf+3, 7, "%5.1fC", celsius);	// handles rounding
	}

	return (&snpbuf);
}

// XXX quick hack
static const char * lcd_disp_sysmode(void)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	const char * restrict msg;

	switch (runtime->systemmode) {
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
int lcd_line1(const tempid_t tempid)
{
	uint8_t buf[LCD_LINELEN];
	memset(buf, ' ', LCD_LINELEN);
	
	memcpy(buf, lcd_disp_sysmode(), 4);
	
	memcpy(buf+6, temp_to_str(tempid), 9);
	
	return (lcd_wline(buf, 16, 0, 0));
}
