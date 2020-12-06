//
//  hw_backends/hw_lib.c
//  rwchcd
//
//  (C) 2019-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware-related functions library.
 * @note Uses floating point arithmetic. Lookup tables can be implemented instead if fp is not available.
 */

#include <stdint.h>
#include <math.h>	// sqrtf

#include "hw_lib.h"


// http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/platinum-rtd-sensors/resistance-calibration-table

/**
 * Convert resistance value to actual temperature based on Callendar - Van Dusen.
 * Use a quadratic fit for simplicity.
 * - http://aviatechno.net/thermo/rtd03.php
 * - https://www.newport.com/medias/sys_master/images/images/h4b/h16/8797291446302/TN-RTD-1-Callendar-Van-Dusen-Equation-and-RTD-Temperature-Sensors.pdf
 * - Rt = R0 + R0*alpha*[t - delta*(t/100 - 1)*(t/100) - beta*(t/100 - 1)*(t/100)^3]
 * - alpha is the mean R change referred to 0C
 * - Rt = R0 * [1 + A*t + B*t^2 - C*(t-100)*t^3]
 * - A = alpha + (alpha*delta)/100
 * - B = - (alpha * delta)/(100^2)
 * - C = - (alpha * beta)/(100^4)
 * @param R0 nominal resistance at 0°C in ohms
 * @param A precomputed A parameter
 * @param B precomputed B parameter
 * @param Rt the resistance value to convert in ohms
 * @return temperature in Celsius
 */
__attribute__((const)) static float quadratic_cvd(const float R0, const float A, const float B, const float Rt)
{
	// quadratic fit: we're going to ignore the cubic term given the temperature range we're looking at
	return ((-R0*A + sqrtf(R0*R0*A*A - 4.0F*R0*B*(R0 - Rt))) / (2.0F*R0*B));
}

/*
 CVD parameters for various Pt RTDs.

 Given						Calculated
 Alpha, α	Delta, δ	Beta, β		A		B		C
 °C-1		°C		°C		°C-1		°C-2		°C-4
 0.003750	1.605		0.16		3.8102 x 10-3	-6.01888 x 10-7	-6.0 x 10-12
 0.003770					3.8285 x 10-3	-5.85 x 10-7
 0.003850	1.4999		0.10863		3.9083 x 10-3	-5.775 x 10-7	-4.18301 x 10-12
 0.003902	1.52		0.11		3.96 x 10-3	-5.93 x 10-7	-4.3 x 10-12
 0.003911					3.9692 × 10-3	–5.829 × 10-7	–4.3303 × 10-12
 0.003916					3.9739 × 10-3	–5.870 × 10-7	–4.4 × 10-12
 0.003920					3.9787 × 10-3	–5.8686 × 10-7	–4.167 × 10-12
 0.003928					3.9888 × 10-3	–5.915 × 10-7	–3.85 × 10-12

 JIS C1604: 3916ppm/K
 US Curve: 3920ppm/K
 */

/**
 * Convert RTD resistance value to actual temperature.
 * @param rtdtype the type of RTD sensor (as defined in enum e_hw_lib_rtdt)
 * @param R0res the nominal resistance at 0°C
 * @param Rtrest the resistance value to convert
 * @return temperature in Celsius
 */
__attribute__((const))
float hw_lib_rtd_res_to_celsius(const enum e_hw_lib_rtdt rtdtype, const res_t R0res, const res_t Rtres)
{
	const float R0 = hw_lib_res_to_ohm(R0res);
	const float Rt = hw_lib_res_to_ohm(Rtres);
	float A, B;

	switch (rtdtype) {
		case HW_RTD_PT3750:
			A = 3.8102e-3F;
			B = -6.01888e-7F;
			goto quadcvd;
		case HW_RTD_PT3770:
			A = 3.8285e-3F;
			B = -5.85e-7F;
			goto quadcvd;
		case HW_RTD_PT3850:
			A = 3.9083e-3F;
			B = -5.775e-7F;
			goto quadcvd;
		case HW_RTD_PT3902:
			A = 3.96e-3F;
			B = -5.93e-7F;
			goto quadcvd;
		case HW_RTD_PT3911:
			A = 3.9692e-3F;
			B = -5.829e-7F;
			goto quadcvd;
		case HW_RTD_PT3916:
			A = 3.9739e-3F;
			B = -5.879e-7F;
			goto quadcvd;
		case HW_RTD_PT3920:
			A = 3.9787e-3F;
			B = -5.8686e-7F;
			goto quadcvd;
		case HW_RTD_PT3928:
			A = 3.9888e-3F;
			B = -5.915e-7F;
			goto quadcvd;
		case HW_RTD_NONE:
		default:
			dbgerr("UNKNOWN SENSOR TYPE!");
			return (-273.0);
	}

quadcvd:
	return (quadratic_cvd(R0, A, B, Rt));
}

/**
 * Convert res_t format back to ohms.
 * @param tk value to convert.
 * @return the value expressed in ohms.
 */
float hw_lib_res_to_ohm(const res_t res)
{
	return ((float)res/RES_OHMMULT);
}

#ifdef HAS_FILECFG
static const char * const hw_lib_rtdtype_str[] = {
	[HW_RTD_NONE] = "NONE",		// invalid
	[HW_RTD_PT3750] = "PT3750",
	[HW_RTD_PT3770] = "PT3770",
	[HW_RTD_PT3850] = "PT3850",
	[HW_RTD_PT3902] = "PT3902",
	[HW_RTD_PT3911] = "PT3911",
	[HW_RTD_PT3916] = "PT3916",
	[HW_RTD_PT3920] = "PT3920",
	[HW_RTD_PT3928] = "PT3928",
};

const char * hw_lib_print_rtdtype(const enum e_hw_lib_rtdt type)
{
	if (type >= ARRAY_SIZE(hw_lib_rtdtype_str))
		return NULL;

	return (hw_lib_rtdtype_str[type]);
}

int hw_lib_match_rtdtype(const char * str)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hw_lib_rtdtype_str); i++) {
		if (!strcmp(hw_lib_rtdtype_str[i], str))
			break;
	}

	if (i >= ARRAY_SIZE(hw_lib_rtdtype_str))
		return (-ENOTFOUND);

	return ((int)i);
}
#endif /* HAS_FILECFG */
