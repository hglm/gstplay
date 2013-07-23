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
#include <gst/gst.h>
#if GST_CHECK_VERSION(1, 0, 0)
#include <gst/video/videooverlay.h>
#else
#include <gst/interfaces/xoverlay.h>
#endif
#include <glib.h>
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

#if GST_CHECK_VERSION(1, 0, 0)
#define GSTREAMER_X_OVERLAY GstVideoOverlay
#define PLAYBIN_STR "playbin"
#else
#define GSTREAMER_X_OVERLAY GstXOverlay
#define PLAYBIN_STR "playbin2"

/* Definitions for compatibility with GStreamer 0.10. */
#define GST_VIDEO_OVERLAY(x) GST_X_OVERLAY(x)

static inline void gst_video_overlay_expose(GstXOverlay *overlay) {
	gst_x_overlay_expose(overlay);
}

static inline void gst_video_overlay_set_window_handle(GstXOverlay *overlay, guintptr handle) {
	gst_x_overlay_set_window_handle(overlay, handle);
}

static inline gboolean gst_is_video_overlay_prepare_window_handle_message(GstMessage *message) {
	if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
		return FALSE;
	if (!gst_structure_has_name(message->structure, "prepare-xwindow-id"))
		return FALSE;
	return TRUE;
}

static inline GstCaps *gst_pad_get_current_caps(GstPad *pad) {
	return gst_pad_get_negotiated_caps(pad);
}

#endif

static GSTREAMER_X_OVERLAY *video_window_overlay = NULL;
static GstElement *playbin_pipeline;
static GstElement *pipeline;
static guint bus_watch_id;
static gboolean bus_quit_on_playing = FALSE;
static GList *created_pads_list = NULL;
static const char *pipeline_description = "";
static GstState suspended_state;
static GstClockTime suspended_pos;
static GstClockTime requested_position;
static gboolean end_of_stream = FALSE;
static gboolean using_playbin;

void gstreamer_expose_video_overlay() {
	if (video_window_overlay == NULL)
		return;
	gst_video_overlay_expose(video_window_overlay);
}

static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data) {
	GMainLoop *loop = (GMainLoop *)data;

	switch (GST_MESSAGE_TYPE (msg)) {
	case GST_MESSAGE_EOS:
		if (config_quit_on_stream_end())
			g_main_loop_quit(loop);
		end_of_stream = TRUE;
		break;
	case GST_MESSAGE_ERROR: {
		gchar  *debug;
		GError *error;

		gst_message_parse_error (msg, &error, &debug);
		g_free (debug);

		g_printerr ("Error: %s\n", error->message);
		g_error_free (error);

		g_main_loop_quit (loop);
		break;
		}
	case GST_MESSAGE_STATE_CHANGED:
		if (bus_quit_on_playing) {
			// When doing the initial run to determine video parameters,
			// stop immediately when play starts.
			if (GST_STATE(playbin_pipeline) == GST_STATE_PLAYING)
				g_main_loop_quit(loop);
		}
		break;
	case GST_MESSAGE_BUFFERING:
		if (bus_quit_on_playing)
			break;
		gint percent = 0;
		gst_message_parse_buffering(msg, &percent);
		if (percent < 100) {
			if (GST_STATE(pipeline) != GST_STATE_PAUSED)
				gst_element_set_state(pipeline, GST_STATE_PAUSED);
			printf("Buffering (%u percent done).n", percent);
		}
		else {
			if (GST_STATE(pipeline) != GST_STATE_PLAYING)
				gst_element_set_state(pipeline, GST_STATE_PLAYING);
		}
		break;
	default:
		break;
	}

	return TRUE;
}

static GstBusSyncReply bus_sync_handler(GstBus *bus, GstMessage *msg, gpointer data) {
	if (!gst_is_video_overlay_prepare_window_handle_message(msg))
		return GST_BUS_PASS;
	guintptr video_window_handle = gui_get_video_window_handle();
	g_assert(video_window_handle != 0);
	// GST_MESSAGE_SRC (message) will be the video sink element.
	video_window_overlay = GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(msg));
	gst_video_overlay_set_window_handle(video_window_overlay, video_window_handle);
	return GST_BUS_DROP;
}

void gstreamer_init(int *argcp, char **argvp[]) {
	gst_init(argcp, argvp);
	guint major, minor, micro, nano;
	gst_version(&major, &minor, &micro, &nano);
	if (major != GST_VERSION_MAJOR) {
		printf("Error: gstreamer API major version is not %d (version %d.%d found).\n",
			GST_VERSION_MAJOR, major, minor);
		exit(1);
	}
	if (minor != GST_VERSION_MINOR) {
		printf("Warning: gstreamer API version is not %d.%d (version %d.%d found).\n",
			GST_VERSION_MAJOR, GST_VERSION_MINOR, major, minor);
	}
}

// Return the dimensions of a non-running pipeline by test-running it briefly.

void gstreamer_determine_video_dimensions(const char *uri, int *video_width,
int *video_height) {
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);

	char *playbin_launch_str = malloc(strlen(uri) + 64);
	sprintf(playbin_launch_str, PLAYBIN_STR
		" uri=%s audio-sink=fakesink video-sink=fakesink", uri);
	GError *error2 = NULL;
	GstElement *playbin = gst_parse_launch(playbin_launch_str, &error2);
	if (error2) {
		printf("Error: Could not create gstreamer pipeline for identification.\n");
		printf("Parse error: %s\n", error2->message);
		exit(1);
	}

	playbin_pipeline = playbin;
	bus_quit_on_playing = TRUE;
	GstBus *playbin_bus = gst_pipeline_get_bus(GST_PIPELINE(playbin));
	guint type_find_bus_watch_id = gst_bus_add_watch(playbin_bus, bus_callback, loop);
	gst_object_unref(playbin_bus);

	gst_element_set_state(GST_ELEMENT(playbin), GST_STATE_READY);
	gst_element_set_state(GST_ELEMENT(playbin), GST_STATE_PLAYING);
	g_main_loop_run(loop);
	gst_element_set_state(GST_ELEMENT(playbin), GST_STATE_PAUSED);

	GstPad *pad = gst_pad_new("", GST_PAD_UNKNOWN);
	g_signal_emit_by_name(playbin, "get-video-pad", 0, &pad, NULL);
	GstCaps *caps = gst_pad_get_current_caps(pad);
	*video_width = g_value_get_int(gst_structure_get_value(
		gst_caps_get_structure(caps, 0), "width"));
	*video_height = g_value_get_int(gst_structure_get_value(
		gst_caps_get_structure(caps, 0), "height"));
	g_object_unref(pad);

	gst_element_set_state(GST_ELEMENT(playbin), GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(playbin));
	g_source_remove(type_find_bus_watch_id);
	g_main_loop_unref(loop);
}


static void read_video_props(GstCaps *caps, const char **formatp, int *widthp, int *heightp,
int *framerate_numeratorp, int *framerate_denomp, int *pixel_aspect_ratio_nump,
int *pixel_aspect_ratio_denomp) {
	const GstStructure *str;
	if (!gst_caps_is_fixed(caps))
		return;
	str = gst_caps_get_structure (caps, 0);
	int width = 0;
	int height = 0;
	int framerate_numerator = 0;
	int framerate_denom = 0;
	int pixel_aspect_ratio_num = 0;
	int pixel_aspect_ratio_denom = 0;

	const char *format = gst_structure_get_string(str, "format");
	gst_structure_get_int(str, "width", widthp);
	gst_structure_get_int(str, "height", heightp);
	gst_structure_get_fraction(str, "pixel-aspect-ratio", pixel_aspect_ratio_nump,
		pixel_aspect_ratio_denomp);
	gst_structure_get_fraction(str, "framerate", framerate_numeratorp, framerate_denomp);

	if (format != NULL)
		*formatp = format;
	if (width != 0)
		*widthp = width;
	if (height != 0)
		*heightp = height;
	if (pixel_aspect_ratio_num != 0 && pixel_aspect_ratio_denom != 0) {
		*pixel_aspect_ratio_nump = pixel_aspect_ratio_num;
		*pixel_aspect_ratio_denomp = pixel_aspect_ratio_denom;
	}
	if (framerate_numerator != 0 && framerate_denom != 0) {
		*framerate_numeratorp = framerate_numerator;
		*framerate_denomp = framerate_denom;
	}
}

// Return info about a running pipeline.

extern void gstreamer_get_video_info(const char **formatp, int *widthp, int *heightp,
int *framerate_numeratorp, int *framerate_denomp, int *pixel_aspect_ratio_nump,
int *pixel_aspect_ratio_denomp) {
	*formatp = NULL;
	*widthp = 0;
	*heightp = 0;
	*framerate_numeratorp = 0;
	*framerate_denomp = 0;
	*pixel_aspect_ratio_nump = 0;
	*pixel_aspect_ratio_denomp = 0;
	GList *list = g_list_first(created_pads_list);
	while (list != NULL) {
		GstPad *pad = list->data;
		GstCaps *caps = gst_pad_get_current_caps(pad);
		read_video_props(caps, formatp, widthp, heightp, framerate_numeratorp,
			framerate_denomp, pixel_aspect_ratio_nump,
			pixel_aspect_ratio_denomp);
		list = g_list_next(list);
	}
}

extern void gstreamer_get_video_dimensions(int *widthp, int *heightp) {
	const char *format = NULL;
	*widthp = 0;
	*heightp = 0;
	int framerate_numerator = 0;
	int framerate_denom = 0;
	int pixel_aspect_ratio_num = 0;
	int pixel_aspect_ratio_denom = 0;
	GList *list = g_list_first(created_pads_list);
	while (list != NULL) {
		GstPad *pad = list->data;
		GstCaps *caps = gst_pad_get_current_caps(pad);
		read_video_props(caps, &format, widthp, heightp, &framerate_numerator,
			&framerate_denom, &pixel_aspect_ratio_num,
			&pixel_aspect_ratio_denom);
		list = g_list_next(list);
	}
}

const char *gstreamer_get_pipeline_description() {
	return pipeline_description;
}

static void new_pad_cb(GstElement *element, GstPad *pad, gpointer data) {
	gchar *name;

	created_pads_list = g_list_append(created_pads_list, pad);
}

#if GST_CHECK_VERSION(1, 0, 0)
void for_each_pipeline_element(const GValue *value, gpointer data) {
	GstElement *element = g_value_get_object(value);
#else
void for_each_pipeline_element(gpointer value_data, gpointer data) {
	GstElement *element = value_data;
#endif

	/* Listen for newly created pads. */
	g_signal_connect(element, "pad-added", G_CALLBACK(new_pad_cb), NULL);
}

void gstreamer_run_pipeline(GMainLoop *loop, const char *s, StartupState state) {
	GError *error = NULL;
	pipeline = gst_parse_launch(s, &error);
	if (!pipeline) {
		printf("Error: Could not create gstreamer pipeline.\n");
		printf("Parse error: %s\n", error->message);
		exit(1);
	}

	bus_quit_on_playing = FALSE;
	GstBus *bus;
	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	bus_watch_id = gst_bus_add_watch(bus, bus_callback, loop);
#if GST_CHECK_VERSION(1, 0, 0)
	gst_bus_set_sync_handler(bus, (GstBusSyncHandler)bus_sync_handler, NULL, NULL);
#else
	gst_bus_set_sync_handler(bus, (GstBusSyncHandler)bus_sync_handler, NULL);
#endif
	gst_object_unref(bus);

	// Iterate over all elements of the pipeline to hook a handler
	// listing newly created pads.
	if (created_pads_list != NULL) {
		g_list_free(created_pads_list);
		created_pads_list = NULL;
	}
	GstIterator *iterator = gst_bin_iterate_elements(GST_BIN(pipeline));
	gst_iterator_foreach(iterator, for_each_pipeline_element, NULL);
	gst_iterator_free(iterator);

	gst_element_set_state(pipeline, GST_STATE_READY);

	if (state == STARTUP_PLAYING)
		gst_element_set_state(pipeline, GST_STATE_PLAYING);
	else
		gst_element_set_state(pipeline, GST_STATE_PAUSED);

	pipeline_description = s;
	end_of_stream = FALSE;
}

void gstreamer_destroy_pipeline() {
	gst_element_set_state(pipeline, GST_STATE_NULL);

	g_source_remove(bus_watch_id);
	gst_object_unref(GST_OBJECT(pipeline));
}

void gstreamer_play() {
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

void gstreamer_pause() {
	gst_element_set_state(pipeline, GST_STATE_PAUSED);
}

static GstState gstreamer_get_state() {
	GstState state = GST_STATE_VOID_PENDING;
	gst_element_get_state(pipeline, &state, NULL, 0);
	return state;
}

gint64 gstreamer_get_position(gboolean *error) {
	gint64 pos, len;

	if (end_of_stream) {
#if GST_CHECK_VERSION(1, 0, 0)
		if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &len)) {
#else
		GstFormat format = GST_FORMAT_TIME;
		if (gst_element_query_duration(pipeline, &format, &len)) {
#endif
			if (error != NULL)
				*error = FALSE;
			return len;
		}
		if (error != NULL)
			*error = TRUE;
		return 0;
	}

#if GST_CHECK_VERSION(1, 0, 0)
	if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos)) {
		if (gst_element_query_duration (pipeline, GST_FORMAT_TIME, &len)) {
#else
	GstFormat format = GST_FORMAT_TIME;
	if (gst_element_query_position(pipeline, &format, &pos)) {
		GstFormat format2 = GST_FORMAT_TIME;
		if (gst_element_query_duration(pipeline, &format2, &len)) {
#endif
			if (error != NULL)
				*error = FALSE;
			return pos;
		}
	}
	printf("gstplay: Could not succesfully query current position.\n");
	if (error != NULL)
		*error = TRUE;
	return 0;
}

gint64 gstreamer_get_duration() {
	GstQuery *query;
	gboolean res;
	query = gst_query_new_duration (GST_FORMAT_TIME);
	res = gst_element_query (pipeline, query);
	gint64 duration;
	if (res) {
		gst_query_parse_duration(query, NULL, &duration);
	}
	else
		duration = 0;
	gst_query_unref (query);
	return duration;
}

const gchar *gstreamer_get_duration_str() {
	gint64 duration = gstreamer_get_duration();
	char s[80];
	sprintf(s, "%"GST_TIME_FORMAT, GST_TIME_ARGS(duration));
	return strdup(s);
}

static gboolean seek_to_time_cb(gpointer data) {
	gstreamer_seek_to_time(requested_position);
	return FALSE;
}

void gstreamer_seek_to_time(gint64 time_nanoseconds) {
	end_of_stream = FALSE;
	if (!gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
	GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, time_nanoseconds)) {
		printf("gstplay: Seek failed!.n");
	}
}

gboolean gstreamer_end_of_stream() {
	return end_of_stream;
}

gboolean gstreamer_no_pipeline() {
	return strlen(pipeline_description) == 0;
}

void gstreamer_suspend_pipeline() {
	if (gstreamer_no_pipeline()) {
		suspended_state = GST_STATE_NULL;
		return;
	}
	/* Save the current position and wind down the pipeline. */
	suspended_pos = gstreamer_get_position(NULL);
	suspended_state = gstreamer_get_state();
	gstreamer_pause();
	gstreamer_destroy_pipeline();
}

void gstreamer_restart_pipeline() {
	if (suspended_state == GST_STATE_NULL)
		return;
	/* Restart the pipeline. */
	const char *uri;
	const char *video_title_filename;
	main_get_current_uri(&uri, &video_title_filename);
	const char *pipeline_str = main_create_pipeline(uri, video_title_filename);
	StartupState startup;
	if (suspended_state == GST_STATE_PLAYING)
		startup = STARTUP_PLAYING;
	else
		startup = STARTUP_PAUSED;
	gstreamer_run_pipeline(main_get_main_loop(), pipeline_str, startup);
	requested_position = suspended_pos;
	g_timeout_add_seconds(1, seek_to_time_cb, NULL);
}

void gstreamer_inform_playbin_used(gboolean status) {
	using_playbin = status;
}

gdouble gstreamer_get_volume() {
	if (!using_playbin) {
		printf("gst_play: Could not get audio volume because playbin is not used.\n");
		return 0;
	}
	gdouble volume;
	g_object_get(G_OBJECT(pipeline), "volume", &volume, NULL);
	printf("Volume = %lf\n", volume); fflush(stdout);
	return volume;
}

void gstreamer_set_volume(gdouble volume) {
	if (!using_playbin) {
		printf("gst_play: Could not set audio volume because playbin is not used.\n");
		return;
	}
	g_object_set(G_OBJECT(pipeline), "volume", volume, NULL);
}

gboolean gstreamer_have_software_color_balance() {
#if GST_CHECK_VERSION(1, 0, 0)
	return TRUE;
#else
	return FALSE;
#endif
}

void gstreamer_get_version(guint *major, guint *minor, guint *micro) {
	guint nano;
	gst_version(major, minor, micro, &nano);
}

void gstreamer_get_compiled_version(guint *major, guint *minor, guint *micro) {
	*major = GST_VERSION_MAJOR;
	*minor = GST_VERSION_MINOR;
	*micro = GST_VERSION_MICRO;
}
