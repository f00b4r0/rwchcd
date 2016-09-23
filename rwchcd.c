//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

//* TODO
// Setup
// Computation
// Control
// accounting (separate module that periodically polls states and write them to timestamped registry)
// Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
// UI + programming
// handle summer switchover
// connection of multiple instances

// http://www.energieplus-lesite.be/index.php?id=10963


#include <unistd.h>	// sleep/usleep
#include <stdlib.h>	// exit
#include "rwchcd.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_config.h"
#include "rwchcd_runtime.h"
#include "rwchcd_spi.h"

/**
 * Implement time-based PI controller in velocity form
 * Saturation : max = boiler temp, min = return temp
 * We want to output
 http://www.plctalk.net/qanda/showthread.php?t=19141
 // http://www.energieplus-lesite.be/index.php?id=11247
 // http://www.ferdinandpiette.com/blog/2011/08/implementer-un-pid-sans-faire-de-calculs/
 // http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/
 // http://controlguru.com/process-gain-is-the-how-far-variable/
 // http://www.rhaaa.fr/regulation-pid-comment-la-regler-12
 // http://controlguru.com/the-normal-or-standard-pid-algorithm/
 // http://www.csimn.com/CSI_pages/PIDforDummies.html
 // https://en.wikipedia.org/wiki/PID_controller
 */
#if 0
static float control_PI(const float target, const float actual)
{
	float Kp, Ki;	// XXX PID settings
	float error, error_prev, error_change, output, iterm;
	static float iterm_prev, error_prev, output_prev, prev;

	error = target - actual;

	// Integral term
	iterm = Ki * error;

	// Proportional term
	pterm = Kp * (prev - actual);
	prev = actual;

	output = iterm + pterm + output_prev;
	output_prev = output;

	return (output);
}
#endif


static int init_process()
{
	struct s_runtime * restrict const runtime = get_runtime();
	struct s_config * restrict config = NULL;
	struct s_plant * restrict plant = NULL;
	struct s_heatsource * restrict heatsource = NULL;
	struct s_heating_circuit * restrict circuit = NULL;
	struct s_dhw_tank * restrict dhwt = NULL;
	struct s_boiler_priv * restrict boiler = NULL;
	int ret;

	/* init hardware */
	
	ret = hardware_init();
	if (ret) {
		dbgerr("hardware init error: %d", ret);
		return (ret);
	}

	/* init config */

	config = config_new();
	ret = config_init(config);
	if (ret) {
		dbgerr("config init error: %d", ret);
		return (ret);
	}
	config_set_building_tau(config, 10 * 60 * 60);	// XXX 10 hours
	config_set_nsensors(config, 4);	// XXX 4 sensors
	config_set_outdoor_sensorid(config, 1);
	config_set_tfrostmin(config, celsius_to_temp(5));	// XXX frost protect at 5C
	config_set_tsummer(config, celsius_to_temp(18));	// XXX summer switch at 18C
	config->configured = true;

	// attach config to runtime
	runtime->config = config;

	/* init plant */

	// create a new plant
	plant = plant_new();
	if (!plant) {
		dbgerr("plant creation failed");
		return (-EOOM);
	}

	// create a new heat source for the plant
	heatsource = plant_new_heatsource(plant, BOILER);
	if (!heatsource) {
		dbgerr("heatsource creation failed");
		return (-EOOM);
	}

	// configure that source	XXX REVISIT
	boiler = heatsource->priv;
	boiler->histeresis = delta_to_temp(5);
	boiler->limit_tmax = celsius_to_temp(90);
	boiler->limit_tmin = celsius_to_temp(45);
	boiler->id_temp = 2;	// XXX VALIDATION
	boiler->id_temp_outgoing = boiler->id_temp;
	boiler->burner_1 = hardware_relay_new();
	if (!boiler->burner_1) {
		dbgerr("burner relay creation failed");
		return (-EOOM);
	}
	hardware_relay_set_id(boiler->burner_1, 13);	// XXX first relay
	boiler->burner_1->configured = true;
	boiler->set_burner_min_time = 2 * 60;	// XXX 2 minutes
	boiler->set_sleeping_time = 2 * 24 * 60 * 60;	// XXX 2 days
	heatsource->configured = true;

	// create a new circuit for the plant
	circuit = plant_new_circuit(plant);
	if (!circuit) {
		dbgerr("circuit creation failed");
		return (-EOOM);
	}

	// configure that circuit
	circuit->limit_wtmax = celsius_to_temp(85);
	circuit->limit_wtmin = celsius_to_temp(20);
	circuit->set_tcomfort = celsius_to_temp(20.5F);
	circuit->set_teco = celsius_to_temp(16);
	circuit->set_tfrostfree = celsius_to_temp(7);
	circuit->set_outhoff_comfort = circuit->set_tcomfort - delta_to_temp(4);
	circuit->set_outhoff_eco = circuit->set_teco - delta_to_temp(4);
	circuit->set_outhoff_frostfree = circuit->set_tfrostfree - delta_to_temp(4);
	circuit->set_outhoff_histeresis = delta_to_temp(1);
	circuit->id_temp_outgoing = 3;	// XXX VALIDATION
	circuit->id_temp_return = 4;	// XXX VALIDATION
	circuit->set_temp_inoffset = delta_to_temp(10);
	circuit->tlaw_data.tout1 = celsius_to_temp(-5);
	circuit->tlaw_data.twater1 = celsius_to_temp(70);
	circuit->tlaw_data.tout2 = celsius_to_temp(15);
	circuit->tlaw_data.twater2 = celsius_to_temp(35);
	circuit_make_linear(circuit);

	// create a valve for that circuit
	circuit->valve = valve_new();
	if (!circuit->valve) {
		dbgerr("valve creation failed");
		return (-EOOM);
	}

	// configure that valve
	circuit->valve->deadzone = delta_to_temp(2);
	circuit->valve->ete_time = 120;	// XXX 120 s
	circuit->valve->id_temp1 = boiler->id_temp_outgoing;
	circuit->valve->id_temp2 = circuit->id_temp_return;
	circuit->valve->id_tempout = circuit->id_temp_outgoing;
	valve_make_linear(circuit->valve);

	// create and configure two relays for that valve
	circuit->valve->open = hardware_relay_new();
	hardware_relay_set_id(circuit->valve->open, 1);
	circuit->valve->open->configured = true;

	circuit->valve->close = hardware_relay_new();
	hardware_relay_set_id(circuit->valve->close, 2);
	circuit->valve->close->configured = true;

	circuit->valve->configured = true;

	// create a pump for that circuit
	circuit->pump = pump_new();
	if (!circuit->pump) {
		dbgerr("pump creation failed");
		return (-EOOM);
	}

	// configure that pump
	circuit->pump->set_cooldown_time = 10 * 60;	// XXX 10 minutes

	// create and configure a relay for that pump
	circuit->pump->relay = hardware_relay_new();
	hardware_relay_set_id(circuit->pump->relay, 3);
	circuit->pump->relay->configured = true;

	circuit->pump->configured = true;

	circuit->configured = true;

	// create a new DHWT for the plant
	dhwt = plant_new_dhwt(plant);
	if (!dhwt) {
		dbgerr("dhwt creation failed");
		return (-EOOM);
	}

	// configure that dhwt
	dhwt->id_temp_bottom = boiler->id_temp;
	dhwt->limit_tmin = celsius_to_temp(5);
	dhwt->limit_tmax = celsius_to_temp(60);
	dhwt->limit_wintmax = celsius_to_temp(90);
	dhwt->set_tcomfort = celsius_to_temp(50);
	dhwt->set_teco = celsius_to_temp(40);
	dhwt->set_tfrostfree = celsius_to_temp(10);	// XXX REVISIT RELATIONS BETWEEN TEMPS
	dhwt->histeresis = delta_to_temp(10);
	dhwt->set_temp_inoffset = delta_to_temp(0);	// Integrated tank
	dhwt->configured = true;


	plant->configured = true;

	runtime->plant = plant;
	
	// set valves to known start state
	//set_mixer_pos(&Valve, -1);	// force fully closed during more than normal ete_time

	runtime_run();	// XXX to gather sensors

	// finally bring the plant online
	return (plant_online(plant));
}

/*
 temp conversion from sensor raw value + calibration
 temp boiler: target water temp + hist; ceil and floor
 temp water: water curve with outdoor temp + timing (PID?) comp (w/ building constant) + indoor comp
 valve position: PID w/ total run time from C to O
 */

int main(void)
{
	struct s_runtime * restrict const runtime = get_runtime();
	enum e_systemmode cursysmode;
	int ret;

	if (init_process() != ALL_OK)
		(0);	//exit(1);

	while (1) {
		// test read peripherals
		ret = hardware_rwchcperiphs_read(&(runtime->rWCHC_peripherals));
		if (ret)
			return (-ESPI);

		if (runtime->rWCHC_peripherals.RQ) {
			cursysmode = runtime->systemmode;
			cursysmode++;
			runtime->rWCHC_peripherals.RQ = 0;

			if (cursysmode > SYS_DHWONLY)
				cursysmode = SYS_OFF;

			rwchcd_spi_lcd_acquire();
			rwchcd_spi_lcd_cmd_w(0x2);	// home
			switch (cursysmode) {
				case SYS_OFF:
					lcd_wstr("Off      ");
					break;
				case SYS_AUTO:
					lcd_wstr("Auto     ");
					break;
				case SYS_COMFORT:
					lcd_wstr("Comfort  ");
					break;
				case SYS_ECO:
					lcd_wstr("Eco      ");
					break;
				case SYS_FROSTFREE:
					lcd_wstr("Frostfree");
					break;
				case SYS_DHWONLY:
					lcd_wstr("DHW Only ");
					break;
				case SYS_MANUAL:
					lcd_wstr("Manual   ");
					break;
			}
			rwchcd_spi_lcd_relinquish();
			runtime_set_systemmode(cursysmode);
		}

		ret = hardware_rwchcperiphs_write(&(runtime->rWCHC_peripherals));
		if (ret)
			return (-ESPI);

		ret = runtime_run();
		if (ret)
			dbgmsg("runtime_run returned: %d", ret);
		
		sleep(1);
	}
}
