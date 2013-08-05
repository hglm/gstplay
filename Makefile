
# Set GTK_MAJOR to 2 or 3 and GST_VERSION to 1.0 or 0.10
GTK_MAJOR=3
GST_VERSION=1.0

CFLAGS = -std=gnu99 -O2
GLIB_PKG_CONFIG_CFLAGS=`pkg-config --cflags glib-2.0`
ifeq ($(GST_VERSION), 0.10)
GST_PKG_CONFIG_LIBS_EXTRA= --libs gstreamer-interfaces-0.10
else
GST_PKG_CONFIG_LIBS_EXTRA=
endif
GST_PKG_CONFIG_CFLAGS=`pkg-config --cflags gstreamer-$(GST_VERSION)`
GST_PKG_CONFIG_LFLAGS=`pkg-config --libs gstreamer-$(GST_VERSION) --libs \
gstreamer-video-$(GST_VERSION) $(GST_PKG_CONFIG_LIBS_EXTRA)`
GTK_PKG_CONFIG_CFLAGS=`pkg-config --cflags gtk+-$(GTK_MAJOR).0`
GTK_PKG_CONFIG_LFLAGS=`pkg-config --libs gtk+-$(GTK_MAJOR).0`

MODULE_OBJECTS = main.o gui.o gstreamer.o config.o stats.o

gstplay : $(MODULE_OBJECTS)
	gcc -O -o gstplay $(MODULE_OBJECTS) $(GTK_PKG_CONFIG_LFLAGS) $(GST_PKG_CONFIG_LFLAGS)

gui.o : gui.c
	$(CC) -c $(CFLAGS) $(GTK_PKG_CONFIG_CFLAGS) $< -o $@

gstreamer.o : gstreamer.c
	$(CC) -c $(CFLAGS) $(GST_PKG_CONFIG_CFLAGS) $< -o $@

main.o : main.c
	$(CC) -c $(CFLAGS) $(GST_PKG_CONFIG_CFLAGS) $< -o $@

.c.o : 
	$(CC) -c $(CFLAGS) $(GLIB_PKG_CONFIG_CFLAGS) $< -o $@

clean :
	rm -f gstplay $(MODULE_OBJECTS)
