/*
    gstplay -- Simple gstreamer-based media player

    Copyright 2013 Harm Hanemaaijer <fgenfb@yahoo.com>
 
    gstplay is free software: you can redistribute it and/or modify it
    under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    gstplay is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with gstplay.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <dirent.h>
#include <stdint.h>
#include <glob.h>
#include <glib.h>
#include <glib-object.h>
#include "gstplay.h"

static gboolean thread_info_enabled = FALSE;

/*
 * Get pid of process by name.
 */

static pid_t find_pid(const char *process_name)
{
	pid_t pid = -1;
	glob_t pglob;
	char *procname, *readbuf;
	int buflen = strlen(process_name) + 2;
	unsigned i;

	/* Get a list of all comm files. man 5 proc */
	if (glob("/proc/*/comm", 0, NULL, &pglob) != 0)
		return pid;

	/* The comm files include trailing newlines, so... */
	procname = malloc(buflen);
	strcpy(procname, process_name);
	procname[buflen - 2] = '\n';
	procname[buflen - 1] = 0;

	/* readbuff will hold the contents of the comm files. */
	readbuf = malloc(buflen);

	for (i = 0; i < pglob.gl_pathc; ++i) {
		FILE *comm;
		char *ret;

		/* Read the contents of the file. */
		if ((comm = fopen(pglob.gl_pathv[i], "r")) == NULL)
			continue;
		ret = fgets(readbuf, buflen, comm);
		fclose(comm);
		if (ret == NULL)
			continue;

		/*
		   If comm matches our process name, extract the process ID from the
		   path, convert it to a pid_t, and return it.
		 */
		if (strcmp(readbuf, procname) == 0) {
			pid =
			    (pid_t) atoi(pglob.gl_pathv[i] + strlen("/proc/"));
			break;
		}
	}

	/* Clean up. */
	free(procname);
	free(readbuf);
	globfree(&pglob);

	return pid;
}

/*
 * CPU usage calculation module.
 */

struct thread_stats_t {
	int pid;
	char name[32];
	uint64_t utime_ticks;
	int64_t cutime_ticks;
	uint64_t stime_ticks;
	int64_t cstime_ticks;
	uint64_t vsize;		// virtual memory size in bytes
	uint64_t rss;		//Resident  Set  Size in bytes
};

struct pstat {
	struct thread_stats_t process_stats;
	int num_threads;
	int max_thread_stats;
	struct thread_stats_t *thread_stats;
	uint64_t cpu_total_time;
};

static void init_pstat(struct pstat *p) {
	p->num_threads = 0;
	p->max_thread_stats = 0;
	strcpy(p->process_stats.name, "Undefined");
}

static void clear_thread_stats(struct thread_stats_t *thread_stats) {
	thread_stats->utime_ticks = 0;
	thread_stats->cutime_ticks = 0;
	thread_stats->stime_ticks = 0;
	thread_stats->cstime_ticks = 0;
	thread_stats->vsize = 0;
	thread_stats->rss = 0;
}

/*
 * read /proc data into the passed struct pstat
 * returns 0 on success, -1 on error
 */
static int get_usage(const pid_t pid, struct pstat *result)
{
	//convert  pid to string
	char pid_s[20];
	snprintf(pid_s, sizeof(pid_s), "%d", pid);
	char stat_filepath[30] = "/proc/";
	strncat(stat_filepath, pid_s,
		sizeof(stat_filepath) - strlen(stat_filepath) - 1);
	char tasks_filepath[64];
	strcpy(tasks_filepath, stat_filepath);
	strncat(stat_filepath, "/stat", sizeof(stat_filepath) -
		strlen(stat_filepath) - 1);

	FILE *fpstat = fopen(stat_filepath, "r");
	if (fpstat == NULL) {
		printf("gstplay: Couldn't open %s.", stat_filepath);
		return -1;
	}

	FILE *fstat = fopen("/proc/stat", "r");
	if (fstat == NULL) {
		printf("gstplay: Couldn't open /proc/stat.\n");
		fclose(fstat);
		return -1;
	}
	// Read values from /proc/pid/stat.
	clear_thread_stats(&result->process_stats);
	result->process_stats.pid = pid;
	int64_t rss;
	if (fscanf
	    (fpstat,
	     "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
	     "%lu %ld %ld %*d %*d %d %*d %*u %lu %ld",
	     &result->process_stats.utime_ticks,
	     &result->process_stats.stime_ticks,
	     &result->process_stats.cutime_ticks,
	     &result->process_stats.cstime_ticks, &result->num_threads,
	     &result->process_stats.vsize, &rss) == EOF) {
		fclose(fpstat);
		return -1;
	}
	fclose(fpstat);
	result->process_stats.rss = rss * getpagesize();

	//read+calc cpu total time from /proc/stat
	uint64_t cpu_time[10];
	bzero(cpu_time, sizeof(cpu_time));
	if (fscanf(fstat, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
		   &cpu_time[0], &cpu_time[1], &cpu_time[2], &cpu_time[3],
		   &cpu_time[4], &cpu_time[5], &cpu_time[6], &cpu_time[7],
		   &cpu_time[8], &cpu_time[9]) == EOF) {
		fclose(fstat);
		return -1;
	}
	fclose(fstat);

	result->cpu_total_time = 0;
	for (int i = 0; i < 10; i++)
		result->cpu_total_time += cpu_time[i];

	if (!thread_info_enabled) {
		result->num_threads = 0;
		return 0;
	}

	// Read thread info.
	if (result->max_thread_stats < result->num_threads) {
		if (result->max_thread_stats > 0)
			free(result->thread_stats);
		result->thread_stats = malloc(sizeof(struct thread_stats_t) *
					      result->num_threads);
		result->max_thread_stats = result->num_threads;
	}

	strncat(tasks_filepath, "/task/", 64 - strlen(tasks_filepath) - 1);
	DIR *tasks_dir = opendir(tasks_filepath);

	for (int i = - 2; i < result->num_threads; i++) {
		struct dirent *dir_entry = readdir(tasks_dir);
		if (dir_entry == NULL)
			break;
		if (i < 0)
			continue;

		clear_thread_stats(&result->thread_stats[i]);
		char pid_str[16];
		strncpy(pid_str, dir_entry->d_name, 16);
		int j;
		for (j = 0; pid_str[j] >= '0' && pid_str[j] <= '9'; j++);
			pid_str[j] = '\0';
		result->thread_stats[i].pid = atoi(pid_str);

		char thread_stat_filepath[64];
		strcpy(thread_stat_filepath, tasks_filepath);
		strncat(thread_stat_filepath, pid_str,
			sizeof(thread_stat_filepath)
			- strlen(thread_stat_filepath) - 1);
		strcat(thread_stat_filepath, "/stat");
		FILE *ftstat = fopen(thread_stat_filepath, "rb");
		if (ftstat == NULL) {
			printf("gstplay: Couldn't open %s.", thread_stat_filepath);
			return -1;
		}
		int64_t rss;
		char s[1024];
		fread(s, 1024, 1, ftstat);
		fflush(stdout);
		int k = 0;
		while (s[k] != '(')
			k++;
		char *name = &s[k];
		while (s[k] != ')')
			k++;
		s[k + 1] = '\0';
		strncpy(result->thread_stats[i].name, name, 32);
		result->thread_stats[i].name[31] = '\0';
		if (sscanf(&s[k + 2],
		     "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
		     "%lu %ld %ld %*d %*d %d %*d %*u %lu %ld",
		     &result->thread_stats[i].utime_ticks,
		     &result->thread_stats[i].stime_ticks,
		     &result->thread_stats[i].cutime_ticks,
		     &result->thread_stats[i].cstime_ticks,
		     &result->num_threads, &result->thread_stats[i].vsize,
		     &rss) == EOF) {
			printf("Error reading %s.\n", thread_stat_filepath);
//			fclose(ftstat);
//			return -1;
		}
		fclose(ftstat);
		result->thread_stats[i].rss = rss * getpagesize();
	}
	closedir(tasks_dir);

	return 0;
}

/*
 * Calculate the elapsed CPU usage between two measuring points, in percent.
 */
static void calc_cpu_usage_pct(const struct pstat *cur_usage,
			       const struct pstat *last_usage,
			       double *ucpu_usage, double *scpu_usage,
				double *thread_ucpu_usage, double *thread_scpu_usage)
{
	const uint64_t total_time_diff = cur_usage->cpu_total_time -
	    last_usage->cpu_total_time;

	*ucpu_usage = 100 * (((cur_usage->process_stats.utime_ticks +
			       cur_usage->process_stats.cutime_ticks)
			      - (last_usage->process_stats.utime_ticks +
				 last_usage->process_stats.cutime_ticks))
			     / (double)total_time_diff);

	*scpu_usage =
	    100 *
	    ((((cur_usage->process_stats.stime_ticks +
		cur_usage->process_stats.cstime_ticks)
	       - (last_usage->process_stats.stime_ticks +
		  last_usage->process_stats.cstime_ticks))) /
	     (double)total_time_diff);

	if (thread_info_enabled && (thread_ucpu_usage != NULL || thread_scpu_usage != NULL)) {
		int j = 0;
		for (int i = 0; i < cur_usage->num_threads; i++) {
			fflush(stdout);
			if (j >= last_usage->num_threads ||
			last_usage->thread_stats[j].pid > cur_usage->thread_stats[i].pid) {
				thread_ucpu_usage[i] = - 1.0;
				thread_scpu_usage[i] = - 1.0;
				continue;
			}
			for (;;) {
				fflush(stdout);
				if (cur_usage->thread_stats[i].pid == last_usage->thread_stats[j].pid)
					break;
				j++;
				if (j >= last_usage->num_threads ||
				last_usage->thread_stats[j].pid > cur_usage->thread_stats[i].pid) {
					thread_ucpu_usage[i] = - 1.0;
					thread_scpu_usage[i] = - 1.0;
					goto next;
				}
			}
			fflush(stdout);
			if (thread_ucpu_usage != NULL) {
				thread_ucpu_usage[i] = 100 * ((
				(cur_usage->thread_stats[i].utime_ticks +
			       cur_usage->thread_stats[i].cutime_ticks)
			      - (last_usage->thread_stats[j].utime_ticks +
				 last_usage->thread_stats[j].cutime_ticks))
			     / (double)total_time_diff);
			}
			if (thread_scpu_usage != NULL) {
				thread_scpu_usage[i] = 100 * ((
				(cur_usage->thread_stats[i].stime_ticks +
			       cur_usage->thread_stats[i].cstime_ticks)
			      - (last_usage->thread_stats[j].stime_ticks +
				 last_usage->thread_stats[j].cstime_ticks))
			     / (double)total_time_diff);
			}
			j++;
next:			;
		}
	}
}

// Statistics

typedef struct {
	gpointer element;
	char *name;
	guint64 processed_frames;
	guint64 dropped_frames;
	guint64 base_processed_frames;
	guint64 base_dropped_frames;
} ElementStatistics;

static GList *element_statistics_list = NULL;
static gboolean stats_enabled = FALSE;
struct pstat pstat_process_base, pstat_Xserver_base;
struct pstat pstat_process_current, pstat_Xserver_current;
static pid_t process_pid = -1;
static pid_t X_pid = -1;

void stats_set_enabled(gboolean status)
{
	stats_enabled = status;
}

void stats_set_thread_info(gboolean status)
{
	thread_info_enabled = status;
}

void stats_reset()
{
	g_list_free(element_statistics_list);
	element_statistics_list = NULL;

	if (X_pid < 0) {
		X_pid = find_pid("Xorg");
		init_pstat(&pstat_Xserver_base);
		init_pstat(&pstat_Xserver_current);
		strcpy(pstat_Xserver_base.process_stats.name, "Xorg");
		strcpy(pstat_Xserver_current.process_stats.name, "Xorg");
	}

	if (process_pid < 0) {
		process_pid = getpid();
		init_pstat(&pstat_process_base);
		init_pstat(&pstat_process_current);
		strcpy(pstat_process_base.process_stats.name, "gstplay");
		strcpy(pstat_process_current.process_stats.name, "gstplay");
	}
	get_usage(process_pid, &pstat_process_base);
	if (X_pid >= 0)
		get_usage(X_pid, &pstat_Xserver_base);
}

void stats_report_dropped_frames_cb(gpointer element, const char *name,
				    guint64 processed, guint64 dropped)
{
	if (!stats_enabled)
		return;
	GList *list = g_list_first(element_statistics_list);
	while (list != NULL) {
		ElementStatistics *stats = list->data;
		if (stats->element == element) {
			stats->processed_frames = processed;
			stats->dropped_frames = dropped;
			break;
		}
		list = g_list_next(list);
	}
	if (list == NULL) {
		ElementStatistics *stats = malloc(sizeof(ElementStatistics));
		stats->element = element;
		stats->name = g_strdup(name);
		stats->processed_frames = processed;
		stats->dropped_frames = dropped;
		stats->base_processed_frames = processed;
		stats->base_dropped_frames = dropped;
		element_statistics_list = g_list_append(element_statistics_list,
							stats);
	}
}

gchar *stats_get_cpu_utilization_str()
{
	get_usage(process_pid, &pstat_process_current);
	double user_percent, sys_percent;
	double *thread_user_percentp, *thread_sys_percentp = NULL;
	if (thread_info_enabled) {
		thread_user_percentp = malloc(sizeof(double) * pstat_process_current.num_threads);
		thread_sys_percentp = malloc(sizeof(double) * pstat_process_current.num_threads);
	}
	calc_cpu_usage_pct(&pstat_process_current, &pstat_process_base,
			   &user_percent, &sys_percent, thread_user_percentp, thread_sys_percentp);
	char *s = g_strdup_printf("CPU utilization (application)\n"
		"%-38s user %4.1lf%%, sys %4.1lf%%\n"
		"Number of threads: %d\n",
		pstat_process_current.process_stats.name,
		user_percent, sys_percent, pstat_process_current.num_threads);
	if (thread_info_enabled) {
		for (int i = 0; i < pstat_process_current.num_threads; i++) {
			char *s2;
			if (thread_user_percentp[i] >= 0 && thread_sys_percentp[i] >= 0)
				s2 = g_strdup_printf("Thread %6d %-24s "
					"user %4.1lf%%, sys %4.1lf%%\n",
					pstat_process_current.thread_stats[i].pid,
					pstat_process_current.thread_stats[i].name,
					thread_user_percentp[i], thread_sys_percentp[i]);
			else
				s2 = g_strdup_printf("Thread %6d %-24s\n",
					pstat_process_current.thread_stats[i].pid,
					pstat_process_current.thread_stats[i].name);
			char *s1 = s;
			s = g_strconcat(s, s2, NULL);
			g_free(s2);
			g_free(s1);
		}
		if (thread_info_enabled) {
			free(thread_user_percentp);
			free(thread_sys_percentp);
		}
	}
	if (X_pid >= 0) {
		get_usage(X_pid, &pstat_Xserver_current);
		double X_user_percent, X_sys_percent;
		calc_cpu_usage_pct(&pstat_Xserver_current, &pstat_Xserver_base,
				   &X_user_percent, &X_sys_percent, NULL, NULL);
		char *s2 = g_strdup_printf
			("\nCPU utilization (X server)\n"
			"%-38s user %4.1lf%%, sys %4.1lf%%\n",
		  pstat_Xserver_current.process_stats.name, X_user_percent, X_sys_percent);
		char *s1 = s;
		s = g_strconcat(s, s2, NULL);
		g_free(s2);
		g_free(s1);
	}
	return s;
}

gchar *stats_get_dropped_frames_str()
{
	guint64 total_processed = 0;
	guint64 total_dropped = 0;
	guint64 total_base_processed = 0;
	guint64 total_base_dropped = 0;
	guint64 sink_processed = 0;
	guint64 sink_dropped = 0;
	guint64 sink_base_processed = 0;
	guint64 sink_base_dropped = 0;
	GList *list = element_statistics_list;
	if (list != NULL)
		list = g_list_first(list);
	while (list != NULL) {
		ElementStatistics *stats = list->data;
		if (stats->processed_frames > total_processed)
			total_processed = stats->processed_frames;
		total_dropped += stats->dropped_frames;
		if (stats->base_processed_frames > total_base_processed)
			total_base_processed = stats->base_processed_frames;
		total_base_dropped += stats->base_dropped_frames;
		if (strstr(stats->name, "sink") != NULL) {
			sink_processed = stats->processed_frames;
			sink_dropped = stats->dropped_frames;
			sink_base_processed = stats->base_processed_frames;
			sink_base_dropped = stats->base_dropped_frames;
		}
		list = g_list_next(list);
	}
	total_processed -= total_base_processed;
	total_dropped -= total_base_dropped;
	if (total_processed + total_dropped == 0)
		total_processed = 1;
	sink_processed -= sink_base_processed;
	sink_dropped -= sink_base_dropped;
	if (sink_processed + sink_dropped == 0)
		sink_processed = 1;
	return g_strdup_printf(
		"Total reported frames:          %lu\n"
		"Dropped frames:                 %.1lf%%\n"
		"Dropped frames by sink:         %.1lf%%",
		total_processed + total_dropped,
		(gdouble) total_dropped * 100.0 / (total_processed + total_dropped),
		(gdouble) sink_dropped * 100.0 / (sink_processed + sink_dropped));
}
