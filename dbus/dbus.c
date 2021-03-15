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
 * remote control over D-Bus.
 *
 * @bug will crash if any operation is attempted before the runtime/config structures
 * are properly set.
 *
 * @todo dynamic D-Bus objects creation (for each plant component:
 * Heat source / circuit / dhwt). See NetworkManager?
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "runtime.h"
#include "plant/plant.h"
#include "dbus.h"

#define DBUS_RUNTIME_IFACE "org.slashdirt.rwchcd.Runtime"
#define DBUS_CIRCUIT_IFACE "org.slashdirt.rwchcd.Circuit"

static GDBusNodeInfo *dbus_introspection_data = NULL;
static GDBusInterfaceInfo *dbus_runtime_interface_info = NULL;
static GDBusInterfaceInfo *dbus_circuit_interface_info = NULL;

static const gchar dbus_introspection_xml[] =
"<node>"
" <interface name='" DBUS_RUNTIME_IFACE "'>"
"  <property name='SystemMode' access='readwrite' type='y' />"
"  <property name='RunMode' access='readwrite' type='y' />"
"  <property name='DhwMode' access='readwrite' type='y' />"
" </interface>"
" <interface name='" DBUS_CIRCUIT_IFACE "'>"
"  <property name='RunMode' access='readwrite' type='y' />"
"  <property name='TempComfort' access='readwrite' type='d' />"
"  <property name='TempEco' access='readwrite' type='d' />"
"  <property name='TempFrostFree' access='readwrite' type='d' />"
"  <property name='OutOffComfort' access='readwrite' type='d' />"
"  <property name='OutOffEco' access='readwrite' type='d' />"
"  <property name='OutOffFrostFree' access='readwrite' type='d' />"
" </interface>"
"</node>";

static GMainLoop *Mainloop = NULL;

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

#if 0
	registration_id = g_dbus_connection_register_subtree (connection,
							"/org/gtk/GDBus/TestSubtree/Devices",
							&subtree_vtable,
							G_DBUS_SUBTREE_FLAGS_NONE,
							NULL,  /* user_data */
							NULL,  /* user_data_free_func */
							NULL); /* GError** */
	g_assert (registration_id > 0);
#endif
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

	// register on dbus
	owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
				  "org.slashdirt.rwchcd",
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
