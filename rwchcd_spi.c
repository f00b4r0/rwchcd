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
 */

#include <stdio.h>
#include <unistd.h>	// sleep/usleep
#include <assert.h>
#include <wiringPiSPI.h>

#include "rwchcd_spi.h"
#include "rwchcd.h"	// for error codes

#define SPIDELAYUS	100		///< time (us) between 2 consecutive SPI exchanges: 100us -> 10kchar/s SPI rate, allows 800 ISNS on the PIC
#define SPIRESYNCMAX	200		///< max resync tries -> terminal delay ~120ms including 100us SPIDELAYUS for each exchange
#define SPICLOCK	1000000		///< SPI clock 1MHz
#define SPICHAN		0
#define SPIMODE		3		///< https://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase

#define USLEEPLCDFAST	50		///< expected completion time (us) for most LCD ops
#define USLEEPLCDSLOW	2000		///< expected completion time (us) for clear/home cmds

#define SPI_ASSERT(emit, expect)	(SPI_rw8bit(emit) == (uint8_t)expect)	///< send emit and ensure we received expect

/**
 * SPI resync routine.
 * This routine ensure we enter the atomic SPI ops in firmware.
 * It uses an exponential back-off delay after each retry, starting from 0
 * (and thus only applying the embedded delay of SPI_rw8bit()), up to a terminal
 * delay of 1ms (5*SPIRESYNCMAX microseconds) on the last run.
 * With SPIRESYNCMAX=200, this translates to a standalone accumulated delay of
 * approximately 100ms. Adding the embedded delay of SPI_rw8bit() (100us), this adds
 * 20ms to this number.
 */
#define SPI_RESYNC(cmd)								\
		({								\
		spitout = SPIRESYNCMAX;						\
		while ((SPI_rw8bit(RWCHC_SPIC_SYNCREQ) != RWCHC_SPIC_SYNCACK) && spitout) usleep(5*SPIRESYNCMAX - 5*spitout--);	\
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
	usleep(SPIDELAYUS);
	return exch;
}

/**
 * Send a keepalive and verify the response.
 * Can be used e.g. at initialization time to ensure that there is a device connected:
 * if this function fails more than a reasonnable number of tries then there's a good
 * chance the device is not connected.
 * Delay: none
 * @return error status
 */
int spi_keepalive(void)
{
	SPI_RESYNC(RWCHC_SPIC_KEEPALIVE);

	if (SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_ALIVE))
		return (ALL_OK);
	else
		return (-ESPI);
}

/**
 * Acquire control over LCD display.
 * Delay: none
 * @return error code
 */
int spi_lcd_acquire(void)
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
 * Delay: none
 * @return error code
 */
int spi_lcd_relinquish(void)
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
 * Request LCD backlight fadeout.
 * Delay: none
 * @return error code
 */
int spi_lcd_fade(void)
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
 * Write LCD command byte.
 * Delay: LCD op execution time after command is sent
 * @param cmd command byte to send
 * @return error code
 */
int spi_lcd_cmd_w(const uint8_t cmd)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_LCDCMDW);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(cmd, ~RWCHC_SPIC_LCDCMDW))
		goto out;
	
	if (cmd & 0xFC)	// quick commands
		usleep(USLEEPLCDFAST);
	else	// clear/home
		usleep(USLEEPLCDSLOW);
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, cmd))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write LCD data byte.
 * Delay: LCD op execution time after data is sent
 * @param data data byte to send
 * @return error code
 */
int spi_lcd_data_w(const uint8_t data)
{
	int ret = -ESPI;
	
	SPI_RESYNC(RWCHC_SPIC_LCDDATW);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(data, ~RWCHC_SPIC_LCDDATW))
		goto out;
	
	usleep(USLEEPLCDFAST);	// wait for completion
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, data))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write LCD backlight duty cycle. Will not be committed
 * to eeprom.
 * Delay: none
 * @param percent backlight duty cycle in percent
 * @return error code
 */
int spi_lcd_bl_w(const uint8_t percent)
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
 * Read peripheral states.
 * Delay: none
 * @param outperiphs pointer to struct whose values will be populated to match current states
 * @return error code
 */
int spi_peripherals_r(union rwchc_u_outperiphs * const outperiphs)
{
	int ret = -ESPI;
	
	assert(outperiphs);
	
	SPI_RESYNC(RWCHC_SPIC_PERIPHSR);
	
	if (!spitout)
		goto out;
	
	outperiphs->BYTE = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);

	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_PERIPHSR))
		goto out;

	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write peripheral states.
 * Delay: none
 * @param outperiphs pointer to struct whose values are populated with desired states
 * @return error code
 */
int spi_peripherals_w(const union rwchc_u_outperiphs * const outperiphs)
{
	int ret = -ESPI;
	
	assert(outperiphs);
	
	SPI_RESYNC(RWCHC_SPIC_PERIPHSW);
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(outperiphs->BYTE, ~RWCHC_SPIC_PERIPHSW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, outperiphs->BYTE))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_PERIPHSW))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Read relay states.
 * Delay: none
 * @param relays pointer to struct whose values will be populated to match current states
 * @return error code
 */
int spi_relays_r(union rwchc_u_relays * const relays)
{
	int ret = -ESPI;
	
	assert(relays);
	
	SPI_RESYNC(RWCHC_SPIC_RELAYRL);
	
	if (!spitout)
		goto out;
	
	relays->LOWB = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_RELAYRL))
		goto out;
	
	SPI_RESYNC(RWCHC_SPIC_RELAYRH);	// resync since we have exited the atomic section in firmware
	
	if (!spitout)
		goto out;
	
	relays->HIGHB = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_RELAYRH))
		goto out;
	
	ret = ALL_OK;
out:
	return ret;
}

/**
 * Write relay states.
 * Delay: none
 * @param relays pointer to struct whose values are populated with desired states
 * @return error code
 */
int spi_relays_w(const union rwchc_u_relays * const relays)
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
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_RELAYWL))
		goto out;
	
	SPI_RESYNC(RWCHC_SPIC_RELAYWH);	// resync since we have exited the atomic section in firmware
	
	if (!spitout)
		goto out;
	
	if (!SPI_ASSERT(relays->HIGHB, ~RWCHC_SPIC_RELAYWH))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, relays->HIGHB))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_RELAYWH))
		goto out;
	
	ret = ALL_OK;	// all good
out:
	return ret;
}

/**
 * Read a single sensor value.
 * Delay: none
 * @param tsensors pointer to target sensor array whose value will be updated regardless of errors
 * @param sensor target sensor number to be read
 * @return error code
 * @note not using rwchc_sensor_t here so that we get a build warning if the type changes
 */
int spi_sensor_r(uint16_t tsensors[], const uint8_t sensor)
{
	int ret = -ESPI;
	
	assert(tsensors);
	
	SPI_RESYNC(sensor);

	if (!spitout)
		return (-ESPI);
	
	/* From here we invert the expectancy logic: we expect things to go well
	 * and we'll flag if they don't. The point is that we must not interrupt
	 * the loop even if there is a mistransfer, since the firmware expects
	 * a full transfer regardless of errors. */
	ret = ALL_OK;
	
	tsensors[sensor] = SPI_rw8bit(~sensor);	// we get LSB first, sent byte must be ~sensor
	tsensors[sensor] |= (SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) << 8);	// then MSB, sent byte is next command

	if ((tsensors[sensor] & 0xFF00) == (RWCHC_SPIC_INVALID << 8))	// MSB indicates an error
		ret = -ESPI;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, sensor))
		ret = -ESPI;
	
	return ret;
}

/**
 * Read a single reference value.
 * Delay: none
 * @param refval pointer to target reference whose value will be updated
 * @param refn target reference number to be read (0 or 1)
 * @return error code
 * @note not using rwchc_sensor_t here so that we get a build warning if the type changes
 * @todo XXX TODO: modify firmware to use the same logic as sensor_r()
 */
int spi_ref_r(uint16_t * const refval, const uint8_t refn)
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
 * Read current ram settings.
 * Delay: none
 * @param settings pointer to struct whose values will be populated to match current settings
 * @return error code
 */
int spi_settings_r(struct rwchc_s_settings * const settings)
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
 * Write current ram settings.
 * Delay: none
 * @param settings pointer to struct whose values are populated with desired settings
 * @return error code
 */
int spi_settings_w(const struct rwchc_s_settings * const settings)
{
	unsigned int i;
	int ret = -ESPI;
	
	assert(settings);
	
	SPI_RESYNC(RWCHC_SPIC_SETTINGSW);
	
	if (!spitout)
		goto out;
	
	/* From here we invert the expectancy logic: we expect things to go well
	 * and we'll flag if they don't. The point is that we must not interrupt
	 * the loop even if there is a mistransfer, since the firmware expects
	 * a full transfer regardless of errors. */
	ret = ALL_OK;
	
	for (i=0; i<sizeof(*settings); i++)
		if (SPI_rw8bit(*((const uint8_t *)settings+i)) != i)
			ret = -ESPI;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_SETTINGSW))
		ret = -ESPI;

out:
	return ret;
}

/**
 * Save current ram settings to eeprom.
 * Delay: none (eeprom write is faster than a SPI cycle)
 * @return error code
 */
int spi_settings_s(void)
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
 * Reset the device.
 * Delay: none (device unavailable until fully restarted: 1-2s delay would be reasonable)
 * @return exec status (ALL_OK if reset is presumably successful)
 */
int spi_reset(void)
{
	const uint8_t trig[] = RWCHC_RESET_TRIGGER;
	unsigned int i;
	int ret = -ESPI;

	SPI_RESYNC(RWCHC_SPIC_RESET);

	if (!spitout)
		goto out;
	
	/* From here we invert the expectancy logic: we expect things to go well
	 * and we'll flag if they don't. The point is that we must not interrupt
	 * the loop even if there is a mistransfer, since the firmware expects
	 * a full transfer regardless of errors. */
	ret = ALL_OK;
	
	for (i=0; i<ARRAY_SIZE(trig); i++)
		if (SPI_rw8bit(trig[i]) != i)
			ret = -ESPI;

out:
	return ret;
}

/**
 * Init spi subsystem
 * @return fd or error code
 */
int spi_init(void)
{
	return (wiringPiSPISetupMode(SPICHAN, SPICLOCK, SPIMODE));
}
