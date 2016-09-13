//
//  rwchcd_runtime.h
//  rwchcd
//
//  Created by Thibaut VARENE on 13/09/16.
//
//

#ifndef rwchcd_runtime_h
#define rwchcd_runtime_h

#include "rwchcd.h"

struct s_runtime * get_runtime(void);
temp_t get_temp(const tempid_t id);

#endif /* rwchcd_runtime_h */
