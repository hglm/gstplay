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

typedef enum { STARTUP_PLAYING, STARTUP_PAUSED } StartupState;

/* main.c */

extern const char *main_create_pipeline(const char *uri, const char *video_title_filename);
extern void main_create_uri(const char *filespec, char **uri, char **video_title_filename);
extern void main_get_current_uri(const char **uri, const char **video_title_filename);
extern GMainLoop *main_get_main_loop();
extern void main_set_video_sink(const char *_video_sink);
extern void main_set_audio_sink(const char *_audio_sink);

/* config.c. */

extern void config_init();
extern StartupState config_get_startup_preference();
extern void config_set_current_video_sink(const char *_video_sink);
extern void config_set_current_audio_sink(const char *_audio_sink);
extern void config_set_current_video_sink_by_index(int i);
extern void config_set_current_audio_sink_by_index(int i);
extern const char *config_get_current_video_sink();
extern const char *config_get_current_audio_sink();
extern int config_get_number_of_video_sinks();
extern int config_get_number_of_audio_sinks();
extern const char *config_get_video_sink_by_index(int i);
extern const char *config_get_audio_sink_by_index(int i);
extern int config_get_current_video_sink_index();
extern int config_get_current_audio_sink_index();
extern void config_set_video_only(gboolean state);
extern gboolean config_video_only();
extern void config_set_quit_on_stream_end(gboolean status);
extern gboolean config_quit_on_stream_end();
extern void config_set_software_volume(gboolean status);
extern gboolean config_software_volume();
extern void config_set_software_color_balance(gboolean status);
extern gboolean config_software_color_balance();

/* gui.c */

extern void gui_init(int *argcp, char **argvp[]);
extern void gui_setup_window(GMainLoop *loop, const char *video_filename, int video_width,
	int video_height, gboolean full_screen);
extern guintptr gui_get_video_window_handle();
extern void gui_get_version(guint *major, guint *minor, guint *micro);

/* gstreamer.c */

extern void gstreamer_init(int *argcp, char **argvp[]);
extern void gstreamer_get_version(guint *major, guint *minor, guint *micro);
extern void gstreamer_get_compiled_version(guint *major, guint *minor, guint *micro);
extern gboolean gstreamer_have_software_color_balance();
extern void gstreamer_determine_video_dimensions(const char *uri, int *video_width, int *video_height);
extern void gstreamer_expose_video_overlay();
extern void gstreamer_run_pipeline(GMainLoop *loop, const char *s, StartupState startup);
extern void gstreamer_destroy_pipeline();
extern void gstreamer_get_video_dimensions(int *width, int *height);
extern void gstreamer_get_video_info(const char **format, int *width, int *height,
int *framerate_numeratorp, int *framerate_denomp, int *pixel_aspect_ratio_nump,
int *pixel_aspect_ratio_denomp);
extern const char *gstreamer_get_pipeline_description();
extern void gstreamer_play();
extern void gstreamer_pause();
extern gint64 gstreamer_get_position(gboolean *error);
extern void gstreamer_seek_to_time(gint64 time_nanoseconds);
extern gint64 gstreamer_get_duration();
extern const char *gstreamer_get_duration_str();
extern gboolean gstreamer_end_of_stream();
extern gboolean gstreamer_no_pipeline();
/* Save the position of the video stream and wind down the pipeline. */
extern void gstreamer_suspend_pipeline();
/* Reconstruct the last-run pipeline and seek to the saved position. */
extern void gstreamer_restart_pipeline();
extern void gstreamer_set_volume(gdouble volume);
extern gdouble gstreamer_get_volume();
extern void gstreamer_inform_playbin_used(gboolean status);
