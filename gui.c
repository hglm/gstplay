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
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms-compat.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>  // for GDK_WINDOW_XID
#endif
#include <glib.h>
#include "gstplay.h"

static GtkWidget *window, *video_window;
static guintptr video_window_handle = 0;
static gboolean full_screen = FALSE;
static GtkWidget *menu_bar;
GtkWidget *open_file_dialog;

void gui_init(int *argcp, char **argvp[]) {
	gtk_init(argcp, argvp);
}

void gui_get_version(guint *major, guint *minor, guint *micro) {
	*major = gtk_major_version;
	*minor = gtk_minor_version;
	*micro = gtk_micro_version;
}

static void video_window_realize_cb (GtkWidget * widget, gpointer data) {
#if GTK_CHECK_VERSION(2,18,0)
	// Tell Gtk+/Gdk to create a native window for this widget instead of
	// drawing onto the parent widget.
	// This is here just for pedagogical purposes, GDK_WINDOW_XID will call
	// it as well in newer Gtk versions
#if !GTK_CHECK_VERSION(3, 0, 0)
	if (!gdk_window_ensure_native(widget->window))
		g_error ("Couldn't create native window needed for GstVideoOverlay!");
#endif
#endif

#ifdef GDK_WINDOWING_X11
	gulong xid = GDK_WINDOW_XID(gtk_widget_get_window (video_window));
	video_window_handle = xid;
#endif
#ifdef GDK_WINDOWING_WIN32
	HWND wnd = GDK_WINDOW_HWND(gtk_widget_get_window (video_window));
	video_window_handle = (guintptr) wnd;
#endif
}

#if GTK_CHECK_VERSION(3, 0, 0)

static gboolean video_window_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
	if (gstreamer_no_video()) {
		cairo_reset_clip(cr);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		return TRUE;
	}
//	printf("gstplay: exposing video overlay.\n");
	gstreamer_expose_video_overlay();
	return TRUE;
}

#else

static gboolean video_window_expose_event_cb(GtkWidget *widget, GdkEventExpose *event,
gpointer data) {
	if (gstreamer_no_video()) {
		cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
		cairo_reset_clip(cr);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		return TRUE;
	}
//	printf("gstplay: exposing video overlay.\n");
	gstreamer_expose_video_overlay();
	return TRUE;
}

#endif

guintptr gui_get_video_window_handle() {
	return video_window_handle;
}


static void enable_full_screen() {
#if GTK_CHECK_VERSION(3, 0, 0)
	GdkRGBA color;
	gdk_rgba_parse(&color, "black");
	gtk_widget_override_background_color(window, GTK_STATE_NORMAL, &color);
#else
	GdkColor color;
	gdk_color_parse("black", &color);
	gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);
#endif
	gtk_widget_hide(menu_bar);
	gtk_window_fullscreen(GTK_WINDOW(window));
	full_screen = 1;
}

static void disable_full_screen() {
	GdkColor color;
	gdk_color_parse("white", &color);
	gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);
	gtk_widget_show(menu_bar);
	gtk_window_unfullscreen(GTK_WINDOW(window));
	full_screen = 0;
}

static void forward_stream_by(gint64 delta);
static void rewind_stream_by(gint64 delta);
static void menu_item_rewind_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void menu_item_seek_to_end_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void set_audio_volume(double volume);
static void increase_audio_volume(double volume);
static void decrease_audio_volume(double volume);
static void toggle_mute();

static gboolean key_press_cb(GtkWidget * widget, GdkEventKey * event, gpointer data) {
        switch (event->keyval) {
        case GDK_q:
        case GDK_Q:
                g_main_loop_quit((GMainLoop *)data);
                break;
	case GDK_f:
	case GDK_F:
		if (!full_screen)
			enable_full_screen();
		else
			disable_full_screen();
		break;
	case GDK_n:
	case GDK_N:
		forward_stream_by((gint64)60 * 1000000000);
		break;
	case GDK_p:
	case GDK_P:
		rewind_stream_by((gint64)60 * 1000000000);
		break;
	case GDK_period:
	case GDK_greater:
		forward_stream_by((gint64)10 * 1000000000);
		break;
	case GDK_comma:
	case GDK_less:
		rewind_stream_by((gint64)10 * 1000000000);
		break;
	case GDK_Home:
		menu_item_rewind_activate_cb(NULL, NULL);
		break;
	case GDK_End:
		menu_item_seek_to_end_activate_cb(NULL, NULL);
		break;
	case GDK_m:
	case GDK_M:
		toggle_mute();
		break;
	case GDK_plus:
	case GDK_equal:
	case GDK_KP_Add:
		increase_audio_volume(0.05);
		break;
	case GDK_minus:
	case GDK_underscore:
	case GDK_KP_Subtract:
		decrease_audio_volume(0.05);
		break;
        }
        return TRUE;
}

static void destroy_cb(GtkWidget *widget, gpointer data) {
	GMainLoop *loop = (GMainLoop *)data;
	g_main_loop_quit(loop);
}

void gui_show_error_message(gchar *message, gchar *message_detail) {
	char *s = malloc(strlen(message) + strlen(message_detail) + 128);
	sprintf(s, "%s\n\nDetailed error message:\n%s", message, message_detail);
	GtkWidget *message_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_ERROR,
		GTK_BUTTONS_OK,
		s);
	gtk_dialog_run(GTK_DIALOG(message_dialog));
	gtk_widget_destroy(message_dialog);
	free(s);
}

static void menu_item_properties_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	char message[1024];
	int width = 0;
	int height = 0;
	if (gstreamer_no_pipeline()) {
		sprintf(message, "No media loaded.\n");
		goto skip_stream_info;
	}
	const char *format;
	int framerate_numerator;
	int framerate_denom;
	int pixel_aspect_ratio_num;
	int pixel_aspect_ratio_denom;
	gstreamer_get_video_info(&format, &width, &height, &framerate_numerator,
		&framerate_denom, &pixel_aspect_ratio_num, &pixel_aspect_ratio_denom);
	sprintf(message, "Media format: ");
	if (format == NULL)
		sprintf(message + strlen(message), "Unknown\n");
	else
		sprintf(message + strlen(message), "%s\n", format);
	sprintf(message + strlen(message), "Dimensions: ");
	if (width == 0 || height == 0)
		sprintf(message + strlen(message), "Unknown\n");
	else
		sprintf(message + strlen(message), "%d x %d pixels\n", width, height);
	sprintf(message + strlen(message), "Pixel aspect ratio: ");
	if (pixel_aspect_ratio_num == 0 || pixel_aspect_ratio_denom == 0)
		sprintf(message + strlen(message), "Unknown\n");
	else
		sprintf(message + strlen(message), "%d : %d\n", pixel_aspect_ratio_num,
			pixel_aspect_ratio_denom);

	sprintf(message + strlen(message), "Frame-rate: ");
	if (framerate_numerator == 0 || framerate_denom == 0)
		sprintf(message + strlen(message), "Unknown\n");
	else
		sprintf(message + strlen(message), "%.2f frames per second (%d / %d)\n",
			(float)framerate_numerator / framerate_denom,
			framerate_numerator, framerate_denom);
	sprintf(message + strlen(message), "Duration: %s", gstreamer_get_duration_str());
	sprintf(message + strlen(message), "\nPipeline:\n");
	sprintf(message + strlen(message), "%s\n", gstreamer_get_pipeline_description());

skip_stream_info: ;
#if GTK_CHECK_VERSION(3, 0, 0)
	int w = gtk_widget_get_allocated_width(video_window);
	int h = gtk_widget_get_allocated_height(video_window);
#else
	int w = video_window->allocation.width;
	int h = video_window->allocation.height;
#endif
	sprintf(message + strlen(message), "\nWindow size: %d x %d", w, h);
	if (width != 0 && height != 0) {
		sprintf(message + strlen(message), " (zoom %.2f x %.2f)", (float)w / width,
			(float)h / height);
	}

	GtkWidget *properties_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
		GTK_BUTTONS_OK, message);
	gtk_dialog_run(GTK_DIALOG(properties_dialog));
	gtk_widget_hide(GTK_WIDGET(properties_dialog));
	gtk_widget_destroy(GTK_WIDGET(properties_dialog));
}

static void menu_item_open_file_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	int r = gtk_dialog_run(GTK_DIALOG(open_file_dialog));
	if (r == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(
			open_file_dialog));
		if (!gstreamer_no_pipeline()) {
			gstreamer_pause();
			gstreamer_destroy_pipeline();
		}
		char *uri;
		char *video_title_filename;
		main_create_uri(filename, &uri, &video_title_filename);
		g_free(filename);
		const char *pipeline_str = main_create_pipeline(uri, video_title_filename);
		gtk_widget_hide(open_file_dialog);
		if (!gstreamer_run_pipeline(main_get_main_loop(), pipeline_str,
		config_get_startup_preference())) {
			gui_show_error_message("Pipeline parse problem.", "");
		}
		return;
	}
	gtk_widget_hide(open_file_dialog);
}

static gint64 requested_position;

gboolean seek_to_time_cb(gpointer data) {
	gstreamer_seek_to_time(requested_position);
	return FALSE;
}

static GtkWidget **video_output_radio_button;
static GtkWidget **audio_output_radio_button;
static GtkWidget *video_only_check_button;
static GtkWidget *software_volume_check_button;
static GtkWidget *software_color_balance_check_button;

static void menu_item_preferences_activate_cb(GtkMenuItem *menu_item, GtkWidget *dialog) {
	gtk_widget_show_all(dialog);
	int r = gtk_dialog_run(GTK_DIALOG(dialog));
	if (r == GTK_RESPONSE_APPLY || r == GTK_RESPONSE_ACCEPT) {
		gstreamer_suspend_pipeline();
		/* Process the settings changes. */
		int video_sink_index;
		int n = config_get_number_of_video_sinks();
		for (int i = 0; i < n; i++)
			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			video_output_radio_button[i])))
				video_sink_index = i;
		config_set_current_video_sink_by_index(video_sink_index);
		int audio_sink_index;
		n = config_get_number_of_audio_sinks();
		for (int i = 0; i < n; i++)
			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			audio_output_radio_button[i])))
				audio_sink_index = i;
		config_set_current_audio_sink_by_index(audio_sink_index);
		config_set_video_only(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			video_only_check_button)));
		config_set_software_volume(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			software_volume_check_button)));
		if (gstreamer_have_software_color_balance())
			config_set_software_color_balance(gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(software_color_balance_check_button)));
		gstreamer_restart_pipeline();
	}
	gtk_widget_hide(GTK_WIDGET(dialog));
}

static void menu_item_close_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (!gstreamer_no_pipeline()) {
		gstreamer_pause();
		gstreamer_destroy_pipeline();
	}
}

static void menu_item_quit_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (!gstreamer_no_pipeline())
		gstreamer_destroy_pipeline();
	g_main_loop_quit((GMainLoop *)data);
	exit(0);
}

static void menu_item_play_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_play();
}

static void menu_item_pause_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_pause();
}

static void menu_item_rewind_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_seek_to_time(0);
}

static void menu_item_seek_to_end_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	if (gstreamer_end_of_stream())
		return;
	gint64 duration = gstreamer_get_duration();
	if (duration > 0) {
		// Actually seek to end of stream - 0.1s.
		if (duration - 100000000 >= 0)
			gstreamer_seek_to_time(duration - 100000000);
	}
}

static void forward_stream_by(gint64 delta) {
	if (gstreamer_no_pipeline())
		return;
	gboolean error;
	gint64 pos = gstreamer_get_position(&error);
	if (error)
		return;
	pos += delta;
	gint64 duration = gstreamer_get_duration();
	if (duration != 0 && pos > duration) {
		// When the end of stream is eached, actually seek to end of stream - 0.1s.
		pos = duration - 100000000;
		if (pos < 0)
			pos = 0;
	}
	gstreamer_seek_to_time(pos);
}

static void rewind_stream_by(gint64 delta) {
	if (gstreamer_no_pipeline())
		return;
	gboolean error;
	gint64 pos = gstreamer_get_position(&error);
	if (error)
		return;
	pos -= delta;
	if (pos < 0)
		pos = 0;
	gstreamer_seek_to_time(pos);
}

static void menu_item_plus_1min_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	forward_stream_by((gint64)60 * 1000000000);
}

static void menu_item_min_1min_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	rewind_stream_by((gint64)60 * 1000000000);
}

static void menu_item_plus_10sec_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	forward_stream_by((gint64)10 * 1000000000);
}

static void menu_item_min_10sec_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	rewind_stream_by((gint64)10 * 1000000000);
}

static double last_non_zero_audio_volume = 1.0;

static void set_audio_volume(double volume) {
	if (volume != 0)
		last_non_zero_audio_volume = volume;
	gstreamer_set_volume(volume);
}

static void increase_audio_volume(double delta) {
	if (gstreamer_no_pipeline())
		return;
	gdouble vol = gstreamer_get_volume();
	vol += delta;
	if (vol > 1.0)
		vol = 1.0;
	set_audio_volume(vol);
}

static void decrease_audio_volume(double delta) {
	if (gstreamer_no_pipeline())
		return;
	gdouble vol = gstreamer_get_volume();
	vol -= delta;
	if (vol < 0)
		vol = 0;
	set_audio_volume(vol);
}

static void check_menu_item_mute_activate_cb(GtkCheckMenuItem *check_menu_item,
gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(check_menu_item)))
		set_audio_volume(0);
	else
		set_audio_volume(last_non_zero_audio_volume);
}

GtkWidget *check_menu_item_mute;

static void toggle_mute() {
	// Trigger activate call-back.
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(check_menu_item_mute)))
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check_menu_item_mute),
			FALSE);
	else
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check_menu_item_mute),
			TRUE);
}

static void resize_video_window(int width, int height) {
	// Get the height of the menu bar.
#if GTK_CHECK_VERSION(3, 0, 0)
	int h = gtk_widget_get_allocated_height(menu_bar);
#else
	int h = menu_bar->allocation.height;
#endif
	// Resize to a height of the menu bar height + the video window height.
	gtk_widget_set_size_request(video_window, width, height);
	gtk_window_resize(GTK_WINDOW(window), width, height + h);
}

static void menu_item_one_to_one_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	int width, height;
	gstreamer_get_video_dimensions(&width, &height);
	if (width != 0 && height != 0)
		resize_video_window(width, height);
}

static void menu_item_zoom_x2_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	int width, height;
	gstreamer_get_video_dimensions(&width, &height);
	if (width != 0 && height != 0)
		resize_video_window(width * 2, height * 2);
}


static void menu_item_zoom_x05_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	int width, height;
	gstreamer_get_video_dimensions(&width, &height);
	if (width != 0 && height != 0)
		resize_video_window(width / 2, height / 2);
}

static void menu_item_full_screen_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	enable_full_screen();
}

static void menu_item_increase_volume_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	increase_audio_volume(0.05);
}

static void menu_item_decrease_volume_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	decrease_audio_volume(0.05);
}

#if GTK_CHECK_VERSION(3, 0, 0)

GtkWidget *color_slider_label[4];
GtkWidget *color_slider[4];
static gboolean color_controls_active = FALSE;

void color_balance_pipeline_destroyed_cb(gpointer data, GtkDialog *dialog) {
	gtk_widget_hide(GTK_WIDGET(dialog));
	color_controls_active = FALSE;
}

static void menu_item_color_controls_activate_cb(GtkMenuItem *menu_item, GtkDialog *dialog) {
	if (color_controls_active)
		return;
	if (!config_software_color_balance()) {
		GtkWidget *message_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			"Software color balance disabled in settings.");
		gtk_dialog_run(GTK_DIALOG(message_dialog));
		gtk_widget_destroy(message_dialog);
		return;
	}
	int r = 0;
	if (!gstreamer_no_pipeline())
		r = gstreamer_prepare_color_balance();
	if (r == 0) {
		GtkWidget *message_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			"Software color balance not available with current stream\n"
			"or newer version of GStreamer required.\n"
			);
		gtk_dialog_run(GTK_DIALOG(message_dialog));
		gtk_widget_destroy(message_dialog);
		return;
	}
	for (int i = 0; i < 4; i++) {
		gboolean status;
		if (r & (1 << i))
			status = TRUE;
		else
			status = FALSE;
		if (status) {
			gdouble value;
			// Get default value from config
			value = config_get_global_color_balance_default(i);
			// Read default from gstreamer (disabled).
//			value = gstreamer_get_color_balance(i));
			gtk_range_set_value(GTK_RANGE(color_slider[i]), value);
		}
		gtk_widget_set_visible(color_slider[i], status);
		gtk_widget_set_visible(color_slider_label[i], status);
	}
	gtk_widget_show_all(GTK_WIDGET(dialog));
	color_controls_active = TRUE;

	gstreamer_add_pipeline_destroyed_cb(color_balance_pipeline_destroyed_cb, dialog);
}

static void color_slider_value_changed_cb(GtkScale *scale, GtkLabel *label) {
	const gchar *str = gtk_label_get_text(label);
	switch (str[0]) {
	case 'B':
		gstreamer_set_color_balance(CHANNEL_BRIGHTNESS, gtk_range_get_value(
			GTK_RANGE(scale)));
		break;
	case 'C':
		gstreamer_set_color_balance(CHANNEL_CONTRAST, gtk_range_get_value(
			GTK_RANGE(scale)));
		break;
	case 'H':
		gstreamer_set_color_balance(CHANNEL_HUE, gtk_range_get_value(
			GTK_RANGE(scale)));
		break;
	case 'S':
		gstreamer_set_color_balance(CHANNEL_SATURATION, gtk_range_get_value(
			GTK_RANGE(scale)));
		break;
	}
}

static void color_balance_set_defaults_button_clicked_cb(GtkButton *button, gpointer data) {
	for (int i = 0; i < 4; i++) {
		gtk_range_set_value(GTK_RANGE(color_slider[i]), 50.0);
	}
}

static void color_balance_set_global_defaults_button_clicked_cb(GtkButton *button,
gpointer data) {
	for (int i = 0; i < 4; i++)
		if (gtk_widget_is_visible(color_slider[i]))
			config_set_global_color_balance_default(i,
				gtk_range_get_value(GTK_RANGE(color_slider[i])));
}

static void color_balance_set_uri_defaults_button_clicked_cb(GtkButton *button,
gpointer data) {
	for (int i = 0; i < 4; i++)
		if (gtk_widget_is_visible(color_slider[i]))
			config_set_uri_color_balance_default(i,
				gtk_range_get_value(GTK_RANGE(color_slider[i])));
}

void color_balance_dialog_response_cb(GtkDialog *dialog, gpointer data) {
	gtk_widget_hide(GTK_WIDGET(dialog));
	color_controls_active = FALSE;
}

#endif

static void menu_item_about_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	guint gst_major, gst_minor, gst_micro;
	gstreamer_get_version(&gst_major, &gst_minor, &gst_micro);
	guint gst_major_compiled, gst_minor_compiled, gst_micro_compiled;
	gstreamer_get_compiled_version(&gst_major_compiled, &gst_minor_compiled,
		&gst_micro_compiled);
	guint gtk_major, gtk_minor, gtk_micro;
	gui_get_version(&gtk_major, &gtk_minor, &gtk_micro);

	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_INFO,
		GTK_BUTTONS_OK,
		"gstplay -- a simple but fast media player\n\n"
		"Copyright 2013 Harm Hanemaaijer\n\n"
		"Compiled with GStreamer %d.%d.%d and GTK+ %d.%d.%d\n\n"
		"Using Gstreamer %d.%d.%d and GTK+ %d.%d.%d\n",
		gst_major_compiled, gst_minor_compiled, gst_micro_compiled,
		GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION,
		gst_major, gst_minor, gst_micro,
		gtk_major, gtk_minor, gtk_micro);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_hide(dialog);
	gtk_widget_destroy(dialog);
}

static GtkWidget *create_preferences_dialog() {
	GtkWidget *dialog = gtk_dialog_new_with_buttons(
		"Preferences",
		GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_APPLY, GTK_RESPONSE_APPLY,
		GTK_STOCK_SAVE,	GTK_RESPONSE_ACCEPT,
		NULL);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	// Video and audio output sinks.
	GtkWidget *label = gtk_label_new("Video output:");
	int nu_video_sinks = config_get_number_of_video_sinks();
	video_output_radio_button = malloc(sizeof(GtkRadioButton *) * nu_video_sinks);
	video_output_radio_button[0] = gtk_radio_button_new_with_label(NULL,
		config_get_video_sink_by_index(0));
	for (int i = 1; i < nu_video_sinks; i++)
		video_output_radio_button[i] = gtk_radio_button_new_with_label(
			gtk_radio_button_get_group(GTK_RADIO_BUTTON(
			video_output_radio_button[0])), config_get_video_sink_by_index(i));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(video_output_radio_button[
		config_get_current_video_sink_index()]), TRUE);
	GtkWidget *label2 = gtk_label_new("Audio output:");
	int nu_audio_sinks = config_get_number_of_audio_sinks();
	audio_output_radio_button = malloc(sizeof(GtkRadioButton *) * nu_audio_sinks);
	audio_output_radio_button[0] = gtk_radio_button_new_with_label(NULL,
		config_get_audio_sink_by_index(0));
	for (int i = 1; i < nu_audio_sinks; i++)
		audio_output_radio_button[i] = gtk_radio_button_new_with_label(
			gtk_radio_button_get_group(GTK_RADIO_BUTTON(
			audio_output_radio_button[0])), config_get_audio_sink_by_index(i));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audio_output_radio_button[
		config_get_current_audio_sink_index()]), TRUE);
#if GTK_CHECK_VERSION(3, 0, 0)
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
#else
	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
#endif
	gtk_container_add(GTK_CONTAINER(content), hbox);
#if GTK_CHECK_VERSION(3, 0, 0)
	GtkWidget *vbox1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
	GtkWidget *vbox1 = gtk_vbox_new(FALSE, 0);
#endif
	gtk_container_add(GTK_CONTAINER(vbox1), label);
	for (int i = 0; i < nu_video_sinks; i++)
		gtk_container_add(GTK_CONTAINER(vbox1), video_output_radio_button[i]);
	gtk_container_add(GTK_CONTAINER(hbox), vbox1);
#if GTK_CHECK_VERSION(3, 0, 0)
	GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
	GtkWidget *vbox2 = gtk_vbox_new(FALSE, 0);
#endif
	gtk_container_add(GTK_CONTAINER(vbox2), label2);
	for (int i = 0; i < nu_audio_sinks; i++)
		gtk_container_add(GTK_CONTAINER(vbox2), audio_output_radio_button[i]);
	gtk_container_add(GTK_CONTAINER(hbox), vbox2);

	// Other options.
	video_only_check_button = gtk_check_button_new_with_label(
		"Video output only (completely disable audio)");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(video_only_check_button),
		config_video_only());
	software_volume_check_button = gtk_check_button_new_with_label(
		"Software audio volume control");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(software_volume_check_button),
		config_software_volume());
	software_color_balance_check_button = gtk_check_button_new_with_label(
		"Software color balance control");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(software_color_balance_check_button),
		config_software_color_balance());
	GtkWidget *spacing_label = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(content), spacing_label);
	gtk_container_add(GTK_CONTAINER(content), video_only_check_button);
	gtk_container_add(GTK_CONTAINER(content), software_volume_check_button);
	if (gstreamer_have_software_color_balance())
		gtk_container_add(GTK_CONTAINER(content), software_color_balance_check_button);
	return dialog;
}

#if GTK_CHECK_VERSION(3, 0, 0)

static GtkWidget *create_color_balance_dialog() {
	GtkWidget *dialog = gtk_dialog_new_with_buttons(
		"Color balance",
		GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 480, - 1);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	color_slider_label[0] = gtk_label_new("Brightness");
	color_slider_label[1] = gtk_label_new("Contrast");
	color_slider_label[2] = gtk_label_new("Hue");
	color_slider_label[3] = gtk_label_new("Saturation");
	GtkWidget *grid = gtk_grid_new();
	for (int i = 0; i < 4; i++) {
		color_slider[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0,
			100.0, 10.0);
		gtk_scale_set_value_pos(GTK_SCALE(color_slider[i]), GTK_POS_RIGHT);
		gtk_scale_set_digits(GTK_SCALE(color_slider[i]), 0);
		gtk_range_set_value(GTK_RANGE(color_slider[i]), 50.0);
		gtk_widget_set_hexpand(color_slider[i], TRUE);
		g_signal_connect(G_OBJECT(color_slider[i]), "value-changed", G_CALLBACK(
			color_slider_value_changed_cb), color_slider_label[i]);
		gtk_grid_attach(GTK_GRID(grid), color_slider_label[i], 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), color_slider[i], 1, i, 1, 1);
	}
	gtk_container_add(GTK_CONTAINER(content), grid);
	GtkWidget *color_balance_set_global_defaults_button = gtk_button_new_with_label(
		"Set as global default color settings");
	g_signal_connect(G_OBJECT(color_balance_set_global_defaults_button), "clicked",
		G_CALLBACK(color_balance_set_global_defaults_button_clicked_cb), NULL);
	GtkWidget *color_balance_set_uri_defaults_button = gtk_button_new_with_label(
		"Set as default color settings for this uri");
	g_signal_connect(G_OBJECT(color_balance_set_uri_defaults_button), "clicked",
		G_CALLBACK(color_balance_set_uri_defaults_button_clicked_cb), NULL);
	gtk_container_add(GTK_CONTAINER(content), color_balance_set_global_defaults_button);
	gtk_container_add(GTK_CONTAINER(content), color_balance_set_uri_defaults_button);

	GtkWidget *action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
	GtkWidget *set_defaults_button = gtk_button_new_with_label("Set defaults");
	g_signal_connect(G_OBJECT(set_defaults_button), "clicked", G_CALLBACK(
		color_balance_set_defaults_button_clicked_cb), NULL);
	gtk_container_add(GTK_CONTAINER(action_area), set_defaults_button);
	g_signal_connect(dialog, "response", G_CALLBACK(color_balance_dialog_response_cb),
		dialog);
	return dialog;
}

#endif

static void create_menus(GMainLoop *loop) {
	GtkWidget *preferences_dialog = create_preferences_dialog();

#if GTK_CHECK_VERSION(3, 0, 0)
	// Create the color balance dialog
	GtkWidget *color_balance_dialog = create_color_balance_dialog();
#endif

	// Create the menu bar.
	menu_bar = gtk_menu_bar_new();

	// Create the menu items.
	GtkWidget *menu_item_open_file = gtk_menu_item_new_with_label("Open File");
	g_signal_connect(G_OBJECT(menu_item_open_file), "activate",
		G_CALLBACK(menu_item_open_file_activate_cb), NULL);
	GtkWidget *menu_item_properties = gtk_menu_item_new_with_label("Media Properties");
	g_signal_connect(G_OBJECT(menu_item_properties), "activate",
		G_CALLBACK(menu_item_properties_activate_cb), NULL);
	GtkWidget *menu_item_preferences = gtk_menu_item_new_with_label("Preferences");
	g_signal_connect(G_OBJECT(menu_item_preferences), "activate",
		G_CALLBACK(menu_item_preferences_activate_cb), preferences_dialog);
	GtkWidget *menu_item_close = gtk_menu_item_new_with_label("Close stream");
	g_signal_connect(G_OBJECT(menu_item_close), "activate",
		G_CALLBACK(menu_item_close_activate_cb), loop);
	GtkWidget *menu_item_quit = gtk_menu_item_new_with_label("Quit");
	g_signal_connect(G_OBJECT(menu_item_quit), "activate",
		G_CALLBACK(menu_item_quit_activate_cb), loop);
	GtkWidget *file_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_item_open_file);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_item_properties);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_item_preferences);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_item_close);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_item_quit);
	// Create the File menu.
	GtkWidget *file_item = gtk_menu_item_new_with_label("File");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_item);

	GtkWidget *menu_item_play = gtk_menu_item_new_with_label("Play");
	g_signal_connect(G_OBJECT(menu_item_play), "activate",
		G_CALLBACK(menu_item_play_activate_cb), NULL);
	GtkWidget *menu_item_pause = gtk_menu_item_new_with_label("Pause");
	g_signal_connect(G_OBJECT(menu_item_pause), "activate",
		G_CALLBACK(menu_item_pause_activate_cb), NULL);
	GtkWidget *menu_item_rewind = gtk_menu_item_new_with_label("Rewind (Home)");
	g_signal_connect(G_OBJECT(menu_item_rewind), "activate",
		G_CALLBACK(menu_item_rewind_activate_cb), loop);
	GtkWidget *menu_item_seek_to_end = gtk_menu_item_new_with_label("Seek to End (End)");
	g_signal_connect(G_OBJECT(menu_item_seek_to_end), "activate",
		G_CALLBACK(menu_item_seek_to_end_activate_cb), loop);
	GtkWidget *menu_item_plus_1min = gtk_menu_item_new_with_label("+ 1 min (N)");
	g_signal_connect(G_OBJECT(menu_item_plus_1min), "activate",
		G_CALLBACK(menu_item_plus_1min_activate_cb), loop);
	GtkWidget *menu_item_min_1min = gtk_menu_item_new_with_label("- 1 min (P)");
	g_signal_connect(G_OBJECT(menu_item_min_1min), "activate",
		G_CALLBACK(menu_item_min_1min_activate_cb), loop);
	GtkWidget *menu_item_plus_10sec = gtk_menu_item_new_with_label("+ 10 sec (>)");
	g_signal_connect(G_OBJECT(menu_item_plus_10sec), "activate",
		G_CALLBACK(menu_item_plus_1min_activate_cb), loop);
	GtkWidget *menu_item_min_10sec = gtk_menu_item_new_with_label("- 10 sec (<)");
	g_signal_connect(G_OBJECT(menu_item_min_10sec), "activate",
		G_CALLBACK(menu_item_min_10sec_activate_cb), loop);
	GtkWidget *menu_item_increase_volume = gtk_menu_item_new_with_label(
		"Increase volume (+)");
	g_signal_connect(G_OBJECT(menu_item_increase_volume), "activate",
		G_CALLBACK(menu_item_increase_volume_activate_cb), loop);
	GtkWidget *menu_item_decrease_volume = gtk_menu_item_new_with_label(
		"Decrease volume (-)");
	g_signal_connect(G_OBJECT(menu_item_decrease_volume), "activate",
		G_CALLBACK(menu_item_decrease_volume_activate_cb), loop);
	check_menu_item_mute = gtk_check_menu_item_new_with_label("Mute (M)");
	g_signal_connect(G_OBJECT(check_menu_item_mute), "activate",
		G_CALLBACK(check_menu_item_mute_activate_cb), loop);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check_menu_item_mute), FALSE);
	GtkWidget *control_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_play);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_pause);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_rewind);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_seek_to_end);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_plus_1min);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_min_1min);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_plus_10sec);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_min_10sec);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_increase_volume);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), menu_item_decrease_volume);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), check_menu_item_mute);
	// Create the Control menu.
	GtkWidget *control_item = gtk_menu_item_new_with_label("Control");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(control_item), control_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), control_item);

	GtkWidget *menu_item_one_to_one = gtk_menu_item_new_with_label("1:1 window size");
	g_signal_connect(G_OBJECT(menu_item_one_to_one), "activate",
		G_CALLBACK(menu_item_one_to_one_activate_cb), NULL);
	GtkWidget *menu_item_zoom_x2 = gtk_menu_item_new_with_label("Zoom x2");
	g_signal_connect(G_OBJECT(menu_item_zoom_x2), "activate",
		G_CALLBACK(menu_item_zoom_x2_activate_cb), NULL);
	GtkWidget *menu_item_zoom_x05 = gtk_menu_item_new_with_label("Zoom x0.5");
	g_signal_connect(G_OBJECT(menu_item_zoom_x05), "activate",
		G_CALLBACK(menu_item_zoom_x05_activate_cb), NULL);
	GtkWidget *menu_item_full_screen = gtk_menu_item_new_with_label("Full-screen (F)");
	g_signal_connect(G_OBJECT(menu_item_full_screen), "activate",
		G_CALLBACK(menu_item_full_screen_activate_cb), NULL);
	GtkWidget *view_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), menu_item_one_to_one);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), menu_item_zoom_x2);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), menu_item_zoom_x05);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), menu_item_full_screen);
	// Create the view menu.
	GtkWidget *view_item = gtk_menu_item_new_with_label("View");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_item), view_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), view_item);

#if GTK_CHECK_VERSION(3, 0, 0)
	GtkWidget *menu_item_color_controls = gtk_menu_item_new_with_label("Open color controls");
	g_signal_connect(G_OBJECT(menu_item_color_controls), "activate",
		G_CALLBACK(menu_item_color_controls_activate_cb), color_balance_dialog);
	GtkWidget *color_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(color_menu), menu_item_color_controls);
	// Create the color menu.
	GtkWidget *color_item = gtk_menu_item_new_with_label("Color");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(color_item), color_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), color_item);
#endif

	GtkWidget *menu_item_about = gtk_menu_item_new_with_label("About");
	g_signal_connect(G_OBJECT(menu_item_about), "activate",
		G_CALLBACK(menu_item_about_activate_cb), NULL);
	GtkWidget *help_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), menu_item_about);
	// Create the help menu.
	GtkWidget *help_item = gtk_menu_item_new_with_label("Help");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), help_item);
}

void gui_setup_window(GMainLoop *loop, const char *video_filename, int width, int height,
gboolean full_screen_requested) {

	/* Set up the window. */
	char *title = malloc(strlen(video_filename) + 32);
	sprintf(title, "gstplay %s", video_filename);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), title);
#if !GTK_CHECK_VERSION(3, 0, 0)
	// For GTK+ 3, disabling double buffering causes corruption in the menu bar,
	// so leave it enabled.
	gtk_widget_set_double_buffered(window, FALSE);
#endif
	g_signal_connect(G_OBJECT(window), "key-press-event",
		G_CALLBACK(key_press_cb), loop);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy_cb), loop);

	create_menus(loop);

	video_window = gtk_drawing_area_new();
	g_signal_connect(video_window, "realize", G_CALLBACK(video_window_realize_cb), NULL);
#if GTK_CHECK_VERSION(3, 0, 0)
	g_signal_connect(G_OBJECT(video_window), "draw",
		G_CALLBACK(video_window_draw_cb), NULL);
#else
	g_signal_connect(G_OBJECT(video_window), "expose-event",
		G_CALLBACK(video_window_expose_event_cb), NULL);
#endif
	gtk_widget_set_double_buffered(video_window, FALSE);

	// Create the menu vbox for holding the menu and the rest of the application.
#if GTK_CHECK_VERSION(3, 0, 0)
	GtkWidget *menu_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
	GtkWidget *menu_vbox = gtk_vbox_new(FALSE, 0);
#endif
	// Add the menu and the video window to the vbox.
	gtk_box_pack_start(GTK_BOX(menu_vbox), menu_bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(menu_vbox), video_window, TRUE, TRUE, 0);
	// Add the vbox to the window.
	gtk_container_add(GTK_CONTAINER(window), menu_vbox);

	// Create the open file load dialog.
	open_file_dialog = gtk_file_chooser_dialog_new("Select a video file to open.",
		GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
		NULL);

	/* Set size request and realize window. */
	gtk_widget_set_size_request(video_window, width, height);
#if GTK_CHECK_VERSION(3, 0, 0)
	GdkRGBA color;
	gdk_rgba_parse(&color, "black");
	gtk_widget_override_background_color(video_window, GTK_STATE_NORMAL, &color);
#else
	GdkColor color;
	gdk_color_parse("black", &color);
	gtk_widget_modify_bg(video_window, GTK_STATE_NORMAL, &color);
#endif
	gtk_widget_show_all(window);
	gtk_widget_realize(window);
	if (full_screen_requested) {
		gtk_window_fullscreen(GTK_WINDOW(window));
	}
	full_screen = full_screen_requested;
}


