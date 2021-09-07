//
//  plant/dhwt.h
//  rwchcd
//
//  (C) 2017,2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT operation API.
 */

#ifndef dhwt_h
#define dhwt_h

struct s_dhwt;

int dhwt_online(struct s_dhwt * const dhwt) __attribute__((warn_unused_result));
int dhwt_offline(struct s_dhwt * const dhwt);
int dhwt_run(struct s_dhwt * const dhwt) __attribute__((warn_unused_result));
void dhwt_cleanup(struct s_dhwt * restrict dhwt);

#endif /* dhwt_h */
