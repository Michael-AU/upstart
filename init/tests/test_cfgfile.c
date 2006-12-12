/* upstart
 *
 * test_cfgfile.c - test suite for init/cfgfile.c
 *
 * Copyright © 2006 Canonical Ltd.
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/main.h>

#include "cfgfile.h"


/* Macro to aid us in testing the output which includes the program name
 * and filename.
 */
#define TEST_ERROR_EQ(_text) \
	do { \
		char text[512]; \
		sprintf (text, "%s:%s:%s", program_name, filename, (_text)); \
		TEST_FILE_EQ (output, text); \
	} while (0);


static int was_called = 0;

static int
destructor_called (void *ptr)
{
	was_called++;

	return 0;
}

static void
my_timer (void *data, NihTimer *timer)
{
	return;
}

void
test_read_job (void)
{
	Job  *job;
	FILE *jf, *output;
	char  dirname[PATH_MAX], filename[PATH_MAX];
	int   i;

	TEST_FUNCTION ("cfg_read_job");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (dirname);
	sprintf (filename, "%s/foo", dirname);
	mkdir (dirname, 0700);


	/* Check that a simple job file can be parsed, with all of the
	 * information given filled into the job structure.
	 */
	TEST_FEATURE ("with simple job file");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);
	TEST_LIST_EMPTY (&job->depends);

	TEST_EQ_STR (job->command, "/sbin/daemon -d");
	TEST_ALLOC_PARENT (job->command, job);
	TEST_EQ_STR (job->start_script, "rm /var/lock/daemon\n");
	TEST_ALLOC_PARENT (job->start_script, job);


	/* Check that we can give a new file for an existing job; this
	 * frees the existing structure, while copying over critical
	 * information from it to a new structure.
	 */
	TEST_FEATURE ("with re-reading existing job file");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon --daemon\n");
	fclose (jf);

	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;

	job->kill_timer = nih_timer_add_timeout (job, 1000, my_timer, job);
	job->pid_timer = nih_timer_add_timeout (job, 500, my_timer, job);

	was_called = 0;
	nih_alloc_set_destructor (job, destructor_called);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (was_called);

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);
	TEST_LIST_EMPTY (&job->depends);

	TEST_EQ_STR (job->command, "/sbin/daemon --daemon");
	TEST_ALLOC_PARENT (job->command, job);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_EQ (job->pid, 1000);

	TEST_ALLOC_PARENT (job->kill_timer, job);
	TEST_LE (job->kill_timer->due, time (NULL) + 1000);
	TEST_EQ_P (job->kill_timer->callback, my_timer);
	TEST_EQ_P (job->kill_timer->data, job);

	TEST_ALLOC_PARENT (job->pid_timer, job);
	TEST_LE (job->pid_timer->due, time (NULL) + 500);
	TEST_EQ_P (job->pid_timer->callback, my_timer);
	TEST_EQ_P (job->pid_timer->data, job);

	nih_list_free (&job->entry);


	/* Check a pretty complete job file, with all the major toggles.
	 * Make sure the job structure is filled in properly.
	 */
	TEST_FEATURE ("with complete job file");
	jf = fopen (filename, "w");
	fprintf (jf, "# this is a comment\n");
	fprintf (jf, "\n");
	fprintf (jf, "description \"an example daemon\"\n");
	fprintf (jf, "author \"joe bloggs\"\n");
	fprintf (jf, "version \"1.0\"\n");
	fprintf (jf, "\n");
	fprintf (jf, "exec /sbin/daemon -d \"arg here\"\n");
	fprintf (jf, "respawn  # restart the job when it fails\n");
	fprintf (jf, "console owner\n");
	fprintf (jf, "\n");
	fprintf (jf, "start on startup\n");
	fprintf (jf, "stop on shutdown\n");
	fprintf (jf, "\n");
	fprintf (jf, "on explosion\n");
	fprintf (jf, "\n");
	fprintf (jf, "depends frodo bilbo\n");
	fprintf (jf, "depends galadriel\n");
	fprintf (jf, "\n");
	fprintf (jf, "env PATH=\"/usr/games:/usr/bin\"\n");
	fprintf (jf, "env LANG=C\n");
	fprintf (jf, "\n");
	fprintf (jf, "umask 0155\n");
	fprintf (jf, "nice -20\n");
	fprintf (jf, "limit core 0 0\n");
	fprintf (jf, "limit cpu 50 100\n");
	fprintf (jf, "respawn limit 5 120\n");
	fprintf (jf, "\n");
	fprintf (jf, "chroot /jail/daemon\n");
	fprintf (jf, "chdir /var/lib\n");
	fprintf (jf, "\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    [ -d /var/run/daemon ] || mkdir /var/run/daemon\n");
	fprintf (jf, "  [ -d /var/lock/daemon ] || mkdir /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "    rm -rf /var/run/daemon /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "kill timeout 30\n");
	fprintf (jf, "normalexit 0\n");
	fprintf (jf, "normalexit 99 100\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ_STR (job->description, "an example daemon");
	TEST_ALLOC_PARENT (job->description, job);
	TEST_EQ_STR (job->author, "joe bloggs");
	TEST_ALLOC_PARENT (job->author, job);
	TEST_EQ_STR (job->version, "1.0");
	TEST_ALLOC_PARENT (job->version, job);

	TEST_EQ_STR (job->command, "/sbin/daemon -d \"arg here\"");
	TEST_ALLOC_PARENT (job->command, job);

	TEST_EQ_STR (job->start_script,
		     ("  [ -d /var/run/daemon ] || mkdir /var/run/daemon\n"
		      "[ -d /var/lock/daemon ] || mkdir /var/lock/daemon\n"));
	TEST_ALLOC_PARENT (job->start_script, job);
	TEST_EQ_STR (job->stop_script,
		     ("rm -rf /var/run/daemon /var/lock/daemon\n"));
	TEST_ALLOC_PARENT (job->stop_script, job);

	TEST_EQ_STR (job->chroot, "/jail/daemon");
	TEST_ALLOC_PARENT (job->chroot, job);
	TEST_EQ_STR (job->chdir, "/var/lib");
	TEST_ALLOC_PARENT (job->chdir, job);

	TEST_TRUE (job->respawn);
	TEST_EQ (job->console, CONSOLE_OWNER);
	TEST_EQ (job->umask, 0155);
	TEST_EQ (job->nice, -20);
	TEST_EQ (job->kill_timeout, 30);

	/* Check we got all of the start events we expected */
	i = 0;
	TEST_LIST_NOT_EMPTY (&job->start_events);
	NIH_LIST_FOREACH (&job->start_events, iter) {
		Event *event = (Event *)iter;

		TEST_ALLOC_PARENT (event, job);

		if (! strcmp (event->name, "startup")) {
			i |= 1;
		} else if (! strcmp (event->name, "explosion")) {
			i |= 2;
		} else {
			TEST_FAILED ("wrong start event, got unexpected '%s'",
				     event->name);
		}
	}
	if (i != 3)
		TEST_FAILED ("missing at least one start event");

	/* Check we got all of the start events we expected */
	i = 0;
	TEST_LIST_NOT_EMPTY (&job->stop_events);
	NIH_LIST_FOREACH (&job->stop_events, iter) {
		Event *event = (Event *)iter;

		TEST_ALLOC_PARENT (event, job);

		if (! strcmp (event->name, "shutdown")) {
			i |= 1;
		} else {
			TEST_FAILED ("wrong stop event, got unexpected '%s'",
				     event->name);
		}
	}
	if (i != 1)
		TEST_FAILED ("missing at least one stop event");

	/* Check we got all of the depends we expected */
	i = 0;
	TEST_LIST_NOT_EMPTY (&job->depends);
	NIH_LIST_FOREACH (&job->depends, iter) {
		JobName *dep = (JobName *)iter;

		TEST_ALLOC_PARENT (dep, job);

		if (! strcmp (dep->name, "frodo")) {
			i |= 1;
		} else if (! strcmp (dep->name, "bilbo")) {
			i |= 2;
		} else if (! strcmp (dep->name, "galadriel")) {
			i |= 4;
		} else {
			TEST_FAILED ("wrong dependency, got unexpected '%s'",
				     dep->name);
		}
	}
	if (i != 7)
		TEST_FAILED ("missing at least one dependency");

	TEST_NE_P (job->env, NULL);
	TEST_ALLOC_PARENT (job->env, job);
	TEST_EQ_STR (job->env[0], "PATH=/usr/games:/usr/bin");
	TEST_ALLOC_PARENT (job->env[0], job->env);
	TEST_EQ_STR (job->env[1], "LANG=C");
	TEST_ALLOC_PARENT (job->env[1], job->env);
	TEST_EQ_P (job->env[2], NULL);

	TEST_EQ (job->normalexit_len, 3);
	TEST_NE_P (job->normalexit, NULL);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * 3);
	TEST_ALLOC_PARENT (job->normalexit, job);
	TEST_EQ (job->normalexit[0], 0);
	TEST_EQ (job->normalexit[1], 99);
	TEST_EQ (job->normalexit[2], 100);

	TEST_NE_P (job->limits[RLIMIT_CORE], NULL);
	TEST_ALLOC_SIZE (job->limits[RLIMIT_CORE], sizeof (struct rlimit));
	TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 0);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 0);

	TEST_NE_P (job->limits[RLIMIT_CPU], NULL);
	TEST_ALLOC_SIZE (job->limits[RLIMIT_CPU], sizeof (struct rlimit));
	TEST_ALLOC_PARENT (job->limits[RLIMIT_CPU], job);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_cur, 50);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_max, 100);

	TEST_EQ (job->respawn_limit, 5);
	TEST_EQ (job->respawn_interval, 120);

	nih_list_free (&job->entry);


	/* Check that both exec and respawn can be given together,
	 * and that respawn doesn't clear that.
	 */
	TEST_FEATURE ("with exec and respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo arg\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->respawn);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that respawn can be given arguments, which acts like
	 * passing that and exec with those arguments.
	 */
	TEST_FEATURE ("with arguments to respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "respawn /usr/bin/foo arg\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->respawn);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that both exec and daemon can be given together,
	 * and that daemon doesn't clear that.
	 */
	TEST_FEATURE ("with exec and daemon");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo arg\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->daemon);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that daemon can be given arguments, which acts like
	 * passing that and exec with those arguments.
	 */
	TEST_FEATURE ("with arguments to daemon");
	jf = fopen (filename, "w");
	fprintf (jf, "daemon /usr/bin/foo arg\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->daemon);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that the instance stanza marks the job as such. */
	TEST_FEATURE ("with instance job");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->spawns_instance);

	nih_list_free (&job->entry);


	/* Check an extreme case of bad formatting to make sure the config
	 * file parser does the right thing and makes it sane.
	 */
	TEST_FEATURE ("with interesting formatting");
	jf = fopen (filename, "w");
	fprintf (jf, "    description   \"foo\n");
	fprintf (jf, "   bar\"\n");
	fprintf (jf, "\n");
	fprintf (jf, "author \"  something  with  spaces  \"\n");
	fprintf (jf, "\n");
	fprintf (jf, "version 'foo\\'bar'\n");
	fprintf (jf, "\n");
	fprintf (jf, "exec /usr/bin/foo \\\n");
	fprintf (jf, "  first second \"third \n");
	fprintf (jf, "  argument\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->description, "foo bar");
	TEST_EQ_STR (job->author, "  something  with  spaces  ");
	TEST_EQ_STR (job->version, "foo'bar");
	TEST_EQ_STR (job->command,
		     "/usr/bin/foo first second \"third argument\"");

	nih_list_free (&job->entry);


	/* Check that the parsing of 'end script' is strict enough to allow
	 * all sorts of other things in between.
	 */
	TEST_FEATURE ("with things that aren't script ends");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "endscript\n");
	fprintf (jf, "end foo\n");
	fprintf (jf, "end scripting\n");
	fprintf (jf, "end script # wibble\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "# ok\n");
	fprintf (jf, "  end script");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->start_script,
		     "endscript\nend foo\nend scripting\n");
	TEST_EQ_STR (job->stop_script, "# ok\n");

	nih_list_free (&job->entry);


	/* Check that giving a stanza more than once is permitted, with the
	 * last one taking precedence.
	 */
	TEST_FEATURE ("with multiple stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "respawn\n");
	fprintf (jf, "\n");
	fprintf (jf, "description oops\n");
	fprintf (jf, "description yay\n");
	fprintf (jf, "author oops\n");
	fprintf (jf, "author yay\n");
	fprintf (jf, "version oops\n");
	fprintf (jf, "version yay\n");
	fprintf (jf, "\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "exec oops\n");
	fprintf (jf, "exec yay\n");
	fprintf (jf, "\n");
	fprintf (jf, "chroot oops\n");
	fprintf (jf, "chroot yay\n");
	fprintf (jf, "chdir oops\n");
	fprintf (jf, "chdir yay\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->description, "yay");
	TEST_EQ_STR (job->author, "yay");
	TEST_EQ_STR (job->version, "yay");
	TEST_EQ_STR (job->start_script, "yay\n");
	TEST_EQ_STR (job->stop_script, "yay\n");
	TEST_EQ_STR (job->respawn_script, "yay\n");
	TEST_EQ_STR (job->command, "yay");
	TEST_EQ_STR (job->chroot, "yay");
	TEST_EQ_STR (job->chdir, "yay");

	nih_list_free (&job->entry);


	/* Check that giving a script stanza more than once is permitted,
	 * with the last one taking precedence.  (Tested separately because
	 * we check exec above)
	 */
	TEST_FEATURE ("with multiple script stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->script, "yay\n");

	nih_list_free (&job->entry);


	/* Check that we can give both exec and respawn with arguments, and
	 * the latter takes precedence.
	 */
	TEST_FEATURE ("with exec and respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "exec oops\n");
	fprintf (jf, "respawn yay\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->command, "yay");

	nih_list_free (&job->entry);


	/* Check that we can give both exec and daemon with arguments, and
	 * the latter takes precedence.
	 */
	TEST_FEATURE ("with exec and daemon");
	jf = fopen (filename, "w");
	fprintf (jf, "exec oops\n");
	fprintf (jf, "daemon yay\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->command, "yay");

	nih_list_free (&job->entry);


	/* Check that all sorts of error conditions are caught, and result
	 * in warnings being issued and the stanza being ignored.
	 */
	TEST_FEATURE ("with various errors");
	jf = fopen (filename, "w");
	fprintf (jf, "description\n");
	fprintf (jf, "description foo bar\n");
	fprintf (jf, "author\n");
	fprintf (jf, "author foo bar\n");
	fprintf (jf, "version\n");
	fprintf (jf, "version foo bar\n");
	fprintf (jf, "depends\n");
	fprintf (jf, "on\n");
	fprintf (jf, "on foo bar\n");
	fprintf (jf, "start\n");
	fprintf (jf, "start on\n");
	fprintf (jf, "start on foo bar\n");
	fprintf (jf, "start wibble\n");
	fprintf (jf, "stop\n");
	fprintf (jf, "stop on\n");
	fprintf (jf, "stop on foo bar\n");
	fprintf (jf, "stop wibble\n");
	fprintf (jf, "exec\n");
	fprintf (jf, "instance foo\n");
	fprintf (jf, "pid\n");
	fprintf (jf, "pid file\n");
	fprintf (jf, "pid file foo baz\n");
	fprintf (jf, "pid binary\n");
	fprintf (jf, "pid binary foo baz\n");
	fprintf (jf, "pid timeout\n");
	fprintf (jf, "pid timeout abc\n");
	fprintf (jf, "pid timeout -40\n");
	fprintf (jf, "pid timeout 10 20\n");
	fprintf (jf, "pid wibble\n");
	fprintf (jf, "kill\n");
	fprintf (jf, "kill timeout\n");
	fprintf (jf, "kill timeout abc\n");
	fprintf (jf, "kill timeout -40\n");
	fprintf (jf, "kill timeout 10 20\n");
	fprintf (jf, "kill wibble\n");
	fprintf (jf, "normalexit\n");
	fprintf (jf, "normalexit abc\n");
	fprintf (jf, "console\n");
	fprintf (jf, "console wibble\n");
	fprintf (jf, "console output foo\n");
	fprintf (jf, "env\n");
	fprintf (jf, "env foo=bar baz\n");
	fprintf (jf, "umask\n");
	fprintf (jf, "umask abc\n");
	fprintf (jf, "umask 12345\n");
	fprintf (jf, "umask 099\n");
	fprintf (jf, "umask 0122 foo\n");
	fprintf (jf, "nice\n");
	fprintf (jf, "nice abc\n");
	fprintf (jf, "nice -30\n");
	fprintf (jf, "nice 25\n");
	fprintf (jf, "nice 0 foo\n");
	fprintf (jf, "limit\n");
	fprintf (jf, "limit wibble\n");
	fprintf (jf, "limit core\n");
	fprintf (jf, "limit core 0\n");
	fprintf (jf, "limit core abc 0\n");
	fprintf (jf, "limit core 0 abc\n");
	fprintf (jf, "limit core 0 0 0\n");
	fprintf (jf, "respawn limit\n");
	fprintf (jf, "respawn limit 0\n");
	fprintf (jf, "respawn limit abc 0\n");
	fprintf (jf, "respawn limit 0 abc\n");
	fprintf (jf, "respawn limit 0 0 0\n");
	fprintf (jf, "chroot\n");
	fprintf (jf, "chroot / foo\n");
	fprintf (jf, "chdir\n");
	fprintf (jf, "chdir / foo\n");
	fprintf (jf, "wibble\n");
	fprintf (jf, "script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "start script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "stop script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "respawn script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_ERROR_EQ ("1: expected job description\n");
	TEST_ERROR_EQ ("2: ignored additional arguments\n");
	TEST_ERROR_EQ ("3: expected author name\n");
	TEST_ERROR_EQ ("4: ignored additional arguments\n");
	TEST_ERROR_EQ ("5: expected version string\n");
	TEST_ERROR_EQ ("6: ignored additional arguments\n");
	TEST_ERROR_EQ ("7: expected job name\n");
	TEST_ERROR_EQ ("8: expected event name\n");
	TEST_ERROR_EQ ("9: ignored additional arguments\n");
	TEST_ERROR_EQ ("10: expected 'on' or 'script'\n");
	TEST_ERROR_EQ ("11: expected event name\n");
	TEST_ERROR_EQ ("12: ignored additional arguments\n");
	TEST_ERROR_EQ ("13: expected 'on' or 'script'\n");
	TEST_ERROR_EQ ("14: expected 'on' or 'script'\n");
	TEST_ERROR_EQ ("15: expected event name\n");
	TEST_ERROR_EQ ("16: ignored additional arguments\n");
	TEST_ERROR_EQ ("17: expected 'on' or 'script'\n");
	TEST_ERROR_EQ ("18: expected command\n");
	TEST_ERROR_EQ ("19: ignored additional arguments\n");
	TEST_ERROR_EQ ("20: expected 'file', 'binary' or 'timeout'\n");
	TEST_ERROR_EQ ("21: expected pid filename\n");
	TEST_ERROR_EQ ("22: ignored additional arguments\n");
	TEST_ERROR_EQ ("23: expected binary filename\n");
	TEST_ERROR_EQ ("24: ignored additional arguments\n");
	TEST_ERROR_EQ ("25: expected timeout\n");
	TEST_ERROR_EQ ("26: illegal value\n");
	TEST_ERROR_EQ ("27: illegal value\n");
	TEST_ERROR_EQ ("28: ignored additional arguments\n");
	TEST_ERROR_EQ ("29: expected 'file', 'binary' or 'timeout'\n");
	TEST_ERROR_EQ ("30: expected 'timeout'\n");
	TEST_ERROR_EQ ("31: expected timeout\n");
	TEST_ERROR_EQ ("32: illegal value\n");
	TEST_ERROR_EQ ("33: illegal value\n");
	TEST_ERROR_EQ ("34: ignored additional arguments\n");
	TEST_ERROR_EQ ("35: expected 'timeout'\n");
	TEST_ERROR_EQ ("36: expected exit status\n");
	TEST_ERROR_EQ ("37: illegal value\n");
	TEST_ERROR_EQ ("38: expected 'logged', 'output', 'owner' or 'none'\n");
	TEST_ERROR_EQ ("39: expected 'logged', 'output', 'owner' or 'none'\n");
	TEST_ERROR_EQ ("40: ignored additional arguments\n");
	TEST_ERROR_EQ ("41: expected variable setting\n");
	TEST_ERROR_EQ ("42: ignored additional arguments\n");
	TEST_ERROR_EQ ("43: expected file creation mask\n");
	TEST_ERROR_EQ ("44: illegal value\n");
	TEST_ERROR_EQ ("45: illegal value\n");
	TEST_ERROR_EQ ("46: illegal value\n");
	TEST_ERROR_EQ ("47: ignored additional arguments\n");
	TEST_ERROR_EQ ("48: expected nice level\n");
	TEST_ERROR_EQ ("49: illegal value\n");
	TEST_ERROR_EQ ("50: illegal value\n");
	TEST_ERROR_EQ ("51: illegal value\n");
	TEST_ERROR_EQ ("52: ignored additional arguments\n");
	TEST_ERROR_EQ ("53: expected limit name\n");
	TEST_ERROR_EQ ("54: unknown limit type\n");
	TEST_ERROR_EQ ("55: expected soft limit\n");
	TEST_ERROR_EQ ("56: expected hard limit\n");
	TEST_ERROR_EQ ("57: illegal value\n");
	TEST_ERROR_EQ ("58: illegal value\n");
	TEST_ERROR_EQ ("59: ignored additional arguments\n");
	TEST_ERROR_EQ ("60: expected limit\n");
	TEST_ERROR_EQ ("61: expected interval\n");
	TEST_ERROR_EQ ("62: illegal value\n");
	TEST_ERROR_EQ ("63: illegal value\n");
	TEST_ERROR_EQ ("64: ignored additional arguments\n");
	TEST_ERROR_EQ ("65: expected directory name\n");
	TEST_ERROR_EQ ("66: ignored additional arguments\n");
	TEST_ERROR_EQ ("67: expected directory name\n");
	TEST_ERROR_EQ ("68: ignored additional arguments\n");
	TEST_ERROR_EQ ("69: ignored unknown stanza\n");
	TEST_ERROR_EQ ("70: ignored additional arguments\n");
	TEST_ERROR_EQ ("72: ignored additional arguments\n");
	TEST_ERROR_EQ ("74: ignored additional arguments\n");
	TEST_ERROR_EQ ("76: ignored additional arguments\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that an exec line with an unterminated quote is caught,
	 * but still results in the command being set without it.
	 */
	TEST_FEATURE ("with unterminated quote");
	jf = fopen (filename, "w");
	fprintf (jf, "exec \"/sbin/foo bar");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_STR (job->command, "\"/sbin/foo bar");

	TEST_ERROR_EQ ("1: unterminated quoted string\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that a line with a trailing slash but no following line
	 * is caught, but still results in the command being set.
	 */
	TEST_FEATURE ("with trailing slash");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo bar \\");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_STR (job->command, "/sbin/foo bar");

	TEST_ERROR_EQ ("1: ignored trailing slash\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that an unfinished script stanza is caught, but still results
	 * in the script being set.
	 */
	TEST_FEATURE ("with incomplete script");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "    rm /var/run/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_STR (job->start_script,
		     "    rm /var/lock/daemon\n    rm /var/run/daemon\n");

	TEST_ERROR_EQ ("4: 'end script' expected\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that a job may not be missing both exec and script.
	 * Doing this causes no job to be returned.
	 */
	TEST_FEATURE ("with missing exec and script");
	jf = fopen (filename, "w");
	fprintf (jf, "description buggy");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (" 'exec' or 'script' must be specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a job may not supply both exec and script.
	 * Doing this causes no job to be returned.
	 */
	TEST_FEATURE ("with both exec and script");
	jf = fopen (filename, "w");
	fprintf (jf, "description buggy\n");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "script\n");
	fprintf (jf, "   /sbin/foo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (" only one of 'exec' and 'script' may be specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a job may not use options that normally affect respawn
	 * if it doesn't use respawn itself.  It gets warnings.
	 */
	TEST_FEATURE ("with respawn options and not respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "do something\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "pid file /var/run/foo.pid\n");
	fprintf (jf, "pid binary /lib/foo/foo.bin\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_ERROR_EQ (" 'respawn script' ignored unless 'respawn' specified\n");
	TEST_ERROR_EQ (" 'pid file' ignored unless 'respawn' specified\n");
	TEST_ERROR_EQ (" 'pid binary' ignored unless 'respawn' specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that a non-existant file is caught properly. */
	TEST_FEATURE ("with non-existant file");
	unlink (filename);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (" unable to read: No such file or directory\n");
	TEST_FILE_END (output);


	fclose (output);

	unlink (filename);
	rmdir (dirname);
}


int
main (int   argc,
      char *argv[])
{
	test_read_job ();

	return 0;
}
