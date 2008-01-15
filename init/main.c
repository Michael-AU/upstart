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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <linux/kd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/error.h>
#include <nih/logging.h>

#include "process.h"
#include "job.h"
#include "event.h"
#include "conf.h"
#include "paths.h"


/* Prototypes for static functions */
static void crash_handler   (int signum);
static void term_handler    (void *data, NihSignal *signal);
static void cad_handler     (void *data, NihSignal *signal);
static void kbd_handler     (void *data, NihSignal *signal);
static void pwr_handler     (void *data, NihSignal *signal);
static void hup_handler     (void *data, NihSignal *signal);
static void stop_handler    (void *data, NihSignal *signal);


/**
 * argv0:
 *
 * Path to program executed, used for re-executing the init binary from the
 * same location we were executed from.
 **/
static const char *argv0 = NULL;

/**
 * restart:
 *
 * This is set to TRUE if we're being re-exec'd by an existing init
 * process.
 **/
static int restart = FALSE;

/**
 * rescue:
 *
 * This is set to TRUE if we're being re-exec'd by an existing init process
 * that has crashed.
 **/
static int rescue = FALSE;


/**
 * options:
 *
 * Command-line options we accept.
 **/
static NihOption options[] = {
	{ 0, "restart", NULL, NULL, NULL, &restart, NULL },
	{ 0, "rescue", NULL, NULL, NULL, &rescue, NULL },

	/* Ignore invalid options */
	{ '-', "--", NULL, NULL, NULL, NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    ret;

	argv0 = argv[0];
	nih_main_init (argv0);

	nih_option_set_synopsis (_("Process management daemon."));
	nih_option_set_help (
		_("This daemon is normally executed by the kernel and given "
		  "process id 1 to denote its special status.  When executed "
		  "by a user process, it will actually run /sbin/telinit."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Check we're root */
	if (getuid ()) {
		nih_fatal (_("Need to be root"));
		exit (1);
	}

	/* Check we're process #1 */
	if (getpid () > 1) {
		execv (TELINIT, argv);
		/* Ignore failure, probably just that telinit doesn't exist */

		nih_fatal (_("Not being executed as init"));
		exit (1);
	}

	/* Clear our arguments from the command-line, so that we show up in
	 * ps or top output as /sbin/init, with no extra flags.
	 *
	 * This is a very Linux-specific trick; by deleting the NULL
	 * terminator at the end of the last argument, we fool the kernel
	 * into believing we used a setproctitle()-a-like to extend the
	 * argument space into the environment space, and thus make it use
	 * strlen() instead of its own assumed length.  In fact, we've done
	 * the exact opposite, and shrunk the command line length to just that
	 * of whatever is in argv[0].
	 *
	 * If we don't do this, and just write \0 over the rest of argv, for
	 * example; the command-line length still includes those \0s, and ps
	 * will show whitespace in their place.
	 */
	if (argc > 1) {
		char *arg_end;

		arg_end = argv[argc-1] + strlen (argv[argc-1]);
		*arg_end = ' ';
	}


	/* Become the leader of a new session and process group, shedding
	 * any controlling tty (which we shouldn't have had anyway - but
	 * you never know what initramfs did).
	 */
	setsid ();

	/* Set the standard file descriptors to the ordinary console device,
	 * resetting it to sane defaults unless we're inheriting from another
	 * init process which we know left it in a sane state.
	 */
	if (process_setup_console (CONSOLE_OUTPUT, ! (restart || rescue)) < 0)
		nih_free (nih_error_get ());

	/* Set the PATH environment variable */
	setenv ("PATH", PATH, TRUE);

	/* Switch to the root directory in case we were started from some
	 * strange place, or worse, some directory in the initramfs that's
	 * going to go away soon.
	 */
	chdir ("/");


	/* Reset the signal state and install the signal handler for those
	 * signals we actually want to catch; this also sets those that
	 * can be sent to us, because we're special
	 */
	if (! (restart || rescue))
		nih_signal_reset ();

 	/* Catch fatal errors immediately rather than waiting for a new
	 * iteration through the main loop.
	 */
	nih_signal_set_handler (SIGSEGV, crash_handler);
	nih_signal_set_handler (SIGABRT, crash_handler);

	/* Don't ignore SIGCHLD or SIGALRM, but don't respond to them
	 * directly; it's enough that they interrupt the main loop and
	 * get dealt with during it.
	 */
	nih_signal_set_handler (SIGCHLD, nih_signal_handler);
	nih_signal_set_handler (SIGALRM, nih_signal_handler);

	/* Allow SIGTSTP and SIGCONT to pause and unpause event processing */
	nih_signal_set_handler (SIGTSTP, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTSTP, stop_handler, NULL));

	nih_signal_set_handler (SIGCONT, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGCONT, stop_handler, NULL));

	/* Ask the kernel to send us SIGINT when control-alt-delete is
	 * pressed; generate an event with the same name.
	 */
	reboot (RB_DISABLE_CAD);
	nih_signal_set_handler (SIGINT, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGINT, cad_handler, NULL));

	/* Ask the kernel to send us SIGWINCH when alt-uparrow is pressed;
	 * generate a kbdrequest event.
	 */
	if (ioctl (0, KDSIGACCEPT, SIGWINCH) == 0) {
		nih_signal_set_handler (SIGWINCH, nih_signal_handler);
		NIH_MUST (nih_signal_add_handler (NULL, SIGWINCH,
						  kbd_handler, NULL));
	}

	/* powstatd sends us SIGPWR when it changes /etc/powerstatus */
	nih_signal_set_handler (SIGPWR, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGPWR, pwr_handler, NULL));

	/* SIGHUP instructs us to re-load our configuration */
	nih_signal_set_handler (SIGHUP, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGHUP, hup_handler, NULL));

	/* SIGTERM instructs us to re-exec ourselves; this should be the
	 * last in the list to ensure that all other signals are handled
	 * before a SIGTERM.
	 */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, term_handler, NULL));


	/* Watch children for events */
	NIH_MUST (nih_child_add_watch (NULL, -1, NIH_CHILD_ALL,
				       job_child_handler, NULL));

	/* Process the event queue each time through the main loop */
	NIH_MUST (nih_main_loop_add_func (NULL, (NihMainLoopCb)event_poll,
					  NULL));


	/* Read configuration */
	NIH_MUST (conf_source_new (NULL, CONFDIR "/init.conf", CONF_FILE));
	NIH_MUST (conf_source_new (NULL, CONFDIR "/conf.d", CONF_DIR));
	NIH_MUST (conf_source_new (NULL, CONFDIR "/jobs.d", CONF_JOB_DIR));
#ifdef LEGACY_CONFDIR
	NIH_MUST (conf_source_new (NULL, LEGACY_CONFDIR, CONF_JOB_DIR));
#endif /* LEGACY_CONFDIR */

	conf_reload ();

	/* Now that the startup is complete, send all further logging output
	 * to syslog instead of to the console.
	 */
	openlog (program_name, LOG_CONS, LOG_DAEMON);
	nih_log_set_logger (nih_logger_syslog);


	/* Generate and run the startup event or read the state from the
	 * init daemon that exec'd us
	 */
	if (! (restart || rescue)) {
		event_new (NULL, STARTUP_EVENT, NULL, NULL);
	} else {
		sigset_t mask;

		/* We're ok to receive signals again */
		sigemptyset (&mask);
		sigprocmask (SIG_SETMASK, &mask, NULL);
	}

	/* Run through the loop at least once to deal with signals that were
	 * delivered to the previous process while the mask was set or to
	 * process the startup event we emitted.
	 */
	nih_main_loop_interrupt ();
	ret = nih_main_loop ();

	return ret;
}


/**
 * crash_handler:
 * @signum: signal number received.
 *
 * Handle receiving the SEGV or ABRT signal, usually caused by one of
 * our own mistakes.  We deal with it by dumping core in a child process
 * and just carrying on in the parent.
 *
 * This may or may not work, but the only alternative would be sigjmp()ing
 * to somewhere "safe" leaving inconsistent state everywhere (like dangling
 * lists pointers) or exec'ing another process (which we couldn't transfer
 * our state to anyway).  This just hopes that the kernel resumes on the
 * next instruction.
 **/
static void
crash_handler (int signum)
{
	NihError   *err;
	const char *loglevel = NULL;
	sigset_t    mask, oldmask;
	pid_t       pid;

	nih_assert (argv0 != NULL);

	pid = fork ();
	if (pid == 0) {
		struct sigaction act;
		struct rlimit    limit;
		sigset_t         mask;

		/* Mask out all signals */
		sigfillset (&mask);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Set the handler to the default so core is dumped */
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (signum, &act, NULL);

		/* Don't limit the core dump size */
		limit.rlim_cur = RLIM_INFINITY;
		limit.rlim_max = RLIM_INFINITY;
		setrlimit (RLIMIT_CORE, &limit);

		/* Dump in the root directory */
		chdir ("/");

		/* Raise the signal again */
		raise (signum);

		/* Unmask so that we receive it */
		sigdelset (&mask, signum);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Wait for death */
		pause ();
		exit (0);
	} else if (pid > 0) {
		/* Wait for the core to be generated */
		waitpid (pid, NULL, 0);

		nih_fatal (_("Caught %s, core dumped"),
			   (signum == SIGSEGV
			    ? "segmentation fault" : "abort"));
	} else {
		nih_fatal (_("Caught %s, unable to dump core"),
			   (signum == SIGSEGV
			    ? "segmentation fault" : "abort"));
	}

	/* There's no point carrying on from here; our state is almost
	 * likely in tatters, so we'd just end up core-dumping again and
	 * writing over the one that contains the real bug.  We can't
	 * even re-exec properly, since we'd probably core dump while
	 * trying to transfer the state.
	 *
	 * So we just do the only thing we can, block out all signals
	 * and try and start again from scratch.
	 */
	sigfillset (&mask);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);

	/* Argument list */
	if (nih_log_priority <= NIH_LOG_DEBUG) {
		loglevel = "--debug";
	} else if (nih_log_priority <= NIH_LOG_INFO) {
		loglevel = "--verbose";
	} else if (nih_log_priority >= NIH_LOG_ERROR) {
		loglevel = "--error";
	} else {
		loglevel = NULL;
	}
	execl (argv0, argv0, "--rescue", loglevel, NULL);
	nih_error_raise_system ();

	err = nih_error_get ();
	nih_fatal (_("Failed to re-execute %s: %s"),
		   "/sbin/init", err->message);
	nih_free (err);

	sigprocmask (SIG_SETMASK, &oldmask, NULL);

	/* Oh Bugger */
	exit (1);
}

/**
 * term_handler:
 * @data: unused,
 * @signal: signal caught.
 *
 * This is called when we receive the TERM signal, which instructs us
 * to reexec ourselves.
 **/
static void
term_handler (void      *data,
	      NihSignal *signal)
{
	NihError   *err;
	const char *loglevel;
	sigset_t    mask, oldmask;

	nih_assert (argv0 != NULL);
	nih_assert (signal != NULL);

	nih_warn (_("Re-executing %s"), argv0);

	/* Block signals while we work.  We're the last signal handler
	 * installed so this should mean that they're all handled now.
	 *
	 * The child must make sure that it unblocks these again when
	 * it's ready.
	 */
	sigfillset (&mask);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);

	/* Argument list */
	if (nih_log_priority <= NIH_LOG_DEBUG) {
		loglevel = "--debug";
	} else if (nih_log_priority <= NIH_LOG_INFO) {
		loglevel = "--verbose";
	} else if (nih_log_priority >= NIH_LOG_ERROR) {
		loglevel = "--error";
	} else {
		loglevel = NULL;
	}
	execl (argv0, argv0, "--restart", loglevel, NULL);
	nih_error_raise_system ();

	err = nih_error_get ();
	nih_error (_("Failed to re-execute %s: %s"), argv0, err->message);
	nih_free (err);

	sigprocmask (SIG_SETMASK, &oldmask, NULL);
}


/**
 * cad_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGINT signal, sent to us when somebody
 * presses Ctrl-Alt-Delete on the console.  We just generate a
 * ctrlaltdel event.
 **/
static void
cad_handler (void      *data,
	     NihSignal *signal)
{
	event_new (NULL, CTRLALTDEL_EVENT, NULL, NULL);
}

/**
 * kbd_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGWINCH signal, sent to us when somebody
 * presses Alt-UpArrow on the console.  We just generate a
 * kbdrequest event.
 **/
static void
kbd_handler (void      *data,
	     NihSignal *signal)
{
	event_new (NULL, KBDREQUEST_EVENT, NULL, NULL);
}

/**
 * pwr_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGPWR signal, sent to us when powstatd
 * changes the /etc/powerstatus file.  We just generate a
 * power-status-changed event and jobs read the file.
 **/
static void
pwr_handler (void      *data,
	     NihSignal *signal)
{
	event_new (NULL, PWRSTATUS_EVENT, NULL, NULL);
}

/**
 * hup_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGHUP signal, which we use to instruct us to
 * reload our configuration.
 **/
static void
hup_handler (void      *data,
	     NihSignal *signal)
{
	nih_info (_("Reloading configuration"));
	conf_reload ();
}

/**
 * stop_handler:
 * @data: unused,
 * @signal: signal caught.
 *
 * This is called when we receive the STOP, TSTP or CONT signals; we
 * adjust the paused variable appropriately so that the event queue and
 * job stalled detection is not run.
 **/
static void
stop_handler (void      *data,
	      NihSignal *signal)
{
	nih_assert (signal != NULL);

	if (signal->signum == SIGCONT) {
		nih_info (_("Event queue resumed"));
		paused = FALSE;
	} else {
		nih_info (_("Event queue paused"));
		paused = TRUE;
	}
}
