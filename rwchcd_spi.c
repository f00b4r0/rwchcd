//
//  rWCHCd_spi.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * SPI backend implementation for rWCHC hardware.
 *
 * @warning this implementation is NOT thread safe: callers must ensure proper synchronization.
 * @bug The code can go out of sync right after SPI_RESYNC():
 * I must rewrite the SPI protocol so that the sync is part of the ioctl that's being called.
 */

#include <stdio.h>
#include <unistd.h>	// sleep/usleep
#include <assert.h>
#include <wiringPiSPI.h>

#include "rwchcd_spi.h"
#include "rwchcd.h"	// for error codes

#define SPIDELAYUS	500		///< 500us
#define SPIRESYNCMAX	200		///< max resync tries -> 100ms @500us delay
#define SPICLOCK	1000000		///< SPI clock 1MHz
#define SPICHAN		0
#define SPIMODE		3	///< https://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase

#define SPI_ASSERT(emit, expect)	(SPI_rw8bit(emit) == (uint8_t)expect)
#define SPI_RESYNC(cmd)								\
		({								\
		spitout = SPIRESYNCMAX;						\
		while ((SPI_rw8bit(RWCHC_SPIC_SYNCREQ) != RWCHC_SPIC_SYNCACK) && --spitout);	\
		if (spitout) SPI_rw8bit(cmd);	/* consumes the last SYNCACK */	\
		})

static int_fast16_t spitout;	///< timeout counter used for SPI_RESYNC (pun not intended)

/**
 * Exchange 8bit data over SPI.
 * @param data data to send
 * @return data received
 */
static uint8_t SPI_rw8bit(const uint8_t data)
{
	static uint8_t exch;
	exch = data;
	wiringPiSPIDataRW(SPICHAN, &exch, 1);
	//printf("\tsent: %x, rcvd: %x\n", data, exch);
	usleep(SPIDELAYUS);
	return exch;
}

/**
 * Send a keepalive and verify the response.
 * Can be used e.g. at initialization time to ensure that there is a device connected:
 * if this function fails more than a reasonnable number of tries then there's a good
 * chance the device is not connected.
 * @return error status
 */
int rwchcd_spi_keepalive_once(void)
{
	SPI_RESYNC(RWCHC_SPIC_KEEPALIVE);

	if (SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) != RWCHC_SPIC_ALIVE)
		return (-ESPI);
	else
		return (ALL_OK);
}

/**
 * Acquire control over LCD display
 * @return error code
 */
int rwchcd_spi_lcd_acquire(void)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_LCDACQR);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_LCDACQR))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Relinquish control over LCD display (to embedded firmware).
 * @return error code
 */
int rwchcd_spi_lcd_relinquish(void)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_LCDRLQSH);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_LCDRLQSH))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Request LCD backlight fadeout
 * @return error code
 */
int rwchcd_spi_lcd_fade(void)
{
	int ret = -ESPI;

	SPI_RESYNC(RWCHC_SPIC_LCDFADE);

	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_LCDFADE))
		goto out;

	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write LCD command byte
 * @param cmd command byte to send
 * @return error code
 */
int rwchcd_spi_lcd_cmd_w(const uint8_t cmd)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_LCDCMDW);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(cmd, ~RWCHC_SPIC_LCDCMDW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, cmd))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write LCD data byte
 * @param data data byte to send
 * @return error code
 */
int rwchcd_spi_lcd_data_w(const uint8_t data)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_LCDDATW);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(data, ~RWCHC_SPIC_LCDDATW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, data))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write LCD backlight duty cycle. Will not be committed
 * to eeprom.
 * @param percent backlight duty cycle in percent
 * @return error code
 */
int rwchcd_spi_lcd_bl_w(const uint8_t percent)
{
	int ret = -ESPI;
	
	if (percent > 100)
		return -EINVALID;
	
	SPI_RESYNC(RWCHC_SPIC_LCDBKLW);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(percent, ~RWCHC_SPIC_LCDBKLW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, percent))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Read peripheral states
 * @param outperiphs pointer to struct whose values will be populated to match current states
 * @return error code
 */
int rwchcd_spi_peripherals_r(union rwchc_u_outperiphs * const outperiphs)
{
	int ret = -ESPI;
	
	assert(outperiphs);
	
	SPI_RESYNC(RWCHC_SPIC_PERIPHSR);
	
	if (!spitout)
		goto out;
	
	outperiphs->BYTE = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);

	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write peripheral states
 * @param outperiphs pointer to struct whose values are populated with desired states
 * @return error code
 */
int rwchcd_spi_peripherals_w(const union rwchc_u_outperiphs * const outperiphs)
{
	int ret = -ESPI;
	
	assert(outperiphs);
	
	// XXX TODO: REVISIT
	SPI_RESYNC((RWCHC_SPIC_PERIPHSW | (outperiphs->BYTE & RWCHC_OUTPERIPHMASK)));
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, outperiphs->BYTE))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Read relay states
 * @param relays pointer to struct whose values will be populated to match current states
 * @return error code
 */
int rwchcd_spi_relays_r(union rwchc_u_relays * const relays)
{
	int ret = -ESPI;
	
	assert(relays);
	
	SPI_RESYNC(RWCHC_SPIC_RELAYRL);
	
	if (!spitout)
		goto out;
	
	relays->LOWB = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);
	
	SPI_RESYNC(RWCHC_SPIC_RELAYRH);	// resync since we have exited the atomic section in firmware
	
	if (!spitout)
		goto out;
	
	relays->HIGHB = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write relay states
 * @param relays pointer to struct whose values are populated with desired states
 * @return error code
 */
int rwchcd_spi_relays_w(const union rwchc_u_relays * const relays)
{
	int ret = -ESPI;
	
	assert(relays);
	
	SPI_RESYNC(RWCHC_SPIC_RELAYWL);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(relays->LOWB, ~RWCHC_SPIC_RELAYWL))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, relays->LOWB))
		goto out;
	
	SPI_RESYNC(RWCHC_SPIC_RELAYWH);	// resync since we have exited the atomic section in firmware
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(relays->HIGHB, ~RWCHC_SPIC_RELAYWH))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, relays->HIGHB))
		goto out;
	
	ret = ALL_OK;	// all good
out:
	return ret;
}

/**
 * Read a single sensor value
 * @param tsensors pointer to target sensor array whose value will be updated regardless of errors
 * @param sensor target sensor number to be read
 * @return error code
 * @note not using rwchc_sensor_t here so that we get a build warning if the type changes
 */
int rwchcd_spi_sensor_r(uint16_t tsensors[], const uint8_t sensor)
{
	int ret = -ESPI;
	
	assert(tsensors);
	
	SPI_RESYNC(sensor);

	if (!spitout)
		goto out;
	
	tsensors[sensor] = SPI_rw8bit(~sensor);	// we get LSB first, sent byte must be ~sensor
	tsensors[sensor] |= (SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) << 8);	// then MSB, sent byte is next command

	if ((tsensors[sensor] & 0xFF00) == (RWCHC_SPIC_INVALID << 8))	// MSB indicates an error
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Read a single reference value
 * @param refval pointer to target reference whose value will be updated
 * @param refn target reference number to be read (0 or 1)
 * @return error code
 * @note not using rwchc_sensor_t here so that we get a build warning if the type changes
 */
int rwchcd_spi_ref_r(uint16_t * const refval, const uint8_t refn)
{
	int ret = -ESPI;
	uint8_t cmd;

	assert(refval);
	
	switch (refn) {
		case 0:
			cmd = RWCHC_SPIC_REF0;
			break;
		case 1:
			cmd = RWCHC_SPIC_REF1;
			break;
		default:
			return (-EINVALID);
	}

	SPI_RESYNC(cmd);

	if (!spitout)
		goto out;
	
	*refval = SPI_rw8bit(~cmd);	// we get LSB first, sent byte is ~cmd
	*refval |= (SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) << 8);	// then MSB, sent byte is next command

	if ((*refval & 0xFF00) == (RWCHC_SPIC_INVALID << 8))	// MSB indicates an error
		goto out;

	ret = ALL_OK;
out:
	return ret;
}

/**
 * Read current ram settings
 * @param settings pointer to struct whose values will be populated to match current settings
 * @return error code
 */
int rwchcd_spi_settings_r(struct rwchc_s_settings * const settings)
{
	unsigned int i;
	int ret = -ESPI;
	
	assert(settings);

	SPI_RESYNC(RWCHC_SPIC_SETTINGSR);
	
	if (!spitout)
		goto out;
	
	for (i=0; i<sizeof(*settings); i++)
		*((uint8_t *)settings+i) = SPI_rw8bit(i);
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_SETTINGSR))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write current ram settings
 * @param settings pointer to struct whose values are populated with desired settings
 * @return error code
 */
int rwchcd_spi_settings_w(const struct rwchc_s_settings * const settings)
{
	unsigned int i;
	int ret = -ESPI;
	
	assert(settings);
	
	SPI_RESYNC(RWCHC_SPIC_SETTINGSW);
	
	if (!spitout)
		goto out;
	
	for (i=0; i<sizeof(*settings); i++)
		if (SPI_rw8bit(*((const uint8_t *)settings+i)) != i)
			goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_SETTINGSW))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Save current ram settings to eeprom
 * @return error code
 */
int rwchcd_spi_settings_s(void)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_SETTINGSS);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_SETTINGSS))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Request sensor calibration
 * @return error code
 * @note sleeps
 */
int rwchcd_spi_calibrate(void)
{
	int ret = -ESPI;

	SPI_RESYNC(RWCHC_SPIC_CALIBRATE);

	if (!spitout)
		goto out;
	
	usleep(500*1000);	// must wait for completion (500ms)

	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_CALIBRATE))
		goto out;

	ret = ALL_OK;
out:
	return ret;
}

/**
 * Reset the device
 * @return exec status (ALL_OK if reset is presumably successful)
 */
int rwchcd_spi_reset(void)
{
	const uint8_t trig[] = RWCHC_RESET_TRIGGER;
	unsigned int i;
	int ret = -ESPI;

	SPI_RESYNC(RWCHC_SPIC_RESET);

	if (!spitout)
		goto out;
	
	for (i=0; i<ARRAY_SIZE(trig); i++)
		if (SPI_rw8bit(trig[i]) != i)
			goto out;

	ret = ALL_OK;	// reset successful
out:
	return ret;
}

/**
 * Init spi subsystem
 * @return fd or error code
 */
int rwchcd_spi_init(void)
{
	return (wiringPiSPISetupMode(SPICHAN, SPICLOCK, SPIMODE));
}
