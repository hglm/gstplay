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
#include <glib.h>
#include "gstplay.h"

#define MAX_VIDEO_SINKS 10
#define MAX_AUDIO_SINKS 10

static int nu_video_sinks;
static const char **video_sink_name;
static int nu_audio_sinks;
static const char **audio_sink_name;

static StartupState startup_state;
static int video_sink_index;
static int audio_sink_index;
static gboolean video_only;
static gboolean quit_on_stream_end;
static gboolean software_volume;
static gboolean software_color_balance;

void config_init() {
	/* Initialize with defaults. */
	video_sink_name = malloc(sizeof(char *) * MAX_VIDEO_SINKS);
	nu_video_sinks = 4;
	video_sink_name[0] = "ximagesink";
	video_sink_name[1] = "xvimagesink";
	video_sink_name[2] = "autovideosink";
	video_sink_name[3] = "fakesink";
	audio_sink_name = malloc(sizeof(char *) * MAX_AUDIO_SINKS);
	nu_audio_sinks = 4;
	audio_sink_name[0] = "alsasink";
	audio_sink_name[1] = "jacksink";
	audio_sink_name[2] = "autoaudiosink";
	audio_sink_name[3] = "fakesink";
	startup_state = STARTUP_PLAYING;
	video_only = FALSE;
	video_sink_index = 2;
	audio_sink_index = 2;
	quit_on_stream_end = FALSE;
	software_volume = TRUE;
	software_color_balance = TRUE;
}

StartupState config_get_startup_preference() {
	return startup_state;
}

// Video and audio sinks.

void config_set_current_video_sink(const char *video_sink) {
	for (int i = 0; i < nu_video_sinks; i++)
		if (strcmp(video_sink, video_sink_name[i]) == 0) {
			video_sink_index = i;
			return;
		}
	// Add the video sink as a new entry.
	if (nu_video_sinks == MAX_VIDEO_SINKS) {
		printf("gstplay: Could not add new video sink.\n");
		return;
	}
	video_sink_name[nu_video_sinks] = video_sink;
	video_sink_index = nu_video_sinks;
	nu_video_sinks++;
}

void config_set_current_audio_sink(const char *audio_sink) {
	for (int i = 0; i < nu_audio_sinks; i++)
		if (strcmp(audio_sink, audio_sink_name[i]) == 0) {
			audio_sink_index = i;
			return;
		}
	// Add the audio sink as a new entry.
	if (nu_audio_sinks == MAX_AUDIO_SINKS) {
		printf("gstplay: Could not add new audio sink.\n");
		return;
	}
	audio_sink_name[nu_audio_sinks] = audio_sink;
	audio_sink_index = nu_audio_sinks;
	nu_audio_sinks++;
}

const char *config_get_current_video_sink() {
	return video_sink_name[video_sink_index];
}

const char *config_get_current_audio_sink() {
	return audio_sink_name[audio_sink_index];
}

void config_set_current_video_sink_by_index(int i) {
	config_set_current_video_sink(video_sink_name[i]);
}

void config_set_current_audio_sink_by_index(int i) {
	config_set_current_audio_sink(audio_sink_name[i]);
}

int config_get_number_of_video_sinks() {
	return nu_video_sinks;
}

int config_get_number_of_audio_sinks() {
	return nu_audio_sinks;
}

const char *config_get_video_sink_by_index(int i) {
	return video_sink_name[i];
}

const char *config_get_audio_sink_by_index(int i) {
	return audio_sink_name[i];
}

extern int config_get_current_video_sink_index() {
	return video_sink_index;
}

extern int config_get_current_audio_sink_index() {
	return audio_sink_index;
}

// Other settings.

void config_set_video_only(gboolean state) {
	video_only = state;
}

gboolean config_video_only() {
	return video_only;
}

void config_set_quit_on_stream_end(gboolean status) {
	quit_on_stream_end = status;
}

gboolean config_quit_on_stream_end() {
	return quit_on_stream_end;
}

void config_set_software_volume(gboolean status) {
	software_volume = status;
}

gboolean config_software_volume() {
	return software_volume;
}

void config_set_software_color_balance(gboolean status) {
	software_color_balance = status;
}

gboolean config_software_color_balance() {
	return software_color_balance;
}
