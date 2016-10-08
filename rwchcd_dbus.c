//
//  rwchcd_dbus.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#include "rwchcd_lib.h"
#include "rwchcd_runtime.h"
#include "rwchcd_dbus.h"
#include "rwchcd_dbus-generated.h"


static GMainLoop *Mainloop = NULL;

/**
 * D-Bus method ToutdoorGet handler.
 * Replies with current outdoor temperature.
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
	enum e_systemmode cursys;
	
	//	pthread_rwlock_rdlock();
	cursys = get_runtime()->systemmode;
	//	pthread_rwlock_unlock();
	
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
	enum e_systemmode newsysmode;
	
	if (Sysmode < SYS_UNKNOWN)
		newsysmode = Sysmode;
	else
		return FALSE;

	//	pthread_rwlock_wrlock();
	runtime_set_systemmode(newsysmode);
	//	pthread_rwlock_unlock();
	
	dbus_rwchcd_control_complete_sysmode_set(object, invocation);
	
	return TRUE;
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
