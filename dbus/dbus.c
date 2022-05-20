//
//  dbus/dbus.c
//  rwchcd
//
//  (C) 2016-2018,2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * D-Bus implementation.
 *
 * This is a basic implementation for remote control over D-Bus.
 * It goes too low level into the API for its own good.
 *
 * Objects (circuits, temperatures, etc) are registered with a D-Bus path matching their internal index number,
 * which will always be consistent with the order in which they appear in the configuration file.
 * The general idea is to expose only relevant data and to allow write operations only where it makes sense.
 * Specifically, this API is not a configuration interface.
 *
 * The current implementation supports:
 *  - Changing the global runtime System and Run modes
 *  - Reading heating circuits status and setting manual overrides for runmode and target temperature
 *  - Reading DHWTs status and setting manual override for runmode, forcing charge and anti-legionella cycle
 *  - Reading known temperatures
 *
 * @note will crash if any operation is attempted before the runtime/config structures
 * are properly set, which should never happen in the current setup (dbus_main() is called after full initialization).
 * @note the object properties are only valid if said object is 'online' (as reported by the namesake property).
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "runtime.h"
#include "plant/plant_priv.h"
#include "io/inputs.h"
#include "io/inputs/temperature.h"
#include "dbus.h"

#define DBUS_IFACE_BASE		"org.slashdirt.rwchcd"
#define DBUS_RUNTIME_IFACE	DBUS_IFACE_BASE ".Runtime"
#define DBUS_HCIRCUIT_IFACE	DBUS_IFACE_BASE ".Hcircuit"
#define DBUS_DHWT_IFACE		DBUS_IFACE_BASE ".DHWT"
#define DBUS_HEATSRC_IFACE	DBUS_IFACE_BASE ".Heatsource"
#define DBUS_PUMP_IFACE		DBUS_IFACE_BASE ".Pump"
#define DBUS_TEMP_IFACE		DBUS_IFACE_BASE ".Temperature"

#define DBUS_OBJECT_BASE	"/org/slashdirt/rwchcd"
#define DBUS_HCIRCUITS_OBJECT	DBUS_OBJECT_BASE "/plant/hcircuits"
#define DBUS_DHWTS_OBJECT	DBUS_OBJECT_BASE "/plant/dhwts"
#define DBUS_HEATSRCS_OBJECT	DBUS_OBJECT_BASE "/plant/heatsources"
#define DBUS_PUMPS_OBJECT	DBUS_OBJECT_BASE "/plant/pumps"
#define DBUS_TEMPS_OBJECT	DBUS_OBJECT_BASE "/inputs/temperatures"

static GDBusNodeInfo *dbus_introspection_data = NULL;
static GDBusInterfaceInfo *dbus_runtime_interface_info = NULL;
static GDBusInterfaceInfo *dbus_hcircuit_interface_info = NULL;
static GDBusInterfaceInfo *dbus_dhwt_interface_info = NULL;
static GDBusInterfaceInfo *dbus_heatsrc_interface_info = NULL;
static GDBusInterfaceInfo *dbus_pump_interface_info = NULL;
static GDBusInterfaceInfo *dbus_temp_interface_info = NULL;

static const gchar dbus_introspection_xml[] =
"<node>"
" <interface name='" DBUS_RUNTIME_IFACE "'>"
"  <property name='SystemMode' access='readwrite' type='y'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='RunMode' access='readwrite' type='y'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='DhwMode' access='readwrite' type='y'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='StopDhw' access='readwrite' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
" </interface>"
" <interface name='" DBUS_HCIRCUIT_IFACE "'>"
"  <property name='Online' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='Name' access='read' type='s'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='RunModeOverride' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='RunMode' access='read' type='y'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='TempComfort' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempEco' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempFrostFree' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempOffsetOverride' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='AmbientRequest' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='AmbientActual' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='WtempTarget' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='WtempActual' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='OutOffComfort' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='OutOffEco' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='OutOffFrostFree' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='InOffTemp' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <method name='SetTempOffsetOverride'>"
"   <arg name='offset' direction='in' type='d' />"
"  </method>"
"  <method name='SetRunmodeOverride'>"
"   <arg name='runmode' direction='in' type='y' />"
"  </method>"
"  <method name='DisableRunmodeOverride' />"
" </interface>"
" <interface name='" DBUS_DHWT_IFACE "'>"
"  <property name='Online' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='Name' access='read' type='s'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='ForceChargeOn' access='readwrite' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='LegionellaOn' access='readwrite' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='RecycleOn' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='ElectricModeOn' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='RunModeOverride' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='RunMode' access='read' type='y'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='TempComfort' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempEco' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempFrostFree' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempLegionella' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempTarget' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='TempCurrent' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <method name='SetRunmodeOverride'>"
"   <arg name='runmode' direction='in' type='y' />"
"  </method>"
"  <method name='DisableRunmodeOverride' />"
" </interface>"
" <interface name='" DBUS_HEATSRC_IFACE "'>"
"  <property name='Online' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='Name' access='read' type='s'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='Overtemp' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='RunMode' access='read' type='y'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='TempRequest' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
" </interface>"
" <interface name='" DBUS_PUMP_IFACE "'>"
"  <property name='Online' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
"  <property name='Name' access='read' type='s'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='Active' access='read' type='b'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
" </interface>"
" <interface name='" DBUS_TEMP_IFACE "'>"
"  <property name='Name' access='read' type='s'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const' />"
"  </property>"
"  <property name='Value' access='read' type='d'>"
"   <annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='false' />"
"  </property>"
" </interface>"
"</node>";

static GMainLoop *Mainloop = NULL;

/* Runtime */

static GVariant *
runtime_get_property(GDBusConnection  *connection,
		    const gchar      *sender,
		    const gchar      *object_path,
		    const gchar      *interface_name,
		    const gchar      *property_name,
		    GError          **error,
		    gpointer          user_data)
{
	GVariant *var;
	const gchar *node;

	var = NULL;
	if (g_strcmp0(property_name, "SystemMode") == 0) {
		enum e_systemmode sysmode = runtime_systemmode();
		var = g_variant_new_byte((guchar)sysmode);
	}
	else if (g_strcmp0(property_name, "RunMode") == 0) {
		enum e_runmode runmode = runtime_runmode();
		var = g_variant_new_byte((guchar)runmode);
	}
	else if (g_strcmp0(property_name, "DhwMode") == 0) {
		enum e_runmode dhwmode = runtime_dhwmode();
		var = g_variant_new_byte((guchar)dhwmode);
	}
	else if (g_strcmp0(property_name, "StopDhw") == 0)
		var = g_variant_new_boolean((gboolean)runtime_get_stopdhw());
	else
		g_assert_not_reached();

	return var;
}

static gboolean
runtime_set_property(GDBusConnection  *connection,
		    const gchar      *sender,
		    const gchar      *object_path,
		    const gchar      *interface_name,
		    const gchar      *property_name,
		    GVariant         *value,
		    GError          **error,
		    gpointer          user_data)
{
	if (g_strcmp0(property_name, "SystemMode") == 0) {
		enum e_systemmode sysmode = g_variant_get_byte(value);
		if ((sysmode > SYS_NONE) && (sysmode < SYS_UNKNOWN))
			runtime_set_systemmode(sysmode);
		else
			goto error;
	}
	else if (g_strcmp0(property_name, "RunMode") == 0) {
		enum e_runmode runmode = g_variant_get_byte(value);
		if ((runmode >= 0) && (runmode < RM_UNKNOWN))
			runtime_set_runmode(runmode);
		else
			goto error;
	}
	else if (g_strcmp0(property_name, "DhwMode") == 0) {
		enum e_runmode runmode = g_variant_get_byte(value);
		if ((runmode >= 0) && (runmode < RM_UNKNOWN))
			runtime_set_dhwmode(runmode);
		else
			goto error;
	}
	else if (g_strcmp0(property_name, "StopDhw") == 0) {
		gboolean state = g_variant_get_boolean(value);
		runtime_set_stopdhw(state);
	}
	else
		g_assert_not_reached();

	return TRUE;

error:
	g_set_error_literal(error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Invalid argument");
	return FALSE;
}

static const GDBusInterfaceVTable runtime_vtable = {
	NULL,
	runtime_get_property,
	runtime_set_property,
};

/* Circuit */

static void
hcircuit_method_call(GDBusConnection       *connection,
		     const gchar           *sender,
		     const gchar           *object_path,
		     const gchar           *interface_name,
		     const gchar           *method_name,
		     GVariant              *parameters,
		     GDBusMethodInvocation *invocation,
		     gpointer               user_data)
{
	const gchar *node;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	struct s_hcircuit * restrict hcircuit;
	int ret;
	plid_t id;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto notfound;

	id = (plid_t)ret;
	if (id >= plant->hcircuits.last)
		goto notfound;

	hcircuit = &plant->hcircuits.all[id];
	if (!hcircuit)
		goto notfound;

	if (!aler(&hcircuit->run.online))
		goto offline;

	if (g_strcmp0(method_name, "SetTempOffsetOverride") == 0) {
		gdouble offset;
		g_variant_get(parameters, "(d)", &offset);
		aser(&hcircuit->overrides.t_offset, deltaK_to_tempdiff(offset));
	}
	else if (g_strcmp0(method_name, "SetRunmodeOverride") == 0) {
		guint8 runmode;
		g_variant_get(parameters, "(y)", &runmode);
		if ((runmode >= 0) && (runmode < RM_UNKNOWN)) {
			aser(&hcircuit->overrides.runmode, runmode);
			aser(&hcircuit->overrides.o_runmode, true);
		}
		else
			goto invalid;
	}
	else if (g_strcmp0(method_name, "DisableRunmodeOverride") == 0)
		aser(&hcircuit->overrides.o_runmode, false);

	g_dbus_method_invocation_return_value(invocation, NULL);
	return;

notfound:
	g_dbus_method_invocation_return_dbus_error(invocation,
						   DBUS_HCIRCUIT_IFACE ".Error.Failed",
						   "Hcircuit not found");
	return;
offline:
	g_dbus_method_invocation_return_dbus_error(invocation,
						   DBUS_HCIRCUIT_IFACE ".Error.Failed",
						   "Hcircuit offline");
	return;
invalid:
	g_dbus_method_invocation_return_dbus_error(invocation,
						   DBUS_HCIRCUIT_IFACE ".Error.Failed",
						   "Invalid argument");
	return;
}



static GVariant *
hcircuit_get_property(GDBusConnection  *connection,
		      const gchar      *sender,
		      const gchar      *object_path,
		      const gchar      *interface_name,
		      const gchar      *property_name,
		      GError          **error,
		      gpointer          user_data)
{
	GVariant *var;
	const gchar *node;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	const struct s_hcircuit * restrict hcircuit;
	plid_t id;
	temp_t temp;
	int ret;

	var = NULL;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto out;

	id = (plid_t)ret;
	if (id >= plant->hcircuits.last)
		goto out;

	hcircuit = &plant->hcircuits.all[id];

	if (g_strcmp0(property_name, "Online") == 0)
		var = g_variant_new_boolean((gboolean)aler(&hcircuit->run.online));
	else if (g_strcmp0(property_name, "Name") == 0)
		var = g_variant_new_string(hcircuit->name);
	else if (g_strcmp0(property_name, "RunMode") == 0) {
		const enum e_runmode runmode = aler(&hcircuit->overrides.o_runmode) ? aler(&hcircuit->overrides.runmode) : hcircuit->set.runmode;
		var = g_variant_new_byte((guchar)runmode);
	}
	else if (g_strcmp0(property_name, "RunModeOverride") == 0)
		var = g_variant_new_boolean((gboolean)aler(&hcircuit->overrides.o_runmode));
	else if (g_strcmp0(property_name, "TempOffsetOverride") == 0) {
		temp = aler(&hcircuit->overrides.t_offset);
		var = g_variant_new_double(temp_to_deltaK((tempdiff_t)temp));
	}
	else if (g_str_has_prefix(property_name, "Temp")) {
		property_name += strlen("Temp");
		if (g_strcmp0(property_name, "Comfort") == 0)
			temp = SETorDEF(hcircuit->set.params.t_comfort, hcircuit->pdata->set.def_hcircuit.t_comfort);
		else if (g_strcmp0(property_name, "Eco") == 0)
			temp = SETorDEF(hcircuit->set.params.t_eco, hcircuit->pdata->set.def_hcircuit.t_eco);
		else if (g_strcmp0(property_name, "FrostFree") == 0)
			temp = SETorDEF(hcircuit->set.params.t_frostfree, hcircuit->pdata->set.def_hcircuit.t_frostfree);
		temp += SETorDEF(hcircuit->set.params.t_offset, hcircuit->pdata->set.def_hcircuit.t_offset);
		var = g_variant_new_double(temp_to_celsius(temp));
	}
	else if (g_str_has_prefix(property_name, "OutOff")) {
		property_name += strlen("OutOff");
		if (g_strcmp0(property_name, "Comfort") == 0)
			temp = SETorDEF(hcircuit->set.params.outhoff_comfort, hcircuit->pdata->set.def_hcircuit.outhoff_comfort);
		else if (g_strcmp0(property_name, "Eco") == 0)
			temp = SETorDEF(hcircuit->set.params.outhoff_eco, hcircuit->pdata->set.def_hcircuit.outhoff_eco);
		else if (g_strcmp0(property_name, "FrostFree") == 0)
			temp = SETorDEF(hcircuit->set.params.outhoff_frostfree, hcircuit->pdata->set.def_hcircuit.outhoff_frostfree);
		var = g_variant_new_double(temp_to_celsius(temp));
	}
	else if (g_strcmp0(property_name, "InOffTemp") == 0) {
		temp = SETorDEF(hcircuit->set.params.inoff_temp, hcircuit->pdata->set.def_hcircuit.inoff_temp);
		var = g_variant_new_double(temp_to_celsius(temp));
	}
	else {
		if (g_strcmp0(property_name, "AmbientRequest") == 0)
			temp = aler(&hcircuit->run.request_ambient);
		else if (g_strcmp0(property_name, "AmbientActual") == 0)
			temp = aler(&hcircuit->run.actual_ambient);
		else if (g_strcmp0(property_name, "WtempTarget") == 0)
			temp = aler(&hcircuit->run.target_wtemp);
		else if (g_strcmp0(property_name, "WtempActual") == 0)
			temp = aler(&hcircuit->run.actual_wtemp);
		else
			g_assert_not_reached();
		var = g_variant_new_double(temp_to_celsius(temp));
	}

out:
	if (!var)
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Error");

	return var;
}

static const GDBusInterfaceVTable hcircuit_vtable = {
	hcircuit_method_call,
	hcircuit_get_property,
	//hcircuit_set_property,
};

/* DHWT */

static void
dhwt_method_call(GDBusConnection       *connection,
		 const gchar           *sender,
		 const gchar           *object_path,
		 const gchar           *interface_name,
		 const gchar           *method_name,
		 GVariant              *parameters,
		 GDBusMethodInvocation *invocation,
		 gpointer               user_data)
{
	const gchar *node;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	struct s_dhwt * restrict dhwt;
	int ret;
	plid_t id;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto notfound;

	id = (plid_t)ret;
	if (id >= plant->dhwts.last)
		goto notfound;

	dhwt = &plant->dhwts.all[id];
	if (!dhwt)
		goto notfound;

	if (!aler(&dhwt->run.online))
		goto offline;

	if (g_strcmp0(method_name, "SetRunmodeOverride") == 0) {
		guint8 runmode;
		g_variant_get(parameters, "(y)", &runmode);
		if ((runmode >= 0) && (runmode < RM_UNKNOWN)) {
			aser(&dhwt->overrides.runmode, runmode);
			aser(&dhwt->overrides.o_runmode, true);
		}
		else
			goto invalid;
	}
	else if (g_strcmp0(method_name, "DisableRunmodeOverride") == 0)
		aser(&dhwt->overrides.o_runmode, false);

	g_dbus_method_invocation_return_value(invocation, NULL);
	return;

notfound:
	g_dbus_method_invocation_return_dbus_error(invocation,
						   DBUS_DHWT_IFACE ".Error.Failed",
						   "DHWT not found");
	return;
offline:
	g_dbus_method_invocation_return_dbus_error(invocation,
						   DBUS_DHWT_IFACE ".Error.Failed",
						   "DHWT offline");
	return;
invalid:
	g_dbus_method_invocation_return_dbus_error(invocation,
						   DBUS_DHWT_IFACE ".Error.Failed",
						   "Invalid argument");
	return;
}



static GVariant *
dhwt_get_property(GDBusConnection  *connection,
		  const gchar      *sender,
		  const gchar      *object_path,
		  const gchar      *interface_name,
		  const gchar      *property_name,
		  GError          **error,
		  gpointer          user_data)
{
	GVariant *var;
	const gchar *node;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	const struct s_dhwt * restrict dhwt;
	plid_t id;
	temp_t temp;
	int ret;

	var = NULL;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto out;

	id = (plid_t)ret;
	if (id >= plant->dhwts.last)
		goto out;

	dhwt = &plant->dhwts.all[id];

	if (g_strcmp0(property_name, "Online") == 0)
		var = g_variant_new_boolean((gboolean)aler(&dhwt->run.online));
	else if (g_strcmp0(property_name, "Name") == 0)
		var = g_variant_new_string(dhwt->name);
	else if (g_strcmp0(property_name, "RunMode") == 0) {
		const enum e_runmode runmode = aler(&dhwt->overrides.o_runmode) ? aler(&dhwt->overrides.runmode) : dhwt->set.runmode;
		var = g_variant_new_byte((guchar)runmode);
	}
	else if (g_strcmp0(property_name, "RunModeOverride") == 0)
		var = g_variant_new_boolean((gboolean)aler(&dhwt->overrides.o_runmode));
	else if (g_strcmp0(property_name, "ForceChargeOn") == 0)
		var = g_variant_new_boolean((gboolean)aler(&dhwt->run.force_on));
	else if (g_strcmp0(property_name, "LegionellaOn") == 0)
		var = g_variant_new_boolean((gboolean)aler(&dhwt->run.legionella_on));
	else if (g_strcmp0(property_name, "RecycleOn") == 0)
		var = g_variant_new_boolean((gboolean)aler(&dhwt->run.recycle_on));
	else if (g_strcmp0(property_name, "ElectricModeOn") == 0)
		var = g_variant_new_boolean((gboolean)aler(&dhwt->run.electric_mode));
	else if (g_str_has_prefix(property_name, "Temp")) {
		property_name += strlen("Temp");
		if (g_strcmp0(property_name, "Comfort") == 0)
			temp = SETorDEF(dhwt->set.params.t_comfort, dhwt->pdata->set.def_dhwt.t_comfort);
		else if (g_strcmp0(property_name, "Eco") == 0)
			temp = SETorDEF(dhwt->set.params.t_eco, dhwt->pdata->set.def_dhwt.t_eco);
		else if (g_strcmp0(property_name, "FrostFree") == 0)
			temp = SETorDEF(dhwt->set.params.t_frostfree, dhwt->pdata->set.def_dhwt.t_frostfree);
		else if (g_strcmp0(property_name, "Legionella") == 0)
			temp = SETorDEF(dhwt->set.params.t_legionella, dhwt->pdata->set.def_dhwt.t_legionella);
		else if (g_strcmp0(property_name, "Target") == 0)
			temp = aler(&dhwt->run.target_temp);
		else if (g_strcmp0(property_name, "Current") == 0)
			temp = aler(&dhwt->run.actual_temp);
		var = g_variant_new_double(temp_to_celsius(temp));
	}
	else
		g_assert_not_reached();

out:
	if (!var)
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Error");

	return var;
}

static gboolean
dhwt_set_property(GDBusConnection  *connection,
		  const gchar      *sender,
		  const gchar      *object_path,
		  const gchar      *interface_name,
		  const gchar      *property_name,
		  GVariant         *value,
		  GError          **error,
		  gpointer          user_data)
{
	const gchar *node;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	const struct s_dhwt * restrict dhwt;
	plid_t id;
	int ret;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto error;

	id = (plid_t)ret;
	if (id >= plant->dhwts.last)
		goto error;

	dhwt = &plant->dhwts.all[id];

	if (g_strcmp0(property_name, "ForceChargeOn") == 0) {
		gboolean on = g_variant_get_boolean(value);
		aser(&dhwt->run.force_on, on);
	}
	else if (g_strcmp0(property_name, "LegionellaOn") == 0) {
		gboolean on = g_variant_get_boolean(value);
		aser(&dhwt->run.legionella_on, on);
	}
	else
		g_assert_not_reached();

	return TRUE;

error:
	g_set_error_literal(error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Invalid argument");
	return FALSE;
}

static const GDBusInterfaceVTable dhwt_vtable = {
	dhwt_method_call,
	dhwt_get_property,
	dhwt_set_property,
};

/* Heatsource */

static GVariant *
heatsource_get_property(GDBusConnection  *connection,
			const gchar      *sender,
			const gchar      *object_path,
			const gchar      *interface_name,
			const gchar      *property_name,
			GError          **error,
			gpointer          user_data)
{
	GVariant *var;
	const gchar *node;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	const struct s_heatsource * restrict heat;
	plid_t id;
	int ret;

	var = NULL;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto out;

	id = (plid_t)ret;
	if (id >= plant->heatsources.last)
		goto out;

	heat = &plant->heatsources.all[id];

	if (g_strcmp0(property_name, "Online") == 0)
		var = g_variant_new_boolean((gboolean)aler(&heat->run.online));
	else if (g_strcmp0(property_name, "Name") == 0)
		var = g_variant_new_string(heat->name);
	else if (g_strcmp0(property_name, "RunMode") == 0) {
		const enum e_runmode runmode = heat->set.runmode;
		var = g_variant_new_byte((guchar)runmode);
	}
	else if (g_strcmp0(property_name, "Overtemp") == 0)
		var = g_variant_new_boolean((gboolean)aler(&heat->run.overtemp));
	else if (g_strcmp0(property_name, "TempRequest") == 0) {
		temp_t temp = aler(&heat->run.temp_request);
		var = g_variant_new_double(temp_to_celsius(temp));
	}
	else
		g_assert_not_reached();

out:
	if (!var)
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Error");

	return var;
}

static const GDBusInterfaceVTable heatsource_vtable = {
	NULL,
	heatsource_get_property,
	//NULL,
};

/* Pump */

static GVariant *
pump_get_property(GDBusConnection  *connection,
		  const gchar      *sender,
		  const gchar      *object_path,
		  const gchar      *interface_name,
		  const gchar      *property_name,
		  GError          **error,
		  gpointer          user_data)
{
	GVariant *var;
	const gchar *node, *name;
	const struct s_plant * restrict const plant = runtime_get()->plant;
	const struct s_pump * restrict pump;
	plid_t id;
	int ret;

	var = NULL;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto out;

	id = (plid_t)ret;
	if (id >= plant->pumps.last)
		goto out;

	pump = &plant->pumps.all[id];

	if (g_strcmp0(property_name, "Online") == 0)
		var = g_variant_new_boolean((gboolean)aler(&pump->run.online));
	else if (g_strcmp0(property_name, "Name") == 0)
		var = g_variant_new_string(pump->name);
	else if (g_strcmp0(property_name, "Active") == 0)
		var = g_variant_new_boolean((gboolean)aler(&pump->run.state));
	else
		g_assert_not_reached();

out:
	if (!var)
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Error");

	return var;
}

static const GDBusInterfaceVTable pump_vtable = {
	NULL,
	pump_get_property,
	//NULL,
};

/* Temperature */

static GVariant *
temperature_get_property(GDBusConnection  *connection,
			 const gchar      *sender,
			 const gchar      *object_path,
			 const gchar      *interface_name,
			 const gchar      *property_name,
			 GError          **error,
			 gpointer          user_data)
{
	GVariant *var;
	const gchar *node, *name;
	itid_t id;
	int ret;

	var = NULL;

	node = strrchr(object_path, '/') + 1;
	ret = atoi(node);
	if (ret < 0)
		goto out;

	id = (itid_t)ret + 1;	// XXX

	name = inputs_temperature_name(id);
	if (!name)
		goto out;

	if (g_strcmp0(property_name, "Name") == 0)
		var = g_variant_new_string(name);
	else if (g_strcmp0(property_name, "Value") == 0) {
		temp_t temp;
		ret = inputs_temperature_get(id, &temp);
		if (ALL_OK == ret)
			var = g_variant_new_double(temp_to_celsius(temp));
		else
			goto out;
	}
	else
		g_assert_not_reached();

out:
	if (!var)
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Error");

	return var;
}

static const GDBusInterfaceVTable temperature_vtable = {
	NULL,
	temperature_get_property,
	//NULL,
};

extern struct s_inputs Inputs;	// XXX

static gchar **
rwchcd_subtree_enumerate(GDBusConnection       *connection,
			 const gchar           *sender,
			 const gchar           *object_path,
			 gpointer               user_data)
{
	gchar **nodes;
	GPtrArray *p;

	// Note: D-Bus path element must only contain the ASCII characters "[A-Z][a-z][0-9]_"

	p = g_ptr_array_new();

	if (g_strcmp0(object_path, DBUS_HCIRCUITS_OBJECT) == 0) {
		const struct s_plant * restrict const plant = runtime_get()->plant;
		for (plid_t id = 0; id < plant->hcircuits.last; id++)
			g_ptr_array_add(p, g_strdup_printf("%d", id));
	}
	else if (g_strcmp0(object_path, DBUS_DHWTS_OBJECT) == 0) {
		const struct s_plant * restrict const plant = runtime_get()->plant;
		for (plid_t id = 0; id < plant->dhwts.last; id++)
			g_ptr_array_add(p, g_strdup_printf("%d", id));
	}
	else if (g_strcmp0(object_path, DBUS_HEATSRCS_OBJECT) == 0) {
		const struct s_plant * restrict const plant = runtime_get()->plant;
		for (plid_t id = 0; id < plant->heatsources.last; id++)
			g_ptr_array_add(p, g_strdup_printf("%d", id));
	}
	else if (g_strcmp0(object_path, DBUS_PUMPS_OBJECT) == 0) {
		const struct s_plant * restrict const plant = runtime_get()->plant;
		for (plid_t id = 0; id < plant->pumps.last; id++)
			g_ptr_array_add(p, g_strdup_printf("%d", id));
	}
	else if (g_strcmp0(object_path, DBUS_TEMPS_OBJECT) == 0) {
		for (itid_t id = 0; id < Inputs.temps.last; id++)
			g_ptr_array_add(p, g_strdup_printf("%d", id));
	}

	g_ptr_array_add(p, NULL);
	nodes = (gchar **)g_ptr_array_free(p, FALSE);

	return nodes;
}

static GDBusInterfaceInfo **
rwchcd_subtree_introspect(GDBusConnection       *connection,
			  const gchar           *sender,
			  const gchar           *object_path,
			  const gchar           *node,
			  gpointer               user_data)
{
	GPtrArray *p;

	p = g_ptr_array_new();

	if (g_str_has_prefix(object_path, DBUS_HCIRCUITS_OBJECT) && node)
		g_ptr_array_add(p, g_dbus_interface_info_ref(dbus_hcircuit_interface_info));
	else if (g_str_has_prefix(object_path, DBUS_DHWTS_OBJECT) && node)
		g_ptr_array_add(p, g_dbus_interface_info_ref(dbus_dhwt_interface_info));
	else if (g_str_has_prefix(object_path, DBUS_HEATSRCS_OBJECT) && node)
		g_ptr_array_add(p, g_dbus_interface_info_ref(dbus_heatsrc_interface_info));
	else if (g_str_has_prefix(object_path, DBUS_PUMPS_OBJECT) && node)
		g_ptr_array_add(p, g_dbus_interface_info_ref(dbus_pump_interface_info));
	else if (g_str_has_prefix(object_path, DBUS_TEMPS_OBJECT) && node)
		g_ptr_array_add(p, g_dbus_interface_info_ref(dbus_temp_interface_info));

	g_ptr_array_add(p, NULL);

	return (GDBusInterfaceInfo **)g_ptr_array_free(p, FALSE);
}

static const GDBusInterfaceVTable *
rwchcd_subtree_dispatch(GDBusConnection             *connection,
			const gchar                 *sender,
			const gchar                 *object_path,
			const gchar                 *interface_name,
			const gchar                 *node,
			gpointer                    *out_user_data,
			gpointer                     user_data)
{
	const GDBusInterfaceVTable *vtable_to_return;
	gpointer user_data_to_return;

	if (g_strcmp0(interface_name, DBUS_HCIRCUIT_IFACE) == 0)
		vtable_to_return = &hcircuit_vtable;
	else if (g_strcmp0(interface_name, DBUS_DHWT_IFACE) == 0)
		vtable_to_return = &dhwt_vtable;
	else if (g_strcmp0(interface_name, DBUS_HEATSRC_IFACE) == 0)
		vtable_to_return = &heatsource_vtable;
	else if (g_strcmp0(interface_name, DBUS_PUMP_IFACE) == 0)
		vtable_to_return = &pump_vtable;
	else if (g_strcmp0(interface_name, DBUS_TEMP_IFACE) == 0)
		vtable_to_return = &temperature_vtable;
	else
		g_assert_not_reached ();

	return vtable_to_return;
}

static const GDBusSubtreeVTable rwchcd_subtree_vtable =
{
	rwchcd_subtree_enumerate,
	rwchcd_subtree_introspect,
	rwchcd_subtree_dispatch
};



static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	guint registration_id;

	registration_id = g_dbus_connection_register_object(connection,
							    DBUS_OBJECT_BASE,
							    dbus_runtime_interface_info,
							    &runtime_vtable,
							    NULL,  /* user_data */
							    NULL,  /* user_data_free_func */
							    NULL); /* GError** */
	g_assert (registration_id > 0);

	registration_id = g_dbus_connection_register_subtree(connection,
							     DBUS_HCIRCUITS_OBJECT,
							     &rwchcd_subtree_vtable,
							     G_DBUS_SUBTREE_FLAGS_NONE,
							     NULL,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);

	registration_id = g_dbus_connection_register_subtree(connection,
							     DBUS_DHWTS_OBJECT,
							     &rwchcd_subtree_vtable,
							     G_DBUS_SUBTREE_FLAGS_NONE,
							     NULL,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);

	registration_id = g_dbus_connection_register_subtree(connection,
							     DBUS_HEATSRCS_OBJECT,
							     &rwchcd_subtree_vtable,
							     G_DBUS_SUBTREE_FLAGS_NONE,
							     NULL,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);

	registration_id = g_dbus_connection_register_subtree(connection,
							     DBUS_PUMPS_OBJECT,
							     &rwchcd_subtree_vtable,
							     G_DBUS_SUBTREE_FLAGS_NONE,
							     NULL,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);

	registration_id = g_dbus_connection_register_subtree(connection,
							     DBUS_TEMPS_OBJECT,
							     &rwchcd_subtree_vtable,
							     G_DBUS_SUBTREE_FLAGS_NONE,
							     NULL,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);
}

/**
 * D-Bus name acquired handler.
 * Connects the D-Bus custom method handlers, and exports the Object and Interface.
 */
static void on_name_acquired(GDBusConnection *connection,
			     __attribute__((unused)) const gchar *name,
			     __attribute__((unused)) gpointer user_data)
{
}

/**
 * D-Bus name lost handler.
 * Signals name could not be acquired or has been lost.
 */
static void on_name_lost(GDBusConnection *connection,
			 const gchar *name,
			 __attribute__((unused)) gpointer user_data)
{
	dbgerr("Could not acquire name \"%s\", connection is %p", name, connection);	// pr_warn()
}

/**
 * Gracefully quit D-Bus subsystem.
 */
void dbus_quit(void)
{
	g_main_loop_quit(Mainloop);
}

/**
 * D-Bus subsystem main thread.
 * @note Blocking
 */
int dbus_main(void)
{
	guint owner_id;
	
	dbus_introspection_data = g_dbus_node_info_new_for_xml(dbus_introspection_xml, NULL);
	g_assert(dbus_introspection_data != NULL);

	dbus_runtime_interface_info = g_dbus_node_info_lookup_interface(dbus_introspection_data, DBUS_RUNTIME_IFACE);
	g_assert(dbus_runtime_interface_info != NULL);

	dbus_hcircuit_interface_info = g_dbus_node_info_lookup_interface(dbus_introspection_data, DBUS_HCIRCUIT_IFACE);
	g_assert(dbus_hcircuit_interface_info != NULL);

	dbus_dhwt_interface_info = g_dbus_node_info_lookup_interface(dbus_introspection_data, DBUS_DHWT_IFACE);
	g_assert(dbus_dhwt_interface_info != NULL);

	dbus_heatsrc_interface_info = g_dbus_node_info_lookup_interface(dbus_introspection_data, DBUS_HEATSRC_IFACE);
	g_assert(dbus_heatsrc_interface_info != NULL);

	dbus_pump_interface_info = g_dbus_node_info_lookup_interface(dbus_introspection_data, DBUS_PUMP_IFACE);
	g_assert(dbus_pump_interface_info != NULL);

	dbus_temp_interface_info = g_dbus_node_info_lookup_interface(dbus_introspection_data, DBUS_TEMP_IFACE);
	g_assert(dbus_temp_interface_info != NULL);

	// register on dbus
	owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
				  DBUS_IFACE_BASE,
				  G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
				  on_bus_acquired,
				  on_name_acquired,
				  on_name_lost,
				  NULL,
				  NULL);
	
	// loop forever
	Mainloop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(Mainloop);
	
	// cleanup
	g_bus_unown_name(owner_id);
	g_main_loop_unref(Mainloop);
	
	return 0;
}
