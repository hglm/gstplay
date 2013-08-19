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
static GtkWidget *menu_bar, *status_bar, *status_bar_duration_label;
static GtkWidget *position_slider;
GtkWidget *open_file_dialog;
guint update_status_bar_cb_id;

static void gui_reset_status_bar();
static gboolean gui_update_status_bar_cb(gpointer data);
static void gui_status_bar_pipeline_destroyed_cb(gpointer data, GtkWidget *status_bar);

gboolean gui_init(int *argcp, char **argvp[]) {
	return gtk_init_check(argcp, argvp);
}

void gui_get_version(guint *major, guint *minor, guint *micro) {
	*major = gtk_major_version;
	*minor = gtk_minor_version;
	*micro = gtk_micro_version;
}

static void video_widget_realize_cb (GtkWidget * widget, gpointer data) {
#if GTK_CHECK_VERSION(2,18,0)
	// Tell Gtk+/Gdk to create a native window for this widget instead of
	// drawing onto the parent widget.
	// This is here just for pedagogical purposes, GDK_WINDOW_XID will call
	// it as well in newer Gtk versions
#if !GTK_CHECK_VERSION(3, 0, 0)
	if (!gdk_window_ensure_native(gtk_widget_get_window(widget)))
		g_error("Couldn't create native window needed for GstVideoOverlay!");
#endif
#endif

#ifdef GDK_WINDOWING_X11
	gulong xid = GDK_WINDOW_XID(gtk_widget_get_window(widget));
	video_window_handle = xid;
#endif
#ifdef GDK_WINDOWING_WIN32
	HWND wnd = GDK_WINDOW_HWND(gtk_widget_get_window(widget));
	video_window_handle = (guintptr) wnd;
#endif

}

void gui_get_render_rectangle(int *x, int *y, int *w, int *h) {
#if 0
/* Video window is not a seperate subwindow. */
#if GTK_CHECK_VERSION(3, 0, 0)
	int window_w = gtk_widget_get_allocated_width(window);
	int window_h = gtk_widget_get_allocated_height(window);
	// Get the height of the menu bar.
	int menu_h = gtk_widget_get_allocated_height(menu_bar);
#else
	int window_w = window->allocation.width;
	int window_h = window->allocation.height;
	int menu_h = menu_bar->allocation.height;
#endif
	if (full_screen)
		menu_h = 0;
	*x = 0;
	*y = menu_h;
	*w = window_w;
	*h = window_h - menu_h;
#else
/* Video window is a seperate subwindow. */
#if GTK_CHECK_VERSION(3, 0, 0)
	int video_window_w = gtk_widget_get_allocated_width(video_window);
	int video_window_h = gtk_widget_get_allocated_height(video_window);
#else
	int video_window_w = video_window->allocation.width;
	int video_window_h = video_window->allocation.height;
#endif
	*x = 0;
	*y = 0;
	*w = video_window_w;
	*h = video_window_h;
#endif
}

#if GTK_CHECK_VERSION(3, 0, 0)

static gboolean video_window_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
	if (gstreamer_no_video()) {
		cairo_reset_clip(cr);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		return FALSE;
	}
//	printf("gstplay: exposing video overlay.\n");
	int x, y, w, h;
	gui_get_render_rectangle(&x, &y, &w, &h);
	gstreamer_expose_video_overlay(x, y, w, h);
	return FALSE;
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
		return FALSE;
	}
//	printf("gstplay: exposing video overlay.\n");
	int x, y, w, h;
	gui_get_render_rectangle(&x, &y, &w, &h);
	gstreamer_expose_video_overlay(x, y, w, h);
	return FALSE;
}

#endif

guintptr gui_get_video_window_handle() {
	return video_window_handle;
}

void gui_status_bar_pipeline_destroyed_cb(gpointer data, GtkWidget *widget) {
	/* Remove the periodic time-out to update the position slider. */
	g_source_remove(update_status_bar_cb_id);
}

void gui_play_start_cb() {
	gui_reset_status_bar();
	/* Add a periodic time-out to update the position slider. */
	update_status_bar_cb_id = g_timeout_add_seconds(1, gui_update_status_bar_cb, NULL);
	gstreamer_add_pipeline_destroyed_cb(gui_status_bar_pipeline_destroyed_cb, status_bar);
}

void gui_set_window_title(const char *title) {
	gtk_window_set_title(GTK_WINDOW(window), title);
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
	gtk_widget_hide(status_bar);
	full_screen = 1;
	gtk_window_fullscreen(GTK_WINDOW(window));
}

static void disable_full_screen() {
	GdkColor color;
	gdk_color_parse("white", &color);
	gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);
	gtk_widget_show(menu_bar);
	gtk_widget_show(status_bar);
	full_screen = 0;
	gtk_window_unfullscreen(GTK_WINDOW(window));
}

static void forward_stream_by(gint64 delta);
static void rewind_stream_by(gint64 delta);
static void menu_item_rewind_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void menu_item_seek_to_end_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void set_audio_volume(double volume);
static void increase_audio_volume(double volume);
static void decrease_audio_volume(double volume);
static void toggle_mute();
static void menu_item_play_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void menu_item_pause_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void menu_item_next_frame_activate_cb(GtkMenuItem *menu_item, gpointer data);
static void menu_item_previous_frame_activate_cb(GtkMenuItem *menu_item, gpointer data);

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
        case GDK_Return:
	case GDK_KP_Enter:
		menu_item_play_activate_cb(NULL, NULL);
		break;
	case GDK_p:
	case GDK_P:
		menu_item_pause_activate_cb(NULL, NULL);
		break;
	case GDK_bracketright:
	case GDK_braceright:
		forward_stream_by((gint64)60 * 1000000000);
		break;
	case GDK_bracketleft:
	case GDK_braceleft:
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
	case GDK_Right:
	case GDK_KP_Right:
		menu_item_next_frame_activate_cb(NULL, NULL);
		break;
	case GDK_Left:
	case GDK_KP_Left:
		menu_item_previous_frame_activate_cb(NULL, NULL);
		break;
        }
        return TRUE;
}

static void destroy_cb(GtkWidget *widget, gpointer data) {
	GMainLoop *loop = (GMainLoop *)data;
	g_main_loop_quit(loop);
}

void gui_show_error_message(const gchar *message, const gchar *message_detail) {
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
		gui_reset_status_bar();
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
	gtk_range_set_value(GTK_RANGE(position_slider), 0);
}

static void menu_item_seek_to_end_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	if (gstreamer_end_of_stream())
		return;
	gint64 duration = gstreamer_get_duration();
	if (duration > 0) {
		// Actually seek to end of stream - 0.1s.
		if (duration - 100000000 >= 0) {
			gdouble value = (gdouble) (duration - 100000000) * 100.0 / duration;
			gtk_range_set_value(GTK_RANGE(position_slider), value);
		}
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
		// When the end of stream is reached, actually seek to end of stream - 0.1s.
		pos = duration - 100000000;
		if (pos < 0)
			pos = 0;
	}
	/* Updating the status bar slider will trigger a seek on the stream. */
	gdouble value = (gdouble) pos * 100.0 / duration;
	gtk_range_set_value(GTK_RANGE(position_slider), value);
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
	gint64 duration = gstreamer_get_duration();
	/* Updating the status bar slider will trigger a seek on the stream. */
	gdouble value = (gdouble) pos * 100.0 / duration;
	gtk_range_set_value(GTK_RANGE(position_slider), value);
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

static void resize_video_window(int width, int height, gboolean draw) {
	// Get the height of the menu bar.
#if GTK_CHECK_VERSION(3, 0, 0)
	int menu_h = gtk_widget_get_allocated_height(menu_bar);
	int status_h = gtk_widget_get_allocated_height(status_bar);
#else
	int menu_h = menu_bar->allocation.height;
	int status_h = status_bar->allocation.height;
#endif
//	gtk_widget_set_default_size(video_window, width, height);
	gtk_window_resize(GTK_WINDOW(window), width, height + menu_h + status_h);

	gboolean result;
	if (draw)
		gtk_widget_queue_draw(video_window);
}

static void menu_item_one_to_one_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	int width, height;
	gstreamer_get_video_dimensions(&width, &height);
	if (width != 0 && height != 0)
		resize_video_window(width, height, TRUE);
}

static void menu_item_zoom_x2_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	int width, height;
	gstreamer_get_video_dimensions(&width, &height);
	if (width != 0 && height != 0)
		resize_video_window(width * 2, height * 2, TRUE);
}


static void menu_item_zoom_x05_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	int width, height;
	gstreamer_get_video_dimensions(&width, &height);
	if (width != 0 && height != 0)
		resize_video_window(width / 2, height / 2, TRUE);
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
	int r = 0;
	if (!gstreamer_no_pipeline())
		r = gstreamer_prepare_color_balance();
	if (r == 0) {
		char s[256];
		sprintf(s, "Color balance control not available with current stream\n"
			"or newer version of GStreamer required.\n");
		if (!config_software_color_balance())
			sprintf(s + strlen(s),
			"\nNote: Software color balance disabled in settings.\n");
		GtkWidget *message_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK, s);
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
	gstreamer_refresh_frame();
}

static void color_balance_set_defaults_button_clicked_cb(GtkButton *button, gpointer data) {
	for (int i = 0; i < 4; i++) {
		gtk_range_set_value(GTK_RANGE(color_slider[i]), 50.0);
	}
}

static void color_balance_set_global_defaults_button_clicked_cb(GtkButton *button,
gpointer data) {
	for (int i = 0; i < 4; i++)
		if (gtk_widget_get_visible(color_slider[i]))
			config_set_global_color_balance_default(i,
				gtk_range_get_value(GTK_RANGE(color_slider[i])));
}

static void color_balance_set_uri_defaults_button_clicked_cb(GtkButton *button,
gpointer data) {
	for (int i = 0; i < 4; i++)
		if (gtk_widget_get_visible(color_slider[i]))
			config_set_uri_color_balance_default(i,
				gtk_range_get_value(GTK_RANGE(color_slider[i])));
}

void color_balance_dialog_response_cb(GtkDialog *dialog, gpointer data) {
	gtk_widget_hide(GTK_WIDGET(dialog));
	color_controls_active = FALSE;
}

#endif

static void menu_item_increase_playback_speed_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_increase_playback_speed();
}

static void menu_item_decrease_playback_speed_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_decrease_playback_speed();
}

static void check_menu_item_reverse_activate_cb(GtkCheckMenuItem *check_menu_item, gpointer data) {
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(check_menu_item)))
		gstreamer_set_playback_speed_reverse(TRUE);
	else
		gstreamer_set_playback_speed_reverse(FALSE);
}

static void menu_item_reset_playback_speed_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_reset_playback_speed();
}

static void menu_item_next_frame_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_pause();
	gstreamer_next_frame();
}

static void menu_item_previous_frame_activate_cb(GtkMenuItem *menu_item, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gstreamer_pause();
	gstreamer_previous_frame();
}

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

// Statistics window.

GtkWidget *cpu_utilization_text_view;
GtkWidget *dropped_frames_text_view;
guint stats_dialog_update_cb_id;

/* Replace the text of a text view, and apply an existing tag to all text. */

static void replace_text_view_text(GtkWidget *text_view, const char *tag_name,
const char *text) {
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	GtkTextIter end_iter;
	gtk_text_buffer_get_end_iter(buffer, &end_iter);
	GtkTextIter start_iter;
	gtk_text_buffer_get_iter_at_offset(buffer, &start_iter, 0);
	gtk_text_buffer_delete(buffer, &start_iter, &end_iter);
	gtk_text_buffer_insert(buffer, &start_iter, text, -1);
	gtk_text_buffer_get_iter_at_offset(buffer, &start_iter, 0);
	gtk_text_buffer_get_end_iter(buffer, &end_iter);
	gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start_iter, &end_iter);
}

static gboolean stats_dialog_update_cb(gpointer data) {
	char *s;
	s = stats_get_cpu_utilization_str();
	replace_text_view_text(cpu_utilization_text_view, "my_font", s);
	g_free(s);
	s = stats_get_dropped_frames_str();
	replace_text_view_text(dropped_frames_text_view, "my_font", s);
	g_free(s);
	return TRUE;
}

static void menu_item_stats_activate_cb(GtkMenuItem *menu_item, GtkDialog *dialog) {
	gtk_widget_show_all(GTK_WIDGET(dialog));
	stats_dialog_update_cb_id = g_timeout_add(200, stats_dialog_update_cb, NULL);
	stats_reset();
	stats_set_enabled(TRUE);
}

static void stats_dialog_response_cb(GtkDialog *dialog, gpointer data) {
	g_source_remove(stats_dialog_update_cb_id);
	gtk_widget_hide(GTK_WIDGET(dialog));
	stats_set_enabled(FALSE);
}

static void stats_reset_button_clicked_cb(GtkButton *button, gpointer data) {
	stats_reset();
}

static void stats_thread_info_check_button_toggled_cb(GtkToggleButton *button, gpointer data ) {
	if (gtk_toggle_button_get_active(button))
		stats_set_thread_info(TRUE);
	else
		stats_set_thread_info(FALSE);
}

// Status bar.

// Number of milliseconds to play for after a scrub seek.
#define SCRUB_TIME 100

static guint position_slider_value_changed_cb_id;
static guint position_slider_value_changed_scrub_seek_cb_id;
static guint seek_timeout_id = 0;
static gboolean state_was_playing;
static gint64 current_seek_target_position;
static gint64 next_seek_target_position;
static gint64 cached_duration;

static void position_slider_value_changed_cb(GtkScale *scale, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gdouble v = gtk_range_get_value(GTK_RANGE(scale));
	gint64 duration = gstreamer_get_duration();
	gint64 pos = (v / 100.0) * duration;
	gstreamer_seek_to_time(pos);
}

void gui_reset_status_bar() {
	g_signal_handlers_block_by_func(position_slider, position_slider_value_changed_cb,
		NULL);
	gtk_range_set_value(GTK_RANGE(position_slider), 0);
	g_signal_handlers_unblock_by_func(position_slider, position_slider_value_changed_cb,
		NULL);
	gtk_label_set_text(GTK_LABEL(status_bar_duration_label),
		gstreamer_get_duration_str());
}

// Handler that is called every second to update the progress slider in the status bar.

gboolean gui_update_status_bar_cb(gpointer data) {
	if (gstreamer_no_pipeline())
		return TRUE;
	// Return when doing scrub seek.
	if (position_slider_value_changed_cb_id == 0)
		return TRUE;
	gint64 duration = gstreamer_get_duration();
	if (duration == 0)
		return TRUE;
	gboolean error;
	gint64 pos = gstreamer_get_position(&error);
	if (error)
		return TRUE;
	gdouble value = (gdouble)pos * 100.0 / duration;
	// Don't trigger a gstreamer seek when the value is changed.
	g_signal_handlers_block_by_func(position_slider, position_slider_value_changed_cb,
		NULL);
	gtk_range_set_value(GTK_RANGE(position_slider), value);
	g_signal_handlers_unblock_by_func(position_slider, position_slider_value_changed_cb,
		NULL);
	return TRUE;
}

static gchar *position_slider_format_value_cb(GtkScale *scale, gdouble value) {
	return g_strdup_printf("%.0lf%%", value);
}

static gboolean end_scrub(gpointer data) {
	if (position_slider_value_changed_cb_id != 0)
		return FALSE;
	seek_timeout_id = 0;
	if (next_seek_target_position == - 1) {
		gstreamer_pause();
		current_seek_target_position = - 1;
	}
	else {
		// If a new seek target is pending, perform a new seek.
		gstreamer_pause();
		gstreamer_seek_to_time(next_seek_target_position);
		current_seek_target_position = next_seek_target_position;	
		next_seek_target_position = -1;
		gstreamer_play();
	}
	return FALSE;
}

void gui_state_change_to_playing_cb() {
	// When doing a scrub seek, play for SCRUB_TIME ms.
	if (position_slider_value_changed_cb_id == 0) {
		if (seek_timeout_id == 0) {
			seek_timeout_id = g_timeout_add(SCRUB_TIME,
				(GSourceFunc)end_scrub, NULL);
		}
	}
}

static void position_slider_value_changed_scrub_seek_cb(GtkScale *scale, gpointer data) {
	if (gstreamer_no_pipeline())
		return;
	gdouble v = gtk_range_get_value(GTK_RANGE(scale));
	if (cached_duration == - 1)
		cached_duration = gstreamer_get_duration();
	gint64 pos = (v / 100.0) * cached_duration;
	// Wait until the current seek operation is finished before updating the
	// seek position.
	if (current_seek_target_position == - 1) {
//		printf("value changed to %lu, seek\n", pos);
		gstreamer_seek_to_time(pos);
		current_seek_target_position = pos;
		gstreamer_play();
	}
	else {
//		printf("value changed to %lu, seek next\n", pos);
		next_seek_target_position = pos;
	}
	// Try to prevent the GUI from taking up most CPU cycles (reduce the frequency of
	// this callback).
	main_thread_yield();
}

static gboolean position_slider_button_press_cb(GtkWidget *scale, GdkEventButton * event,
gpointer data) {
	if (position_slider_value_changed_cb_id > 0) {
		// Disable the signal handler that instantly seeks when the slider value is changed.
		g_signal_handler_disconnect(position_slider, position_slider_value_changed_cb_id);
		position_slider_value_changed_cb_id = 0;
		state_was_playing = gstreamer_state_is_playing();
		gstreamer_pause();
		// Install the scrub seek signal handler.
		position_slider_value_changed_scrub_seek_cb_id = g_signal_connect(
			G_OBJECT(position_slider), "value-changed",
			G_CALLBACK(position_slider_value_changed_scrub_seek_cb), NULL);
		current_seek_target_position = - 1;
		next_seek_target_position = - 1;
		seek_timeout_id = 0;
		cached_duration = - 1;
	}
	return FALSE;
}

static gboolean position_slider_button_release_cb(GtkScale *scale, GdkEventButton * event,
gpointer data) {
	if (position_slider_value_changed_cb_id == 0) {
		// Disable the scrub seek signal handler.
		g_signal_handler_disconnect(position_slider,
			position_slider_value_changed_scrub_seek_cb_id);
		// Reconnect the signal handler that instantly seeks when the slider value is changed.
		position_slider_value_changed_cb_id = g_signal_connect(G_OBJECT(position_slider),
			"value-changed", G_CALLBACK(position_slider_value_changed_cb), NULL);
		if (state_was_playing)
			gstreamer_play();
	}
	return FALSE;
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

static GtkWidget *create_stats_dialog() {
	GtkWidget *dialog = gtk_dialog_new_with_buttons(
		"Performance Statistics",
		GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT,
		NULL);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	cpu_utilization_text_view = gtk_text_view_new();
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cpu_utilization_text_view));
	gtk_text_buffer_create_tag(buffer, "my_font", "family", "monospace", NULL);
 	dropped_frames_text_view = gtk_text_view_new();
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(dropped_frames_text_view));
	gtk_text_buffer_create_tag(buffer, "my_font", "family", "monospace", NULL);
	GtkWidget *thread_info_check_button =
		gtk_check_button_new_with_label("Show thread info");
	g_signal_connect(G_OBJECT(thread_info_check_button), "toggled", G_CALLBACK(
		stats_thread_info_check_button_toggled_cb), NULL);
#if GTK_CHECK_VERSION(3, 0, 0)
	GtkWidget *vbox1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
	GtkWidget *vbox1 = gtk_vbox_new(FALSE, 0);
#endif
	gtk_container_add(GTK_CONTAINER(vbox1), cpu_utilization_text_view);
	gtk_container_add(GTK_CONTAINER(vbox1), thread_info_check_button);
	GtkWidget *space_label = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(vbox1), space_label);
	gtk_container_add(GTK_CONTAINER(vbox1), dropped_frames_text_view);
	gtk_container_add(GTK_CONTAINER(content), vbox1);
	GtkWidget *stats_reset_button = gtk_button_new_with_label("Reset");
	g_signal_connect(G_OBJECT(stats_reset_button), "clicked", G_CALLBACK(
		stats_reset_button_clicked_cb), NULL);
	GtkWidget *action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
	gtk_container_add(GTK_CONTAINER(action_area), stats_reset_button);
	g_signal_connect(dialog, "response", G_CALLBACK(stats_dialog_response_cb),
		dialog);
	return dialog;
}

static void create_menus(GMainLoop *loop) {
	GtkWidget *preferences_dialog = create_preferences_dialog();

#if GTK_CHECK_VERSION(3, 0, 0)
	// Create the color balance dialog
	GtkWidget *color_balance_dialog = create_color_balance_dialog();
#endif

	GtkWidget *stats_dialog = create_stats_dialog();

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

	GtkWidget *menu_item_play = gtk_menu_item_new_with_label("Play (Enter)");
	g_signal_connect(G_OBJECT(menu_item_play), "activate",
		G_CALLBACK(menu_item_play_activate_cb), NULL);
	GtkWidget *menu_item_pause = gtk_menu_item_new_with_label("Pause (P)");
	g_signal_connect(G_OBJECT(menu_item_pause), "activate",
		G_CALLBACK(menu_item_pause_activate_cb), NULL);
	GtkWidget *menu_item_rewind = gtk_menu_item_new_with_label("Rewind (Home)");
	g_signal_connect(G_OBJECT(menu_item_rewind), "activate",
		G_CALLBACK(menu_item_rewind_activate_cb), loop);
	GtkWidget *menu_item_seek_to_end = gtk_menu_item_new_with_label("Seek to End (End)");
	g_signal_connect(G_OBJECT(menu_item_seek_to_end), "activate",
		G_CALLBACK(menu_item_seek_to_end_activate_cb), loop);
	GtkWidget *menu_item_plus_1min = gtk_menu_item_new_with_label("+ 1 min ([)");
	g_signal_connect(G_OBJECT(menu_item_plus_1min), "activate",
		G_CALLBACK(menu_item_plus_1min_activate_cb), loop);
	GtkWidget *menu_item_min_1min = gtk_menu_item_new_with_label("- 1 min (])");
	g_signal_connect(G_OBJECT(menu_item_min_1min), "activate",
		G_CALLBACK(menu_item_min_1min_activate_cb), loop);
	GtkWidget *menu_item_plus_10sec = gtk_menu_item_new_with_label("+ 10 sec (>)");
	g_signal_connect(G_OBJECT(menu_item_plus_10sec), "activate",
		G_CALLBACK(menu_item_plus_10sec_activate_cb), loop);
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

	GtkWidget *menu_item_increase_playback_speed =
		gtk_menu_item_new_with_label("Increase playback speed");
	g_signal_connect(G_OBJECT(menu_item_increase_playback_speed), "activate",
		G_CALLBACK(menu_item_increase_playback_speed_activate_cb), NULL);
	GtkWidget *menu_item_decrease_playback_speed =
		gtk_menu_item_new_with_label("Decrease playback speed");
	g_signal_connect(G_OBJECT(menu_item_decrease_playback_speed), "activate",
		G_CALLBACK(menu_item_decrease_playback_speed_activate_cb), NULL);
#if 0
	GtkWidget *check_menu_item_reverse = gtk_check_menu_item_new_with_label("Reverse");
	g_signal_connect(G_OBJECT(check_menu_item_reverse), "activate",
		G_CALLBACK(check_menu_item_reverse_activate_cb), NULL);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check_menu_item_reverse), FALSE);
#endif
	GtkWidget *menu_item_reset_playback_speed =
		gtk_menu_item_new_with_label("Reset playback speed");
	g_signal_connect(G_OBJECT(menu_item_reset_playback_speed), "activate",
		G_CALLBACK(menu_item_reset_playback_speed_activate_cb), NULL);
	GtkWidget *menu_item_next_frame = gtk_menu_item_new_with_label("Next frame (->)");
	g_signal_connect(G_OBJECT(menu_item_next_frame), "activate",
		G_CALLBACK(menu_item_next_frame_activate_cb), NULL);
	GtkWidget *menu_item_previous_frame = gtk_menu_item_new_with_label("Previous frame (<-)");
	g_signal_connect(G_OBJECT(menu_item_previous_frame), "activate",
		G_CALLBACK(menu_item_previous_frame_activate_cb), NULL);
	GtkWidget *trick_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(trick_menu), menu_item_increase_playback_speed);
	gtk_menu_shell_append(GTK_MENU_SHELL(trick_menu), menu_item_decrease_playback_speed);
//	gtk_menu_shell_append(GTK_MENU_SHELL(trick_menu), check_menu_item_reverse);
	gtk_menu_shell_append(GTK_MENU_SHELL(trick_menu), menu_item_reset_playback_speed);
	gtk_menu_shell_append(GTK_MENU_SHELL(trick_menu), menu_item_next_frame);
	gtk_menu_shell_append(GTK_MENU_SHELL(trick_menu), menu_item_previous_frame);
	// Create the trick menu.
	GtkWidget *trick_item = gtk_menu_item_new_with_label("Trick");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(trick_item), trick_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), trick_item);

	GtkWidget *menu_item_stats = gtk_menu_item_new_with_label("Open performance statistics");
	g_signal_connect(G_OBJECT(menu_item_stats), "activate",
		G_CALLBACK(menu_item_stats_activate_cb), stats_dialog);
	GtkWidget *stats_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(stats_menu), menu_item_stats);
	// Create the stats menu.
	GtkWidget *stats_item = gtk_menu_item_new_with_label("Statistics");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(stats_item), stats_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), stats_item);

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

static void create_status_bar() {
#if GTK_CHECK_VERSION(3, 0, 0)
	status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	position_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0,
		100.0, 1.0);
	gtk_widget_set_hexpand(position_slider, TRUE);
#else
	status_bar = gtk_hbox_new(FALSE, 0);
	position_slider = gtk_hscale_new_with_range(0,
		100.0, 1.0);
#endif
	g_object_set(status_bar, "spacing", 8, NULL);
	gtk_scale_set_draw_value(GTK_SCALE(position_slider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(position_slider), GTK_POS_LEFT);
	gtk_scale_set_digits(GTK_SCALE(position_slider), 3);
	gtk_range_set_value(GTK_RANGE(position_slider), 0.0);
	g_signal_connect(G_OBJECT(position_slider), "format-value", G_CALLBACK(
		position_slider_format_value_cb), NULL);
	g_signal_connect(G_OBJECT(position_slider), "button-press-event", G_CALLBACK(
		position_slider_button_press_cb), NULL);
	g_signal_connect(G_OBJECT(position_slider), "button-release-event", G_CALLBACK(
		position_slider_button_release_cb), NULL);
	position_slider_value_changed_cb_id = g_signal_connect(G_OBJECT(position_slider),
		"value-changed", G_CALLBACK(position_slider_value_changed_cb), NULL);
	status_bar_duration_label = gtk_label_new("");
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_container_add(GTK_CONTAINER(status_bar), position_slider);
	gtk_container_add(GTK_CONTAINER(status_bar), status_bar_duration_label);
#else
	gtk_box_pack_start(GTK_BOX(status_bar), position_slider, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(status_bar), status_bar_duration_label, FALSE, FALSE, 0);
#endif
}

void gui_setup_window(GMainLoop *loop, const char *video_filename, int width, int height,
gboolean full_screen_requested) {

	/* Set up the window. */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	char *title = g_strdup_printf(title, "gstplay %s", video_filename);
	gui_set_window_title(title);
	g_free(title);
#if !GTK_CHECK_VERSION(3, 0, 0) && 0
	// For GTK+ 3, disabling double buffering causes corruption in the menu bar,
	// so leave it enabled.
	// For GTK+ 2, disabling double buffering causes flickering in the status bar.
	gtk_widget_set_double_buffered(window, FALSE);
#endif
	g_signal_connect(G_OBJECT(window), "key-press-event",
		G_CALLBACK(key_press_cb), loop);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy_cb), loop);

	create_menus(loop);
	create_status_bar();

	video_window = gtk_drawing_area_new();
	g_signal_connect(G_OBJECT(video_window), "realize",
		G_CALLBACK(video_widget_realize_cb), NULL);
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
	// Add the menu, video window, and status bar to the vbox.
	gtk_box_pack_start(GTK_BOX(menu_vbox), menu_bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(menu_vbox), video_window, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(menu_vbox), status_bar, FALSE, FALSE, 0);
	// Add the vbox to the window.
	gtk_container_add(GTK_CONTAINER(window), menu_vbox);

	// Create the open file load dialog.
	open_file_dialog = gtk_file_chooser_dialog_new("Select a video file to open.",
		GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
		NULL);

	/* Set the default size. */
	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	gtk_widget_show_all(window);
	resize_video_window(width, height, FALSE);
#if GTK_CHECK_VERSION(3, 0, 0)
	GdkRGBA color;
	gdk_rgba_parse(&color, "black");
	gtk_widget_override_background_color(video_window, GTK_STATE_NORMAL, &color);
	gtk_widget_override_color(video_window, GTK_STATE_NORMAL, &color);
#else
	GdkColor color;
	gdk_color_parse("black", &color);
	gtk_widget_modify_bg(video_window, GTK_STATE_NORMAL, &color);
#endif
	gtk_widget_show_all(window);
	full_screen = 0;
	if (full_screen_requested) {
		enable_full_screen();
	}
}


