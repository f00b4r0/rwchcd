//
//  rwchcd_runtime.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#ifndef rwchcd_runtime_h
#define rwchcd_runtime_h

#include "rwchcd.h"

struct s_runtime * get_runtime(void);
void runtime_init(void);
int runtime_set_systemmode(enum e_systemmode sysmode);
int runtime_set_runmode(enum e_runmode runmode);
int runtime_set_dhwmode(enum e_runmode dhwmode);
int runtime_run(void);

#endif /* rwchcd_runtime_h */
