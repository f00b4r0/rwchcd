//
//  dbus.c
//  rwchcd
//
//  (C) 2016-2018,2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * D-Bus implementation.
 *
 * This is a very basic (read: gross hack) implementation for bare minimal
 * remote control over D-Bus. It goes too low level into the API for its own good.
 *
 * @warning will crash if any operation is attempted before the runtime/config structures
 * are properly set.
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "runtime.h"
#include "plant/plant.h"
#include "plant/hcircuit.h"
#include "dbus.h"

#define DBUS_IFACE_BASE		"org.slashdirt.rwchcd"
#define DBUS_RUNTIME_IFACE	DBUS_IFACE_BASE ".Runtime"
#define DBUS_HCIRCUIT_IFACE	DBUS_IFACE_BASE ".Hcircuit"

static GDBusNodeInfo *dbus_introspection_data = NULL;
static GDBusInterfaceInfo *dbus_runtime_interface_info = NULL;
static GDBusInterfaceInfo *dbus_hcircuit_interface_info = NULL;

static const gchar dbus_introspection_xml[] =
"<node>"
" <interface name='" DBUS_RUNTIME_IFACE "'>"
"  <property name='SystemMode' access='readwrite' type='y' />"
"  <property name='RunMode' access='readwrite' type='y' />"
"  <property name='DhwMode' access='readwrite' type='y' />"
" </interface>"
" <interface name='" DBUS_HCIRCUIT_IFACE "'>"
"  <property name='Name' access='read' type='s' />"
"  <property name='RunModeOverride' access='read' type='b' />"
"  <property name='RunMode' access='read' type='y' />"
"  <property name='TempComfort' access='read' type='d' />"
"  <property name='TempEco' access='read' type='d' />"
"  <property name='TempFrostFree' access='read' type='d' />"
"  <property name='TempOffsetOverride' access='read' type='d' />"
"  <property name='TempTarget' access='read' type='d' />"
"  <property name='OutOffComfort' access='read' type='d' />"
"  <property name='OutOffEco' access='read' type='d' />"
"  <property name='OutOffFrostFree' access='read' type='d' />"
"  <method name='SetTempOffsetOverride'>"
"   <arg name='offset' direction='in' type='d' />"
"  </method>"
"  <method name='SetRunmodeOverride'>"
"   <arg name='runmode' direction='in' type='y' />"
"  </method>"
"  <method name='DisableRunmodeOverride' />"
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

	if (g_strcmp0(property_name, "Name") == 0)
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
	else if (g_strcmp0(property_name, "TempTarget") == 0) {
		temp = aler(&hcircuit->run.request_ambient);
		var = g_variant_new_double(temp_to_celsius(temp));
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

static const GDBusInterfaceVTable hcircuit_vtable = {
	hcircuit_method_call,
	hcircuit_get_property,
	//hcircuit_set_property,
};

static gchar **
hcircuit_subtree_enumerate(GDBusConnection       *connection,
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

	g_ptr_array_add(p, NULL);
	nodes = (gchar **)g_ptr_array_free(p, FALSE);

	return nodes;
}

static GDBusInterfaceInfo **
hcircuit_subtree_introspect(GDBusConnection       *connection,
			    const gchar           *sender,
			    const gchar           *object_path,
			    const gchar           *node,
			    gpointer               user_data)
{
	GPtrArray *p;

	p = g_ptr_array_new();

	if (node)
		g_ptr_array_add(p, g_dbus_interface_info_ref(dbus_hcircuit_interface_info));

	g_ptr_array_add(p, NULL);

	return (GDBusInterfaceInfo **)g_ptr_array_free(p, FALSE);
}

static const GDBusInterfaceVTable *
hcircuit_subtree_dispatch(GDBusConnection             *connection,
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
	else
		g_assert_not_reached ();

	return vtable_to_return;
}

const GDBusSubtreeVTable hcircuit_subtree_vtable =
{
	hcircuit_subtree_enumerate,
	hcircuit_subtree_introspect,
	hcircuit_subtree_dispatch
};



static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	guint registration_id;

	registration_id = g_dbus_connection_register_object(connection,
							    "/org/slashdirt/rwchcd",
							    dbus_runtime_interface_info,
							    &runtime_vtable,
							    NULL,  /* user_data */
							    NULL,  /* user_data_free_func */
							    NULL); /* GError** */
	g_assert (registration_id > 0);

	registration_id = g_dbus_connection_register_subtree(connection,
							     "/org/slashdirt/rwchcd/Hcircuits",
							     &hcircuit_subtree_vtable,
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
