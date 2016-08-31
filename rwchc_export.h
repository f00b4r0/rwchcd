/* 
 * File:   
 * Author: 
 * Comments:
 * Revision history: 
 */

#ifndef RWCHC_EXPORT_H
#define	RWCHC_EXPORT_H

#define RWCHC_SPIC_RELAYRL	0b00010000
#define RWCHC_SPIC_RELAYRH	0b00010001
#define RWCHC_SPIC_RELAYWL	0b00010100
#define RWCHC_SPIC_RELAYWH	0b00010101
#define RWCHC_SPIC_LCDCMDR	0b00100000
#define RWCHC_SPIC_LCDDATR	0b00100001
#define RWCHC_SPIC_LCDBKLR	0b00100010
#define RWCHC_SPIC_LCDCMDW	0b00100100
#define RWCHC_SPIC_LCDDATW	0b00100101
#define RWCHC_SPIC_LCDBKLW	0b00100110
#define RWCHC_SPIC_PERIPHSR	0b01000000
#define RWCHC_SPIC_PERIPHSW	0b01100000
#define RWCHC_SPIC_SETTINGSR	0x80
#define RWCHC_SPIC_SETTINGSW	0x81
#define RWCHC_SPIC_SETTINGSS	0x82
#define RWCHC_SPIC_LCDRLQSH	0x90
#define RWCHC_SPIC_LCDACQR	0x91
#define RWCHC_SPIC_RESET	0xF0

#define RWCHC_SPIC_KEEPALIVE	0xAA
#define RWCHC_SPIC_VALID	0x66
#define RWCHC_SPIC_INVALID	0x99

union u_relays {
	struct {
		unsigned T1	:1;
		unsigned T2	:1;
		unsigned T3	:1;
		unsigned T4	:1;
		unsigned T5	:1;
		unsigned T6	:1;
		unsigned T7	:1;
		unsigned	:1;
		
		unsigned T8	:1;
		unsigned T9	:1;
		unsigned T10	:1;
		unsigned T11	:1;
		unsigned T12	:1;
		unsigned RL1	:1;
		unsigned RL2	:1;
		unsigned	:1;
	};
	struct {
		uint8_t LOWB;	///< access low bank (T1-T7)
		uint8_t HIGHB;	///< access high bank (T8-RL2)
	} __attribute__ ((packed));
};

#define OUTPERIPHMASK   0x7
union u_outperiphs {
	struct {
		unsigned LED2	:1;	///< LED2 = alarm (LED1 is the system's heartbeat)
		unsigned buzzer	:1;
		unsigned LCDbl	:1;
		unsigned	:1;	// one bit left available
		unsigned	:1;	// one bit left available
		unsigned	:3;	// DO NOT USE
	};
	uint8_t BYTE;
};

/**
 * nibble pairs for sensors/actuators addresses. 0xF is invalid
 * For relays (T), bit 3 is bank, bit 2-0 is actual address
 * @warning Never energize T_Vopen and T_Vclose at the same time!
 */
struct s_addresses {
	unsigned T_burner	:4;	// for overtemp/fallback
	unsigned T_pump		:4;	// for overtemp/fallback
	unsigned T_Vopen	:4;	// for overtemp/fallback
	unsigned T_Vclose	:4;	// for fallback
	unsigned S_burner	:4;	// for overtemp/fallback
	unsigned S_water	:4;	// for fallback
	unsigned S_outdoor	:4;	// for fallback
	unsigned nsensors	:4;	///< last connected sensor: max 14
};

/* limit values, temperatures in C */
struct s_limits {
	uint8_t	burner_tmax;	///< maximum allowable burner temperature
	uint8_t burner_tmin;	///< minimum burner temperature (for fallback)
	uint8_t water_tmin;	///< minimum heatpipe water temp (for fallback)
	int8_t frost_tmin;	///< minimum outdoor temp for triggering frost protect
} __attribute__ ((packed));

/* settings pair */
struct s_settings {
	uint8_t lcdblpct;	///< LCD backlight duty cycle in percent (0=off)
	struct s_limits limits;
	struct s_addresses addresses;
} __attribute__ ((packed));

#define NTSENSORS   16

#endif	/* RWCHC_EXPORT_H */

