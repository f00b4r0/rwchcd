//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

// Setup
// Computation
// Control + accounting
// UI + programming


#include <stdio.h>
#include <unistd.h>	// sleep/usleep
#include "rwchcd_spi.h"


static uint16_t TSENSORS[NTSENSORS];
static union u_relays rWCHC_relays;
static union u_outperiphs rWCHC_peripherals;
static struct s_settings rWCHC_settings;

/**
 * Read all sensors
 */
static int sensors_read(uint16_t tsensors[], int last)
{
	int sensor, ret = -1;
	
	for (sensor=0; sensor<last; sensor++) {
		if (rwchcd_spi_sensor_r(tsensors, sensor))
			goto out;
	}
	
	if (rwchcd_spi_sensor_r(tsensors, NTSENSORS-1))	// grab reference
		goto out;
	
	ret = 0;
out:
	return ret;
}

/**
 * Write a string to LCD.
 * @warning No boundary checks
 * @param str string to send
 * @return error code
 */
static int lcd_wstr(const char * str)
{
	int ret = -1;
	
	while (*str != '\0') {
		if (rwchcd_spi_lcd_data_w(*str))
			goto out;
		str++;
		//usleep(100); DISABLED: SPI_rw8bit() already sleeps
	}
	
	ret = 0;
out:
	return ret;
}


/*
 temp conversion from sensor raw value
 temp burner: target water temp + hist; ceil and floor
 temp water: water curve with outdoor temp + timing (PID?) comp (w/ building constant) + indoor comp
 valve position: PID w/ total run time from C to O
 */

int main(void)
{
	int i, ret;
	
	if (rwchcd_spi_init() < 0)
		printf("init error\n");
	
	 ret = rwchcd_spi_lcd_acquire();
	 printf("rwchcd_spi_lcd_acquire: %d\n", ret);
	 
	 ret = rwchcd_spi_lcd_cmd_w(0x1);	// clear
	 printf("rwchcd_spi_lcd_cmd_w: %d\n", ret);
	 sleep(2);
	 ret = lcd_wstr("Hello!");
	 printf("lcd_wstr: %d\n", ret);
	 
	 ret = rwchcd_spi_lcd_relinquish();
	 printf("rwchcd_spi_lcd_relinquish: %d\n", ret);
	
	rWCHC_peripherals.LCDbl = 0;
	ret = rwchcd_spi_peripherals_w(&rWCHC_peripherals);
	printf("rwchcd_spi_peripherals_w: %d\n", ret);
	
#define S rWCHC_settings
	while (1) {
		//i = rwchcd_spi_peripherals_r();
		//printf("periph byte: %d, ret: %d\n", rWCHC_peripherals.BYTE, i);
		i=0;
		/*
		 ret = rwchcd_spi_settings_r(&rWCHC_settings);
		 printf("rwchcd_spi_settings_r: %d\n", ret);
		 printf("settings: %d; %d, %d, %d, %d;\n"
		 "\t%x, %x, %x, %x, %x, %x, %x, %x\n",
		 S.lcdblpct, S.limits.burner_tmax, S.limits.burner_tmin,
		 S.limits.water_tmin, S.limits.frost_tmin,
		 S.addresses.T_burner, S.addresses.T_pump,
		 S.addresses.T_Vopen, S.addresses.T_Vclose,
		 S.addresses.S_burner, S.addresses.S_water,
		 S.addresses.S_outdoor, S.addresses.nsensors);
		 */
		//		rWCHC_settings.lcdblpct = 30;
		
		/*		do {
			ret = rwchcd_spi_settings_w(&rWCHC_settings);
			printf("rwchcd_spi_settings_w: %d\n", ret);
		 } while (ret);
		 //ret = rwchcd_spi_settings_s();
		 //printf("rwchcd_spi_settings_s: %d\n", ret);
		 */
		
		 for (i=0; i<128; i++) {
			 //	ret = rwchcd_spi_lcd_bl_w(i);
			 //printf("rwchcd_spi_lcd_bl_w: %d\n", ret);
		 
			rWCHC_relays.LOWB = i;
			rWCHC_relays.HIGHB= i;
		 again:
			printf("LOWB: %x, HIGHB: %x\n", rWCHC_relays.LOWB, rWCHC_relays.HIGHB);
			ret = rwchcd_spi_relays_w(&rWCHC_relays);
			printf("rwchcd_spi_relays_w: %d\n", ret);
			if (ret) {
				printf("TRYING AGAIN!\n");
				goto again;
			}
			 //rWCHC_peripherals.LCDbl = (i&1);
			 //ret = rwchcd_spi_peripherals_w(&rWCHC_peripherals);
			 //printf("rwchcd_spi_peripherals_w: %d\n", ret);
			sleep(1);
		 }
		//for (i=100; i>0; i--)
		//	rwchcd_spi_lcd_bl_w(i);
		
		ret = sensors_read(TSENSORS, 0xF);
		printf("sensors_read: %d\n", ret);
		if (!ret)
			for (i=0; i<NTSENSORS; i++)
				printf("sensor %d: %d\n", i, TSENSORS[i]);
		sleep(1);
	}
}
