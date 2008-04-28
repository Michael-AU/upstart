/* upstart
 *
 * control.c - D-Bus connections, objects and methods
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dbus/dbus.h>

#include <stdio.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <nih/dbus.h>

#include "control.h"
#include "errors.h"


/**
 * CONTROL_BUS_NAME:
 *
 * Well-known name that we register on the system bus so that clients may
 * contact us.
 **/
#define CONTROL_BUS_NAME "com.ubuntu.Upstart"

/**
 * CONTROL_ROOT:
 *
 * Well-known object name that we register for the manager object, and that we
 * use as the root path for all of our other objects.
 **/
#define CONTROL_ROOT "/com/ubuntu/Upstart"

/**
 * CONTROL_JOB_ROOT:
 *
 * Root path for all job objects, under the manager.
 **/
#define CONTROL_JOB_ROOT CONTROL_ROOT "/jobs"


/* Prototypes for static functions */
static void  control_bus_disconnected (DBusConnection *conn);
static int   control_register_all     (DBusConnection *conn);
static char *control_path_append      (char **path, const void *parent,
				       const char *name);


/**
 * control_bus:
 *
 * Open connection to D-Bus system bus.  The connection may be opened with
 * control_bus_open() and if lost will become NULL.
 **/
DBusConnection *control_bus = NULL;

/**
 * control_manager:
 *
 * Interfaces exported by the control manager object.
 **/
const static NihDBusInterface *control_manager[] = {
	NULL
};


/**
 * control_bus_open:
 *
 * Open a connection to the D-Bus system bus and store it in the control_bus
 * global.  The connection is handled automatically in the main loop and
 * will be closed should we exec() a different process.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_bus_open (void)
{
	DBusConnection *conn;
	DBusError       error;
	int             fd, ret;

	nih_assert (control_bus == NULL);

	/* Connect to the D-Bus System Bus and hook everything up into
	 * our own main loop automatically.
	 */
	conn = nih_dbus_bus (DBUS_BUS_SYSTEM, control_bus_disconnected);
	if (! conn)
		return -1;

	/* In theory all D-Bus file descriptors are set to be closed on exec
	 * anyway, but there's no harm in making damned sure since that's
	 * not actually documented anywhere that I can tell.
	 */
	if (dbus_connection_get_unix_fd (conn, &fd))
		nih_io_set_cloexec (fd);

	/* Register objects on the bus. */
	if (control_register_all (conn) < 0) {
		errno = ENOMEM;
		nih_error_raise_system ();

		dbus_connection_unref (conn);
		return -1;
	}

	/* Request our well-known name.  We do this last so that once it
	 * appears on the bus, clients can assume we're ready to talk to
	 * them.
	 */
	dbus_error_init (&error);
	ret = dbus_bus_request_name (conn, CONTROL_BUS_NAME,
				     DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
	if (ret < 0) {
		/* Error while requesting the name */
		nih_dbus_error_raise (error.name, error.message);
		dbus_error_free (&error);

		dbus_connection_unref (conn);
		return -1;
	} else if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		/* Failed to obtain the name (already taken usually) */
		nih_error_raise (CONTROL_NAME_TAKEN,
				 _(CONTROL_NAME_TAKEN_STR));

		dbus_connection_unref (conn);
		return -1;
	}

	control_bus = conn;

	return 0;
}

/**
 * control_bus_disconnected:
 *
 * This function is called when the connection to the D-Bus system bus is
 * dropped and our reference is about to be lost.  We simply clear the
 * control_bus global.
 **/
static void
control_bus_disconnected (DBusConnection *conn)
{
	nih_assert (conn != NULL);

	if (control_bus)
		nih_warn (_("Disconnected from system bus"));

	control_bus = NULL;
}

/**
 * control_bus_close:
 *
 * Close the connection to the D-Bus system bus.  Since the connection is
 * shared inside libdbus, this really only drops our reference to it so
 * it's possible to have method and signal handlers called even after calling
 * this (normally to dispatch what's in the queue).
 **/
void
control_bus_close (void)
{
	nih_assert (control_bus != NULL);

	dbus_connection_unref (control_bus);

	control_bus = NULL;
}


/**
 * control_register_all:
 * @conn: connection to register objects for.
 *
 * Registers the manager object and objects for all jobs and instances on
 * the given connection.
 **/
static int
control_register_all (DBusConnection *conn)
{

	/* Register the manager object, this is the primary point of contact
	 * for clients.  We only check for success, otherwise we're happy
	 * to let this object be tied to the lifetime of the connection.
	 */
	if (! nih_dbus_object_new (NULL, conn, CONTROL_ROOT, control_manager,
				   NULL))
		return -1;

	/* FIXME register objects for jobs and their instances */

	return 0;
}


/**
 * control_job_config_path:
 * @parent: parent of returned path,
 * @config_name: name of job config.
 *
 * Generates a D-Bus object path name for a job config named @config_name,
 * this will be rooted under the manager and any non-permissable characters
 * in the name will be escaped.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
control_job_config_path (const void *parent,
			 const char *config_name)
{
	char *path;

	nih_assert (config_name != NULL);

	path = nih_strdup (parent, CONTROL_JOB_ROOT);
	if (! path)
		return NULL;

	if (! control_path_append (&path, parent, config_name)) {
		nih_free (path);
		return NULL;
	}

	return path;
}

/**
 * control_job_path:
 * @parent: parent of returned path,
 * @config_name: name of job config,
 * @job_name: name of instance.
 *
 * Generates a D-Bus object path name for an instance of a job named
 * @config_name named @job_name, this will be rooted under the path for
 * the job itself and any non-permissable characters in the name will be
 * escaped.
 *
 * If @job_name is NULL (which is the case for non-instance jobs), the
 * string "active" will be substituted instead.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
control_job_path (const void *parent,
		  const char *config_name,
		  const char *job_name)
{
	char *path;

	nih_assert (config_name != NULL);

	path = control_job_config_path (parent, config_name);
	if (! path)
		return NULL;

	if (! control_path_append (&path, parent,
				   job_name ? job_name : "active")) {
		nih_free (path);
		return NULL;
	}

	return path;
}

/**
 * control_path_append:
 * @path: path to change,
 * @parent: parent of @path,
 * @name: element to add.
 *
 * Append @name to @path, escaping any non-permissable characters and
 * preceeding with a forwards slash.  Modifies @path in place.
 *
 * Returns: new string pointer on success or NULL if insufficient memory.
 **/
static char *
control_path_append (char       **path,
		     const void  *parent,
		     const char  *name)
{
	size_t      len, new_len;
	char       *ret;
	const char *s;

	nih_assert (path != NULL);
	nih_assert (*path != NULL);
	nih_assert (name != NULL);

	/* Calculate how much space we'll need first, makes the expansion
	 * easier in a moment.
	 */
	len = strlen (*path);
	new_len = len + 1;
	for (s = name; *s; s++) {
		new_len++;
		if (   ((*s < 'a') || (*s > 'z'))
		    && ((*s < 'A') || (*s > 'Z'))
		    && ((*s < '0') || (*s > '9')))
			new_len += 2;
	}

	/* Now we can just realloc to the desired size and fill it in. */
	ret = nih_realloc (*path, parent, new_len + 1);
	if (! ret)
		return NULL;

	/* Append the name, escaping as we go. */
	*path = ret;
	(*path)[len++] = '/';
	for (s = name; *s; s++) {
		if (   ((*s >= 'a') && (*s <= 'z'))
		    || ((*s >= 'A') && (*s <= 'Z'))
		    || ((*s >= '0') && (*s <= '9'))) {
			(*path)[len++] = *s;
		} else {
			sprintf (*path + len, "_%02x", *s);
			len += 3;
		}
	}
	(*path)[len] = '\0';

	return ret;
}
