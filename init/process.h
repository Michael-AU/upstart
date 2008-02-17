/* upstart
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

#ifndef INIT_PROCESS_H
#define INIT_PROCESS_H

#include <sys/types.h>

#include <nih/macros.h>
#include <nih/error.h>

#include "job.h"


/**
 * PROCESS_ENVIRONMENT:
 *
 * Environment variables to always copy from our own environment.
 **/
#define PROCESS_ENVIRONMENT \
	"PATH",		    \
	"TERM"


/**
 * ProcessErrorType:
 *
 * These constants represent the different steps of process spawning that
 * can produce an error.
 **/
typedef enum process_error_type {
	PROCESS_ERROR_CONSOLE,
	PROCESS_ERROR_RLIMIT,
	PROCESS_ERROR_ENVIRON,
	PROCESS_ERROR_PRIORITY,
	PROCESS_ERROR_CHROOT,
	PROCESS_ERROR_CHDIR,
	PROCESS_ERROR_PTRACE,
	PROCESS_ERROR_EXEC
} ProcessErrorType;

/**
 * ProcessError:
 * @error: ordinary NihError,
 * @type: specific error,
 * @arg: relevant argument to @type,
 * @errnum: system error number.
 *
 * This structure builds on NihError to include additional fields useful
 * for an error generated by spawning a process.  @error includes the single
 * error number and human-readable message which are sufficient for many
 * purposes.
 *
 * @type indicates which step of the spawning process failed, @arg is any
 * information relevant to @type (such as the resource limit that could not
 * be set) and @errnum is the actual system error number.
 *
 * If you receive a PROCESS_ERROR, the returned NihError structure is actually
 * this structure and can be cast to get the additional fields.
 **/
typedef struct process_error {
	NihError         error;
	ProcessErrorType type;
	int              arg;
	int              errnum;
} ProcessError;


NIH_BEGIN_EXTERN

pid_t  process_spawn           (Job *job, char * const argv[], int trace)
	__attribute__ ((warn_unused_result));

int    process_setup_console   (ConsoleType type, int reset)
	__attribute__ ((warn_unused_result));

int    process_kill            (Job *job, pid_t pid, int force)
	__attribute__ ((warn_unused_result));

char **process_environment     (Job *job)
	__attribute__ ((warn_unused_result, malloc));

char **process_environment_add (char ***env, const void *parent,
				size_t *len, const char *str)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_PROCESS_H */
