//
//  hw_backends/hw_p1/hw_p1_spi.c
//  rwchcd
//
//  (C) 2016-2018,2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * SPI protocol implementation for rWCHC Prototype 1 hardware.
 *
 * The SPI logic and code flow must ensure that the firmware will never be left
 * in a dangling state where an ongoing SPI call is interrupted.
 * Thus, most of the functions here expect things to go well and
 * flag if they don't. The point is that we must not interrupt
 * the flow even if there is a mistransfer, since the firmware expects
 * a full transfer regardless of errors.
 *
 * @note the LCD operations assume fixed timings: although we could query the
 * hardware to confirm completion of the operation, it would typically be slower
 * due to the embedded delay in SPI_rw8bit().
 *
 * https://www.raspberrypi.org/documentation/hardware/raspberrypi/spi/README.md
 *
 * @warning this implementation is NOT thread safe: callers must ensure proper synchronization.
 */

#include <unistd.h>	// sleep/usleep
#include <assert.h>
#include <wiringPiSPI.h>

#include "hw_p1_spi.h"
#include "rwchcd.h"	// for error codes

#define SPIDELAYUS	100		///< time (us) between 2 consecutive SPI exchanges: 100us -> 10kchar/s SPI rate, allows 800 ISNS on the PIC
#define SPIRESYNCMAX	250		///< max resync tries -> terminal delay ~125ms including 100us SPIDELAYUS for each exchange
/**
 * SPI Mode.
 * See https://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase
 * @verbatim
 Standard SPI Mode | Microchip PIC
 Terminology       | Control Bits
 Using CPOL,CPHA   |   CKP CKE
 ------------------+--------------
      0,0 (0)      |    0   1
      0,1 (1)      |    0   0
      1,0 (2)      |    1   1
      1,1 (3)      |    1   0
 @endverbatim
 */
#define SPIMODE		3		///< https://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase

#define USLEEPLCDFAST	50		///< expected completion time (us) for most LCD ops
#define USLEEPLCDSLOW	2000		///< expected completion time (us) for clear/home cmds

#define SPI_ASSERT(spi, emit, expect)	(SPI_rw8bit(spi, emit) == (uint8_t)expect)	///< send emit and ensure we received expect

/**
 * SPI resync routine.
 * This routine ensure we enter the atomic SPI ops in firmware.
 * It uses an exponential back-off delay after each retry, starting from 0
 * (and thus only applying the embedded delay of SPI_rw8bit()), up to a terminal
 * delay of 1ms (4*SPIRESYNCMAX microseconds) on the last run.
 * With SPIRESYNCMAX=250, this translates to a standalone accumulated delay of
 * approximately 100ms. Adding the embedded delay of SPI_rw8bit() (100us), this adds
 * 25ms to this number.
 */
#define SPI_RESYNC(spi, cmd)								\
		({								\
		spi->run.spitout = SPIRESYNCMAX;						\
		while ((SPI_rw8bit(spi, RWCHC_SPIC_SYNCREQ) != RWCHC_SPIC_SYNCACK) && spi->run.spitout) usleep((SPIRESYNCMAX - spi->run.spitout--)*4);	\
		if (spi->run.spitout) SPI_rw8bit(spi, cmd);	/* consumes the last SYNCACK */	\
		})

/**
 * Exchange 8bit data over SPI.
 * @param spi HW P1 spi private data
 * @param data data to send
 * @return data received
 */
static uint8_t SPI_rw8bit(const struct s_hw_p1_spi * const spi, const uint8_t data)
{
	uint8_t exch = data;
	wiringPiSPIDataRW(spi->set.chan, &exch, 1);
	usleep(SPIDELAYUS);
	return exch;
}

/**
 * Send a keepalive and verify the response.
 * Can be used e.g. at initialization time to ensure that there is a device connected:
 * if this function fails more than a reasonnable number of tries then there's a good
 * chance the device is not connected.
 * Delay: none
 * @param spi HW P1 spi private data
 * @return error status
 */
int hw_p1_spi_keepalive(struct s_hw_p1_spi * const spi)
{
	SPI_RESYNC(spi, RWCHC_SPIC_KEEPALIVE);

	if (SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_ALIVE))
		return (ALL_OK);
	else
		return (-ESPI);
}

static int _spi_fwversion(struct s_hw_p1_spi * const spi)
{
	int ret;

	SPI_RESYNC(spi, RWCHC_SPIC_FWVERSION);

	if (!spi->run.spitout)
		return (-ESPI);

	ret = SPI_rw8bit(spi, RWCHC_SPIC_KEEPALIVE);

	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_FWVERSION))
		ret = -ESPI;

	return ret;
}

static int _spi_hwversion(struct s_hw_p1_spi * const spi)
{
	int ret;

	SPI_RESYNC(spi, RWCHC_SPIC_HWVERSION);

	if (!spi->run.spitout)
		return (-ESPI);

	ret = SPI_rw8bit(spi, RWCHC_SPIC_KEEPALIVE);

	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_HWVERSION))
		ret = -ESPI;

	return ret;
}

/**
 * Retrieve firmware version number.
 * Delay: none
 * @param spi HW P1 spi private data
 * @return negative error code or positive version number
 */
int hw_p1_spi_fwversion(struct s_hw_p1_spi * const spi)
{
	if (spi->run.FWversion <= 0)
		spi->run.FWversion = _spi_fwversion(spi);

	return (spi->run.FWversion);
}

/**
 * Retrieve hardware version number.
 * Delay: none
 * @param spi HW P1 spi private data
 * @return negative error code or positive version number
 */
int hw_p1_spi_hwversion(struct s_hw_p1_spi * const spi)
{
	if (spi->run.HWversion <= 0)
		spi->run.HWversion = _spi_hwversion(spi);

	return (spi->run.HWversion);
}

/**
 * Acquire control over LCD display.
 * Delay: none
 * @param spi HW P1 spi private data
 * @return error code
 */
int hw_p1_spi_lcd_acquire(struct s_hw_p1_spi * const spi)
{
	int ret = ALL_OK;
	
	SPI_RESYNC(spi, RWCHC_SPIC_LCDACQR);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_LCDACQR))
		ret = -ESPI;

	return ret;
}

/**
 * Relinquish control over LCD display (to embedded firmware).
 * Delay: none
 * @param spi HW P1 spi private data
 * @return error code
 */
int hw_p1_spi_lcd_relinquish(struct s_hw_p1_spi * const spi)
{
	int ret = ALL_OK;
	
	SPI_RESYNC(spi, RWCHC_SPIC_LCDRLQSH);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_LCDRLQSH))
		ret = -ESPI;

	return ret;
}

/**
 * Request LCD backlight fadeout.
 * Delay: none
 * @param spi HW P1 spi private data
 * @return error code
 */
int hw_p1_spi_lcd_fade(struct s_hw_p1_spi * const spi)
{
	int ret = ALL_OK;

	SPI_RESYNC(spi, RWCHC_SPIC_LCDFADE);

	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_LCDFADE))
		ret = -ESPI;

	return ret;
}

/**
 * Write LCD command byte.
 * Delay: LCD op execution time after command is sent
 * @param spi HW P1 spi private data
 * @param cmd command byte to send
 * @return error code
 */
int hw_p1_spi_lcd_cmd_w(struct s_hw_p1_spi * const spi, const uint8_t cmd)
{
	int ret = ALL_OK;
	
	SPI_RESYNC(spi, RWCHC_SPIC_LCDCMDW);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, cmd, ~RWCHC_SPIC_LCDCMDW))
		ret = -ESPI;

	if (cmd & 0xFC)	// quick commands
		usleep(USLEEPLCDFAST);
	else	// clear/home
		usleep(USLEEPLCDSLOW);
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, cmd))
		ret = -ESPI;

	return ret;
}

/**
 * Write LCD data byte.
 * Delay: LCD op execution time after data is sent
 * @param spi HW P1 spi private data
 * @param data data byte to send
 * @return error code
 */
int hw_p1_spi_lcd_data_w(struct s_hw_p1_spi * const spi, const uint8_t data)
{
	int ret = ALL_OK;
	
	SPI_RESYNC(spi, RWCHC_SPIC_LCDDATW);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, data, ~RWCHC_SPIC_LCDDATW))
		ret = -ESPI;

	usleep(USLEEPLCDFAST);	// wait for completion
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, data))
		ret = -ESPI;

	return ret;
}

/**
 * Write LCD backlight duty cycle. Will not be committed
 * to eeprom.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param percent backlight duty cycle in percent
 * @return error code
 */
int hw_p1_spi_lcd_bl_w(struct s_hw_p1_spi * const spi, const uint8_t percent)
{
	int ret = ALL_OK;
	
	if (percent > 100)
		return (-EINVALID);
	
	SPI_RESYNC(spi, RWCHC_SPIC_LCDBKLW);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, percent, ~RWCHC_SPIC_LCDBKLW))
		ret = -ESPI;
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, percent))
		ret = -ESPI;

	return ret;
}

/**
 * Read peripheral states.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param periphs pointer to struct whose values will be populated to match current states if no error occurs
 * @return error code
 */
int hw_p1_spi_peripherals_r(struct s_hw_p1_spi * const spi, union rwchc_u_periphs * const periphs)
{
	int ret = ALL_OK;
	uint8_t byte;
	
	assert(periphs);
	
	SPI_RESYNC(spi, RWCHC_SPIC_PERIPHSR);
	
	if (!spi->run.spitout)
		return (-ESPI);

	byte = SPI_rw8bit(spi, RWCHC_SPIC_KEEPALIVE);

	if (!SPI_ASSERT(spi, (uint8_t)~byte, ~RWCHC_SPIC_PERIPHSR))
		ret = -ESPI;

	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_PERIPHSR))
		ret = -ESPI;

	if (ALL_OK == ret)
		periphs->BYTE = byte;

	return ret;
}

/**
 * Write peripheral states.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param periphs pointer to struct whose values are populated with desired states
 * @return error code
 */
int hw_p1_spi_peripherals_w(struct s_hw_p1_spi * const spi, const union rwchc_u_periphs * const periphs)
{
	int ret = ALL_OK;
	
	assert(periphs);
	
	SPI_RESYNC(spi, RWCHC_SPIC_PERIPHSW);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	if (!SPI_ASSERT(spi, periphs->BYTE, ~RWCHC_SPIC_PERIPHSW))
		ret = -ESPI;
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, periphs->BYTE))
		ret = -ESPI;
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_PERIPHSW))
		ret = -ESPI;
	
	return ret;
}

/**
 * Read relay states.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param relays pointer to struct whose values will be populated to match current states if no error occurs
 * @return error code
 */
int hw_p1_spi_relays_r(struct s_hw_p1_spi * const spi, union rwchc_u_relays * const relays)
{
	int ret = ALL_OK;
	uint8_t lowb, highb;

	assert(relays);

	SPI_RESYNC(spi, RWCHC_SPIC_RELAYRL);

	if (!spi->run.spitout)
		return (-ESPI);

	lowb = SPI_rw8bit(spi, RWCHC_SPIC_KEEPALIVE);
	highb = SPI_rw8bit(spi, RWCHC_SPIC_KEEPALIVE);

	if (!SPI_ASSERT(spi, (lowb^highb), ~RWCHC_SPIC_RELAYRL))
		ret = -ESPI;

	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_RELAYRL))
		ret = -ESPI;

	if (ALL_OK == ret) {
		relays->LOWB = lowb;
		relays->HIGHB = highb;
	}

	return (ret);
}

/**
 * Write relay states.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param relays pointer to struct whose values are populated with desired states
 * @return error code
 */
int hw_p1_spi_relays_w(struct s_hw_p1_spi * const spi, const union rwchc_u_relays * const relays)
{
	int ret = ALL_OK;
	uint8_t temp;	// work around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=38341

	assert(relays);

	SPI_RESYNC(spi, RWCHC_SPIC_RELAYWL);

	if (!spi->run.spitout)
		return (-ESPI);

	if (!SPI_ASSERT(spi, relays->LOWB, ~RWCHC_SPIC_RELAYWL))
		ret = -ESPI;

	temp = (uint8_t)~relays->LOWB;
	if (!SPI_ASSERT(spi, relays->HIGHB, temp))
		ret = -ESPI;

	temp = (uint8_t)~relays->HIGHB;
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, temp))
		ret = -ESPI;

	if (ALL_OK == ret) {
		if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_KEEPALIVE))
			ret = -ESPI;
	}
	else
		SPI_rw8bit(spi, RWCHC_SPIC_INVALID);

	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, RWCHC_SPIC_RELAYWL))
		ret = -ESPI;

	return (ret);
}

/**
 * 1-wire style CRC function.
 */
static inline uint8_t crc1w(uint8_t byte, uint8_t crc)
{
	uint8_t b;

	crc ^= byte;
	for (b = 8; b > 0; b--)
		crc = (crc & 0x01) ? (crc >> 1U) ^ RWCHC_CRC_POLY : (crc >> 1U);

	return crc;
}

/**
 * Receive a block of CRC'd data over SPI.
 * @param spi HW P1 spi private data
 * @param dst pointer to target for data
 * @param len length of data to receive
 * @return exec status
 */
static int _spi_crc_recv(const struct s_hw_p1_spi * const spi, uint8_t * const dst, int len)
{
	uint8_t byte, crc, i;

	for (crc = 0, i = 0; i < len; i++) {
		byte = SPI_rw8bit(spi, i);
		crc = crc1w(byte, crc);
		dst[i] = byte;
	}

	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, crc))
		return -ESPI;
	else
		return ALL_OK;
}

/**
 * Read all sensors.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param sensors pointer to array whose values will be populated to match current sensors values
 * @return error code
 */
int hw_p1_spi_sensors_r(struct s_hw_p1_spi * const spi, rwchc_sensor_t * const sensors)
{
	assert(sensors);

	SPI_RESYNC(spi, RWCHC_SPIC_SENSORSR);

	if (!spi->run.spitout)
		return (-ESPI);

	return _spi_crc_recv(spi, (uint8_t *)sensors, RWCHC_NTSENSORS*sizeof(*sensors));
}

/**
 * Read all calibration references.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param refs pointer to array whose values will be populated to match current refs values
 * @return error code
 */
int hw_p1_spi_refs_r(struct s_hw_p1_spi * const spi, rwchc_sensor_t * const refs)
{
	assert(refs);

	SPI_RESYNC(spi, RWCHC_SPIC_REFSR);

	if (!spi->run.spitout)
		return (-ESPI);

	return _spi_crc_recv(spi, (uint8_t *)refs, RWCHC_NTREFS*sizeof(*refs));
}

/**
 * Read current ram settings.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param settings pointer to struct whose values will be populated to match current settings
 * @return error code
 */
int hw_p1_spi_settings_r(struct s_hw_p1_spi * const spi, struct rwchc_s_settings * const settings)
{
	assert(settings);

	SPI_RESYNC(spi, RWCHC_SPIC_SETTINGSR);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	return _spi_crc_recv(spi, (uint8_t *)settings, sizeof(*settings));
}

/**
 * Write current ram settings.
 * Delay: none
 * @param spi HW P1 spi private data
 * @param settings pointer to struct whose values are populated with desired settings
 * @return error code
 */
int hw_p1_spi_settings_w(struct s_hw_p1_spi * const spi, const struct rwchc_s_settings * const settings)
{
	unsigned int i;
	int ret = ALL_OK;
	
	assert(settings);
	
	SPI_RESYNC(spi, RWCHC_SPIC_SETTINGSW);
	
	if (!spi->run.spitout)
		return (-ESPI);
	
	for (i=0; i<sizeof(*settings); i++)
		if (SPI_rw8bit(spi, *((const uint8_t *)settings+i)) != i)
			ret = -ESPI;
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_SETTINGSW))
		ret = -ESPI;

	return ret;
}

/**
 * Save current ram settings to eeprom.
 * Delay: 100ms
 * @param spi HW P1 spi private data
 * @return error code
 */
int hw_p1_spi_settings_s(struct s_hw_p1_spi * const spi)
{
	int ret = ALL_OK;
	
	SPI_RESYNC(spi, RWCHC_SPIC_SETTINGSS);
	
	if (!spi->run.spitout)
		return (-ESPI);

	usleep(100000);	// give time to write the flash
	
	if (!SPI_ASSERT(spi, RWCHC_SPIC_KEEPALIVE, ~RWCHC_SPIC_SETTINGSS))
		ret = -ESPI;
	
	return ret;
}

/**
 * Reset the device.
 * Delay: none (device unavailable until fully restarted: 1-2s delay would be reasonable)
 * @param spi HW P1 spi private data
 * @return exec status (ALL_OK if reset is presumably successful)
 */
int hw_p1_spi_reset(struct s_hw_p1_spi * const spi)
{
	static const uint8_t trig[] = RWCHC_RESET_TRIGGER;
	unsigned int i;
	int ret = ALL_OK;

	SPI_RESYNC(spi, RWCHC_SPIC_RESET);

	if (!spi->run.spitout)
		return (-ESPI);
	
	for (i=0; i<ARRAY_SIZE(trig); i++)
		if (SPI_rw8bit(spi, trig[i]) != i)
			ret = -ESPI;

	return ret;
}

/**
 * Setup spi subsystem
 * @param spi HW P1 spi private data
 * @return exec status or error code
 * @note needs root
 */
int hw_p1_spi_setup(struct s_hw_p1_spi * const spi)
{
	int ret;

	ret = wiringPiSPISetupMode(spi->set.chan, spi->set.clock, SPIMODE);

	if (ret >= 0)
		ret = _spi_fwversion(spi);

	if (ret >= 0) {
		spi->run.FWversion = ret;
		ret = ALL_OK;
	}

	return (ret);
}
