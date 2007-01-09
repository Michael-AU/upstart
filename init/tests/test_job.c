/* upstart
 *
 * test_job.c - test suite for init/job.c
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>

#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/main.h>

#include "event.h"
#include "job.h"
#include "control.h"


void
test_new (void)
{
	Job *job;
	int  i;

	/* Check that we can create a new job structure; the structure
	 * should be allocated with nih_alloc, placed in the jobs list
	 * and have sensible defaults.
	 */
	TEST_FUNCTION ("job_new");
	job = job_new (NULL, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->entry);
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);

	TEST_EQ_STR (job->name, "test");
	TEST_ALLOC_PARENT (job->name, job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process_state, PROCESS_NONE);

	TEST_EQ (job->kill_timeout, JOB_DEFAULT_KILL_TIMEOUT);
	TEST_EQ (job->pid_timeout, JOB_DEFAULT_PID_TIMEOUT);
	TEST_EQ (job->respawn_limit, JOB_DEFAULT_RESPAWN_LIMIT);
	TEST_EQ (job->respawn_interval, JOB_DEFAULT_RESPAWN_INTERVAL);

	TEST_EQ (job->console, CONSOLE_LOGGED);
	TEST_EQ (job->umask, JOB_DEFAULT_UMASK);

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		TEST_EQ_P (job->limits[i], NULL);

	nih_list_free (&job->entry);
}

void
test_find_by_name (void)
{
	Job *job1, *job2, *job3, *ptr;

	TEST_FUNCTION ("job_find_by_name");
	job1 = job_new (NULL, "foo");
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");

	/* Check that we can find a job that exists by its name. */
	TEST_FEATURE ("with name we expect to find");
	ptr = job_find_by_name ("bar");

	TEST_EQ_P (ptr, job2);


	/* Check that we get NULL if the job doesn't exist. */
	TEST_FEATURE ("with name we do not expect to find");
	ptr = job_find_by_name ("frodo");

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if the job list is empty, and nothing
	 * bad happens.
	 */
	TEST_FEATURE ("with empty job list");
	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_name ("bar");

	TEST_EQ_P (ptr, NULL);
}

void
test_find_by_pid (void)
{
	Job *job1, *job2, *job3, *ptr;

	TEST_FUNCTION ("job_find_by_pid");
	job1 = job_new (NULL, "foo");
	job1->pid = 10;
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");
	job3->pid = 20;

	/* Check that we can find a job that exists by the pid of its
	 * primary process.
	 */
	TEST_FEATURE ("with pid we expect to find");
	ptr = job_find_by_pid (20);

	TEST_EQ_P (ptr, job3);


	/* Check that we get NULL if no job has a process with that pid. */
	TEST_FEATURE ("with pid we do not expect to find");
	ptr = job_find_by_pid (30);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are jobs in the list, but none
	 * have pids.
	 */
	TEST_FEATURE ("with no pids in job list");
	nih_list_free (&job3->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_pid (20);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no jobs in the list. */
	TEST_FEATURE ("with empty job list");
	nih_list_free (&job2->entry);
	ptr = job_find_by_pid (20);

	TEST_EQ_P (ptr, NULL);
}


void
test_change_state (void)
{
	FILE        *output;
	Job         *job;
	Event       *event;
	NihList     *list;
	struct stat  statbuf;
	char         dirname[PATH_MAX], filename[PATH_MAX];
	int          i;

	TEST_FUNCTION ("job_change_state");
	program_name = "test";
	TEST_FILENAME (dirname);
	mkdir (dirname, 0700);

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...
	 */
	event_queue_run ();
	event = event_queue ("wibble");
	list = event->entry.prev;
	nih_list_free (&event->entry);

	job = job_new (NULL, "test");
	job->start_script = nih_sprintf (job, "touch %s/start", dirname);
	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);
	job->respawn_script = nih_sprintf (job, "touch %s/respawn", dirname);
	job->command = nih_sprintf (job, "touch %s/run", dirname);


	/* Check that a job can move from waiting to starting, and that if
	 * it has a start script, that is run and the job left in the starting
	 * state.  The test/start event should be emitted.
	 */
	TEST_FEATURE ("waiting to starting with script");
	job->goal = JOB_START;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;

	job_change_state (job, JOB_STARTING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/start");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/start", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job can move directly from waiting to running if it
	 * has no start script, emitting both the start and started events.
	 */
	TEST_FEATURE ("waiting to starting with no script");
	job->goal = JOB_START;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	nih_free (job->start_script);
	job->start_script = NULL;
	job_change_state (job, JOB_STARTING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/started");
	nih_list_free (&event->entry);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/start");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);
	job->start_script = nih_sprintf (job, "touch %s/start", dirname);


	/* Check that a job in the starting state moves into the running state,
	 * emitting the started event.
	 */
	TEST_FEATURE ("starting to running with command");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_RUNNING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/started");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the starting state moves into the running state,
	 * and if respawn is set, this causes both the started and job
	 * event to be emitted.
	 */
	TEST_FEATURE ("starting to running with respawn");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->respawn = TRUE;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_RUNNING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test");
	nih_list_free (&event->entry);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/started");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the starting state can move into the running
	 * state, and that a script instead of a command can be run.  Only
	 * the started event should be emitted.
	 */
	TEST_FEATURE ("starting to running with script");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_NONE;
	job->respawn = FALSE;
	job->script = job->command;
	job->command = NULL;
	job_change_state (job, JOB_RUNNING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/started");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the running state can move into the respawning
	 * state, resulting in the respawn script being run and the respawn
	 * event being emitted.
	 */
	TEST_FEATURE ("running to respawning with script");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_RESPAWNING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RESPAWNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/respawn");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/respawn", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the running state can move straight through
	 * the respawning state back into the running state if there's no
	 * script; emitting the respawn then started events as it goes.
	 */
	TEST_FEATURE ("running to respawning without script");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	nih_free (job->respawn_script);
	job->respawn_script = NULL;
	job_change_state (job, JOB_RESPAWNING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/started");
	nih_list_free (&event->entry);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/respawn");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the running state can move into the stopping
	 * state, running the script.  Both the job event and the stop event
	 * should be emitted.
	 */
	TEST_FEATURE ("running to stopping with script");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_STOPPING);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test");
	nih_list_free (&event->entry);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/stop");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the running state can move into the stopping
	 * state, running the script.  As it's marked respawn, only the
	 * stop event should be emitted.
	 */
	TEST_FEATURE ("running to stopping with script and respawn");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	job->respawn = TRUE;
	job_change_state (job, JOB_STOPPING);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/stop");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job in the running state can move directly into the
	 * waiting state, emitting the stop and stopped events as it goes,
	 * if there's no script.
	 */
	TEST_FEATURE ("running to stopping without script");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	nih_free (job->stop_script);
	job->stop_script = NULL;
	job_change_state (job, JOB_STOPPING);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process_state, PROCESS_NONE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/stopped");
	nih_list_free (&event->entry);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/stop");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);


	/* Check that a job in the stopping state can move into the waiting
	 * state, emitting the stopped event as it goes.
	 */
	TEST_FEATURE ("stopping to waiting");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_WAITING);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process_state, PROCESS_NONE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/stopped");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);


	/* Check that a job in the stopping state can move round into the
	 * starting state if the goal is JOB_START, emitting only the
	 * start event.
	 */
	TEST_FEATURE ("stopping to starting");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_STARTING);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "test/start");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/start", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);


	/* Check that a job that tries to enter the running state from the
	 * starting state too fast results in it being stopped.  An error
	 * message should be emitted.
	 */
	TEST_FEATURE ("starting to running too fast");
	job->respawn_count = 0;
	job->respawn_time = 0;
	job->respawn_limit = 10;
	job->respawn_interval = 100;

	output = tmpfile ();
	TEST_DIVERT_STDERR (output) {
		for (i = 0; i < 11; i++) {
			job->goal = JOB_START;
			job->state = JOB_STARTING;
			job->process_state = PROCESS_NONE;
			job_change_state (job, JOB_RUNNING);

			if (job->goal == JOB_START)
				waitpid (job->pid, NULL, 0);
		}
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);

	sprintf (filename, "%s/run", dirname);
	unlink (filename);

	TEST_FILE_EQ (output, "test: test respawning too fast, stopped\n");
	TEST_FILE_RESET (output);

	event_queue_run ();


	/* Check that a job entering the respawning state from the running
	 * state too fast results in the job being stopped.  An error
	 * message should be emitted.
	 */
	TEST_FEATURE ("running to respawning too fast");
	job->respawn_count = 0;
	job->respawn_time = 0;
	job->respawn_limit = 10;
	job->respawn_interval = 100;

	TEST_DIVERT_STDERR (output) {
		for (i = 0; i < 11; i++) {
			job->goal = JOB_START;
			job->state = JOB_RUNNING;
			job->process_state = PROCESS_NONE;
			job_change_state (job, JOB_RESPAWNING);
		}
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);

	sprintf (filename, "%s/run", dirname);
	unlink (filename);

	TEST_FILE_EQ (output, "test: test respawning too fast, stopped\n");

	event_queue_run ();


	fclose (output);
	rmdir (dirname);

	nih_list_free (&job->entry);
}

void
test_next_state (void)
{
	Job *job;

	TEST_FUNCTION ("job_next_state");
	job = job_new (NULL, "test");

	/* Check that the next state if we're stopping a waiting job is
	 * waiting.
	 */
	TEST_FEATURE ("with waiting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	/* Check that the next state if we're starting a waiting job is
	 * starting.
	 */
	TEST_FEATURE ("with waiting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a starting job is
	 * stopping.
	 */
	TEST_FEATURE ("with starting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a starting job is
	 * running.
	 */
	TEST_FEATURE ("with starting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a running job is
	 * stopping.
	 */
	TEST_FEATURE ("with running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a running job is
	 * respawning.
	 */
	TEST_FEATURE ("with running job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_RESPAWNING);


	/* Check that the next state if we're stopping a stopping job is
	 * waiting.
	 */
	TEST_FEATURE ("with stopping job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	/* Check that the next state if we're starting a stopping job is
	 * starting.
	 */
	TEST_FEATURE ("with stopping job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a respawning job is
	 * stopping.
	 */
	TEST_FEATURE ("with respawning job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RESPAWNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a respawning job is
	 * running.
	 */
	TEST_FEATURE ("with respawning job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RESPAWNING;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	nih_list_free (&job->entry);
}


void
test_run_command (void)
{
	Job         *job;
	FILE        *output;
	struct stat  statbuf;
	char         filename[PATH_MAX], buf[80];

	TEST_FUNCTION ("job_run_command");
	TEST_FILENAME (filename);

	/* Check that we can run a simple command, and have the process id
	 * and state filled in.  We should be able to wait for the pid to
	 * finish and see that it has been run as expected.
	 */
	TEST_FEATURE ("with simple command");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->command = nih_sprintf (job, "touch %s", filename);
	job_run_command (job, job->command);

	TEST_NE (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, NULL, 0);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);
	nih_list_free (&job->entry);


	/* Check that we can run a command that requires a shell to be
	 * intepreted correctly, a shell should automatically be used to
	 * make this work.  Check the contents of a file we'll create to
	 * check that a shell really was used.
	 */
	TEST_FEATURE ("with shell command");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->command = nih_sprintf (job, "echo $$ > %s", filename);
	job_run_command (job, job->command);

	TEST_NE (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, NULL, 0);
	TEST_EQ (stat (filename, &statbuf), 0);

	/* Filename should contain the pid */
	output = fopen (filename, "r");
	sprintf (buf, "%d\n", job->pid);
	TEST_FILE_EQ (output, buf);
	TEST_FILE_END (output);
	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);
}

void
test_run_script (void)
{
	Job  *job;
	FILE *output;
	char  filename[PATH_MAX];
	int   status, first;

	TEST_FUNCTION ("job_run_script");
	TEST_FILENAME (filename);

	/* Check that we can run a small shell script, and that it's run
	 * by using the shell directly and passing the script in on the
	 * command-line.
	 */
	TEST_FEATURE ("with small script");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->script = nih_sprintf (job, "exec > %s\necho $0\necho $@",
				   filename);
	job_run_script (job, job->script);

	TEST_NE (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	output = fopen (filename, "r");
	TEST_FILE_EQ (output, "/bin/sh\n");
	TEST_FILE_EQ (output, "\n");
	TEST_FILE_END (output);
	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);


	/* Check that shell scripts are run with the -e option set, so that
	 * any failing command causes the entire script to fail.
	 */
	TEST_FEATURE ("with script that will fail");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->script = nih_sprintf (job, "exec > %s\ntest -d %s\necho oops",
				   filename, filename);
	job_run_script (job, job->script);

	TEST_NE (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	output = fopen (filename, "r");
	TEST_FILE_END (output);
	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);


	/* Check that a particularly long script is instead invoked by
	 * using the /dev/fd feature, with the shell script fed to the
	 * child process by an NihIo structure.
	 */
	TEST_FEATURE ("with long script");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->script = nih_alloc (job, 4096);
	sprintf (job->script, "exec > %s\necho $0\necho $@\n", filename);
	while (strlen (job->script) < 4000)
		strcat (job->script, "# this just bulks it out a bit");
	job_run_script (job, job->script);

	TEST_NE (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	/* Loop until we've fed all of the data. */
	first = TRUE;
	control_close ();
	for (;;) {
		fd_set readfds, writefds, exceptfds;
		int    nfds;

		nfds = 0;
		FD_ZERO (&readfds);
		FD_ZERO (&writefds);
		FD_ZERO (&exceptfds);

		nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
		if (! nfds) {
			if (first)
				TEST_FAILED ("expected to have data to feed.");
			break;
		}
		first = FALSE;

		select (nfds, &readfds, &writefds, &exceptfds, NULL);

		nih_io_handle_fds (&readfds, &writefds, &exceptfds);
	}

	waitpid (job->pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	output = fopen (filename, "r");
	TEST_FILE_EQ_N (output, "/dev/fd/");
	TEST_FILE_EQ (output, "\n");
	TEST_FILE_END (output);
	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);
}


void
test_kill_process (void)
{
	Job         *job;
	NihTimer    *timer;
	pid_t        pid;
	char         filename[PATH_MAX];
	struct stat  statbuf;
	int          status;

	TEST_FUNCTION ("job_kill_process");
	job = job_new (NULL, "test");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->kill_timeout = 1000;


	/* Check that an easily killed process goes away with just a single
	 * call to job_kill_process, having received the TERM signal.  The
	 * process state should be changed to KILLED and a kill timer set
	 * to handle it if it doesn't get reaped.
	 */
	TEST_FEATURE ("with easily killed process");
	TEST_CHILD (job->pid) {
		pause ();
	}
	pid = job->pid;

	job_kill_process (job);
	waitpid (pid, &status, 0);

	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);

	TEST_EQ (job->pid, pid);
	TEST_EQ (job->process_state, PROCESS_KILLED);

	TEST_NE_P (job->kill_timer, NULL);
	TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
	TEST_ALLOC_PARENT (job->kill_timer, job);
	TEST_GE (job->kill_timer->due, time (NULL) + 950);
	TEST_LE (job->kill_timer->due, time (NULL) + 1000);

	nih_free (job->kill_timer);
	job->kill_timer = NULL;


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal and makes out that the
	 * job has in fact died.
	 */
	TEST_FEATURE ("with hard to kill process");
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	TEST_CHILD (job->pid) {
		struct sigaction act;

		act.sa_handler = SIG_IGN;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (SIGTERM, &act, NULL);

		for (;;)
			pause ();
	}
	pid = job->pid;

	job_kill_process (job);

	TEST_EQ (kill (pid, 0), 0);

	TEST_EQ (job->pid, pid);
	TEST_EQ (job->process_state, PROCESS_KILLED);

	TEST_NE_P (job->kill_timer, NULL);
	TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
	TEST_ALLOC_PARENT (job->kill_timer, job);
	TEST_GE (job->kill_timer->due, time (NULL) + 950);
	TEST_LE (job->kill_timer->due, time (NULL) + 1000);

	/* Run the kill timer */
	timer = job->kill_timer;
	timer->callback (timer->data, timer);
	nih_free (timer);

	waitpid (pid, &status, 0);

	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);

	TEST_EQ (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_NONE);

	TEST_EQ_P (job->kill_timer, NULL);

	TEST_EQ (job->state, JOB_WAITING);


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal and makes out that the
	 * job has in fact died.  Also make sure it triggers a state
	 * transition by using a stop sdcript and checking that it has run.
	 */
	TEST_FEATURE ("with hard to kill process and stop script");
	TEST_FILENAME (filename);
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->stop_script = nih_sprintf (job, "touch %s", filename);
	TEST_CHILD (job->pid) {
		struct sigaction act;

		act.sa_handler = SIG_IGN;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (SIGTERM, &act, NULL);

		for (;;)
			pause ();
	}
	pid = job->pid;

	job_kill_process (job);

	TEST_EQ (kill (pid, 0), 0);

	TEST_EQ (job->pid, pid);
	TEST_EQ (job->process_state, PROCESS_KILLED);

	TEST_NE_P (job->kill_timer, NULL);
	TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
	TEST_ALLOC_PARENT (job->kill_timer, job);
	TEST_GE (job->kill_timer->due, time (NULL) + 950);
	TEST_LE (job->kill_timer->due, time (NULL) + 1000);

	/* Run the kill timer */
	timer = job->kill_timer;
	timer->callback (timer->data, timer);
	nih_free (timer);

	TEST_EQ_P (job->kill_timer, NULL);

	waitpid (pid, &status, 0);

	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);

	TEST_NE (job->pid, 0);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, NULL, 0);

	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);
	nih_free (job->stop_script);
	job->stop_script = NULL;


	/* Check that if we kill an already dead process, the process is
	 * forgotten and the state transitioned immediately.
	 */
	TEST_FEATURE ("with already dead process");
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	TEST_CHILD (job->pid) {
		exit (0);
	}
	waitpid (job->pid, NULL, 0);

	job_kill_process (job);

	TEST_EQ (job->pid, 0);
	TEST_EQ (job->process_state, PROCESS_NONE);
	TEST_EQ (job->state, JOB_WAITING);

	TEST_EQ_P (job->kill_timer, NULL);


	nih_list_free (&job->entry);
}


static int was_called = 0;

static int
destructor_called (void *ptr)
{
	was_called++;
	return 0;
}

void
test_child_reaper (void)
{
	Job  *job;
	FILE *output;
	int   exitcodes[1] = { 0 };

	TEST_FUNCTION ("job_child_reaper");
	program_name = "test";

	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job->command = "echo";
	job->stop_script = "echo";
	job->respawn_script = "echo";


	/* Check that the child reaper can be called with a pid that doesn't
	 * match the job, and that the job state doesn't change.
	 */
	TEST_FEATURE ("with unknown pid");
	job_child_reaper (NULL, 999, FALSE, 0);

	TEST_EQ (job->state, JOB_RUNNING);


	/* Check that we can reap the running task of the job, which should
	 * set the goal to stop and transition a state change into the
	 * stopping state.
	 */
	TEST_FEATURE ("with running task");
	job_child_reaper (NULL, 1, FALSE, 0);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);


	/* Check that we can reap a running task of the job after it's been
	 * sent the TERM signal and a kill timer set.  The kill timer should
	 * be cancelled and freed.
	 */
	TEST_FEATURE ("with kill timer");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;

	was_called = 0;
	job->kill_timer = (void *) nih_strdup (job, "test");
	nih_alloc_set_destructor (job->kill_timer, destructor_called);

	job_child_reaper (NULL, 1, FALSE, 0);

	TEST_TRUE (was_called);
	TEST_EQ_P (job->kill_timer, NULL);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);


	/* Check that we can reap the starting task of the job, and if it
	 * terminates with a good error code, end up in the running state.
	 */
	TEST_FEATURE ("with starting task");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job_child_reaper (NULL, 1, FALSE, 0);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);


	/* Check that we can reap a failing starting task of the job, which
	 * changes the goal to stop and transitions a state change in that
	 * direction to the stopping state.  An error should be emitted.
	 */
	TEST_FEATURE ("with starting task failure");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;

	output = tmpfile ();
	TEST_DIVERT_STDERR (output) {
		job_child_reaper (NULL, 1, FALSE, 1);
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);

	TEST_FILE_EQ (output, ("test: test process (1) terminated "
			       "with status 1\n"));
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);



	/* Check that we can reap a killed starting task, which should
	 * act as if it failed.  A different error should be output.
	 */
	TEST_FEATURE ("with starting task kill");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;

	TEST_DIVERT_STDERR (output) {
		job_child_reaper (NULL, 1, TRUE, SIGTERM);
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);

	TEST_FILE_EQ (output, ("test: test process (1) killed "
			       "by signal 15\n"));
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that we can catch the running task failing, and if the job
	 * is to be respawned, go into the respawning state instead of
	 * stopping the job.  This should also emit a warning.
	 */
	TEST_FEATURE ("with running task to respawn");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job->respawn = TRUE;

	TEST_DIVERT_STDERR (output) {
		job_child_reaper (NULL, 1, FALSE, 0);
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RESPAWNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);

	TEST_FILE_EQ (output, "test: test process ended, respawning\n");
	TEST_FILE_END (output);

	fclose (output);


	/* Check that we can catch a running task exiting with a "normal"
	 * exit code, and even if it's marked respawn, setting the goal to
	 * stop and transitioning into the stopping state.
	 */
	TEST_FEATURE ("with running task and normal exit");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job->respawn = TRUE;
	job->normalexit = exitcodes;
	job->normalexit_len = 1;
	job_child_reaper (NULL, 1, FALSE, 0);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 1);

	waitpid (job->pid, NULL, 0);


	nih_list_free (&job->entry);

	event_queue_run ();
}


void
test_start (void)
{
	Job *job;

	TEST_FUNCTION ("job_start");
	program_name = "test";

	job = job_new (NULL, "test");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job->pid = 0;
	job->start_script = "echo";


	/* Check that an attempt to start a waiting job results in the
	 * goal being changed to start, and the state transitioned to
	 * starting.
	 */
	TEST_FEATURE ("with waiting job");
	job_start (job);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 0);

	waitpid (job->pid, NULL, 0);


	/* Check that an attempt to start a job that's in the process of
	 * stopping changes only the goal, and leaves the rest of the
	 * state transition up to the normal process.
	 */
	TEST_FEATURE ("with stopping job");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job_start (job);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_EQ (job->pid, 1);


	/* Check that an attempt to start a job that's running and still
	 * with a start goal does nothing.
	 */
	TEST_FEATURE ("with running job");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job_start (job);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_EQ (job->pid, 1);

	nih_list_free (&job->entry);
}

void
test_stop (void)
{
	Job   *job;
	pid_t  pid;
	int    status;

	TEST_FUNCTION ("job_stop");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;

	TEST_CHILD (job->pid) {
		pause ();
	}
	pid = job->pid;


	/* Check that an attempt to stop a running job results in the goal
	 * being changed, the job killed and the state left to be changed by
	 * the child reaper.
	 */
	TEST_FEATURE ("with running job");
	job_stop (job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_KILLED);
	TEST_EQ (job->pid, pid);

	TEST_NE_P (job->kill_timer, NULL);
	TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
	TEST_ALLOC_PARENT (job->kill_timer, job);

	nih_free (job->kill_timer);
	job->kill_timer = NULL;

	waitpid (pid, &status, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);


	/* Check that an attempt to stop a starting job only results in the
	 * goal being changed, the starting process should not be killed and
	 * instead left to terminate normally.
	 */
	TEST_FEATURE ("with starting job");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;

	TEST_CHILD (job->pid) {
		pause ();
	}
	pid = job->pid;

	job_stop (job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STARTING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_EQ (job->pid, pid);

	TEST_EQ_P (job->kill_timer, NULL);

	kill (pid, SIGTERM);
	waitpid (pid, NULL, 0);


	/* Check that an attempt to stop a waiting job does nothing. */
	TEST_FEATURE ("with waiting job");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job->pid = 0;

	job_stop (job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process_state, PROCESS_NONE);
	TEST_EQ (job->pid, 0);


	nih_list_free (&job->entry);
}


void
test_start_event (void)
{
	Event *event;
	Job   *job;

	TEST_FUNCTION ("job_start_event");
	job = job_new (NULL, "test");
	job->command = "echo";

	event = event_new (job, "wibble");
	nih_list_add (&job->start_events, &event->entry);


	/* Check that we can't start a job with an event that doesn't match
	 * any in the start events list.
	 */
	TEST_FEATURE ("with non-matching event");
	event = event_new (NULL, "biscuit");
	job_start_event (job, event);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process_state, PROCESS_NONE);

	nih_free (event);


	/* Check that we can start a job with an event that matches, which
	 * results in job_start being called.
	 */
	TEST_FEATURE ("with matching event");
	event = event_new (NULL, "wibble");
	job_start_event (job, event);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_NE (job->pid, 0);
	waitpid (job->pid, NULL, 0);

	nih_free (event);
	nih_list_free (&job->entry);
}

void
test_stop_event (void)
{
	Event *event;
	Job   *job;
	pid_t  pid;
	int    status;

	TEST_FUNCTION ("job_stop_event");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;

	TEST_CHILD (job->pid) {
		pause ();
	}
	pid = job->pid;

	event = event_new (job, "wibble");
	nih_list_add (&job->stop_events, &event->entry);


	/* Check that we can't stop a job with an event that doesn't match
	 * any in the stop events list.
	 */
	TEST_FEATURE ("with non-matching event");
	event = event_new (NULL, "biscuit");
	job_stop_event (job, event);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_EQ (job->pid, pid);
	TEST_EQ_P (job->kill_timer, NULL);

	nih_free (event);


	/* Check that we can stop a job with an event that matches, which
	 * results in job_stop being called.
	 */
	TEST_FEATURE ("with matching event");
	event = event_new (NULL, "wibble");
	job_stop_event (job, event);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_KILLED);

	TEST_EQ (job->pid, pid);
	TEST_NE_P (job->kill_timer, NULL);

	waitpid (job->pid, &status, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);

	nih_free (event);
	nih_list_free (&job->entry);
}

void
test_handle_event (void)
{
	Event *event;
	Job   *job1, *job2, *job3, *job4, *job5;
	int    status;

	/* Check that an event starts all jobs that have it in their start
	 * events list, stops all jobs that have it in their stop events list
	 * and restarts any that have it in both.
	 */
	TEST_FUNCTION ("job_handle_event");
	job1 = job_new (NULL, "foo");
	job1->goal = JOB_STOP;
	job1->state = JOB_WAITING;
	job1->process_state = PROCESS_NONE;
	job1->command = "echo";
	nih_list_add (&job1->start_events, &(event_new (job1, "poke")->entry));

	job2 = job_new (NULL, "bar");
	job2->goal = JOB_START;
	job2->state = JOB_RUNNING;
	job2->process_state = PROCESS_ACTIVE;
	nih_list_add (&job2->stop_events, &(event_new (job2, "poke")->entry));

	TEST_CHILD (job2->pid) {
		pause ();
	}

	job3 = job_new (NULL, "baz");
	job3->goal = JOB_START;
	job3->state = JOB_RUNNING;
	job3->process_state = PROCESS_ACTIVE;
	nih_list_add (&job3->start_events, &(event_new (job3, "poke")->entry));
	nih_list_add (&job3->stop_events, &(event_new (job3, "poke")->entry));

	TEST_CHILD (job3->pid) {
		pause ();
	}

	job4 = job_new (NULL, "frodo");
	job4->goal = JOB_STOP;
	job4->state = JOB_WAITING;
	job4->process_state = PROCESS_NONE;
	job4->command = "echo";

	job5 = job_new (NULL, "bilbo");
	job5->goal = JOB_START;
	job5->state = JOB_RUNNING;
	job5->process_state = PROCESS_ACTIVE;

	TEST_CHILD (job5->pid) {
		pause ();
	}

	event = event_new (NULL, "poke");
	job_handle_event (event);

	TEST_EQ (job1->goal, JOB_START);
	TEST_EQ (job1->state, JOB_RUNNING);
	TEST_EQ (job1->process_state, PROCESS_ACTIVE);

	TEST_NE (job1->pid, 0);
	kill (job1->pid, SIGTERM);
	waitpid (job1->pid, NULL, 0);


	TEST_EQ (job2->goal, JOB_STOP);
	TEST_EQ (job2->state, JOB_RUNNING);
	TEST_EQ (job2->process_state, PROCESS_KILLED);

	TEST_NE (job2->pid, 0);
	waitpid (job2->pid, &status, 0);

	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);


	TEST_EQ (job3->goal, JOB_START);
	TEST_EQ (job3->state, JOB_RUNNING);
	TEST_EQ (job3->process_state, PROCESS_KILLED);

	TEST_NE (job3->pid, 0);
	waitpid (job3->pid, &status, 0);

	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);


	TEST_EQ (job4->goal, JOB_STOP);
	TEST_EQ (job4->state, JOB_WAITING);
	TEST_EQ (job4->process_state, PROCESS_NONE);

	TEST_EQ (job4->pid, 0);


	TEST_EQ (job5->goal, JOB_START);
	TEST_EQ (job5->state, JOB_RUNNING);
	TEST_EQ (job5->process_state, PROCESS_ACTIVE);

	TEST_NE (job5->pid, 0);
	kill (job5->pid, SIGTERM);
	waitpid (job5->pid, NULL, 0);


	nih_free (event);

	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job3->entry);
	nih_list_free (&job4->entry);
	nih_list_free (&job5->entry);
}

void
test_detect_idle (void)
{
	Job     *job1, *job2;
	Event   *event;
	NihList *list;

	TEST_FUNCTION ("job_detect_idle");

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...
	 */
	event_queue_run ();
	event = event_queue ("wibble");
	list = event->entry.prev;
	nih_list_free (&event->entry);

	job1 = job_new (NULL, "foo");
	job1->goal = JOB_STOP;
	job1->state = JOB_WAITING;
	job1->process_state = PROCESS_NONE;

	job2 = job_new (NULL, "bar");
	job2->goal = JOB_STOP;
	job2->state = JOB_WAITING;
	job2->process_state = PROCESS_NONE;


	/* Check that even if we detect the stalled state, we do nothing
	 * if there's no handled for it.
	 */
	TEST_FEATURE ("with stalled state and no handler");
	job_detect_idle ();

	TEST_LIST_EMPTY (list);


	/* Check that we can detect the stalled event, when all jobs are
	 * stopped, which results in the stalled event being queued but
	 * not the idle event.
	 */
	TEST_FEATURE ("with stalled state");
	event = event_new (job1, "stalled");
	nih_list_add (&job1->start_events, &event->entry);
	job_detect_idle ();

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "stalled");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect either state if one of the jobs
	 * is just waiting.
	 */
	TEST_FEATURE ("with waiting job");
	job1->goal = JOB_START;
	job_set_idle_event ("reboot");
	job_detect_idle ();

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect either state if one of the jobs
	 * is starting.
	 */
	TEST_FEATURE ("with starting job");
	job1->state = JOB_STARTING;
	job_set_idle_event ("reboot");
	job_detect_idle ();

	TEST_LIST_EMPTY (list);


	/* Check that we detect the idle state if the jobs are either
	 * stopped and waiting or starting and running.
	 */
	TEST_FEATURE ("with running job");
	job1->state = JOB_RUNNING;
	job1->process_state = PROCESS_ACTIVE;
	job_set_idle_event ("reboot");
	job_detect_idle ();

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "reboot");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect either state if one of the jobs is
	 * waiting.
	 */
	TEST_FEATURE ("with stopping job");
	job1->goal = JOB_STOP;
	job1->state = JOB_STOPPING;
	job1->process_state = PROCESS_NONE;
	job_set_idle_event ("reboot");
	job_detect_idle ();

	TEST_LIST_EMPTY (list);


	/* Check that if both a stalled handler and idle event are set,
	 * only the idle event is issued when we're really stalled.
	 */
	TEST_FEATURE ("with stalled state and idle event");
	job1->state = JOB_WAITING;
	job_set_idle_event ("reboot");
	job_detect_idle ();

	/* Idle event should have been queued */
	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "reboot");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);

	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);
}


void
test_read_state (void)
{
	Job  *job, *ptr;
	char  buf[80];

	TEST_FUNCTION ("job_read_state");
	job = job_new (NULL, "test");


	/* Check that a Job header allocates or returns a job of that name. */
	TEST_FEATURE ("with header");
	sprintf (buf, "Job test");
	ptr = job_read_state (NULL, buf);

	TEST_EQ_P (ptr, job);


	/* Check that the goal line sets the goal of the job. */
	TEST_FEATURE ("with goal");
	sprintf (buf, ".goal start");
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_EQ (job->goal, JOB_START);

	/* Check that the state line sets the state of the job. */
	TEST_FEATURE ("with state");
	sprintf (buf, ".state stopping");
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_EQ (job->state, JOB_STOPPING);


	/* Check that the process_state line sets the process state of
	 * the job.
	 */
	TEST_FEATURE ("with process state");
	sprintf (buf, ".process_state active");
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);


	/* Check that the pid line sets the process id of the job. */
	TEST_FEATURE ("with pid");
	sprintf (buf, ".pid 9128");
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_EQ (job->pid, 9128);


	/* Check that the kill_timer_due line results in a timer being
	 * allocated with that amount of time left.
	 */
	TEST_FEATURE ("with kill timer due");
	sprintf (buf, ".kill_timer_due %ld", time (NULL) + 10);
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_NE_P (job->kill_timer, NULL);
	TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
	TEST_ALLOC_PARENT (job->kill_timer, job);
	TEST_LE (job->kill_timer->due, time (NULL) + 10);
	TEST_EQ_P (job->kill_timer->data, job);


	/* Check that the respawn count_line results in the respawn count
	 * of the job being set.
	 */
	TEST_FEATURE ("with respawn count");
	sprintf (buf, ".respawn_count 6");
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_EQ (job->respawn_count, 6);


	/* Check that the respawn_time line results in the respawn time
	 * of the job being set.
	 */
	TEST_FEATURE ("with respawn time");
	sprintf (buf, ".respawn_time 91");
	ptr = job_read_state (job, buf);

	TEST_EQ_P (ptr, job);
	TEST_EQ (job->respawn_time, 91);

	nih_list_free (&job->entry);
}

void
test_write_state (void)
{
	FILE   *output;
	Job    *job1, *job2;

	/* Check that we can write the state of multiple jobs out into a
	 * text form that can be passed between init processes of different
	 * versions.
	 */
	TEST_FUNCTION ("job_write_state");
	job1 = job_new (NULL, "frodo");
	job1->goal = JOB_START;
	job1->state = JOB_RUNNING;
	job1->process_state = PROCESS_SPAWNED;
	job1->pid = 1234;
	job1->respawn_count = 3;
	job1->respawn_time = 888;

	job2 = job_new (NULL, "bilbo");
	job2->goal = JOB_STOP;
	job2->state = JOB_STOPPING;
	job2->process_state = PROCESS_KILLED;
	job2->pid = 999;
	job2->respawn_count = 0;
	job2->respawn_time = 0;

	output = tmpfile ();
	job_write_state (output);

	rewind (output);

	TEST_FILE_EQ (output, "Job frodo\n");
	TEST_FILE_EQ (output, ".goal start\n");
	TEST_FILE_EQ (output, ".state running\n");
	TEST_FILE_EQ (output, ".process_state spawned\n");
	TEST_FILE_EQ (output, ".pid 1234\n");
	TEST_FILE_EQ (output, ".respawn_count 3\n");
	TEST_FILE_EQ (output, ".respawn_time 888\n");
	TEST_FILE_EQ (output, "Job bilbo\n");
	TEST_FILE_EQ (output, ".goal stop\n");
	TEST_FILE_EQ (output, ".state stopping\n");
	TEST_FILE_EQ (output, ".process_state killed\n");
	TEST_FILE_EQ (output, ".pid 999\n");
	TEST_FILE_EQ (output, ".respawn_count 0\n");
	TEST_FILE_EQ (output, ".respawn_time 0\n");
	TEST_FILE_END (output);

	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);

	fclose (output);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_find_by_name ();
	test_find_by_pid ();
	test_change_state ();
	test_next_state ();
	test_run_command ();
	test_run_script ();
	test_kill_process ();
	test_child_reaper ();
	test_start ();
	test_stop ();
	test_start_event ();
	test_stop_event ();
	test_handle_event ();
	test_detect_idle ();
	test_read_state ();
	test_write_state ();

	return 0;
}
