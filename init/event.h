/* upstart
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
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

#ifndef INIT_EVENT_H
#define INIT_EVENT_H

#include <stdio.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/main.h>


/**
 * Event:
 * @entry: list header,
 * @name: string name of the event,
 * @args: NULL-terminated list of arguments,
 * @env: NULL-terminated list of environment variables.
 *
 * Events occur whenever something, somewhere changes state.  They are
 * placed in the event queue and can cause jobs to change their goal to
 * start or stop.
 *
 * Once processed, they are forgotten about.  The state is stored by the
 * event generator (the job state machine or external process) and upstart
 * makes no attempt to track it.
 **/
typedef struct event {
	NihList   entry;

	char     *name;
	char    **args;
	char    **env;
} Event;


/**
 * STARTUP_EVENT:
 *
 * Name of the event that we generate when init is first executed.
 **/
#define STARTUP_EVENT "startup"

/**
 * SHUTDOWN_EVENT:
 *
 * Name of the event that we generate to begin the shutdown process.
 **/
#define SHUTDOWN_EVENT "shutdown"

/**
 * STALLED_EVENT:
 *
 * Name of the event that we generate if the system stalls (all jobs are
 * stopped/waiting)
 **/
#define STALLED_EVENT "stalled"

/**
 * CTRLALTDEL_EVENT:
 *
 * Name of the event that we generate when the Control-Alt-Delete key
 * combination is pressed.
 **/
#define CTRLALTDEL_EVENT "ctrlaltdel"

/**
 * KBDREQUEST_EVENT:
 *
 * Name of the event that we generate when the Alt-UpArrow key combination
 * is pressed.
 **/
#define KBDREQUEST_EVENT "kbdrequest"


/**
 * JOB_START_EVENT:
 *
 * Name of the event we generate when a job begins to be started.
 **/
#define JOB_START_EVENT "start"

/**
 * JOB_STARTED_EVENT:
 *
 * Name of the event we generate once a job has been started and is now
 * running.
 **/
#define JOB_STARTED_EVENT "started"

/**
 * JOB_STOP_EVENT:
 *
 * Name of the event we generate when a job begins to be stopped.
 **/
#define JOB_STOP_EVENT "stop"

/**
 * JOB_STOPPED_EVENT:
 *
 * Name of the event we generate once a job has been stopped and is now
 * waiting.
 **/
#define JOB_STOPPED_EVENT "stopped"


NIH_BEGIN_EXTERN

int paused;


Event *event_new         (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

int    event_match       (Event *event1, Event *event2);

Event *event_queue       (const char *name);
void   event_queue_run   (void);

Event *event_read_state  (Event *event, char *buf);
void   event_write_state (FILE *state);

NIH_END_EXTERN

#endif /* INIT_EVENT_H */
