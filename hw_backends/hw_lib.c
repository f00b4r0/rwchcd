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
 * Provides tools to convert RTDs value to temperature.
 * Supported RTDs include several Pt and Ni types.
 * @note Uses floating point arithmetic. Integer lookup tables can be implemented instead if fp is not available.
 */

#include <stdint.h>
#include <math.h>	// sqrtf/modff

#include "hw_lib.h"

// Note: a tool (temp_table.c) is provided in the tools/ directory to compute and output well formatted lookup tables

/** Resistance->Temperature lookup table data */
struct s_hw_lib_lookupt {
	const unsigned int rstart;	///< table resistance start value (resistance at index 0)
	const unsigned int rstep;	///< table resistance step (resistance increment between two consecutive table values)
	const float R0nom;		///< nominal R0 for the table, in ohms
	const float * const table;	///< resistance -> temperature lookup table
	const size_t tsize;		///< table size
};

/**
 * Nickel NL TK5000 "LG-Ni" (5000ppm/K) lookup table.
 * Unidimensional resistance -> °C temperature lookup table, tmin: c.-60°C, tmax: c.160°C
 * R0: 1000, R start value: 755, step: 5
 */
static const float Nickel_NL[] = {
//   R             0 ,         5 ,        10 ,        15 ,        20 ,        25 ,        30 ,        35 ,        40 ,        45 ,
			-59.1713F,  -57.8823F,  -56.5969F,  -55.3150F,  -54.0367F,  -52.7620F,  -51.4908F,  -50.2232F,  -48.9590F,
/*  800 */  -47.6984F,  -46.4412F,  -45.1875F,  -43.9373F,  -42.6906F,  -41.4473F,  -40.2074F,  -38.9709F,  -37.7378F,  -36.5081F,
/*  850 */  -35.2818F,  -34.0589F,  -32.8393F,  -31.6231F,  -30.4101F,  -29.2005F,  -27.9942F,  -26.7912F,  -25.5915F,  -24.3950F,
/*  900 */  -23.2018F,  -22.0118F,  -20.8250F,  -19.6415F,  -18.4611F,  -17.2839F,  -16.1099F,  -14.9391F,  -13.7714F,  -12.6069F,
/*  950 */  -11.4454F,  -10.2871F,   -9.1319F,   -7.9797F,   -6.8307F,   -5.6846F,   -4.5417F,   -3.4017F,   -2.2648F,   -1.1309F,
/* 1000 */    0.0000F,    1.1280F,    2.2530F,    3.3750F,    4.4941F,    5.6102F,    6.7235F,    7.8338F,    8.9412F,   10.0458F,
/* 1050 */   11.1475F,   12.2463F,   13.3423F,   14.4354F,   15.5258F,   16.6133F,   17.6981F,   18.7800F,   19.8592F,   20.9356F,
/* 1100 */   22.0093F,   23.0803F,   24.1485F,   25.2141F,   26.2769F,   27.3370F,   28.3945F,   29.4493F,   30.5015F,   31.5510F,
/* 1150 */   32.5979F,   33.6422F,   34.6839F,   35.7229F,   36.7595F,   37.7934F,   38.8248F,   39.8536F,   40.8799F,   41.9037F,
/* 1200 */   42.9250F,   43.9437F,   44.9600F,   45.9738F,   46.9851F,   47.9940F,   49.0004F,   50.0044F,   51.0060F,   52.0052F,
/* 1250 */   53.0019F,   53.9963F,   54.9883F,   55.9779F,   56.9651F,   57.9500F,   58.9326F,   59.9128F,   60.8907F,   61.8663F,
/* 1300 */   62.8396F,   63.8107F,   64.7794F,   65.7459F,   66.7101F,   67.6721F,   68.6318F,   69.5893F,   70.5446F,   71.4977F,
/* 1350 */   72.4485F,   73.3972F,   74.3438F,   75.2881F,   76.2303F,   77.1703F,   78.1082F,   79.0440F,   79.9776F,   80.9091F,
/* 1400 */   81.8386F,   82.7659F,   83.6911F,   84.6143F,   85.5354F,   86.4544F,   87.3714F,   88.2864F,   89.1993F,   90.1102F,
/* 1450 */   91.0191F,   91.9260F,   92.8309F,   93.7338F,   94.6347F,   95.5337F,   96.4306F,   97.3257F,   98.2188F,   99.1099F,
/* 1500 */   99.9992F,  100.8865F,  101.7719F,  102.6554F,  103.5370F,  104.4167F,  105.2945F,  106.1705F,  107.0446F,  107.9168F,
/* 1550 */  108.7872F,  109.6558F,  110.5225F,  111.3874F,  112.2505F,  113.1118F,  113.9712F,  114.8289F,  115.6848F,  116.5390F,
/* 1600 */  117.3913F,  118.2419F,  119.0907F,  119.9378F,  120.7832F,  121.6268F,  122.4687F,  123.3088F,  124.1473F,  124.9841F,
/* 1650 */  125.8191F,  126.6525F,  127.4841F,  128.3142F,  129.1425F,  129.9692F,  130.7942F,  131.6175F,  132.4393F,  133.2594F,
/* 1700 */  134.0778F,  134.8946F,  135.7099F,  136.5235F,  137.3355F,  138.1459F,  138.9547F,  139.7620F,  140.5676F,  141.3717F,
/* 1750 */  142.1743F,  142.9753F,  143.7747F,  144.5726F,  145.3689F,  146.1637F,  146.9570F,  147.7488F,  148.5390F,  149.3278F,
/* 1800 */  150.1150F,  150.9007F,  151.6850F,  152.4678F,  153.2491F,  154.0289F,  154.8072F,  155.5841F,  156.3596F,  157.1335F,
/* 1850 */  157.9061F,  158.6772F,  159.4469F,
};
// 222 values; R end value: 1860

static const struct s_hw_lib_lookupt Nickel_NL_lt = {
	.rstart = 755,
	.rstep = 5,
	.R0nom = 1000.0F,
	.table = Nickel_NL,
	.tsize = ARRAY_SIZE(Nickel_NL),
};

/**
 * Nickel ND (6180ppm/K) lookup table.
 * Unidimensional resistance -> °C temperature lookup table, tmin: c.-60°C, tmax: c.160°C
 * R0: 1000, R start value: 700, step: 5
 */
static const float Nickel_ND[] = {
//   R             0 ,         5 ,        10 ,        15 ,        20 ,        25 ,        30 ,        35 ,        40 ,        45 ,
/*  700 */  -58.9727F,  -57.9056F,  -56.8419F,  -55.7817F,  -54.7249F,  -53.6715F,  -52.6214F,  -51.5745F,  -50.5309F,  -49.4906F,
/*  750 */  -48.4533F,  -47.4192F,  -46.3882F,  -45.3603F,  -44.3353F,  -43.3134F,  -42.2944F,  -41.2784F,  -40.2653F,  -39.2550F,
/*  800 */  -38.2476F,  -37.2429F,  -36.2411F,  -35.2420F,  -34.2456F,  -33.2519F,  -32.2609F,  -31.2726F,  -30.2868F,  -29.3037F,
/*  850 */  -28.3231F,  -27.3451F,  -26.3696F,  -25.3966F,  -24.4261F,  -23.4581F,  -22.4925F,  -21.5293F,  -20.5685F,  -19.6101F,
/*  900 */  -18.6540F,  -17.7003F,  -16.7488F,  -15.7997F,  -14.8529F,  -13.9083F,  -12.9660F,  -12.0259F,  -11.0880F,  -10.1523F,
/*  950 */   -9.2188F,   -8.2874F,   -7.3582F,   -6.4311F,   -5.5062F,   -4.5833F,   -3.6625F,   -2.7438F,   -1.8272F,   -0.9125F,
/* 1000 */    0.0000F,    0.9106F,    1.8192F,    2.7258F,    3.6304F,    4.5330F,    5.4337F,    6.3325F,    7.2293F,    8.1242F,
/* 1050 */    9.0172F,    9.9083F,   10.7976F,   11.6849F,   12.5704F,   13.4541F,   14.3359F,   15.2159F,   16.0941F,   16.9704F,
/* 1100 */   17.8450F,   18.7178F,   19.5888F,   20.4580F,   21.3255F,   22.1912F,   23.0552F,   23.9174F,   24.7779F,   25.6367F,
/* 1150 */   26.4938F,   27.3492F,   28.2029F,   29.0550F,   29.9053F,   30.7540F,   31.6010F,   32.4464F,   33.2901F,   34.1322F,
/* 1200 */   34.9726F,   35.8115F,   36.6487F,   37.4843F,   38.3183F,   39.1507F,   39.9815F,   40.8107F,   41.6384F,   42.4645F,
/* 1250 */   43.2890F,   44.1120F,   44.9334F,   45.7533F,   46.5716F,   47.3884F,   48.2037F,   49.0174F,   49.8297F,   50.6404F,
/* 1300 */   51.4496F,   52.2573F,   53.0636F,   53.8683F,   54.6716F,   55.4733F,   56.2737F,   57.0725F,   57.8699F,   58.6658F,
/* 1350 */   59.4602F,   60.2533F,   61.0448F,   61.8350F,   62.6237F,   63.4109F,   64.1968F,   64.9812F,   65.7642F,   66.5458F,
/* 1400 */   67.3260F,   68.1048F,   68.8821F,   69.6581F,   70.4327F,   71.2059F,   71.9778F,   72.7482F,   73.5173F,   74.2850F,
/* 1450 */   75.0513F,   75.8163F,   76.5799F,   77.3422F,   78.1031F,   78.8627F,   79.6209F,   80.3778F,   81.1334F,   81.8876F,
/* 1500 */   82.6405F,   83.3920F,   84.1423F,   84.8912F,   85.6388F,   86.3851F,   87.1301F,   87.8738F,   88.6161F,   89.3572F,
/* 1550 */   90.0970F,   90.8355F,   91.5727F,   92.3086F,   93.0433F,   93.7766F,   94.5087F,   95.2395F,   95.9691F,   96.6974F,
/* 1600 */   97.4244F,   98.1501F,   98.8746F,   99.5979F,  100.3199F,  101.0406F,  101.7601F,  102.4784F,  103.1954F,  103.9112F,
/* 1650 */  104.6257F,  105.3391F,  106.0512F,  106.7620F,  107.4717F,  108.1801F,  108.8873F,  109.5933F,  110.2981F,  111.0017F,
/* 1700 */  111.7040F,  112.4052F,  113.1052F,  113.8039F,  114.5015F,  115.1979F,  115.8931F,  116.5871F,  117.2799F,  117.9716F,
/* 1750 */  118.6620F,  119.3513F,  120.0394F,  120.7264F,  121.4121F,  122.0967F,  122.7802F,  123.4625F,  124.1436F,  124.8235F,
/* 1800 */  125.5024F,  126.1800F,  126.8565F,  127.5319F,  128.2061F,  128.8792F,  129.5512F,  130.2220F,  130.8916F,  131.5602F,
/* 1850 */  132.2276F,  132.8939F,  133.5590F,  134.2231F,  134.8860F,  135.5478F,  136.2085F,  136.8681F,  137.5266F,  138.1839F,
/* 1900 */  138.8402F,  139.4953F,  140.1494F,  140.8024F,  141.4542F,  142.1050F,  142.7547F,  143.4033F,  144.0508F,  144.6972F,
/* 1950 */  145.3425F,  145.9868F,  146.6300F,  147.2721F,  147.9131F,  148.5531F,  149.1920F,  149.8298F,  150.4666F,  151.1023F,
/* 2000 */  151.7369F,  152.3705F,  153.0030F,  153.6345F,  154.2650F,  154.8944F,  155.5227F,  156.1500F,  156.7763F,  157.4015F,
/* 2050 */  158.0257F,  158.6488F,  159.2709F,  159.8920F,
};
// 274 values; R end value: 2065

static const struct s_hw_lib_lookupt Nickel_ND_lt = {
	.rstart = 700,
	.rstep = 5,
	.R0nom = 1000.0F,
	.table = Nickel_ND,
	.tsize = ARRAY_SIZE(Nickel_ND),
};

/**
 * Quadratic lookup table interpolation.
 * @param R0 Nominal resistance at 0°C in ohms
 * @param lt the lookup table data
 * @param Rt the resistance value to convert in ohms
 * @return temperature in Celsius
 */
static float quadratic_interpol(const float R0, const struct s_hw_lib_lookupt * lt, const float Rt)
{
	float Rtnorm, ip, rem;
	float A, b, c;
	unsigned int index;

	// normalise Rt to match table's R0 reference
	Rtnorm = Rt * (lt->R0nom / R0);

	// offset Rt from beginning of table,
	Rtnorm -= (float)lt->rstart;

	// compute lookup index by dividing by table step, preserve remainder for interpolation
	rem = modff(Rtnorm / (float)lt->rstep, &ip);
	index = (unsigned int)ip;

	// we need one step before and one step after current index for quadratic interpolation
	if (unlikely(index < 0+1))
		return (-273.0F);	// Houston, we have a problem
	else if (unlikely(index >= lt->tsize-1))
		return (+273.0F);	// Houston, we have a problem

	/*
	 second order Lagrange interpolation (parabolic) with constant step.
	 For a sampled function F(Xi) = Yi, where Xi is the closest sample immediately below the input point X being interpolated,
	 and rem the remainder of the division X / sampling step:
	 with A = Yi, B = Yi+1 and C = Yi-1:
		temp = A + rem * (B - C)/2 + rem^2 * (B - 2A + C)/2

	 For reference, single order (linear) is: temp = A + rem * (B - A)

	 with A = Yi, b = Yi+1 / 2, c = Yi-1 / 2:
		temp = A + rem * (b - c) + rem^2 * (b - A + c)
	 factorising:
		temp = A + rem * (b - c + rem * (b - A + c))
	 */
	A = lt->table[index];
	b = lt->table[index+1] / 2.0F;
	c = lt->table[index-1] / 2.0F;
	return (A + rem * (b - c + rem * (b - A + c)));
}

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
 * @param Rtres the resistance value to convert
 * @return temperature in Celsius
 */
__attribute__((const))
float hw_lib_rtd_res_to_celsius(const enum e_hw_lib_rtdt rtdtype, const res_t R0res, const res_t Rtres)
{
	const float R0 = hw_lib_res_to_ohm(R0res);
	const float Rt = hw_lib_res_to_ohm(Rtres);
	const struct s_hw_lib_lookupt * lt;
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
		case HW_RTD_NI5000:
			lt = &Nickel_NL_lt;
			goto quadapprox;
		case HW_RTD_NI6180:
			lt = &Nickel_ND_lt;
			goto quadapprox;
		case HW_RTD_NONE:
		default:
			dbgerr("UNKNOWN SENSOR TYPE!");
			return (-273.0);
	}

quadcvd:
	return (quadratic_cvd(R0, A, B, Rt));
quadapprox:
	return (quadratic_interpol(R0, lt, Rt));
}

/**
 * Convert res_t format back to ohms.
 * @param res value to convert.
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
	[HW_RTD_NI5000] = "NI5000",
	[HW_RTD_NI6180] = "NI6180",
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
