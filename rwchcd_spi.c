//
//  rWCHCd_spi.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#include <stdio.h>
#include <unistd.h>	// sleep/usleep
#include <wiringPiSPI.h>
#include "rwchcd_spi.h"

#define SPISPEED	1000000		///< 1MHz
#define SPICHAN		0
#define SPIMODE		3
// https://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase

#define SPI_RESYNC()	while(SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) != RWCHC_SPIC_VALID)
#define SPI_ASSERT(cmd, expect)	(SPI_rw8bit(cmd) == expect)

/**
 * Exchange 8bit data over SPI.
 * @param data data to send
 * @return data received
 */
static uint8_t SPI_rw8bit(const uint8_t data)
{
	uint8_t exch = data;
	wiringPiSPIDataRW(SPICHAN, &exch, 1);
	//printf("\tsent: %x, rcvd: %x\n", data, exch);
	usleep(500);
	return exch;
}

/**
 * Acquire control over LCD display
 * @return error code
 */
int rwchcd_spi_lcd_acquire(void)
{
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_LCDACQR, RWCHC_SPIC_VALID))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_LCDACQR))
		goto out;
	
	ret = 0;
out:
	return ret;
}

/**
 * Relinquish control over LCD display (to embedded firmware).
 * @return error code
 */
int rwchcd_spi_lcd_relinquish(void)
{
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_LCDRLQSH, RWCHC_SPIC_VALID))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_LCDRLQSH))
		goto out;
	
	ret = 0;
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
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_LCDCMDW, RWCHC_SPIC_VALID))
		goto out;
	
	if (!SPI_ASSERT(cmd, RWCHC_SPIC_LCDCMDW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, cmd))
		goto out;
	
	ret = 0;
out:
	return ret;
}

/**
 * Write LCD data byte
 * @param data data byte to send
 * @return error code
 */
int rwchcd_spi_lcd_data_w(uint8_t data)
{
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_LCDDATW, RWCHC_SPIC_VALID))
		goto out;
	
	if (!SPI_ASSERT(data, RWCHC_SPIC_LCDDATW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, data))
		goto out;
	
	ret = 0;
out:
	return ret;
}

/**
 * Write LCD backlight duty cycle. Will not be committed
 * to eeprom.
 * @param percent backlight duty cycle in percent
 * @return error code
 */
int rwchcd_spi_lcd_bl_w(uint8_t percent)
{
	int ret = -1;
	
	if (percent > 100)
		return -2;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_LCDBKLW, RWCHC_SPIC_VALID))
		goto out;

	if (!SPI_ASSERT(percent, RWCHC_SPIC_LCDBKLW))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, percent))
		goto out;
	
	ret = 0;
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
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_PERIPHSR, RWCHC_SPIC_VALID))
		goto out;
	
	outperiphs->BYTE = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);

	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_VALID))
		goto out;
	
	ret = 0;
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
	int ret = -1;
	
	SPI_RESYNC();
	
	// XXX REVISIT
	if (!SPI_ASSERT((RWCHC_SPIC_PERIPHSW | (outperiphs->BYTE & 0xF)), RWCHC_SPIC_VALID))
		goto out;

	// return value is active BYTE, not necessarily == the one we just sent
//	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, outperiphs->BYTE))
//		goto out;
	
	ret = 0;
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
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_RELAYRL, RWCHC_SPIC_VALID))
		goto out;
	
	relays->LOWB = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);
	
	if (!SPI_ASSERT(RWCHC_SPIC_RELAYRH, RWCHC_SPIC_VALID))
		goto out;

	relays->HIGHB = SPI_rw8bit(RWCHC_SPIC_KEEPALIVE);
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_VALID))
		goto out;
	
	ret = 0;
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
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_RELAYWL, RWCHC_SPIC_VALID))
		goto out;
	
	if (!SPI_ASSERT(relays->LOWB, RWCHC_SPIC_RELAYWL))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_RELAYWH, relays->LOWB))
		goto out;
	
	if (!SPI_ASSERT(relays->HIGHB, RWCHC_SPIC_RELAYWH))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, relays->HIGHB))
		goto out;
	
	ret = 0;	// all good
out:
	return ret;
}

/**
 * Read a single sensor value
 * @param tsensors pointer to target sensor array whose value will be updated
 * @param sensor target sensor number to be read
 * @return error code
 */
int rwchcd_spi_sensor_r(uint16_t tsensors[], int sensor)
{
	int ret = -1;
	
	SPI_RESYNC();

	if (!SPI_ASSERT(sensor, RWCHC_SPIC_VALID))	// check we're in sync, otherwise query until we get what we expect
		goto out;
	
	tsensors[sensor] = SPI_rw8bit(sensor);	// we get LSB first, sent byte is ignored
	tsensors[sensor] |= (SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) << 8);	// then MSB, sent byte is next command
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_VALID))
		goto out;

	ret = 0;
out:
	return ret;
}

/**
 * Read a single reference value
 * @param refval pointer to target reference whose value will be updated
 * @param refn target reference number to be read (0 or 1)
 * @return error code
 */
int rwchcd_spi_ref_r(uint16_t * const refval, const int refn)
{
	int cmd, ret = -1;

	if (1 == refn)
		cmd = RWCHC_SPIC_REF1;
	else
		cmd = RWCHC_SPIC_REF0;


	SPI_RESYNC();

	if (!SPI_ASSERT(cmd, RWCHC_SPIC_VALID))	// check we're in sync, otherwise query until we get what we expect
		goto out;

	*refval = SPI_rw8bit(cmd);	// we get LSB first, sent byte is ignored
	*refval |= (SPI_rw8bit(RWCHC_SPIC_KEEPALIVE) << 8);	// then MSB, sent byte is next command

	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_VALID))
		goto out;

	ret = 0;
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
	int i, ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_SETTINGSR, RWCHC_SPIC_VALID))
		goto out;
	
	for (i=0; i<sizeof(*settings); i++)
		*((uint8_t *)settings+i) = SPI_rw8bit(i);
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_SETTINGSR))
		goto out;
	
	ret = 0;
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
	int i, ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_SETTINGSW, RWCHC_SPIC_VALID))
		goto out;
	
	for (i=0; i<sizeof(*settings); i++)
		if (SPI_rw8bit(*((uint8_t *)settings+i)) != i)
			goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_SETTINGSW))
		goto out;
	
	ret = 0;
out:
	return ret;
}

/**
 * Save current ram settings to eeprom
 * @return error code
 */
int rwchcd_spi_settings_s(void)
{
	int ret = -1;
	
	SPI_RESYNC();
	
	if (!SPI_ASSERT(RWCHC_SPIC_SETTINGSS, RWCHC_SPIC_VALID))
		goto out;
	
	if (!SPI_ASSERT(RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_SETTINGSS))
		goto out;
	
	ret = 0;
out:
	return ret;
}

/**
 * Reset the device
 */
void rwchcd_spi_reset(void)
{
	SPI_RESYNC();
	SPI_rw8bit(RWCHC_SPIC_RESET);
}

/**
 * Init spi subsystem
 * @return fd or error code
 */
int rwchcd_spi_init(void)
{
	return (wiringPiSPISetupMode(SPICHAN, SPISPEED, SPIMODE));
}
