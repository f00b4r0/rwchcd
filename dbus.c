//
//  dbus.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
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

#include "lib.h"
#include "runtime.h"
#include "models.h"	// models_outtemp()
#include "plant.h"
#include "dbus.h"
#include "dbus-generated.h"


static GMainLoop *Mainloop = NULL;

/**
 * D-Bus method ToutdoorGet handler.
 * Replies with current outdoor temperature.
 * @bug we don't lock because we don't care
 */
static gboolean on_handle_toutdoor_get(dbusRwchcdControl *object,
				      GDBusMethodInvocation *invocation,
				      __attribute__((unused)) gpointer user_data)
{
	float temp = temp_to_celsius(models_outtemp());
	
	dbus_rwchcd_control_complete_toutdoor_get(object, invocation, temp);
	
	return TRUE;
}

/**
 * D-Bus method SysmodeGet handler.
 * Replies with current system mode.
 */
static gboolean on_handle_sysmode_get(dbusRwchcdControl *object,
				      GDBusMethodInvocation *invocation,
				      __attribute__((unused)) gpointer user_data)
{
	struct s_runtime * restrict const runtime = runtime_get();
	enum e_systemmode cursys;
	
	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	cursys = runtime->run.systemmode;
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
				      __attribute__((unused)) gpointer user_data)
{
	struct s_runtime * restrict const runtime = runtime_get();
	enum e_systemmode newsysmode;
	
	if ((Sysmode > SYS_NONE) && (Sysmode < SYS_UNKNOWN))
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
 * D-Bus method ConfigTempModeGet handler.
 * Replies with current config default circuit temperature for the given runmode. XXX QUICK HACK.
 * @param Runmode target runmode for query
 * @note only handles default circuit temp for now.
 * @todo make it generic to get any config temp
 */
static gboolean on_handle_config_temp_mode_get(dbusRwchcdControl *object,
					       GDBusMethodInvocation *invocation,
					       guchar Runmode,
					       __attribute__((unused)) gpointer user_data)
{
	struct s_runtime * restrict const runtime = runtime_get();
	enum e_runmode runmode = Runmode;
	temp_t systemp;
	float temp;
	bool err = false;
	
	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	switch (runmode) {
		case RM_COMFORT:
			systemp = runtime->plant->pdata.set.def_hcircuit.t_comfort;
			break;
		case RM_ECO:
			systemp = runtime->plant->pdata.set.def_hcircuit.t_eco;
			break;
		case RM_FROSTFREE:
			systemp = runtime->plant->pdata.set.def_hcircuit.t_frostfree;
			break;
		default:
			err = true;
			break;
	}
	pthread_rwlock_unlock(&runtime->runtime_rwlock);
	
	if (err)
		return false;
	
	temp = temp_to_celsius(systemp);
	
	dbus_rwchcd_control_complete_config_temp_mode_get(object, invocation, temp);
	
	return true;
}

/**
 * D-Bus method ConfigTempModeSet handler.
 * Sets the desired config default circuit temperature for the given runmode.
 * @param Runmode target runmode for the new temp
 * @param NewTemp new temperature value
 * @note only handles default circuit temp for now.
 * @warning doesn't save runtime after set
 * @deprecated XXX QUICK HACK incompatible with file-based configuration.
 */
__attribute__ ((deprecated))
static gboolean on_handle_config_temp_mode_set(dbusRwchcdControl *object,
					       GDBusMethodInvocation *invocation,
					       guchar Runmode,
					       gdouble NewTemp,
					       __attribute__((unused)) gpointer user_data)
{
	struct s_runtime * restrict const runtime = runtime_get();
	temp_t newtemp = celsius_to_temp(NewTemp);
	enum e_runmode runmode = Runmode;
	bool err = false;

	if (validate_temp(newtemp) != ALL_OK)
		return false;

	pthread_rwlock_wrlock(&runtime->runtime_rwlock);
	switch (runmode) {
		case RM_COMFORT:
			runtime->plant->pdata.set.def_hcircuit.t_comfort = newtemp;
			break;
		case RM_ECO:
			runtime->plant->pdata.set.def_hcircuit.t_eco = newtemp;
			break;
		case RM_FROSTFREE:
			runtime->plant->pdata.set.def_hcircuit.t_frostfree = newtemp;
			break;
		default:
			err = true;
			break;
	}
	pthread_rwlock_unlock(&runtime->runtime_rwlock);

	if (err)
		return false;
	
	dbus_rwchcd_control_complete_config_temp_mode_set(object, invocation);
	
	return true;
}

/**
 * D-Bus method ConfigOuthoffModeGet handler.
 * Replies with current config default circuit outhoff temp for the given runmode. XXX QUICK HACK.
 * @param Runmode target runmode for query
 * @note only handles default circuit outhoff temp for now.
 * @todo make it generic to set any config temp
 */
static gboolean on_handle_config_outhoff_mode_get(dbusRwchcdControl *object,
					       GDBusMethodInvocation *invocation,
					       guchar Runmode,
					       __attribute__((unused)) gpointer user_data)
{
	struct s_runtime * restrict const runtime = runtime_get();
	enum e_runmode runmode = Runmode;
	temp_t systemp;
	float temp;
	bool err = false;

	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	switch (runmode) {
		case RM_COMFORT:
			systemp = runtime->plant->pdata.set.def_hcircuit.outhoff_comfort;
			break;
		case RM_ECO:
			systemp = runtime->plant->pdata.set.def_hcircuit.outhoff_eco;
			break;
		case RM_FROSTFREE:
			systemp = runtime->plant->pdata.set.def_hcircuit.outhoff_frostfree;
			break;
		default:
			err = true;
			break;
	}
	pthread_rwlock_unlock(&runtime->runtime_rwlock);

	if (err)
		return false;

	temp = temp_to_celsius(systemp);

	dbus_rwchcd_control_complete_config_outhoff_mode_get(object, invocation, temp);

	return true;
}

/**
 * D-Bus method ConfigOuthoffModeSet handler.
 * Sets the desired config default circuit outhoff temp for the given runmode.
 * @param Runmode target runmode for the new temp
 * @param NewTemp new temperature value
 * @note only handles default circuit outhoff temp for now.
 * @warning doesn't save runtime after set
 * @deprecated XXX QUICK HACK incompatible with file-based configuration.
 */
__attribute__ ((deprecated))
static gboolean on_handle_config_outhoff_mode_set(dbusRwchcdControl *object,
					       GDBusMethodInvocation *invocation,
					       guchar Runmode,
					       gdouble NewTemp,
					       __attribute__((unused)) gpointer user_data)
{
	struct s_runtime * restrict const runtime = runtime_get();
	temp_t newtemp = celsius_to_temp(NewTemp);
	enum e_runmode runmode = Runmode;
	bool err = false;

	if (validate_temp(newtemp) != ALL_OK)
		return false;

	pthread_rwlock_wrlock(&runtime->runtime_rwlock);
	switch (runmode) {
		case RM_COMFORT:
			runtime->plant->pdata.set.def_hcircuit.outhoff_comfort = newtemp;
			break;
		case RM_ECO:
			runtime->plant->pdata.set.def_hcircuit.outhoff_eco = newtemp;
			break;
		case RM_FROSTFREE:
			runtime->plant->pdata.set.def_hcircuit.outhoff_frostfree = newtemp;
			break;
		default:
			err = true;
			break;
	}
	pthread_rwlock_unlock(&runtime->runtime_rwlock);

	if (err)
		return false;

	dbus_rwchcd_control_complete_config_outhoff_mode_set(object, invocation);

	return true;
}

/**
 * D-Bus name acquired handler.
 * Connects the D-Bus custom method handlers, and exports the Object and Interface.
 */
static void on_name_acquired(GDBusConnection *connection,
			     __attribute__((unused)) const gchar *name,
			     __attribute__((unused)) gpointer user_data)
{
	dbusRwchcdControl *skeleton = dbus_rwchcd_control_skeleton_new();
	g_signal_connect(skeleton, "handle-sysmode-set", G_CALLBACK(on_handle_sysmode_set), NULL);
	g_signal_connect(skeleton, "handle-sysmode-get", G_CALLBACK(on_handle_sysmode_get), NULL);
	g_signal_connect(skeleton, "handle-toutdoor-get", G_CALLBACK(on_handle_toutdoor_get), NULL);
	g_signal_connect(skeleton, "handle-config-temp-mode-get", G_CALLBACK(on_handle_config_temp_mode_get), NULL);
	g_signal_connect(skeleton, "handle-config-temp-mode-set", G_CALLBACK(on_handle_config_temp_mode_set), NULL);
	g_signal_connect(skeleton, "handle-config-outhoff-mode-get", G_CALLBACK(on_handle_config_outhoff_mode_get), NULL);
	g_signal_connect(skeleton, "handle-config-outhoff-mode-set", G_CALLBACK(on_handle_config_outhoff_mode_set), NULL);
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
