//
//  temp_table.c
//
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/*
 This program outputs a resistance -> Celsius temp table.
 To select wich table is produced, see "settings" in main().
 The result is floating point celsius.
 Adaptation to produce integers in native temp_t format is trivial if necessary.
 */

#include <stdio.h>
#include <math.h>

#define WRAP_COLUMNS	10

/*
 Nickel ND (6180ppm/K)
 Rt = R0(1+ 5.485E-3*t + 6.65E-6*t^2 + 2.805E-11*t^4 + -2.10E-17*t^6)
 t in Celsius
 */
const double Nickel_ND(const double R0, const double t)
{
	const double A = 5.485E-3;
	const double B = 6.65E-6;
	const double D = 2.805E-11;
	const double F = -2.10E-17;

	return (R0 * (1 + A*t + B*t*t + D*t*t*t*t + F*t*t*t*t*t*t));
}

/*
 Nickel NL TK5000 "LG-Ni" (5000ppm/K)
 Rt = R0(1+ 4.427E-3*t + 5.172E-6*t^2 + 5.585E-9*t^3)
 t in Celsius
 */
const double Nickel_NL(const double R0, const double t)
{
	const double A = 4.427E-3;
	const double B = 5.172E-6;
	const double C = 5.585E-9;

	return (R0 * (1 + A*t + B*t*t + C*t*t*t));
}

/*
 Nickel NJ (5370ppm/K)
 Rt = R0(1+ 5.64742E-3*t + 6.69504E-6*t^2 + 5.68816E-9*t^3)
 t in Celsius
 */
const double Nickel_NJ(const double R0, const double t)
{
	const double A = 5.64742E-3;
	const double B = 6.69504E-6;
	const double C = 5.68816E-9;

	return (R0 * (1 + A*t + B*t*t + C*t*t*t));
}

/*
 Nickel NA (6720ppm/K)
 Rt = R0(1+ 5.88025E-3*t + 8.28385E-6*t^2 + 7.67175E-12*t^4 + -1.5E-16*t^6)
 t in Celsius
 */
const double Nickel_NA(const double R0, const double t)
{
	const double A = 5.88025E-3;
	const double B = 8.28385E-6;
	const double D = 7.67175E-12;
	const double F = -1.5E-16;

	return (R0 * (1 + A*t + B*t*t + D*t*t*t*t + F*t*t*t*t*t*t));
}

int pheader(double R0, double tmin, double tmax, double tinc, long rstart, long rstep)
{
	int dec = 0;
	int offset;

	printf("/**\n");
	printf(" * Unidimensional resistance -> °C temperature lookup table, tmin: c.%ld°C, tmax: c.%ld°C\n", (long)tmin, (long)tmax);
	printf(" * R0: %ld, R start value: %ld, step: %ld\n", (long)R0, rstart, rstep);
	printf(" */\n");

	printf("static const float table[] = {\n");

	while (tinc < 1.0) {
		dec++;
		tinc *= 10.0;
	}

	printf("//   R    ");
	for (int i = 0; i < WRAP_COLUMNS; i++)
		printf("%*ld ,", 6+dec, i*rstep);

	printf("\n");

	offset = (rstart / rstep) % WRAP_COLUMNS;

	if (offset) {
		printf("%10s", "");
		while (offset--)
			printf("%*s  ", 6+dec, "");
	}

	return (rstart / rstep) % WRAP_COLUMNS;
}

void ptelmt(int index, long rid, double temp, double tinc)
{
	const int wrap = WRAP_COLUMNS;
	int dec = 0;

	while (tinc < 1.0) {
		dec++;
		tinc *= 10.0;
	}

	if (!(index % wrap)) {
		if (index)
			printf("\n");
		printf("/* %4ld */", rid);
	}

	printf("%*.*fF,", 6+dec, dec, temp);
}

void pfooter(int vals, long rend)
{
	printf("\n};\n");
	printf("// %d values; R end value: %ld\n", vals, rend);
}

int main(void)
{
	// settings
	const double (*poly)(const double R0, const double t) = Nickel_ND;
	const double R0 = 1000.0;
	const double tmin = -60.0;
	const double tmax = 160.0;
	const double tinc = 0.0001;
	const double rprec = 0.001;
	const long rstep = 5;
	// end settings

	double r, rem, ip;
	long rid;
	int offset, i, ret = 0;

	rid = (long)ceil(poly(R0, tmin));
	rid += rstep - (rid % rstep);

	offset = pheader(R0, tmin, tmax, tinc, rid, rstep);
	i = 0;

	for (double t = tmin; t < tmax; t += tinc) {
		r = poly(R0, t);
		rem = modf(r, &ip);
		// first match "above" value
		if (rid == (long)ip) {
			if (rem >= rprec) {
				ret = -1;
				fprintf(stderr, "warning: R precision exceeded, decrease tinc or increase rprec!\n");
			}
			ptelmt(i+offset, rid, t, tinc);
			rid += rstep;
			i++;
		}
	}

	pfooter(i, rid-rstep);

	return (ret);
}
