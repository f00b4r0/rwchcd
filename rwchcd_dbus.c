//
//  rwchcd_dbus.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
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
 * @note the D-Bus handler lives in a separate thread: beware of synchronisation.
 * Currently no locking is being used based on the fact that the minor inconsistency
 * triggered by a concurrent modification will not have nefarious effects and will
 * only last for 1s at most (between two consecutive runs of the master thread),
 * and thus does not warranty the performance penalty of using locks everywhere.
 * XXX REVIEW
 */

#include "rwchcd_lib.h"
#include "rwchcd_runtime.h"
#include "rwchcd_dbus.h"
#include "rwchcd_dbus-generated.h"


static GMainLoop *Mainloop = NULL;

/**
 * D-Bus method ToutdoorGet handler.
 * Replies with current outdoor temperature.
 * @bug we don't lock because we don't care
 */
static gboolean on_handle_toutdoor_get(dbusRwchcdControl *object,
				      GDBusMethodInvocation *invocation,
				      gpointer user_data)
{
	float temp = temp_to_celsius(get_runtime()->t_outdoor_60);
	
	dbus_rwchcd_control_complete_toutdoor_get(object, invocation, temp);
	
	return TRUE;
}

/**
 * D-Bus method SysmodeGet handler.
 * Replies with current system mode.
 */
static gboolean on_handle_sysmode_get(dbusRwchcdControl *object,
				      GDBusMethodInvocation *invocation,
				      gpointer user_data)
{
	struct s_runtime * restrict const runtime = get_runtime();
	enum e_systemmode cursys;
	
	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	cursys = runtime->systemmode;
	pthread_rwlock_unlock(&runtime->runtime_rwlock);
	
	dbus_rwchcd_control_complete_sysmode_get(object, invocation, (guchar)cursys);
	
	return TRUE;
}

/**
 * D-Bus method SystemSet handler.
 * Sets the desired system mode.
 * @param Sysmode Target system mode
 */
static gboolean on_handle_sysmode_set(dbusRwchcdControl *object,
				      GDBusMethodInvocation *invocation,
				      guchar Sysmode,
				      gpointer user_data)
{
	struct s_runtime * restrict const runtime = get_runtime();
	enum e_systemmode newsysmode;
	
	if (Sysmode < SYS_UNKNOWN)
		newsysmode = Sysmode;
	else
		return FALSE;

	pthread_rwlock_wrlock(&runtime->runtime_rwlock);
	runtime_set_systemmode(newsysmode);
	pthread_rwlock_unlock(&runtime->runtime_rwlock);
	
	dbus_rwchcd_control_complete_sysmode_set(object, invocation);
	
	return TRUE;
}

/**
 * D-Bus method ConfigTempGet handler.
 * Replies with current config temperature for the current sysmode. XXX QUICK HACK.
 * @note only handles default circuit comfort temp for now.
 * @todo make it generic to set any config temp
 */
static gboolean on_handle_config_temp_get(dbusRwchcdControl *object,
				       GDBusMethodInvocation *invocation,
				       gpointer user_data)
{
	struct s_runtime * restrict const runtime = get_runtime();
	enum e_systemmode cursys;
	temp_t systemp;
	float temp;
	
	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	cursys = runtime->systemmode;
	switch (cursys) {
		case SYS_COMFORT:
			systemp = runtime->config->def_circuit.t_comfort;
			break;
		case SYS_ECO:
			systemp = runtime->config->def_circuit.t_eco;
			break;
		case SYS_FROSTFREE:
			systemp = runtime->config->def_circuit.t_frostfree;
			break;
		default:
			return false;
			break;
	}
	pthread_rwlock_unlock(&runtime->runtime_rwlock);
	
	temp = temp_to_celsius(systemp);
	
	dbus_rwchcd_control_complete_config_temp_get(object, invocation, temp);
	
	return true;
}

/**
 * D-Bus method ConfigTempSet handler.
 * Sets the desired config temperature for the current sysmode. XXX QUICK HACK.
 * @param temp new temperature value
 * @note only handles default circuit comfort temp for now.
 * @todo make it generic to set any config temp
 * @todo save config: cannot do for now because config_save() calls hardware
 * @warning doesn't save runtime after set
 */
static gboolean on_handle_config_temp_set(dbusRwchcdControl *object,
				      GDBusMethodInvocation *invocation,
				      gdouble NewTemp,
				      gpointer user_data)
{
	struct s_runtime * restrict const runtime = get_runtime();
	temp_t newtemp = celsius_to_temp(NewTemp);
	enum e_systemmode cursys;

	if (validate_temp(newtemp) != ALL_OK)
		return false;

	pthread_rwlock_wrlock(&runtime->runtime_rwlock);
	cursys = runtime->systemmode;
	switch (cursys) {
		case SYS_COMFORT:
			get_runtime()->config->def_circuit.t_comfort = newtemp;
			break;
		case SYS_ECO:
			get_runtime()->config->def_circuit.t_eco = newtemp;
			break;
		case SYS_FROSTFREE:
			get_runtime()->config->def_circuit.t_frostfree = newtemp;
			break;
		default:
			return false;
			break;
	}
	pthread_rwlock_unlock(&runtime->runtime_rwlock);

	dbus_rwchcd_control_complete_config_temp_set(object, invocation);
	
	return true;
}

/**
 * D-Bus name acquired handler.
 * Connects the D-Bus custom method handlers, and exports the Object and Interface.
 */
static void on_name_acquired(GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	dbusRwchcdControl *skeleton = dbus_rwchcd_control_skeleton_new();
	g_signal_connect(skeleton, "handle-sysmode-set", G_CALLBACK(on_handle_sysmode_set), NULL);
	g_signal_connect(skeleton, "handle-sysmode-get", G_CALLBACK(on_handle_sysmode_get), NULL);
	g_signal_connect(skeleton, "handle-toutdoor-get", G_CALLBACK(on_handle_toutdoor_get), NULL);
	g_signal_connect(skeleton, "handle-config-temp-get", G_CALLBACK(on_handle_config_temp_get), NULL);
	g_signal_connect(skeleton, "handle-config-temp-set", G_CALLBACK(on_handle_config_temp_set), NULL);
	g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton),
					 connection,
					 "/org/slashdirt/rwchcd",
					 NULL);
}

/**
 * D-Bus name lost handler.
 * Signals name could not be acquired or has been lost.
 */
static void on_name_lost(GDBusConnection *connection,
			 const gchar *name,
			 gpointer user_data)
{
	dbgerr("Could not acquire name %s, connection is %p", name, connection);
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
	
	// register on dbus
	owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
				  "org.slashdirt.rwchcd",
				  G_BUS_NAME_OWNER_FLAGS_NONE,
				  NULL,
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
