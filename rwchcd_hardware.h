//
//  rwchcd_hardware.h
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#ifndef rwchcd_hardware_h
#define rwchcd_hardware_h

struct s_stateful_relay {
	bool configured;
	unsigned short id;	///< id matching hardware: 1 to 14, with 13==RL1 and 14==RL2
	bool is_on;		///< relay currently active
	time_t on_since;	// XXX these variable should really be handled by the actual hardware call
	time_t on_time;
	time_t off_since;
	time_t off_time;
	char * name;
};


#endif /* rwchcd_hardware_h */
