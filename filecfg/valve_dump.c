//
//  filecfg/valve_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Valve subsystem file configuration dumping.
 */

#include "valve_dump.h"
#include "filecfg.h"
#include "lib.h"
#include "hardware.h"


static int filecfg_v_bangbang_dump(const struct s_valve * restrict const valve __attribute__((unused)))
{
	return (ALL_OK);
}

static int filecfg_v_sapprox_dump(const struct s_valve * restrict const valve)
{
	const struct s_valve_sapprox_priv * restrict priv;

	if (!valve)
		return (-EINVALID);

	if (VA_TALG_SAPPROX != valve->set.tset.tmix.algo)
		return (-EINVALID);

	priv = valve->priv;

	filecfg_iprintf("amount %" PRIdFAST16 ";\n", priv->set.amount);
	filecfg_iprintf("sample_intvl %ld;\n", timekeep_tk_to_sec(priv->set.sample_intvl));

	return (ALL_OK);
}

static int filecfg_v_pi_dump(const struct s_valve * restrict const valve)
{
	const struct s_valve_pi_priv * restrict priv;

	if (!valve)
		return (-EINVALID);

	if (VA_TALG_PI != valve->set.tset.tmix.algo)
		return (-EINVALID);

	priv = valve->priv;

	filecfg_iprintf("sample_intvl %ld;\n", timekeep_tk_to_sec(priv->set.sample_intvl));
	filecfg_iprintf("Tu %ld;\n", timekeep_tk_to_sec(priv->set.Tu));
	filecfg_iprintf("Td %ld;\n", timekeep_tk_to_sec(priv->set.Td));
	filecfg_iprintf("Ksmax %.1f;\n", temp_to_deltaK(priv->set.Ksmax));
	filecfg_iprintf("tune_f %" PRIdFAST8 ";\n", priv->set.tune_f);

	return (ALL_OK);
}

static int filecfg_valve_algo_dump(const struct s_valve * restrict const valve)
{
	const char * algoname;
	int (* privdump)(const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.tset.tmix.algo) {
		case VA_TALG_BANGBANG:
			algoname = "bangbang";
			privdump = filecfg_v_bangbang_dump;
			break;
		case VA_TALG_SAPPROX:
			algoname = "sapprox";
			privdump = filecfg_v_sapprox_dump;
			break;
		case VA_TALG_PI:
			algoname = "PI";
			privdump = filecfg_v_pi_dump;
			break;
		case VA_TALG_NONE:
		default:
			algoname = "";
			privdump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_printf(" \"%s\" {\n", algoname);
	filecfg_ilevel_inc();
	if (privdump)
		ret = privdump(valve);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

static int filecfg_valve_tmix_dump(const struct s_valve * restrict const valve)
{
	if (FCD_Exhaustive || valve->set.tset.tmix.tdeadzone)
		filecfg_iprintf("tdeadzone %.1f;\n", temp_to_deltaK(valve->set.tset.tmix.tdeadzone));
	if (FCD_Exhaustive || hardware_sensor_name(valve->set.tset.tmix.tid_hot))
		filecfg_iprintf("tid_hot"), filecfg_tempid_dump(valve->set.tset.tmix.tid_hot);
	if (FCD_Exhaustive || hardware_sensor_name(valve->set.tset.tmix.tid_cold))
		filecfg_iprintf("tid_cold"), filecfg_tempid_dump(valve->set.tset.tmix.tid_cold);
	filecfg_iprintf("tid_out"); filecfg_tempid_dump(valve->set.tset.tmix.tid_out);		// mandatory

	filecfg_iprintf("algo");
	return (filecfg_valve_algo_dump(valve));			// mandatory
}

static int filecfg_valve_tisol_dump(const struct s_valve * restrict const valve)
{
	filecfg_iprintf("reverse %s;\n", filecfg_bool_str(valve->set.tset.tisol.reverse));	// mandatory

	return (ALL_OK);
}

static int filecfg_valve_type_dump(const struct s_valve * restrict const valve)
{
	const char * tname;
	int (* vtypedump)(const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.type) {
		case VA_TYPE_MIX:
			tname = "mix";
			vtypedump = filecfg_valve_tmix_dump;
			break;
		case VA_TYPE_ISOL:
			tname = "isol";
			vtypedump = filecfg_valve_tisol_dump;
			break;
		case VA_TYPE_NONE:
		case VA_TYPE_UNKNOWN:
		default:
			tname = "";
			vtypedump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_printf(" \"%s\" {\n", tname);
	filecfg_ilevel_inc();
	if (vtypedump)
		ret = vtypedump(valve);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

static int filecfg_valve_m3way_dump(const struct s_valve * restrict const valve)
{
	filecfg_iprintf("rid_open"); filecfg_relid_dump(valve->set.mset.m3way.rid_open);	// mandatory
	filecfg_iprintf("rid_close"); filecfg_relid_dump(valve->set.mset.m3way.rid_close);	// mandatory

	return (ALL_OK);
}

static int filecfg_valve_m2way_dump(const struct s_valve * restrict const valve)
{
	filecfg_iprintf("rid_trigger"); filecfg_relid_dump(valve->set.mset.m2way.rid_trigger);	// mandatory
	filecfg_iprintf("trigger_opens %s;\n", filecfg_bool_str(valve->set.mset.m2way.trigger_opens));// mandatory

	return (ALL_OK);
}

static int filecfg_valve_motor_dump(const struct s_valve * restrict const valve)
{
	const char * mname;
	int (* vmotordump)(const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.motor) {
		case VA_M_3WAY:
			mname = "3way";
			vmotordump = filecfg_valve_m3way_dump;
			break;
		case VA_M_2WAY:
			mname = "2way";
			vmotordump = filecfg_valve_m2way_dump;
			break;
		case VA_M_NONE:
		default:
			mname = "";
			vmotordump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_printf(" \"%s\" {\n", mname);
	filecfg_ilevel_inc();
	if (vmotordump)
		ret = vmotordump(valve);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

int filecfg_valve_dump(const struct s_valve * restrict const valve)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("valve \"%s\" {\n", valve->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || valve->set.deadband)
		filecfg_iprintf("deadband %" PRIdFAST16 ";\n", valve->set.deadband);
	filecfg_iprintf("ete_time %ld;\n", timekeep_tk_to_sec(valve->set.ete_time));	// mandatory

	filecfg_iprintf("type"); filecfg_valve_type_dump(valve);			// mandatory
	filecfg_iprintf("motor"); filecfg_valve_motor_dump(valve);			// mandatory

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
