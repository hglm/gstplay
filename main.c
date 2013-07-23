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
#include <unistd.h>
#include <glib.h>
#include <gst/gst.h>
#include "gstplay.h"

/* The constants below don't appear to be defined in any standard header files. */
typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0),
  GST_PLAY_FLAG_AUDIO         = (1 << 1),
  GST_PLAY_FLAG_TEXT          = (1 << 2),
  GST_PLAY_FLAG_VIS           = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
  GST_PLAY_FLAG_BUFFERING     = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE   = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10)
} GstPlayFlags;

/* gstreamer version dependent definitions. */

#if GST_CHECK_VERSION(1, 0, 0)
#define PLAYBIN_STR "playbin"
#define DECODEBIN_STR "decodebin"
#else
#define PLAYBIN_STR "playbin2"
#define DECODEBIN_STR "decodebin2"
#endif

enum { DECODE_PATH_PLAYBIN = 0, DECODE_PATH_DECODEBIN, DECODE_PATH_MP4AVI,
	DECODE_PATH_MP4QT, DECODE_PATH_H264QT };
enum { VIDEO_SINK_AUTO = 0, VIDEO_SINK_XIMAGE, VIDEO_SINK_XVIMAGE };

/* Command line settings that otherwise are not included in the general configuration. */
static gboolean full_screen = FALSE;
static int decode_path = DECODE_PATH_PLAYBIN;
static gboolean preload_file = FALSE;
static gboolean verbose = FALSE;
static int width = 0;		// Requested width and height (0 = use video dimension).
static int height = 0;

GMainLoop *loop;
static const char *current_uri;
static const char *current_video_title_filename;

static void usage(int argc, char *argv[]) {
	printf("gstplay -- simple media player using gstreamer 1.0\n"
		"Usage:\n"
		"    gstplay <options> <filename>\n"
		"Options:\n"
		"    --help, --options This help message.\n"
		"    --width <n>       Set width of the output window.\n"
		"    --height <n>      Set height of the output window.\n"
		"    --fullscreen      Use full-screen output.\n"
		"    --videoonly       Display video only, drop audio.\n"
		"    --decodebin       Use decodebin instead of playbin.\n"
		"    --preload         Read the entire file into the buffer cache before\n"
		"                      playing.\n"
		"    --videosink <snk> Select to video output sink to use (for example\n"
		"                      xvimagesink or ximagesink). Default autovideosink.\n"
		"    --audiosink <snk> Select to audio output sink to use (for example\n"
		"                      alsasink or jackaudiosink). Default autoaudiosink.\n"
		"    --verbose         Print messages/info.\n"
		"    --quit            Quit application when the end of the stream is reached.\n"
		"The following three options can be used to replace playbin or decodebin\n"
		"with a specific decode path, which avoids audio processing completely when\n"
		"--videoonly is specified.\n"
		"    --mp4avi          Use the MPEG4 decode path for .avi files.\n"
		"    --mp4qt           Use the MPEG4 decode path for .mp4/mov files.\n"
		"    --h264qt          Use the H.264 decode path for .mov files.\n"
		);
}

static void check_and_preload_file(const char *filename, gboolean preload) {
	FILE *f;
	f = fopen(filename, "rb");
	if (f == NULL) {
		printf("Error: Could not open file %s.\n", filename);
		exit(1);
	}
	if (!preload) {
		fclose(f);
		return;
	}
	printf("gstplay: Preloading file.\n");
	char *buffer = malloc(4096 * 32);
	for (;;) {
		fread(buffer, 1, 4096 * 32, f);
		if (feof(f))
			break;
	}
	free(buffer);
	fclose(f);
}

const char *main_create_pipeline(const char *uri, const char *video_title_filename) {
	const char *adjusted_video_sink;
	const char *video_sink = config_get_current_video_sink();
	const char *audio_sink = config_get_current_audio_sink();
	if (strcmp(video_sink, "ximagesink") == 0)
		adjusted_video_sink = "videoconvert ! ximagesink";
	else
		adjusted_video_sink = video_sink;
	char *audio_pipeline = malloc(strlen(audio_sink) + 128);
	sprintf(audio_pipeline, "audioconvert ! audioresample ! %s", audio_sink);
	if (config_video_only())
		sprintf(audio_pipeline, "");

	char *source = malloc(strlen(uri) + 64);
	if (strstr(uri, "file://") != NULL)
		sprintf(source, "filesrc location=%s", video_title_filename);
	else
		// Any decode path other than playbin will require
		// the presence of the dataurissrc from the plugins-bad package
		// for non-file sources.
		sprintf(source, "dataurisrc uri=%s", uri);

	char *s = malloc(strlen(uri) + strlen(audio_sink) + strlen(video_sink) + 256);
	const char *glue = "";
	gstreamer_inform_playbin_used(FALSE);
	if (decode_path != DECODE_PATH_DECODEBIN & decode_path != DECODE_PATH_PLAYBIN) {
		if (!config_video_only())
			glue = "demuxer. ! queue ! ";
		if (decode_path == DECODE_PATH_MP4AVI)
			sprintf(s, "%s ! avidemux name=demuxer  "
				"demuxer. ! queue ! avdec_mpeg4 ! %s  %s%s", source,
				video_title_filename, adjusted_video_sink, glue, audio_pipeline);
		else if (decode_path == DECODE_PATH_MP4QT)
			sprintf(s, "%s ! qtdemux name=demuxer  "
				"demuxer. ! queue ! avdec_mpeg4 ! %s  %s%s", source,
				video_title_filename, adjusted_video_sink, glue, audio_pipeline);
		else if (decode_path == DECODE_PATH_H264QT)
			sprintf(s, "%s ! qtdemux name=demuxer  "
				"demuxer. ! queue ! avdec_h264 ! %s  %s%s", source,
				video_title_filename, adjusted_video_sink, glue, audio_pipeline);
	}
	else if (decode_path == DECODE_PATH_DECODEBIN) {
		if (!config_video_only())
			glue = "decoder. ! queue !";
		sprintf(s, "%s ! " DECODEBIN_STR " name=decoder  decoder. ! queue ! "
			" %s  %s %s", source,
			video_title_filename, adjusted_video_sink, glue, audio_pipeline);
	}
	else {	/* DECODE_PATH_PLAYBIN */
		char flags_str[80];
		flags_str[0] = '\0';
		int default_flags = GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO |
				GST_PLAY_FLAG_TEXT |
				GST_PLAY_FLAG_DEINTERLACE | GST_PLAY_FLAG_SOFT_VOLUME |
				GST_PLAY_FLAG_SOFT_COLORBALANCE;
		int flags = default_flags;
		if (config_video_only()) {
			flags &= ~(GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_SOFT_VOLUME);
			audio_sink = "fakesink";
		}
		if (!config_software_volume()) {
			flags &= ~(GST_PLAY_FLAG_SOFT_VOLUME);
		}
		if (gstreamer_have_software_color_balance()) {
			if (!config_software_color_balance()) {
				flags &= ~(GST_PLAY_FLAG_SOFT_COLORBALANCE);
			}
		}
		else
			/* GStreamer 0.10 doesn't support this flag. */
			flags &= ~(GST_PLAY_FLAG_SOFT_COLORBALANCE);
		sprintf(flags_str, " flags=%d", flags);
		sprintf(s,  PLAYBIN_STR " name=playbin uri=%s video-sink=%s audio-sink=%s%s",
			uri, video_sink, audio_sink, flags_str);
		gstreamer_inform_playbin_used(TRUE);
	}
	current_uri = uri;
	current_video_title_filename;
	return s;
}

void main_create_uri(const char *filespec, char **_uri, char **_video_title_filename) {
	if (strstr(filespec, "://") != NULL) {
		*_uri = strdup(filespec);
		*_video_title_filename = *_uri;
	}
	else {
		*_video_title_filename = strdup(filespec);

		check_and_preload_file(*_video_title_filename, preload_file);
		char *cwd = getcwd(NULL, 0);
		char *cwdstr;
		if ((*_video_title_filename)[0] == '/')
			cwdstr = "";
		else
			cwdstr = cwd;
		*_uri = malloc(strlen(cwdstr) + strlen(*_video_title_filename) + 32);
		sprintf(*_uri, "file://%s/%s", cwdstr, *_video_title_filename);
		free(cwd);
	}
}

void main_get_current_uri(const char **uri, const char **video_title_filename) {
	*uri = current_uri;
	*video_title_filename = current_video_title_filename;
}

GMainLoop *main_get_main_loop() {
	return loop;
}

int main(int argc, char *argv[]) {
	int argi = 1;

	config_init();

	gstreamer_init(&argc, &argv);

	gui_init(&argc, &argv);

	/* Process options. */
	for (;;) {
		if (argi >= argc)
			break;
		if (strcasecmp(argv[argi], "--width") == 0 && argi + 1 < argc) {
			width = atoi(argv[argi + 1]);
			if (width <= 0 || width > 4095) {
				printf("Width out of range.\n");
				return 1;
			}
			argi += 2;
			continue;
                }
		if (strcasecmp(argv[argi], "--height") == 0 && argi + 1 < argc) {
			height = atoi(argv[argi + 1]);
			if (height <= 0 || height > 4095) {
				printf("Height out of range.\n");
				return 1;
			}
			argi += 2;
			continue;
                }
		if (strcasecmp(argv[argi], "--fullscreen") == 0) {
			full_screen = TRUE;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--videoonly") == 0) {
			config_set_video_only(TRUE);
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--decodebin") == 0) {
			decode_path = DECODE_PATH_DECODEBIN;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--mp4avi") == 0) {
			decode_path = DECODE_PATH_MP4AVI;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--mp4qt") == 0) {
			decode_path = DECODE_PATH_MP4QT;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--h264qt") == 0) {
			decode_path = DECODE_PATH_H264QT;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--preload") == 0) {
			preload_file = TRUE;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--videosink") == 0 && argi + 1 < argc) {
			config_set_current_video_sink(argv[argi + 1]);
			argi += 2;
			continue;
                }
		if (strcasecmp(argv[argi], "--audiosink") == 0 && argi + 1 < argc) {
			config_set_current_audio_sink(argv[argi + 1]);
			argi += 2;
			continue;
                }
		if (strcasecmp(argv[argi], "--verbose") == 0) {
			verbose = TRUE;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--help") == 0 ||
		strcasecmp(argv[argi], "--options") == 0) {
			usage(argc, argv);
			return 0;
		}
		if (strcasecmp(argv[argi], "--quit") == 0) {
			config_set_quit_on_stream_end(TRUE);
			argi++;
			continue;
		}
		if (argv[argi][0] == '-') {
			printf("Unknown option %s.\n", argv[argi]);
			return 1;
		}
		break;
	}

	if (argi >= argc) {
		/* Run in interactive mode. */
		loop = g_main_loop_new(NULL, FALSE);
		if (width == 0)
			width = 720;
		if (height == 0)
			height = 576;
		gui_setup_window(loop, "", width, height, full_screen);
		g_main_loop_run(loop);
		g_main_loop_unref(loop);
		return 0;
	}

	char *uri;
	char *video_title_filename;
	main_create_uri(argv[argi], &uri, &video_title_filename);

	/* Determine the file type and video dimensions. */
	int video_width, video_height;
	gstreamer_determine_video_dimensions(uri, &video_width, &video_height);
	if (verbose)
		printf("gstplay: Video dimensions %dx%d\n", video_width, video_height);

	const char *s = main_create_pipeline(uri, video_title_filename);

	loop = g_main_loop_new(NULL, FALSE);

	if (width == 0)
		width = video_width;
	if (height == 0)
		height = video_height;
	gui_setup_window(loop, video_title_filename, width, height, full_screen);

	if (verbose)
		printf("gstplay: pipeline: %s\n", s);
	printf("gstplay: Playing %s\n", video_title_filename);

	gstreamer_run_pipeline(loop, s, config_get_startup_preference());

	g_main_loop_run(loop);

	gstreamer_destroy_pipeline();

	g_main_loop_unref(loop);

	return 0;
}

